/**
 *  ShellTypeIconTests.cpp
 *
 *  按类型取本机系统图标（Common/Graphics/ShellTypeIcon）的测试，标签 [shellicon]：
 *    - SHI1：扩展名缓存键的取法（大小写、路径里带点的目录、没有扩展名、以点开头的名字、点在末尾）；
 *    - SHI2：磁盘、文件夹、几种扩展名与没有扩展名的文件都取得到图标下标，磁盘、文件夹与文件的图标互不相同；
 *    - SHI3：同一类型只查询一次（扩展名大小写不同算同一类型），不认识的条目类型按文件处理；
 *    - SHI4：在 32 位位图上按 16 与 48 两种边长绘制，图标区域内有不透明像素、区域外保持原样；下标无效或矩形为空时不画；
 *    - SHI5：生成的位图尺寸正确、带透明度；释放缓存之后照常可用。
 *
 *  外壳的图标查询要求调用线程已初始化 COM：用例自己初始化（线程已按别的模式初始化过时照常可用）。
 *
 *  balloonwj@qq.com   2026-10-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../Common/Graphics/ShellTypeIcon.h"

#include <vector>

namespace {

// 测试用的两种绘制边长（像素）：列表行里的小图标与缩略图视图里的大图标
const int kSmallPx = 16;
const int kLargePx = 48;

// 绘制用的位图边长，比大图标多出一圈，用来检查图标区域外没有被画到
const int kCanvasPx = 64;

// 图标在画布里的左上角偏移
const int kIconOffset = 8;

// 位图每个像素的字节数
const int kBytesPerPixel = 4;

// 像素的透明度在 32 位 BGRA 里的字节序号
const int kAlphaByte = 3;

// 打底用的不透明品红（BGRA），用来判断某个像素有没有被改动
const unsigned int kBackground = 0xFFFF00FFu;

// 在本作用域内初始化 COM，析构时配平；线程已按别的模式初始化过时不再初始化
class ComScope
{
public:
    ComScope()
        : m_hr(::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED))
    {
    }

    ~ComScope()
    {
        if (SUCCEEDED(m_hr))
            ::CoUninitialize();
    }

private:
    HRESULT m_hr;   // CoInitializeEx 的返回值，析构时据它决定要不要配平
};

// 一块 32 位自上而下的画布
class Canvas
{
public:
    explicit Canvas(int px)
        : m_px(px)
        , m_bits(NULL)
        , m_bmp(NULL)
        , m_dc(NULL)
        , m_old(NULL)
    {
        BITMAPINFO bmi;
        ::ZeroMemory(&bmi, sizeof(bmi));
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = px;
        bmi.bmiHeader.biHeight = -px;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* bits = NULL;
        m_bmp = ::CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
        m_bits = static_cast<unsigned int*>(bits);
        m_dc = ::CreateCompatibleDC(NULL);
        m_old = ::SelectObject(m_dc, m_bmp);
        for (int i = 0; i < px * px; ++i)
            m_bits[i] = kBackground;
    }

    ~Canvas()
    {
        ::SelectObject(m_dc, m_old);
        ::DeleteDC(m_dc);
        ::DeleteObject(m_bmp);
    }

    HDC Dc() const { return m_dc; }

    // 统计矩形内被改动过的像素个数
    int ChangedIn(const RECT& rc) const
    {
        int n = 0;
        for (int y = rc.top; y < rc.bottom; ++y)
        {
            for (int x = rc.left; x < rc.right; ++x)
            {
                if (m_bits[y * m_px + x] != kBackground)
                    ++n;
            }
        }
        return n;
    }

    // 统计矩形外被改动过的像素个数
    int ChangedOutside(const RECT& rc) const
    {
        const RECT all = { 0, 0, m_px, m_px };
        return ChangedIn(all) - ChangedIn(rc);
    }

private:
    int           m_px;    // 画布边长，像素
    unsigned int* m_bits;  // 像素数据，属于 m_bmp
    HBITMAP       m_bmp;   // 画布位图
    HDC           m_dc;    // 选入了 m_bmp 的内存设备上下文
    HGDIOBJ       m_old;   // 选入前的位图，析构时选回
};

} // namespace

TEST_CASE("SHI1 扩展名缓存键", "[shellicon]")
{
    CHECK(shellicon::ExtensionKey(L"报告.DOCX") == L".docx");
    CHECK(shellicon::ExtensionKey(L"archive.tar.GZ") == L".gz");
    CHECK(shellicon::ExtensionKey(L"noext") == L"");
    CHECK(shellicon::ExtensionKey(L"末尾是点.") == L"");
    //路径里带点的目录不算扩展名
    CHECK(shellicon::ExtensionKey(L"D:\\v1.2\\readme") == L"");
    CHECK(shellicon::ExtensionKey(L"docs/v1.2/a.TXT") == L".txt");
    //以点开头的名字整体按扩展名处理
    CHECK(shellicon::ExtensionKey(L".gitignore") == L".gitignore");
    CHECK(shellicon::ExtensionKey(L"") == L"");
}

TEST_CASE("SHI2 各类条目都取得到图标下标", "[shellicon]")
{
    ComScope com;
    shellicon::ReleaseCache();
    const int disk = shellicon::TypeIconIndex(shellicon::kItemDisk, L"");
    const int folder = shellicon::TypeIconIndex(shellicon::kItemFolder, L"任意名字");
    const int txt = shellicon::TypeIconIndex(shellicon::kItemFile, L"a.txt");
    const int unknown = shellicon::TypeIconIndex(shellicon::kItemFile, L"a.zzqqxx");
    const int noExt = shellicon::TypeIconIndex(shellicon::kItemFile, L"readme");
    CHECK(disk >= 0);
    CHECK(folder >= 0);
    CHECK(txt >= 0);
    CHECK(unknown >= 0);
    CHECK(noExt >= 0);
    CHECK(disk != folder);
    CHECK(folder != txt);
    CHECK(disk != txt);
}

TEST_CASE("SHI3 同一类型只查询一次", "[shellicon]")
{
    ComScope com;
    shellicon::ReleaseCache();
    REQUIRE(shellicon::CachedTypeCount() == 0);
    const int a = shellicon::TypeIconIndex(shellicon::kItemFile, L"x.txt");
    const int b = shellicon::TypeIconIndex(shellicon::kItemFile, L"D:\\dir\\Y.TXT");
    CHECK(a == b);
    CHECK(shellicon::CachedTypeCount() == 1);
    shellicon::TypeIconIndex(shellicon::kItemFile, L"z.log");
    CHECK(shellicon::CachedTypeCount() == 2);
    //文件夹与磁盘与名字无关
    shellicon::TypeIconIndex(shellicon::kItemFolder, L"甲");
    shellicon::TypeIconIndex(shellicon::kItemFolder, L"乙.txt");
    CHECK(shellicon::CachedTypeCount() == 3);
    //不认识的条目类型按文件处理，与同扩展名的文件共用一个缓存项
    const int odd = shellicon::TypeIconIndex(99, L"w.txt");
    CHECK(odd == a);
    CHECK(shellicon::CachedTypeCount() == 3);
}

TEST_CASE("SHI4 按两种边长绘制", "[shellicon]")
{
    ComScope com;
    const int sizes[] = { kSmallPx, kLargePx };
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i)
    {
        Canvas canvas(kCanvasPx);
        const RECT rc = { kIconOffset, kIconOffset, kIconOffset + sizes[i], kIconOffset + sizes[i] };
        CHECK(shellicon::DrawItemIcon(canvas.Dc(), shellicon::kItemFolder, L"", rc));
        //图标区域内有被画到的像素，区域外原样
        CHECK(canvas.ChangedIn(rc) > 0);
        CHECK(canvas.ChangedOutside(rc) == 0);
    }

    Canvas canvas(kCanvasPx);
    const RECT rc = { kIconOffset, kIconOffset, kIconOffset + kSmallPx, kIconOffset + kSmallPx };
    CHECK_FALSE(shellicon::DrawTypeIcon(canvas.Dc(), -1, rc));
    const RECT empty = { kIconOffset, kIconOffset, kIconOffset, kIconOffset + kSmallPx };
    CHECK_FALSE(shellicon::DrawItemIcon(canvas.Dc(), shellicon::kItemFile, L"a.txt", empty));
    const RECT all = { 0, 0, kCanvasPx, kCanvasPx };
    CHECK(canvas.ChangedIn(all) == 0);
}

TEST_CASE("SHI5 生成位图与释放缓存", "[shellicon]")
{
    ComScope com;
    HBITMAP bmp = shellicon::CreateItemIconBitmap(shellicon::kItemDisk, L"", kLargePx);
    REQUIRE(bmp != NULL);
    BITMAP info;
    ::ZeroMemory(&info, sizeof(info));
    REQUIRE(::GetObject(bmp, sizeof(info), &info) == sizeof(info));
    CHECK(info.bmWidth == kLargePx);
    CHECK(info.bmHeight == kLargePx);
    CHECK(info.bmBitsPixel == 32);
    //图标有透明的边角，也有不透明的主体
    const unsigned char* bits = static_cast<const unsigned char*>(info.bmBits);
    REQUIRE(bits != NULL);
    int opaque = 0;
    int transparent = 0;
    for (int i = 0; i < kLargePx * kLargePx; ++i)
    {
        if (bits[i * kBytesPerPixel + kAlphaByte] == 0)
            ++transparent;
        else
            ++opaque;
    }
    CHECK(opaque > 0);
    CHECK(transparent > 0);
    ::DeleteObject(bmp);

    CHECK(shellicon::CreateItemIconBitmap(shellicon::kItemFile, L"a.txt", 0) == NULL);

    shellicon::ReleaseCache();
    CHECK(shellicon::CachedTypeCount() == 0);
    HBITMAP again = shellicon::CreateItemIconBitmap(shellicon::kItemFile, L"a.txt", kSmallPx);
    CHECK(again != NULL);
    if (again != NULL)
        ::DeleteObject(again);
}
