/**
 *  通话窗标题栏「图钉」固定，说明见 WindowPin.h。
 *  balloonwj@qq.com   2026-10-05（2026-10-06 图钉图标改画方案 C：竖直线框 / 实心蓝加底线，按覆盖率抗锯齿）
 */
#include "stdafx.h"
#include "WindowPin.h"

#include <string.h>   //memset

#include "../../Source/Common/I18n/I18n.h"   //TR：界面文字的多语言查找

namespace flamingomedia {

namespace {

//图标位图边长（像素），与 caption icon 的绘制尺寸一致
const int kPinIconSize = 16;
//每个像素在横、纵方向上各取几个采样点：落在图形内的采样点占比就是该像素的不透明度（抗锯齿）
const int kPinSubSamples = 8;

//图钉造型（方案 C，2026-10-06，docs/音视频通话-界面美化方案.md 第 10 节）。以下坐标都是放大之前的图标坐标
//（单位：像素，左上角为原点）；绘制时整体以图标中心为轴放大 kPinScale 倍，与选定方案的预览图一致
//（tmp/pinicon/render_pin_options.py）
const double kPinCenter = 8.0;      //放大的中心（图标正中）
const double kPinScale = 1.08;      //整体放大倍数
//钉帽：圆角矩形
const double kCapLeft = 4.6;
const double kCapRight = 11.4;
const double kCapTop = 1.2;
const double kCapBottom = 3.4;
const double kCapRadius = 0.8;      //四角圆角半径
//钉身：矩形，上端伸进钉帽、下端伸进托盘，使三段连成一体
const double kBodyLeft = 6.0;
const double kBodyRight = 10.0;
const double kBodyTop = 3.0;
const double kBodyBottom = 7.8;
//托盘：上窄下宽的六边形（凸多边形），顶点按顺时针排列
const double kFlangeTop = 7.4;          //上边（与钉身同宽）
const double kFlangeShoulder = 9.4;     //斜边与竖边的交界
const double kFlangeBottom = 10.6;      //下边
const double kFlangeHalfTop = 2.0;      //上边半宽
const double kFlangeHalfBottom = 4.6;   //下半段半宽
//钉针：竖直的细条，末端是半圆
const double kNeedleHalfWidth = 0.65;
const double kNeedleTop = 10.4;
const double kNeedleTipY = 15.0;        //末端半圆的圆心
//未固定时线框的粗细：图形内与边缘距离小于该值的部分画出来，其余镂空
const double kPinStroke = 1.4;
//已固定时的底线（不随图钉放大，按像素网格对齐成清晰的一整行）
const double kBaseLeft = 3.0;
const double kBaseRight = 13.0;
const double kBaseTop = 14.0;
const double kBaseBottom = 15.0;

//未固定与标题栏三按钮的字形同色（balloonui DuiFrameWindow.cpp 的 kGlyphInk）；已固定用主题蓝，一眼看出「已固定」
const COLORREF kPinInkNormal = RGB(60, 60, 60);
const COLORREF kPinInkActive = RGB(0x18, 0x6E, 0xDC);
//每像素字节数（32 位 BGRA）与不透明的 alpha
const int kBytesPerPixel = 4;
const int kOpaqueAlpha = 255;
//WM_SYSCOMMAND 的 wParam 低 4 位是系统内部用的标志（如拖标题栏时的 SC_MOVE | HTCAPTION），判断命令前去掉
const WPARAM kSysCommandMask = 0xFFF0;

//点 (x, y) 是否在钉帽的圆角矩形内
bool InPinCap(double x, double y)
{
    if (x < kCapLeft || x > kCapRight || y < kCapTop || y > kCapBottom)
        return false;
    //四个角：落在圆角之外的部分不算
    const double cx = (x < kCapLeft + kCapRadius) ? (kCapLeft + kCapRadius)
                    : ((x > kCapRight - kCapRadius) ? (kCapRight - kCapRadius) : x);
    const double cy = (y < kCapTop + kCapRadius) ? (kCapTop + kCapRadius)
                    : ((y > kCapBottom - kCapRadius) ? (kCapBottom - kCapRadius) : y);
    const double dx = x - cx;
    const double dy = y - cy;
    return dx * dx + dy * dy <= kCapRadius * kCapRadius;
}

//点 (x, y) 是否在托盘的六边形内：凸多边形，顶点顺时针排列时点在每条边的右侧（屏幕坐标下叉积不小于 0）
bool InPinFlange(double x, double y)
{
    const int kVertexCount = 6;
    const double vx[kVertexCount] = { kPinCenter - kFlangeHalfTop, kPinCenter + kFlangeHalfTop,
                                      kPinCenter + kFlangeHalfBottom, kPinCenter + kFlangeHalfBottom,
                                      kPinCenter - kFlangeHalfBottom, kPinCenter - kFlangeHalfBottom };
    const double vy[kVertexCount] = { kFlangeTop, kFlangeTop, kFlangeShoulder, kFlangeBottom, kFlangeBottom,
                                      kFlangeShoulder };
    for (int i = 0; i < kVertexCount; ++i)
    {
        const int j = (i + 1) % kVertexCount;
        const double cross = (vx[j] - vx[i]) * (y - vy[i]) - (vy[j] - vy[i]) * (x - vx[i]);
        if (cross < 0)
            return false;
    }
    return true;
}

//点 (x, y) 是否在图钉的主体（钉帽、钉身、托盘三段合在一起）内
bool InPinSilhouette(double x, double y)
{
    if (InPinCap(x, y))
        return true;
    if (x >= kBodyLeft && x <= kBodyRight && y >= kBodyTop && y <= kBodyBottom)
        return true;
    return InPinFlange(x, y);
}

//点 (x, y) 是否在线框镂空的部分：以它为中心、边长 2×kPinStroke 的正方形整个落在主体内（取四角、四边中点与中心
//九个点判断，对这几段凸形拼成的主体足够准确）
bool InPinHollow(double x, double y)
{
    for (int i = -1; i <= 1; ++i)
    {
        for (int j = -1; j <= 1; ++j)
        {
            if (!InPinSilhouette(x + i * kPinStroke, y + j * kPinStroke))
                return false;
        }
    }
    return true;
}

//点 (x, y) 是否在钉针上
bool InPinNeedle(double x, double y)
{
    if (x >= kPinCenter - kNeedleHalfWidth && x <= kPinCenter + kNeedleHalfWidth && y >= kNeedleTop
        && y <= kNeedleTipY)
        return true;
    const double dx = x - kPinCenter;
    const double dy = y - kNeedleTipY;
    return dx * dx + dy * dy <= kNeedleHalfWidth * kNeedleHalfWidth;
}

//图标坐标 (x, y) 处是否要着色。active 为已固定：实心并带底线；否则为线框
bool InPinIcon(double x, double y, bool active)
{
    if (active && x >= kBaseLeft && x <= kBaseRight && y >= kBaseTop && y <= kBaseBottom)
        return true;
    //换算到放大之前的坐标再判断
    const double ux = kPinCenter + (x - kPinCenter) / kPinScale;
    const double uy = kPinCenter + (y - kPinCenter) / kPinScale;
    if (InPinNeedle(ux, uy))
        return true;
    if (!InPinSilhouette(ux, uy))
        return false;
    return active || !InPinHollow(ux, uy);
}

} // namespace

HBITMAP MakePinCaptionIcon(bool active)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kPinIconSize;
    bi.bmiHeader.biHeight = -kPinIconSize;   //自上而下
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    HBITMAP bmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp == NULL || bits == NULL)
        return NULL;

    unsigned char* px = static_cast<unsigned char*>(bits);
    memset(px, 0, static_cast<size_t>(kPinIconSize) * kPinIconSize * kBytesPerPixel);   //全透明底
    const COLORREF ink = active ? kPinInkActive : kPinInkNormal;
    const int samplesPerPixel = kPinSubSamples * kPinSubSamples;

    for (int y = 0; y < kPinIconSize; ++y)
    {
        for (int x = 0; x < kPinIconSize; ++x)
        {
            //数一数这个像素里有多少个采样点落在图形内（采样点取各小格的中心）
            int covered = 0;
            for (int sy = 0; sy < kPinSubSamples; ++sy)
            {
                for (int sx = 0; sx < kPinSubSamples; ++sx)
                {
                    const double fx = x + (sx + 0.5) / kPinSubSamples;
                    const double fy = y + (sy + 0.5) / kPinSubSamples;
                    if (InPinIcon(fx, fy, active))
                        ++covered;
                }
            }
            if (covered == 0)
                continue;
            //AlphaBlend 要求预乘 alpha：各颜色通道先乘上不透明度
            const int alpha = covered * kOpaqueAlpha / samplesPerPixel;
            unsigned char* p = px + (static_cast<size_t>(y) * kPinIconSize + x) * kBytesPerPixel;
            p[0] = static_cast<unsigned char>(GetBValue(ink) * alpha / kOpaqueAlpha);
            p[1] = static_cast<unsigned char>(GetGValue(ink) * alpha / kOpaqueAlpha);
            p[2] = static_cast<unsigned char>(GetRValue(ink) * alpha / kOpaqueAlpha);
            p[3] = static_cast<unsigned char>(alpha);
        }
    }
    return bmp;
}

const wchar_t* PinCaptionTooltip(bool pinned)
{
    return pinned ? TR(L"取消固定") : TR(L"固定在桌面（置顶，不能移动和改变大小）");
}

bool IsSysCommandBlockedWhenPinned(WPARAM wParam)
{
    switch (wParam & kSysCommandMask)
    {
    //拖标题栏、Alt+空格菜单的「移动」
    case SC_MOVE:
    //拖边框、Alt+空格菜单的「大小」
    case SC_SIZE:
    //最小化按钮、Win+↓
    case SC_MINIMIZE:
    //最大化按钮、双击标题栏
    case SC_MAXIMIZE:
    //还原（从最小化或最大化恢复）
    case SC_RESTORE:
        return true;
    //关闭等其余命令照常
    default:
        return false;
    }
}

void KeepWindowPosWhenPinned(WINDOWPOS* wp)
{
    if (wp == NULL)
        return;
    wp->flags |= SWP_NOMOVE | SWP_NOSIZE;
}

bool ApplyWindowTopmost(HWND hwnd, bool topmost)
{
    if (!::IsWindow(hwnd))
        return false;
    //只改层叠次序：不动位置与大小，也不激活窗口
    const UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE;
    //置顶前先取消一次置顶，原因见头文件说明
    if (topmost)
        ::SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, flags);
    ::SetWindowPos(hwnd, topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, flags);
    return (::GetWindowLong(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
}

} // namespace flamingomedia
