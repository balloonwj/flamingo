/**
 *  BalloonChatWindowDirectFile.cpp
 *
 *  单聊窗里「单聊单文件 P2P 直传」的那部分（2026-10-04，docs/单聊文件P2P直传与P2P徽章方案.md 第 3 节），从
 *  BalloonChatWindow.cpp 分出来单独成文件：
 *    · 发文件时先试在线直传（对方没被确知离线、本进程登记了直传的 P2P 业务方），不行就照旧上传服务器；
 *    · 直传复用文件夹传输的信令与事件通路，本文件把那几种事件（邀请应答、状态推送、开始传输、进度、结果）落到
 *      「在线直传的文件气泡」上，处置规则来自 DirectFileTransfer.h、气泡怎么变来自 ChatDirectFileModel.h；
 *    · 文件气泡上的按钮（发送离线文件、取消、接收、拒绝、重新发送）交给文件夹传输管理器；
 *    · 服务端为一次直传留下的文件消息到达时接管会话里那条实时气泡。
 *  事件不属于本会话的直传气泡时，各函数返回 false，调用方照旧按文件夹传输处理。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "BalloonChatWindow.h"

#include "ChatMessageList.h"
#include "ChatMockData.h"
#include "ChatFileAction.h"
#include "ChatFileStateModel.h"
#include "ChatDirectFileModel.h"
#include "ChatSendLimit.h"                                       //FileSendFailureText：发送失败原因的文案
#include "../../Feature/FolderTransfer/FolderTransferManager.h"
#include "../../Feature/FolderTransfer/DirectFileTransfer.h"
#include "../../Feature/P2P/FolderP2PRegistration.h"            //IsDirectFileP2PRegistered
#include "../../net/FileTransfer.h"
#include "../../net/FolderProtocolClient.h"
#include "../../net/MessageProtocolClient.h"
#include "../../net/CurrentUserAccount.h"
#include "../../net/UserCachePath.h"
#include "../../net/Protocol.h"
#include "../../Session/UserSessionData.h"                       //g_szDataRoot
#include "../../Common/Log/IULog.h"
#include "../../Common/I18n/I18n.h"

namespace {

//单聊的会话类型（FileTransfer::EnqueueSend 的 ctype）
const int kDirectFileChatTypeSingle = 1;

//百分比的满值
const int kDirectFileFullPercent = 100;

//在线直传的接收端落地的缓存子目录：与普通下载缓存同一个 ChatFile，文件放在其下 direct-<会话号>\ 里
const wchar_t* const kDirectFileCacheSubDir = L"ChatFile";

//把直传处置对文件气泡的变化写进消息
void ApplyDirectChange(balloonchatwnd::ChatMessage& msg, const balloonchatwnd::DirectBubbleChange& c)
{
    if (c.m_fileState != balloonchatwnd::kDirectKeepState)
        msg.fileState = c.m_fileState;
    msg.fileDirect = c.m_direct;
    if (c.m_peerState != balloonchatwnd::kDirectKeepState)
        msg.filePeerState = c.m_peerState;
    if (c.m_directEnd != 0)
        msg.fileDirectEnd = c.m_directEnd;
}

//进度百分比（0~100）；总数为 0 时按 0
int DirectFilePercent(int64_t done, int64_t total)
{
    if (total <= 0)
        return 0;
    const int64_t pct = done * kDirectFileFullPercent / total;
    return (int)(pct > kDirectFileFullPercent ? kDirectFileFullPercent : pct);
}

} // namespace

bool CBalloonChatWindow::TrySendFileDirect(const std::wstring& path, const CString& fileName, long long sizeBytes,
                                           const std::string& clientMsgId, bool peerKnownOffline, bool forceOffline)
{
    //对方确知离线、本进程没有登记直传的业务方（P2P 模块没加载、诊断开关关掉了）、调用方要求按离线文件上传：照旧上传服务器
    if (m_peerUserId == 0
        || !foldertransfer::ShouldTryDirectFileSend(peerKnownOffline, balloonp2p::IsDirectFileP2PRegistered(), forceOffline))
        return false;
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    if (list == NULL)
        return false;

    balloonchatwnd::ChatMessage msg = balloonchatwnd::MakeFileMessage(
        true, fileName, sizeBytes, CString(path.c_str()), clientMsgId, balloonchatwnd::kFileDirectWaiting);
    msg.fileDirect = true;
    FillSenderAvatar(msg, true);
    list->AppendMessage(msg);

    const int verdict = foldertransfer::FolderTransferManager::GetInstance().StartSendFile(
        (int64_t)m_peerUserId, path, clientMsgId);
    if (verdict != foldertransfer::kFolderStartOk)
    {
        //邀请发不出去（文件刚被移走等）：就地改为上传服务器，由上传那条路报告结果
        LOG_WARNING("Direct file send: start refused, verdict=%d, clientmsgid=%s, fall back to upload.", verdict,
                    clientMsgId.c_str());
        StartDirectFileOfflineUpload(clientMsgId);
        return true;
    }
    LOG_INFO("Direct file send: invite started, to=%u, clientmsgid=%s, size=%lld.", m_peerUserId, clientMsgId.c_str(),
             sizeBytes);
    return true;
}

void CBalloonChatWindow::StartDirectFileOfflineUpload(const std::string& key)
{
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    balloonchatwnd::ChatMessage msg;
    if (list == NULL || !list->GetDirectFileMessage(key, msg))
        return;
    //只有本端发出的那条才有本地文件与消息号可上传
    if (!msg.fromMe || msg.fileClientMsgId.empty() || msg.fileLocalPath.IsEmpty())
    {
        LOG_WARNING("Direct file: cannot fall back to upload, key=%s, fromme=%d.", key.c_str(), msg.fromMe ? 1 : 0);
        return;
    }

    //气泡从此是一条普通的「上传中」文件消息，后续进度与结果按 clientMsgId 走上传那条路
    const std::string clientMsgId = msg.fileClientMsgId;
    msg.fileDirect = false;
    msg.fileState = balloonchatwnd::kFileUploading;
    msg.fileProgress = 0;
    msg.filePeerState = balloonchatwnd::kFilePeerNone;
    msg.fileLinkPath = 0;
    list->SetDirectFileMessage(key, msg);

    net::FileTransfer::GetInstance().EnqueueSend(kDirectFileChatTypeSingle, (int64_t)m_peerUserId, clientMsgId,
                                                 std::wstring((LPCWSTR)msg.fileLocalPath));

    //与普通发文件一样登记待冒泡：发送回执成功后才把会话冒泡到顶部（docs/敏感词方案.md 8.5）
    ConvSendBumpData bump;
    bump.ctype   = kDirectFileChatTypeSingle;
    bump.cid     = (long long)m_peerUserId;
    bump.msgType = message_type_file;
    bump.digest  = net::MessageProtocolClient::digestForMsgType(message_type_file, std::string());
    m_sendBumps.Add(clientMsgId, bump);

    LOG_INFO("Direct file: switched to offline upload, to=%u, clientmsgid=%s.", m_peerUserId, clientMsgId.c_str());
}

bool CBalloonChatWindow::OnDirectFileBound(const foldertransfer::FolderBindNotice& bind)
{
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    balloonchatwnd::ChatMessage msg;
    if (list == NULL || !list->GetDirectFileMessage(bind.m_localId, msg))
        return false;

    if (bind.m_code == error_code_ok)
    {
        //记下服务端分配的会话 id，此后的状态、进度、结果都按它对回这条气泡
        msg.fileDirectSessionId = bind.m_sessionId;
        list->SetDirectFileMessage(bind.m_localId, msg);
        LOG_INFO("Direct file send: bound to session %s, clientmsgid=%s.", bind.m_sessionId.c_str(),
                 bind.m_localId.c_str());
        return true;
    }

    const int action = foldertransfer::DecideDirectSendOnInviteResponse(bind.m_code);
    LOG_INFO("Direct file send: invite refused, code=%d, action=%d, clientmsgid=%s.", bind.m_code, action,
             bind.m_localId.c_str());
    if (action == foldertransfer::kDirectSendToOffline)
    {
        StartDirectFileOfflineUpload(bind.m_localId);
        return true;
    }

    //文件名含敏感词：离线发送同样会被拒，气泡转失败态并在下方写原因
    ApplyDirectChange(msg, balloonchatwnd::SenderBubbleForAction(action));
    list->SetDirectFileMessage(bind.m_localId, msg);
    list->MarkSendFailed(bind.m_localId, CString(balloonchatwnd::FileSendFailureText(bind.m_code).c_str()));
    return true;
}

bool CBalloonChatWindow::OnDirectFileState(const net::FolderStateNotify& notify)
{
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    balloonchatwnd::ChatMessage msg;
    if (list == NULL || !list->GetDirectFileMessage(notify.m_sessionId, msg))
        return false;

    if (msg.fromMe)
    {
        const int action = foldertransfer::DecideDirectSendOnState(notify.m_state, notify.m_errCode);
        if (action == foldertransfer::kDirectSendToOffline)
        {
            //对方 5 分钟没接收、离线、或建不起 P2P 链路：改为上传服务器发离线文件
            LOG_INFO("Direct file send: state %d (errcode %d), switch to offline upload, session=%s.", notify.m_state,
                     notify.m_errCode, notify.m_sessionId.c_str());
            StartDirectFileOfflineUpload(notify.m_sessionId);
            return true;
        }
        ApplyDirectChange(msg, balloonchatwnd::SenderBubbleForAction(action));
    }
    else
    {
        const int action = foldertransfer::DecideDirectRecvOnState(notify.m_state, notify.m_errCode);
        ApplyDirectChange(msg, balloonchatwnd::ReceiverBubbleForAction(action, notify.m_state));
    }
    list->SetDirectFileMessage(notify.m_sessionId, msg);
    return true;
}

bool CBalloonChatWindow::OnDirectFileTransferStarted(const std::string& sessionId)
{
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    balloonchatwnd::ChatMessage msg;
    if (list == NULL || !list->GetDirectFileMessage(sessionId, msg))
        return false;
    //对方接收了（或重试续传开始了）：发送方转「传送中」，接收方转「接收中」
    msg.fileState = msg.fromMe ? balloonchatwnd::kFileUploading : balloonchatwnd::kFileDownloading;
    msg.fileDirectEnd = 0;
    list->SetDirectFileMessage(sessionId, msg);
    return true;
}

bool CBalloonChatWindow::OnDirectFileProgress(const foldertransfer::FolderProgressNotice& progress)
{
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    balloonchatwnd::ChatMessage msg;
    if (list == NULL || !list->GetDirectFileMessage(progress.m_sessionId, msg))
        return false;
    msg.fileState = msg.fromMe ? balloonchatwnd::kFileUploading : balloonchatwnd::kFileDownloading;
    msg.fileProgress = DirectFilePercent(progress.m_doneBytes, progress.m_totalBytes);
    msg.fileLinkPath = progress.m_linkPath;
    list->SetDirectFileMessage(progress.m_sessionId, msg);
    return true;
}

bool CBalloonChatWindow::OnDirectFileResult(const foldertransfer::FolderResultNotice& result)
{
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    balloonchatwnd::ChatMessage msg;
    if (list == NULL || !list->GetDirectFileMessage(result.m_sessionId, msg))
        return false;

    if (result.m_success)
    {
        if (msg.fromMe)
        {
            ApplyDirectChange(msg, balloonchatwnd::SenderBubbleForAction(foldertransfer::kDirectSendCompleted));
        }
        else
        {
            ApplyDirectChange(msg, balloonchatwnd::ReceiverBubbleForAction(foldertransfer::kDirectRecvCompleted,
                                                                           folder_state_completed));
            //接收方在这里才知道落地路径：「打开文件 / 打开所在文件夹 / 另存为」都用它
            msg.fileLocalPath = CString(result.m_localFile.c_str());
        }
        msg.fileProgress = kDirectFileFullPercent;
        msg.fileLinkPath = result.m_linkPath;
        if (!result.m_md5.empty())
            msg.fileMd5 = result.m_md5;
        list->SetDirectFileMessage(result.m_sessionId, msg);
        LOG_INFO("Direct file: completed, session=%s, fromme=%d, linkpath=%d.", result.m_sessionId.c_str(),
                 msg.fromMe ? 1 : 0, result.m_linkPath);
        return true;
    }

    LOG_INFO("Direct file: failed, session=%s, fromme=%d, errcode=%d.", result.m_sessionId.c_str(),
             msg.fromMe ? 1 : 0, result.m_errCode);
    if (msg.fromMe)
    {
        //本端取消：已取消；其余按状态推送同一套规则（622 改为离线文件，别的失败给「重新发送」）
        int action = foldertransfer::kDirectSendCanceled;
        if (result.m_errCode != error_code_folder_canceled)
            action = foldertransfer::DecideDirectSendOnState(folder_state_failed, result.m_errCode);
        if (action == foldertransfer::kDirectSendToOffline)
        {
            StartDirectFileOfflineUpload(result.m_sessionId);
            return true;
        }
        ApplyDirectChange(msg, balloonchatwnd::SenderBubbleForAction(action));
    }
    else
    {
        const int action = foldertransfer::DecideDirectRecvOnState(folder_state_failed, result.m_errCode);
        ApplyDirectChange(msg, balloonchatwnd::ReceiverBubbleForAction(action, folder_state_failed));
        //本端取消了接收：作废原因单独一档，措辞是「已取消接收」
        if (result.m_errCode == error_code_folder_canceled)
            msg.fileDirectEnd = balloonchatwnd::kDirectEndSelfCanceled;
    }
    list->SetDirectFileMessage(result.m_sessionId, msg);
    return true;
}

void CBalloonChatWindow::AppendDirectFileInviteBubble(const std::string& sessionIdUtf8,
                                                      const std::string& fileNameUtf8, long long sizeBytes)
{
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    if (list == NULL)
        return;
    //已经画过（实时推送与点开会话补画都可能走到这里）：不重复画
    balloonchatwnd::ChatMessage existing;
    if (list->GetDirectFileMessage(sessionIdUtf8, existing))
        return;

    std::wstring fileName;
    foldertransfer::Utf8ToUtf16(fileNameUtf8, fileName);
    balloonchatwnd::ChatMessage msg = balloonchatwnd::MakeIncomingFileMessage(
        false, CString(fileName.c_str()), sizeBytes, 0, std::string(), std::string(),
        balloonchatwnd::kFilePendingAccept);
    msg.fileDirect = true;
    msg.fileDirectSessionId = sessionIdUtf8;
    FillSenderAvatar(msg, false);
    list->AppendMessage(msg);
    LOG_INFO("Direct file recv: invite bubble created, session=%s, size=%lld.", sessionIdUtf8.c_str(), sizeBytes);
}

bool CBalloonChatWindow::HandleDirectFileAction(const balloonchatwnd::ChatFileActionData& d)
{
    if (!d.direct)
        return false;
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    const std::string key = !d.directSessionId.empty() ? d.directSessionId : d.clientMsgId;
    balloonchatwnd::ChatMessage msg;
    if (list == NULL || key.empty() || !list->GetDirectFileMessage(key, msg))
        return false;
    foldertransfer::FolderTransferManager& mgr = foldertransfer::FolderTransferManager::GetInstance();

    switch (d.action)
    {
    //发送方在对方接收之前改为上传服务器：撤回邀请（服务端告诉对方「已改为离线发送」），再上传
    case balloonchatwnd::kChatFileSendOffline:
        LOG_INFO("Direct file send: user chose offline upload, key=%s.", key.c_str());
        mgr.CancelTransfer(key, true);
        StartDirectFileOfflineUpload(key);
        return true;

    //取消：还在等对方接收时是撤回邀请，传输中则停掉本端会话、按失败结束（结果回投到达时改气泡）
    case balloonchatwnd::kChatFileCancel:
        LOG_INFO("Direct file: user canceled, key=%s, state=%d.", key.c_str(), msg.fileState);
        mgr.CancelTransfer(key);
        if (msg.fileState == balloonchatwnd::kFileDirectWaiting)
        {
            ApplyDirectChange(msg, balloonchatwnd::SenderBubbleForAction(foldertransfer::kDirectSendCanceled));
            list->SetDirectFileMessage(key, msg);
        }
        return true;

    //接收：不弹保存框，落在下载缓存目录下的 direct-<会话号>\ 里
    case balloonchatwnd::kChatFileAccept:
    {
        const std::wstring chatFileDir = net::UserCacheDir(g_szDataRoot, net::CurrentUserAccount(),
                                                           kDirectFileCacheSubDir);
        const std::wstring saveDir = foldertransfer::DirectFileSaveDir(chatFileDir, msg.fileDirectSessionId);
        const int verdict = saveDir.empty() ? foldertransfer::kFolderStartBadArgument
                                            : mgr.AcceptInvite(msg.fileDirectSessionId, saveDir);
        if (verdict == foldertransfer::kFolderStartOk)
        {
            msg.fileState = balloonchatwnd::kFileDownloading;
            msg.fileProgress = 0;
        }
        else
        {
            //这一笔已经结束（对方撤回、超时、已改为离线发送）：气泡作废，不留一个点了没反应的「接收」
            LOG_WARNING("Direct file recv: accept refused, verdict=%d, session=%s.", verdict,
                        msg.fileDirectSessionId.c_str());
            msg.fileState = balloonchatwnd::kFileDirectToOffline;
            msg.fileDirectEnd = folder_state_canceled;
        }
        list->SetDirectFileMessage(key, msg);
        return true;
    }

    //拒绝：告诉服务端，气泡作废（原因「已拒绝接收」）
    case balloonchatwnd::kChatFileReject:
        LOG_INFO("Direct file recv: user rejected, session=%s.", msg.fileDirectSessionId.c_str());
        mgr.RejectInvite(msg.fileDirectSessionId);
        ApplyDirectChange(msg, balloonchatwnd::ReceiverBubbleForAction(foldertransfer::kDirectRecvRejected,
                                                                       folder_state_rejected));
        list->SetDirectFileMessage(key, msg);
        return true;

    //重新发送（发送方）：保留期内先试续传，续不了就改为上传服务器
    case balloonchatwnd::kChatFileRetry:
        if (!msg.fromMe)
            return true;
        if (!msg.fileDirectSessionId.empty() && mgr.IsWithinRetryWindow(msg.fileDirectSessionId)
            && mgr.RetryTransfer(msg.fileDirectSessionId) == foldertransfer::kFolderStartOk)
        {
            LOG_INFO("Direct file send: retry requested, session=%s.", msg.fileDirectSessionId.c_str());
            msg.fileState = balloonchatwnd::kFileUploading;
            list->SetDirectFileMessage(key, msg);
            return true;
        }
        StartDirectFileOfflineUpload(key);
        return true;

    //打开文件等与会话无关的动作已由 HandleLocalFileAction 处理，走不到这里
    default:
        return false;
    }
}

bool CBalloonChatWindow::AdoptDirectFileRecord(const net::MessageNotify& notify)
{
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    balloonchatwnd::ChatMessage msg;
    if (list == NULL || notify.m_fileDirectSessionId.empty()
        || !list->GetDirectFileMessage(notify.m_fileDirectSessionId, msg))
        return false;

    //会话里那条实时气泡就是这条记录：补上服务器消息号（撤回、删除、转发要用）、MD5 与链路路径，不另插一行
    msg.msgid = (long long)notify.m_msgid;
    msg.fileMsgid = (long long)notify.m_msgid;
    if (!notify.m_fileMd5.empty())
        msg.fileMd5 = notify.m_fileMd5;
    if (notify.m_fileLinkPath != 0)
        msg.fileLinkPath = notify.m_fileLinkPath;
    list->SetDirectFileMessage(notify.m_fileDirectSessionId, msg);
    LOG_INFO("Direct file: record adopted the live bubble, session=%s, msgid=%lld, result=%d.",
             notify.m_fileDirectSessionId.c_str(), (long long)notify.m_msgid, notify.m_fileDirectResult);
    return true;
}
