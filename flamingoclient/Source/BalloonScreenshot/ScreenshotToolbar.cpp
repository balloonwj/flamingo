/**
 *  ScreenshotToolbar.cpp —— 见同名头文件说明。
 *
 *  所有图标 / 色点 / 线宽样条 / 圆角底，统一用 GDI+（SmoothingModeAntiAlias）绘制，
 *  保证抗锯齿、边缘平滑；文字仍用 GDI DrawText（系统 ClearType）。GDI+ 进程级已由
 *  主程序 GdiplusStartup 初始化。
 *
 *  balloonwj@qq.com   2026-06-05
 */
#include "stdafx.h"
#include "ScreenshotToolbar.h"
#include "AnnotationModel.h"   // 复用 ComputeArrowHead 算箭头翼点
#include "../../balloonui/balloonui/DuiNotify.h"   // DUIN_CLICK

#include <gdiplus.h>
#include <math.h>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

namespace balloonscreenshot {

namespace {

// ---- 视觉常量（集中此处便于微调）----
const COLORREF kBarBg        = RGB(0x2D, 0x2D, 0x2D);   // 深色条背景
const int      kBarRadius    = 9;                        // 条圆角半径
const COLORREF kIconColor    = RGB(0xEC, 0xEC, 0xEC);    // 图标 / 文字（白）
const COLORREF kHoverBg      = RGB(0x47, 0x47, 0x47);    // hover 底
const COLORREF kActiveBg     = RGB(0x3A, 0x3A, 0x3A);    // 选中底
const COLORREF kActiveIcon   = RGB(0x4D, 0x9B, 0xFF);    // 选中图标 / 文字（蓝）
const COLORREF kPrimaryBg    = RGB(0xFF, 0x6B, 0x2C);    // 主按钮（发送到对话框）橙
const COLORREF kPrimaryHover = RGB(0xFF, 0x7E, 0x47);
const COLORREF kWidthSample  = RGB(0xFF, 0x4D, 0x4F);    // 线宽样条红
const int      kBtnInnerRadius = 6;                      // 按钮 hover / 选中底圆角

int MinInt(int a, int b) { return a < b ? a : b; }
int MaxInt(int a, int b) { return a > b ? a : b; }

// COLORREF → GDI+ Color（不透明）。
Color GC(COLORREF c)
{
    return Color(255, GetRValue(c), GetGValue(c), GetBValue(c));
}

RECT InsetRect(const RECT& r, int d)
{
    RECT o = { r.left + d, r.top + d, r.right - d, r.bottom - d };
    return o;
}

// 往 path 加一个圆角矩形子路径。
void AddRoundRect(GraphicsPath& path, REAL x, REAL y, REAL w, REAL h, REAL r)
{
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    REAL d = r * 2;
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

// 抗锯齿填充一个圆角矩形。
void FillRoundRectAA(Graphics& g, const RECT& r, int radius, COLORREF color)
{
    GraphicsPath path;
    AddRoundRect(path, (REAL)r.left, (REAL)r.top,
                 (REAL)(r.right - r.left), (REAL)(r.bottom - r.top), (REAL)radius);
    SolidBrush br(GC(color));
    g.FillPath(&br, &path);
}

// 在 tip 处按 (prev→tip) 方向画箭头头（两翼），GDI+ 抗锯齿。
void DrawArrowHeadAA(Graphics& g, Pen& pen, POINT prev, POINT tip, int headLen)
{
    POINT w1, w2;
    ComputeArrowHead(prev, tip, headLen, w1, w2);
    g.DrawLine(&pen, (REAL)tip.x, (REAL)tip.y, (REAL)w1.x, (REAL)w1.y);
    g.DrawLine(&pen, (REAL)tip.x, (REAL)tip.y, (REAL)w2.x, (REAL)w2.y);
}

// 画一段曲线箭头（撤销 / 重做）。leftward=true 为撤销（开口朝右、末端箭头朝左下）。
void DrawArcArrowAA(Graphics& g, const RECT& a, COLORREF color, REAL lw, bool leftward)
{
    REAL aw = (REAL)(a.right - a.left);
    REAL ah = (REAL)(a.bottom - a.top);
    REAL r = (aw < ah ? aw : ah) / 2.0f;
    if (r < 2.0f)
    {
        return;
    }
    REAL cx = (REAL)(a.left + a.right) / 2.0f;
    REAL cy = (REAL)(a.top + a.bottom) / 2.0f;

    Pen pen(GC(color), lw);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);

    // GDI+ 角度：从 3 点钟方向顺时针为正。撤销画 -40°起、扫 -260°（逆时针）。
    REAL startA = leftward ? -40.0f : 220.0f;
    REAL sweep  = leftward ? -260.0f : 260.0f;
    g.DrawArc(&pen, (REAL)a.left, (REAL)a.top, aw, ah, startA, sweep);

    // 末端点 + 近端点（算箭头朝向）。
    const REAL deg2rad = 3.14159265f / 180.0f;
    REAL endA  = (startA + sweep) * deg2rad;
    REAL nearA = (startA + sweep - (sweep > 0 ? 6.0f : -6.0f)) * deg2rad;
    POINT pe = { (LONG)(cx + r * ::cos(endA)),  (LONG)(cy + r * ::sin(endA)) };
    POINT pn = { (LONG)(cx + r * ::cos(nearA)), (LONG)(cy + r * ::sin(nearA)) };
    DrawArrowHeadAA(g, pen, pn, pe, MaxInt(4, (int)(r / 2)));
}

} // anonymous namespace

void DrawToolIcon(HDC hdc, const RECT& rc, ToolIconKind kind, COLORREF color)
{
    int side = MinInt(rc.right - rc.left, rc.bottom - rc.top);
    int pad = side / 5;
    RECT a = InsetRect(rc, pad);
    REAL lw = (REAL)MaxInt(2, side / 12);

    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);

    Pen pen(GC(color), lw);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);

    REAL ax = (REAL)a.left;
    REAL ay = (REAL)a.top;
    REAL aw = (REAL)(a.right - a.left);
    REAL ah = (REAL)(a.bottom - a.top);

    switch (kind)
    {
    case kIconRect:
    {
        GraphicsPath path;
        AddRoundRect(path, ax, ay, aw, ah, 3.0f);
        g.DrawPath(&pen, &path);
        break;
    }
    case kIconEllipse:
        g.DrawEllipse(&pen, ax, ay, aw, ah);
        break;
    case kIconArrow:
    {
        POINT s = { a.left, a.bottom };
        POINT e = { a.right, a.top };
        g.DrawLine(&pen, (REAL)s.x, (REAL)s.y, (REAL)e.x, (REAL)e.y);
        DrawArrowHeadAA(g, pen, s, e, MaxInt(4, side / 3));
        break;
    }
    case kIconPen:
    {
        POINT s = { a.left, a.bottom };
        POINT e = { a.right, a.top };
        g.DrawLine(&pen, (REAL)s.x, (REAL)s.y, (REAL)e.x, (REAL)e.y);
        int nib = side / 4;
        g.DrawLine(&pen, (REAL)s.x, (REAL)s.y, (REAL)(s.x + nib), (REAL)s.y);
        g.DrawLine(&pen, (REAL)s.x, (REAL)s.y, (REAL)s.x, (REAL)(s.y - nib));
        break;
    }
    case kIconUndo:
        DrawArcArrowAA(g, a, color, lw, true);
        break;
    case kIconRedo:
        DrawArcArrowAA(g, a, color, lw, false);
        break;
    case kIconSend:
    {
        SolidBrush br(GC(color));
        PointF tri[3] =
        {
            PointF(ax, ay),
            PointF((REAL)a.right, (REAL)(a.top + a.bottom) / 2.0f),
            PointF(ax, (REAL)a.bottom),
        };
        g.FillPolygon(&br, tri, 3);
        break;
    }
    case kIconReselect:
    {
        // 环形刷新箭头（重新框选）。
        REAL r2  = (aw < ah ? aw : ah) / 2.0f * 0.92f;
        REAL ccx = ax + aw / 2.0f;
        REAL ccy = ay + ah / 2.0f;
        const REAL startA = -50.0f;
        const REAL sweep  = 285.0f;
        g.DrawArc(&pen, ccx - r2, ccy - r2, r2 * 2, r2 * 2, startA, sweep);
        const REAL d2r = 3.14159265f / 180.0f;
        REAL endA  = (startA + sweep) * d2r;
        REAL nearA = (startA + sweep - 8.0f) * d2r;
        POINT pe = { (LONG)(ccx + r2 * ::cos(endA)),  (LONG)(ccy + r2 * ::sin(endA)) };
        POINT pn = { (LONG)(ccx + r2 * ::cos(nearA)), (LONG)(ccy + r2 * ::sin(nearA)) };
        DrawArrowHeadAA(g, pen, pn, pe, MaxInt(3, (int)(r2 * 0.7f)));
        break;
    }
    case kIconCopy:
    {
        // 两个叠放的圆角矩形。
        REAL off = (REAL)side * 0.16f;
        GraphicsPath back;
        AddRoundRect(back, ax + off, ay, aw - off, ah - off, 2.0f);
        g.DrawPath(&pen, &back);
        GraphicsPath front;
        AddRoundRect(front, ax, ay + off, aw - off, ah - off, 2.0f);
        g.DrawPath(&pen, &front);
        break;
    }
    case kIconSave:
    {
        // 向下箭头 + 底部托盘（下载 / 保存）。
        REAL mx = ax + aw / 2.0f;
        REAL arrowBot = ay + ah * 0.62f;
        g.DrawLine(&pen, mx, ay, mx, arrowBot);
        g.DrawLine(&pen, mx, arrowBot, mx - aw * 0.22f, ay + ah * 0.40f);
        g.DrawLine(&pen, mx, arrowBot, mx + aw * 0.22f, ay + ah * 0.40f);
        REAL trayY  = (REAL)a.bottom;
        REAL trayUp = ah * 0.18f;
        g.DrawLine(&pen, ax, trayY - trayUp, ax, trayY);
        g.DrawLine(&pen, ax, trayY, (REAL)a.right, trayY);
        g.DrawLine(&pen, (REAL)a.right, trayY, (REAL)a.right, trayY - trayUp);
        break;
    }
    case kIconStage:
    {
        // 聊天气泡（圆角矩形 + 左下小尾巴）。
        REAL bodyH = ah * 0.72f;
        GraphicsPath bubble;
        AddRoundRect(bubble, ax, ay, aw, bodyH, 3.0f);
        g.DrawPath(&pen, &bubble);
        g.DrawLine(&pen, ax + aw * 0.22f, ay + bodyH, ax + aw * 0.10f, (REAL)a.bottom);
        g.DrawLine(&pen, ax + aw * 0.10f, (REAL)a.bottom, ax + aw * 0.40f, ay + bodyH);
        break;
    }
    case kIconCancel:
    {
        g.DrawLine(&pen, ax, ay, (REAL)a.right, (REAL)a.bottom);
        g.DrawLine(&pen, (REAL)a.right, ay, ax, (REAL)a.bottom);
        break;
    }
    case kIconCursor:
    {
        // 鼠标指针箭头（实心），按经典光标轮廓的归一化点缩放进 a。
        SolidBrush br(GC(color));
        const REAL cur[7][2] =
        {
            { 0.0f, 0.0f }, { 0.0f, 16.0f }, { 4.0f, 12.0f }, { 7.0f, 18.0f },
            { 9.0f, 17.0f }, { 6.0f, 11.0f }, { 11.0f, 11.0f },
        };
        REAL sc = ah / 18.0f;
        REAL ox = ax + (aw - 11.0f * sc) / 2.0f;
        REAL oy = ay;
        PointF pts[7];
        for (int i = 0; i < 7; ++i)
        {
            pts[i] = PointF(ox + cur[i][0] * sc, oy + cur[i][1] * sc);
        }
        g.FillPolygon(&br, pts, 7);
        break;
    }
    case kIconMosaic:
    {
        // 方框内 3×3 棋盘格（实心小方块），表示"打码 / 像素化"。
        SolidBrush br(GC(color));
        const int kCells = 3;
        REAL cw = aw / kCells;
        REAL ch = ah / kCells;
        for (int j = 0; j < kCells; ++j)
        {
            for (int i = 0; i < kCells; ++i)
            {
                if (((i + j) & 1) == 0)
                {
                    g.FillRectangle(&br, ax + i * cw, ay + j * ch, cw, ch);
                }
            }
        }
        break;
    }
    case kIconMosaicBrush:
    {
        // 左下→右上斜向三块方块刷痕，表示"涂抹式马赛克"。
        SolidBrush br(GC(color));
        REAL bs = (aw < ah ? aw : ah) / 3.0f;   // 方块边长
        REAL stepX = (aw - bs) / 2.0f;
        REAL stepY = (ah - bs) / 2.0f;
        for (int k = 0; k < 3; ++k)
        {
            REAL bx = ax + stepX * k;
            REAL by = ay + ah - bs - stepY * k;
            g.FillRectangle(&br, bx, by, bs, bs);
        }
        break;
    }
    case kIconText:
    {
        // 字母 "A"：顶点 + 两腿 + 横杠。
        REAL cx = ax + aw / 2.0f;
        g.DrawLine(&pen, cx, ay, ax, (REAL)a.bottom);
        g.DrawLine(&pen, cx, ay, (REAL)a.right, (REAL)a.bottom);
        REAL barY = ay + ah * 0.62f;
        g.DrawLine(&pen, ax + aw * 0.22f, barY, ax + aw * 0.78f, barY);
        break;
    }
    default:
        break;
    }
}

// ============================================================
// DarkBar
// ============================================================
void DarkBar::OnPaint(HDC hdc, const RECT& rcDirty)
{
    {
        Graphics g(hdc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        FillRoundRectAA(g, m_rcItem, kBarRadius, kBarBg);
    }
    // 子控件（按钮 / 色点）。
    balloonwjui::DuiControl::OnPaint(hdc, rcDirty);
}

// ============================================================
// IconButton
// ============================================================
IconButton::IconButton()
    : m_icon(kIconNone)
    , m_active(false)
    , m_primary(false)
    , m_tipSink(NULL)
{
}

void IconButton::SetActive(bool a)
{
    if (m_active != a)
    {
        m_active = a;
        Invalidate();
    }
}

void IconButton::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    RECT r = m_rcItem;

    COLORREF bg = CLR_INVALID;
    if (m_primary)
    {
        bg = m_bHover ? kPrimaryHover : kPrimaryBg;
    }
    else if (m_active)
    {
        bg = kActiveBg;
    }
    else if (m_bHover)
    {
        bg = kHoverBg;
    }

    COLORREF fg = m_primary ? RGB(0xFF, 0xFF, 0xFF)
                            : (m_active ? kActiveIcon : kIconColor);
    bool hasIcon = (m_icon != kIconNone);
    bool hasLabel = !m_label.IsEmpty();
    int cy = (r.top + r.bottom) / 2;

    // 底 + 图标用 GDI+（抗锯齿）；用花括号确保 Graphics 在画文字前析构 / flush。
    if (bg != CLR_INVALID || (hasIcon && !hasLabel))
    {
        Graphics g(hdc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        if (bg != CLR_INVALID)
        {
            FillRoundRectAA(g, InsetRect(r, 2), kBtnInnerRadius, bg);
        }
        if (hasIcon && !hasLabel)
        {
            int s = MinInt(r.right - r.left, r.bottom - r.top);
            int cx = (r.left + r.right) / 2;
            RECT ir = { cx - s / 2, cy - s / 2, cx + s / 2, cy + s / 2 };
            DrawToolIcon(hdc, ir, m_icon, fg);
        }
    }

    // 文字（GDI DrawText）。
    if (hasLabel)
    {
        HFONT font = (HFONT)::GetStockObject(DEFAULT_GUI_FONT);
        HFONT oldFont = (HFONT)::SelectObject(hdc, font);
        int oldMode = ::SetBkMode(hdc, TRANSPARENT);
        COLORREF oldText = ::SetTextColor(hdc, fg);

        if (hasIcon)
        {
            const int iconSize = 18;
            const int gap = 4;
            RECT m = { 0, 0, 0, 0 };
            ::DrawText(hdc, m_label, -1, &m, DT_CALCRECT | DT_SINGLELINE);
            int labelW = m.right - m.left;
            int total = iconSize + gap + labelW;
            int x0 = r.left + ((r.right - r.left) - total) / 2;
            RECT ir = { x0, cy - iconSize / 2, x0 + iconSize, cy + iconSize / 2 };
            DrawToolIcon(hdc, ir, m_icon, fg);
            RECT tr = { x0 + iconSize + gap, r.top, r.right, r.bottom };
            ::DrawText(hdc, m_label, -1, &tr, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
        }
        else
        {
            RECT tr = r;
            ::DrawText(hdc, m_label, -1, &tr, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        }
        ::SetTextColor(hdc, oldText);
        ::SetBkMode(hdc, oldMode);
        ::SelectObject(hdc, oldFont);
    }
}

bool IconButton::OnLButtonUp(POINT /*pt*/, UINT /*mkFlags*/)
{
    NotifyParent(balloonwjui::DUIN_CLICK);
    return true;
}

bool IconButton::OnMouseEnter()
{
    balloonwjui::DuiControl::OnMouseEnter();
    // 悬停时请宿主画布在本按钮正下方显示提示。
    if (m_tipSink != NULL && !m_tip.IsEmpty())
    {
        m_tipSink->ShowTip(m_rcItem, m_tip);
    }
    Invalidate();
    return false;
}

bool IconButton::OnMouseLeave()
{
    balloonwjui::DuiControl::OnMouseLeave();
    if (m_tipSink != NULL)
    {
        m_tipSink->HideTip();
    }
    Invalidate();
    return false;
}

bool IconButton::OnSetCursor(POINT /*pt*/)
{
    ::SetCursor(::LoadCursor(NULL, IDC_HAND));
    return true;
}

// ============================================================
// ColorDot
// ============================================================
ColorDot::ColorDot()
    : m_color(RGB(0, 0, 0))
    , m_active(false)
{
}

void ColorDot::SetActive(bool a)
{
    if (m_active != a)
    {
        m_active = a;
        Invalidate();
    }
}

void ColorDot::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    RECT r = m_rcItem;
    REAL cx = (REAL)(r.left + r.right) / 2.0f;
    REAL cy = (REAL)(r.top + r.bottom) / 2.0f;
    REAL rad = (REAL)(MinInt(r.right - r.left, r.bottom - r.top)) / 2.0f - 1.0f;
    if (rad < 2.0f)
    {
        return;
    }

    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);

    // 选中：白色高亮环（略大），随后色点缩小内嵌。
    if (m_active)
    {
        Pen ring(Color(255, 255, 255, 255), 2.0f);
        g.DrawEllipse(&ring, cx - rad, cy - rad, rad * 2, rad * 2);
        rad -= 3.0f;
    }
    SolidBrush fill(GC(m_color));
    g.FillEllipse(&fill, cx - rad, cy - rad, rad * 2, rad * 2);
    Pen border(Color(255, 0x20, 0x20, 0x20), 1.0f);
    g.DrawEllipse(&border, cx - rad, cy - rad, rad * 2, rad * 2);
}

bool ColorDot::OnLButtonUp(POINT /*pt*/, UINT /*mkFlags*/)
{
    NotifyParent(balloonwjui::DUIN_CLICK);
    return true;
}

bool ColorDot::OnSetCursor(POINT /*pt*/)
{
    ::SetCursor(::LoadCursor(NULL, IDC_HAND));
    return true;
}

// ============================================================
// WidthDot
// ============================================================
WidthDot::WidthDot()
    : m_width(4)
    , m_active(false)
    , m_tipSink(NULL)
{
}

void WidthDot::SetActive(bool a)
{
    if (m_active != a)
    {
        m_active = a;
        Invalidate();
    }
}

void WidthDot::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    RECT r = m_rcItem;
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);

    if (m_active)
    {
        FillRoundRectAA(g, InsetRect(r, 2), kBtnInnerRadius, kActiveBg);
    }
    REAL cy = (REAL)(r.top + r.bottom) / 2.0f;
    Pen pen(GC(kWidthSample), (REAL)m_width);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    g.DrawLine(&pen, (REAL)(r.left + 6), cy, (REAL)(r.right - 6), cy);
}

bool WidthDot::OnLButtonUp(POINT /*pt*/, UINT /*mkFlags*/)
{
    NotifyParent(balloonwjui::DUIN_CLICK);
    return true;
}

bool WidthDot::OnMouseEnter()
{
    balloonwjui::DuiControl::OnMouseEnter();
    // 悬停时请宿主画布在本样条正下方显示提示（细线 / 中等线 / 粗线）。
    if (m_tipSink != NULL && !m_tip.IsEmpty())
    {
        m_tipSink->ShowTip(m_rcItem, m_tip);
    }
    Invalidate();
    return false;
}

bool WidthDot::OnMouseLeave()
{
    balloonwjui::DuiControl::OnMouseLeave();
    if (m_tipSink != NULL)
    {
        m_tipSink->HideTip();
    }
    Invalidate();
    return false;
}

bool WidthDot::OnSetCursor(POINT /*pt*/)
{
    ::SetCursor(::LoadCursor(NULL, IDC_HAND));
    return true;
}

} // namespace balloonscreenshot
