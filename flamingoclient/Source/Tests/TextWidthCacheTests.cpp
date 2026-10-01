/**
 *  文字宽度缓存的单元测试（Suite TWC），TextWidthCacheTests.cpp
 *
 *  覆盖 balloonmain::TextWidthCache：
 *    TWC1  同一字体、同一段文字量两次 —— 宽度相同，测量函数只调用一次；
 *    TWC2  换了字体句柄 —— 缓存作废，重新测量；
 *    TWC3  条目超过上限 —— 清空后继续正常工作，结果仍然正确；
 *    TWC4  空文字或 NULL —— 直接返回 0，不调用测量函数；测量失败（返回 0）不缓存。
 *
 *  测量函数用替身：宽度 = 字符数 × 固定值，并按字体句柄加一个偏移，便于区分字体；
 *  另记录调用次数。字体句柄只作为区分用的键，不会被选入任何 DC，故用伪造的值即可。
 *
 *  框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"
#include "../UI/BalloonMain/TextWidthCache.h"

using balloonmain::TextWidthCache;

namespace {

// 替身的每字符宽度（像素）。
const int kFakeCharWidth = 7;

// 两个伪造的字体句柄，只用来当键。
const HFONT kFontA = (HFONT)(ULONG_PTR)0x1000;
const HFONT kFontB = (HFONT)(ULONG_PTR)0x2000;

// 替身被调用的次数，各用例开头清零。
int g_measureCalls = 0;

// 替身是否模拟"测量失败"。
bool g_measureFails = false;

// 测量函数替身：宽度 = 字符数 × 7，字体 B 再加 100，便于看出用的是哪个字体。
int FakeMeasure(HFONT font, LPCWSTR /*text*/, int len)
{
    ++g_measureCalls;
    if (g_measureFails)
    {
        return 0;
    }
    return len * kFakeCharWidth + ((font == kFontB) ? 100 : 0);
}

// 每个用例开头调用。
void ResetFake()
{
    g_measureCalls = 0;
    g_measureFails = false;
}

} // namespace

TEST_CASE("TWC1 同一字体、同一段文字量两次:宽度相同,只测量一次", "[textwidth]")
{
    ResetFake();
    TextWidthCache cache(FakeMeasure);

    CHECK(cache.Width(kFontA, L"23:56") == 5 * kFakeCharWidth);
    CHECK(cache.Width(kFontA, L"23:56") == 5 * kFakeCharWidth);
    CHECK(g_measureCalls == 1);

    // 不同的文字各自测量一次。
    CHECK(cache.Width(kFontA, L" ") == kFakeCharWidth);
    CHECK(g_measureCalls == 2);
    CHECK(cache.Count() == 2);
}

TEST_CASE("TWC2 换了字体句柄:缓存作废,重新测量", "[textwidth]")
{
    ResetFake();
    TextWidthCache cache(FakeMeasure);

    CHECK(cache.Width(kFontA, L"昨天") == 2 * kFakeCharWidth);
    CHECK(cache.Width(kFontB, L"昨天") == 2 * kFakeCharWidth + 100);
    CHECK(g_measureCalls == 2);

    // 字体 B 下的结果已缓存；而字体 A 的旧结果随字体切换作废，切回去要重新测量。
    CHECK(cache.Width(kFontB, L"昨天") == 2 * kFakeCharWidth + 100);
    CHECK(g_measureCalls == 2);
    CHECK(cache.Width(kFontA, L"昨天") == 2 * kFakeCharWidth);
    CHECK(g_measureCalls == 3);
    CHECK(cache.Count() == 1);
}

TEST_CASE("TWC3 条目超过上限:清空后继续正常工作", "[textwidth]")
{
    ResetFake();
    const size_t kMax = 3;
    TextWidthCache cache(FakeMeasure, kMax);

    const wchar_t* const kTexts[] = { L"a", L"bb", L"ccc", L"dddd", L"eeeee" };
    for (size_t i = 0; i < _countof(kTexts); ++i)
    {
        CHECK(cache.Width(kFontA, kTexts[i]) == (int)(i + 1) * kFakeCharWidth);
        CHECK(cache.Count() <= kMax);
    }
    CHECK(g_measureCalls == 5);

    // 刚加入的仍在缓存里，不必重新测量；结果始终正确。
    CHECK(cache.Width(kFontA, L"eeeee") == 5 * kFakeCharWidth);
    CHECK(g_measureCalls == 5);
    CHECK(cache.Width(kFontA, L"a") == kFakeCharWidth);

    cache.Clear();
    CHECK(cache.Count() == 0);
}

TEST_CASE("TWC4 空文字或 NULL 直接返回 0;测量失败不缓存", "[textwidth]")
{
    ResetFake();
    TextWidthCache cache(FakeMeasure);

    CHECK(cache.Width(kFontA, NULL) == 0);
    CHECK(cache.Width(kFontA, L"") == 0);
    CHECK(g_measureCalls == 0);
    CHECK(cache.Count() == 0);

    // 测量失败：返回 0 且不缓存，恢复后再取会重新测量并得到正确值。
    g_measureFails = true;
    CHECK(cache.Width(kFontA, L"23:56") == 0);
    CHECK(cache.Count() == 0);
    g_measureFails = false;
    CHECK(cache.Width(kFontA, L"23:56") == 5 * kFakeCharWidth);
    CHECK(g_measureCalls == 2);
}
