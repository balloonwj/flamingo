/**
 *  界面文字多语言查找模块（Common/I18n/I18n）的单元测试，用例 I1 ~ I8、I10、I11（I9 在 I18nXmlTests.cpp）。
 *
 *  用例清单（2026-10-03 与用户确认，见 docs/多语言方案.md 第 11 节 1b）：
 *    - I1  语言包 DLL 不存在：简体中文，TR 原样返回原文指针，初始化说明为英文
 *    - I2  配置为 zh-CN：不加载 DLL
 *    - I3  配置的语言 DLL 里没有、选定失败、函数表比最小版本短、文件不是 DLL：都退回简体中文并说明原因
 *    - I4  跟随系统：系统语言到语言代码的映射；映射到的语言 DLL 里没有时退回简体中文
 *    - I5  TRC 上下文
 *    - I6  TRF 模板：{n} 替换、重复使用、多余占位符保留、各类参数、译文调换顺序
 *    - I7  TR_NOOP 原样返回
 *    - I8  初始化之前调用 TR：返回原文并计数
 *    - I10 界面字体：取候选列表里第一个已安装的，都没装时退回微软雅黑，简体中文为微软雅黑
 *    - I11 加载真实的 FlamingoLang.dll（构建后由 FlamingoUnit.vcxproj 复制到测试程序目录）
 *    - I12 TR8、I13 TRF8：UTF-8 原文的查找与模板代入
 *    - I14 插件 DLL 经 InitFromHost 把查找转交给宿主
 *    - I15 I18nLayout：非简体中文界面下按译文宽度只增不减地加宽，简体中文界面不改宽度（第 4 期 4e）
 *    - I16 文件对话框过滤串：说明文字逐段翻译后拼成「说明\0通配符\0……\0\0」
 *    - I17 加载语言包（quietload::LoadLibraryQuietly）：文件不是 DLL、路径不存在时返回失败并保留错误码，调用前后线程的错误模式不变
 *          （不弹「损坏的映像」框这一点由 tmp/i18n_phase5/fallback_check.ps1 对真实程序做端到端检查；
 *          测试进程启动时已整体屏蔽错误框，见 TestCrashDialogSetup.cpp，单测里观察不到弹框）
 *    - I18 I18nLayout 按本机所有显示器中最高的 DPI 量文字（2026-10-07）：各档 DPI 下按钮宽度放得下该 DPI 下绘制的文字，
 *          最高 DPI 不低于系统 DPI 与每块显示器的 DPI，简体中文保持设计宽度（另有标签 [i18ndpi]）；另有一段切换到按显示器
 *          感知 DPI 的线程上下文（单元测试程序本身按 96 DPI 运行），检查取到显示器的真实 DPI、按钮放得下该 DPI 下的译文
 *
 *  除 I11 外，语言包一律用本文件里的假函数表注入（InitWithApiForTest），不依赖真实 DLL。
 *  模块是进程内的全局状态，每个用例开头先 ResetForTest。框架 Catch2 v3，标签 [i18n]。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <stdio.h>
#include <string.h>
#include <set>
#include <string>
#include <vector>

#include "../Common/I18n/I18n.h"
#include "../Common/I18n/I18nLayout.h"
#include "TestDpiAwareScope.h"   // I18：按显示器感知 DPI 的线程上下文
#include "../Common/Util/FileDialogFilter.h"
#include "../Common/Util/QuietLoadLibrary.h"

using flamingolang::LangApi;
using flamingolang::LangInfo;
using flamingolang::LangStats;

namespace {

// ---- 假语言包：四种语言，en-US 与 ja-JP 有数据，ko-KR 取不到数据 ----

const LangInfo kFakeLanguages[] =
{
    { (uint32_t)sizeof(LangInfo), L"zh-TW", L"繁體中文", 3, { L"Microsoft JhengHei", L"PMingLiU", L"Microsoft YaHei" } },
    { (uint32_t)sizeof(LangInfo), L"en-US", L"English", 2, { L"Segoe UI", L"Microsoft YaHei" } },
    { (uint32_t)sizeof(LangInfo), L"ja-JP", L"日本語", 5, { L"Yu Gothic UI", L"Meiryo UI", L"Meiryo", L"MS UI Gothic", L"Microsoft YaHei" } },
    { (uint32_t)sizeof(LangInfo), L"ko-KR", L"한국어", 3, { L"Malgun Gothic", L"Gulim", L"Microsoft YaHei" } },
};
const uint32_t kFakeLanguageCount = sizeof(kFakeLanguages) / sizeof(kFakeLanguages[0]);

//假语言包当前选定的语言下标，-1 表示未选定
int g_fakeSelected = -1;

//一条假译文
struct FakeEntry
{
    const wchar_t* m_language;
    const wchar_t* m_context;   //NULL 表示没有上下文
    const wchar_t* m_source;
    const wchar_t* m_text;
};

const FakeEntry kFakeEntries[] =
{
    { L"en-US", NULL,              L"记住密码", L"Remember password" },
    { L"en-US", L"群成员菜单",     L"移出",     L"Remove" },
    { L"en-US", L"文件菜单",       L"移出",     L"Move out" },
    { L"en-US", NULL,              L"已选择 {0} 个联系人，共 {1} 个", L"{1} contacts in total, {0} selected" },
    { L"ja-JP", NULL,              L"记住密码", L"パスワードを保存" },
};

const wchar_t* __cdecl FakeGetPackVersion()
{
    return L"9.9.9";
}

uint32_t __cdecl FakeGetLanguageCount()
{
    return kFakeLanguageCount;
}

const LangInfo* __cdecl FakeGetLanguageInfo(uint32_t index)
{
    return (index < kFakeLanguageCount) ? &kFakeLanguages[index] : NULL;
}

int __cdecl FakeSelectLanguage(const wchar_t* code)
{
    if (code == NULL || code[0] == L'\0')
        return flamingolang::lang_select_bad_argument;
    for (uint32_t i = 0; i < kFakeLanguageCount; ++i)
    {
        if (_wcsicmp(kFakeLanguages[i].m_code, code) != 0)
            continue;
        if (wcscmp(kFakeLanguages[i].m_code, L"ko-KR") == 0)
            return flamingolang::lang_select_no_data;
        g_fakeSelected = (int)i;
        return flamingolang::lang_select_ok;
    }
    return flamingolang::lang_select_unknown;
}

const wchar_t* __cdecl FakeLookup(const wchar_t* context, const wchar_t* source)
{
    if (g_fakeSelected < 0 || source == NULL)
        return NULL;
    const wchar_t* language = kFakeLanguages[g_fakeSelected].m_code;
    bool hasContext = (context != NULL && context[0] != L'\0');
    for (size_t i = 0; i < sizeof(kFakeEntries) / sizeof(kFakeEntries[0]); ++i)
    {
        const FakeEntry& e = kFakeEntries[i];
        if (wcscmp(e.m_language, language) != 0 || wcscmp(e.m_source, source) != 0)
            continue;
        bool entryHasContext = (e.m_context != NULL);
        if (hasContext != entryHasContext)
            continue;
        if (hasContext && wcscmp(e.m_context, context) != 0)
            continue;
        return e.m_text;
    }
    return NULL;
}

bool __cdecl FakeGetStats(LangStats* stats)
{
    if (stats == NULL || g_fakeSelected < 0)
        return false;
    stats->m_entries = 4;
    stats->m_errors = 1;
    stats->m_warnings = 2;
    return true;
}

const LangApi kFakeApi =
{
    (uint32_t)sizeof(LangApi),
    FakeGetPackVersion,
    FakeGetLanguageCount,
    FakeGetLanguageInfo,
    FakeSelectLanguage,
    FakeLookup,
    FakeGetStats,
};

//比最小版本短的函数表（模拟只有前三个函数的旧语言包）
const LangApi kShortApi =
{
    (uint32_t)(offsetof(LangApi, m_getLanguageInfo) + sizeof(void*)),
    FakeGetPackVersion,
    FakeGetLanguageCount,
    FakeGetLanguageInfo,
    NULL,
    NULL,
    NULL,
};

// ---- 假的字体查询：只认 g_installedFonts 里的字体 ----

std::set<std::wstring> g_installedFonts;

bool FakeFontInstalled(const wchar_t* face)
{
    return g_installedFonts.count(face) > 0;
}

//系统界面语言 LANGID：简体中文（中国）
const LANGID kLangZhCn = 0x0804;
//日文（日本）
const LANGID kLangJaJp = 0x0411;
//韩文（韩国）
const LANGID kLangKoKr = 0x0412;

//每个用例开头调用：清空模块状态与假语言包状态，并让常用字体都算作已安装
void FreshState()
{
    i18n::ResetForTest();
    g_fakeSelected = -1;
    g_installedFonts.clear();
    g_installedFonts.insert(L"Microsoft YaHei");
    g_installedFonts.insert(L"Segoe UI");
    g_installedFonts.insert(L"Yu Gothic UI");
    i18n::SetFontProbeForTest(FakeFontInstalled);
}

bool TextEquals(const wchar_t* got, const wchar_t* expect)
{
    return got != NULL && wcscmp(got, expect) == 0;
}

//判断文字是否只含可打印 ASCII
bool IsPrintableAscii(const std::string& text)
{
    for (size_t i = 0; i < text.size(); ++i)
    {
        unsigned char c = (unsigned char)text[i];
        if (c < 0x20 || c > 0x7E)
            return false;
    }
    return true;
}

//测试程序所在目录（带结尾反斜杠）
std::wstring GetTestExeDirectory()
{
    wchar_t path[MAX_PATH] = { 0 };
    DWORD len = ::GetModuleFileNameW(NULL, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return std::wstring();
    std::wstring dir(path);
    size_t pos = dir.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? std::wstring() : dir.substr(0, pos + 1);
}

/**
 *  用例结束时把多语言模块恢复到未初始化状态（含中途 REQUIRE 失败退出的情况）：同一测试进程里的其它用例
 *  断言的是中文原文，不能沿用本组用例选定的语言或注入的字体查询函数。
 */
struct ScopedI18nReset
{
    ~ScopedI18nReset()
    {
        i18n::ResetForTest();
        i18n::SetFontProbeForTest(NULL);
    }
};

} // namespace

TEST_CASE("I1 语言包不存在时显示简体中文原文", "[i18n]")
{
    ScopedI18nReset resetOnExit;
    FreshState();
    std::wstring missing = GetTestExeDirectory() + L"no_such_dir\\FlamingoLang.dll";
    i18n::InitResult r = i18n::Init(L"en-US", missing, kLangZhCn);

    CHECK(r.m_status == i18n::init_dll_missing);
    CHECK(r.m_requested == L"en-US");
    CHECK(r.m_language == i18n::kSourceLanguage);
    CHECK(r.m_uiFont == i18n::kDefaultUiFont);
    CHECK(r.m_packVersion.empty());
    CHECK(i18n::IsSourceLanguage());
    CHECK(i18n::GetLanguage() == i18n::kSourceLanguage);

    //原文指针原样返回
    const wchar_t* source = L"记住密码";
    CHECK(TR(source) == source);
    CHECK(TRC(L"按钮", source) == source);
    CHECK(TR((const wchar_t*)NULL) == NULL);

    //初始化说明是纯 ASCII 的英文，写明了原因
    std::string text = i18n::DescribeInitResult(r);
    CHECK(IsPrintableAscii(text));
    CHECK(text.find("not found") != std::string::npos);
    CHECK(text.find("en-US") != std::string::npos);
}

TEST_CASE("I2 配置为简体中文时不加载语言包", "[i18n]")
{
    ScopedI18nReset resetOnExit;
    FreshState();
    //路径指向不存在的文件：若尝试加载，结果会是 init_dll_missing 而不是 init_source_language
    std::wstring missing = GetTestExeDirectory() + L"no_such_dir\\FlamingoLang.dll";

    i18n::InitResult r = i18n::Init(L"zh-CN", missing, kLangJaJp);
    CHECK(r.m_status == i18n::init_source_language);
    CHECK_FALSE(r.m_followSystem);
    CHECK(r.m_loadError == 0);
    CHECK(r.m_language == i18n::kSourceLanguage);

    //大小写不同、前后有空白也认作简体中文
    FreshState();
    r = i18n::Init(L"  ZH-cn ", missing, kLangJaJp);
    CHECK(r.m_status == i18n::init_source_language);
    CHECK(r.m_requested == i18n::kSourceLanguage);

    CHECK(IsPrintableAscii(i18n::DescribeInitResult(r)));
}

TEST_CASE("I3 各种失败都退回简体中文并说明原因", "[i18n]")
{
    ScopedI18nReset resetOnExit;
    const wchar_t* source = L"记住密码";

    SECTION("语言包里没有配置的语言")
    {
        FreshState();
        i18n::InitResult r = i18n::InitWithApiForTest(L"fr-FR", &kFakeApi, kLangZhCn);
        CHECK(r.m_status == i18n::init_language_unavailable);
        CHECK(r.m_requested == L"fr-FR");
        CHECK(r.m_language == i18n::kSourceLanguage);
        CHECK(r.m_packVersion == L"9.9.9");
        CHECK(TR(source) == source);
        CHECK(i18n::DescribeInitResult(r).find("not available") != std::string::npos);
    }

    SECTION("有这种语言但选定失败")
    {
        FreshState();
        i18n::InitResult r = i18n::InitWithApiForTest(L"ko-KR", &kFakeApi, kLangZhCn);
        CHECK(r.m_status == i18n::init_language_unavailable);
        CHECK(r.m_language == i18n::kSourceLanguage);
        CHECK(TR(source) == source);
    }

    SECTION("函数表比最小版本短")
    {
        FreshState();
        i18n::InitResult r = i18n::InitWithApiForTest(L"en-US", &kShortApi, kLangZhCn);
        CHECK(r.m_status == i18n::init_dll_invalid);
        CHECK(r.m_language == i18n::kSourceLanguage);
        CHECK(TR(source) == source);
        CHECK(i18n::DescribeInitResult(r).find("invalid") != std::string::npos);
    }

    SECTION("没有函数表（相当于语言包不存在）")
    {
        FreshState();
        i18n::InitResult r = i18n::InitWithApiForTest(L"en-US", NULL, kLangZhCn);
        CHECK(r.m_status == i18n::init_dll_missing);
        CHECK(TR(source) == source);
    }

    SECTION("文件存在但不是 DLL")
    {
        FreshState();
        std::wstring fake = GetTestExeDirectory() + L"i18n_not_a_dll.dll";
        FILE* fp = _wfopen(fake.c_str(), L"wb");
        REQUIRE(fp != NULL);
        fputs("this is not a dll", fp);
        fclose(fp);

        i18n::InitResult r = i18n::Init(L"en-US", fake, kLangZhCn);
        ::DeleteFileW(fake.c_str());
        CHECK(r.m_status == i18n::init_dll_missing);
        CHECK(r.m_loadError != 0);
        CHECK(r.m_language == i18n::kSourceLanguage);
        CHECK(TR(source) == source);
        std::string text = i18n::DescribeInitResult(r);
        CHECK(IsPrintableAscii(text));
        CHECK(text.find("cannot be loaded") != std::string::npos);
    }
}

TEST_CASE("I4 跟随系统界面语言", "[i18n]")
{
    ScopedI18nReset resetOnExit;
    SECTION("系统语言到语言代码的映射")
    {
        CHECK(i18n::MapSystemLanguage(0x0404) == L"zh-TW");   //中文（台湾）
        CHECK(i18n::MapSystemLanguage(0x0C04) == L"zh-TW");   //中文（香港）
        CHECK(i18n::MapSystemLanguage(0x1404) == L"zh-TW");   //中文（澳门）
        CHECK(i18n::MapSystemLanguage(0x7C04) == L"zh-TW");   //中文（繁体，中性）
        CHECK(i18n::MapSystemLanguage(0x0804) == L"zh-CN");   //中文（中国）
        CHECK(i18n::MapSystemLanguage(0x1004) == L"zh-CN");   //中文（新加坡）
        CHECK(i18n::MapSystemLanguage(0x0004) == L"zh-CN");   //中文（简体，中性）
        CHECK(i18n::MapSystemLanguage(0x0409) == L"en-US");   //英文（美国）
        CHECK(i18n::MapSystemLanguage(0x0809) == L"en-US");   //英文（英国）
        CHECK(i18n::MapSystemLanguage(0x0411) == L"ja-JP");   //日文
        CHECK(i18n::MapSystemLanguage(0x0412) == L"ko-KR");   //韩文
        CHECK(i18n::MapSystemLanguage(0x040C) == L"zh-CN");   //法文：语言包没有，用简体中文
        CHECK(i18n::MapSystemLanguage(0x0407) == L"zh-CN");   //德文
    }

    SECTION("配置为空时按系统语言决定")
    {
        bool follow = false;
        CHECK(i18n::ResolveRequestedLanguage(L"", kLangJaJp, &follow) == L"ja-JP");
        CHECK(follow);
        CHECK(i18n::ResolveRequestedLanguage(L"   ", kLangJaJp, &follow) == L"ja-JP");
        CHECK(follow);
        CHECK(i18n::ResolveRequestedLanguage(L"en-US", kLangJaJp, &follow) == L"en-US");
        CHECK_FALSE(follow);
        CHECK(i18n::ResolveRequestedLanguage(L"", kLangJaJp, NULL) == L"ja-JP");
    }

    SECTION("跟随系统选中语言包里的语言")
    {
        FreshState();
        i18n::InitResult r = i18n::InitWithApiForTest(L"", &kFakeApi, kLangJaJp);
        CHECK(r.m_status == i18n::init_ok);
        CHECK(r.m_followSystem);
        CHECK(r.m_language == L"ja-JP");
        CHECK(TextEquals(TR(L"记住密码"), L"パスワードを保存"));
        CHECK(i18n::DescribeInitResult(r).find("system") != std::string::npos);
    }

    SECTION("跟随系统映射到的语言取不到数据时退回简体中文")
    {
        FreshState();
        i18n::InitResult r = i18n::InitWithApiForTest(L"", &kFakeApi, kLangKoKr);
        CHECK(r.m_status == i18n::init_language_unavailable);
        CHECK(r.m_followSystem);
        CHECK(r.m_requested == L"ko-KR");
        CHECK(r.m_language == i18n::kSourceLanguage);
    }

    SECTION("系统语言是简体中文时不需要语言包")
    {
        FreshState();
        i18n::InitResult r = i18n::InitWithApiForTest(L"", &kFakeApi, kLangZhCn);
        CHECK(r.m_status == i18n::init_source_language);
        CHECK(r.m_followSystem);
        CHECK(r.m_packVersion.empty());
    }
}

TEST_CASE("I5 TRC 按上下文取不同译文", "[i18n]")
{
    ScopedI18nReset resetOnExit;
    FreshState();
    i18n::InitResult r = i18n::InitWithApiForTest(L"en-US", &kFakeApi, kLangZhCn);
    REQUIRE(r.m_status == i18n::init_ok);
    CHECK(r.m_language == L"en-US");
    CHECK(r.m_entries == 4);
    CHECK(r.m_errors == 1);
    CHECK(r.m_warnings == 2);

    CHECK(TextEquals(TR(L"记住密码"), L"Remember password"));
    CHECK(TextEquals(TRC(L"群成员菜单", L"移出"), L"Remove"));
    CHECK(TextEquals(TRC(L"文件菜单", L"移出"), L"Move out"));

    //不带上下文、或上下文没有对应译文：返回原文
    const wchar_t* source = L"移出";
    CHECK(TR(source) == source);
    CHECK(TRC(L"别的菜单", source) == source);
    CHECK(TRC(NULL, L"记住密码") != NULL);
    CHECK(TextEquals(TRC(NULL, L"记住密码"), L"Remember password"));
}

TEST_CASE("I6 TRF 模板代入参数", "[i18n]")
{
    ScopedI18nReset resetOnExit;
    SECTION("FormatPattern 的规则")
    {
        std::vector<std::wstring> args;
        args.push_back(L"A");
        args.push_back(L"B");
        CHECK(i18n::FormatPattern(L"{0}-{1}", args) == L"A-B");
        CHECK(i18n::FormatPattern(L"{1}{0}{1}", args) == L"BAB");
        CHECK(i18n::FormatPattern(L"{2} 与 {0}", args) == L"{2} 与 A");
        //只认一位数字：{10} 不是占位符，与 {a}、{}、结尾落单的 { 一样原样保留
        CHECK(i18n::FormatPattern(L"{a}{}{10}{", args) == L"{a}{}{10}{");
        CHECK(i18n::FormatPattern(L"没有占位符", args) == L"没有占位符");
        CHECK(i18n::FormatPattern(NULL, args) == L"");
        CHECK(i18n::FormatPattern(L"{0}", std::vector<std::wstring>()) == L"{0}");
    }

    SECTION("各类参数")
    {
        FreshState();
        CString name(L"张三");
        std::wstring group(L"研发部");
        size_t count = 12;
        long long big = 1234567890123LL;
        CHECK(TRF(L"{0}|{1}|{2}|{3}|{4}|{5}|{6}", -5, 7u, count, big, L"字", name, group)
              == L"-5|7|12|1234567890123|字|张三|研发部");
        unsigned long ul = 42;
        long l = -42;
        CHECK(TRF(L"{0},{1}", ul, l) == L"42,-42");
    }

    SECTION("先查译文再代入：译文调换了占位符的顺序")
    {
        FreshState();
        REQUIRE(i18n::InitWithApiForTest(L"en-US", &kFakeApi, kLangZhCn).m_status == i18n::init_ok);
        CHECK(TRF(L"已选择 {0} 个联系人，共 {1} 个", 3, 10) == L"10 contacts in total, 3 selected");
        //没有译文的模板按原文代入
        CHECK(TRF(L"共 {0} 人", 5) == L"共 5 人");
    }
}

TEST_CASE("I7 TR_NOOP 原样返回原文", "[i18n]")
{
    ScopedI18nReset resetOnExit;
    FreshState();
    REQUIRE(i18n::InitWithApiForTest(L"en-US", &kFakeApi, kLangZhCn).m_status == i18n::init_ok);
    static const wchar_t* const kNames[] = { TR_NOOP(L"记住密码"), TR_NOOP(L"在线") };
    CHECK(wcscmp(kNames[0], L"记住密码") == 0);
    CHECK(wcscmp(kNames[1], L"在线") == 0);
    //到使用处再翻译
    CHECK(TextEquals(TR(kNames[0]), L"Remember password"));
}

TEST_CASE("I8 初始化之前调用 TR 返回原文并计数", "[i18n]")
{
    ScopedI18nReset resetOnExit;
    FreshState();
    CHECK(i18n::GetEarlyLookupCount() == 0);
    const wchar_t* source = L"记住密码";
    CHECK(TR(source) == source);
    CHECK(TRC(L"按钮", source) == source);
    CHECK(i18n::GetEarlyLookupCount() == 2);
    CHECK(i18n::GetLanguage() == i18n::kSourceLanguage);
    CHECK(i18n::GetUiFontFace() == i18n::kDefaultUiFont);

    //初始化之后不再计数
    REQUIRE(i18n::InitWithApiForTest(L"en-US", &kFakeApi, kLangZhCn).m_status == i18n::init_ok);
    TR(source);
    CHECK(i18n::GetEarlyLookupCount() == 2);
}

TEST_CASE("I10 界面字体取第一个已安装的候选字体", "[i18n]")
{
    ScopedI18nReset resetOnExit;
    SECTION("首选字体已安装")
    {
        FreshState();
        i18n::InitResult r = i18n::InitWithApiForTest(L"ja-JP", &kFakeApi, kLangZhCn);
        REQUIRE(r.m_status == i18n::init_ok);
        CHECK(r.m_uiFont == L"Yu Gothic UI");
        CHECK(i18n::GetUiFontFace() == L"Yu Gothic UI");
    }

    SECTION("首选字体没装，取下一个已安装的")
    {
        FreshState();
        g_installedFonts.clear();
        g_installedFonts.insert(L"Meiryo");
        g_installedFonts.insert(L"Microsoft YaHei");
        i18n::InitResult r = i18n::InitWithApiForTest(L"ja-JP", &kFakeApi, kLangZhCn);
        CHECK(r.m_uiFont == L"Meiryo");
    }

    SECTION("候选字体都没装，退回微软雅黑")
    {
        FreshState();
        g_installedFonts.clear();
        i18n::InitResult r = i18n::InitWithApiForTest(L"ja-JP", &kFakeApi, kLangZhCn);
        CHECK(r.m_status == i18n::init_ok);
        CHECK(r.m_uiFont == i18n::kDefaultUiFont);
    }

    SECTION("简体中文与退回简体中文时都用微软雅黑")
    {
        FreshState();
        CHECK(i18n::InitWithApiForTest(L"zh-CN", &kFakeApi, kLangZhCn).m_uiFont == i18n::kDefaultUiFont);
        FreshState();
        CHECK(i18n::InitWithApiForTest(L"fr-FR", &kFakeApi, kLangZhCn).m_uiFont == i18n::kDefaultUiFont);
    }

    SECTION("查询系统字体的默认实现认得微软雅黑")
    {
        i18n::ResetForTest();
        i18n::SetFontProbeForTest(NULL);
        i18n::InitResult r = i18n::InitWithApiForTest(L"en-US", &kFakeApi, kLangZhCn);
        //Segoe UI 与微软雅黑都是 Windows 7 起的系统自带字体，取到的必是其中之一
        CHECK((r.m_uiFont == L"Segoe UI" || r.m_uiFont == L"Microsoft YaHei"));
    }
}

TEST_CASE("I11 加载真实的语言包", "[i18n]")
{
    ScopedI18nReset resetOnExit;
    FreshState();
    std::wstring dllPath = GetTestExeDirectory() + FLAMINGO_LANG_DLL_NAME;
    INFO("dll: " << (const char*)CStringA(dllPath.c_str()));
    REQUIRE(::GetFileAttributesW(dllPath.c_str()) != INVALID_FILE_ATTRIBUTES);

    i18n::InitResult r = i18n::Init(L"en-US", dllPath, kLangZhCn);
    CHECK(r.m_status == i18n::init_ok);
    CHECK(r.m_language == L"en-US");
    CHECK_FALSE(r.m_packVersion.empty());
    CHECK(r.m_errors == 0);
    CHECK(IsPrintableAscii(i18n::DescribeInitResult(r)));

    //伪语言也能经配置选定，查找结果是加了括号的伪译文
    FreshState();
    r = i18n::Init(FLAMINGO_LANG_PSEUDO_CODE, dllPath, kLangZhCn);
    CHECK(r.m_status == i18n::init_ok);
    CHECK(TextEquals(TR(L"记住密码"), L"«记住密码~~~~~~~~»"));
}

TEST_CASE("I12 TR8 查找 UTF-8 原文", "[i18n]")
{
    ScopedI18nReset resetOnExit;
    FreshState();

    //初始化之前：返回原文的副本
    CHECK(TR8(u8"记住密码") == std::string(u8"记住密码"));
    CHECK(i18n::TranslateUtf8(NULL).empty());

    //有译文时返回 UTF-8 译文，没有时返回原文
    REQUIRE(i18n::InitWithApiForTest(L"ja-JP", &kFakeApi, kLangZhCn).m_status == i18n::init_ok);
    CHECK(TR8(u8"记住密码") == std::string(u8"パスワードを保存"));
    CHECK(TR8(u8"没有译文") == std::string(u8"没有译文"));
    CHECK(TR8("") == std::string());

    //简体中文：原样返回
    FreshState();
    REQUIRE(i18n::InitWithApiForTest(L"zh-CN", &kFakeApi, kLangZhCn).m_status == i18n::init_source_language);
    CHECK(TR8(u8"记住密码") == std::string(u8"记住密码"));
}

TEST_CASE("I13 TRF8 代入 UTF-8 模板", "[i18n]")
{
    ScopedI18nReset resetOnExit;
    FreshState();

    SECTION("代入规则与 FormatPattern 相同")
    {
        std::vector<std::string> args;
        args.push_back("A");
        args.push_back(u8"乙");
        CHECK(i18n::FormatPatternUtf8("{0}-{1}", args) == std::string(u8"A-乙"));
        CHECK(i18n::FormatPatternUtf8(u8"{1}{0}{1}", args) == std::string(u8"乙A乙"));
        CHECK(i18n::FormatPatternUtf8(u8"{2} 与 {0}", args) == std::string(u8"{2} 与 A"));
        CHECK(i18n::FormatPatternUtf8("{0}", std::vector<std::string>()) == std::string("{0}"));
        CHECK(i18n::FormatPatternUtf8("", args).empty());
    }

    SECTION("各类参数：整数、const char*、std::string")
    {
        const std::string name(u8"张三");
        const long long big = 9000000000LL;
        CHECK(TRF8(u8"{0}|{1}|{2}|{3}", -5, 7u, big, name) == std::string(u8"-5|7|9000000000|张三"));
        CHECK(TRF8(u8"{0} 个文件", 12) == std::string(u8"12 个文件"));
        CHECK(TRF8("{0}", (const char*)NULL).empty());
    }

    SECTION("先查模板的译文再代入")
    {
        REQUIRE(i18n::InitWithApiForTest(L"en-US", &kFakeApi, kLangZhCn).m_status == i18n::init_ok);
        CHECK(TRF8(u8"已选择 {0} 个联系人，共 {1} 个", 3, 10) == std::string("10 contacts in total, 3 selected"));
        //没有译文的模板照原文代入
        CHECK(TRF8(u8"共 {0} 人", 5) == std::string(u8"共 5 人"));
    }
}

namespace {

//I14 用的宿主查找函数被调用的次数
int g_hostLookupCalls = 0;

//I14 用的宿主查找函数：只认识两条，其余返回 NULL（表示没有译文）
const wchar_t* FakeHostTranslate(const wchar_t* context, const wchar_t* source)
{
    ++g_hostLookupCalls;
    if (context == NULL && wcscmp(source, L"记住密码") == 0)
        return L"Remember password";
    if (context != NULL && wcscmp(context, L"群成员菜单") == 0 && wcscmp(source, L"移出") == 0)
        return L"Remove";
    return NULL;
}

} // namespace

TEST_CASE("I14 插件 DLL 把查找转交给宿主", "[i18n]")
{
    ScopedI18nReset resetOnExit;

    SECTION("宿主为英文：TR、TRC、TRF、TR8 都经宿主查找，语言与字体取宿主给的值")
    {
        FreshState();
        g_hostLookupCalls = 0;
        i18n::InitFromHost(&FakeHostTranslate, L"en-US", L"Segoe UI");
        CHECK_FALSE(i18n::IsSourceLanguage());
        CHECK(i18n::GetLanguage() == L"en-US");
        CHECK(i18n::GetUiFontFace() == L"Segoe UI");
        CHECK(TextEquals(TR(L"记住密码"), L"Remember password"));
        CHECK(TextEquals(TRC(L"群成员菜单", L"移出"), L"Remove"));
        //宿主没有译文时返回传入的原文指针本身
        const wchar_t* source = L"没有译文";
        CHECK(TR(source) == source);
        CHECK(TRF(L"共 {0} 人", 3) == L"共 3 人");
        CHECK(TR8(u8"记住密码") == std::string("Remember password"));
        CHECK(g_hostLookupCalls == 5);
        //InitFromHost 之后的查找不计入「初始化之前的查找」
        CHECK(i18n::GetEarlyLookupCount() == 0);
    }

    SECTION("宿主为简体中文：不转交，TR 直接返回原文")
    {
        FreshState();
        g_hostLookupCalls = 0;
        i18n::InitFromHost(&FakeHostTranslate, L"zh-CN", L"Microsoft YaHei");
        CHECK(i18n::IsSourceLanguage());
        CHECK(TextEquals(TR(L"记住密码"), L"记住密码"));
        CHECK(g_hostLookupCalls == 0);
    }

    SECTION("没有查找函数或语言为空：视同简体中文；字体为空时沿用默认字体")
    {
        FreshState();
        i18n::InitFromHost(NULL, L"en-US", L"");
        CHECK(i18n::IsSourceLanguage());
        CHECK(i18n::GetUiFontFace() == i18n::kDefaultUiFont);
        FreshState();
        g_hostLookupCalls = 0;
        i18n::InitFromHost(&FakeHostTranslate, L"  ", L"Segoe UI");
        CHECK(i18n::IsSourceLanguage());
        CHECK(TextEquals(TR(L"记住密码"), L"记住密码"));
        CHECK(g_hostLookupCalls == 0);
    }

    SECTION("恢复到未初始化状态后不再转交")
    {
        FreshState();
        i18n::InitFromHost(&FakeHostTranslate, L"en-US", L"Segoe UI");
        i18n::ResetForTest();
        g_hostLookupCalls = 0;
        CHECK(TextEquals(TR(L"记住密码"), L"记住密码"));
        CHECK(g_hostLookupCalls == 0);
        CHECK(i18n::GetLanguage() == i18n::kSourceLanguage);
    }
}

namespace {

//I15 量宽用的字号（像素）
const int kI15TextPx = 14;

//I15 的设计宽度（像素）：比下面的长文字窄，比空文字加留白宽
const int kI15DesignW = 20;

//I15 文字宽度之外的留白（像素）
const int kI15ExtraPx = 10;

//I15 量宽用的长文字（英文界面里常见长度的一条译文）
const wchar_t* const kI15LongText = L"Organization management";

//往水平布局末尾追加一个固定宽度的子控件，返回它的指针（所有权归布局容器）
balloonwjui::DuiControl* AddFixedChild(balloonwjui::DuiHBox& row, int width)
{
    std::unique_ptr<balloonwjui::DuiControl> child(new balloonwjui::DuiControl());
    balloonwjui::DuiControl* raw = child.get();
    row.AddChild(std::move(child), balloonwjui::DuiLayout::Hint().Fixed(width));
    return raw;
}

} // namespace

TEST_CASE("I15 译文较长时按文字宽度只增不减地加宽", "[i18n]")
{
    ScopedI18nReset resetOnExit;

    SECTION("简体中文：一律保持设计宽度，布局不变")
    {
        FreshState();
        i18n::InitFromHost(&FakeHostTranslate, L"zh-CN", L"Microsoft YaHei");
        REQUIRE(i18n::IsSourceLanguage());
        CHECK(i18n::WidthForText(kI15LongText, kI15DesignW, kI15TextPx, false, kI15ExtraPx) == kI15DesignW);
        CHECK(i18n::DefaultButtonWidthForText(kI15LongText, kI15DesignW) == kI15DesignW);

        balloonwjui::DuiHBox row;
        balloonwjui::DuiControl* child = AddFixedChild(row, kI15DesignW);
        CHECK_FALSE(i18n::GrowFixedWidthToText(child, kI15LongText, kI15TextPx, false, kI15ExtraPx));
        CHECK(row.GetHint(child).fixedMain == kI15DesignW);
    }

    SECTION("非简体中文：放不下时加宽到文字宽度加留白，放得下时不变")
    {
        FreshState();
        i18n::InitFromHost(&FakeHostTranslate, L"en-US", L"Segoe UI");
        REQUIRE_FALSE(i18n::IsSourceLanguage());
        const int textW = i18n::MeasureUiTextWidth(kI15LongText, kI15TextPx, false);
        REQUIRE(textW > kI15DesignW);
        CHECK(i18n::WidthForText(kI15LongText, kI15DesignW, kI15TextPx, false, kI15ExtraPx) == textW + kI15ExtraPx);
        //空文字加留白比设计宽度窄：保持设计宽度
        CHECK(i18n::WidthForText(L"", kI15DesignW, kI15TextPx, false, kI15ExtraPx) == kI15DesignW);
        //半粗体不比常规字重窄
        CHECK(i18n::MeasureUiTextWidth(kI15LongText, kI15TextPx, true) >= textW);
        //会切换文字的按钮取两种文字中较宽的一个，与参数顺序无关
        const int longBtnW = i18n::DefaultButtonWidthForText(kI15LongText, kI15DesignW);
        CHECK(longBtnW > kI15DesignW);
        CHECK(i18n::DefaultButtonWidthForTexts(L"", kI15LongText, kI15DesignW) == longBtnW);
        CHECK(i18n::DefaultButtonWidthForTexts(kI15LongText, L"", kI15DesignW) == longBtnW);

        balloonwjui::DuiHBox row;
        balloonwjui::DuiControl* fixedChild = AddFixedChild(row, kI15DesignW);
        CHECK(i18n::GrowFixedWidthToText(fixedChild, kI15LongText, kI15TextPx, false, kI15ExtraPx));
        CHECK(row.GetHint(fixedChild).fixedMain == textW + kI15ExtraPx);
        //已经加宽过，再调一次不再变化
        CHECK_FALSE(i18n::GrowFixedWidthToText(fixedChild, kI15LongText, kI15TextPx, false, kI15ExtraPx));

        //按权重分配宽度的子控件、空指针、不在布局容器里的控件：都不处理
        std::unique_ptr<balloonwjui::DuiControl> weightOwned(new balloonwjui::DuiControl());
        balloonwjui::DuiControl* weightChild = weightOwned.get();
        row.AddChild(std::move(weightOwned), balloonwjui::DuiLayout::Hint().Weight(1));
        CHECK_FALSE(i18n::GrowFixedWidthToText(weightChild, kI15LongText, kI15TextPx, false, kI15ExtraPx));
        CHECK_FALSE(i18n::GrowFixedWidthToText(NULL, kI15LongText, kI15TextPx, false, kI15ExtraPx));
        balloonwjui::DuiControl orphan;
        CHECK_FALSE(i18n::GrowFixedWidthToText(&orphan, kI15LongText, kI15TextPx, false, kI15ExtraPx));
    }

    SECTION("磅值字号换算与 balloonui 建字体时相同")
    {
        const int kPointSize = 9;        // balloonui 的默认字号（磅）
        const int kPointsPerInch = 72;   // 1 英寸 = 72 磅
        CHECK(i18n::PointToPixelHeight(kPointSize) ==
              ::MulDiv(kPointSize, balloonwjui::DuiDpi::GetSystemDpi(), kPointsPerInch));
    }
}

TEST_CASE("I16 文件对话框过滤串由逐段翻译的说明文字拼成", "[i18n]")
{
    //一项：说明\0通配符\0\0
    const std::wstring one = filedlg::MakeFilter(L"All files", L"*.*");
    const wchar_t kOne[] = L"All files\0*.*\0";
    CHECK(one == std::wstring(kOne, sizeof(kOne) / sizeof(kOne[0])));

    //两项：顺序与参数一致，末尾两个 \0
    const std::wstring two = filedlg::MakeFilter(L"Image files", filedlg::kImagePatterns, L"All files", L"*.*");
    const wchar_t kTwo[] = L"Image files\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0All files\0*.*\0";
    CHECK(two == std::wstring(kTwo, sizeof(kTwo) / sizeof(kTwo[0])));

    //c_str() 交给对话框时以两个 \0 结尾：最后一项之后紧跟字符串自身的结束符
    CHECK(two[two.size() - 1] == L'\0');
    CHECK(two.c_str()[two.size()] == L'\0');

    //说明或通配符为 NULL 时按空串处理，不崩溃
    const std::wstring empty = filedlg::MakeFilter(NULL, NULL);
    CHECK(empty == std::wstring(3, L'\0'));
}

TEST_CASE("I17 加载语言包失败时返回错误码，不改变线程的错误模式", "[i18n]")
{
    const DWORD modeBefore = ::GetThreadErrorMode();

    SECTION("文件存在但不是 DLL")
    {
        wchar_t tempDir[MAX_PATH] = { 0 };
        REQUIRE(::GetTempPathW(MAX_PATH, tempDir) > 0);
        const std::wstring path = std::wstring(tempDir) + L"FlamingoLang_i17_notdll.dll";
        FILE* fp = NULL;
        REQUIRE(_wfopen_s(&fp, path.c_str(), L"wb") == 0);
        fputs("this is not a dll", fp);
        fclose(fp);

        HMODULE module = quietload::LoadLibraryQuietly(path.c_str());
        const DWORD err = ::GetLastError();
        ::DeleteFileW(path.c_str());
        CHECK(module == NULL);
        CHECK(err == ERROR_BAD_EXE_FORMAT);
        CHECK(::GetThreadErrorMode() == modeBefore);
    }

    SECTION("路径不存在")
    {
        const std::wstring missing = GetTestExeDirectory() + L"no_such_dir\\FlamingoLang.dll";
        HMODULE module = quietload::LoadLibraryQuietly(missing.c_str());
        const DWORD err = ::GetLastError();
        CHECK(module == NULL);
        CHECK(err == ERROR_MOD_NOT_FOUND);
        CHECK(::GetThreadErrorMode() == modeBefore);
    }
}

namespace {

//I18：「恢复默认」按钮的设计宽度（像素），与 BalloonSystemSettingDlg.cpp 的 kBtnWidth 相同
const int kI18DesignW = 88;
//I18：要检查的显示器 DPI，依次为 100%、125%、150%、175%、200% 缩放
const int kI18Dpis[] = { 96, 120, 144, 168, 192 };
//I18：英文界面下「恢复默认」的译文（快捷键页截图里被截断的那一个）
const wchar_t* const kI18RestoreText = L"Restore default";

//I18 枚举显示器的回调：把每块显示器中心点所在显示器的 DPI 追加进 data 指向的列表
BOOL CALLBACK I18CollectMonitorDpi(HMONITOR /*monitor*/, HDC /*hdc*/, LPRECT rc, LPARAM data)
{
    std::vector<int>* dpis = reinterpret_cast<std::vector<int>*>(data);
    POINT center;
    center.x = rc->left + (rc->right - rc->left) / 2;
    center.y = rc->top + (rc->bottom - rc->top) / 2;
    dpis->push_back(balloonwjui::DuiDpi::GetDpiForPoint(center));
    return TRUE;
}

} // namespace

//改动前（按系统 DPI 量）的实测，本机系统 DPI 96：按钮 95 像素；显示器 DPI 为 120、144、168、192 时文字加留白分别要
//117、136、157、178 像素，四档都放不下（tmp/verify7/step2b_before.log）。改为按所有显示器中最高的 DPI 量之后，本用例检查
//测量的换算规则与「取最高 DPI」这一步；多显示器、不同缩放的组合无法在测试里模拟，由各档 DPI 的检查覆盖。
TEST_CASE("I18 按钮宽度按所有显示器中最高的 DPI 量，窗口放在哪块显示器上都放得下译文", "[i18n][i18ndpi]")
{
    ScopedI18nReset resetOnExit;

    SECTION("按某档 DPI 量出的按钮宽度放得下该 DPI 下绘制的文字，DPI 越高越宽")
    {
        FreshState();
        i18n::InitFromHost(&FakeHostTranslate, L"en-US", L"Segoe UI");
        REQUIRE_FALSE(i18n::IsSourceLanguage());
        int previous = 0;
        for (size_t i = 0; i < sizeof(kI18Dpis) / sizeof(kI18Dpis[0]); ++i)
        {
            const int dpi = kI18Dpis[i];
            const int buttonW = i18n::DefaultButtonWidthForTextAtDpi(kI18RestoreText, kI18DesignW, dpi);
            //按钮绘制时的字号按窗口所在显示器的 DPI 换算（DuiControl::GetDefaultFont → DuiResMgr::GetDefaultFontForDpi）
            const int need = i18n::MeasureUiTextWidth(kI18RestoreText, i18n::DefaultFontPixelHeightForDpi(dpi), false)
                + i18n::kDefaultButtonTextPadPx;
            INFO("monitor dpi " << dpi << ", button width " << buttonW << ", text width + padding " << need);
            CHECK(buttonW >= need);
            CHECK(buttonW >= previous);
            previous = buttonW;
        }
    }

    SECTION("最高 DPI 不低于系统 DPI，也不低于任何一块显示器的 DPI")
    {
        std::vector<int> dpis;
        ::EnumDisplayMonitors(NULL, NULL, I18CollectMonitorDpi, reinterpret_cast<LPARAM>(&dpis));
        REQUIRE_FALSE(dpis.empty());
        int expected = balloonwjui::DuiDpi::GetSystemDpi();
        for (size_t i = 0; i < dpis.size(); ++i)
        {
            if (dpis[i] > expected)
                expected = dpis[i];
        }
        INFO("system dpi " << balloonwjui::DuiDpi::GetSystemDpi() << ", monitors " << dpis.size()
             << ", max " << i18n::MaxMonitorDpi());
        CHECK(i18n::MaxMonitorDpi() == expected);
    }

    SECTION("测量用的字高与按钮宽度都按最高 DPI 换算")
    {
        FreshState();
        i18n::InitFromHost(&FakeHostTranslate, L"en-US", L"Segoe UI");
        const int maxDpi = i18n::MaxMonitorDpi();
        const int pointSize = balloonwjui::DuiTheme::Inst().GetDefaultFontPt();
        CHECK(i18n::PointToPixelHeight(pointSize) == i18n::PointToPixelHeightForDpi(pointSize, maxDpi));
        CHECK(i18n::DefaultFontPixelHeight() == i18n::DefaultFontPixelHeightForDpi(maxDpi));
        CHECK(i18n::DefaultButtonWidthForText(kI18RestoreText, kI18DesignW)
              == i18n::DefaultButtonWidthForTextAtDpi(kI18RestoreText, kI18DesignW, maxDpi));
    }

    SECTION("按显示器感知 DPI 时取到显示器的真实 DPI，按钮放得下该 DPI 下绘制的译文")
    {
        //单元测试程序没有声明 DPI 感知，上面几段取到的都是 96；真实客户端按显示器感知，在 125% 缩放的显示器上是 120。
        //切换到与客户端相同的上下文再检查一次（2026-10-07 补，见 TestDpiAwareScope.h）
        TestDpiAwareScope dpiAware;
        FreshState();
        i18n::InitFromHost(&FakeHostTranslate, L"en-US", L"Segoe UI");
        std::vector<int> dpis;
        ::EnumDisplayMonitors(NULL, NULL, I18CollectMonitorDpi, reinterpret_cast<LPARAM>(&dpis));
        REQUIRE_FALSE(dpis.empty());
        int expected = balloonwjui::DuiDpi::GetSystemDpi();
        for (size_t i = 0; i < dpis.size(); ++i)
        {
            if (dpis[i] > expected)
                expected = dpis[i];
        }
        const int maxDpi = i18n::MaxMonitorDpi();
        WARN("per-monitor aware: scope active " << (dpiAware.Active() ? 1 : 0) << ", max monitor dpi " << maxDpi);
        CHECK(maxDpi == expected);
        const int buttonW = i18n::DefaultButtonWidthForText(kI18RestoreText, kI18DesignW);
        const int need = i18n::MeasureUiTextWidth(kI18RestoreText, i18n::DefaultFontPixelHeightForDpi(maxDpi), false)
            + i18n::kDefaultButtonTextPadPx;
        INFO("button width " << buttonW << ", text width + padding " << need);
        CHECK(buttonW >= need);
    }

    SECTION("简体中文界面不论 DPI 都保持设计宽度")
    {
        FreshState();
        i18n::InitFromHost(&FakeHostTranslate, L"zh-CN", L"Microsoft YaHei");
        REQUIRE(i18n::IsSourceLanguage());
        CHECK(i18n::DefaultButtonWidthForTextAtDpi(kI18RestoreText, kI18DesignW, kI18Dpis[4]) == kI18DesignW);
        CHECK(i18n::DefaultButtonWidthForText(kI18RestoreText, kI18DesignW) == kI18DesignW);
    }
}
