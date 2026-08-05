/**
 *  BalloonGroupChatDlg.cpp
 *
 *  群聊对话框实现。P1a：从 BalloonChatLayout 的内嵌 XML 构建 balloonui
 *  客户区控件树（左半聊天区 + 右侧栏群公告 / 群成员）并装入
 *  DuiFrameWindow，缓存关键控件指针，接好底部"关闭"按钮。业务收发
 *  （OnRecvMsg 等）仍是 P0 留下的空 stub，留待 P1b。
 *
 *  balloonwj@qq.com   2026.05.21
 */

#include "stdafx.h"
#include "BalloonGroupChatDlg.h"

#include "BalloonChatLayout.h"
#include "BalloonChatMsgFormat.h"
#include "../IULog.h"
#include "../FlamingoClient.h"          // CFlamingoClient：发送 / 取用户信息 / 消息列表
#include "../UserSessionData.h"         // CBuddyMessage / CContent / CMessageList ...
#include "../CustomMsgDef.h"            // WM_DEL_MSG_SENDER
#include "../net/IUProtocolData.h"      // FMG_MSG_TYPE_GROUP
#include "../../balloonui/balloonui/Controls/Input/DuiRichEditHost.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../balloonui/balloonui/Controls/List/DuiTreeView.h"

CBalloonGroupChatDlg::CBalloonGroupChatDlg()
    : m_lpFMGClient(NULL)
    , m_lpFaceList(NULL)
    , m_lpCascadeWinManager(NULL)
    , m_hMainDlg(NULL)
    , m_nGroupCode(0)
    , m_pRecvEdit(NULL)
    , m_pSendEdit(NULL)
    , m_pGroupAnnounce(NULL)
    , m_pMemberList(NULL)
    , m_pBtnClose(NULL)
    , m_pBtnSend(NULL)
    , m_hAvatarBmp(NULL)
{
    SetTitle(_T("Group Chat"));
    SetTitleBarHeight(28);
    SetButtons(true, true, true);
    SetMinSize(600, 400);

    // 标题栏透明 —— 让 9-grid 皮肤背景（ChatDlgBg.png）顶部的红蓝渐变区
    // 直接充当标题栏；标题文字与 min/max/close glyph 改白色，以在深色
    // 渐变上保持可读。背景图本身在 InitChatUi 里 ApplyChatWindowBackground。
    SetTitleBarTransparent(true);
    SetTitleTextColor(RGB(255, 255, 255));
    SetCaptionGlyphColor(RGB(255, 255, 255));
}

CBalloonGroupChatDlg::~CBalloonGroupChatDlg()
{
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

void CBalloonGroupChatDlg::OnRecvMsg(UINT nGroupCode, UINT nMsgId)
{
    // 结构与旧版 CGroupChatDlg::OnRecvMsg 一致。注意群消息在 CMessageList
    // 里仍以 CBuddyMessage 形态存取（GetGroupMsg / GetGroupMsgById 返回
    // CBuddyMessage*）。
    if (m_lpFMGClient == NULL || m_pRecvEdit == NULL || m_nGroupCode != nGroupCode)
    {
        return;
    }

    CMessageList* lpMsgList = m_lpFMGClient->GetMessageList();
    if (lpMsgList == NULL)
    {
        return;
    }
    CMessageSender* lpMsgSender = lpMsgList->GetMsgSender(FMG_MSG_TYPE_GROUP, nGroupCode);
    if (lpMsgSender == NULL)
    {
        return;
    }

    if (nMsgId == 0)
    {
        // nMsgId == 0：渲染该群名下的全部待显示消息。
        const int nCount = lpMsgSender->GetMsgCount();
        for (int i = 0; i < nCount; ++i)
        {
            AppendGroupMsgToRecv(lpMsgSender->GetGroupMsg(i));
        }
        lpMsgList->DelMsgSender(FMG_MSG_TYPE_GROUP, nGroupCode);
        ::PostMessage(m_hMainDlg, WM_DEL_MSG_SENDER, FMG_MSG_TYPE_GROUP, nGroupCode);
    }
    else
    {
        // 指定 nMsgId：只渲染该条；渲染后若发送者已空则一并删除。
        CBuddyMessage* lpMsg = lpMsgSender->GetGroupMsgById(nMsgId);
        if (lpMsg != NULL)
        {
            AppendGroupMsgToRecv(lpMsg);
            lpMsgSender->DelMsgById(nMsgId);
        }
        if (lpMsgSender->GetMsgCount() <= 0)
        {
            lpMsgList->DelMsgSender(FMG_MSG_TYPE_GROUP, nGroupCode);
            ::PostMessage(m_hMainDlg, WM_DEL_MSG_SENDER, FMG_MSG_TYPE_GROUP, nGroupCode);
        }
    }
}

void CBalloonGroupChatDlg::AppendGroupMsgToRecv(CBuddyMessage* lpMsg)
{
    if (lpMsg == NULL || m_pRecvEdit == NULL || m_lpFMGClient == NULL)
    {
        return;
    }

    // P1b 只渲染纯文本：拼接所有 CONTENT_TYPE_TEXT 片段。
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

    // 群成员昵称：优先用消息体自带的 m_strNickName（后门字段，群成员
    // 不一定在本地好友表里）；为空再按 from-uin 查用户信息。
    CString sender = lpMsg->m_strNickName.c_str();
    if (sender.IsEmpty())
    {
        CBuddyInfo* lpFrom = m_lpFMGClient->GetUserInfo(lpMsg->m_nFromUin);
        if (lpFrom != NULL)
        {
            sender = lpFrom->m_strNickName.c_str();
        }
    }

    m_pRecvEdit->AppendText(
        balloonchat::FormatDisplayLine(sender, (time_t)lpMsg->m_nTime, body));
}

void CBalloonGroupChatDlg::DoSendMessage()
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

    // 回显自己发出的这条到接收区，发送者取登录用户名。
    CString myName;
    CBuddyInfo* lpMe = m_lpFMGClient->GetUserInfo();
    if (lpMe != NULL)
    {
        myName = lpMe->m_strNickName.c_str();
    }
    m_pRecvEdit->AppendText(balloonchat::FormatDisplayLine(myName, now, text));

    // 发往服务器。SendGroupMsg 形参是 LPCTSTR，CString 可直接转。
    m_lpFMGClient->SendGroupMsg(m_nGroupCode, now, (LPCTSTR)text, m_hWnd);

    m_pSendEdit->SetText(_T(""));
    m_pSendEdit->SetFocus();

    LOG_INFO(_T("群聊已发送一条消息 (groupCode=%u, 字符数=%d)."),
             m_nGroupCode, text.GetLength());
}

void CBalloonGroupChatDlg::OnUpdateGroupInfo()
{
    // 群资料（含群名称）更新 —— 刷新标题栏。
    RefreshTitleBar();
}

void CBalloonGroupChatDlg::OnUpdateGroupNumber()
{
    // P1: refresh "members (X/Y)" label.
}

void CBalloonGroupChatDlg::OnUpdateGroupHeadPic()
{
    // 群头像更新 —— 重新加载并刷新标题栏图标。
    RefreshTitleBar();
}

void CBalloonGroupChatDlg::OnUpdateGMemberNumber(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    // P1: refresh count badge for the specified member.
}

void CBalloonGroupChatDlg::OnUpdateGMemberHeadPic(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    // P1: re-load the avatar for the specified member.
}

// ---- P1a: balloonui 客户区 UI ----

void CBalloonGroupChatDlg::InitChatUi()
{
    // 每次调用都重建客户区控件树 —— SetClientContent 会替换并释放旧树。
    // 这样窗口被销毁后重新 Create 时，再调一次本函数即可拿到与新 HWND
    // 配套的全新控件树（旧树的控件指针随之失效，这里一并刷新 m_pXxx）。

    // 旧控件树即将被替换销毁 —— 先取消其工具栏按钮的 tooltip 注册。
    balloonchat::UnregisterTooltips(m_tooltipCtrls);
    m_tooltipCtrls.clear();

    std::unique_ptr<balloonwjui::DuiControl> root = balloonchat::BuildGroupChatBody();
    if (!root)
    {
        // 内嵌 XML 是编译期常量、形态固定，正常情况下不会解析失败。
        LOG_ERROR(_T("BuildGroupChatBody 返回空 —— 群聊客户区未能构建。"));
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
    m_pGroupAnnounce = dynamic_cast<balloonwjui::DuiRichEditHost*>(
        content->FindCtrlById(balloonchat::kIdGroupAnnounce));
    m_pMemberList = dynamic_cast<balloonwjui::DuiTreeView*>(
        content->FindCtrlById(balloonchat::kIdMemberList));
    m_pBtnClose = dynamic_cast<balloonwjui::DuiButton*>(
        content->FindCtrlById(balloonchat::kIdBtnClose));
    m_pBtnSend  = dynamic_cast<balloonwjui::DuiButton*>(
        content->FindCtrlById(balloonchat::kIdBtnSend));

    // 工具栏按钮贴真实图标皮肤、注册 tooltip，底部按钮贴 PNG 皮肤。
    balloonchat::ApplyToolbarButtonStyles(content, m_tooltipCtrls);

    // DuiRichEditHost 内嵌真 RICHEDIT50W 子窗 —— XML builder 只建控件对象、
    // 不调 EnsureCreated，须在 host HWND 就绪后由调用方显式建一次。
    if (m_pRecvEdit != NULL && !m_pRecvEdit->EnsureCreated(m_hWnd))
    {
        LOG_WARNING(_T("群聊接收区 RichEdit EnsureCreated 失败。"));
    }
    if (m_pSendEdit != NULL && !m_pSendEdit->EnsureCreated(m_hWnd))
    {
        LOG_WARNING(_T("群聊输入区 RichEdit EnsureCreated 失败。"));
    }
    if (m_pGroupAnnounce != NULL && !m_pGroupAnnounce->EnsureCreated(m_hWnd))
    {
        LOG_WARNING(_T("群公告 RichEdit EnsureCreated 失败。"));
    }

    // 套 9-grid 皮肤背景（须在窗口 Create 之后）。
    balloonchat::ApplyChatWindowBackground(this);

    // 标题栏显示群名称 + 群头像。
    RefreshTitleBar();

    LOG_INFO(_T("群聊客户区 UI 初始化完成 (recv=%p, send=%p, announce=%p, members=%p)."),
             m_pRecvEdit, m_pSendEdit, m_pGroupAnnounce, m_pMemberList);
}

void CBalloonGroupChatDlg::RefreshTitleBar()
{
    if (m_lpFMGClient == NULL)
    {
        return;
    }
    CGroupList* lpGroupList = m_lpFMGClient->GetGroupList();
    if (lpGroupList == NULL)
    {
        return;
    }
    CGroupInfo* lpGroup = lpGroupList->GetGroupByCode(m_nGroupCode);
    if (lpGroup == NULL)
    {
        return;   // 群资料尚未就绪 —— 等后续 OnUpdateGroupXxx 再刷新
    }

    // 标题文字 = 群名称。
    SetTitle(lpGroup->m_strName.c_str());

    // 标题栏图标 = 群头像。加载成功才替换，失败保留原图标。
    HBITMAP hNewAvatar = balloonchat::LoadChatAvatar(lpGroup->m_nFace);
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

LRESULT CBalloonGroupChatDlg::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
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
            LOG_INFO(_T("群聊关闭按钮点击 —— 隐藏窗口。"));
            ShowWindow(SW_HIDE);
            return 0;
        }
        if (pNotify->ctrlId == balloonchat::kIdBtnSend)
        {
            DoSendMessage();
            return 0;
        }
    }

    // 其余通知（标题栏 min/max/close 等）交回 DuiFrameWindow 处理。
    bHandled = FALSE;
    return 0;
}
