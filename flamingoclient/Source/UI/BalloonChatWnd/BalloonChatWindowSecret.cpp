/**
 *  BalloonChatWindowSecret.cpp
 *
 *  单聊窗里「单聊密聊」的那部分（docs/密聊方案.md 4.7、4.8），从 BalloonChatWindow.cpp 分出来单独成文件：
 *    · 头部「密聊」按钮的显隐与点击：在线检查、已有邀请时提示、本机无法防截图时先确认，再经密聊宿主发起邀请；
 *    · 邀请卡片：本人发起成功后插入「等待对方同意」卡片（带「取消」），对方发来邀请时插入「同意」「忽略」卡片，
 *      都带剩余秒数、每秒刷新；同意、忽略、撤回、超时、在其他设备处理之后移除，没能建立时插一条本地提示；
 *    · 卡片是纯本地气泡，首屏历史渲染清空列表后按登记重建；窗口没开时到达的邀请在倒计时内打开窗口时补出；
 *    · 窗口的三种密聊状态（普通、密聊中、只能查看）：进入时清空普通历史、打开防截图与列表的密聊限制、隐藏受管按钮、
 *      显示头部条；本人退出时清零并移除密聊内容、撤销内存图片、恢复普通历史；会话被服务端结束或本机断线时转为只能查看；
 *    · 密聊文字与图片的收发：发送交给密聊模块加密，图片读入内存、登记为内存图片（不落盘）；收到的内容只存在于列表内存里。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "BalloonChatWindow.h"

#include <ctime>    // time：本人密聊气泡的时间
#include <vector>
#include <gdiplus.h> // 发送密聊图片前读取宽高

#include "ChatMessageList.h"
#include "ChatMockData.h"
#include "ChatImageCache.h"                               // 密聊图片登记为内存图片，退出密聊时撤销
#include "ChatMessageMenu.h"                              // kChatImageState*：图片气泡的状态取值
#include "ChatInputRichEdit.h"                            // 只能查看时输入框只读
#include "ChatMarkdown.h"                                 // ApplyMarkdownEditorButton：退出密聊时恢复工具栏按钮
#include "ChatScreenshotMenu.h"                           // ApplyScreenshotButtons：退出密聊时截图按钮按截图模块是否可用显隐
#include "../../Feature/Screenshot/ScreenshotEntry.h"     // IsScreenshotAvailable
#include "ChatSecretMode.h"                               // 卡片内容、按钮动作、提示文字
#include "ChatSendLimit.h"                                // kSendTextTooLargeTip：超长文字的提示
#include "SecretImageOverlay.h"                           // 退出密聊时关闭大图浮层
#include "ChatWindowLayout.h"                             // kIdHdrSecret 等控件 id
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"    // 头部条的文字
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"  // 头部条加高
#include "../BalloonMain/OnlineCapability.h"              // kActionRealtimeCall：发起与同意前的在线检查
#include "../../Feature/SecretChat/SecretChatHost.h"      // 发起、应答、撤回与待应答邀请的查询
#include "../../Feature/SecretChat/SecretChatGate.h"      // 剩余时间的换算
#include "../../App/BrandText.h"                          // g_strAppTitle：确认框标题
#include "../../App/FlamingoClient.h"                     // 取本人 userid
#include "../../Common/Log/IULog.h"
#include "../../Common/I18n/I18n.h"                       // TR：界面文字的多语言查找
#include "../../net/Protocol.h"                           // error_code_secret_busy / secret_state_*

namespace {

// 邀请卡片倒计时定时器的间隔（毫秒）：卡片上的剩余时间以秒为单位
const UINT kSecretInviteTickMs = 1000;

// 邀请过了到期时刻还没等到服务端的结果（服务端每秒扫描一次超时，正常情况下一两秒内就会推来「未接受」），
// 再等这么久（毫秒）仍没有结果时，本机撤回或忽略这条邀请并移除卡片。典型情形是到期前后本机断了网
const unsigned long long kSecretInviteGraceMs = 5000;

// 秒换算成毫秒：本人密聊气泡的时间取 time() 的秒数
const long long kMsPerSecond = 1000;

// 密聊头部条的高度（像素）：一行状态文字与链接的高度，与 ChatWindowLayout.cpp 里 XML 的 fixedHeight="34" 一致；
// 有防截图提示时再加一行，提示行高与 XML 里那个标签的 fixedHeight="20" 一致
const int kSecretBannerRowPx  = 34;
const int kSecretBannerWarnPx = 20;

} // namespace

bool CBalloonChatWindow::IsPeerSelf() const
{
    return m_lpFMGClient != NULL && m_peerUserId != 0
        && m_peerUserId == m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID;
}

void CBalloonChatWindow::ApplySecretEntryVisibility()
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonwjui::DuiControl* btn = (content != NULL) ? content->FindCtrlById(balloonchatwnd::kIdHdrSecret) : NULL;
    if (btn == NULL)
    {
        return;
    }
    const bool show = m_peerUserId != 0 && !IsPeerSelf()
                      && balloonsecret::CSecretChatHost::GetInstance().IsEntryAvailable()
                      && balloonchatwnd::IsSecretCtrlAvailable(m_secretState, balloonchatwnd::kIdHdrSecret);
    if (btn->IsVisible() != show)
    {
        btn->SetVisible(show);
        //SetVisible 不会重排，对按钮所在的那一行补一次布局（UI 约定第 5 条）
        balloonwjui::DuiControl* row = btn->GetParent();
        if (row != NULL)
        {
            row->ForceLayout(row->GetRect());
        }
        LOG_INFO("secret entry %s, peer=%u.", show ? "shown" : "hidden", m_peerUserId);
    }
    //「只能查看」头部条上的「重新邀请」同样取决于入口是否可用（企业关闭开关后不再显示）
    if (m_secretState == balloonchatwnd::kSecretStateReadOnly)
    {
        ApplySecretBanner();
    }
}

LRESULT CBalloonChatWindow::OnSecretActionMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    switch ((int)wParam)
    {
    //头部「密聊」按钮：发起邀请
    case kSecretActInvite:
        StartSecretInvite();
        break;

    //头部条「退出密聊」：不再二次确认（docs/密聊方案.md 4.7），清除内容并恢复普通历史
    case kSecretActExit:
        ExitSecretMode(true, true);
        break;

    //「只能查看」头部条「重新邀请」：与「密聊」按钮走同一条发起流程，对方同意后回到密聊中，新消息接在原有内容之后
    case kSecretActReinvite:
        StartSecretInvite();
        break;

    //头部条「安全码」
    case kSecretActSafetyCode:
        ShowSecretSafetyCode();
        break;

    //不认识的操作：只记日志
    default:
        LOG_WARNING("unknown secret window action %d, peer=%u.", (int)wParam, m_peerUserId);
        break;
    }
    return 0;
}

void CBalloonChatWindow::StartSecretInvite()
{
    if (m_peerUserId == 0 || IsPeerSelf())
    {
        return;
    }
    //邀请要经服务端转达，离线时发起必然失败，在入口就拦住
    if (!CheckOnlineFor(balloonmain::kActionRealtimeCall))
    {
        return;
    }
    balloonsecret::CSecretChatHost& host = balloonsecret::CSecretChatHost::GetInstance();
    if (!host.IsEntryAvailable())
    {
        ShowToast(balloonchatwnd::SecretInviteFailureText(-1).c_str());
        return;
    }
    //已经发出、还在等应答，或已有卡片（本人发出的或对方发来的），或正在密聊中：同一对用户同一时刻只能有一个
    if (m_secretInviting || !m_secretInviteCards.empty() || m_secretState == balloonchatwnd::kSecretStateActive)
    {
        ShowToast(balloonchatwnd::SecretInviteFailureText(error_code_secret_busy).c_str());
        return;
    }
    //本机做不到防截图时如实告知并让用户确认（docs/密聊方案.md 4.6 第 1 条，用户 2026-10-06 确认允许继续）
    const int protect = host.ProbeProtect();
    if (protect <= secretchat::kProtectNone)
    {
        LOG_WARNING("secret invite: local capture protect unavailable, asking for confirmation, peer=%u.", m_peerUserId);
        const int answer = ::MessageBox(m_hWnd, TR(L"本机系统无法防止截图，对方会看到这一提示。仍要发起密聊吗？"),
                                        g_strAppTitle.c_str(), MB_YESNO | MB_ICONWARNING);
        if (answer != IDYES)
        {
            return;
        }
    }
    if (!host.Invite((int64_t)m_peerUserId))
    {
        ShowToast(balloonchatwnd::SecretInviteFailureText(-1).c_str());
        LOG_WARNING("secret invite not sent, peer=%u.", m_peerUserId);
        return;
    }
    m_secretInviting = true;
    LOG_INFO("secret invite sent from chat window, peer=%u, local protect=%d.", m_peerUserId, protect);
}

void CBalloonChatWindow::OnSecretInviteSent(unsigned long long sessionId, unsigned int remainMs, int code)
{
    m_secretInviting = false;
    if (code != 0 || sessionId == 0)
    {
        ShowToast(balloonchatwnd::SecretInviteFailureText(code).c_str());
        LOG_INFO("secret invite failed, peer=%u, code=%d.", m_peerUserId, code);
        return;
    }
    //发起方在邀请阶段不知道对方的防截图级别（2602 应答不带），按能防截图处理，建立时再按 2605 里的级别提示
    AddSecretInviteCard(sessionId, true, remainMs, secretchat::kProtectExclude);
    LOG_INFO("secret invite waiting, session=%llu, peer=%u, remain=%u ms.", sessionId, m_peerUserId, remainMs);
}

void CBalloonChatWindow::ShowSecretInviteCard(unsigned long long sessionId, unsigned int remainMs, int peerProtect)
{
    AddSecretInviteCard(sessionId, false, remainMs, peerProtect);
    LOG_INFO("secret invite card shown, session=%llu, peer=%u, remain=%u ms, peer protect=%d.", sessionId,
             m_peerUserId, remainMs, peerProtect);
}

void CBalloonChatWindow::OnSecretSessionState(unsigned long long sessionId, int state, int reason, int peerProtect)
{
    LOG_INFO("secret session state, session=%llu, peer=%u, state=%d, reason=%d, peer protect=%d, window state=%d.",
             sessionId, m_peerUserId, state, reason, peerProtect, m_secretState);
    //已建立：移除卡片（发起方的还在；被邀请方点「同意」时已移除），进入密聊中
    if (state == secretchat::session_state_active)
    {
        RemoveSecretInviteCard(sessionId);
        EnterSecretActive(sessionId, peerProtect);
        return;
    }
    //对方的防截图级别变化：会话状态不变，卡片与内容都保留，只刷新提示
    if (state == secretchat::session_state_peer_protect)
    {
        if (sessionId == m_secretSessionId && balloonchatwnd::IsSecretState(m_secretState))
        {
            m_secretPeerProtect = peerProtect;
            ApplySecretBanner();
        }
        return;
    }
    //邀请阶段（卡片还在）就结束了：未接受、已撤回、邀请阶段被服务端结束、本机出错。已在其他设备处理时不留提示
    std::map<unsigned long long, SecretInviteCardInfo>::iterator it = m_secretInviteCards.find(sessionId);
    if (it != m_secretInviteCards.end())
    {
        const bool iAmInviter = it->second.m_iAmInviter;
        RemoveSecretInviteCard(sessionId);
        const std::wstring tip = balloonchatwnd::SecretInviteEndTip(state, iAmInviter);
        if (!tip.empty())
        {
            AppendSecretTip(CString(tip.c_str()));
        }
        return;
    }
    //进行中的会话在服务端结束（对方退出、掉线、管理员关闭、服务器维护）或本机出错：本人没有主动退出，转为只能查看
    if (sessionId == m_secretSessionId && m_secretState == balloonchatwnd::kSecretStateActive)
    {
        const int endReason = (state == secretchat::session_state_local_error)
                              ? balloonchatwnd::kSecretEndLocalError : reason;
        EnterSecretReadOnly(endReason);
        return;
    }
    //被邀请方点了「同意」、还没等到「已建立」就失败了（DLL 报「已撤回」）：卡片已移除，告诉用户邀请已失效
    if (m_secretState == balloonchatwnd::kSecretStateNormal && state == secretchat::session_state_canceled)
    {
        AppendSecretTip(CString(balloonchatwnd::SecretInviteEndTip(state, false).c_str()));
    }
}

bool CBalloonChatWindow::SendSecretTextPart(const CString& text)
{
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    if (list == NULL || m_secretState != balloonchatwnd::kSecretStateActive)
    {
        return false;
    }
    //与普通文字消息同一个字符上限（SecretChatApi.h 的 kMaxTextChars）
    if (static_cast<unsigned int>(text.GetLength()) > secretchat::kMaxTextChars)
    {
        ::MessageBox(m_hWnd, TR(balloonchatwnd::kSendTextTooLargeTip), g_strAppTitle.c_str(), MB_OK | MB_ICONWARNING);
        return false;
    }
    secretchat::ISecretChatModule* module = balloonsecret::CSecretChatHost::GetInstance().Module();
    uint32_t msgId = 0;
    if (module == NULL || !module->SendText(m_secretSessionId, (LPCWSTR)text, &msgId))
    {
        ShowToast(balloonchatwnd::SecretSendFailureText(-1).c_str());
        LOG_WARNING("secret text not sent, session=%llu, peer=%u.", m_secretSessionId, m_peerUserId);
        return false;
    }
    //本人的密聊气泡：没有服务器 msgid（不画已读回执、不能撤回），时间取本机当前时刻，用于时间分隔
    const long long nowMs = static_cast<long long>(::time(NULL)) * kMsPerSecond;
    balloonchatwnd::ChatMessage msg = MakeRealTextMessage(true, text, net::TextFont(), 0, nowMs,
                                                          balloonchatwnd::SecretClientMsgId(m_secretSessionId, true, msgId));
    msg.isSecret = true;
    list->AppendMessage(msg);
    list->ScrollToBottom();
    LOG_INFO("secret text sent, session=%llu, peer=%u, msg=%u, chars=%d.", m_secretSessionId, m_peerUserId, msgId,
             text.GetLength());
    return true;
}

void CBalloonChatWindow::OnSecretText(unsigned long long sessionId, unsigned int msgId, const wchar_t* text,
                                      long long serverTimeMs)
{
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    if (list == NULL || text == NULL || sessionId != m_secretSessionId
        || m_secretState != balloonchatwnd::kSecretStateActive)
    {
        LOG_WARNING("secret text dropped, session=%llu, window session=%llu, state=%d, peer=%u.", sessionId,
                    m_secretSessionId, m_secretState, m_peerUserId);
        return;
    }
    balloonchatwnd::ChatMessage msg = MakeRealTextMessage(false, CString(text), net::TextFont(), 0, serverTimeMs,
                                                          balloonchatwnd::SecretClientMsgId(sessionId, false, msgId));
    msg.isSecret = true;
    list->AppendMessage(msg);
    LOG_INFO("secret text shown, session=%llu, peer=%u, msg=%u.", sessionId, m_peerUserId, msgId);
}

void CBalloonChatWindow::OnSecretSendResult(unsigned long long sessionId, unsigned int msgId, int code)
{
    if (code == 0)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    if (list != NULL)
    {
        list->MarkSendFailed(balloonchatwnd::SecretClientMsgId(sessionId, true, msgId),
                             CString(balloonchatwnd::SecretSendFailureText(code).c_str()));
    }
    LOG_WARNING("secret text send failed, session=%llu, peer=%u, msg=%u, code=%d.", sessionId, m_peerUserId, msgId,
                code);
}

bool CBalloonChatWindow::SendSecretImagePart(int imageIndex)
{
    if (imageIndex < 0 || (size_t)imageIndex >= m_pendingShotPaths.size())
    {
        LOG_WARNING("secret image part index out of range, index=%d, peer=%u.", imageIndex, m_peerUserId);
        return false;
    }
    return SendSecretImageFile(m_pendingShotPaths[imageIndex]);
}

bool CBalloonChatWindow::SendSecretImageFile(const std::wstring& path)
{
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    if (list == NULL || m_secretState != balloonchatwnd::kSecretStateActive)
    {
        return false;
    }
    //先看大小：超过 10 MB 的不读入（docs/密聊方案.md 4.10，原图发送、不压缩）
    WIN32_FILE_ATTRIBUTE_DATA attr;
    if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attr))
    {
        ShowToast(balloonchatwnd::SecretSendFailureText(-1).c_str());
        LOG_WARNING("secret image file not readable, peer=%u.", m_peerUserId);
        return false;
    }
    const unsigned long long fileBytes =
        (static_cast<unsigned long long>(attr.nFileSizeHigh) << 32) | attr.nFileSizeLow;
    if (!balloonchatwnd::IsSecretImageSizeAcceptable(fileBytes))
    {
        ::MessageBox(m_hWnd, balloonchatwnd::SecretImageTooLargeTip().c_str(), g_strAppTitle.c_str(),
                     MB_OK | MB_ICONWARNING);
        return false;
    }
    //整个文件读入内存：之后只交给密聊模块与内存图源，不另写任何文件
    std::vector<unsigned char> bytes(static_cast<size_t>(fileBytes));
    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD readBytes = 0;
    const bool readOk = (file != INVALID_HANDLE_VALUE)
                        && ::ReadFile(file, &bytes[0], static_cast<DWORD>(bytes.size()), &readBytes, NULL)
                        && readBytes == bytes.size();
    if (file != INVALID_HANDLE_VALUE)
    {
        ::CloseHandle(file);
    }
    if (!readOk)
    {
        ShowToast(balloonchatwnd::SecretSendFailureText(-1).c_str());
        LOG_WARNING("secret image read failed, peer=%u, bytes=%llu.", m_peerUserId, fileBytes);
        return false;
    }
    //宽高：给接收方先画占位气泡用；解不开的图片不发
    int realW = 0;
    int realH = 0;
    {
        Gdiplus::Image img(path.c_str());
        if (img.GetLastStatus() == Gdiplus::Ok)
        {
            realW = static_cast<int>(img.GetWidth());
            realH = static_cast<int>(img.GetHeight());
        }
    }
    if (realW <= 0 || realH <= 0)
    {
        ::SecureZeroMemory(&bytes[0], bytes.size());
        ShowToast(balloonchatwnd::SecretSendFailureText(-1).c_str());
        LOG_WARNING("secret image not decodable, peer=%u.", m_peerUserId);
        return false;
    }
    secretchat::ISecretChatModule* module = balloonsecret::CSecretChatHost::GetInstance().Module();
    uint32_t imageId = 0;
    if (module == NULL
        || !module->SendImage(m_secretSessionId, &bytes[0], static_cast<uint32_t>(bytes.size()), (uint32_t)realW,
                              (uint32_t)realH, &imageId))
    {
        ::SecureZeroMemory(&bytes[0], bytes.size());
        ShowToast(balloonchatwnd::SecretSendFailureText(-1).c_str());
        LOG_WARNING("secret image not sent, session=%llu, peer=%u.", m_secretSessionId, m_peerUserId);
        return false;
    }
    //本人的气泡显示同一份字节：登记为内存图片，气泡以伪路径作 imagePath
    const std::wstring memPath = balloonchatwnd::SecretImageMemoryPath(m_peerUserId, m_secretSessionId, true, imageId);
    balloonchatwnd::ChatImageCache::Inst().PutMemoryImage(memPath, &bytes[0], bytes.size());
    ::SecureZeroMemory(&bytes[0], bytes.size());

    int dispW = 0;
    int dispH = 0;
    SecretImageDisplaySize(realW, realH, dispW, dispH);
    balloonchatwnd::ChatMessage msg = balloonchatwnd::MakeImageMessage(
        true, CString(memPath.c_str()), dispW, dispH,
        balloonchatwnd::SecretImageClientMsgId(m_secretSessionId, true, imageId), 1 /*上传中*/);
    FillSenderAvatar(msg, true);
    msg.isSecret = true;
    msg.timestampMs = static_cast<long long>(::time(NULL)) * kMsPerSecond;
    list->AppendMessage(msg);
    list->ScrollToBottom();
    LOG_INFO("secret image sending, session=%llu, peer=%u, image=%u, bytes=%llu, size=%dx%d.", m_secretSessionId,
             m_peerUserId, imageId, fileBytes, realW, realH);
    return true;
}

void CBalloonChatWindow::OnSecretImageSendProgress(unsigned long long sessionId, unsigned int imageId,
                                                   unsigned int sentBytes, unsigned int totalBytes, int code)
{
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    if (list == NULL)
    {
        return;
    }
    const std::string clientMsgId = balloonchatwnd::SecretImageClientMsgId(sessionId, true, imageId);
    if (code != 0)
    {
        list->MarkImageUploadFailed(clientMsgId);
        LOG_WARNING("secret image send failed, session=%llu, peer=%u, image=%u, code=%d.", sessionId, m_peerUserId,
                    imageId, code);
        return;
    }
    if (totalBytes > 0 && sentBytes >= totalBytes)
    {
        list->MarkImageUploadDone(clientMsgId);
        LOG_INFO("secret image sent, session=%llu, peer=%u, image=%u.", sessionId, m_peerUserId, imageId);
        return;
    }
    const int kPercentMax = 100;
    const int percent = (totalBytes > 0) ? (int)((unsigned long long)sentBytes * kPercentMax / totalBytes) : 0;
    list->UpdateImageUploadProgress(clientMsgId, percent);
}

void CBalloonChatWindow::OnSecretImageBegin(unsigned long long sessionId, unsigned int imageId, unsigned int totalBytes,
                                            unsigned int width, unsigned int height, long long serverTimeMs)
{
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    if (list == NULL || sessionId != m_secretSessionId || m_secretState != balloonchatwnd::kSecretStateActive)
    {
        LOG_WARNING("secret image begin dropped, session=%llu, window session=%llu, peer=%u.", sessionId,
                    m_secretSessionId, m_peerUserId);
        return;
    }
    int dispW = 0;
    int dispH = 0;
    SecretImageDisplaySize((int)width, (int)height, dispW, dispH);
    //占位气泡：下载中（没有图片路径），收齐后按 clientMsgId 找回本行换成内存图片
    balloonchatwnd::ChatMessage msg = balloonchatwnd::MakeImageMessage(
        false, CString(), dispW, dispH, balloonchatwnd::SecretImageClientMsgId(sessionId, false, imageId),
        balloonchatwnd::kChatImageStateDownloading);
    FillSenderAvatar(msg, false);
    msg.isSecret = true;
    msg.timestampMs = serverTimeMs;
    list->AppendMessage(msg);
    m_secretRecvImageBytes[imageId] = totalBytes;
    LOG_INFO("secret image receiving, session=%llu, peer=%u, image=%u, bytes=%u.", sessionId, m_peerUserId, imageId,
             totalBytes);
}

void CBalloonChatWindow::OnSecretImageProgress(unsigned long long sessionId, unsigned int imageId,
                                               unsigned int receivedBytes)
{
    std::map<unsigned int, unsigned int>::const_iterator it = m_secretRecvImageBytes.find(imageId);
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    if (list == NULL || it == m_secretRecvImageBytes.end() || it->second == 0 || sessionId != m_secretSessionId)
    {
        return;
    }
    const std::string clientMsgId = balloonchatwnd::SecretImageClientMsgId(sessionId, false, imageId);
    balloonchatwnd::ChatMessage msg;
    if (!list->GetMessageByClientMsgId(clientMsgId, msg))
    {
        return;
    }
    const int kPercentMax = 100;
    const int percent = (int)((unsigned long long)receivedBytes * kPercentMax / it->second);
    if (percent == msg.imageProgress)
    {
        return;
    }
    msg.imageProgress = (percent > kPercentMax) ? kPercentMax : percent;
    list->SetMessageByClientMsgId(clientMsgId, msg);
}

void CBalloonChatWindow::OnSecretImage(unsigned long long sessionId, unsigned int imageId, const void* bytes,
                                       unsigned int len, bool ok)
{
    m_secretRecvImageBytes.erase(imageId);
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    const std::string clientMsgId = balloonchatwnd::SecretImageClientMsgId(sessionId, false, imageId);
    balloonchatwnd::ChatMessage msg;
    if (list == NULL || !list->GetMessageByClientMsgId(clientMsgId, msg))
    {
        LOG_WARNING("secret image dropped, no placeholder, session=%llu, peer=%u, image=%u.", sessionId, m_peerUserId,
                    imageId);
        return;
    }
    if (!ok || bytes == NULL || len == 0)
    {
        msg.imageState = balloonchatwnd::kChatImageStateDownloadFailed;
        list->SetMessageByClientMsgId(clientMsgId, msg);
        LOG_WARNING("secret image receive failed, session=%llu, peer=%u, image=%u.", sessionId, m_peerUserId, imageId);
        return;
    }
    //字节只登记在内存图源里（伪路径 secretmem://），不落盘；退出密聊时按前缀清零删除
    const std::wstring memPath = balloonchatwnd::SecretImageMemoryPath(m_peerUserId, sessionId, false, imageId);
    balloonchatwnd::ChatImageCache::Inst().PutMemoryImage(memPath, bytes, len);
    msg.imagePath = memPath.c_str();
    msg.imageState = balloonchatwnd::kChatImageStateReady;
    msg.imageProgress = 0;
    list->SetMessageByClientMsgId(clientMsgId, msg);
    LOG_INFO("secret image shown, session=%llu, peer=%u, image=%u, bytes=%u.", sessionId, m_peerUserId, imageId, len);
}

void CBalloonChatWindow::AppendSecretTip(const CString& text)
{
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    if (list == NULL || text.IsEmpty())
    {
        return;
    }
    //与 AppendSystemTip 相同的居中提示行；AppendSystemTip 在密聊状态下不插入普通提示，密聊自己的提示走这里
    balloonchatwnd::ChatMessage tip = balloonchatwnd::MakeTextMessage(false, text);
    tip.kind = balloonchatwnd::kMsgTime;
    list->AppendMessage(tip);
}

void CBalloonChatWindow::EnterSecretActive(unsigned long long sessionId, int peerProtect)
{
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    const bool fromReadOnly = (m_secretState == balloonchatwnd::kSecretStateReadOnly);
    //从普通状态进入：列表里只显示密聊消息，先把普通历史拿掉（退出密聊时重新拉取）。打开列表的密聊限制：
    //没有右键菜单、不响应 Ctrl+C、不能多选（正在多选时立即退出）、不能拖出
    if (list != NULL)
    {
        list->SetSecretRestricted(true);
        if (!fromReadOnly)
        {
            list->ClearAllMessages();
        }
    }
    m_secretSessionId = sessionId;
    m_secretState = balloonchatwnd::kSecretStateActive;
    m_secretEndReason = 0;
    m_secretPeerProtect = peerProtect;
    m_secretExitTipPending = false;
    //防截图：从只能查看回来时已经打开着，重新设置一次以取得当前实际达到的级别
    m_secretMyProtect = balloonsecret::CSecretChatHost::GetInstance().ProtectWindow(m_hWnd, true);
    //密聊中不发「正在输入」，正在显示的引用条也收起（引用的是普通消息）
    StopTypingNotify();
    CancelPendingReply();
    ApplySecretUi();
    LOG_INFO("secret mode entered, session=%llu, peer=%u, from read-only=%d, my protect=%d, peer protect=%d.",
             sessionId, m_peerUserId, fromReadOnly ? 1 : 0, m_secretMyProtect, peerProtect);
}

void CBalloonChatWindow::EnterSecretReadOnly(int endReason)
{
    if (!balloonchatwnd::IsSecretState(m_secretState))
    {
        return;
    }
    m_secretState = balloonchatwnd::kSecretStateReadOnly;
    m_secretEndReason = endReason;
    //内容保留、防截图保持；会话密钥已由模块清零
    ApplySecretUi();
    LOG_INFO("secret mode read-only, session=%llu, peer=%u, reason=%d.", m_secretSessionId, m_peerUserId, endReason);
}

void CBalloonChatWindow::ExitSecretMode(bool closeSession, bool reloadHistory)
{
    if (!balloonchatwnd::IsSecretState(m_secretState))
    {
        return;
    }
    balloonsecret::CSecretChatHost& host = balloonsecret::CSecretChatHost::GetInstance();
    //只能查看时会话已在服务端结束，模块里也没有它了，Close 返回 false，宿主照样删掉登记
    if (closeSession && m_secretSessionId != 0)
    {
        host.Close(m_secretSessionId);
    }
    if (IsWindow())
    {
        host.ProtectWindow(m_hWnd, false);
    }
    const unsigned long long sessionId = m_secretSessionId;
    //正在看的大图先关掉（释放浮层里解码出的位图）
    if (m_secretImageOverlay != NULL)
    {
        m_secretImageOverlay->Close();
    }
    //清除内容：文字先在内存里清零再移除，图片字节与解码出的位图由图片缓存按本会话的前缀清零后删除
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    size_t wiped = 0;
    if (list != NULL)
    {
        wiped = list->WipeSecretMessages();
        list->ClearAllMessages();
        list->SetSecretRestricted(false);
    }
    const size_t images = balloonchatwnd::ChatImageCache::Inst().RemoveMemoryImages(
        balloonchatwnd::SecretImageMemoryPrefix(m_peerUserId));
    LOG_INFO("secret content wiped, session=%llu, peer=%u, rows=%u, images=%u.", sessionId, m_peerUserId,
             (unsigned)wiped, (unsigned)images);
    m_secretState = balloonchatwnd::kSecretStateNormal;
    m_secretSessionId = 0;
    m_secretEndReason = 0;
    m_secretMyProtect = 0;
    m_secretPeerProtect = 0;
    //恢复普通状态的界面与普通历史：首屏渲染完插入「你已退出密聊」提示（这条提示不落库，关窗即消失）。
    //关窗、销毁时窗口马上就不在了，不刷新界面也不拉历史
    if (reloadHistory && IsWindow())
    {
        ApplySecretUi();
        m_secretExitTipPending = true;
        ReloadLatestMessages();
    }
    LOG_INFO("secret mode exited, session=%llu, peer=%u, close=%d, reload=%d.", sessionId, m_peerUserId,
             closeSession ? 1 : 0, reloadHistory ? 1 : 0);
}

void CBalloonChatWindow::ApplySecretUi()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    const int state = m_secretState;
    const bool secret = balloonchatwnd::IsSecretState(state);

    //受管的头部与工具栏按钮：密聊状态下按 IsSecretCtrlAvailable 设显隐；回到普通状态时先全部显示，再由各自的
    //显隐函数按模块是否可用收起（与建窗时同一套判断）
    size_t count = 0;
    const unsigned int* ids = balloonchatwnd::SecretManagedCtrlIds(count);
    for (size_t i = 0; i < count; ++i)
    {
        balloonwjui::DuiControl* ctrl = content->FindCtrlById(ids[i]);
        if (ctrl != NULL)
        {
            ctrl->SetVisible(secret ? balloonchatwnd::IsSecretCtrlAvailable(state, ids[i]) : true);
        }
    }
    if (!secret)
    {
        balloonchatwnd::ApplyMarkdownEditorButton(content, balloonchatwnd::kIdTbMarkdown);
        //截图的剪刀与 ▾：退出密聊恢复显示之后，截图模块不可用时仍要隐藏（docs/截图独立模块方案.md 3.3 节）
        balloonchatwnd::ApplyScreenshotButtons(content, balloonchatwnd::kIdTbScreenshot, balloonchatwnd::kIdTbScreenshotMenu,
                                               balloonscreenshot::IsScreenshotAvailable());
        ApplyShareButtonVisibility();
        ApplyScreenShareButtonVisibility();
        ApplyVoiceButtonVisibility();
    }
    ApplySecretEntryVisibility();
    //SetVisible 不会重排：对头部按钮行与工具栏各补一次布局（UI 约定第 5 条）
    const UINT rowAnchors[] = { balloonchatwnd::kIdHdrVoice, balloonchatwnd::kIdTbEmoji };
    for (size_t i = 0; i < sizeof(rowAnchors) / sizeof(rowAnchors[0]); ++i)
    {
        balloonwjui::DuiControl* anchor = content->FindCtrlById(rowAnchors[i]);
        balloonwjui::DuiControl* row = (anchor != NULL) ? anchor->GetParent() : NULL;
        if (row != NULL)
        {
            row->ForceLayout(row->GetRect());
        }
    }

    //输入框：只能查看时只读；发送按钮还要看是否在线
    balloonchatwnd::ChatInputRichEdit* input =
        dynamic_cast<balloonchatwnd::ChatInputRichEdit*>(content->FindCtrlById(balloonchatwnd::kIdInputEdit));
    if (input != NULL)
    {
        input->SetReadOnly(!balloonchatwnd::IsSecretCtrlAvailable(state, balloonchatwnd::kIdInputEdit));
    }
    balloonwjui::DuiControl* sendBtn = content->FindCtrlById(balloonchatwnd::kIdSendButton);
    if (sendBtn != NULL)
    {
        sendBtn->SetEnabled(m_connUiState == (int)balloonmain::kConnUiOnline
                            && balloonchatwnd::IsSecretCtrlAvailable(state, balloonchatwnd::kIdSendButton));
    }

    ApplySecretBanner();
    RefreshPinBanner();

    //标题：密聊状态下加「（密聊）」后缀
    SetTitle(secret ? CString(balloonchatwnd::SecretWindowTitle((LPCWSTR)m_windowTitle).c_str()) : m_windowTitle);
    InvalidateRect(NULL, FALSE);
}

void CBalloonChatWindow::ApplySecretBanner()
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonwjui::DuiControl* banner = (content != NULL) ? content->FindCtrlById(balloonchatwnd::kIdSecretBanner) : NULL;
    if (banner == NULL)
    {
        return;
    }
    const int state = m_secretState;
    const bool secret = balloonchatwnd::IsSecretState(state);
    balloonwjui::DuiLabel* text = dynamic_cast<balloonwjui::DuiLabel*>(content->FindCtrlById(balloonchatwnd::kIdSecretBannerText));
    if (text != NULL)
    {
        text->SetText(balloonchatwnd::SecretBannerText(state, m_secretEndReason).c_str());
    }
    //防截图提示：任一方做不到时显示，头部条加高一行
    const std::wstring warnText = secret ? balloonchatwnd::SecretProtectWarning(m_secretMyProtect, m_secretPeerProtect)
                                         : std::wstring();
    balloonwjui::DuiLabel* warn = dynamic_cast<balloonwjui::DuiLabel*>(content->FindCtrlById(balloonchatwnd::kIdSecretBannerWarn));
    if (warn != NULL)
    {
        warn->SetText(warnText.c_str());
        warn->SetVisible(!warnText.empty());
    }
    //三个链接：安全码只在密聊中，重新邀请只在只能查看且入口可用时，退出密聊在两种密聊状态下都有
    const bool entryAvailable = balloonsecret::CSecretChatHost::GetInstance().IsEntryAvailable();
    const UINT linkIds[] = { balloonchatwnd::kIdSecretSafety, balloonchatwnd::kIdSecretReinvite,
                             balloonchatwnd::kIdSecretExit };
    const bool linkShows[] = { balloonchatwnd::ShowSecretSafetyCode(state),
                               balloonchatwnd::ShowSecretReinvite(state, entryAvailable), secret };
    for (size_t i = 0; i < sizeof(linkIds) / sizeof(linkIds[0]); ++i)
    {
        balloonwjui::DuiControl* link = content->FindCtrlById(linkIds[i]);
        if (link != NULL)
        {
            link->SetVisible(linkShows[i]);
        }
    }
    //头部条的高度：一行状态文字，有防截图提示时再加一行（两个高度与 ChatWindowLayout.cpp 里的 XML 一致）
    balloonwjui::DuiLayout* root = dynamic_cast<balloonwjui::DuiLayout*>(banner->GetParent());
    if (root != NULL)
    {
        balloonwjui::DuiLayout::Hint hint = root->GetHint(banner);
        hint.fixedMain = warnText.empty() ? kSecretBannerRowPx : (kSecretBannerRowPx + kSecretBannerWarnPx);
        root->SetHint(banner, hint);
    }
    banner->SetVisible(secret);
    //显隐与高度变化都要重排：头部条是客户区根容器的直接子控件，对根容器补一次布局；
    //它自己的子行（链接显隐）也补一次
    if (root != NULL)
    {
        root->ForceLayout(root->GetRect());
    }
    banner->ForceLayout(banner->GetRect());
}

void CBalloonChatWindow::ShowSecretSafetyCode()
{
    std::wstring code;
    if (m_secretState != balloonchatwnd::kSecretStateActive
        || !balloonsecret::CSecretChatHost::GetInstance().GetSafetyCode(m_secretSessionId, code))
    {
        ShowToast(TR(L"当前没有进行中的密聊，无法查看安全码"));
        return;
    }
    const std::wstring message =
        TRF(L"安全码：{0}\n\n请当面或通过电话与对方核对。双方看到的安全码一致，说明密聊没有被第三方截取。", code);
    ::MessageBox(m_hWnd, message.c_str(), g_strAppTitle.c_str(), MB_OK | MB_ICONINFORMATION);
}

LRESULT CBalloonChatWindow::OnChatSecretInviteAction(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
{
    unsigned long long* pSessionId = reinterpret_cast<unsigned long long*>(lParam);
    if (pSessionId == NULL)
    {
        return 0;
    }
    const unsigned long long sessionId = *pSessionId;
    delete pSessionId;

    balloonsecret::CSecretChatHost& host = balloonsecret::CSecretChatHost::GetInstance();
    switch ((int)wParam)
    {
    //被邀请方点了「同意」：离线时提示并保留卡片；模块受理后移除卡片，等服务端推来「已建立」进入密聊状态
    case balloonchatwnd::kSecretInviteAccept:
        if (!CheckOnlineFor(balloonmain::kActionRealtimeCall))
        {
            break;
        }
        if (host.Respond(sessionId, true))
        {
            RemoveSecretInviteCard(sessionId);
            LOG_INFO("secret invite accepted, session=%llu, peer=%u.", sessionId, m_peerUserId);
        }
        else
        {
            //会话已不在了（过期、被撤回）
            RemoveSecretInviteCard(sessionId);
            AppendSecretTip(CString(balloonchatwnd::SecretInviteEndTip(secret_state_ended, false).c_str()));
            LOG_WARNING("secret invite accept rejected locally, session=%llu, peer=%u.", sessionId, m_peerUserId);
        }
        break;

    //被邀请方点了「忽略」：卡片立即消失，发起方看到的提示与超时相同（docs/密聊方案.md 4.2）
    case balloonchatwnd::kSecretInviteIgnore:
        host.Respond(sessionId, false);
        RemoveSecretInviteCard(sessionId);
        LOG_INFO("secret invite ignored, session=%llu, peer=%u.", sessionId, m_peerUserId);
        break;

    //发起方点了「取消」：撤回邀请
    case balloonchatwnd::kSecretInviteCancel:
        host.Cancel(sessionId);
        RemoveSecretInviteCard(sessionId);
        LOG_INFO("secret invite canceled, session=%llu, peer=%u.", sessionId, m_peerUserId);
        break;

    //不认识的动作：只记日志
    default:
        LOG_WARNING("unknown secret invite action %d, session=%llu.", (int)wParam, sessionId);
        break;
    }
    return 0;
}

void CBalloonChatWindow::AddSecretInviteCard(unsigned long long sessionId, bool iAmInviter, unsigned int remainMs,
                                             int peerProtect)
{
    if (sessionId == 0)
    {
        return;
    }
    SecretInviteCardInfo& info = m_secretInviteCards[sessionId];
    info.m_deadlineTick = ::GetTickCount64() + remainMs;
    info.m_iAmInviter = iAmInviter;
    info.m_peerProtect = peerProtect;
    InsertSecretInviteCardRow(sessionId, info);
    if (IsWindow())
    {
        ::SetTimer(m_hWnd, kSecretInviteTimerId, kSecretInviteTickMs, NULL);
    }
}

void CBalloonChatWindow::InsertSecretInviteCardRow(unsigned long long sessionId, const SecretInviteCardInfo& info)
{
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    if (list == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessage existing;
    if (list->GetSecretInviteMessage(sessionId, existing))
    {
        return;
    }
    //对方名字：打开方没有传入时用「用户<id>」，与窗口标题的回退方式相同
    CString peerName = m_peerName;
    if (peerName.IsEmpty())
    {
        peerName.Format(TR(L"用户%u"), m_peerUserId);
    }
    const unsigned long long remainMs = balloonsecret::SecretInviteRemainMs(info.m_deadlineTick, ::GetTickCount64());
    balloonchatwnd::ChatMessage msg;
    FillSenderAvatar(msg, info.m_iAmInviter);
    balloonchatwnd::ApplySecretInviteCard(msg, info.m_iAmInviter, peerName, sessionId,
                                          balloonsecret::SecretInviteRemainSeconds(remainMs), info.m_peerProtect);
    list->AppendMessage(msg);
    list->ScrollToBottom();
}

void CBalloonChatWindow::RemoveSecretInviteCard(unsigned long long sessionId)
{
    m_secretInviteCards.erase(sessionId);
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    if (list != NULL)
    {
        list->RemoveSecretInviteMessage(sessionId);
    }
    if (m_secretInviteCards.empty() && IsWindow())
    {
        ::KillTimer(m_hWnd, kSecretInviteTimerId);
    }
}

void CBalloonChatWindow::RestoreSecretInviteCards()
{
    if (m_peerUserId == 0)
    {
        return;
    }
    //对方发来、还在倒计时内的邀请：窗口没开时到达的，这里第一次登记
    balloonsecret::PendingSecretInvite invite;
    if (balloonsecret::CSecretChatHost::GetInstance().GetIncomingInvite((int64_t)m_peerUserId, invite)
        && m_secretInviteCards.find(invite.m_sessionId) == m_secretInviteCards.end())
    {
        const unsigned long long remainMs = balloonsecret::SecretInviteRemainMs(invite.m_deadlineTick,
                                                                                ::GetTickCount64());
        AddSecretInviteCard(invite.m_sessionId, false, (unsigned int)remainMs, invite.m_peerProtect);
        LOG_INFO("secret invite card restored, session=%llu, peer=%u, remain=%llu ms.", invite.m_sessionId,
                 m_peerUserId, remainMs);
    }
    //登记了却不在列表里的卡片（首屏历史渲染清空了列表）重新插入；已在列表里的不重复插入
    for (std::map<unsigned long long, SecretInviteCardInfo>::const_iterator it = m_secretInviteCards.begin();
         it != m_secretInviteCards.end(); ++it)
    {
        InsertSecretInviteCardRow(it->first, it->second);
    }
}

void CBalloonChatWindow::OnSecretInviteTick()
{
    const unsigned long long now = ::GetTickCount64();
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    std::vector<unsigned long long> overdue;
    for (std::map<unsigned long long, SecretInviteCardInfo>::const_iterator it = m_secretInviteCards.begin();
         it != m_secretInviteCards.end(); ++it)
    {
        const unsigned long long remainMs = balloonsecret::SecretInviteRemainMs(it->second.m_deadlineTick, now);
        if (remainMs == 0 && now >= it->second.m_deadlineTick + kSecretInviteGraceMs)
        {
            overdue.push_back(it->first);
            continue;
        }
        balloonchatwnd::ChatMessage msg;
        if (list != NULL && list->GetSecretInviteMessage(it->first, msg))
        {
            balloonchatwnd::UpdateSecretInviteRemain(msg, balloonsecret::SecretInviteRemainSeconds(remainMs));
            list->SetSecretInviteMessage(it->first, msg, false);
        }
    }
    //过期太久仍没有结果：本机撤回（发起方）或忽略（被邀请方），让模块与服务端都结束这条邀请，再按「未接受」收尾
    balloonsecret::CSecretChatHost& host = balloonsecret::CSecretChatHost::GetInstance();
    for (size_t i = 0; i < overdue.size(); ++i)
    {
        const unsigned long long sessionId = overdue[i];
        const bool iAmInviter = m_secretInviteCards[sessionId].m_iAmInviter;
        if (iAmInviter)
        {
            host.Cancel(sessionId);
        }
        else
        {
            host.Respond(sessionId, false);
        }
        RemoveSecretInviteCard(sessionId);
        AppendSecretTip(CString(balloonchatwnd::SecretInviteEndTip(secret_state_not_accepted, iAmInviter).c_str()));
        LOG_WARNING("secret invite overdue without server result, closed locally, session=%llu, peer=%u.", sessionId,
                    m_peerUserId);
    }
}
