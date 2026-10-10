/**
 *  MainPanelListItem.cpp
 *
 *  主面板通用列表行的绘制实现。形状（悬停底 / 头像圆角 / 状态点 / 角标）
 *  用 GDI+ 抗锯齿，文字用 GDI DrawText（清晰）。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-05-21
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（等价 NewChatDemo/stdafx.h）----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限（窗口类仍为 RichEdit20W）
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>
#include <atlcrack.h>
#include <atlmisc.h>

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "msimg32.lib")   // AlphaBlend（画自定义图头像）

#include "MainPanelListItem.h"
#include "MainPanelMockData.h"
#include "PresenceBadge.h"          // DrawPresenceBadge（头像右下角的状态徽标）
#include "AvatarGlyph.h"            // DrawCenteredGlyphGdi：头像字居中
#include "../../Common/I18n/I18n.h"   // i18n::UiFontName：当前界面语言的界面字体

namespace balloonmain {

namespace {

// ---- 配色 --------------------------------------------------------------

// 名称文字色 —— 比 design --text(#1A1A1A) 略浅，避免视觉过黑过重。
const COLORREF kNameColor = RGB(0x3C, 0x3C, 0x3C);
// 搜索命中高亮黄底（淡黄，承托深色名称文字，对应 design 搜索框命中色）。
const COLORREF kHighlightFill = RGB(0xFF, 0xF1, 0x76);
// 副行文字色（design --text-muted #6B6B6B）。
const COLORREF kSubColor  = RGB(0x6B, 0x6B, 0x6B);
// 右上角时间色（design --text-faint）。
const COLORREF kTimeColor = RGB(0xA8, 0xA6, 0xA2);
// 未读角标红 #FF4D4F。
const Gdiplus::Color kBadgeRed(255, 0xFF, 0x4D, 0x4F);
// 状态点白色描边（与底色拉开）。
const Gdiplus::Color kStatusRing(255, 255, 255, 255);

// ---- 几何常量 ----------------------------------------------------------

// 头像边长与圆角取自类内公开常量 —— 调用方生成真图位图时用的是同两个值，
// 在此复述一份数字会让两边慢慢对不上。
const int           kAvatarSize    = MainPanelListItem::kAvatarSizePx;
const int           kAvatarLeft    = 8;     // 头像距行左边
const int           kAvatarTextGap = 10;    // 头像与文字列间距
const Gdiplus::REAL kAvatarRadius  = (Gdiplus::REAL)MainPanelListItem::kAvatarRadiusPx;
// 状态徽标的绘制方框边长。徽标圆底只占方框的 20/24，故 9 对应约 7.5px 的圆点 ——
// 与改版前 7px 的纯色小圆点接近，但字形（时钟 / 横杠 / 空心环）能看出来了。
const Gdiplus::REAL kStatusDotSize = 9.0f;
const Gdiplus::REAL kStatusRingW   = 2.0f;  // 状态点白描边宽度
// 徽标圆底在绘制方框中所占的比例（viewBox 24 里半径 10 的圆，直径 20/24）。
// 白描边要贴着圆底外扩，故需要知道这个比例。
const Gdiplus::REAL kBadgeDiscRatio = 20.0f / 24.0f;
const int           kNamePx        = 12;    // 名称字号
const int           kSubPx         = 10;    // 副行字号
const int           kRightPx       = 11;    // 右侧文字（时间 / 成员数）字号
const Gdiplus::REAL kBadgeDiameter = 16.0f; // 角标直径
const int           kRowRightPad   = 10;    // 行右内距

// 名称 / 副行右界与行右内距之间的呼吸位（逻辑像素），免得文字贴着行边缘。
const int kNameRightGap       = 4;
// 名称 / 副行右界与未读角标之间的间距（逻辑像素）。
const int kBadgeTextGap       = 6;
// 第一行右侧文字与其左侧名称之间的最小间距（逻辑像素）。两者同处第一行，靠得太近
// 会读成一串，故留出这一段空白。
const int kRightTextGap       = 8;
// 量不到右侧文字宽度（取字体失败）时的兜底预留宽（逻辑像素）。取值参照本控件改造前
// 硬编码的右侧文字框宽度，足够放下 "12 人" / "昨天" 这类短文字。
const int kRightTextFallbackW = 70;
// 单行行（没有副行）里右侧文字距行顶的距离与文字框高度（逻辑像素）。这种行没有第二行
// 可作参照，右侧文字沿用"贴着行右上角"的老位置。
const int kRightTextTop       = 8;
const int kRightTextHeight    = 16;

// ---- 悬停态小按钮（定位 / 打开文件 / 打开所在文件夹）--------------------
//
// 只在配了按钮的行上出现（搜索结果里的联系人行、本机有文件的聊天文件行），且仅当鼠标
// 悬停于该行（或该行被键盘选中）时才绘制；其余时候完全不占位。

// 图标绘制方框的边长（逻辑像素）。取 18 与行内其它次要元素（未读角标 16）量级相当：
// 再大会与 36 见方的头像抢视觉重量，再小则难以点中。
const int kActionIconSize    = 18;
// 图标绘制方框的右边界与行右内距 kRowRightPad 之间的间距（逻辑像素）。0 表示图标
// 右边界正好落在行右内距那条线上，与未读角标、右侧文字的右对齐口径一致。
const int kActionIconRightGap = 0;
// 鼠标落在图标上时，垫在图标下方的圆底相对图标方框每边外扩的距离（逻辑像素）。
// 外扩 4 后圆底直径 26，与常见工具栏图标按钮的触发区尺寸相当。
const int kActionHotPad      = 4;
// 相邻两个按钮图标方框之间的间距（逻辑像素）。要大于两倍 kActionHotPad，两个按钮的
// 浅色圆底才不会连在一起。
const int kActionIconGap     = 10;
// 图标常态描边色（中灰）。刻意弱于名称文字色 kNameColor，避免抢走主行的注意力。
const COLORREF kActionIconColor    = RGB(0x8C, 0x8C, 0x8C);
// 鼠标正落在图标上时的描边色（深灰）。与常态之间拉开明显的明度差，表示"这里可以点"。
const COLORREF kActionIconHotColor = RGB(0x40, 0x40, 0x40);
// 鼠标正落在图标上时垫在图标下方的浅色圆底。比搜索结果行的悬停底色
// （SearchDropdown 里的 RGB(0xE9,0xEC,0xF1)）再深一档，才能在已经变色的行底上看出来。
const Gdiplus::Color kActionHotFill(255, 0xD6, 0xDB, 0xE3);
// 图标描边宽度，以 viewBox 24 为基准（与 MainPanelIcons 里其它线性图标同口径）。
const Gdiplus::REAL kActionStrokeWidth = 2.0f;
// 图标的 viewBox 基准边长（design icons.jsx 的图标统一用 0 0 24 24）。
const Gdiplus::REAL kActionViewBox     = 24.0f;

// ---- 地图定位针在 viewBox 24 里的几何 ----
// 针头圆心。x 取正中；y 取 9.5 而非 12 —— 针身要向下伸出一段尖角，圆心上移后
// 整枚针在方框内才是上下居中的。
const Gdiplus::REAL kPinHeadCx     = 12.0f;
const Gdiplus::REAL kPinHeadCy     = 9.5f;
// 针头圆半径。5.5 是在 18 像素目标尺寸下实测的下限附近：再小针孔就糊进圆环里了。
const Gdiplus::REAL kPinHeadR      = 5.5f;
// 针尖端点的 y 坐标（落在针头圆心正下方）。20.5 使整枚针在 viewBox 上下各留约 3.5
// 的余量，与本文件其它图标的留白一致。
const Gdiplus::REAL kPinTipY       = 20.5f;
// 针身两条斜边与针头圆的相接点相对圆心的偏移量（圆上 45° 方位，左下 / 右下各一个）。
// 数值即 kPinHeadR * cos(45°) ≈ 3.889，取 3.9 便于阅读。
const Gdiplus::REAL kPinSideOffset = 3.9f;
// 针头正中那个实心小圆（针孔）的半径。太小在 18 像素下看不见，太大会把针头填满、
// 看着像个实心水滴而不是定位针。
const Gdiplus::REAL kPinHoleR      = 1.9f;

// 在 (x, y) 起、边长 size 的方形区域内描边绘制一枚地图定位针（圆形针头 + 下方尖角
// + 针头正中的实心小圆）。画法与 MainPanelIcons::DrawLineIcon 同口径：先把 viewBox 24
// 映射到目标区域，再用圆头圆角的 Pen 描边。
//
// 之所以不把它加进 MainPanelIcons 的 IconKind：那份图标表是主面板 Tab 栏与各菜单
// 共用的公共资源，本图标目前只有搜索结果行一个使用方，先就地画；将来出现第二个
// 使用方再归并过去。
void DrawLocatePin(Gdiplus::Graphics& g,
                   Gdiplus::REAL x, Gdiplus::REAL y, Gdiplus::REAL size,
                   const Gdiplus::Color& color)
{
    if (size <= 0.0f)
    {
        return;
    }

    Gdiplus::GraphicsState st = g.Save();
    g.TranslateTransform(x, y);
    g.ScaleTransform(size / kActionViewBox, size / kActionViewBox);

    Gdiplus::Pen pen(color, kActionStrokeWidth);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);

    // 针头圆。
    g.DrawEllipse(&pen, kPinHeadCx - kPinHeadR, kPinHeadCy - kPinHeadR,
                  kPinHeadR * 2.0f, kPinHeadR * 2.0f);

    // 针身：自针头圆左下 45° 处斜下到针尖，再斜上回到圆右下 45° 处。
    {
        Gdiplus::GraphicsPath tip;
        tip.AddLine(kPinHeadCx - kPinSideOffset, kPinHeadCy + kPinSideOffset,
                    kPinHeadCx, kPinTipY);
        tip.AddLine(kPinHeadCx, kPinTipY,
                    kPinHeadCx + kPinSideOffset, kPinHeadCy + kPinSideOffset);
        g.DrawPath(&pen, &tip);
    }

    // 针孔：针头正中的实心小圆，有了它才一眼能认出这是定位针。
    {
        Gdiplus::SolidBrush brush(color);
        g.FillEllipse(&brush, kPinHeadCx - kPinHoleR, kPinHeadCy - kPinHoleR,
                      kPinHoleR * 2.0f, kPinHoleR * 2.0f);
    }

    g.Restore(st);
}

// ---- 「打开文件」图标在 viewBox 24 里的几何：左下方框 + 指向右上角的箭头 ----
// 方框左、下边的位置与方框右、上边的位置。方框右上角留空给箭头穿出。
const Gdiplus::REAL kOpenBoxLeft   = 4.0f;
const Gdiplus::REAL kOpenBoxBottom = 20.0f;
const Gdiplus::REAL kOpenBoxRight  = 18.0f;
const Gdiplus::REAL kOpenBoxTop    = 6.0f;
// 方框上边、右边各画到哪里为止（留出右上角的缺口）。
const Gdiplus::REAL kOpenBoxTopEnd   = 11.0f;
const Gdiplus::REAL kOpenBoxRightEnd = 13.0f;
// 箭头：从方框中部指向右上角，箭头两翼的长度。
const Gdiplus::REAL kOpenArrowFromX = 10.0f;
const Gdiplus::REAL kOpenArrowFromY = 14.0f;
const Gdiplus::REAL kOpenArrowTip   = 20.0f;   // 箭头尖端的 x（y 取 24 - 20 = 4）
const Gdiplus::REAL kOpenArrowWing  = 6.0f;

// ---- 「打开所在文件夹」图标在 viewBox 24 里的几何：文件夹轮廓（左上带页签）----
const Gdiplus::REAL kFolderLeft    = 3.0f;
const Gdiplus::REAL kFolderRight   = 21.0f;
const Gdiplus::REAL kFolderTop     = 5.0f;    // 页签顶边
const Gdiplus::REAL kFolderBodyTop = 8.0f;    // 主体顶边
const Gdiplus::REAL kFolderBottom  = 19.0f;
const Gdiplus::REAL kFolderTabEnd  = 9.0f;    // 页签顶边的右端
const Gdiplus::REAL kFolderTabFoot = 11.0f;   // 页签斜边落到主体顶边的位置

// 在 (x, y) 起、边长 size 的方形区域内描边绘制「打开文件」图标（方框 + 指向右上角的箭头）。
// 画法与 DrawLocatePin 同口径。
void DrawOpenFileIcon(Gdiplus::Graphics& g,
                      Gdiplus::REAL x, Gdiplus::REAL y, Gdiplus::REAL size,
                      const Gdiplus::Color& color)
{
    if (size <= 0.0f)
    {
        return;
    }
    Gdiplus::GraphicsState st = g.Save();
    g.TranslateTransform(x, y);
    g.ScaleTransform(size / kActionViewBox, size / kActionViewBox);

    Gdiplus::Pen pen(color, kActionStrokeWidth);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);

    //方框：从上边的缺口处开始，逆时针经左上、左下、右下，停在右边的缺口处。
    {
        Gdiplus::GraphicsPath box;
        box.AddLine(kOpenBoxTopEnd, kOpenBoxTop, kOpenBoxLeft, kOpenBoxTop);
        box.AddLine(kOpenBoxLeft, kOpenBoxTop, kOpenBoxLeft, kOpenBoxBottom);
        box.AddLine(kOpenBoxLeft, kOpenBoxBottom, kOpenBoxRight, kOpenBoxBottom);
        box.AddLine(kOpenBoxRight, kOpenBoxBottom, kOpenBoxRight, kOpenBoxRightEnd);
        g.DrawPath(&pen, &box);
    }

    //箭头：杆从方框中部斜向右上角，尖端处两翼分别向左、向下。
    const Gdiplus::REAL tipX = kOpenArrowTip;
    const Gdiplus::REAL tipY = kActionViewBox - kOpenArrowTip;
    g.DrawLine(&pen, kOpenArrowFromX, kOpenArrowFromY, tipX, tipY);
    g.DrawLine(&pen, tipX - kOpenArrowWing, tipY, tipX, tipY);
    g.DrawLine(&pen, tipX, tipY, tipX, tipY + kOpenArrowWing);

    g.Restore(st);
}

// 在 (x, y) 起、边长 size 的方形区域内描边绘制「打开所在文件夹」图标（左上带页签的文件夹）。
// 画法与 DrawLocatePin 同口径。
void DrawFolderIcon(Gdiplus::Graphics& g,
                    Gdiplus::REAL x, Gdiplus::REAL y, Gdiplus::REAL size,
                    const Gdiplus::Color& color)
{
    if (size <= 0.0f)
    {
        return;
    }
    Gdiplus::GraphicsState st = g.Save();
    g.TranslateTransform(x, y);
    g.ScaleTransform(size / kActionViewBox, size / kActionViewBox);

    Gdiplus::Pen pen(color, kActionStrokeWidth);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);

    //一笔画出整个轮廓：左下 → 左上（页签顶）→ 页签右端 → 斜落到主体顶边 → 右上 → 右下 → 闭合。
    Gdiplus::GraphicsPath folder;
    folder.AddLine(kFolderLeft, kFolderBottom, kFolderLeft, kFolderTop);
    folder.AddLine(kFolderLeft, kFolderTop, kFolderTabEnd, kFolderTop);
    folder.AddLine(kFolderTabEnd, kFolderTop, kFolderTabFoot, kFolderBodyTop);
    folder.AddLine(kFolderTabFoot, kFolderBodyTop, kFolderRight, kFolderBodyTop);
    folder.AddLine(kFolderRight, kFolderBodyTop, kFolderRight, kFolderBottom);
    folder.CloseFigure();
    g.DrawPath(&pen, &folder);

    g.Restore(st);
}

// 用四段 90° 圆弧 + 闭合，向 path 添加一个圆角矩形。
void AddRoundRect(Gdiplus::GraphicsPath& path,
                  Gdiplus::REAL x, Gdiplus::REAL y,
                  Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL radius)
{
    Gdiplus::REAL r = radius;
    if (r > w / 2.0f) { r = w / 2.0f; }
    if (r > h / 2.0f) { r = h / 2.0f; }
    const Gdiplus::REAL d = r * 2.0f;
    path.AddArc(x,         y,         d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y,         d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f,   90.0f);
    path.AddArc(x,         y + h - d, d, d, 90.0f,  90.0f);
    path.CloseFigure();
}

// 用 GDI 绘制文字 —— DrawText 经系统 ClearType 光栅化，清晰锐利。
void DrawGdiText(HDC hdc, LPCWSTR text, const RECT& rc,
                 int pixelHeight, bool bold, COLORREF color, UINT dtFlags)
{
    if (text == NULL || text[0] == L'\0' || pixelHeight <= 0)
    {
        return;
    }
    HFONT hFont = ::CreateFontW(
        -pixelHeight, 0, 0, 0,
        bold ? FW_SEMIBOLD : FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        i18n::UiFontName());
    if (hFont == NULL)
    {
        return;
    }
    HFONT    hOldFont  = (HFONT)::SelectObject(hdc, hFont);
    int      oldBkMode = ::SetBkMode(hdc, TRANSPARENT);
    COLORREF oldColor  = ::SetTextColor(hdc, color);
    RECT r = rc;
    ::DrawTextW(hdc, text, -1, &r, dtFlags);
    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldBkMode);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
}

// 用与 DrawGdiText 完全相同的字体，量一段文字的渲染宽度（逻辑像素）。用来给第一行
// 右侧的文字预留位置，使左侧名称的省略号截在它左边、两段字不叠在一起。
// 取不到字体或文字为空时返回 0，调用方据此走兜底宽。
int MeasureGdiTextWidth(HDC hdc, LPCWSTR text, int pixelHeight, bool bold)
{
    if (text == NULL || text[0] == L'\0' || pixelHeight <= 0)
    {
        return 0;
    }
    HFONT hFont = ::CreateFontW(
        -pixelHeight, 0, 0, 0,
        bold ? FW_SEMIBOLD : FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        i18n::UiFontName());
    if (hFont == NULL)
    {
        return 0;
    }
    HFONT hOldFont = (HFONT)::SelectObject(hdc, hFont);
    SIZE sz = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text, ::lstrlenW(text), &sz);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
    return sz.cx;
}

// 命中高亮黄底相对所在文字行上下各缩进的距离（逻辑像素），让上下相邻的两行黄底不连成一片。
const int kHighlightInsetY = 1;

// 在一行文字的指定子串下方画搜索命中黄底：用与 DrawGdiText 完全相同的字体测量子串的
// 左偏移与宽度，保证黄底与随后画上去的文字对齐。名称与副行共用这一份实现。
//   text：整行文字；hiStart / hiLen：高亮的字符区间（越界会被裁剪，非法时什么都不画）；
//   pixelHeight：该行文字的字号；lineRc：该行文字的绘制矩形，黄底不越过它的右界。
void DrawSubstringHighlight(HDC hdc, const CString& text, int hiStart, int hiLen,
                            int pixelHeight, const RECT& lineRc)
{
    const int textLen = text.GetLength();
    if (hiStart < 0 || hiLen <= 0 || hiStart >= textLen)
    {
        return;
    }
    if (hiStart + hiLen > textLen)
    {
        hiLen = textLen - hiStart;
    }

    HFONT hMeasure = ::CreateFontW(
        -pixelHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, i18n::UiFontName());
    if (hMeasure == NULL)
    {
        return;
    }
    HFONT hOld = (HFONT)::SelectObject(hdc, hMeasure);
    SIZE szPre = { 0, 0 };
    SIZE szHi  = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, (LPCWSTR)text, hiStart, &szPre);
    ::GetTextExtentPoint32W(hdc, ((LPCWSTR)text) + hiStart, hiLen, &szHi);
    ::SelectObject(hdc, hOld);
    ::DeleteObject(hMeasure);

    const int hiLeft  = lineRc.left + szPre.cx;
    int       hiRight = hiLeft + szHi.cx;
    if (hiRight > lineRc.right)
    {
        hiRight = lineRc.right;   // 不越过文字区右界（与省略号区一致）
    }
    if (hiRight > hiLeft)
    {
        RECT hl = { hiLeft, lineRc.top + kHighlightInsetY, hiRight, lineRc.bottom - kHighlightInsetY };
        HBRUSH hb = ::CreateSolidBrush(kHighlightFill);
        ::FillRect(hdc, &hl, hb);
        ::DeleteObject(hb);
    }
}

// 按底色明度选头像文字色：浅底用深字、深底用白字（服务号头像底色偏浅）。
COLORREF AvatarTextColor(COLORREF bg)
{
    int lum = (GetRValue(bg) * 299 + GetGValue(bg) * 587
               + GetBValue(bg) * 114) / 1000;
    return (lum > 150) ? RGB(0x4A, 0x4A, 0x4A) : RGB(255, 255, 255);
}

} // anonymous namespace

MainPanelListItem::MainPanelListItem()
    : m_avatarColor(RGB(0xFF, 0x7A, 0x45))
    , m_avatarBitmap(NULL)
    , m_nameHiStart(-1)
    , m_nameHiLen(0)
    , m_subHiStart(-1)
    , m_subHiLen(0)
    , m_keySelected(false)
    , m_showStatus(false)
    , m_status(kMockOffline)
    , m_badgeCount(0)
    , m_hoverFill(RGB(0xF4, 0xF3, 0xF0))   // design --hover 浅灰（默认，与主面板列表一致）
    , m_hotAction(-1)                      // 默认没有悬停按钮（m_hoverActions 为空）
    , m_pressedAction(-1)
{
}

MainPanelListItem::~MainPanelListItem()
{
    if (m_avatarBitmap != NULL)
    {
        ::DeleteObject(m_avatarBitmap);
        m_avatarBitmap = NULL;
    }
}

void MainPanelListItem::SetAvatar(LPCTSTR text, COLORREF color)
{
    m_avatarText  = (text != NULL) ? text : _T("");
    m_avatarColor = color;
    Invalidate();
}

void MainPanelListItem::SetOwnedAvatarBitmap(HBITMAP hbm)
{
    if (m_avatarBitmap == hbm)
    {
        return;   //同一张图重复设，什么都不做（尤其别把它删了又留着指针）
    }
    if (m_avatarBitmap != NULL)
    {
        ::DeleteObject(m_avatarBitmap);
    }
    m_avatarBitmap = hbm;   //接管所有权
    Invalidate();
}

void MainPanelListItem::SetName(LPCTSTR name)
{
    m_name = (name != NULL) ? name : _T("");
    Invalidate();
}

void MainPanelListItem::SetNameHighlight(int start, int len)
{
    if (len <= 0 || start < 0)
    {
        m_nameHiStart = -1;
        m_nameHiLen   = 0;
    }
    else
    {
        m_nameHiStart = start;
        m_nameHiLen   = len;
    }
    Invalidate();
}

void MainPanelListItem::SetKeySelected(bool on)
{
    if (m_keySelected != on)
    {
        m_keySelected = on;
        Invalidate();
    }
}

void MainPanelListItem::SetSubHighlight(int start, int len)
{
    if (len <= 0 || start < 0)
    {
        m_subHiStart = -1;
        m_subHiLen   = 0;
    }
    else
    {
        m_subHiStart = start;
        m_subHiLen   = len;
    }
    Invalidate();
}

void MainPanelListItem::SetSubText(LPCTSTR sub)
{
    m_subText = (sub != NULL) ? sub : _T("");
    Invalidate();
}

void MainPanelListItem::SetStatus(bool show, int status)
{
    m_showStatus = show;
    m_status     = status;
    Invalidate();
}

void MainPanelListItem::SetRightText(LPCTSTR text)
{
    m_rightText = (text != NULL) ? text : _T("");
    Invalidate();
}

void MainPanelListItem::SetBadgeCount(int count)
{
    m_badgeCount = (count > 0) ? count : 0;
    Invalidate();
}

void MainPanelListItem::SetHoverFill(COLORREF c)
{
    m_hoverFill = c;
    Invalidate();
}

void MainPanelListItem::SetLocateButtonVisible(bool show)
{
    std::vector<int> actions;
    if (show)
    {
        actions.push_back(kHoverActionLocate);
    }
    SetHoverActions(actions);
}

void MainPanelListItem::SetHoverActions(const std::vector<int>& actions)
{
    std::vector<int> valid;
    for (size_t i = 0; i < actions.size(); ++i)
    {
        if (actions[i] == kHoverActionLocate || actions[i] == kHoverActionOpenFile
            || actions[i] == kHoverActionOpenFolder)
        {
            valid.push_back(actions[i]);
        }
    }
    if (valid == m_hoverActions)
    {
        return;
    }
    m_hoverActions = valid;
    //换了一组按钮：命中态与按下态都对应旧的下标，一并复位。
    m_hotAction = -1;
    m_pressedAction = -1;
    Invalidate();
}

RECT MainPanelListItem::HoverActionRect(size_t index) const
{
    RECT rc = { 0, 0, 0, 0 };

    const int h = m_rcItem.bottom - m_rcItem.top;
    if (h <= 0 || index >= m_hoverActions.size())
    {
        return rc;   //尚未布局（或行高为 0），或下标越界
    }

    //最右一个按钮贴着行右内距，往左依次排开。
    const size_t fromRight = m_hoverActions.size() - 1 - index;
    rc.right  = m_rcItem.right - kRowRightPad - kActionIconRightGap
              - (int)fromRight * (kActionIconSize + kActionIconGap);
    rc.left   = rc.right - kActionIconSize;
    rc.top    = m_rcItem.top + (h - kActionIconSize) / 2;   //垂直居中
    rc.bottom = rc.top + kActionIconSize;
    return rc;
}

int MainPanelListItem::HitTestHoverAction(POINT pt) const
{
    for (size_t i = 0; i < m_hoverActions.size(); ++i)
    {
        RECT rc = HoverActionRect(i);
        if (rc.right > rc.left && ::PtInRect(&rc, pt))
        {
            return (int)i;
        }
    }
    return -1;
}

bool MainPanelListItem::HoverActionsShown() const
{
    return !m_hoverActions.empty() && (IsHover() || m_keySelected);
}

bool MainPanelListItem::OnMouseEnter()
{
    bool r = balloonwjui::DuiButton::OnMouseEnter();   // 基类置 m_bHover=true 并通知父
    // 刚进入本行时先按"鼠标不在按钮上"起步 —— 紧随其后的 OnMouseMove 会立刻把真实
    // 落点算出来，不会停在错误状态上。
    m_hotAction = -1;
    Invalidate();                                      // 基类不重绘，这里补一次让 hover 底色立刻显示
    return r;
}

bool MainPanelListItem::OnMouseLeave()
{
    bool r = balloonwjui::DuiButton::OnMouseLeave();   // 基类置 m_bHover=false 并通知父
    // 鼠标已离开本行，按钮随 hover 底色一起消失，命中态必须跟着复位；否则下次
    // 移进来时会先闪一帧加深色的图标。
    m_hotAction = -1;
    // 按下态一并复位：按住左键拖出本行时不会有松开事件传到这里，不复位的话它会
    // 一直留着，让下一次真正的点击被误判成点了按钮。
    m_pressedAction = -1;
    Invalidate();                                      // 补一次让 hover 底色立刻消失
    return r;
}

bool MainPanelListItem::OnMouseMove(POINT pt, UINT mkFlags)
{
    bool r = balloonwjui::DuiButton::OnMouseMove(pt, mkFlags);

    // 只在"鼠标落在哪个按钮上"发生变化时才重绘。鼠标在行内移动是高频事件，每次都
    // Invalidate 会让整张下拉不停刷新。
    const int hot = HitTestHoverAction(pt);
    if (hot != m_hotAction)
    {
        m_hotAction = hot;
        Invalidate();
    }

    return r;
}

bool MainPanelListItem::OnLButtonDown(POINT pt, UINT mkFlags)
{
    // 记下这一次按下的落点在哪个按钮上。只判松开点是不够的：在行的空白处按下、拖到
    // 按钮上再松开，本该算一次普通的行点击（打开会话），只判松开点会把它当成点了按钮。
    m_pressedAction = HitTestHoverAction(pt);

    return balloonwjui::DuiButton::OnLButtonDown(pt, mkFlags);
}

bool MainPanelListItem::OnLButtonUp(POINT pt, UINT mkFlags)
{
    // 按下与松开的落点都在同一个按钮上：本次点击只表示该按钮的动作（定位、打开文件、
    // 打开所在文件夹），绝不能再走基类那条 DUIN_CLICK 通路 —— 否则宿主会既执行按钮的
    // 动作又打开这一行。
    //
    // 拦截手法是把基类调用改派到一个<u>落在本行之外</u>的坐标上：DuiButton::OnLButtonUp
    // 只有在"按下时在按钮内、松开时也在按钮内"两个条件同时成立时才发 DUIN_CLICK
    // （见 DuiButton.h 的行为约定），松开点在外就走它自己的取消路径。这样按下态、
    // 鼠标捕获、重绘等收尾工作仍由基类完成，本类不必去碰基类的私有成员，也就无需
    // 改动 balloonui 库。
    const int pressed = m_pressedAction;
    m_pressedAction = -1;   // 一次点击用完即复位，不跨事件保留

    if (pressed >= 0 && pressed == HitTestHoverAction(pt))
    {
        POINT ptOutside;
        ptOutside.x = m_rcItem.left - 1;
        ptOutside.y = m_rcItem.top - 1;
        balloonwjui::DuiButton::OnLButtonUp(ptOutside, mkFlags);

        UINT code = DUIN_LISTITEM_LOCATE;
        switch (m_hoverActions[(size_t)pressed])
        {
        //「打开文件」按钮
        case kHoverActionOpenFile:
            code = DUIN_LISTITEM_OPEN_FILE;
            break;
        //「打开所在文件夹」按钮
        case kHoverActionOpenFolder:
            code = DUIN_LISTITEM_OPEN_FOLDER;
            break;
        //「定位」按钮（SetHoverActions 只收下三种取值，不会有别的）
        default:
            code = DUIN_LISTITEM_LOCATE;
            break;
        }
        NotifyParent(code);
        return true;
    }

    return balloonwjui::DuiButton::OnLButtonUp(pt, mkFlags);
}

void MainPanelListItem::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const int x = m_rcItem.left;
    const int y = m_rcItem.top;
    const int w = m_rcItem.right  - m_rcItem.left;
    const int h = m_rcItem.bottom - m_rcItem.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    const int avX = x + kAvatarLeft;
    const int avY = y + (h - kAvatarSize) / 2;
    const int textLeft = avX + kAvatarSize + kAvatarTextGap;

    // ---- GDI+ 形状层 ----
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        // 悬停底（行内缩 3px，留出圆角呼吸位）。底色取 m_hoverFill（可经
        // SetHoverFill 调整，默认 design --hover 浅灰）。被键盘选中的行（搜索下拉里用上下
        // 方向键移到的那一行）画同样的底色，鼠标与键盘两种选中看起来一致。
        if (IsHover() || m_keySelected)
        {
            Gdiplus::GraphicsPath hover;
            AddRoundRect(hover, (Gdiplus::REAL)(x + 3), (Gdiplus::REAL)(y + 1),
                         (Gdiplus::REAL)(w - 6), (Gdiplus::REAL)(h - 2), 6.0f);
            Gdiplus::SolidBrush hb(Gdiplus::Color(255,
                GetRValue(m_hoverFill), GetGValue(m_hoverFill), GetBValue(m_hoverFill)));
            g.FillPath(&hb, &hover);
        }

        // 头像圆角底。有真图时不画底色方块 —— 真图本身就是一张裁好的圆角方图，
        // 稍后在 GDI 层用 AlphaBlend 盖上去（同 MainPanelHeader 的处理）。
        if (m_avatarBitmap == NULL)
        {
            Gdiplus::GraphicsPath avatar;
            AddRoundRect(avatar, (Gdiplus::REAL)avX, (Gdiplus::REAL)avY,
                         (Gdiplus::REAL)kAvatarSize, (Gdiplus::REAL)kAvatarSize,
                         kAvatarRadius);
            Gdiplus::SolidBrush ab(Gdiplus::Color(255,
                GetRValue(m_avatarColor), GetGValue(m_avatarColor),
                GetBValue(m_avatarColor)));
            // 按像素格对齐地填充，与头像字、照片头像对齐（见 AvatarGlyph.h）
            FillAvatarBackground(g, ab, avatar);
        }

        // 头像状态点（离线不画）。
        if (m_showStatus && m_status != kMockOffline)
        {
            const Gdiplus::REAL dotCx = (Gdiplus::REAL)(avX + kAvatarSize) - kStatusDotSize / 2.0f - 1.0f;
            const Gdiplus::REAL dotCy = (Gdiplus::REAL)(avY + kAvatarSize) - kStatusDotSize / 2.0f - 1.0f;
            // 先垫一圈白描边，把徽标与头像本身的颜色隔开（否则深色头像上的徽标会糊进去）。
            // 白圈按徽标<u>圆底</u>的半径外扩，而不是按绘制方框 —— 方框比圆底大一圈，
            // 照方框外扩会让白圈离徽标太远、看着像单独的一个白点。
            const Gdiplus::REAL discR = kStatusDotSize * kBadgeDiscRatio / 2.0f;
            const Gdiplus::REAL ringR = discR + kStatusRingW;
            Gdiplus::SolidBrush ring(kStatusRing);
            g.FillEllipse(&ring, dotCx - ringR, dotCy - ringR, ringR * 2.0f, ringR * 2.0f);

            // 徽标本体：字形与配色都取自共用映射，与通讯录列表里同一个人身上的那枚一致。
            DrawPresenceBadge(g, MockPresenceFromStatus(m_status), false,
                              dotCx - kStatusDotSize / 2.0f,
                              dotCy - kStatusDotSize / 2.0f,
                              kStatusDotSize);
        }

        // 未读角标。
        if (m_badgeCount > 0)
        {
            const Gdiplus::REAL bx = (Gdiplus::REAL)(x + w - kRowRightPad) - kBadgeDiameter;
            const Gdiplus::REAL by = (Gdiplus::REAL)y + (Gdiplus::REAL)(h) / 2.0f - kBadgeDiameter / 2.0f;
            Gdiplus::SolidBrush bb(kBadgeRed);
            g.FillEllipse(&bb, bx, by, kBadgeDiameter, kBadgeDiameter);
        }
    }

    // ---- GDI 位图层：真实头像 ----
    //
    // 真图盖在圆角底的位置上（底色方块此时没画）。位图本身已按行内头像边长裁成
    // 圆角方形，故这里等尺寸贴上即可；尺寸对不上时按位图实际大小拉伸，不裁切。
    if (m_avatarBitmap != NULL)
    {
        BITMAP bm = {};
        ::GetObject(m_avatarBitmap, sizeof(bm), &bm);
        if (bm.bmWidth > 0 && bm.bmHeight > 0)
        {
            HDC     mem = ::CreateCompatibleDC(hdc);
            HGDIOBJ old = ::SelectObject(mem, m_avatarBitmap);
            BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            ::AlphaBlend(hdc, avX, avY, kAvatarSize, kAvatarSize,
                         mem, 0, 0, bm.bmWidth, bm.bmHeight, bf);
            ::SelectObject(mem, old);
            ::DeleteDC(mem);
        }
    }

    // ---- GDI 文字层 ----

    // 头像首字 / emoji（有真图时不画 —— 那张图就是头像本身）。
    if (m_avatarBitmap == NULL && !m_avatarText.IsEmpty())
    {
        RECT avRc = { avX, avY, avX + kAvatarSize, avY + kAvatarSize };
        // 按字形笔画外框居中（见 AvatarGlyph.h），DrawText 按单元格居中会让字偏低（emoji 等取不到字形度量时内部退回原画法）
        DrawCenteredGlyphGdi(hdc, (LPCWSTR)m_avatarText, avRc, 16, true, AvatarTextColor(m_avatarColor));
    }

    // 名称 + 副行（整体垂直居中）。
    const bool hasSub = !m_subText.IsEmpty();
    const int  blockH = hasSub ? 33 : 18;
    const int  blockTop = y + (h - blockH) / 2;

    // 悬停按钮显示时，第一行右侧的时间文字让位给按钮，不画。
    const bool actionsShown = HoverActionsShown();
    const bool drawRightText = !m_rightText.IsEmpty() && !actionsShown;

    // 第一行右侧文字（时间 / 群成员数）先量宽 —— 它与名称同处第一行，名称的右界要
    // 为它让位，否则长名称会一直画到它底下、两段字叠在一起。
    int rightTextW = 0;
    if (drawRightText)
    {
        rightTextW = MeasureGdiTextWidth(hdc, (LPCWSTR)m_rightText, kRightPx, false);
        if (rightTextW <= 0)
        {
            rightTextW = kRightTextFallbackW;
        }
    }

    // 名称 / 副行右界：默认留 kNameRightGap；有未读角标时再让出角标宽度，避免长文字
    // 钻到角标下面（角标垂直居中靠右绘制）。无角标的行（如搜索结果）右界不变。
    int textRight = x + w - kRowRightPad - kNameRightGap;
    if (m_badgeCount > 0)
    {
        textRight = x + w - kRowRightPad - (int)kBadgeDiameter - kBadgeTextGap;
    }
    // 悬停按钮显示时，名称与副行都让到最左一个按钮的左边（再留出浅色圆底的外扩）。
    if (actionsShown)
    {
        const RECT rcFirst = HoverActionRect(0);
        const int limit = rcFirst.left - kActionHotPad - kNameRightGap;
        if (rcFirst.right > rcFirst.left && limit < textRight)
        {
            textRight = limit;
        }
    }

    // 名称还要再为右侧文字让位。副行（第二行）不受影响 —— 它在右侧文字的下一行，
    // 底下那一整条横向空间都归它，群号这类短文字更没必要跟着缩。
    int nameRight = textRight;
    if (rightTextW > 0)
    {
        const int limit = x + w - kRowRightPad - rightTextW - kRightTextGap;
        if (limit < nameRight)
        {
            nameRight = limit;
        }
    }

    RECT nameRc = { textLeft, blockTop, nameRight, blockTop + 18 };

    // 搜索命中高亮：先在名称对应子串处画黄底，再照常画名称文字盖在其上。
    DrawSubstringHighlight(hdc, m_name, m_nameHiStart, m_nameHiLen, kNamePx, nameRc);

    // 名称用常规字重（非半粗）—— 半粗 + 深色会显得过黑过重。
    DrawGdiText(hdc, (LPCWSTR)m_name, nameRc, kNamePx, false, kNameColor,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (hasSub)
    {
        RECT subRc = { textLeft, blockTop + 17, textRight, blockTop + 33 };
        // 副行同样可以有命中高亮（搜索下拉「消息」标签里命中的那句话）。
        DrawSubstringHighlight(hdc, m_subText, m_subHiStart, m_subHiLen, kSubPx, subRc);
        DrawGdiText(hdc, (LPCWSTR)m_subText, subRc, kSubPx, false, kSubColor,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    // 第一行右侧文字（会话行的时间 / 群行的成员数）。两行的行里与名称同高 —— 名称在上、
    // 副行在下时，右侧文字要和名称处在同一条水平线上才不显得飘在两行中间的缝里；单行的
    // 行没有第二行作参照，沿用贴着行右上角的老位置。
    if (drawRightText)
    {
        RECT rightRc;
        rightRc.right = x + w - kRowRightPad;
        rightRc.left  = rightRc.right - rightTextW;
        if (hasSub)
        {
            rightRc.top    = nameRc.top;
            rightRc.bottom = nameRc.bottom;
        }
        else
        {
            rightRc.top    = y + kRightTextTop;
            rightRc.bottom = rightRc.top + kRightTextHeight;
        }
        DrawGdiText(hdc, (LPCWSTR)m_rightText, rightRc, kRightPx, false, kTimeColor,
                    DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }

    // 角标数字。
    if (m_badgeCount > 0)
    {
        CString badgeStr;
        badgeStr.Format(_T("%d"), m_badgeCount);
        const int bx = x + w - kRowRightPad - (int)kBadgeDiameter;
        const int by = y + h / 2 - (int)kBadgeDiameter / 2;
        RECT badgeRc = { bx, by, bx + (int)kBadgeDiameter, by + (int)kBadgeDiameter };
        DrawGdiText(hdc, (LPCWSTR)badgeStr, badgeRc, 10, true, RGB(255, 255, 255),
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }

    // ---- 悬停态小按钮 ----
    //
    // 画在最后，浮在行内其它元素之上。只有本行配了按钮、且鼠标此刻正悬停在本行内（或本行
    // 被键盘选中）时才画：其余时候完全不绘制、也不占位。
    if (actionsShown)
    {
        Gdiplus::Graphics gi(hdc);
        gi.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        for (size_t i = 0; i < m_hoverActions.size(); ++i)
        {
            RECT rcIcon = HoverActionRect(i);
            if (rcIcon.right <= rcIcon.left)
            {
                continue;
            }
            const bool hot = (m_hotAction == (int)i);

            // 鼠标正落在图标上：先垫一层浅色圆底作为可点击反馈。
            if (hot)
            {
                const Gdiplus::REAL hotR = (Gdiplus::REAL)kActionIconSize / 2.0f
                                           + (Gdiplus::REAL)kActionHotPad;
                const Gdiplus::REAL hotCx = (Gdiplus::REAL)(rcIcon.left + rcIcon.right) / 2.0f;
                const Gdiplus::REAL hotCy = (Gdiplus::REAL)(rcIcon.top + rcIcon.bottom) / 2.0f;
                Gdiplus::SolidBrush hotBrush(kActionHotFill);
                gi.FillEllipse(&hotBrush, hotCx - hotR, hotCy - hotR,
                               hotR * 2.0f, hotR * 2.0f);
            }

            // 图标本体。鼠标落在它上面时画深一档，与浅色圆底一起构成"这里可以点"的提示。
            const COLORREF iconColor = hot ? kActionIconHotColor : kActionIconColor;
            Gdiplus::Color iconArgb(255, GetRValue(iconColor), GetGValue(iconColor),
                                    GetBValue(iconColor));
            const Gdiplus::REAL ix = (Gdiplus::REAL)rcIcon.left;
            const Gdiplus::REAL iy = (Gdiplus::REAL)rcIcon.top;
            const Gdiplus::REAL isz = (Gdiplus::REAL)kActionIconSize;
            switch (m_hoverActions[i])
            {
            //「打开文件」：方框 + 右上箭头
            case kHoverActionOpenFile:
                DrawOpenFileIcon(gi, ix, iy, isz, iconArgb);
                break;
            //「打开所在文件夹」：文件夹轮廓
            case kHoverActionOpenFolder:
                DrawFolderIcon(gi, ix, iy, isz, iconArgb);
                break;
            //「定位」：地图定位针
            default:
                DrawLocatePin(gi, ix, iy, isz, iconArgb);
                break;
            }
        }
    }
}

} // namespace balloonmain
