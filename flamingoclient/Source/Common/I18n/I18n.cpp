/**
 *  界面文字多语言查找的实现，见 I18n.h。
 *
 *  模块状态全部放在本文件的全局变量里：初始化在启动时单线程完成，之后只读，Translate 不加锁。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "I18n.h"

#include <windows.h>
#include "../Util/QuietLoadLibrary.h"   // LoadLibraryQuietly：加载语言包时屏蔽系统错误框
#include <stdio.h>
#include <string.h>

namespace i18n {

const wchar_t* const kSourceLanguage = L"zh-CN";
const wchar_t* const kDefaultUiFont = L"Microsoft YaHei";
const wchar_t* const kIniSection = L"ui";
const wchar_t* const kIniKey = L"language";

namespace {

//繁体中文的子语言：台湾、香港、澳门，以及繁体中性（zh-Hant，LANGID 0x7C04 的子语言部分）
const WORD kTraditionalChineseSublangs[] =
{
    SUBLANG_CHINESE_TRADITIONAL,
    SUBLANG_CHINESE_HONGKONG,
    SUBLANG_CHINESE_MACAU,
    0x1F,
};

//占位符的写法：左花括号 + 一位数字 + 右花括号，共三个字符
const size_t kPlaceholderChars = 3;

//初始化说明里语言代码、字体名等文字的最大字符数，防止异常输入把日志行撑得过长
const size_t kMaxDescribedChars = 64;

//整数转文字时的缓冲区大小（足够放下 64 位整数与符号）
const size_t kIntTextChars = 32;

//是否已经初始化
bool g_initialized = false;

//语言包的函数表；未加载、加载失败或当前为简体中文时为 NULL
const flamingolang::LangApi* volatile g_api = NULL;

//插件 DLL 用 InitFromHost 初始化后，把查找转交给宿主 exe 的函数；exe 自身与当前为简体中文时为 NULL
volatile HostTranslateFn g_hostTranslate = NULL;

//当前生效的语言代码与界面字体
std::wstring g_language = kSourceLanguage;
std::wstring g_uiFont = kDefaultUiFont;

//初始化之前调用 Translate 的次数
volatile LONG g_earlyLookups = 0;

//判断字体是否已安装的函数；NULL 表示查询系统字体
FontInstalledFn g_fontProbe = NULL;

//EnumFontFamiliesExW 的回调：找到任意一个即停止枚举
int CALLBACK FoundFontProc(const LOGFONTW* lf, const TEXTMETRICW* tm, DWORD type, LPARAM lParam)
{
    (void)lf;
    (void)tm;
    (void)type;
    *reinterpret_cast<bool*>(lParam) = true;
    return 0;
}

//查询系统里是否安装了某个字体
bool IsSystemFontInstalled(const wchar_t* face)
{
    if (face == NULL || face[0] == L'\0' || wcslen(face) >= LF_FACESIZE)
        return false;
    LOGFONTW lf;
    memset(&lf, 0, sizeof(lf));
    lf.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(lf.lfFaceName, face, _TRUNCATE);
    bool found = false;
    HDC hdc = ::GetDC(NULL);
    if (hdc == NULL)
        return false;
    ::EnumFontFamiliesExW(hdc, &lf, FoundFontProc, reinterpret_cast<LPARAM>(&found), 0);
    ::ReleaseDC(NULL, hdc);
    return found;
}

bool IsFontInstalled(const wchar_t* face)
{
    return (g_fontProbe != NULL) ? g_fontProbe(face) : IsSystemFontInstalled(face);
}

//去掉首尾空白
std::wstring TrimText(const std::wstring& text)
{
    const wchar_t* blanks = L" \t\r\n";
    size_t begin = text.find_first_not_of(blanks);
    if (begin == std::wstring::npos)
        return std::wstring();
    size_t end = text.find_last_not_of(blanks);
    return text.substr(begin, end - begin + 1);
}

//宽字符转成可打印 ASCII，非 ASCII 字符换成 '?'，超长时截断。只用于写日志
std::string ToAsciiForLog(const std::wstring& text)
{
    std::string out;
    for (size_t i = 0; i < text.size() && i < kMaxDescribedChars; ++i)
    {
        wchar_t c = text[i];
        out.push_back((c >= 0x20 && c <= 0x7E) ? (char)c : '?');
    }
    return out;
}

//在语言包里找某种语言的信息（不区分大小写），找不到返回 NULL
const flamingolang::LangInfo* FindLanguageInfo(const flamingolang::LangApi* api, const std::wstring& code)
{
    uint32_t count = api->m_getLanguageCount();
    for (uint32_t i = 0; i < count; ++i)
    {
        const flamingolang::LangInfo* info = api->m_getLanguageInfo(i);
        if (info != NULL && info->m_code != NULL && _wcsicmp(info->m_code, code.c_str()) == 0)
            return info;
    }
    return NULL;
}

//从候选列表里取第一个已安装的字体，都没装时返回 kDefaultUiFont
std::wstring PickUiFont(const flamingolang::LangInfo* info)
{
    if (info == NULL)
        return kDefaultUiFont;
    uint32_t count = (info->m_fontCount <= flamingolang::kMaxFontCandidates) ? info->m_fontCount
                                                                            : flamingolang::kMaxFontCandidates;
    for (uint32_t i = 0; i < count; ++i)
    {
        if (info->m_fonts[i] != NULL && IsFontInstalled(info->m_fonts[i]))
            return info->m_fonts[i];
    }
    return kDefaultUiFont;
}

//把模块状态置为「使用简体中文」
void UseSourceLanguage()
{
    g_api = NULL;
    g_hostTranslate = NULL;
    g_language = kSourceLanguage;
    g_uiFont = kDefaultUiFont;
}

/**
 *  在已取得函数表的前提下完成初始化：校验函数表、选定语言、取统计信息与界面字体。
 *  result 里的 m_requested / m_followSystem 已由调用方填好。
 */
void InitWithApi(const flamingolang::LangApi* api, InitResult& result)
{
    if (api == NULL)
    {
        result.m_status = init_dll_missing;
        UseSourceLanguage();
        return;
    }
    if (api->m_cbSize < flamingolang::kLangApiMinSize || api->m_getPackVersion == NULL
        || api->m_getLanguageCount == NULL || api->m_getLanguageInfo == NULL || api->m_selectLanguage == NULL
        || api->m_lookup == NULL || api->m_getStats == NULL)
    {
        result.m_status = init_dll_invalid;
        UseSourceLanguage();
        return;
    }

    const wchar_t* version = api->m_getPackVersion();
    result.m_packVersion = (version != NULL) ? version : L"";

    if (api->m_selectLanguage(result.m_requested.c_str()) != flamingolang::lang_select_ok)
    {
        result.m_status = init_language_unavailable;
        UseSourceLanguage();
        return;
    }

    //语言代码以语言包登记的写法为准；伪语言不在登记表里，沿用请求的写法
    const flamingolang::LangInfo* info = FindLanguageInfo(api, result.m_requested);
    g_language = (info != NULL && info->m_code != NULL) ? std::wstring(info->m_code) : result.m_requested;
    g_uiFont = PickUiFont(info);

    flamingolang::LangStats stats;
    memset(&stats, 0, sizeof(stats));
    stats.m_cbSize = sizeof(stats);
    if (api->m_getStats(&stats))
    {
        result.m_entries = stats.m_entries;
        result.m_errors = stats.m_errors;
        result.m_warnings = stats.m_warnings;
    }

    //状态全部就绪后再发布函数表，其它线程的 Translate 读到非 NULL 时语言已经选定。
    //InterlockedExchangePointer 只接受 PVOID volatile*：先用 const_cast 去掉指向对象的 const，再转成 PVOID
    flamingolang::LangApi* volatile* slot = const_cast<flamingolang::LangApi* volatile*>(&g_api);
    ::InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(slot), const_cast<flamingolang::LangApi*>(api));
    result.m_status = init_ok;
}

//初始化的公共前半段：决定期望的语言；返回 true 表示需要语言包
bool BeginInit(const std::wstring& configured, LANGID systemUiLang, InitResult& result)
{
    UseSourceLanguage();
    result.m_requested = ResolveRequestedLanguage(configured, systemUiLang, &result.m_followSystem);
    g_initialized = true;
    if (_wcsicmp(result.m_requested.c_str(), kSourceLanguage) == 0)
    {
        result.m_status = init_source_language;
        return false;
    }
    return true;
}

//初始化的公共后半段：把最终状态写回结果
void EndInit(InitResult& result)
{
    result.m_language = g_language;
    result.m_uiFont = g_uiFont;
}

//初始化之前的查找：计数，Debug 构建下输出一行调试信息
void NoteEarlyLookup(const wchar_t* source)
{
    ::InterlockedIncrement(&g_earlyLookups);
#ifdef _DEBUG
    ::OutputDebugStringW(L"i18n: TR called before i18n::Init: ");
    ::OutputDebugStringW(source);
    ::OutputDebugStringW(L"\n");
#else
    (void)source;
#endif
}

} // namespace

std::wstring MapSystemLanguage(LANGID langId)
{
    WORD primary = PRIMARYLANGID(langId);
    WORD sub = SUBLANGID(langId);
    switch (primary)
    {
    //中文：繁体的几个地区用繁体中文，其余（中国、新加坡、简体中性）用简体中文
    case LANG_CHINESE:
        for (size_t i = 0; i < sizeof(kTraditionalChineseSublangs) / sizeof(kTraditionalChineseSublangs[0]); ++i)
        {
            if (sub == kTraditionalChineseSublangs[i])
                return L"zh-TW";
        }
        return kSourceLanguage;
    //任何地区的英文都用美式英文
    case LANG_ENGLISH:
        return L"en-US";
    //日文
    case LANG_JAPANESE:
        return L"ja-JP";
    //韩文
    case LANG_KOREAN:
        return L"ko-KR";
    //语言包没有的语言：用简体中文
    default:
        return kSourceLanguage;
    }
}

std::wstring ResolveRequestedLanguage(const std::wstring& configured, LANGID systemUiLang, bool* followSystem)
{
    std::wstring value = TrimText(configured);
    bool follow = value.empty();
    if (followSystem != NULL)
        *followSystem = follow;
    if (follow)
        value = MapSystemLanguage(systemUiLang);
    if (_wcsicmp(value.c_str(), kSourceLanguage) == 0)
        return kSourceLanguage;
    return value;
}

InitResult Init(const std::wstring& configured, const std::wstring& dllPath, LANGID systemUiLang)
{
    InitResult result;
    if (BeginInit(configured, systemUiLang, result))
    {
        const flamingolang::LangApi* api = NULL;
        //加载后不卸载：Translate 返回的指针指向语言包的内存。语言包损坏时不让系统弹框卡住启动（QuietLoadLibrary.h）
        HMODULE module = quietload::LoadLibraryQuietly(dllPath.c_str());
        if (module == NULL)
        {
            result.m_loadError = ::GetLastError();
            result.m_status = init_dll_missing;
            UseSourceLanguage();
        }
        else
        {
            FlamingoLangGetApiFn getApi = reinterpret_cast<FlamingoLangGetApiFn>(
                ::GetProcAddress(module, FLAMINGO_LANG_ENTRY_NAME));
            if (getApi == NULL)
            {
                result.m_status = init_dll_invalid;
                UseSourceLanguage();
            }
            else
            {
                api = getApi();
                InitWithApi(api, result);
                //导出函数返回了 NULL：语言包损坏，按无效处理
                if (api == NULL)
                    result.m_status = init_dll_invalid;
            }
        }
    }
    EndInit(result);
    return result;
}

InitResult InitWithApiForTest(const std::wstring& configured, const flamingolang::LangApi* api, LANGID systemUiLang)
{
    InitResult result;
    if (BeginInit(configured, systemUiLang, result))
        InitWithApi(api, result);
    EndInit(result);
    return result;
}

std::string DescribeInitResult(const InitResult& result)
{
    std::string requested = ToAsciiForLog(result.m_requested) + (result.m_followSystem ? " (system)" : " (configured)");
    std::string text = "i18n: ";
    char buf[256];
    switch (result.m_status)
    {
    //期望的就是简体中文
    case init_source_language:
        text += "language " + requested + ", built-in source text, no language pack needed";
        break;
    //语言包加载并选定成功
    case init_ok:
        sprintf_s(buf, "%u entries, %u errors, %u warnings", result.m_entries, result.m_errors, result.m_warnings);
        text += "language " + ToAsciiForLog(result.m_language) + ", requested " + requested + ", pack "
                + ToAsciiForLog(result.m_packVersion) + ", " + buf;
        break;
    //语言包不存在或无法加载
    case init_dll_missing:
        sprintf_s(buf, "error %lu", result.m_loadError);
        if (result.m_loadError == ERROR_MOD_NOT_FOUND || result.m_loadError == ERROR_FILE_NOT_FOUND
            || result.m_loadError == ERROR_PATH_NOT_FOUND || result.m_loadError == 0)
            text += "requested " + requested + ", language pack not found (" + buf + "), falling back to zh-CN";
        else
            text += "requested " + requested + ", language pack cannot be loaded (" + buf + "), falling back to zh-CN";
        break;
    //语言包没有导出函数或函数表不完整
    case init_dll_invalid:
        text += "requested " + requested + ", language pack is invalid (missing entry or function table too short), "
                "falling back to zh-CN";
        break;
    //语言包里没有这种语言
    case init_language_unavailable:
        text += "requested " + requested + " is not available in language pack " + ToAsciiForLog(result.m_packVersion)
                + ", falling back to zh-CN";
        break;
    //不会出现的取值
    default:
        text += "unknown status";
        break;
    }
    text += ", ui font " + ToAsciiForLog(result.m_uiFont) + ".";
    return text;
}

const wchar_t* Translate(const wchar_t* source)
{
    return Translate(NULL, source);
}

const wchar_t* Translate(const wchar_t* context, const wchar_t* source)
{
    if (source == NULL)
        return NULL;
    if (!g_initialized)
    {
        NoteEarlyLookup(source);
        return source;
    }
    const flamingolang::LangApi* api = g_api;
    if (api != NULL)
    {
        const wchar_t* text = api->m_lookup(context, source);
        return (text != NULL) ? text : source;
    }
    //插件 DLL：转交给宿主 exe 的语言模块
    HostTranslateFn host = g_hostTranslate;
    if (host != NULL)
    {
        const wchar_t* text = host(context, source);
        return (text != NULL) ? text : source;
    }
    return source;
}

//UTF-8 转宽字符串；source 为空串或转换失败时返回空串
static std::wstring Utf8ToWideForLookup(const char* source)
{
    int wideLen = ::MultiByteToWideChar(CP_UTF8, 0, source, -1, NULL, 0);
    if (wideLen <= 1)
        return std::wstring();
    std::vector<wchar_t> wide((size_t)wideLen, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, source, -1, &wide[0], wideLen);
    return std::wstring(&wide[0]);
}

//查到的宽字符译文转回 UTF-8；转换失败时返回 fallback 的副本
static std::string WideToUtf8ForLookup(const wchar_t* text, const char* fallback)
{
    int utf8Len = ::WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    if (utf8Len <= 1)
        return std::string(fallback);
    std::string out((size_t)(utf8Len - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text, -1, &out[0], utf8Len, NULL, NULL);
    return out;
}

std::string TranslateUtf8(const char* source)
{
    return TranslateUtf8(NULL, source);
}

std::string TranslateUtf8(const char* context, const char* source)
{
    if (source == NULL)
        return std::string();
    //当前为简体中文时不做任何转换，直接返回原文
    if (g_initialized && IsSourceLanguage())
        return std::string(source);

    const std::wstring wideSource = Utf8ToWideForLookup(source);
    if (wideSource.empty())
        return std::string(source);
    const std::wstring wideContext = (context != NULL) ? Utf8ToWideForLookup(context) : std::wstring();

    const wchar_t* text = (context != NULL) ? Translate(wideContext.c_str(), wideSource.c_str())
                                            : Translate(wideSource.c_str());
    //Translate 没有译文时返回传入的指针本身
    if (text == wideSource.c_str())
        return std::string(source);
    return WideToUtf8ForLookup(text, source);
}

std::wstring GetLanguage()
{
    return g_language;
}

bool IsSourceLanguage()
{
    return g_api == NULL && g_hostTranslate == NULL;
}

void InitFromHost(HostTranslateFn fn, const std::wstring& language, const std::wstring& uiFont)
{
    UseSourceLanguage();
    g_initialized = true;
    if (!uiFont.empty())
        g_uiFont = uiFont;
    //宿主为简体中文（或没有给出查找函数）时不转交，TR 直接返回原文
    if (fn == NULL || TrimText(language).empty() || _wcsicmp(language.c_str(), kSourceLanguage) == 0)
        return;
    g_language = language;
    //状态全部就绪后再发布查找函数，其它线程的 Translate 读到非 NULL 时语言与字体已经确定
    g_hostTranslate = fn;
}

std::wstring GetUiFontFace()
{
    return g_uiFont;
}

const wchar_t* UiFontName()
{
    return g_uiFont.c_str();
}

uint32_t GetEarlyLookupCount()
{
    return (uint32_t)g_earlyLookups;
}

//FormatPattern 与 FormatPatternUtf8 共用的代入逻辑。Ch 为 wchar_t 或 char；占位符只含 ASCII 字符，
//两种编码下都能逐个字符比较
template <typename Ch>
static std::basic_string<Ch> FormatPatternImpl(const Ch* pattern, size_t len,
                                               const std::vector<std::basic_string<Ch> >& args)
{
    std::basic_string<Ch> out;
    out.reserve(len);
    size_t i = 0;
    while (i < len)
    {
        Ch c = pattern[i];
        if (c == (Ch)'{' && i + kPlaceholderChars <= len && pattern[i + 1] >= (Ch)'0' && pattern[i + 1] <= (Ch)'9'
            && pattern[i + 2] == (Ch)'}')
        {
            size_t index = (size_t)(pattern[i + 1] - (Ch)'0');
            if (index < args.size())
            {
                out += args[index];
                i += kPlaceholderChars;
                continue;
            }
        }
        out.push_back(c);
        ++i;
    }
    return out;
}

std::wstring FormatPattern(const wchar_t* pattern, const std::vector<std::wstring>& args)
{
    if (pattern == NULL)
        return std::wstring();
    return FormatPatternImpl<wchar_t>(pattern, wcslen(pattern), args);
}

std::string FormatPatternUtf8(const std::string& pattern, const std::vector<std::string>& args)
{
    return FormatPatternImpl<char>(pattern.c_str(), pattern.size(), args);
}

std::wstring ToFormatArg(int value)
{
    return ToFormatArg((long long)value);
}

std::wstring ToFormatArg(unsigned int value)
{
    return ToFormatArg((unsigned long long)value);
}

std::wstring ToFormatArg(long value)
{
    return ToFormatArg((long long)value);
}

std::wstring ToFormatArg(unsigned long value)
{
    return ToFormatArg((unsigned long long)value);
}

std::wstring ToFormatArg(long long value)
{
    wchar_t buf[kIntTextChars];
    swprintf_s(buf, L"%lld", value);
    return buf;
}

std::wstring ToFormatArg(unsigned long long value)
{
    wchar_t buf[kIntTextChars];
    swprintf_s(buf, L"%llu", value);
    return buf;
}

std::wstring ToFormatArg(const wchar_t* value)
{
    return (value != NULL) ? std::wstring(value) : std::wstring();
}

std::wstring ToFormatArg(const std::wstring& value)
{
    return value;
}

std::string ToFormatArgUtf8(int value)
{
    return ToFormatArgUtf8((long long)value);
}

std::string ToFormatArgUtf8(unsigned int value)
{
    return ToFormatArgUtf8((unsigned long long)value);
}

std::string ToFormatArgUtf8(long value)
{
    return ToFormatArgUtf8((long long)value);
}

std::string ToFormatArgUtf8(unsigned long value)
{
    return ToFormatArgUtf8((unsigned long long)value);
}

std::string ToFormatArgUtf8(long long value)
{
    char buf[kIntTextChars];
    sprintf_s(buf, "%lld", value);
    return buf;
}

std::string ToFormatArgUtf8(unsigned long long value)
{
    char buf[kIntTextChars];
    sprintf_s(buf, "%llu", value);
    return buf;
}

std::string ToFormatArgUtf8(const char* value)
{
    return (value != NULL) ? std::string(value) : std::string();
}

std::string ToFormatArgUtf8(const std::string& value)
{
    return value;
}

void SetFontProbeForTest(FontInstalledFn fn)
{
    g_fontProbe = fn;
}

void ResetForTest()
{
    g_initialized = false;
    UseSourceLanguage();
    g_earlyLookups = 0;
}

} // namespace i18n
