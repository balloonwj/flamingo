/**
 *  ChatScrollThumb.cpp
 *
 *  ChatScrollThumb.h 的实现：聊天窗口一族共用的细滚动条的几何计算与绘制。计算方式与聊天消息区
 *  原有的实现逐项相同（滑块高度按可视区占内容的比例、不短于 kChatScrollMinThumb）。
 *  本文件不 include stdafx.h，与 FlamingoUnit 测试工程逐字共用同一份源码。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include <windows.h>
#include <gdiplus.h>

#include "ChatScrollThumb.h"

namespace balloonchatwnd {

namespace {

// 按可视区占内容的比例算滑块高度，夹在 [kChatScrollMinThumb, viewHeight] 之间。
int ThumbHeight(int viewHeight, int contentHeight)
{
    int thumbH = (int)((LONGLONG)viewHeight * viewHeight / contentHeight);
    if (thumbH < kChatScrollMinThumb)
    {
        thumbH = kChatScrollMinThumb;
    }
    if (thumbH > viewHeight)
    {
        thumbH = viewHeight;
    }
    return thumbH;
}

} // anonymous namespace

int ClampChatScrollY(int scrollY, int viewHeight, int contentHeight)
{
    const int maxScroll = contentHeight - viewHeight;
    if (maxScroll <= 0 || scrollY < 0)
    {
        return 0;
    }
    return (scrollY > maxScroll) ? maxScroll : scrollY;
}

RECT ComputeChatScrollThumb(const RECT& view, int viewHeight, int contentHeight, int scrollY)
{
    RECT none = { 0, 0, 0, 0 };
    if (viewHeight <= 0 || contentHeight <= viewHeight)
    {
        return none;
    }
    const int thumbH = ThumbHeight(viewHeight, contentHeight);
    const int maxScroll = contentHeight - viewHeight;
    const int y = ClampChatScrollY(scrollY, viewHeight, contentHeight);
    const int thumbY = view.top + (int)((LONGLONG)y * (viewHeight - thumbH) / maxScroll);
    const int thumbX = view.right - kChatScrollBarWidth - kChatScrollBarMargin;
    RECT r = { thumbX, thumbY, thumbX + kChatScrollBarWidth, thumbY + thumbH };
    return r;
}

int ChatScrollYFromThumbTop(int thumbTop, const RECT& view, int viewHeight, int contentHeight)
{
    if (viewHeight <= 0 || contentHeight <= viewHeight)
    {
        return 0;
    }
    const int thumbH = ThumbHeight(viewHeight, contentHeight);
    const int track = viewHeight - thumbH;   // 滑块可移动的像素范围
    const int maxScroll = contentHeight - viewHeight;
    int rel = thumbTop - view.top;
    if (rel < 0)
    {
        rel = 0;
    }
    if (rel > track)
    {
        rel = track;
    }
    const int y = (track > 0) ? (int)((LONGLONG)rel * maxScroll / track) : 0;
    return ClampChatScrollY(y, viewHeight, contentHeight);
}

bool HitChatScrollBand(POINT pt, const RECT& thumb, const RECT& view)
{
    if (thumb.bottom <= thumb.top)
    {
        return false;   // 内容没有超出，没有滚动条
    }
    const bool inBand = (pt.x >= thumb.left - kChatScrollBarMargin
                         && pt.x <= thumb.right + kChatScrollBarMargin);
    const bool inView = (pt.y >= view.top && pt.y < view.bottom);
    return inBand && inView;
}

void PaintChatScrollThumb(Gdiplus::Graphics& g, const RECT& thumb)
{
    if (thumb.bottom <= thumb.top || thumb.right <= thumb.left)
    {
        return;
    }
    Gdiplus::GraphicsPath path;
    const Gdiplus::REAL d = (Gdiplus::REAL)(thumb.right - thumb.left);
    path.AddArc((Gdiplus::REAL)thumb.left, (Gdiplus::REAL)thumb.top, d, d, 180.0f, 180.0f);
    path.AddArc((Gdiplus::REAL)thumb.left, (Gdiplus::REAL)thumb.bottom - d, d, d, 0.0f, 180.0f);
    path.CloseFigure();
    Gdiplus::SolidBrush brush(Gdiplus::Color(kChatScrollThumbAlpha, kChatScrollThumbGray,
                                             kChatScrollThumbGray, kChatScrollThumbGray));
    g.FillPath(&brush, &path);
}

} // namespace balloonchatwnd
