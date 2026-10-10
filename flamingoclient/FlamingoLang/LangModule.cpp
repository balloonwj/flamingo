/**
 *  语言包模块与导出函数表的实现，见 LangModule.h。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "LangModule.h"

#include <string.h>

#include "PoParser.h"
#include "ScopedLock.h"
#include "resource.h"

namespace flamingolang {

namespace {

/**
 *  语言包里的语言，顺序即语言选择菜单里的顺序。简体中文是代码里的原文，不在此列。
 *  候选字体按优先顺序排列，宿主取第一个已安装的（理由与实测见 docs/多语言方案.md 第 8.5 节）：
 *  Yu Gothic UI 据推测从 Windows 8.1 起才自带，排在其后的 Meiryo UI 用于 Windows 7。
 */
const LangInfo kLanguages[] =
{
    { (uint32_t)sizeof(LangInfo), L"zh-TW", L"繁體中文", 3,
      { L"Microsoft JhengHei", L"PMingLiU", L"Microsoft YaHei" } },
    { (uint32_t)sizeof(LangInfo), L"en-US", L"English", 2,
      { L"Segoe UI", L"Microsoft YaHei" } },
    { (uint32_t)sizeof(LangInfo), L"ja-JP", L"日本語", 5,
      { L"Yu Gothic UI", L"Meiryo UI", L"Meiryo", L"MS UI Gothic", L"Microsoft YaHei" } },
    { (uint32_t)sizeof(LangInfo), L"ko-KR", L"한국어", 3,
      { L"Malgun Gothic", L"Gulim", L"Microsoft YaHei" } },
};

//kLanguages 中各语言的 .po 资源编号，下标一一对应
const int kLanguageResourceIds[] = { IDR_PO_ZH_TW, IDR_PO_EN_US, IDR_PO_JA_JP, IDR_PO_KO_KR };

const uint32_t kLanguageCount = (uint32_t)(sizeof(kLanguages) / sizeof(kLanguages[0]));

static_assert(sizeof(kLanguageResourceIds) / sizeof(kLanguageResourceIds[0]) == sizeof(kLanguages) / sizeof(kLanguages[0]),
              "kLanguageResourceIds must match kLanguages one to one");

//判断 LangStats 中某个字段是否落在调用方声明的结构大小之内
inline bool StatsHasField(const LangStats* stats, size_t fieldEndOffs)
{
    return stats->m_cbSize >= fieldEndOffs;
}

// ---- 导出函数表里的函数：全部转发给 CLangModule::Instance()，并保证异常不越过模块边界 ----

const wchar_t* __cdecl ApiGetPackVersion()
{
    return CLangModule::Instance().GetPackVersion();
}

uint32_t __cdecl ApiGetLanguageCount()
{
    return CLangModule::Instance().GetLanguageCount();
}

const LangInfo* __cdecl ApiGetLanguageInfo(uint32_t index)
{
    return CLangModule::Instance().GetLanguageInfo(index);
}

int __cdecl ApiSelectLanguage(const wchar_t* code)
{
    try
    {
        return CLangModule::Instance().SelectLanguage(code);
    }
    catch (...)
    {
        //解析时内存不足等：当作取不到数据，当前选定的语言保持不变
        return lang_select_no_data;
    }
}

const wchar_t* __cdecl ApiLookup(const wchar_t* context, const wchar_t* source)
{
    try
    {
        return CLangModule::Instance().Lookup(context, source);
    }
    catch (...)
    {
        //伪语言生成译文时内存不足：返回 NULL，宿主显示原文
        return NULL;
    }
}

bool __cdecl ApiGetStats(LangStats* stats)
{
    return CLangModule::Instance().GetStats(stats);
}

//导出给宿主的函数表
const LangApi kApiTable =
{
    (uint32_t)sizeof(LangApi),
    ApiGetPackVersion,
    ApiGetLanguageCount,
    ApiGetLanguageInfo,
    ApiSelectLanguage,
    ApiLookup,
    ApiGetStats,
};

} // namespace

CLangModule& CLangModule::Instance()
{
    static CLangModule s_instance;
    return s_instance;
}

CLangModule::CLangModule()
    : m_poSource(NULL),
      m_current(NULL)
{
    ::InitializeCriticalSection(&m_lock);
}

CLangModule::~CLangModule()
{
    ::DeleteCriticalSection(&m_lock);
}

void CLangModule::SetPoSource(PoSourceFn fn)
{
    CScopedLock guard(&m_lock);
    m_poSource = fn;
}

void CLangModule::ResetForTest()
{
    CScopedLock guard(&m_lock);
    ::InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(&m_current), NULL);
    m_loaded.clear();
    m_pseudo.Clear();
    m_poSource = NULL;
}

const wchar_t* CLangModule::GetPackVersion() const
{
    return FLAMINGO_LANG_PACK_VERSION;
}

uint32_t CLangModule::GetLanguageCount() const
{
    return kLanguageCount;
}

const LangInfo* CLangModule::GetLanguageInfo(uint32_t index) const
{
    if (index >= kLanguageCount)
        return NULL;
    return &kLanguages[index];
}

int CLangModule::FindLanguage(const wchar_t* code)
{
    for (uint32_t i = 0; i < kLanguageCount; ++i)
    {
        //语言代码按 BCP 47 的约定不区分大小写
        if (_wcsicmp(kLanguages[i].m_code, code) == 0)
            return (int)i;
    }
    return -1;
}

int CLangModule::SelectLanguage(const wchar_t* code)
{
    if (code == NULL || code[0] == L'\0')
        return lang_select_bad_argument;

    bool isPseudo = (_wcsicmp(code, FLAMINGO_LANG_PSEUDO_CODE) == 0);
    int index = isPseudo ? -1 : FindLanguage(code);
    if (!isPseudo && index < 0)
        return lang_select_unknown;

    //一律以登记表里的写法作为键，"en-us" 与 "en-US" 对应同一份已解析的数据
    std::wstring key = isPseudo ? std::wstring(FLAMINGO_LANG_PSEUDO_CODE) : std::wstring(kLanguages[index].m_code);

    CScopedLock guard(&m_lock);
    LoadedLang* lang = NULL;
    std::map<std::wstring, std::unique_ptr<LoadedLang> >::iterator it = m_loaded.find(key);
    if (it != m_loaded.end())
    {
        lang = it->second.get();
    }
    else
    {
        std::unique_ptr<LoadedLang> loaded(new LoadedLang());
        loaded->m_code = key;
        loaded->m_isPseudo = isPseudo;
        loaded->m_errors = 0;
        if (!isPseudo)
        {
            const char* data = NULL;
            size_t size = 0;
            if (m_poSource == NULL || !m_poSource(key.c_str(), kLanguageResourceIds[index], &data, &size) || data == NULL)
                return lang_select_no_data;

            PoParseResult parsed;
            ParsePo(data, size, parsed);
            loaded->m_table.reset(new CLangTable());
            loaded->m_table->Build(parsed);
            loaded->m_errors = parsed.m_errors + loaded->m_table->GetErrorCount();
        }
        lang = loaded.get();
        m_loaded[key] = std::move(loaded);
    }

    //整个对象构造完成后再发布；InterlockedExchangePointer 带完整内存屏障，Lookup 读到指针时对象已经可用
    ::InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(&m_current), lang);
    return lang_select_ok;
}

const wchar_t* CLangModule::Lookup(const wchar_t* context, const wchar_t* source)
{
    if (source == NULL)
        return NULL;
    LoadedLang* lang = m_current;
    if (lang == NULL)
        return NULL;
    if (lang->m_isPseudo)
        return m_pseudo.Lookup(context, source);
    return lang->m_table->Lookup(context, source);
}

bool CLangModule::GetStats(LangStats* stats)
{
    if (stats == NULL)
        return false;
    LoadedLang* lang = m_current;
    if (lang == NULL)
        return false;

    uint32_t entries = lang->m_isPseudo ? m_pseudo.GetEntryCount() : lang->m_table->GetEntryCount();
    uint32_t warnings = lang->m_isPseudo ? 0 : lang->m_table->GetWarningCount();
    if (StatsHasField(stats, offsetof(LangStats, m_entries) + sizeof(stats->m_entries)))
        stats->m_entries = entries;
    if (StatsHasField(stats, offsetof(LangStats, m_errors) + sizeof(stats->m_errors)))
        stats->m_errors = lang->m_errors;
    if (StatsHasField(stats, offsetof(LangStats, m_warnings) + sizeof(stats->m_warnings)))
        stats->m_warnings = warnings;
    return true;
}

const LangApi* GetLangApiTable()
{
    return &kApiTable;
}

} // namespace flamingolang
