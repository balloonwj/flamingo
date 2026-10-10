/**
 *  ImageViewerToolbar.cpp
 *
 *  ImageViewerToolbar 的实现：GDI+ 画半透明圆角背景条 + 一排矢量图标 / 文字按钮，
 *  内部按 x 命中各按钮，点击调所属 ImageCanvas 的看图动作。矢量图标（放大镜、四角
 *  适应框、旋转圆箭头、下载箭头）用 GDI+ 描边现画，与聊天工具栏的线性图标风格一致。
 *
 *  本 .cpp 用 Flamingo 的 stdafx.h（仅在 Flamingo.vcxproj 编译）。
 *
 *  balloonwj@qq.com   2026-06-04
 */

#include "stdafx.h"

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include <cmath>

#include "ImageViewerToolbar.h"
#include "ImageCanvas.h"
#include "../../../third-party/balloonui/balloonui/DuiHost.h"                         // GetHost()->m_hWnd（ClientToScreen）
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"    // DuiToolTipMgr（按钮 tooltip）
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonimageviewer {

namespace {

// ---- 几何常量（逻辑像素）----

// 工具栏条高度。
const int kBarHeight = 44;
// 条内左右内边距。
const int kBarPadX = 6;
// 图标按钮 / 文字按钮 / 比例读数 / 分隔线各自宽度。
const int kIconCellW = 38;
const int kPctCellW  = 56;
const int kSepCellW  = 14;
// 圆角半径：背景条 / 按钮悬停高亮。
const float kBarRadius   = 10.0f;
const float kHoverRadius = 7.0f;
// 图标描边宽度。
const float kStroke = 1.8f;

// ---- 配色（含 alpha：半透明深色条浮在图片上）----

const Gdiplus::Color kBarFill   (214, 30, 30, 30);    // 背景条填充（~84% 不透明深色）
const Gdiplus::Color kBarBorder (40, 255, 255, 255);  // 背景条 1px 浅色描边
const Gdiplus::Color kHoverFill (38, 255, 255, 255);  // 悬停按钮浅色高亮底
const Gdiplus::Color kIconColor (255, 225, 225, 225);  // 图标 / 文字常态色
const Gdiplus::Color kIconHover (255, 255, 255, 255);  // 图标 / 文字悬停色
const Gdiplus::Color kSepColor  (55, 255, 255, 255);   // 分隔竖线色

// 工具栏元素定义表：种类 + 宽度。顺序即从左到右的排列。
struct TbItemDef
{
    ToolbarItemKind kind;
    int             width;
};

const TbItemDef kItemDefs[] =
{
    { kTbZoomOut,   kIconCellW },
    { kTbZoomPct,   kPctCellW  },
    { kTbZoomIn,    kIconCellW },
    { kTbSeparator, kSepCellW  },
    { kTbActual,     kIconCellW },
    { kTbFit,        kIconCellW },
    { kTbRotate,     kIconCellW },
    { kTbRotateLeft, kIconCellW },
    { kTbSeparator,  kSepCellW  },
    { kTbCopyImage,  kIconCellW },
    { kTbSaveAs,     kIconCellW },
};

const int kItemCount = (int)(sizeof(kItemDefs) / sizeof(kItemDefs[0]));

// 「只看不存」模式（密聊图片的大图浮层，docs/密聊方案.md 4.10）只取上表的前 8 个元素：缩小、比例、放大、分隔线、
// 1:1、适应窗口、两个旋转；之后的分隔线、复制、另存与「图片文字识别」都不出现
const int kViewOnlyItemCount = 8;

// 「图片文字识别」按钮：只在图片文字识别模块可用时出现，接在上表之后
const TbItemDef kOcrItemDef = { kTbOcr, kIconCellW };

// 元素个数：带「图片文字识别」按钮时多一个；「只看不存」模式固定为前 kViewOnlyItemCount 个。
int CountFor(bool withOcr, bool viewOnly)
{
    if (viewOnly)
    {
        return kViewOnlyItemCount;
    }
    return kItemCount + (withOcr ? 1 : 0);
}

// 第 index 个元素的定义；调用方保证 index 在 0 ~ CountFor(withOcr, viewOnly)-1 之内（只看不存模式下 index 不会到达
// 「图片文字识别」按钮的位置）。
const TbItemDef& DefAt(int index, bool withOcr)
{
    if (withOcr && index == kItemCount)
    {
        return kOcrItemDef;
    }
    return kItemDefs[index];
}

// 工具栏内容总宽（不含左右内边距）。
int ContentWidth(bool withOcr, bool viewOnly)
{
    int sum = 0;
    for (int i = 0; i < CountFor(withOcr, viewOnly); ++i)
    {
        sum += DefAt(i, withOcr).width;
    }
    return sum;
}

// 往 path 加一个圆角矩形（GDI+）。
void AddRoundRect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rc, float r)
{
    float d = r * 2.0f;
    path.AddArc(rc.X, rc.Y, d, d, 180.0f, 90.0f);
    path.AddArc(rc.X + rc.Width - d, rc.Y, d, d, 270.0f, 90.0f);
    path.AddArc(rc.X + rc.Width - d, rc.Y + rc.Height - d, d, d, 0.0f, 90.0f);
    path.AddArc(rc.X, rc.Y + rc.Height - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

// 画放大镜图标（镜片 + 手柄 + 镜片内 -/+ ）。plus=true 画加号（放大）、false 画减号（缩小）。
void DrawMagnifier(Gdiplus::Graphics& g, Gdiplus::Pen& pen, float cx, float cy, bool plus)
{
    // 镜片：直径 11，略偏左上。
    const float d = 11.0f;
    const float lx = cx - 4.0f;
    const float ly = cy - 4.0f;
    g.DrawEllipse(&pen, lx - d / 2.0f, ly - d / 2.0f, d, d);
    // 手柄：从镜片右下沿向外。
    g.DrawLine(&pen, lx + 3.7f, ly + 3.7f, cx + 7.0f, cy + 7.0f);
    // 镜片内 -（横）/ +（横+竖）。
    g.DrawLine(&pen, lx - 2.6f, ly, lx + 2.6f, ly);
    if (plus)
    {
        g.DrawLine(&pen, lx, ly - 2.6f, lx, ly + 2.6f);
    }
}

// 画"适应窗口"四角框图标。
void DrawFitFrame(Gdiplus::Graphics& g, Gdiplus::Pen& pen, float cx, float cy)
{
    const float s = 15.0f;          // 方框边长
    const float arm = 4.5f;         // 角臂长
    const float ox = cx - s / 2.0f;
    const float oy = cy - s / 2.0f;
    // 左上角。
    g.DrawLine(&pen, ox, oy + arm, ox, oy);
    g.DrawLine(&pen, ox, oy, ox + arm, oy);
    // 右上角。
    g.DrawLine(&pen, ox + s - arm, oy, ox + s, oy);
    g.DrawLine(&pen, ox + s, oy, ox + s, oy + arm);
    // 左下角。
    g.DrawLine(&pen, ox, oy + s - arm, ox, oy + s);
    g.DrawLine(&pen, ox, oy + s, ox + arm, oy + s);
    // 右下角。
    g.DrawLine(&pen, ox + s - arm, oy + s, ox + s, oy + s);
    g.DrawLine(&pen, ox + s, oy + s - arm, ox + s, oy + s);
}

// 画顺时针旋转圆箭头图标。
void DrawRotate(Gdiplus::Graphics& g, Gdiplus::Pen& pen, const Gdiplus::Color& color,
                float cx, float cy)
{
    const float r = 8.0f;
    Gdiplus::RectF arcRc(cx - r, cy - r, r * 2.0f, r * 2.0f);
    // 顺时针弧：从上方偏右起、顺时针扫 270°，顶部留缺口给箭头。
    const float startDeg = 300.0f;
    g.DrawArc(&pen, arcRc, startDeg, 270.0f);

    // 箭头画在弧的起点（顺时针切线方向）。
    const double pi = 3.14159265358979323846;
    double a = startDeg * pi / 180.0;
    float sx = cx + r * (float)cos(a);
    float sy = cy + r * (float)sin(a);
    double t = a + pi / 2.0;                 // 顺时针切线方向
    float tx = (float)cos(t);
    float ty = (float)sin(t);
    float px = -ty;                          // 切线的垂直方向
    float py = tx;
    // 小三角箭头。
    Gdiplus::PointF tip(sx + tx * 4.0f, sy + ty * 4.0f);
    Gdiplus::PointF baseC(sx - tx * 1.5f, sy - ty * 1.5f);
    Gdiplus::PointF w1(baseC.X + px * 3.2f, baseC.Y + py * 3.2f);
    Gdiplus::PointF w2(baseC.X - px * 3.2f, baseC.Y - py * 3.2f);
    Gdiplus::PointF pts[3] = { tip, w1, w2 };
    Gdiplus::SolidBrush fill(color);
    g.FillPolygon(&fill, pts, 3);
}

// 画"另存为"下载箭头图标（向下箭头 + 托盘底线）。
void DrawDownload(Gdiplus::Graphics& g, Gdiplus::Pen& pen, float cx, float cy)
{
    // 箭头竖杆。
    g.DrawLine(&pen, cx, cy - 8.0f, cx, cy + 3.0f);
    // 箭头头部。
    g.DrawLine(&pen, cx - 4.0f, cy - 1.0f, cx, cy + 3.0f);
    g.DrawLine(&pen, cx + 4.0f, cy - 1.0f, cx, cy + 3.0f);
    // 托盘底线。
    g.DrawLine(&pen, cx - 7.0f, cy + 7.0f, cx + 7.0f, cy + 7.0f);
}

// 「复制图片」图标的几何尺寸（像素，相对按钮中心）：两张叠放的纸，前一张完整画出，
// 后一张向左上错开、只画露在外面的那一截折线，这样不必先涂底色去遮挡。
const float kCopySheetW      = 9.0f;    // 每张纸的宽
const float kCopySheetH      = 11.0f;   // 每张纸的高
const float kCopyFrontLeft   = -3.0f;   // 前一张纸左边相对按钮中心的偏移
const float kCopyFrontTop    = -4.0f;   // 前一张纸上边相对按钮中心的偏移
const float kCopySheetOffset = 4.0f;    // 后一张纸相对前一张向左、向上错开的距离
const float kCopyCornerR     = 1.5f;    // 前一张纸的圆角半径

// 画「复制图片」图标：两张叠放的纸。
void DrawCopy(Gdiplus::Graphics& g, Gdiplus::Pen& pen, float cx, float cy)
{
    // 前一张纸：完整的圆角矩形。
    const float fl = cx + kCopyFrontLeft;
    const float ft = cy + kCopyFrontTop;
    Gdiplus::GraphicsPath front;
    AddRoundRect(front, Gdiplus::RectF(fl, ft, kCopySheetW, kCopySheetH), kCopyCornerR);
    g.DrawPath(&pen, &front);

    // 后一张纸：只画露在前一张左边、上边之外的折线（左下 → 左上 → 右上 → 右边落到前一张顶边）。
    const float bl = fl - kCopySheetOffset;
    const float bt = ft - kCopySheetOffset;
    const float br = bl + kCopySheetW;
    const float bb = bt + kCopySheetH;
    Gdiplus::PointF back[] =
    {
        Gdiplus::PointF(fl, bb),
        Gdiplus::PointF(bl, bb),
        Gdiplus::PointF(bl, bt),
        Gdiplus::PointF(br, bt),
        Gdiplus::PointF(br, ft),
    };
    g.DrawLines(&pen, back, (INT)(sizeof(back) / sizeof(back[0])));
}

// 「图片文字识别」图标的几何（相对按钮中心，逻辑像素）：四角取景框里一个「T」
const float kOcrFrameHalf = 7.5f;   // 取景框半边长
const float kOcrFrameArm  = 4.0f;   // 取景框每个角的角臂长
const float kOcrTHalfW    = 3.5f;   // 「T」横笔半宽
const float kOcrTTop      = -3.5f;  // 「T」横笔所在的纵坐标
const float kOcrTBottom   = 4.0f;   // 「T」竖笔下端的纵坐标

// 画「图片文字识别」图标：四个角的取景框（表示扫描），中间一个「T」（表示文字）。
void DrawOcrGlyph(Gdiplus::Graphics& g, Gdiplus::Pen& pen, float cx, float cy)
{
    const float l = cx - kOcrFrameHalf;
    const float t = cy - kOcrFrameHalf;
    const float r = cx + kOcrFrameHalf;
    const float b = cy + kOcrFrameHalf;
    // 左上、右上、右下、左下四个角。
    g.DrawLine(&pen, l, t + kOcrFrameArm, l, t);
    g.DrawLine(&pen, l, t, l + kOcrFrameArm, t);
    g.DrawLine(&pen, r - kOcrFrameArm, t, r, t);
    g.DrawLine(&pen, r, t, r, t + kOcrFrameArm);
    g.DrawLine(&pen, r, b - kOcrFrameArm, r, b);
    g.DrawLine(&pen, r, b, r - kOcrFrameArm, b);
    g.DrawLine(&pen, l + kOcrFrameArm, b, l, b);
    g.DrawLine(&pen, l, b, l, b - kOcrFrameArm);
    // 中间的「T」：一横一竖。
    g.DrawLine(&pen, cx - kOcrTHalfW, cy + kOcrTTop, cx + kOcrTHalfW, cy + kOcrTTop);
    g.DrawLine(&pen, cx, cy + kOcrTTop, cx, cy + kOcrTBottom);
}

} // anonymous namespace

ImageViewerToolbar::ImageViewerToolbar(ImageCanvas* pCanvas, bool withOcr, bool viewOnly)
    : m_pCanvas(pCanvas)
    , m_hoverIndex(-1)
    , m_withOcr(withOcr && !viewOnly)
    , m_viewOnly(viewOnly)
{
}

ImageViewerToolbar::~ImageViewerToolbar()
{
    // DuiToolTipMgr 按裸指针登记，必须在析构里取消注册，否则其后 hover 拿到已销毁
    // 指针会崩（见 DuiToolTip.h 注释）。
    balloonwjui::DuiToolTipMgr::Inst().Unregister(this);
}

SIZE ImageViewerToolbar::GetBarSize() const
{
    SIZE s;
    s.cx = BarWidth(m_withOcr, m_viewOnly);
    s.cy = kBarHeight;
    return s;
}

int ImageViewerToolbar::BarWidth(bool withOcr, bool viewOnly)
{
    return ContentWidth(withOcr, viewOnly) + kBarPadX * 2;
}

void ImageViewerToolbar::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const RECT rc = GetRect();
    const float left = (float)rc.left;
    const float top = (float)rc.top;
    const float w = (float)(rc.right - rc.left);
    const float h = (float)(rc.bottom - rc.top);
    if (w <= 0 || h <= 0)
    {
        return;
    }

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);

    // 背景圆角条 + 浅色描边。
    Gdiplus::GraphicsPath barPath;
    AddRoundRect(barPath, Gdiplus::RectF(left, top, w - 1.0f, h - 1.0f), kBarRadius);
    Gdiplus::SolidBrush barBrush(kBarFill);
    g.FillPath(&barBrush, &barPath);
    Gdiplus::Pen barPen(kBarBorder, 1.0f);
    g.DrawPath(&barPen, &barPath);

    // 文字字体（比例读数 / "1:1"）。
    Gdiplus::Font font(L"Segoe UI", 12.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::StringFormat fmt;
    fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    // 逐元素绘制。
    float x = left + kBarPadX;
    for (int i = 0; i < CountFor(m_withOcr, m_viewOnly); ++i)
    {
        const TbItemDef& def = DefAt(i, m_withOcr);
        const float cw = (float)def.width;
        const float cx = x + cw / 2.0f;
        const float cy = top + h / 2.0f;

        const bool clickable = IsClickable(i);
        const bool hovered = (i == m_hoverIndex);
        const Gdiplus::Color& ink = hovered ? kIconHover : kIconColor;

        // 悬停高亮底（仅可点击元素）。
        if (clickable && hovered)
        {
            Gdiplus::GraphicsPath hp;
            AddRoundRect(hp, Gdiplus::RectF(x + 2.0f, top + 5.0f, cw - 4.0f, h - 10.0f),
                         kHoverRadius);
            Gdiplus::SolidBrush hb(kHoverFill);
            g.FillPath(&hb, &hp);
        }

        Gdiplus::Pen pen(ink, kStroke);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        pen.SetLineJoin(Gdiplus::LineJoinRound);

        switch (def.kind)
        {
        case kTbZoomOut:
            DrawMagnifier(g, pen, cx, cy, false);
            break;
        case kTbZoomIn:
            DrawMagnifier(g, pen, cx, cy, true);
            break;
        case kTbFit:
            DrawFitFrame(g, pen, cx, cy);
            break;
        case kTbRotate:
            // 向右旋转（顺时针）。
            DrawRotate(g, pen, ink, cx, cy);
            break;
        case kTbRotateLeft:
        {
            // 向左旋转（逆时针）：对顺时针图标做水平镜像即得逆时针图标。
            Gdiplus::Matrix saved;
            g.GetTransform(&saved);
            g.TranslateTransform(cx, cy);
            g.ScaleTransform(-1.0f, 1.0f);
            g.TranslateTransform(-cx, -cy);
            DrawRotate(g, pen, ink, cx, cy);
            g.SetTransform(&saved);
            break;
        }
        case kTbSaveAs:
            DrawDownload(g, pen, cx, cy);
            break;
        //复制图片：两张叠放的纸
        case kTbCopyImage:
            DrawCopy(g, pen, cx, cy);
            break;
        //图片文字识别：取景框里一个「T」
        case kTbOcr:
            DrawOcrGlyph(g, pen, cx, cy);
            break;
        case kTbActual:
        {
            // "1:1" 文字按钮。
            Gdiplus::SolidBrush tb(ink);
            Gdiplus::RectF cell(x, top, cw, h);
            g.DrawString(L"1:1", -1, &font, cell, &fmt, &tb);
            break;
        }
        case kTbZoomPct:
        {
            // 当前缩放比例读数。
            CString pct = (m_pCanvas != NULL) ? m_pCanvas->GetZoomReadout() : CString(_T("--"));
            Gdiplus::SolidBrush tb(kIconColor);
            Gdiplus::RectF cell(x, top, cw, h);
            g.DrawString((LPCWSTR)pct, -1, &font, cell, &fmt, &tb);
            break;
        }
        case kTbSeparator:
        {
            // 居中竖线。
            Gdiplus::Pen sp(kSepColor, 1.0f);
            g.DrawLine(&sp, cx, top + 11.0f, cx, top + h - 11.0f);
            break;
        }
        default:
            break;
        }

        x += cw;
    }
}

bool ImageViewerToolbar::OnLButtonDown(POINT pt, UINT /*mkFlags*/)
{
    int idx = HitItemIndex(pt);
    if (idx >= 0 && IsClickable(idx))
    {
        TriggerItem(idx);
    }
    // 落在工具栏上的点击一律消费，避免穿透到画布触发平移。
    return true;
}

bool ImageViewerToolbar::OnMouseMove(POINT pt, UINT /*mkFlags*/)
{
    int idx = HitItemIndex(pt);
    int newHover = (idx >= 0 && IsClickable(idx)) ? idx : -1;
    if (newHover != m_hoverIndex)
    {
        m_hoverIndex = newHover;
        Invalidate();        // 悬停态变化须主动重绘（基类 OnMouseEnter/Leave 不重绘）
        UpdateTooltip(pt);   // 按新悬停按钮切换 tooltip 文字
    }
    return true;
}

bool ImageViewerToolbar::OnMouseLeave()
{
    if (m_hoverIndex != -1)
    {
        m_hoverIndex = -1;
        Invalidate();
    }
    // 离开工具栏：关掉本控件的 tooltip（待显示 / 已显示）。
    balloonwjui::DuiToolTipMgr::Inst().OnLeave(this);
    return true;
}

bool ImageViewerToolbar::OnSetCursor(POINT pt)
{
    int idx = HitItemIndex(pt);
    if (idx >= 0 && IsClickable(idx))
    {
        ::SetCursor(::LoadCursor(NULL, IDC_HAND));
        return true;
    }
    return false;
}

int ImageViewerToolbar::HitItemIndex(POINT ptHost) const
{
    const RECT rc = GetRect();
    int localX = ptHost.x - rc.left;
    int x = kBarPadX;
    if (localX < x)
    {
        return -1;
    }
    for (int i = 0; i < CountFor(m_withOcr, m_viewOnly); ++i)
    {
        const int w = DefAt(i, m_withOcr).width;
        if (localX >= x && localX < x + w)
        {
            return i;
        }
        x += w;
    }
    return -1;
}

int ImageViewerToolbar::ItemCount(bool withOcr, bool viewOnly)
{
    return CountFor(withOcr, viewOnly);
}

ToolbarItemKind ImageViewerToolbar::ItemKindAt(int index, bool withOcr, bool viewOnly)
{
    if (index < 0 || index >= CountFor(withOcr, viewOnly))
    {
        return kTbSeparator;
    }
    return DefAt(index, withOcr).kind;
}

bool ImageViewerToolbar::IsClickableKind(ToolbarItemKind kind)
{
    return (kind != kTbZoomPct && kind != kTbSeparator);
}

bool ImageViewerToolbar::IsClickable(int index) const
{
    if (index < 0 || index >= CountFor(m_withOcr, m_viewOnly))
    {
        return false;
    }
    return IsClickableKind(DefAt(index, m_withOcr).kind);
}

void ImageViewerToolbar::TriggerItem(int index)
{
    if (m_pCanvas == NULL || index < 0 || index >= CountFor(m_withOcr, m_viewOnly))
    {
        return;
    }
    switch (DefAt(index, m_withOcr).kind)
    {
    case kTbZoomOut:
        m_pCanvas->ZoomOut();
        break;
    case kTbZoomIn:
        m_pCanvas->ZoomIn();
        break;
    case kTbActual:
        m_pCanvas->ZoomToActual();
        break;
    case kTbFit:
        m_pCanvas->ZoomToFit();
        break;
    case kTbRotateLeft:
        m_pCanvas->RotateCounterClockwise();
        break;
    case kTbRotate:
        m_pCanvas->RotateClockwise();
        break;
    case kTbSaveAs:
        m_pCanvas->SaveImageAs();
        break;
    //复制图片：写进剪贴板，之后可粘进聊天输入框直接发送
    case kTbCopyImage:
        m_pCanvas->CopyImage();
        break;
    //图片文字识别：按当前旋转方向识别图片里的文字，打开识别结果窗口
    case kTbOcr:
        m_pCanvas->RecognizeText();
        break;
    default:
        break;
    }
}

LPCTSTR ImageViewerToolbar::TooltipFor(int index) const
{
    if (index < 0 || index >= CountFor(m_withOcr, m_viewOnly))
    {
        return _T("");
    }
    return TooltipForKind(DefAt(index, m_withOcr).kind);
}

LPCTSTR ImageViewerToolbar::TooltipForKind(ToolbarItemKind kind)
{
    switch (kind)
    {
    //缩小一档
    case kTbZoomOut:
        return TR(L"缩小");
    //放大一档
    case kTbZoomIn:
        return TR(L"放大");
    //切到 100% 原始像素
    case kTbActual:
        return TR(L"原始尺寸");
    //等比放进窗口
    case kTbFit:
        return TR(L"适应窗口");
    //逆时针旋转 90°
    case kTbRotateLeft:
        return TR(L"向左旋转");
    //顺时针旋转 90°
    case kTbRotate:
        return TR(L"向右旋转");
    //把源文件另存到别处
    case kTbSaveAs:
        return TR(L"另存为");
    //把图片放进剪贴板，可粘进聊天输入框发送
    case kTbCopyImage:
        return TR(L"复制图片");
    //识别图片里的文字
    case kTbOcr:
        return TR(L"图片文字识别");
    //比例读数、分隔线：不可点击，没有提示
    default:
        return _T("");
    }
}

void ImageViewerToolbar::UpdateTooltip(POINT ptHost)
{
    balloonwjui::DuiToolTipMgr& tt = balloonwjui::DuiToolTipMgr::Inst();

    // 先离开：清掉上一个按钮的待显示 / 已显示 tooltip（OnHover 对同一控件有
    // "已 pending 则直接返回"的短路，必须先 OnLeave 复位才能切到新文字）。
    tt.OnLeave(this);

    if (m_hoverIndex < 0 || !IsClickable(m_hoverIndex))
    {
        return;
    }
    LPCTSTR text = TooltipFor(m_hoverIndex);
    if (text == NULL || text[0] == _T('\0'))
    {
        return;
    }

    tt.Register(this, text);

    // tooltip 弹出位置：当前鼠标点换算成屏幕坐标。
    POINT screen = ptHost;
    balloonwjui::DuiHost* host = GetHost();
    if (host != NULL)
    {
        ::ClientToScreen(host->m_hWnd, &screen);
    }
    tt.OnHover(this, screen);
}

} // namespace balloonimageviewer
