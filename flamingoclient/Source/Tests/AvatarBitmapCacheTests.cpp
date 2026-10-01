/**
 *  头像位图缓存的单元测试（Suite ABC），AvatarBitmapCacheTests.cpp
 *
 *  覆盖 balloonmain::CAvatarBitmapCache：
 *    ABC1  同样的首字、底色、边长取两次 —— 同一个位图，只生成一次；
 *    ABC2  首字、底色、边长任一不同 —— 各自生成；
 *    ABC3  真图头像按"路径 + 边长"缓存，换边长另生成；
 *    ABC4  生成失败 —— 返回 NULL 且不缓存，下次重新生成；
 *    ABC5  Clear —— 全部位图被释放，条目数归零；
 *    ABC6  字形头像与真图头像的键不会互相混淆。
 *
 *  生成函数用替身：只建一个 1x1 的位图并计数，不走 GDI+，也不读文件。用例据计数判断
 *  是否命中缓存，据本进程 GDI 对象数的增减判断位图是否已被释放（不用 GetObjectType 的
 *  原因见 GdiObjectCount 的注释）。
 *
 *  框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-09-30
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"
#include "../UI/BalloonMain/AvatarBitmapCache.h"

using balloonmain::CAvatarBitmapCache;

namespace {

// 替身被调用的次数。每个用例开头清零。
int g_monogramCalls = 0;
int g_imageCalls    = 0;

// 替身是否模拟"生成失败"。
bool g_monogramFails = false;

// 用例里用到的边长（像素），与会话列表、通讯录的头像边长同值。
const int kSizeConv    = 36;
const int kSizeContact = 32;

// 生成一个 1x1 的位图代替真实头像，仅用于计数与释放检查。
HBITMAP MakeDummyBitmap()
{
    return ::CreateBitmap(1, 1, 1, 32, NULL);
}

// 字形头像的替身。
HBITMAP FakeMonogram(LPCWSTR /*text*/, COLORREF /*bgColor*/, int /*sizePx*/)
{
    ++g_monogramCalls;
    if (g_monogramFails)
    {
        return NULL;
    }
    return MakeDummyBitmap();
}

// 真图头像的替身。
HBITMAP FakeImage(LPCWSTR /*path*/, int /*sizePx*/, int /*radiusPx*/)
{
    ++g_imageCalls;
    return MakeDummyBitmap();
}

// 每个用例开头调用，清零计数与失败开关。
void ResetFakes()
{
    g_monogramCalls = 0;
    g_imageCalls    = 0;
    g_monogramFails = false;
}

// 本进程当前持有的 GDI 对象数。用来判断位图是否真的被释放 —— 不能用 GetObjectType：
// 2026-09-30 在 Windows 10 上实测，DeleteObject 成功返回之后，GetObjectType 对该句柄
// 仍然返回 OBJ_BITMAP，据它判断会误认为位图没有释放。
DWORD GdiObjectCount()
{
    return ::GetGuiResources(::GetCurrentProcess(), GR_GDIOBJECTS);
}

} // namespace

TEST_CASE("ABC1 同样的首字、底色、边长取两次:同一个位图,只生成一次", "[avatarcache]")
{
    ResetFakes();
    CAvatarBitmapCache cache(FakeMonogram, FakeImage);

    HBITMAP a = cache.GetMonogram(L"张", RGB(1, 2, 3), kSizeConv);
    HBITMAP b = cache.GetMonogram(L"张", RGB(1, 2, 3), kSizeConv);

    REQUIRE(a != NULL);
    CHECK(a == b);
    CHECK(g_monogramCalls == 1);
    CHECK(cache.Count() == 1);
}

TEST_CASE("ABC2 首字、底色、边长任一不同:各自生成", "[avatarcache]")
{
    ResetFakes();
    CAvatarBitmapCache cache(FakeMonogram, FakeImage);

    HBITMAP base      = cache.GetMonogram(L"张", RGB(1, 2, 3), kSizeConv);
    HBITMAP diffGlyph = cache.GetMonogram(L"王", RGB(1, 2, 3), kSizeConv);
    HBITMAP diffColor = cache.GetMonogram(L"张", RGB(9, 2, 3), kSizeConv);
    HBITMAP diffSize  = cache.GetMonogram(L"张", RGB(1, 2, 3), kSizeContact);

    REQUIRE(base != NULL);
    CHECK(diffGlyph != base);
    CHECK(diffColor != base);
    CHECK(diffSize != base);
    CHECK(diffGlyph != diffColor);
    CHECK(g_monogramCalls == 4);
    CHECK(cache.Count() == 4);

    // 空首字（只画底色圆）也是一个合法的键，且与非空首字不同。
    HBITMAP empty = cache.GetMonogram(L"", RGB(1, 2, 3), kSizeConv);
    HBITMAP nullGlyph = cache.GetMonogram(NULL, RGB(1, 2, 3), kSizeConv);
    CHECK(empty != NULL);
    CHECK(empty != base);
    CHECK(nullGlyph == empty);
    CHECK(g_monogramCalls == 5);
}

TEST_CASE("ABC3 真图头像按路径与边长缓存,换边长另生成", "[avatarcache]")
{
    ResetFakes();
    CAvatarBitmapCache cache(FakeMonogram, FakeImage);

    const wchar_t* kPath = L"C:\\Users\\a\\Avatar\\abc123";
    HBITMAP a = cache.GetImage(kPath, kSizeConv);
    HBITMAP b = cache.GetImage(kPath, kSizeConv);
    HBITMAP c = cache.GetImage(kPath, kSizeContact);
    HBITMAP d = cache.GetImage(L"C:\\Users\\a\\Avatar\\other", kSizeConv);

    REQUIRE(a != NULL);
    CHECK(a == b);
    CHECK(c != a);
    CHECK(d != a);
    CHECK(g_imageCalls == 3);
    CHECK(g_monogramCalls == 0);

    // 非法参数不调用生成函数。
    CHECK(cache.GetImage(NULL, kSizeConv) == NULL);
    CHECK(cache.GetImage(L"", kSizeConv) == NULL);
    CHECK(cache.GetImage(kPath, 0) == NULL);
    CHECK(g_imageCalls == 3);
}

TEST_CASE("ABC4 生成失败:返回 NULL 且不缓存,下次重新生成", "[avatarcache]")
{
    ResetFakes();
    CAvatarBitmapCache cache(FakeMonogram, FakeImage);

    g_monogramFails = true;
    CHECK(cache.GetMonogram(L"李", RGB(1, 2, 3), kSizeConv) == NULL);
    CHECK(cache.Count() == 0);

    g_monogramFails = false;
    HBITMAP ok = cache.GetMonogram(L"李", RGB(1, 2, 3), kSizeConv);
    CHECK(ok != NULL);
    CHECK(g_monogramCalls == 2);
    CHECK(cache.Count() == 1);

    // 边长非法时直接返回 NULL，不调用生成函数。
    CHECK(cache.GetMonogram(L"李", RGB(1, 2, 3), 0) == NULL);
    CHECK(g_monogramCalls == 2);
}

TEST_CASE("ABC5 Clear 释放全部位图,条目数归零", "[avatarcache]")
{
    ResetFakes();
    const DWORD baseline = GdiObjectCount();
    {
        CAvatarBitmapCache cache(FakeMonogram, FakeImage);
        REQUIRE(cache.GetMonogram(L"赵", RGB(1, 2, 3), kSizeConv) != NULL);
        REQUIRE(cache.GetImage(L"C:\\x\\y", kSizeConv) != NULL);
        REQUIRE(GdiObjectCount() == baseline + 2);

        cache.Clear();
        CHECK(cache.Count() == 0);
        CHECK(GdiObjectCount() == baseline);

        // 清空后再取会重新生成。
        CHECK(cache.GetMonogram(L"赵", RGB(1, 2, 3), kSizeConv) != NULL);
        CHECK(g_monogramCalls == 2);
        CHECK(GdiObjectCount() == baseline + 1);
    }
    // 析构时同样释放。
    CHECK(GdiObjectCount() == baseline);
}

TEST_CASE("ABC6 字形头像与真图头像的键不会互相混淆", "[avatarcache]")
{
    ResetFakes();
    CAvatarBitmapCache cache(FakeMonogram, FakeImage);

    // 路径字符串恰好等于某个首字：两者仍是不同的缓存条目。
    HBITMAP mono  = cache.GetMonogram(L"A", RGB(1, 2, 3), kSizeConv);
    HBITMAP image = cache.GetImage(L"A", kSizeConv);
    CHECK(mono != NULL);
    CHECK(image != NULL);
    CHECK(mono != image);
    CHECK(g_monogramCalls == 1);
    CHECK(g_imageCalls == 1);

    // 首字里带分隔符一类的字符也不会与别的键撞上。
    HBITMAP pipe1 = cache.GetMonogram(L"|", RGB(1, 2, 3), kSizeConv);
    HBITMAP pipe2 = cache.GetMonogram(L"|3", RGB(1, 2, 3), kSizeConv);
    CHECK(pipe1 != pipe2);
    CHECK(cache.Count() == 4);
}
