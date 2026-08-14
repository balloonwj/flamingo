/**
 *  BalloonBuddyChatDlg.cpp
 *
 *  单聊对话框实现。P1a：从 BalloonChatLayout 的内嵌 XML 构建 balloonui
 *  客户区控件树并装入 DuiFrameWindow，缓存关键控件指针，接好底部
 *  "关闭"按钮。业务收发（OnRecvMsg 等）仍是 P0 留下的空 stub，留待 P1b。
 *
 *  balloonwj@qq.com   2026.05.21
 */

#include "stdafx.h"
#include "BalloonBuddyChatDlg.h"

#include "BalloonChatLayout.h"
#include "BalloonChatMsgFormat.h"
#include "../IULog.h"
#include "../FlamingoClient.h"          // CFlamingoClient：发送 / 取用户信息 / 消息列表
#include "../UserSessionData.h"         // CBuddyMessage / CContent / CMessageList ...
#include "../CustomMsgDef.h"            // WM_DEL_MSG_SENDER
#include "../net/IUProtocolData.h"      // FMG_MSG_TYPE_BUDDY
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiRichEditHost.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"

CBalloonBuddyChatDlg::CBalloonBuddyChatDlg()
    : m_lpFMGClient(NULL)
    , m_lpFaceList(NULL)
    , m_lpCascadeWinManager(NULL)
    , m_hMainDlg(NULL)
    , m_nUTalkUin(0)
    , m_UserId(0)
    , m_LoginUserId(0)
    , m_pRecvEdit(NULL)
    , m_pSendEdit(NULL)
    , m_pBtnClose(NULL)
    , m_pBtnSend(NULL)
    , m_pHistoryPanel(NULL)
    , m_pHistorySep(NULL)
    , m_hAvatarBmp(NULL)
{
    // Configure the frame window appearance up-front so the title bar
    // child gets built with the right values when the window is created.
    SetTitle(_T("Chat"));
    SetTitleBarHeight(28);
    SetButtons(true, true, true);
    SetMinSize(480, 360);

    // 标题栏透明 —— 让 9-grid 皮肤背景（ChatDlgBg.png）顶部的红蓝渐变区
    // 直接充当标题栏；标题文字与 min/max/close glyph 改白色，以在深色
    // 渐变上保持可读。背景图本身在 InitChatUi 里 ApplyChatWindowBackground。
    SetTitleBarTransparent(true);
    SetTitleTextColor(RGB(255, 255, 255));
    SetCaptionGlyphColor(RGB(255, 255, 255));
}

CBalloonBuddyChatDlg::~CBalloonBuddyChatDlg()
{
    // DuiFrameWindow destructor handles the HWND lifetime if still alive.

    // 工具栏按钮的 tooltip 须在控件树销毁前取消注册，否则 DuiToolTipMgr
    // 会留下悬空指针。
    balloonchat::UnregisterTooltips(m_tooltipCtrls);
    m_tooltipCtrls.clear();

    // 标题栏头像位图由本类持有，需在此释放。
    if (m_hAvatarBmp != NULL)
    {
        ::DeleteObject(m_hAvatarBmp);
        m_hAvatarBmp = NULL;
    }
}

// ---- Business stubs ----
// Intentionally empty in P0. Each is documented with what P1+ must do.

void CBalloonBuddyChatDlg::OnRecvMsg(UINT nUTalkUin, UINT nMsgId)
{
    // 结构与旧版 CBuddyChatDlg::OnRecvMsg 一致：从 CMessageList 取出该
    // 好友名下的消息，逐条渲染后把对应的 MsgSender 整体删除，并通知主
    // 窗口同步会话列表。
    if (m_lpFMGClient == NULL || m_pRecvEdit == NULL)
    {
        return;
    }

    CMessageList* lpMsgList = m_lpFMGClient->GetMessageList();
    if (lpMsgList == NULL)
    {
        return;
    }
    CMessageSender* lpMsgSender = lpMsgList->GetMsgSender(FMG_MSG_TYPE_BUDDY, nUTalkUin);
    if (lpMsgSender == NULL)
    {
        return;
    }

    if (nMsgId == 0)
    {
        // nMsgId == 0：渲染该发送者名下的全部待显示消息。
        const int nCount = lpMsgSender->GetMsgCount();
        for (int i = 0; i < nCount; ++i)
        {
            AppendBuddyMsgToRecv(lpMsgSender->GetBuddyMsg(i));
        }
        lpMsgList->DelMsgSender(FMG_MSG_TYPE_BUDDY, nUTalkUin);
        ::PostMessage(m_hMainDlg, WM_DEL_MSG_SENDER, FMG_MSG_TYPE_BUDDY, nUTalkUin);
    }
    else
    {
        // 指定 nMsgId：只渲染该条；渲染后若发送者已空则一并删除。
        CBuddyMessage* lpMsg = lpMsgSender->GetBuddyMsgById(nMsgId);
        if (lpMsg != NULL)
        {
            AppendBuddyMsgToRecv(lpMsg);
            lpMsgSender->DelMsgById(nMsgId);
        }
        if (lpMsgSender->GetMsgCount() <= 0)
        {
            lpMsgList->DelMsgSender(FMG_MSG_TYPE_BUDDY, nUTalkUin);
            ::PostMessage(m_hMainDlg, WM_DEL_MSG_SENDER, FMG_MSG_TYPE_BUDDY, nUTalkUin);
        }
    }
}

void CBalloonBuddyChatDlg::AppendBuddyMsgToRecv(CBuddyMessage* lpMsg)
{
    if (lpMsg == NULL || m_pRecvEdit == NULL || m_lpFMGClient == NULL)
    {
        return;
    }

    // P1b 只渲染纯文本：把消息体里所有 CONTENT_TYPE_TEXT 片段拼起来。
    // 图片 / 表情 / 文件 / 窗口抖动等其它内容类型留待 P2。
    CString body;
    for (size_t i = 0; i < lpMsg->m_arrContent.size(); ++i)
    {
        CContent* lpContent = lpMsg->m_arrContent[i];
        if (lpContent != NULL && lpContent->m_nType == CONTENT_TYPE_TEXT)
        {
            body += lpContent->m_strText.c_str();
        }
    }
    if (body.IsEmpty())
    {
        return;   // 不含文本内容的消息，P1b 不显示
    }
    body = balloonchat::StripControlTags(body);

    // 发送者昵称：自己发的取登录用户名，对方发的按 from-uin 查用户信息。
    CString sender;
    if (lpMsg->m_nFromUin == (UINT)m_LoginUserId)
    {
        CBuddyInfo* lpMe = m_lpFMGClient->GetUserInfo();
        if (lpMe != NULL)
        {
            sender = lpMe->m_strNickName.c_str();
        }
    }
    else
    {
        CBuddyInfo* lpBuddy = m_lpFMGClient->GetUserInfo(lpMsg->m_nFromUin);
        if (lpBuddy != NULL)
        {
            sender = lpBuddy->m_strNickName.c_str();
        }
    }

    m_pRecvEdit->AppendText(
        balloonchat::FormatDisplayLine(sender, (time_t)lpMsg->m_nTime, body));
}

void CBalloonBuddyChatDlg::DoSendMessage()
{
    if (m_pSendEdit == NULL || m_lpFMGClient == NULL)
    {
        return;
    }

    CString text = m_pSendEdit->GetText();
    const balloonchat::SendValidateResult vr = balloonchat::ValidateOutgoing(text);
    if (vr == balloonchat::kSendEmpty)
    {
        ::MessageBox(m_hWnd, _T("发送内容不能为空！"), _T("提示"),
                     MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (vr == balloonchat::kSendTooLong)
    {
        ::MessageBox(m_hWnd, _T("发送内容太长，请分条发送！"), _T("提示"),
                     MB_OK | MB_ICONINFORMATION);
        return;
    }

    const time_t now = time(NULL);

    // 取登录用户名 / 好友昵称 —— 回显与发送都要用。
    CString myName;
    CString buddyName;
    CBuddyInfo* lpMe = m_lpFMGClient->GetUserInfo();
    if (lpMe != NULL)
    {
        myName = lpMe->m_strNickName.c_str();
    }
    CBuddyInfo* lpBuddy = m_lpFMGClient->GetUserInfo(m_UserId);
    if (lpBuddy != NULL)
    {
        buddyName = lpBuddy->m_strNickName.c_str();
    }

    // 先把自己发出的这条回显到接收区。
    m_pRecvEdit->AppendText(balloonchat::FormatDisplayLine(myName, now, text));

    // 发往服务器（const wchar_t* 会隐式构造 tstring 实参）。
    m_lpFMGClient->SendBuddyMsg(m_LoginUserId, (LPCTSTR)myName,
                                m_UserId, (LPCTSTR)buddyName,
                                now, (LPCTSTR)text, m_hWnd);

    // 清空输入区，焦点回到输入区，便于连续输入。
    m_pSendEdit->SetText(_T(""));
    m_pSendEdit->SetFocus();

    LOG_INFO(_T("单聊已发送一条消息 (toUid=%d, 字符数=%d)."),
             m_UserId, text.GetLength());
}

void CBalloonBuddyChatDlg::OnUpdateBuddyNumber()
{
    // 好友资料（含昵称）更新 —— 刷新标题栏。
    RefreshTitleBar();
}

void CBalloonBuddyChatDlg::OnUpdateBuddySign()
{
    // P2: 签名目前未在标题栏 / 客户区展示，暂不处理。
}

void CBalloonBuddyChatDlg::OnUpdateBuddyHeadPic()
{
    // 好友头像更新 —— 重新加载并刷新标题栏图标。
    RefreshTitleBar();
}

BOOL CBalloonBuddyChatDlg::IsFilesTransferring()
{
    // P2: query the in-dialog file transfer controller.
    return FALSE;
}

// ---- P1a: balloonui 客户区 UI ----

void CBalloonBuddyChatDlg::InitChatUi()
{
    // 每次调用都重建客户区控件树 —— SetClientContent 会替换并释放旧树。
    // 这样窗口被销毁后重新 Create 时，再调一次本函数即可拿到与新 HWND
    // 配套的全新控件树（旧树的控件指针随之失效，这里一并刷新 m_pXxx）。

    // 旧控件树即将被替换销毁 —— 先取消其工具栏按钮的 tooltip 注册。
    balloonchat::UnregisterTooltips(m_tooltipCtrls);
    m_tooltipCtrls.clear();

    std::unique_ptr<balloonwjui::DuiControl> root = balloonchat::BuildBuddyChatBody();
    if (!root)
    {
        // 内嵌 XML 是编译期常量、形态固定，正常情况下不会解析失败。
        LOG_ERROR(_T("BuildBuddyChatBody 返回空 —— 单聊客户区未能构建。"));
        return;
    }
    SetClientContent(std::move(root));

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        LOG_ERROR(_T("SetClientContent 之后 GetClientContent 仍为空。"));
        return;
    }

    // 控件点名：按 BalloonChatLayout 约定的数字 id 取回关键控件指针。
    m_pRecvEdit = dynamic_cast<balloonwjui::DuiRichEditHost*>(
        content->FindCtrlById(balloonchat::kIdRecvEdit));
    m_pSendEdit = dynamic_cast<balloonwjui::DuiRichEditHost*>(
        content->FindCtrlById(balloonchat::kIdSendEdit));
    m_pBtnClose = dynamic_cast<balloonwjui::DuiButton*>(
        content->FindCtrlById(balloonchat::kIdBtnClose));
    m_pBtnSend  = dynamic_cast<balloonwjui::DuiButton*>(
        content->FindCtrlById(balloonchat::kIdBtnSend));
    m_pHistoryPanel = content->FindCtrlById(balloonchat::kIdHistoryPanel);
    m_pHistorySep   = content->FindCtrlById(balloonchat::kIdHistorySep);

    // 消息记录面板默认隐藏 —— 点中部"记录"按钮才展开（窗口随后 ShowWindow
    // / SetWindowPos 触发的 WM_SIZE 会按隐藏状态重排，主聊天列撑满）。
    if (m_pHistoryPanel != NULL)
    {
        m_pHistoryPanel->SetVisible(false);
    }
    if (m_pHistorySep != NULL)
    {
        m_pHistorySep->SetVisible(false);
    }

    // 工具栏按钮贴真实图标皮肤、注册 tooltip，底部按钮贴 PNG 皮肤。
    balloonchat::ApplyToolbarButtonStyles(content, m_tooltipCtrls);

    // DuiRichEditHost 内嵌真 RICHEDIT50W 子窗 —— XML builder 只建控件对象、
    // 不调 EnsureCreated，须在 host HWND 就绪后由调用方显式建一次。
    if (m_pRecvEdit != NULL && !m_pRecvEdit->EnsureCreated(m_hWnd))
    {
        LOG_WARNING(_T("单聊接收区 RichEdit EnsureCreated 失败。"));
    }
    if (m_pSendEdit != NULL && !m_pSendEdit->EnsureCreated(m_hWnd))
    {
        LOG_WARNING(_T("单聊输入区 RichEdit EnsureCreated 失败。"));
    }

    // 套 9-grid 皮肤背景（须在窗口 Create 之后）。
    balloonchat::ApplyChatWindowBackground(this);

    // 标题栏显示好友昵称 + 头像。
    RefreshTitleBar();

    LOG_INFO(_T("单聊客户区 UI 初始化完成 (recv=%p, send=%p, close=%p, send-btn=%p)."),
             m_pRecvEdit, m_pSendEdit, m_pBtnClose, m_pBtnSend);
}

void CBalloonBuddyChatDlg::RefreshTitleBar()
{
    if (m_lpFMGClient == NULL)
    {
        return;
    }
    CBuddyInfo* lpBuddy = m_lpFMGClient->GetUserInfo(m_UserId);
    if (lpBuddy == NULL)
    {
        return;   // 好友资料尚未就绪 —— 等后续 OnUpdateBuddyXxx 再刷新
    }

    // 标题文字 = 好友昵称。
    SetTitle(lpBuddy->m_strNickName.c_str());

    // 标题栏图标 = 好友头像。加载成功才替换，失败保留原图标。
    HBITMAP hNewAvatar = balloonchat::LoadChatAvatar((int)lpBuddy->m_nFace);
    if (hNewAvatar != NULL)
    {
        SetIcon(hNewAvatar);
        if (m_hAvatarBmp != NULL)
        {
            ::DeleteObject(m_hAvatarBmp);
        }
        m_hAvatarBmp = hNewAvatar;
    }
}

void CBalloonBuddyChatDlg::ToggleHistoryPanel()
{
    if (m_pHistoryPanel == NULL)
    {
        return;
    }

    const bool show = !m_pHistoryPanel->IsVisible();
    m_pHistoryPanel->SetVisible(show);
    if (m_pHistorySep != NULL)
    {
        m_pHistorySep->SetVisible(show);
    }

    // 强制重排客户区 —— DuiLayout 会跳过隐藏子控件，主聊天列随之撑满 /
    // 让出空间；再整窗 Invalidate 触发重绘。
    balloonwjui::DuiControl* content = GetClientContent();
    if (content != NULL)
    {
        content->ForceLayout(content->GetRect());
    }
    ::InvalidateRect(m_hWnd, NULL, FALSE);

    LOG_INFO(_T("单聊消息记录面板切换为 %s。"), show ? _T("展开") : _T("隐藏"));
}

LRESULT CBalloonBuddyChatDlg::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                          LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify =
        reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);

    if (pNotify != NULL && pNotify->code == balloonwjui::DUIN_CLICK)
    {
        if (pNotify->ctrlId == balloonchat::kIdBtnClose)
        {
            // P1a：关闭 = 隐藏窗口。不 DestroyWindow —— 等 P1b 把对话框接入
            // MainDlg 的会话表后，再由那条路径统一管理销毁与移除。
            LOG_INFO(_T("单聊关闭按钮点击 —— 隐藏窗口。"));
            ShowWindow(SW_HIDE);
            return 0;
        }
        if (pNotify->ctrlId == balloonchat::kIdBtnSend)
        {
            DoSendMessage();
            return 0;
        }
        if (pNotify->ctrlId == balloonchat::kIdTbMsgHistory)
        {
            ToggleHistoryPanel();
            return 0;
        }
    }

    // 其余通知（标题栏 min/max/close 等）交回 DuiFrameWindow 处理。
    bHandled = FALSE;
    return 0;
}
