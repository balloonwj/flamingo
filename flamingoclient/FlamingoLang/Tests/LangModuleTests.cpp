/**
 *  FlamingoLang.dll 的语言包模块（CLangModule）、导出函数表与伪语言的单元测试，用例 LD12、LD13、LD15、LD16。
 *
 *  用例清单（2026-10-03 与用户确认，见 docs/多语言方案.md 第 11 节 1a）：
 *    - LD12 函数表：cbSize 正确；恰好 4 种语言，代码、本族语名称、字体候选列表正确
 *    - LD13 选定语言：不存在的语言返回失败；未选定时查找返回 NULL；选定后返回该语言译文；
 *           换选另一种语言后，此前拿到的指针仍然有效
 *    - LD15 伪语言 qps-ploc：不在语言列表里但能按代码选定；格式为「«原文 + 填充»」，每个宽字符补两个 '~'；
 *           {0} 与 & 原样保留；同一原文两次查找返回同一指针；微软雅黑里有括号与填充字符的字形
 *    - LD16 多线程同时查找（含伪语言、含另一线程反复切换语言）结果正确、不崩溃
 *
 *  测试经 GetLangApiTable() 取得与 DLL 导出完全相同的函数表，经 SetPoSource 换成内存中的译文数据。
 *  CLangModule 是进程内单例，每个用例开头先 ResetForTest。框架 Catch2 v3，标签 [langdll]。
 *  balloonwj@qq.com   2026-10-03
 */
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <string.h>
#include <string>
#include <thread>
#include <vector>

#include "../FlamingoLangApi.h"
#include "../LangModule.h"
#include "../PseudoLang.h"

using flamingolang::CLangModule;
using flamingolang::CPseudoLang;
using flamingolang::LangApi;
using flamingolang::LangInfo;
using flamingolang::LangStats;

namespace {

//测试用的英文译文：两条有效、一条空译文
const char kEnPo[] =
    u8"msgid \"发送\"\n"
    u8"msgstr \"Send\"\n"
    u8"\n"
    u8"msgid \"取消\"\n"
    u8"msgstr \"Cancel\"\n"
    u8"\n"
    u8"msgid \"未翻译\"\n"
    u8"msgstr \"\"\n";

//测试用的日文译文
const char kJaPo[] =
    u8"msgid \"发送\"\n"
    u8"msgstr \"送信\"\n"
    u8"\n"
    u8"msgid \"取消\"\n"
    u8"msgstr \"キャンセル\"\n";

//测试用的韩文译文：一条有效、一条坏条目，用来检查统计信息里的错误数
const char kKoPo[] =
    u8"msgid \"发送\"\n"
    u8"msgstr \"보내기\"\n"
    u8"\n"
    u8"msgid \"坏条目\n"
    u8"msgstr \"Bad\"\n";

//内存数据来源被调用的次数，用来确认同一语言不会重复解析
volatile LONG g_sourceCalls = 0;

//内存中的译文数据来源：en-US、ja-JP、ko-KR 有数据，zh-TW 故意取不到
bool TestPoSource(const wchar_t* code, int resourceId, const char** data, size_t* size)
{
    (void)resourceId;
    ::InterlockedIncrement(&g_sourceCalls);
    if (wcscmp(code, L"en-US") == 0)
    {
        *data = kEnPo;
        *size = sizeof(kEnPo) - 1;
        return true;
    }
    if (wcscmp(code, L"ja-JP") == 0)
    {
        *data = kJaPo;
        *size = sizeof(kJaPo) - 1;
        return true;
    }
    if (wcscmp(code, L"ko-KR") == 0)
    {
        *data = kKoPo;
        *size = sizeof(kKoPo) - 1;
        return true;
    }
    return false;
}

//把模块恢复到初始状态并换上内存数据来源，返回函数表
const LangApi* FreshApi()
{
    CLangModule::Instance().ResetForTest();
    CLangModule::Instance().SetPoSource(TestPoSource);
    g_sourceCalls = 0;
    return flamingolang::GetLangApiTable();
}

//一种语言在语言包里应有的登记信息
struct ExpectedLang
{
    const wchar_t* m_code;
    const wchar_t* m_nativeName;
    const wchar_t* m_fonts[flamingolang::kMaxFontCandidates];
    uint32_t       m_fontCount;
};

const ExpectedLang kExpectedLangs[] =
{
    { L"zh-TW", L"繁體中文", { L"Microsoft JhengHei", L"PMingLiU", L"Microsoft YaHei" }, 3 },
    { L"en-US", L"English",  { L"Segoe UI", L"Microsoft YaHei" }, 2 },
    { L"ja-JP", L"日本語",   { L"Yu Gothic UI", L"Meiryo UI", L"Meiryo", L"MS UI Gothic", L"Microsoft YaHei" }, 5 },
    { L"ko-KR", L"한국어",   { L"Malgun Gothic", L"Gulim", L"Microsoft YaHei" }, 3 },
};
const uint32_t kExpectedLangCount = sizeof(kExpectedLangs) / sizeof(kExpectedLangs[0]);

bool TextEquals(const wchar_t* got, const wchar_t* expect)
{
    return got != NULL && expect != NULL && wcscmp(got, expect) == 0;
}

//LD16 各线程的查找次数
const int kLookupsPerThread = 20000;
//LD16 并发查找的线程数
const int kLookupThreads = 4;
//LD16 伪语言阶段使用的不同原文条数
const int kPseudoKeyCount = 50;
//LD16 切换语言的次数
const int kSwitchRounds = 2000;

//真实语言阶段：反复查找并核对结果，不符的次数累加到 *mismatches
struct RealLookupArgs
{
    const LangApi* m_api;
    volatile LONG* m_mismatches;
};

void RealLookupWorker(RealLookupArgs args)
{
    for (int i = 0; i < kLookupsPerThread; ++i)
    {
        int which = i % 3;
        const wchar_t* got = NULL;
        bool ok = false;
        if (which == 0)
        {
            got = args.m_api->m_lookup(NULL, L"发送");
            ok = TextEquals(got, L"Send");
        }
        else if (which == 1)
        {
            got = args.m_api->m_lookup(NULL, L"取消");
            ok = TextEquals(got, L"Cancel");
        }
        else
        {
            got = args.m_api->m_lookup(NULL, L"不存在");
            ok = (got == NULL);
        }
        if (!ok)
            ::InterlockedIncrement(args.m_mismatches);
    }
}

//伪语言阶段：每个线程按不同的起点遍历全部原文，记下每条原文得到的指针
struct PseudoLookupArgs
{
    const LangApi*                      m_api;
    const std::vector<std::wstring>*    m_keys;
    std::vector<const wchar_t*>*        m_results;  //下标与 m_keys 对应
    int                                 m_offset;
};

void PseudoLookupWorker(PseudoLookupArgs args)
{
    const int n = (int)args.m_keys->size();
    for (int round = 0; round < kLookupsPerThread / kPseudoKeyCount; ++round)
    {
        for (int i = 0; i < n; ++i)
        {
            int k = (i + args.m_offset) % n;
            (*args.m_results)[k] = args.m_api->m_lookup(NULL, (*args.m_keys)[k].c_str());
        }
    }
}

//切换阶段：一个线程反复在英文与日文之间切换
void SwitchWorker(const LangApi* api)
{
    for (int i = 0; i < kSwitchRounds; ++i)
        api->m_selectLanguage((i % 2 == 0) ? L"ja-JP" : L"en-US");
}

//切换阶段的查找线程：结果只能是英文或日文译文之一
void SwitchLookupWorker(RealLookupArgs args)
{
    for (int i = 0; i < kLookupsPerThread; ++i)
    {
        const wchar_t* got = args.m_api->m_lookup(NULL, L"发送");
        if (!TextEquals(got, L"Send") && !TextEquals(got, L"送信"))
            ::InterlockedIncrement(args.m_mismatches);
    }
}

} // namespace

TEST_CASE("LD12 函数表与语言列表", "[langdll]")
{
    const LangApi* api = FreshApi();
    REQUIRE(api != NULL);
    CHECK(api->m_cbSize == sizeof(LangApi));
    CHECK(api->m_cbSize >= flamingolang::kLangApiMinSize);
    CHECK(flamingolang::LangApiHasField(api, offsetof(LangApi, m_getStats) + sizeof(api->m_getStats)));
    REQUIRE(api->m_getPackVersion != NULL);
    REQUIRE(api->m_getLanguageCount != NULL);
    REQUIRE(api->m_getLanguageInfo != NULL);
    REQUIRE(api->m_selectLanguage != NULL);
    REQUIRE(api->m_lookup != NULL);
    REQUIRE(api->m_getStats != NULL);

    const wchar_t* version = api->m_getPackVersion();
    REQUIRE(version != NULL);
    CHECK(wcslen(version) > 0);

    REQUIRE(api->m_getLanguageCount() == kExpectedLangCount);
    for (uint32_t i = 0; i < kExpectedLangCount; ++i)
    {
        const ExpectedLang& expect = kExpectedLangs[i];
        const LangInfo* info = api->m_getLanguageInfo(i);
        REQUIRE(info != NULL);
        CHECK(info->m_cbSize == sizeof(LangInfo));
        CHECK(TextEquals(info->m_code, expect.m_code));
        CHECK(TextEquals(info->m_nativeName, expect.m_nativeName));
        REQUIRE(info->m_fontCount == expect.m_fontCount);
        for (uint32_t f = 0; f < expect.m_fontCount; ++f)
            CHECK(TextEquals(info->m_fonts[f], expect.m_fonts[f]));
        //伪语言不出现在列表里
        CHECK(wcscmp(info->m_code, FLAMINGO_LANG_PSEUDO_CODE) != 0);
    }
    CHECK(api->m_getLanguageInfo(kExpectedLangCount) == NULL);
}

TEST_CASE("LD13 选定语言", "[langdll]")
{
    const LangApi* api = FreshApi();

    //尚未选定：查不到，也没有统计信息
    CHECK(api->m_lookup(NULL, L"发送") == NULL);
    LangStats stats;
    stats.m_cbSize = sizeof(stats);
    CHECK_FALSE(api->m_getStats(&stats));
    CHECK_FALSE(api->m_getStats(NULL));

    //参数错误与语言包里没有的语言
    CHECK(api->m_selectLanguage(NULL) == flamingolang::lang_select_bad_argument);
    CHECK(api->m_selectLanguage(L"") == flamingolang::lang_select_bad_argument);
    CHECK(api->m_selectLanguage(L"fr-FR") == flamingolang::lang_select_unknown);
    CHECK(api->m_selectLanguage(L"zh-CN") == flamingolang::lang_select_unknown);

    //有这种语言但取不到数据：当前选定保持不变（仍是未选定）
    CHECK(api->m_selectLanguage(L"zh-TW") == flamingolang::lang_select_no_data);
    CHECK(api->m_lookup(NULL, L"发送") == NULL);

    //选定英文
    REQUIRE(api->m_selectLanguage(L"en-US") == flamingolang::lang_select_ok);
    const wchar_t* sendEn = api->m_lookup(NULL, L"发送");
    CHECK(TextEquals(sendEn, L"Send"));
    CHECK(TextEquals(api->m_lookup(L"", L"取消"), L"Cancel"));
    CHECK(api->m_lookup(NULL, L"未翻译") == NULL);
    CHECK(api->m_lookup(NULL, L"不存在") == NULL);
    REQUIRE(api->m_getStats(&stats));
    CHECK(stats.m_entries == 2);
    CHECK(stats.m_errors == 0);
    CHECK(stats.m_warnings == 0);

    //取数据失败不影响当前选定
    CHECK(api->m_selectLanguage(L"zh-TW") == flamingolang::lang_select_no_data);
    CHECK(TextEquals(api->m_lookup(NULL, L"发送"), L"Send"));

    //换成日文：查到日文，先前拿到的英文指针仍然有效
    REQUIRE(api->m_selectLanguage(L"ja-JP") == flamingolang::lang_select_ok);
    CHECK(TextEquals(api->m_lookup(NULL, L"发送"), L"送信"));
    CHECK(TextEquals(sendEn, L"Send"));

    //再换回英文：不重新解析，返回的还是同一个指针；语言代码不区分大小写
    LONG callsBefore = g_sourceCalls;
    REQUIRE(api->m_selectLanguage(L"en-us") == flamingolang::lang_select_ok);
    CHECK(api->m_lookup(NULL, L"发送") == sendEn);
    CHECK(g_sourceCalls == callsBefore);

    //统计信息里带出坏条目数；m_cbSize 小于完整结构时只填写其范围内的字段
    REQUIRE(api->m_selectLanguage(L"ko-KR") == flamingolang::lang_select_ok);
    REQUIRE(api->m_getStats(&stats));
    CHECK(stats.m_entries == 1);
    CHECK(stats.m_errors == 1);
    LangStats partial;
    partial.m_cbSize = (uint32_t)(offsetof(LangStats, m_entries) + sizeof(partial.m_entries));
    partial.m_errors = 0xFFFFFFFFu;
    partial.m_warnings = 0xFFFFFFFFu;
    REQUIRE(api->m_getStats(&partial));
    CHECK(partial.m_entries == 1);
    CHECK(partial.m_errors == 0xFFFFFFFFu);
    CHECK(partial.m_warnings == 0xFFFFFFFFu);
}

TEST_CASE("LD15 伪语言", "[langdll]")
{
    const LangApi* api = FreshApi();

    //伪语言不需要数据来源
    CLangModule::Instance().SetPoSource(NULL);
    REQUIRE(api->m_selectLanguage(FLAMINGO_LANG_PSEUDO_CODE) == flamingolang::lang_select_ok);

    //每个宽字符补两个 '~'，前后加 « »
    const wchar_t* login = api->m_lookup(NULL, L"记住密码");
    CHECK(TextEquals(login, L"«记住密码~~~~~~~~»"));

    //占位符与助记符原样保留；ASCII 字符不补填充
    CHECK(TextEquals(api->m_lookup(NULL, L"已选择 {0} 个(&S)"), L"«已选择 {0} 个(&S)~~~~~~~~»"));
    CHECK(TextEquals(api->m_lookup(NULL, L"Ctrl+Enter 发送"), L"«Ctrl+Enter 发送~~~~»"));

    //全角标点、假名与谚文同样按宽字符处理
    CHECK(CPseudoLang::MakePseudoText(L"取消？") == std::wstring(L"«取消？~~~~~~»"));
    CHECK(CPseudoLang::MakePseudoText(L"ログ한") == std::wstring(L"«ログ한~~~~~~»"));
    CHECK(CPseudoLang::MakePseudoText(L"") == std::wstring(L"«»"));

    //同一原文两次查找返回同一指针；上下文不影响结果
    CHECK(api->m_lookup(NULL, L"记住密码") == login);
    CHECK(TextEquals(api->m_lookup(L"按钮", L"记住密码"), L"«记住密码~~~~~~~~»"));
    CHECK(api->m_lookup(NULL, NULL) == NULL);

    //界面字体微软雅黑里有括号与填充字符的字形，伪译文不会显示成方框
    HDC hdc = ::GetDC(NULL);
    const int kProbeFontPx = 15;
    HFONT font = ::CreateFontW(-kProbeFontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH,
                               L"Microsoft YaHei");
    HGDIOBJ old = ::SelectObject(hdc, font);
    const wchar_t probe[] = L"«»~";
    WORD glyphs[3] = { 0, 0, 0 };
    REQUIRE(::GetGlyphIndicesW(hdc, probe, 3, glyphs, GGI_MARK_NONEXISTING_GLYPHS) == 3);
    ::SelectObject(hdc, old);
    ::DeleteObject(font);
    ::ReleaseDC(NULL, hdc);
    CHECK(glyphs[0] != 0xFFFF);
    CHECK(glyphs[1] != 0xFFFF);
    CHECK(glyphs[2] != 0xFFFF);
}

TEST_CASE("LD16 多线程同时查找", "[langdll]")
{
    const LangApi* api = FreshApi();

    SECTION("真实语言：4 个线程并发查找")
    {
        REQUIRE(api->m_selectLanguage(L"en-US") == flamingolang::lang_select_ok);
        volatile LONG mismatches = 0;
        std::vector<std::thread> threads;
        for (int t = 0; t < kLookupThreads; ++t)
        {
            RealLookupArgs args;
            args.m_api = api;
            args.m_mismatches = &mismatches;
            threads.push_back(std::thread(RealLookupWorker, args));
        }
        for (size_t t = 0; t < threads.size(); ++t)
            threads[t].join();
        CHECK(mismatches == 0);
    }

    SECTION("伪语言：4 个线程并发生成，同一原文得到同一指针")
    {
        REQUIRE(api->m_selectLanguage(FLAMINGO_LANG_PSEUDO_CODE) == flamingolang::lang_select_ok);
        std::vector<std::wstring> keys;
        for (int i = 0; i < kPseudoKeyCount; ++i)
            keys.push_back(L"文字" + std::to_wstring(i));
        std::vector<std::vector<const wchar_t*> > results(kLookupThreads,
                                                          std::vector<const wchar_t*>(keys.size(), (const wchar_t*)NULL));
        std::vector<std::thread> threads;
        for (int t = 0; t < kLookupThreads; ++t)
        {
            PseudoLookupArgs args;
            args.m_api = api;
            args.m_keys = &keys;
            args.m_results = &results[t];
            args.m_offset = t * (kPseudoKeyCount / kLookupThreads);
            threads.push_back(std::thread(PseudoLookupWorker, args));
        }
        for (size_t t = 0; t < threads.size(); ++t)
            threads[t].join();

        for (size_t k = 0; k < keys.size(); ++k)
        {
            REQUIRE(results[0][k] != NULL);
            CHECK(std::wstring(results[0][k]) == CPseudoLang::MakePseudoText(keys[k].c_str()));
            for (int t = 1; t < kLookupThreads; ++t)
                CHECK(results[t][k] == results[0][k]);
        }
    }

    SECTION("一个线程反复切换语言，其余线程同时查找")
    {
        REQUIRE(api->m_selectLanguage(L"en-US") == flamingolang::lang_select_ok);
        REQUIRE(api->m_selectLanguage(L"ja-JP") == flamingolang::lang_select_ok);
        volatile LONG mismatches = 0;
        std::vector<std::thread> threads;
        threads.push_back(std::thread(SwitchWorker, api));
        for (int t = 0; t < kLookupThreads - 1; ++t)
        {
            RealLookupArgs args;
            args.m_api = api;
            args.m_mismatches = &mismatches;
            threads.push_back(std::thread(SwitchLookupWorker, args));
        }
        for (size_t t = 0; t < threads.size(); ++t)
            threads[t].join();
        CHECK(mismatches == 0);
    }
}
