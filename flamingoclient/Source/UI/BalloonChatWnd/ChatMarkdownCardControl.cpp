/**
 *  ChatMarkdownCardControl.cpp
 *
 *  Markdown 消息卡片控件，见 ChatMarkdownCardControl.h。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "ChatMarkdownCardControl.h"

#include <objidl.h>
#include <gdiplus.h>

#include "ChatMarkdownCard.h"     // 卡片宽度、高度与内容绘制（与聊天气泡共用）
#include "ChatMessageList.h"      // WM_CHAT_OPEN_MARKDOWN_PREVIEW / MarkdownPreviewOpenRequest
#include "../../Common/Log/IULog.h"
#include "../../../third-party/balloonui/balloonui/DuiHost.h"

namespace balloonchatwnd {

ChatMarkdownCardControl::ChatMarkdownCardControl()
    : m_summaryH(-1)
    , m_measuredWidth(-1)
{
}

SIZE ChatMarkdownCardControl::MeasureCard(const std::wstring& summary, int maxWidth)
{
    SIZE size = { 0, 0 };
    const int width = (maxWidth < kMarkdownCardWidth) ? maxWidth : kMarkdownCardWidth;
    if (width <= 0)
        return size;
    HDC hdc = ::GetDC(NULL);
    const int summaryH = MeasureMarkdownCardSummary(hdc, summary, width);
    ::ReleaseDC(NULL, hdc);
    size.cx = width;
    size.cy = MarkdownCardHeight(summaryH);
    return size;
}

void ChatMarkdownCardControl::SetCard(const MarkdownCardInfo& info)
{
    m_info = info;
    m_summaryH = -1;
    Invalidate();
}

const MarkdownCardInfo& ChatMarkdownCardControl::GetCard() const
{
    return m_info;
}

void ChatMarkdownCardControl::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const int width = m_rcItem.right - m_rcItem.left;
    const int height = m_rcItem.bottom - m_rcItem.top;
    if (width <= 0 || height <= 0)
        return;
    //摘要高度按控件宽度测量；宽度不变时沿用上次的结果
    if (m_summaryH < 0 || m_measuredWidth != width)
    {
        m_summaryH = MeasureMarkdownCardSummary(hdc, m_info.m_summary, width);
        m_measuredWidth = width;
    }
    //白色底板由卡片自己画（docs/聊天卡片美化方案.md 3.2，2026-10-14 起与聊天窗同一套样式）
    PaintMarkdownCardContent(hdc, m_rcItem, m_info.m_title, m_info.m_summary, m_summaryH);
}

bool ChatMarkdownCardControl::OnSetCursor(POINT pt)
{
    if (!::PtInRect(&m_rcItem, pt))
        return false;
    ::SetCursor(::LoadCursor(NULL, IDC_HAND));
    return true;
}

bool ChatMarkdownCardControl::OnLButtonDown(POINT pt, UINT /*mkFlags*/)
{
    if (!::PtInRect(&m_rcItem, pt))
        return false;
    PostPreviewOpen();
    return true;
}

bool ChatMarkdownCardControl::OnLButtonDblClk(POINT /*pt*/, UINT /*mkFlags*/)
{
    return true;
}

void ChatMarkdownCardControl::PostPreviewOpen()
{
    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (!::IsWindow(hwnd))
        return;
    MarkdownPreviewOpenRequest* req = new MarkdownPreviewOpenRequest();
    req->m_sourceUtf8 = m_info.m_sourceUtf8;
    req->m_title = m_info.m_title;
    req->m_sender = m_info.m_sender;
    req->m_fromMe = m_info.m_fromMe;
    req->m_timestampMs = m_info.m_timestampMs;
    req->m_key = m_info.m_key;
    const unsigned bytes = (unsigned)req->m_sourceUtf8.size();
    //投递失败时请求不会被宿主释放，在这里释放
    if (!::PostMessage(hwnd, WM_CHAT_OPEN_MARKDOWN_PREVIEW, 0, (LPARAM)req))
    {
        delete req;
        return;
    }
    LOG_INFO("markdown card: open preview requested, key=%s, bytes=%u.", m_info.m_key.c_str(), bytes);
}

} // namespace balloonchatwnd
