/**
 *  界面文字的多语言查找：启动时按配置加载可选的语言包 FlamingoLang.dll，此后由 TR 系列宏把简体中文原文
 *  换成当前语言的译文。方案见 docs/多语言方案.md。
 *
 *  核心规则是「查不到就显示原文」：语言包不存在、加载失败、没有这种语言、或某条文字还没有译文，界面一律显示
 *  代码里的简体中文原文，不会出现空白。以后新增的界面只要把文字包进 TR，就自动具备多语言能力。
 *
 *  写法：
 *      pLabel->SetText(TR(L"记住密码"));                                  //普通文字
 *      menu.AddItem(TRC(L"群成员菜单", L"移出"));                          //同一原文在不同场合需要不同译法时附加上下文
 *      std::wstring s = TRF(L"已选择 {0} 个联系人，共 {1} 个", n, total); //带参数的整句模板，译者可以调整 {n} 的顺序
 *      static const wchar_t* const kNames[] = { TR_NOOP(L"在线"), ... }; //静态表只做标记，到使用处再调 TR
 *
 *  TR 只用于字符串字面量：查不到译文时返回的就是传入的指针本身，传入临时对象的缓冲区会在语句结束后失效。
 *
 *  初始化：Init 只在进程启动时、创建任何窗口之前调用一次（见 App/Startup.cpp）。语言在进程生命周期内不变，
 *  切换语言须重启。初始化之前调用 TR 也安全（返回原文），但说明有文字在语言确定之前就被取用了，
 *  模块会计数，Debug 构建下还会输出一行调试信息，便于定位。
 *
 *  线程：Init 与 *ForTest 系列只在单线程环境调用；Translate 可以在任意线程调用。
 *
 *  本文件只依赖标准库与 FlamingoLang/FlamingoLangApi.h，不包含 stdafx.h、不写日志，被 Flamingo.vcxproj、
 *  FlamingoUnit.vcxproj 以及直接编译了 Source/ 文件的各 DLL 工程共用；在 Flamingo.vcxproj 里须标记
 *  PrecompiledHeader=NotUsing。初始化结果由调用方写日志（DescribeInitResult）。
 *
 *  本头文件刻意不包含 <windows.h>：它会被几百个界面文件包含，其中有的文件没有包含 windows.h、
 *  直接使用 std::min / std::max，windows.h 定义的 min / max 宏会让这些文件编译失败。
 *  因此接口里的 LANGID / DWORD 写成与之等价的 unsigned short / unsigned long。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "../../../FlamingoLang/FlamingoLangApi.h"

//普通文字：返回当前语言的译文，没有译文时返回传入的原文指针本身。
#define TR(source)              ::i18n::Translate(source)

//带上下文的文字：上下文只用于区分同一原文的不同译法，不显示。
#define TRC(context, source)    ::i18n::Translate(context, source)

//带参数的整句模板：先查模板的译文，再把 {0}、{1} …… 替换为参数，返回 std::wstring。
#define TRF(pattern, ...)       ::i18n::FormatText(::i18n::Translate(pattern), __VA_ARGS__)

//只做标记、不做翻译：原样返回原文，供提取脚本收录。用于静态字符串表与运行期拼接的 XML 片段。
#define TR_NOOP(source)         (source)

//UTF-8 文字（u8"..." 或 /utf-8 下的 "..."）：返回当前语言的译文，类型为 std::string（UTF-8）；没有译文时返回原文的副本。
//用于消息摘要等本来就以 UTF-8 std::string 处理的文字；宽字符串一律用 TR。
#define TR8(source)             ::i18n::TranslateUtf8(source)

//带上下文的 UTF-8 文字：同一原文在不同场合要不同译法时用（同 TRC），返回 std::string（UTF-8）。
#define TRC8(context, source)   ::i18n::TranslateUtf8(context, source)

//带参数的整句模板（UTF-8 版）：模板与字符串参数都是 UTF-8，先查模板的译文再代入参数，返回 std::string（UTF-8）。
//用于本来就以 UTF-8 std::string 处理的文字；宽字符串一律用 TRF。
#define TRF8(pattern, ...)      ::i18n::FormatTextUtf8(::i18n::TranslateUtf8(pattern), __VA_ARGS__)

namespace i18n {

//代码里原文的语言，即简体中文。选中它时不加载语言包。
extern const wchar_t* const kSourceLanguage;

//简体中文与一切退回情况下使用的界面字体。
extern const wchar_t* const kDefaultUiFont;

//flamingo.ini 中保存界面语言的节名与键名：[ui] language。值为空或没有这一项表示跟随系统。
extern const wchar_t* const kIniSection;
extern const wchar_t* const kIniKey;

//初始化的结果。
enum InitStatus
{
    init_source_language      = 0,  //期望的语言就是简体中文，未加载语言包
    init_ok                   = 1,  //已加载语言包并选定期望的语言
    init_dll_missing          = 2,  //期望的语言不是简体中文，但语言包不存在或无法加载，退回简体中文
    init_dll_invalid          = 3,  //语言包没有导出函数、或函数表比最小版本短，退回简体中文
    init_language_unavailable = 4,  //语言包里没有期望的语言、或取不到它的数据，退回简体中文
};

/**
 *  初始化的详细结果，由调用方写日志。
 */
struct InitResult
{
    InitStatus   m_status;          //结果，取值见 enum InitStatus
    bool         m_followSystem;    //配置里没有指定语言，按系统界面语言决定
    std::wstring m_requested;       //期望的语言代码：配置值，或跟随系统时由系统语言映射出的代码
    std::wstring m_language;        //最终生效的语言代码；退回时为 kSourceLanguage
    std::wstring m_uiFont;          //最终生效的界面字体
    std::wstring m_packVersion;     //语言包版本；没有加载语言包时为空
    uint32_t     m_entries;         //当前语言的有效译文条数；简体中文时为 0
    uint32_t     m_errors;          //语言包解析时跳过的坏条目数
    uint32_t     m_warnings;        //语言包解析时的警告数
    unsigned long m_loadError;       //加载 DLL 失败时的 GetLastError()；其它情况为 0

    InitResult()
        : m_status(init_source_language), m_followSystem(false), m_entries(0), m_errors(0), m_warnings(0),
          m_loadError(0)
    {
    }
};

/**
 *  把系统界面语言映射成语言包的语言代码。
 *  @param langId 系统界面语言，如 GetUserDefaultUILanguage() 的返回值
 *  @return 繁体中文（台湾、香港、澳门及繁体中性）为 L"zh-TW"，任何英文为 L"en-US"，日文为 L"ja-JP"，
 *          韩文为 L"ko-KR"，其余（含简体中文）为 kSourceLanguage
 */
std::wstring MapSystemLanguage(unsigned short langId);

/**
 *  由配置值与系统界面语言决定期望的语言。
 *  @param configured   flamingo.ini 中 [ui] language 的值；为空（或全是空白）表示跟随系统
 *  @param systemUiLang 系统界面语言
 *  @param followSystem [出参] 是否按系统界面语言决定，可为 NULL
 *  @return 期望的语言代码；配置为简体中文（不区分大小写）时统一返回 kSourceLanguage
 */
std::wstring ResolveRequestedLanguage(const std::wstring& configured, unsigned short systemUiLang, bool* followSystem);

/**
 *  初始化：决定期望的语言，需要时加载语言包并选定该语言，再确定界面字体。进程启动时调用一次。
 *  语言包加载后不卸载（TR 返回的指针指向它的内存）。
 *  @param configured   flamingo.ini 中 [ui] language 的值，见 ResolveRequestedLanguage
 *  @param dllPath      语言包的完整路径，通常是「exe 所在目录 + FLAMINGO_LANG_DLL_NAME」
 *  @param systemUiLang 系统界面语言，通常是 GetUserDefaultUILanguage()
 *  @return 结果；任何失败都退回简体中文，不影响程序继续运行
 */
InitResult Init(const std::wstring& configured, const std::wstring& dllPath, unsigned short systemUiLang);

//插件 DLL 把查找转交给宿主时用的函数类型，参数与返回值同 Translate(context, source)。
typedef const wchar_t* (*HostTranslateFn)(const wchar_t* context, const wchar_t* source);

/**
 *  插件 DLL（FlamingoMedia、FlamingoFileShare）用的初始化：不加载语言包，把查找转交给宿主 exe 的语言模块，
 *  整个进程只有一份语言状态。DLL 在自己的 Init 里、创建任何窗口之前调用一次。
 *  @param fn       查找函数，通常经宿主接口转到 exe 的 i18n::Translate；为 NULL 时视同简体中文
 *  @param language 宿主当前生效的语言代码；为空或为简体中文时不转交，TR 直接返回原文
 *  @param uiFont   宿主当前生效的界面字体；为空时沿用 kDefaultUiFont
 */
void InitFromHost(HostTranslateFn fn, const std::wstring& language, const std::wstring& uiFont);

/**
 *  把初始化结果写成一句英文说明（纯 ASCII），供调用方写日志。
 *  @param result Init 的返回值
 *  @return 形如 "i18n: language en-US (configured), pack 1.0.0, 523 entries, 0 errors, ui font Segoe UI."
 */
std::string DescribeInitResult(const InitResult& result);

/**
 *  查找译文。
 *  @param source 简体中文原文（字符串字面量）；为 NULL 时返回 NULL
 *  @return 当前语言的译文，归语言包所有、进程结束前有效；没有译文时返回 source 本身
 */
const wchar_t* Translate(const wchar_t* source);

/**
 *  按上下文查找译文。
 *  @param context 上下文（字符串字面量），可为 NULL
 *  @param source  简体中文原文（字符串字面量）；为 NULL 时返回 NULL
 *  @return 同 Translate(source)
 */
const wchar_t* Translate(const wchar_t* context, const wchar_t* source);

/**
 *  查找 UTF-8 原文的译文。
 *  @param source UTF-8 原文；为 NULL 时返回空串
 *  @return 当前语言的译文（UTF-8）；没有译文、或当前为简体中文时返回原文的副本
 */
std::string TranslateUtf8(const char* source);

/**
 *  按上下文查找 UTF-8 原文的译文（TRC 的 UTF-8 版本）。
 *  @param context 上下文（UTF-8 字符串字面量），可为 NULL（等同 TranslateUtf8(source)）
 *  @param source  UTF-8 原文；为 NULL 时返回空串
 *  @return 当前语言的译文（UTF-8）；没有译文、或当前为简体中文时返回原文的副本
 */
std::string TranslateUtf8(const char* context, const char* source);

//当前生效的语言代码；初始化之前为 kSourceLanguage。
std::wstring GetLanguage();

//当前语言是否就是简体中文（未加载语言包或已退回）。
bool IsSourceLanguage();

//当前生效的界面字体；初始化之前为 kDefaultUiFont。
std::wstring GetUiFontFace();

//当前生效的界面字体名，与 GetUiFontFace 相同，但返回字符串指针，供 CreateFontW、LOGFONT 等直接使用。
//指针指向模块内部的存储，在下一次初始化之前一直有效（进程内只初始化一次，实际上一直有效）；初始化之前为 kDefaultUiFont。
const wchar_t* UiFontName();

//初始化之前调用 Translate 的次数，Debug 与 Release 都计数。调用方在初始化后写日志用。
uint32_t GetEarlyLookupCount();

/**
 *  按 {n} 编号把参数代入模板。只认 {0} ~ {9} 这样「左花括号 + 一位数字 + 右花括号」的写法；
 *  编号超出参数个数的占位符与其它花括号原样保留。
 *  @param pattern 模板，为 NULL 时视为空串
 *  @param args    参数，下标即编号
 *  @return 代入后的文字
 */
std::wstring FormatPattern(const wchar_t* pattern, const std::vector<std::wstring>& args);

//以下 ToFormatArg 把 TRF 的各类参数转成文字；CString 经它到 const wchar_t* 的隐式转换走字符串版本。
std::wstring ToFormatArg(int value);
std::wstring ToFormatArg(unsigned int value);
std::wstring ToFormatArg(long value);
std::wstring ToFormatArg(unsigned long value);
std::wstring ToFormatArg(long long value);
std::wstring ToFormatArg(unsigned long long value);
std::wstring ToFormatArg(const wchar_t* value);
std::wstring ToFormatArg(const std::wstring& value);

//递归收集参数的终止条件。
inline void CollectFormatArgs(std::vector<std::wstring>& out)
{
    (void)out;
}

//逐个把参数转成文字放进 out。
template <typename T, typename... Rest>
void CollectFormatArgs(std::vector<std::wstring>& out, const T& first, const Rest&... rest)
{
    out.push_back(ToFormatArg(first));
    CollectFormatArgs(out, rest...);
}

/**
 *  TRF 的实现：把参数代入（已翻译的）模板。
 *  @param pattern 模板（通常是 Translate 的返回值）
 *  @param args    参数，支持整数、const wchar_t*、std::wstring、CString
 *  @return 代入后的文字
 */
template <typename... Args>
std::wstring FormatText(const wchar_t* pattern, const Args&... args)
{
    std::vector<std::wstring> values;
    values.reserve(sizeof...(Args));
    CollectFormatArgs(values, args...);
    return FormatPattern(pattern, values);
}

/**
 *  FormatPattern 的 UTF-8 版本，规则相同。占位符全由 ASCII 字符组成，不会与 UTF-8 多字节序列混淆。
 *  @param pattern 模板（UTF-8）
 *  @param args    参数（UTF-8），下标即编号
 *  @return 代入后的文字（UTF-8）
 */
std::string FormatPatternUtf8(const std::string& pattern, const std::vector<std::string>& args);

//以下 ToFormatArgUtf8 把 TRF8 的各类参数转成 UTF-8 文字；字符串参数须已是 UTF-8。
std::string ToFormatArgUtf8(int value);
std::string ToFormatArgUtf8(unsigned int value);
std::string ToFormatArgUtf8(long value);
std::string ToFormatArgUtf8(unsigned long value);
std::string ToFormatArgUtf8(long long value);
std::string ToFormatArgUtf8(unsigned long long value);
std::string ToFormatArgUtf8(const char* value);
std::string ToFormatArgUtf8(const std::string& value);

//递归收集参数的终止条件（UTF-8 版）。
inline void CollectFormatArgsUtf8(std::vector<std::string>& out)
{
    (void)out;
}

//逐个把参数转成 UTF-8 文字放进 out。
template <typename T, typename... Rest>
void CollectFormatArgsUtf8(std::vector<std::string>& out, const T& first, const Rest&... rest)
{
    out.push_back(ToFormatArgUtf8(first));
    CollectFormatArgsUtf8(out, rest...);
}

/**
 *  TRF8 的实现：把参数代入（已翻译的）UTF-8 模板。
 *  @param pattern 模板（通常是 TranslateUtf8 的返回值）
 *  @param args    参数，支持整数、const char*、std::string（字符串须为 UTF-8）
 *  @return 代入后的文字（UTF-8）
 */
template <typename... Args>
std::string FormatTextUtf8(const std::string& pattern, const Args&... args)
{
    std::vector<std::string> values;
    values.reserve(sizeof...(Args));
    CollectFormatArgsUtf8(values, args...);
    return FormatPatternUtf8(pattern, values);
}

// ---- 以下只供单元测试使用 ----

//判断字体是否已安装的函数类型，测试用它代替实际查询系统字体。
typedef bool (*FontInstalledFn)(const wchar_t* face);

/**
 *  不加载 DLL、直接使用给定的函数表初始化，其余逻辑与 Init 相同。<u>只供单元测试使用</u>。
 *  @param configured   同 Init
 *  @param api          函数表，可为 NULL（相当于语言包不存在）
 *  @param systemUiLang 同 Init
 */
InitResult InitWithApiForTest(const std::wstring& configured, const flamingolang::LangApi* api, unsigned short systemUiLang);

//替换判断字体是否已安装的函数；传 NULL 恢复为查询系统字体。<u>只供单元测试使用</u>。
void SetFontProbeForTest(FontInstalledFn fn);

//恢复到未初始化状态（不卸载已加载的 DLL），并把初始化之前的查找计数清零。<u>只供单元测试使用</u>。
void ResetForTest();

} // namespace i18n
