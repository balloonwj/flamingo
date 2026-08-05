/**
 *  「消息记录」列表行实现，HistoryRow.cpp
 *  balloonwj@qq.com   2026-08-04
 */
#include "stdafx.h"
#include "HistoryRow.h"

#include <gdiplus.h>

#include "../../balloonui/balloonui/DuiNotify.h"
#include "../BalloonChatWnd/ChatIcons.h"       // kChatIconPin / DrawChatIcon
#include "../BalloonChatWnd/ChatFileVisual.h"  // FileTypeColor（与文件气泡共用配色）
#include "../BalloonChatWnd/ChatImageCache.h"  // 缩略图的后台解码与缓存

namespace balloonhistory {

//往 path 里加一个圆角矩形。GDI+ 没有现成的圆角矩形，四个角各一段 90° 弧拼起来。
static void AddRoundRectPath(Gdiplus::GraphicsPath& path, const RECT& rc, int radius)
{
    const Gdiplus::REAL x = (Gdiplus::REAL)rc.left;
    const Gdiplus::REAL y = (Gdiplus::REAL)rc.top;
    const Gdiplus::REAL w = (Gdiplus::REAL)(rc.right - rc.left);
    const Gdiplus::REAL h = (Gdiplus::REAL)(rc.bottom - rc.top);
    //半径夹到短边的一半，设过大也不会画烂。
    Gdiplus::REAL r = (Gdiplus::REAL)radius;
    const Gdiplus::REAL half = ((w < h) ? w : h) / 2.0f;
    if (r > half)
    {
        r = half;
    }
    if (r <= 0.0f)
    {
        path.AddRectangle(Gdiplus::RectF(x, y, w, h));
        return;
    }

    const Gdiplus::REAL d = r * 2.0f;
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

//图钉默认色：暖琥珀，与聊天气泡上那枚同色，两处一眼能对上是同一件事。
static const COLORREF kDefaultPinColor = RGB(0xE6, 0x8A, 0x2E);

//图钉描边宽，以 ChatIcons 的 viewBox 24 为基准。
static const Gdiplus::REAL kPinStrokeWidth = 1.6f;

HistoryPinMark::HistoryPinMark()
    : m_color(kDefaultPinColor)
{
}

void HistoryPinMark::SetIconColor(COLORREF color)
{
    m_color = color;
    Invalidate();
}

void HistoryPinMark::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const RECT rc = GetRect();
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    //按短边取正方形并居中，免得控件被拉扁时图钉跟着变形。
    const int side = (w < h) ? w : h;
    const Gdiplus::REAL x = (Gdiplus::REAL)(rc.left + (w - side) / 2);
    const Gdiplus::REAL y = (Gdiplus::REAL)(rc.top + (h - side) / 2);

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    balloonchatwnd::DrawChatIcon(
        g, balloonchatwnd::kChatIconPin, x, y, (Gdiplus::REAL)side,
        Gdiplus::Color(255, GetRValue(m_color), GetGValue(m_color), GetBValue(m_color)),
        kPinStrokeWidth);
}

//默认悬停底色：极淡的暖灰，与列表白底拉开一点点层次即可。太重会让鼠标划过时
//整屏跟着闪，比不加还糟。
static const COLORREF kDefaultHoverBg = RGB(0xF6, 0xF4, 0xF0);

// ---- HistoryRowThumb ------------------------------------------------------

//缩略图 / 色块的圆角半径。
static const int kThumbCorner = 6;
//未命中缓存时的占位块底色（极淡的灰）。
static const COLORREF kThumbPlaceholderBg = RGB(0xEE, 0xEC, 0xE8);
//类型色块里那几个字的字号（磅）。扩展名一般 2~4 个字符，小一号才塞得下。
static const int kThumbExtPointSize = 8;

HistoryRowThumb::HistoryRowThumb()
    : m_kind(kNone)
{
}

void HistoryRowThumb::SetImage(const std::wstring& path)
{
    m_kind = path.empty() ? kNone : kImage;
    m_path = path;
    Invalidate();
}

void HistoryRowThumb::SetFile(const CString& ext)
{
    m_kind = kFile;
    m_ext  = ext;
    Invalidate();
}

void HistoryRowThumb::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    if (m_kind == kNone)
    {
        return;
    }

    const RECT rc = GetRect();
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    if (m_kind == kFile)
    {
        //类型色块：圆角矩形铺底 + 居中写扩展名。配色与聊天窗的文件气泡共用
        //（见 ChatFileVisual.h），同一个 .xlsx 在两处必须是同一个绿。
        const COLORREF bg = balloonchatwnd::FileTypeColor(m_ext);
        Gdiplus::SolidBrush brush(
            Gdiplus::Color(255, GetRValue(bg), GetGValue(bg), GetBValue(bg)));
        Gdiplus::GraphicsPath path;
        AddRoundRectPath(path, rc, kThumbCorner);
        g.FillPath(&brush, &path);

        if (!m_ext.IsEmpty())
        {
            //文字用 GDI 画：与列表其余文字同一套字体渲染，不必再引一套 GDI+ 字体。
            const int oldBk = ::SetBkMode(hdc, TRANSPARENT);
            const COLORREF oldClr = ::SetTextColor(hdc, RGB(0xFF, 0xFF, 0xFF));
            HFONT font = ::CreateFont(-::MulDiv(kThumbExtPointSize, 96, 72), 0, 0, 0,
                                      FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH, _T("微软雅黑"));
            HFONT oldFont = (HFONT)::SelectObject(hdc, font);
            RECT textRc = rc;
            ::DrawText(hdc, m_ext, -1, &textRc,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
            ::SelectObject(hdc, oldFont);
            ::DeleteObject(font);
            ::SetTextColor(hdc, oldClr);
            ::SetBkMode(hdc, oldBk);
        }
        return;
    }

    //图片：命中缓存就画缩放好的图，未命中画占位块并请求后台解码。
    //缓存键含展示尺寸，故这里传的宽高要与实际绘制尺寸一致。
    Gdiplus::Bitmap* bmp = balloonchatwnd::ChatImageCache::Inst().Get(m_path, w, h);
    if (bmp != NULL)
    {
        Gdiplus::GraphicsPath clipPath;
        AddRoundRectPath(clipPath, rc, kThumbCorner);
        g.SetClip(&clipPath);
        g.DrawImage(bmp, rc.left, rc.top, w, h);
        g.ResetClip();
        return;
    }

    Gdiplus::SolidBrush ph(Gdiplus::Color(255,
        GetRValue(kThumbPlaceholderBg), GetGValue(kThumbPlaceholderBg),
        GetBValue(kThumbPlaceholderBg)));
    Gdiplus::GraphicsPath phPath;
    AddRoundRectPath(phPath, rc, kThumbCorner);
    g.FillPath(&ph, &phPath);

    //请求后台解码。已缓存 / 进行中会被内部去重，重复调用无害。文件不存在时
    //解码失败、缓存里不会有东西，本块就一直是占位 —— 这正是"缓存没有原图就不下载"
    //那条约定下的表现。
    balloonchatwnd::ChatImageCache::Inst().Request(m_path, w, h);
}

// ---- HistoryRow -----------------------------------------------------------

HistoryRow::HistoryRow()
    : m_hoverBg(kDefaultHoverBg)
    , m_hovered(false)
{
}

void HistoryRow::AddHoverOnlyControl(balloonwjui::DuiControl* ctrl)
{
    if (ctrl == NULL)
    {
        return;
    }
    m_hoverOnly.push_back(ctrl);
    //登记即隐藏：鼠标还没进来，这枚按钮就不该出现。
    ctrl->SetVisible(false);
}

void HistoryRow::SetHoverBgColor(COLORREF color)
{
    m_hoverBg = color;
    Invalidate();
}

void HistoryRow::SetHoverControlsVisible(bool visible)
{
    for (size_t i = 0; i < m_hoverOnly.size(); ++i)
    {
        if (m_hoverOnly[i] == NULL)
        {
            continue;
        }
        m_hoverOnly[i]->SetVisible(visible);

        //改完可见性还要把它<u>所在的那个占位容器</u>重新布局一次，否则按钮由隐藏转为
        //可见后拿不到矩形、画不出来（只是"逻辑上可见"）。
        //
        //为什么不能指望本行 ForceLayout 一路级联下去：DuiLayout 给子控件定位走的是
        //DuiControl::SetRect，而它在矩形<u>没变</u>时会提前 return、连 Layout 都不调。
        //占位容器一直可见、宽度也固定，它的矩形恰恰是不变的 —— 于是级联就在这里断了。
        //ForceLayout 不做这个相等短路，正好用来打通这一层。
        balloonwjui::DuiControl* slot = m_hoverOnly[i]->GetParent();
        if (slot != NULL)
        {
            slot->ForceLayout(slot->GetRect());
        }
    }
}

void HistoryRow::SetHovered(bool hovered)
{
    if (m_hovered == hovered)
    {
        return;
    }
    m_hovered = hovered;
    SetHoverControlsVisible(hovered);
    //切完可见性必须重排一次：DuiLayout 布局时跳过不可见子控件，不重排的话按钮
    //由隐藏转为可见后拿不到矩形，画不出来（只是"逻辑上可见"）。
    ForceLayout(GetRect());
    Invalidate();
}

void HistoryRow::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (m_hovered)
    {
        const RECT rc = GetRect();
        HBRUSH brush = ::CreateSolidBrush(m_hoverBg);
        ::FillRect(hdc, &rc, brush);
        ::DeleteObject(brush);
    }
    //底色铺完再交基类画子控件；反过来的话底色会把刚画好的文字盖掉。
    balloonwjui::DuiHBox::OnPaint(hdc, rcDirty);
}

bool HistoryRow::OnLButtonDblClk(POINT /*pt*/, UINT /*mkFlags*/)
{
    //双击整行 = 点「定位」。本行不认识"会话是哪个""要跳到哪条消息"，故只把
    //"第几行被双击了"冒给宿主窗（DuiNotify 带的是本行的 ctrlId），由它去做。
    //宿主窗须调过 DuiHost::EnableDoubleClick(true)，否则这个虚函数根本不会被调到。
    NotifyParent(balloonwjui::DUIN_DBLCLK);
    return true;
}

} // namespace balloonhistory
