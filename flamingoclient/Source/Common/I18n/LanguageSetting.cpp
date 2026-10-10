/**
 *  界面语言选择的实现，见 LanguageSetting.h。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "LanguageSetting.h"

#include "I18n.h"
#include "../Util/QuietLoadLibrary.h"   // LoadLibraryQuietly：加载语言包时屏蔽系统错误框

namespace i18n {

namespace {

//简体中文的显示名称（语言自己的写法，不翻译）
const wchar_t* const kSourceLanguageName = L"简体中文";    //i18n-ignore: 语言名称用该语言自己的写法

//读 ini 时的缓冲区字符数（语言代码很短，留足余量）
const DWORD kIniValueChars = 64;

std::wstring TrimBlank(const std::wstring& text)
{
    const wchar_t* blanks = L" \t\r\n";
    size_t begin = text.find_first_not_of(blanks);
    if (begin == std::wstring::npos)
        return std::wstring();
    size_t end = text.find_last_not_of(blanks);
    return text.substr(begin, end - begin + 1);
}

} // namespace

bool ListPackLanguages(const std::wstring& dllPath, std::vector<LanguageChoice>& out)
{
    out.clear();
    //加载后不卸载：与 i18n::Init 加载的是同一个模块（引用计数加一），语言信息的字符串归它所有
    HMODULE module = quietload::LoadLibraryQuietly(dllPath.c_str());
    if (module == NULL)
        return false;
    FlamingoLangGetApiFn getApi = reinterpret_cast<FlamingoLangGetApiFn>(
        ::GetProcAddress(module, FLAMINGO_LANG_ENTRY_NAME));
    if (getApi == NULL)
        return false;
    const flamingolang::LangApi* api = getApi();
    if (api == NULL || api->m_cbSize < flamingolang::kLangApiMinSize || api->m_getLanguageCount == NULL
        || api->m_getLanguageInfo == NULL)
        return false;

    uint32_t count = api->m_getLanguageCount();
    for (uint32_t i = 0; i < count; ++i)
    {
        const flamingolang::LangInfo* info = api->m_getLanguageInfo(i);
        if (info == NULL || info->m_code == NULL || info->m_code[0] == L'\0')
            continue;
        LanguageChoice choice;
        choice.m_code = info->m_code;
        choice.m_name = (info->m_nativeName != NULL && info->m_nativeName[0] != L'\0') ? info->m_nativeName
                                                                                       : info->m_code;
        out.push_back(choice);
    }
    return true;
}

std::vector<LanguageChoice> BuildLanguageChoices(const std::vector<LanguageChoice>& pack)
{
    std::vector<LanguageChoice> choices;
    LanguageChoice follow;
    follow.m_name = TR(L"跟随系统");
    choices.push_back(follow);

    LanguageChoice source;
    source.m_code = kSourceLanguage;
    source.m_name = kSourceLanguageName;
    choices.push_back(source);

    for (size_t i = 0; i < pack.size(); ++i)
    {
        //语言包理论上不含简体中文；万一含了也不重复列出
        if (_wcsicmp(pack[i].m_code.c_str(), kSourceLanguage) != 0)
            choices.push_back(pack[i]);
    }
    return choices;
}

int FindLanguageChoice(const std::vector<LanguageChoice>& choices, const std::wstring& configured)
{
    std::wstring value = TrimBlank(configured);
    for (size_t i = 0; i < choices.size(); ++i)
    {
        if (_wcsicmp(choices[i].m_code.c_str(), value.c_str()) == 0)
            return (int)i;
    }
    return -1;
}

std::wstring GetLanguageDisplayName(const std::vector<LanguageChoice>& pack, const std::wstring& code)
{
    if (_wcsicmp(code.c_str(), kSourceLanguage) == 0)
        return kSourceLanguageName;
    for (size_t i = 0; i < pack.size(); ++i)
    {
        if (_wcsicmp(pack[i].m_code.c_str(), code.c_str()) == 0)
            return pack[i].m_name;
    }
    return code;
}

std::wstring ReadConfiguredLanguage(const std::wstring& iniPath)
{
    wchar_t buf[kIniValueChars] = { 0 };
    ::GetPrivateProfileStringW(kIniSection, kIniKey, L"", buf, kIniValueChars, iniPath.c_str());
    return TrimBlank(buf);
}

bool WriteConfiguredLanguage(const std::wstring& iniPath, const std::wstring& code)
{
    std::wstring value = TrimBlank(code);
    //跟随系统：删除这一项（值传 NULL），而不是写一个空值
    const wchar_t* data = value.empty() ? NULL : value.c_str();
    return ::WritePrivateProfileStringW(kIniSection, kIniKey, data, iniPath.c_str()) != FALSE;
}

bool LanguageChangeNeedsRestart(const std::wstring& newConfigured, LANGID systemUiLang)
{
    std::wstring requested = ResolveRequestedLanguage(newConfigured, systemUiLang, NULL);
    return _wcsicmp(requested.c_str(), GetLanguage().c_str()) != 0;
}

} // namespace i18n
