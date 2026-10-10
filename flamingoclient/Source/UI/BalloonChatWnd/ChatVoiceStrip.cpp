/**
 *  ChatVoiceStrip.cpp
 *
 *  语音条控件，见 ChatVoiceStrip.h。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include <gdiplus.h>

#include "ChatVoiceStrip.h"
#include "ChatVoicePaint.h"                // PaintVoiceBarShapes / PaintVoiceBarText / 红点几何
#include "ChatPaintUtil.h"                 // AddRoundRect
#include "ChatMessageList.h"               // WM_CHAT_VOICE_ACTION
#include "../../../third-party/balloonui/balloonui/DuiHost.h"

namespace balloonchatwnd {

namespace {

//语音条底色与圆角，与聊天窗里对方发来的气泡一致
const Gdiplus::Color kStripBg(255, 0xF3, 0xF2, 0xEF);
const Gdiplus::REAL  kStripRadius = 12.0f;

} // namespace

int VoiceStripWidth(const ChatMessage& m)
{
    return VoiceBubbleWidth(m.voiceDuration) + kVoiceDotGap + kVoiceDotSize;
}

RECT VoiceStripBarRect(const RECT& item, const ChatMessage& m)
{
    RECT bar = item;
    bar.right = item.left + VoiceBubbleWidth(m.voiceDuration);
    if (bar.right > item.right)
    {
        bar.right = item.right;
    }
    return bar;
}

VoiceActionRequest MakeVoiceToggleRequest(const ChatMessage& m)
{
    VoiceActionRequest req;
    req.m_msgid = m.msgid;
    req.m_clientMsgId = m.clientMsgId;
    return req;
}

ChatVoiceStrip::ChatVoiceStrip()
    : m_showDot(false)
{
}

void ChatVoiceStrip::SetVoiceMessage(const ChatMessage& m, bool showDot)
{
    m_msg = m;
    m_showDot = showDot;
    Invalidate();
}

const ChatMessage& ChatVoiceStrip::VoiceMessage() const
{
    return m_msg;
}

void ChatVoiceStrip::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const RECT bar = VoiceStripBarRect(m_rcItem, m_msg);
    if (bar.right <= bar.left || bar.bottom <= bar.top)
    {
        return;
    }
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    //只在本控件的矩形之内作画。红点紧贴矩形右边、底色紧贴矩形下边，抗锯齿会给矩形外紧挨着的一列与一行像素
    //涂上浅色；而红点消失等局部刷新只重绘本控件的矩形，画到外面的像素不会被擦除，表现为残留的灰点（第 5 期联调）
    g.SetClip(Gdiplus::Rect(m_rcItem.left, m_rcItem.top, m_rcItem.right - m_rcItem.left, m_rcItem.bottom - m_rcItem.top),
              Gdiplus::CombineModeIntersect);
    //底色：与聊天气泡同色同圆角
    {
        Gdiplus::GraphicsPath bg;
        chatpaint::AddRoundRect(bg, (Gdiplus::REAL)bar.left, (Gdiplus::REAL)bar.top,
                                (Gdiplus::REAL)(bar.right - bar.left), (Gdiplus::REAL)(bar.bottom - bar.top),
                                kStripRadius);
        Gdiplus::SolidBrush brush(kStripBg);
        g.FillPath(&brush, &bg);
    }
    PaintVoiceBarShapes(g, bar, m_msg, m_showDot);
    PaintVoiceBarText(hdc, bar, m_msg);
}

bool ChatVoiceStrip::OnSetCursor(POINT pt)
{
    const RECT bar = VoiceStripBarRect(m_rcItem, m_msg);
    if (::PtInRect(&bar, pt))
    {
        ::SetCursor(::LoadCursor(NULL, IDC_HAND));
        return true;
    }
    return false;
}

bool ChatVoiceStrip::OnLButtonDown(POINT pt, UINT /*mkFlags*/)
{
    const RECT bar = VoiceStripBarRect(m_rcItem, m_msg);
    if (!::PtInRect(&bar, pt))
    {
        return false;
    }
    PostToggle();
    return true;
}

bool ChatVoiceStrip::OnLButtonDblClk(POINT /*pt*/, UINT /*mkFlags*/)
{
    return true;
}

void ChatVoiceStrip::PostToggle()
{
    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (!::IsWindow(hwnd))
    {
        return;
    }
    //负载与聊天气泡 ChatBubble::PostVoiceAction 组的相同，宿主按同一套规则处理；异步 PostMessage，故堆分配传递，
    //宿主处理后 delete
    VoiceActionRequest* req = new VoiceActionRequest(MakeVoiceToggleRequest(m_msg));
    if (!::PostMessage(hwnd, WM_CHAT_VOICE_ACTION, (WPARAM)kVoiceActionToggle, (LPARAM)req))
    {
        delete req;
    }
}

} // namespace balloonchatwnd
