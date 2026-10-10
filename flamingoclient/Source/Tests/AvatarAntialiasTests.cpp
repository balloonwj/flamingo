/**
 *  AvatarAntialiasTests.cpp
 *
 *  照片头像边缘抗锯齿（2026-10-03）的测试，标签 [avataraa]。照片头像原先都是「先设 GDI+ 裁剪区、
 *  再画图」切出圆角，裁剪区按整像素划分，边缘只有完全不透明与完全透明两种像素，看上去是台阶状
 *  的锯齿。改为用纹理画刷填充圆角路径后，边缘应当出现一圈半透明 / 过渡色的像素：
 *    AA1  MakeRoundedAvatarFromFile 生成圆形头像：圆周上有足够多的半透明像素，中心是原色、四角
 *         完全透明，左右两侧的覆盖程度对称（没有半像素的整体偏移）；
 *    AA2  同一函数生成圆角方形头像：四个圆角上有半透明像素，四角顶点完全透明；
 *    AA3  群聊窗成员行画照片头像：在白底上，四个圆角所在的方块里没有「纯原色紧挨纯白」的硬台阶。
 *         硬边裁剪必然在圆弧上留下这种台阶，抗锯齿则在两者之间总有过渡色。不按过渡色像素的数量判断：
 *         原先的画法用高质量插值缩放照片，照片边缘本身就带过渡色，数量上分辨不出圆角是不是硬边
 *         （2026-10-03 模拟：旧画法每个圆角有 2 处硬台阶，新画法 0 处；见 tmp/aa/aa3_sim.py）；
 *    AA4  单聊窗标题栏图标（照片头像与字形头像两种）：圆周斜向的部分没有「纯头像色紧挨纯标题栏底色」
 *         的硬台阶。只看斜向部分（离圆心横、纵都至少五分之一边长），不看上下左右四个切点附近：那里的
 *         边缘几乎是水平或竖直的，整行有色紧挨整行无色正是平直边缘的正确画法，GDI+ 抗锯齿自带的
 *         约 1/4 像素取样偏差也会在那里留下个别这样的像素对。锯齿表现为斜边上的台阶（2026-10-04 模拟：
 *         旧的字形图标斜向部分有 92 处硬台阶、照片图标 92 处，抗锯齿后 0 处；见 tmp/aa/aa4_old_sim.py）。
 *
 *  只在内存位图上绘制与取像素，不创建窗口。框架 Catch2 v3；全局 _Module 由
 *  BalloonChatLayoutTests.cpp 统一定义。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <cmath>     // fabs：像素到图像中心的距离
#include <cstdlib>   // abs：颜色分量比较
#include <string>
#include <vector>
#include <gdiplus.h>

#include "../UI/BalloonMain/AvatarImageUtil.h"
#include "../UI/BalloonChatWnd/ChatAvatarBitmapCache.h"
#include "../UI/BalloonChatWnd/GroupMemberItem.h"
#include "../UI/BalloonChatWnd/ChatTitleIcon.h"

namespace {

// 测试图片的边长（像素），取上传头像的实际尺寸。
const int kSrcPx = 256;

// 测试图片的颜色：纯色，便于区分「原色」「白色」与两者之间的过渡色。
const COLORREF kPhotoColor = RGB(0xD0, 0x30, 0x40);

// AA1 / AA2 生成的头像边长与圆角半径（像素）。
const int kAvatarPx       = 40;
const int kCircleRadiusPx = kAvatarPx / 2;
const int kRoundRadiusPx  = 8;

// 32 位 DIB 每像素的字节数，及 B / G / R / A 四个分量在像素内的偏移。
const int kBytesPerPixel = 4;
const int kOffsetBlue    = 0;
const int kOffsetGreen   = 1;
const int kOffsetRed     = 2;
const int kOffsetAlpha   = 3;

// 完全不透明的 alpha 值。
const int kAlphaOpaque = 255;

// 圆周上至少应有的半透明像素个数：边长 40 的圆周长约 125 像素，抗锯齿后每一圈经过的像素大多
// 是部分覆盖的。取周长的一半作下限，台阶状的硬边（全部像素非 0 即 255）会是 0。
const int kMinCircleEdgePixels = 60;

// 圆角方形四个圆角上至少应有的半透明像素个数：半径 8 的四段圆弧总长约 50 像素。
const int kMinRoundCornerPixels = 20;

// 左右 / 上下对称检查的容差（alpha 值）。GDI+ 的抗锯齿填充本身有约 1/4 像素、偏向右下的取样偏差，
// 镜像位置的透明度实测至多相差 88（2026-10-03，tmp/aa/sym_probe.py；字形头像的底色圆也一样）。
// 容差取 96 仍能发现半个像素的整体错位：那种情况下对称位置相差 190 以上。
const int kSymmetryTolerance = 96;

// 颜色比较的容差：高质量插值缩放后，纯色区域的分量仍可能有 ±1~2 的偏差。
const int kColorTolerance = 3;

// 群成员行的测试尺寸（像素），与成员侧栏的行高相同。
const int kItemW = 200;
const int kItemH = 40;

// 群成员行头像左上角相对行的偏移，与 GroupMemberItem.cpp 的 kEdgePad、纵向居中算法一致。
const int kItemAvatarLeft = 10;
const int kItemAvatarTop  = (kItemH - balloonchatwnd::kGroupMemberAvatarPx) / 2;

// 群成员行头像的圆角半径（像素），与 GroupMemberItem.cpp 的 kAvatarRadius 相同；AA3 检查的方块边长取它加 1。
const int kItemAvatarRadius = 5;

// AA4 标题栏图标的边长（像素），与 CBalloonChatWindow::RebuildPeerIcons 相同。
const int kTitleIconPx = 48;

// AA4 字形标题栏图标的底色：与标题栏底色、照片颜色都明显不同。
const COLORREF kTitleMonoColor = RGB(0x2A, 0x7A, 0xD8);

// AA4 只检查圆周的斜向部分：离圆心横、纵距离都不小于边长除以这个数。
const int kDiagonalRegionDivisor = 5;

// 在作用域内启动 GDI+，离开时关闭。
class AaScopedGdiplus
{
public:
    // 启动 GDI+；失败时 Ok() 返回 false。
    AaScopedGdiplus()
        : m_token(0)
    {
        Gdiplus::GdiplusStartupInput input;
        m_ok = (Gdiplus::GdiplusStartup(&m_token, &input, NULL) == Gdiplus::Ok);
    }

    // 关闭 GDI+。
    ~AaScopedGdiplus()
    {
        if (m_ok)
        {
            Gdiplus::GdiplusShutdown(m_token);
        }
    }

    // GDI+ 是否启动成功。
    bool Ok() const
    {
        return m_ok;
    }

private:
    ULONG_PTR m_token;   // GdiplusStartup 返回的令牌，关闭时要用
    bool      m_ok;      // 是否启动成功
};

// 临时目录下的文件路径，文件名带进程 id（并发跑的测试进程互不干扰），并删掉旧文件。
std::wstring AaTempFile(const wchar_t* name)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    wchar_t prefix[32] = { 0 };
    ::swprintf_s(prefix, L"%u_", static_cast<unsigned int>(::GetCurrentProcessId()));
    std::wstring path = dir;
    path += prefix;
    path += name;
    ::DeleteFileW(path.c_str());
    return path;
}

// 写一张 kSrcPx 见方的纯色 PNG。成功返回 true。需要 GDI+ 已启动。
bool AaWriteSolidPng(const std::wstring& path, COLORREF color)
{
    UINT num = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0)
    {
        return false;
    }
    std::vector<BYTE> buf(size);
    Gdiplus::ImageCodecInfo* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(&buf[0]);
    Gdiplus::GetImageEncoders(num, size, codecs);
    const CLSID* png = NULL;
    for (UINT i = 0; i < num; ++i)
    {
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0)
        {
            png = &codecs[i].Clsid;
            break;
        }
    }
    if (png == NULL)
    {
        return false;
    }
    Gdiplus::Bitmap bmp(kSrcPx, kSrcPx, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&bmp);
        g.Clear(Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color)));
    }
    return bmp.Save(path.c_str(), png, NULL) == Gdiplus::Ok;
}

/**
 *  32 位自上而下 DIB 位图的像素读取器（只读，借用位图）。位图须是 CreateDIBSection 建的。
 */
class DibReader
{
public:
    // 读取 hbm 的像素指针与尺寸；不是 DIB 时 Ok() 返回 false。
    explicit DibReader(HBITMAP hbm)
        : m_bits(NULL)
        , m_w(0)
        , m_h(0)
    {
        DIBSECTION ds;
        if (hbm != NULL && ::GetObject(hbm, sizeof(ds), &ds) == sizeof(ds) && ds.dsBm.bmBits != NULL)
        {
            m_bits = static_cast<const BYTE*>(ds.dsBm.bmBits);
            m_w = ds.dsBm.bmWidth;
            m_h = (ds.dsBm.bmHeight < 0) ? -ds.dsBm.bmHeight : ds.dsBm.bmHeight;
        }
    }

    // 是否读到了像素。
    bool Ok() const { return m_bits != NULL; }

    // (x, y) 处的 alpha 值。
    int Alpha(int x, int y) const { return Px(x, y)[kOffsetAlpha]; }

    // (x, y) 处的颜色（预乘值；不透明处即原色）。
    COLORREF Color(int x, int y) const
    {
        const BYTE* p = Px(x, y);
        return RGB(p[kOffsetRed], p[kOffsetGreen], p[kOffsetBlue]);
    }

    // 位图宽、高（像素）。
    int Width() const { return m_w; }
    int Height() const { return m_h; }

    // 半透明（0 < alpha < 255）的像素个数；限定在 [x0, x1) × [y0, y1) 内。
    int CountPartial(int x0, int y0, int x1, int y1) const
    {
        int n = 0;
        for (int y = y0; y < y1; ++y)
        {
            for (int x = x0; x < x1; ++x)
            {
                const int a = Alpha(x, y);
                if (a > 0 && a < kAlphaOpaque)
                {
                    ++n;
                }
            }
        }
        return n;
    }

private:
    const BYTE* Px(int x, int y) const { return m_bits + ((size_t)y * m_w + x) * kBytesPerPixel; }

    const BYTE* m_bits;   // 像素数据（借用位图的内存，位图须在本对象之后释放）
    int         m_w;      // 宽（像素）
    int         m_h;      // 高（像素）
};

// 读出一张设备相关位图（标题栏图标是屏幕兼容的 DDB）的全部像素，按行存放。失败返回 false。
bool ReadDdbPixels(HBITMAP hbm, int w, int h, std::vector<COLORREF>& out)
{
    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;   // 负高 = 自上而下
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    std::vector<BYTE> buf((size_t)w * h * kBytesPerPixel);
    HDC screenDC = ::GetDC(NULL);
    const int rows = ::GetDIBits(screenDC, hbm, 0, (UINT)h, &buf[0], &bi, DIB_RGB_COLORS);
    ::ReleaseDC(NULL, screenDC);
    if (rows != h)
    {
        return false;
    }
    out.resize((size_t)w * h);
    for (int i = 0; i < w * h; ++i)
    {
        const BYTE* px = &buf[(size_t)i * kBytesPerPixel];
        out[i] = RGB(px[kOffsetRed], px[kOffsetGreen], px[kOffsetBlue]);
    }
    return true;
}

// 两个颜色的每个分量相差都不超过容差。
bool AaColorNear(COLORREF a, COLORREF b)
{
    return abs((int)GetRValue(a) - (int)GetRValue(b)) <= kColorTolerance
        && abs((int)GetGValue(a) - (int)GetGValue(b)) <= kColorTolerance
        && abs((int)GetBValue(a) - (int)GetBValue(b)) <= kColorTolerance;
}

// 数一张 w×h 图像里横向或纵向相邻、一个是 inside 色一个是 outside 色（各在容差内）的像素对个数，
// 只数圆周的斜向部分：像素离图像中心横、纵距离都不小于边长的 1/kDiagonalRegionDivisor。硬边裁剪的
// 斜边上必然有这种硬台阶，抗锯齿的斜边上两者之间总有过渡色。
int CountDiagonalHardSteps(const std::vector<COLORREF>& px, int w, int h, COLORREF inside, COLORREF outside)
{
    const double cx = (w - 1) / 2.0;
    const double cy = (h - 1) / 2.0;
    const double minDx = (double)w / kDiagonalRegionDivisor;
    const double minDy = (double)h / kDiagonalRegionDivisor;
    int n = 0;
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            if (fabs(x - cx) < minDx || fabs(y - cy) < minDy)
            {
                continue;
            }
            const COLORREF a = px[(size_t)y * w + x];
            if (x + 1 < w)
            {
                const COLORREF b = px[(size_t)y * w + x + 1];
                if ((AaColorNear(a, inside) && AaColorNear(b, outside))
                    || (AaColorNear(b, inside) && AaColorNear(a, outside)))
                {
                    ++n;
                }
            }
            if (y + 1 < h)
            {
                const COLORREF b = px[(size_t)(y + 1) * w + x];
                if ((AaColorNear(a, inside) && AaColorNear(b, outside))
                    || (AaColorNear(b, inside) && AaColorNear(a, outside)))
                {
                    ++n;
                }
            }
        }
    }
    return n;
}

// 把控件画到白底的 32 位离屏位图上，回调取像素。画布建不出来时返回 false。
bool PaintOnWhite(balloonwjui::DuiControl& ctrl, int w, int h, std::vector<COLORREF>& outPixels)
{
    HDC screenDC = ::GetDC(NULL);
    HDC memDC = ::CreateCompatibleDC(screenDC);
    ::ReleaseDC(NULL, screenDC);
    if (memDC == NULL)
    {
        return false;
    }
    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;   // 负高 = 自上而下
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL || bits == NULL)
    {
        ::DeleteDC(memDC);
        return false;
    }
    HGDIOBJ oldBmp = ::SelectObject(memDC, bmp);
    RECT all = { 0, 0, w, h };
    HBRUSH white = ::CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
    ::FillRect(memDC, &all, white);
    ::DeleteObject(white);
    ctrl.SetRect(all);
    ctrl.OnPaint(memDC, all);
    ::GdiFlush();

    outPixels.resize((size_t)w * h);
    const BYTE* p = static_cast<const BYTE*>(bits);
    for (int i = 0; i < w * h; ++i)
    {
        outPixels[i] = RGB(p[i * kBytesPerPixel + kOffsetRed], p[i * kBytesPerPixel + kOffsetGreen],
                           p[i * kBytesPerPixel + kOffsetBlue]);
    }
    ::SelectObject(memDC, oldBmp);
    ::DeleteObject(bmp);
    ::DeleteDC(memDC);
    return true;
}

} // anonymous namespace

TEST_CASE("AA1 圆形照片头像：圆周上有半透明像素，中心为原色，四角透明，左右对称", "[avataraa]")
{
    AaScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    const std::wstring path = AaTempFile(L"aa1_photo.png");
    REQUIRE(AaWriteSolidPng(path, kPhotoColor));

    HBITMAP hbm = balloonmain::MakeRoundedAvatarFromFile(path.c_str(), kAvatarPx, kCircleRadiusPx);
    REQUIRE(hbm != NULL);
    {
        DibReader dib(hbm);
        REQUIRE(dib.Ok());
        REQUIRE(dib.Width() == kAvatarPx);
        REQUIRE(dib.Height() == kAvatarPx);

        const int c = kAvatarPx / 2;
        CHECK(dib.Alpha(c, c) == kAlphaOpaque);
        CHECK(AaColorNear(dib.Color(c, c), kPhotoColor));

        // 四个角点在圆外，完全透明。
        CHECK(dib.Alpha(0, 0) == 0);
        CHECK(dib.Alpha(kAvatarPx - 1, 0) == 0);
        CHECK(dib.Alpha(0, kAvatarPx - 1) == 0);
        CHECK(dib.Alpha(kAvatarPx - 1, kAvatarPx - 1) == 0);

        // 抗锯齿：圆周上有一圈半透明像素。台阶状的硬边这里是 0。
        const int partial = dib.CountPartial(0, 0, kAvatarPx, kAvatarPx);
        INFO("partial-alpha pixels = " << partial);
        CHECK(partial >= kMinCircleEdgePixels);

        // 左右两侧最外一列的覆盖程度对称，说明没有半像素的整体偏移。
        CHECK(abs(dib.Alpha(0, c) - dib.Alpha(kAvatarPx - 1, c)) <= kSymmetryTolerance);
        CHECK(abs(dib.Alpha(c, 0) - dib.Alpha(c, kAvatarPx - 1)) <= kSymmetryTolerance);
    }
    ::DeleteObject(hbm);
    ::DeleteFileW(path.c_str());
}

TEST_CASE("AA2 圆角方形照片头像：四个圆角上有半透明像素，角点透明", "[avataraa]")
{
    AaScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    const std::wstring path = AaTempFile(L"aa2_photo.png");
    REQUIRE(AaWriteSolidPng(path, kPhotoColor));

    HBITMAP hbm = balloonmain::MakeRoundedAvatarFromFile(path.c_str(), kAvatarPx, kRoundRadiusPx);
    REQUIRE(hbm != NULL);
    {
        DibReader dib(hbm);
        REQUIRE(dib.Ok());

        const int c = kAvatarPx / 2;
        CHECK(dib.Alpha(c, c) == kAlphaOpaque);
        CHECK(AaColorNear(dib.Color(c, c), kPhotoColor));
        CHECK(dib.Alpha(0, 0) == 0);
        CHECK(dib.Alpha(kAvatarPx - 1, kAvatarPx - 1) == 0);

        // 只数四个圆角所在的方块（边长等于圆角半径），直边上的像素不计入。
        const int r = kRoundRadiusPx;
        const int farEdge = kAvatarPx - r;
        const int partial = dib.CountPartial(0, 0, r, r)
                          + dib.CountPartial(farEdge, 0, kAvatarPx, r)
                          + dib.CountPartial(0, farEdge, r, kAvatarPx)
                          + dib.CountPartial(farEdge, farEdge, kAvatarPx, kAvatarPx);
        INFO("partial-alpha pixels in corners = " << partial);
        CHECK(partial >= kMinRoundCornerPixels);
    }
    ::DeleteObject(hbm);
    ::DeleteFileW(path.c_str());
}

TEST_CASE("AA3 群成员行的照片头像：圆角处有原色与白色之间的过渡色", "[avataraa]")
{
    AaScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    balloonchatwnd::ChatAvatarBitmapCache::Inst().Clear();
    const std::wstring path = AaTempFile(L"aa3_photo.png");
    REQUIRE(AaWriteSolidPng(path, kPhotoColor));
    {
        balloonchatwnd::GroupMemberItem item;
        item.SetMember(_T("Member"), _T("M"), RGB(0x80, 0x80, 0x80), 0, CString(path.c_str()));

        std::vector<COLORREF> px;
        REQUIRE(PaintOnWhite(item, kItemW, kItemH, px));

        const int size = balloonchatwnd::kGroupMemberAvatarPx;
        const int left = kItemAvatarLeft;
        const int top = kItemAvatarTop;

        // 头像中心是照片原色。
        CHECK(AaColorNear(px[(size_t)(top + size / 2) * kItemW + left + size / 2], kPhotoColor));

        // 四个圆角各自的方块（含最外一行一列）：数相邻两个像素一个是纯原色、一个是纯白的情况。
        const COLORREF white = RGB(0xFF, 0xFF, 0xFF);
        const int cornerX[4] = { left, left + size - 1, left, left + size - 1 };
        const int cornerY[4] = { top, top, top + size - 1, top + size - 1 };
        const int stepX[4] = { 1, -1, 1, -1 };
        const int stepY[4] = { 1, 1, -1, -1 };
        for (int i = 0; i < 4; ++i)
        {
            int hardSteps = 0;
            for (int dy = 0; dy <= kItemAvatarRadius; ++dy)
            {
                for (int dx = 0; dx <= kItemAvatarRadius; ++dx)
                {
                    const COLORREF a = px[(size_t)(cornerY[i] + stepY[i] * dy) * kItemW + cornerX[i] + stepX[i] * dx];
                    // 与右侧（沿横向往里）和下侧（沿纵向往里）的相邻像素比较
                    if (dx < kItemAvatarRadius)
                    {
                        const COLORREF b = px[(size_t)(cornerY[i] + stepY[i] * dy) * kItemW
                                              + cornerX[i] + stepX[i] * (dx + 1)];
                        if ((AaColorNear(a, kPhotoColor) && AaColorNear(b, white))
                            || (AaColorNear(b, kPhotoColor) && AaColorNear(a, white)))
                        {
                            ++hardSteps;
                        }
                    }
                    if (dy < kItemAvatarRadius)
                    {
                        const COLORREF b = px[(size_t)(cornerY[i] + stepY[i] * (dy + 1)) * kItemW
                                              + cornerX[i] + stepX[i] * dx];
                        if ((AaColorNear(a, kPhotoColor) && AaColorNear(b, white))
                            || (AaColorNear(b, kPhotoColor) && AaColorNear(a, white)))
                        {
                            ++hardSteps;
                        }
                    }
                }
            }
            INFO("corner " << i << ": hard steps = " << hardSteps);
            CHECK(hardSteps == 0);
        }
    }
    balloonchatwnd::ChatAvatarBitmapCache::Inst().Clear();
    ::DeleteFileW(path.c_str());
}

TEST_CASE("AA4 标题栏图标：圆周上没有头像颜色紧挨标题栏底色的硬台阶", "[avataraa]")
{
    AaScopedGdiplus gdiplus;
    REQUIRE(gdiplus.Ok());
    const int c = kTitleIconPx / 2;

    SECTION("照片头像")
    {
        const std::wstring path = AaTempFile(L"aa4_photo.png");
        REQUIRE(AaWriteSolidPng(path, kPhotoColor));
        HBITMAP icon = balloonchatwnd::MakeRoundTitleIconFromImage(path.c_str(), balloonchatwnd::kChatTitleBarBg,
                                                                   kTitleIconPx);
        REQUIRE(icon != NULL);
        std::vector<COLORREF> px;
        const bool ok = ReadDdbPixels(icon, kTitleIconPx, kTitleIconPx, px);
        ::DeleteObject(icon);
        ::DeleteFileW(path.c_str());
        REQUIRE(ok);

        CHECK(AaColorNear(px[(size_t)c * kTitleIconPx + c], kPhotoColor));
        CHECK(px[0] == balloonchatwnd::kChatTitleBarBg);
        const int steps = CountDiagonalHardSteps(px, kTitleIconPx, kTitleIconPx, kPhotoColor,
                                                 balloonchatwnd::kChatTitleBarBg);
        INFO("hard steps = " << steps);
        CHECK(steps == 0);
    }

    SECTION("字形头像")
    {
        // 字留空，只检查圆本身。
        HBITMAP icon = balloonchatwnd::MakeRoundTitleIcon(_T(""), kTitleMonoColor, balloonchatwnd::kChatTitleBarBg,
                                                          kTitleIconPx);
        REQUIRE(icon != NULL);
        std::vector<COLORREF> px;
        const bool ok = ReadDdbPixels(icon, kTitleIconPx, kTitleIconPx, px);
        ::DeleteObject(icon);
        REQUIRE(ok);

        CHECK(AaColorNear(px[(size_t)c * kTitleIconPx + c], kTitleMonoColor));
        CHECK(px[0] == balloonchatwnd::kChatTitleBarBg);
        const int steps = CountDiagonalHardSteps(px, kTitleIconPx, kTitleIconPx, kTitleMonoColor,
                                                 balloonchatwnd::kChatTitleBarBg);
        INFO("hard steps = " << steps);
        CHECK(steps == 0);
    }
}
