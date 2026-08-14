/**
 *  ChatInputScrollBar.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-06-03
 */
#include "stdafx.h"
#include "ChatInputScrollBar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEditHost.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiRichEditHost.h"   // 富文本输入框（RICHEDIT50W）
#include <commctrl.h>   // SetWindowSubclass / DefSubclassProc 等
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM
#include <gdiplus.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")

namespace balloonchatwnd {

namespace {

// 滚动条几何 / 配色 —— 与接收区 ChatMessageList 保持一致（细圆角半透明灰）。
const int kBarWidth     = 5;    // 滑块宽
const int kBarMargin    = 2;    // 滑块与边缘留白
const int kBarMinThumb  = 36;   // 滑块最小高（太短不好抓）
const int kInputGutterW = kBarWidth + 2 * kBarMargin;   // 预留 gutter 宽（9px）
// 滑块色（半透明灰），与 ChatMessageList 的 kThumbColor 同值。
const Gdiplus::Color kThumbColor(120, 0x5A, 0x5A, 0x5A);

// 本模块在 EDIT 子窗口上挂的「重绘」子类过程的唯一标识（与回车过滤器 id=1 区分）。
const UINT_PTR kScrollRepaintSubclassId = 2;

// 取 EDIT 单行像素高（字体 tmHeight + tmExternalLeading）；取不到时回退 16。
int GetEditLineHeight(HWND hEdit)
{
    HFONT hFont = (HFONT)::SendMessage(hEdit, WM_GETFONT, 0, 0);
    HDC   hdc   = ::GetDC(hEdit);
    if (hdc == NULL)
        return 16;
    HFONT hOld = (hFont != NULL) ? (HFONT)::SelectObject(hdc, hFont) : NULL;
    TEXTMETRIC tm = { 0 };
    ::GetTextMetrics(hdc, &tm);
    if (hOld != NULL)
        ::SelectObject(hdc, hOld);
    ::ReleaseDC(hEdit, hdc);
    int lh = tm.tmHeight + tm.tmExternalLeading;
    return lh > 0 ? lh : 16;
}

// 滑块几何 + 滚动量（painter 与拖拽回调共用，保证画与拖一致）。
struct ThumbMetrics
{
    bool valid;        // 内容是否溢出（false = 不画滚动条、不响应拖拽）
    int  thumbX;       // 滑块左 x
    int  thumbY;       // 滑块顶 y
    int  thumbH;       // 滑块高
    int  trackTop;     // 轨道顶 y（= gutter 顶）
    int  trackH;       // 轨道高（= gutter 高）
    int  first;        // 当前首个可见行
    int  scrollable;   // 可滚动行数（总行 - 可见行）

    ThumbMetrics()
        : valid(false), thumbX(0), thumbY(0), thumbH(0)
        , trackTop(0), trackH(0), first(0), scrollable(0)
    {
    }
};

// 据 EDIT 当前滚动状态 + gutter 矩形，算出滑块几何。内容不溢出时返回 valid=false。
ThumbMetrics ComputeThumbMetrics(HWND hEdit, const RECT& rcIcon)
{
    ThumbMetrics m;
    if (hEdit == NULL || !::IsWindow(hEdit))
        return m;

    int total = (int)::SendMessage(hEdit, EM_GETLINECOUNT, 0, 0);
    int first = (int)::SendMessage(hEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
    if (total < 1)
        total = 1;

    RECT rcFmt = { 0 };
    ::SendMessage(hEdit, EM_GETRECT, 0, (LPARAM)&rcFmt);
    int lineH   = GetEditLineHeight(hEdit);
    int viewH   = rcFmt.bottom - rcFmt.top;
    int visible = (lineH > 0) ? (viewH / lineH) : 1;
    if (visible < 1)
        visible = 1;

    // 不溢出：没有滚动条。
    if (total <= visible)
        return m;

    int trackTop = rcIcon.top;
    int trackH   = rcIcon.bottom - rcIcon.top;
    if (trackH <= 0)
        return m;

    int thumbH = (int)((LONGLONG)trackH * visible / total);
    if (thumbH < kBarMinThumb)
        thumbH = kBarMinThumb;
    if (thumbH > trackH)
        thumbH = trackH;

    int scrollable = total - visible;
    if (first < 0)
        first = 0;
    if (first > scrollable)
        first = scrollable;

    int thumbY = trackTop;
    if (scrollable > 0)
        thumbY += (int)((LONGLONG)(trackH - thumbH) * first / scrollable);

    // 滑块横向：gutter 矩形右边界已被 ComputeIconRect 内缩 1px 边框，这里 +1 补回，
    // 使发送框滑块与接收区滑块落在同一垂直线（接收区用控件原始右边界，无此内缩）。
    int thumbX = rcIcon.right + 1 - kBarWidth - kBarMargin;

    m.valid      = true;
    m.thumbX     = thumbX;
    m.thumbY     = thumbY;
    m.thumbH     = thumbH;
    m.trackTop   = trackTop;
    m.trackH     = trackH;
    m.first      = first;
    m.scrollable = scrollable;
    return m;
}

// 让 gutter 局部重绘：把 EDIT 右侧那条 gutter 在 host 客户区里标脏，触发滑块重画。
void InvalidateGutter(HWND hEdit)
{
    HWND host = ::GetParent(hEdit);
    if (host == NULL)
        return;
    RECT wr = { 0 };
    ::GetWindowRect(hEdit, &wr);
    POINT tl = { wr.left, wr.top };
    POINT br = { wr.right, wr.bottom };
    ::ScreenToClient(host, &tl);
    ::ScreenToClient(host, &br);
    // EDIT 已被 gutter 右缩，其右边界 br.x 在 gutter 左侧。滑块实际画在 EDIT 控件
    // 右边界附近（gutter + 边距 + 边框，约 14px 开外），所以从 br.x 一直标脏到更
    // 右一截，确保滑块整条（含右沿）都重画，避免拖动留下残影。
    RECT inv = { br.x, tl.y, br.x + kInputGutterW + 14, br.y };
    ::InvalidateRect(host, &inv, FALSE);
}

// 在 gutter 内自绘细圆角灰滑块；内容不溢出不画。
void PaintInputThumb(HWND hEdit, HDC hdc, const RECT& rcIcon)
{
    ThumbMetrics m = ComputeThumbMetrics(hEdit, rcIcon);
    if (!m.valid)
        return;

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::GraphicsPath path;
    Gdiplus::REAL d = (Gdiplus::REAL)kBarWidth;
    path.AddArc((Gdiplus::REAL)m.thumbX, (Gdiplus::REAL)m.thumbY, d, d, 180.0f, 180.0f);
    path.AddArc((Gdiplus::REAL)m.thumbX, (Gdiplus::REAL)(m.thumbY + m.thumbH) - d, d, d, 0.0f, 180.0f);
    path.CloseFigure();
    Gdiplus::SolidBrush brush(kThumbColor);
    g.FillPath(&brush, &path);
}

// 把滑块顶部移到 targetThumbTop（host 客户区 y），换算成首个可见行并 EM_LINESCROLL。
void ScrollEditToThumbTop(HWND hEdit, const ThumbMetrics& m, int targetThumbTop)
{
    int trackRange = m.trackH - m.thumbH;   // 滑块可移动的像素范围
    if (trackRange <= 0 || m.scrollable <= 0)
        return;

    int rel = targetThumbTop - m.trackTop;
    if (rel < 0)
        rel = 0;
    if (rel > trackRange)
        rel = trackRange;

    int newFirst = (int)((LONGLONG)rel * m.scrollable / trackRange);
    int delta = newFirst - m.first;
    if (delta != 0)
    {
        ::SendMessage(hEdit, EM_LINESCROLL, 0, delta);
        InvalidateGutter(hEdit);
    }
}

// painter 仿函数（避免 lambda）：持有 EDIT 句柄，转调 PaintInputThumb。
struct InputThumbPainter
{
    HWND m_hEdit;
    void operator()(HDC hdc, const RECT& rcIcon) const
    {
        PaintInputThumb(m_hEdit, hdc, rcIcon);
    }
};

// 拖拽仿函数：DuiEditHost 把 gutter 的鼠标 按下/移动/抬起 回调进来；拖滑块按比例滚 EDIT。
struct InputThumbDragger
{
    HWND m_hEdit;
    bool m_dragging;       // 是否正在拖动滑块
    int  m_dragOffsetY;    // 抓点相对滑块顶部的偏移

    InputThumbDragger()
        : m_hEdit(NULL), m_dragging(false), m_dragOffsetY(0)
    {
    }

    void operator()(UINT msg, POINT pt, const RECT& gutterRect)
    {
        ThumbMetrics m = ComputeThumbMetrics(m_hEdit, gutterRect);
        if (!m.valid)
        {
            // 没有滚动条（不溢出）：忽略，但仍在抬起时清拖拽态。
            if (msg == WM_LBUTTONUP)
                m_dragging = false;
            return;
        }

        if (msg == WM_LBUTTONDOWN)
        {
            if (pt.y >= m.thumbY && pt.y < m.thumbY + m.thumbH)
            {
                // 抓在滑块上：记住抓点偏移，拖动时保持不跳。
                m_dragOffsetY = pt.y - m.thumbY;
            }
            else
            {
                // 点在轨道空白处：先让滑块中心跳到点击处，再按住继续拖。
                m_dragOffsetY = m.thumbH / 2;
                ScrollEditToThumbTop(m_hEdit, m, pt.y - m_dragOffsetY);
            }
            m_dragging = true;
        }
        else if (msg == WM_MOUSEMOVE)
        {
            if (m_dragging)
                ScrollEditToThumbTop(m_hEdit, m, pt.y - m_dragOffsetY);
        }
        else if (msg == WM_LBUTTONUP)
        {
            m_dragging = false;
        }
    }
};

// 重绘子类过程：内容/滚动变化后让 gutter 重绘，使滑块跟手。
LRESULT CALLBACK ScrollRepaintProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                   UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/)
{
    switch (msg)
    {
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_CHAR:
    case WM_PASTE:
    case WM_CUT:
    case WM_CLEAR:
    case WM_UNDO:
    case WM_MOUSEWHEEL:
    case WM_VSCROLL:
    case WM_LBUTTONUP:
    case WM_SIZE:
    case WM_SETTEXT:
    case EM_REPLACESEL:
    case EM_LINESCROLL:
        {
            // 先让 EDIT 处理（更新滚动状态），再标脏 gutter 触发滑块重画。
            LRESULT r = ::DefSubclassProc(hwnd, msg, wParam, lParam);
            InvalidateGutter(hwnd);
            return r;
        }

    case WM_NCDESTROY:
        ::RemoveWindowSubclass(hwnd, ScrollRepaintProc, kScrollRepaintSubclassId);
        break;

    default:
        break;
    }
    return ::DefSubclassProc(hwnd, msg, wParam, lParam);
}

// =====================================================================
// 富文本输入框（DuiRichEditHost / RICHEDIT50W）版细悬浮滚动条
// =====================================================================
//
// 输入框已从 Win32 EDIT 迁成 RICHEDIT50W（DuiRichEditHost）。RichEdit 子窗口盖在
// DUI 合成层之上，无法靠 DUI OnPaint 在其内容上叠画滑块，故给 RichEdit 子窗口挂
// 子类过程：WM_PAINT 默认绘制后在右沿叠画半透明细滑块、右侧窄条命中拖拽、滚动/
// 编辑后局部标脏。滚动度量与定位复用 DuiRichEditHost 的像素级接口
// （GetVScrollMetrics / SetVScrollPos），与接收区 ChatMessageList 的滑块口径一致。

// 本模块在 RichEdit 子窗口上挂的子类过程唯一标识。与 DuiRichEditHost 自己的滚动
// 子类（kScrollSubclassId）、本文件 EDIT 版的两个 id（1/2）都不重叠。
const UINT_PTR kRichScrollSubclassId = 3;

// 右侧可抓宽度：滑块本体约 kBarWidth + 2*kBarMargin（9px），再放宽 4px 便于抓取。
const int kRichHitStripW = kBarWidth + 2 * kBarMargin + 4;

// RichEdit 滑块几何 + 滚动量（painter 与拖拽共用，保证画与拖一致）。
struct RichThumb
{
    bool valid;        // 内容是否溢出（false = 不画、不响应拖拽）
    int  thumbY;       // 滑块顶 y（RichEdit 客户区坐标）
    int  thumbH;       // 滑块高
    int  trackTop;     // 轨道顶 y（= 客户区顶）
    int  trackH;       // 轨道高（= 客户区高）
    int  maxScroll;    // 可滚动像素范围（contentH - viewH）
    int  scrollPos;    // 当前已向下滚动的像素偏移

    RichThumb()
        : valid(false), thumbY(0), thumbH(0)
        , trackTop(0), trackH(0), maxScroll(0), scrollPos(0)
    {
    }
};

// 据 RichEdit 当前滚动度量算出滑块几何。内容不溢出时返回 valid=false。
RichThumb ComputeRichThumb(balloonwjui::DuiRichEditHost* re, HWND hRich)
{
    RichThumb t;
    if (re == NULL || hRich == NULL || !::IsWindow(hRich))
        return t;

    int contentH = 0;
    int scrollPos = 0;
    int viewH = 0;
    re->GetVScrollMetrics(contentH, scrollPos, viewH);
    if (viewH <= 0 || contentH <= viewH)
        return t;   // 放得下 → 无滚动条

    RECT rc = { 0 };
    ::GetClientRect(hRich, &rc);
    int trackTop = rc.top;
    int trackH   = rc.bottom - rc.top;
    if (trackH <= 0)
        return t;

    int thumbH = (int)((LONGLONG)trackH * viewH / contentH);
    if (thumbH < kBarMinThumb)
        thumbH = kBarMinThumb;
    if (thumbH > trackH)
        thumbH = trackH;

    int maxScroll = contentH - viewH;
    int thumbY = trackTop;
    if (maxScroll > 0)
        thumbY += (int)((LONGLONG)(trackH - thumbH) * scrollPos / maxScroll);

    t.valid     = true;
    t.thumbY    = thumbY;
    t.thumbH    = thumbH;
    t.trackTop  = trackTop;
    t.trackH    = trackH;
    t.maxScroll = maxScroll;
    t.scrollPos = scrollPos;
    return t;
}

// 在 RichEdit 客户区右沿叠画细圆角灰滑块；内容不溢出不画。
void PaintRichThumb(balloonwjui::DuiRichEditHost* re, HWND hRich, HDC hdc)
{
    RichThumb t = ComputeRichThumb(re, hRich);
    if (!t.valid)
        return;

    RECT rc = { 0 };
    ::GetClientRect(hRich, &rc);
    int thumbX = rc.right - kBarWidth - kBarMargin;   // 悬浮在右沿，与接收区同口径

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::GraphicsPath path;
    Gdiplus::REAL d = (Gdiplus::REAL)kBarWidth;
    path.AddArc((Gdiplus::REAL)thumbX, (Gdiplus::REAL)t.thumbY, d, d, 180.0f, 180.0f);
    path.AddArc((Gdiplus::REAL)thumbX, (Gdiplus::REAL)(t.thumbY + t.thumbH) - d, d, d, 0.0f, 180.0f);
    path.CloseFigure();
    Gdiplus::SolidBrush brush(kThumbColor);
    g.FillPath(&brush, &path);
}

// 把 RichEdit 右侧那条 gutter 标脏，触发滑块重画（拖动/滚动后跟手）。
void InvalidateRichStrip(HWND hRich)
{
    RECT rc = { 0 };
    ::GetClientRect(hRich, &rc);
    RECT strip = { rc.right - kRichHitStripW, rc.top, rc.right, rc.bottom };
    ::InvalidateRect(hRich, &strip, FALSE);
}

// 把滑块顶移到 targetThumbTop（客户区 y），换算成像素偏移驱动 RichEdit 滚动。
void ScrollRichToThumbTop(balloonwjui::DuiRichEditHost* re, const RichThumb& t, int targetThumbTop)
{
    int track = t.trackH - t.thumbH;   // 滑块可移动的像素范围
    if (track <= 0 || t.maxScroll <= 0)
        return;

    int rel = targetThumbTop - t.trackTop;
    if (rel < 0)
        rel = 0;
    if (rel > track)
        rel = track;

    int newPos = (int)((LONGLONG)rel * t.maxScroll / track);
    re->SetVScrollPos(newPos);
}

// 子类过程随附状态（拖拽态 + 反查 DuiRichEditHost 以取滚动度量）。挂载时 new、
// 子窗口 WM_NCDESTROY 时 delete。
struct RichScrollState
{
    balloonwjui::DuiRichEditHost* re;   // 反查滚动度量/定位；不持有所有权
    HWND hRich;                          // 被子类化的 RichEdit 子窗口
    bool dragging;                       // 是否正在拖滑块
    int  dragOffsetY;                    // 抓点相对滑块顶的偏移

    RichScrollState()
        : re(NULL), hRich(NULL), dragging(false), dragOffsetY(0)
    {
    }
};

// RichEdit 子类过程：叠画滑块 + 右沿窄条拖拽 + 滚动/编辑后局部重绘。
LRESULT CALLBACK RichScrollProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData)
{
    RichScrollState* st = (RichScrollState*)dwRefData;
    if (st == NULL)
        return ::DefSubclassProc(hwnd, msg, wParam, lParam);

    switch (msg)
    {
    case WM_PAINT:
        {
            // 先让 RichEdit 画完正文（并校验更新区），再在其上叠画细滑块。
            LRESULT r = ::DefSubclassProc(hwnd, msg, wParam, lParam);
            HDC hdc = ::GetDC(hwnd);
            if (hdc != NULL)
            {
                PaintRichThumb(st->re, hwnd, hdc);
                ::ReleaseDC(hwnd, hdc);
            }
            return r;
        }

    case WM_LBUTTONDOWN:
        {
            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
            RECT rc = { 0 };
            ::GetClientRect(hwnd, &rc);
            RichThumb t = ComputeRichThumb(st->re, hwnd);
            // 仅当内容溢出（有滑块）且点落在右侧窄条内，才接管为拖拽；否则照常
            // 交 RichEdit 处理（放置光标 / 选区）。
            if (t.valid && pt.x >= rc.right - kRichHitStripW)
            {
                if (pt.y >= t.thumbY && pt.y < t.thumbY + t.thumbH)
                {
                    // 抓在滑块上：记住抓点偏移，拖动时不跳。
                    st->dragOffsetY = pt.y - t.thumbY;
                }
                else
                {
                    // 点在轨道空白：滑块中心跳到点击处，再继续拖。
                    st->dragOffsetY = t.thumbH / 2;
                    ScrollRichToThumbTop(st->re, t, pt.y - st->dragOffsetY);
                    InvalidateRichStrip(hwnd);
                }
                st->dragging = true;
                ::SetCapture(hwnd);
                return 0;   // 消费，不让 RichEdit 起选区
            }
            break;
        }

    case WM_MOUSEMOVE:
        if (st->dragging)
        {
            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
            RichThumb t = ComputeRichThumb(st->re, hwnd);
            if (t.valid)
            {
                ScrollRichToThumbTop(st->re, t, pt.y - st->dragOffsetY);
                InvalidateRichStrip(hwnd);
            }
            return 0;
        }
        break;

    case WM_LBUTTONUP:
        if (st->dragging)
        {
            st->dragging = false;
            ::ReleaseCapture();
            return 0;
        }
        break;

    // 这些消息会改变内容量 / 滚动位置：先让 RichEdit 处理，再标脏滑块条跟手。
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_CHAR:
    case WM_PASTE:
    case WM_CUT:
    case WM_CLEAR:
    case WM_UNDO:
    case WM_MOUSEWHEEL:
    case WM_VSCROLL:
    case WM_SIZE:
    case WM_SETTEXT:
    case EM_REPLACESEL:
    case EM_LINESCROLL:
    case EM_SETSEL:
        {
            LRESULT r = ::DefSubclassProc(hwnd, msg, wParam, lParam);
            InvalidateRichStrip(hwnd);
            return r;
        }

    case WM_NCDESTROY:
        ::RemoveWindowSubclass(hwnd, RichScrollProc, kRichScrollSubclassId);
        delete st;
        return ::DefSubclassProc(hwnd, msg, wParam, lParam);

    default:
        break;
    }
    return ::DefSubclassProc(hwnd, msg, wParam, lParam);
}

} // namespace

void AttachChatInputScrollBar(balloonwjui::DuiEditHost* edit)
{
    if (edit == NULL)
        return;
    HWND hEdit = edit->GetHostedHwnd();
    if (hEdit == NULL || !::IsWindow(hEdit))
        return;

    // 1) 去掉原生竖直滚动条（保留 ES_AUTOVSCROLL，仍可滚轮/光标滚动，只是不画原生灰条）。
    LONG style = ::GetWindowLong(hEdit, GWL_STYLE);
    if ((style & WS_VSCROLL) != 0)
    {
        ::SetWindowLong(hEdit, GWL_STYLE, style & ~WS_VSCROLL);
        ::SetWindowPos(hEdit, NULL, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    // 2) 预留右侧 gutter + 设自绘滑块 painter（仿函数持有 EDIT 句柄）。
    InputThumbPainter painter;
    painter.m_hEdit = hEdit;
    edit->SetIcon(balloonwjui::DuiEditHost::RightIcon, kInputGutterW, painter);

    // 3) 设拖拽回调：gutter 区接管鼠标，拖滑块按比例滚动 EDIT。
    InputThumbDragger dragger;
    dragger.m_hEdit = hEdit;
    edit->SetIconDragHandler(balloonwjui::DuiEditHost::RightIcon, dragger);

    // 4) 挂重绘子类过程（id=2，与回车过滤器 id=1 区分；EDIT 销毁时自摘）。
    ::SetWindowSubclass(hEdit, ScrollRepaintProc, kScrollRepaintSubclassId, 0);
}

void AttachChatInputScrollBar(balloonwjui::DuiRichEditHost* re)
{
    if (re == NULL)
        return;
    HWND hRich = re->GetHostedHwnd();
    if (hRich == NULL || !::IsWindow(hRich))
        return;

    // 原生竖直滚动条由调用方在 EnsureCreated 之前 SetShowVScroll(false) 关掉，这里
    // 只负责自绘滑块：挂子类过程（随附状态在 WM_NCDESTROY 自摘并 delete）。id=3 与
    // DuiRichEditHost 自己的滚动子类、本文件 EDIT 版的两个 id 都不重叠，链式共存。
    RichScrollState* st = new RichScrollState();
    st->re    = re;
    st->hRich = hRich;
    if (!::SetWindowSubclass(hRich, RichScrollProc, kRichScrollSubclassId,
                             reinterpret_cast<DWORD_PTR>(st)))
    {
        delete st;
        return;
    }
    // 立即标脏右沿，使初始（若已有溢出内容）滑块即时出现。
    InvalidateRichStrip(hRich);
}

} // namespace balloonchatwnd
