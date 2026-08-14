/**
 *  PresenceBadge.cpp
 *
 *  在线状态徽标的绘制实现。字形本身画在 MainPanelIcons 的 DrawLineIcon 里
 *  （kIconPresence* 四个 case），本文件负责三件事：把 presence 映射到字形、
 *  把配色从 GetPresenceVisual 取来一并喂进去、以及在需要位图的场合把徽标
 *  渲染到一张 32bpp 预乘 alpha 的 DIB 上。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用（字形映射是纯函数，要能在测试工程里直接断言）。
 *  在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#include <windows.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include "PresenceBadge.h"
#include "MainPanelIcons.h"         // IconKind / DrawLineIcon（字形的实际画法）
#include "MainPanelStatusMenu.h"    // GetPresenceVisual（状态 → 圆点色的唯一出处）
#include "../net/Protocol.h"        // presence_type（在线状态取值的单一事实来源）

namespace balloonmain {

namespace {

// viewBox 基准边长 —— 与 MainPanelIcons 全部图标同口径（design icons.jsx 均为 0 0 24 24）。
const Gdiplus::REAL kViewBox = 24.0f;

// 状态徽标不走描边路径（圆底是填充、白色字形的线宽由 MainPanelIcons 内部常量决定），
// DrawLineIcon 的 strokeWidth 参数对 kIconPresence* 四个 case 无效，传个占位值即可。
const Gdiplus::REAL kUnusedStrokeWidth = 2.0f;

// "当前所处状态"标记的两个参数。徽标缩到 68% 居中，外面套一圈 1.6 宽（viewBox 24
// 单位）的同色细环 —— 缩放比例定在 68% 是为了让环与徽标之间留出肉眼可见的空隙：
// 徽标圆底半径 10 * 0.68 ≈ 6.8，环的内缘在 11.2，空隙 4.4 个单位，16px 下约 2.9 像素。
// 再大就会把字形压得太小，再小则环与徽标粘连成一团。
const Gdiplus::REAL kCurrentBadgeScale = 0.68f;
const Gdiplus::REAL kCurrentRingStroke = 1.6f;

// 32bpp DIB 的每像素字节数（BGRA）。CreateDIBSection 与清零都要用到。
const int kBytesPerPixel = 4;

// GDI+ 懒初始化 token —— 与 MainPanelAvatar.cpp / MainPanelMenuIcons.cpp 同模式
// （多次 GdiplusStartup 是安全的；本翻译单元自留 token，进程退出时自动回收，
// 无需 GdiplusShutdown）。只有造位图那条路径需要它：自绘控件那条路径的 Graphics
// 由调用方给出，GDI+ 早已被宿主初始化过。
ULONG_PTR& GdipToken()
{
    static ULONG_PTR s_token = 0;
    return s_token;
}

void EnsureGdiplus()
{
    if (GdipToken() != 0)
    {
        return;
    }
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&GdipToken(), &input, nullptr);
}

// 取某个在线状态的徽标颜色（COLORREF → GDI+ Color，恒不透明）。
Gdiplus::Color BadgeColor(int presence)
{
    COLORREF c = GetPresenceVisual(presence).dotColor;
    return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c));
}

} // anonymous namespace

int GetPresenceBadgeIconKind(int presence)
{
    switch (presence)
    {
    case presence_online:
        return kIconPresenceOnline;
    case presence_away:
        return kIconPresenceAway;
    case presence_busy:
        return kIconPresenceBusy;
    case presence_offline:
        return kIconPresenceOffline;
    default:
        // 一切越界取值（含已作废的旧状态 4）统一按"离线"画。口径与
        // NormalizePresenceForMenu / GetPresenceVisual 的兜底一致 —— 三者必须同步，
        // 否则会出现"胶囊说离线、图标却画成别的状态"这种自相矛盾。
        return kIconPresenceOffline;
    }
}

void DrawPresenceBadge(Gdiplus::Graphics& g, int presence, bool markCurrent,
                       Gdiplus::REAL x, Gdiplus::REAL y, Gdiplus::REAL size)
{
    if (size <= 0.0f)
    {
        return;
    }

    const int            kind  = GetPresenceBadgeIconKind(presence);
    const Gdiplus::Color color = BadgeColor(presence);

    if (!markCurrent)
    {
        DrawLineIcon(g, kind, x, y, size, color, kUnusedStrokeWidth);
        return;
    }

    // 当前项：先画缩小并居中的徽标。
    const Gdiplus::REAL innerSize = size * kCurrentBadgeScale;
    const Gdiplus::REAL offset    = (size - innerSize) / 2.0f;
    DrawLineIcon(g, kind, x + offset, y + offset, innerSize, color, kUnusedStrokeWidth);

    // 再套外环。外接方框要内收半个线宽，否则描边有一半落在 size 之外被裁掉。
    const Gdiplus::REAL stroke = kCurrentRingStroke * size / kViewBox;
    const Gdiplus::REAL half   = stroke / 2.0f;

    Gdiplus::Pen pen(color, stroke);
    g.DrawEllipse(&pen, x + half, y + half, size - stroke, size - stroke);
}

HBITMAP CreatePresenceBadgeBitmap(int presence, int sizePx)
{
    if (sizePx <= 0)
    {
        return NULL;
    }

    EnsureGdiplus();

    // 32bpp top-down DIB，内存顺序 BGRA（GDI 标准 / GDI+ PARGB 同序）。top-down 时
    // biHeight 取负数，第 0 行落在 bits 起始处，交给 GDI+ 不存在上下翻转的歧义。
    // 做法与 MainPanelAvatar::MakeMonogramAvatar 逐字同款。
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = sizePx;
    bi.bmiHeader.biHeight      = -sizePx;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void*   bits = NULL;
    HBITMAP hBmp = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (hBmp == NULL || bits == NULL)
    {
        if (hBmp != NULL)
        {
            ::DeleteObject(hBmp);
        }
        return NULL;
    }

    // 全部清零 → 整张完全透明（alpha = 0；预乘 alpha 下 rgb 也必须同为 0）。
    ::ZeroMemory(bits, (size_t)sizePx * (size_t)sizePx * kBytesPerPixel);

    // 用 Gdiplus::Bitmap 直接包住 DIB 的像素 buffer，写 Bitmap 即写 HBITMAP 的像素。
    // PARGB 表示 premultiplied alpha，GDI+ 写入时自动预乘，符合 AlphaBlend 的要求。
    const int stride = sizePx * kBytesPerPixel;
    Gdiplus::Bitmap bitmap(sizePx, sizePx, stride,
                           PixelFormat32bppPARGB,
                           static_cast<BYTE*>(bits));

    Gdiplus::Graphics g(&bitmap);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    DrawPresenceBadge(g, presence, false, 0.0f, 0.0f, (Gdiplus::REAL)sizePx);

    // Graphics / Bitmap 析构时自动 flush，像素已落进 DIB，直接返回 HBITMAP 即可。
    return hBmp;
}

} // namespace balloonmain
