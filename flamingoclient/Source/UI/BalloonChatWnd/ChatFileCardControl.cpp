/**
 *  ChatFileCardControl.cpp
 *
 *  聊天文件卡片控件，见 ChatFileCardControl.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-01
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖，与同目录 ChatBubble.cpp 相同 ----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限
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

#include "ChatFileCardControl.h"
#include "ChatPaintUtil.h"                 // AddRoundRect
#include "ChatFileAction.h"                // ChatFileActionData
#include "../../App/CustomMsgDef.h"        // WM_CHAT_FILE_ACTION
#include "../../../third-party/balloonui/balloonui/DuiHost.h"

namespace balloonchatwnd {

ChatFileCardControl::ChatFileCardControl()
    : m_hoverIdx(-1)
{
}

void ChatFileCardControl::SetCardMessage(const ChatMessage& m)
{
    m_msg = m;
    //状态变了，链接的个数与位置都可能不同：旧的点击矩形与悬停作废，等下一次绘制重新排出
    m_links = FileCardLinks();
    m_hoverIdx = -1;
    Invalidate();
}

const ChatMessage& ChatFileCardControl::CardMessage() const
{
    return m_msg;
}

void ChatFileCardControl::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const RECT card = m_rcItem;
    if (card.right <= card.left || card.bottom <= card.top)
    {
        return;
    }
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    //白色底板由文件卡片自己画（docs/聊天卡片美化方案.md 3.2，2026-10-14 起与聊天窗同一套样式）
    PaintFileCardShapes(g, card, m_msg);
    PaintFileCardText(hdc, g, card, m_msg, m_hoverIdx, m_links);
}

bool ChatFileCardControl::OnMouseMove(POINT pt, UINT /*mkFlags*/)
{
    const int idx = FileCardLinkAt(m_links, pt);
    if (idx != m_hoverIdx)
    {
        m_hoverIdx = idx;
        Invalidate();
    }
    return true;
}

bool ChatFileCardControl::OnMouseLeave()
{
    if (m_hoverIdx != -1)
    {
        m_hoverIdx = -1;
        Invalidate();
    }
    return true;
}

bool ChatFileCardControl::OnSetCursor(POINT pt)
{
    if (FileCardLinkAt(m_links, pt) >= 0)
    {
        ::SetCursor(::LoadCursor(NULL, IDC_HAND));
        return true;
    }
    return false;
}

bool ChatFileCardControl::OnLButtonDown(POINT pt, UINT /*mkFlags*/)
{
    const int idx = FileCardLinkAt(m_links, pt);
    if (idx < 0)
    {
        return false;
    }
    PostAction(m_links.m_action[idx]);
    return true;
}

bool ChatFileCardControl::OnLButtonDblClk(POINT /*pt*/, UINT /*mkFlags*/)
{
    return true;
}

void ChatFileCardControl::PostAction(int action)
{
    balloonwjui::DuiHost* host = GetHost();
    HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
    if (!::IsWindow(hwnd))
    {
        return;
    }
    //负载与聊天气泡 ChatBubble::HandleFileButton 组的完全相同，宿主按同一套规则处理；
    //异步 PostMessage，故堆分配传递，宿主处理后 delete
    ChatFileActionData* d = new ChatFileActionData();
    d->action      = action;
    d->fromMe      = m_msg.fromMe;
    d->clientMsgId = m_msg.fileClientMsgId;
    d->msgid       = m_msg.fileMsgid;
    d->localPath   = m_msg.fileLocalPath;
    d->fileName    = m_msg.fileName;
    d->fileId      = m_msg.fileFileId;
    d->md5         = m_msg.fileMd5;
    d->sizeBytes   = m_msg.fileSizeBytes;
    d->direct      = m_msg.fileDirect;
    d->directSessionId = m_msg.fileDirectSessionId;
    if (!::PostMessage(hwnd, WM_CHAT_FILE_ACTION, 0, (LPARAM)d))
    {
        delete d;
    }
}

} // namespace balloonchatwnd
