/**
 *  ChatDirectFileModel.cpp
 *
 *  单聊单文件 P2P 直传在聊天窗一侧的纯逻辑，见 ChatDirectFileModel.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h，在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "../../Feature/FolderTransfer/DirectFileTransfer.h"   //DirectFileSenderAction / DirectFileRecvAction
#include "ChatDirectFileModel.h"

#include <stddef.h>
#include <windows.h>                       //GetFileAttributesExW：DirectFileHasLocalCopy 实测磁盘

#include "../../net/Protocol.h"            //folder_transfer_state
#include "../../Common/I18n/I18n.h"        //TR_NOOP：只标记，由调用方交给 TR
#include "ChatFileStateModel.h"
#include "ChatFolderStateModel.h"

namespace balloonchatwnd {

DirectBubbleChange SenderBubbleForAction(int senderAction)
{
    DirectBubbleChange c;
    switch (senderAction)
    {
    //改为离线文件：气泡转成普通文件的「上传中」，立即交给上传服务器；对方此前的状态清零
    case foldertransfer::kDirectSendToOffline:
        c.m_fileState = kFileUploading;
        c.m_direct = false;
        c.m_startUpload = true;
        c.m_peerState = kFilePeerNone;
        break;

    //对方拒绝：本地源文件还在，停在完成态（可打开），状态行写「对方已拒绝接收」
    case foldertransfer::kDirectSendRejected:
        c.m_fileState = kFileNormal;
        c.m_peerState = kFilePeerRejected;
        break;

    //完成：完成态，状态行写「对方已接收」
    case foldertransfer::kDirectSendCompleted:
        c.m_fileState = kFileNormal;
        c.m_peerState = kFilePeerReceived;
        break;

    //传输中途失败：失败态，仍是直传，「重新发送」先试保留期内的续传
    case foldertransfer::kDirectSendFailed:
        c.m_fileState = kFileFailed;
        break;

    //本端撤回：已取消，记下取消原因，状态行写「已取消发送」、卡片不画进度条（2026-10-05 用户确认），仍可「重新发送」
    case foldertransfer::kDirectSendCanceled:
        c.m_fileState = kFileCancelled;
        c.m_directEnd = folder_state_canceled;
        break;

    //文件名含敏感词：失败态，不再是直传（「重新发送」走普通上传，由服务端照常拦截并提示）
    case foldertransfer::kDirectSendBlocked:
        c.m_fileState = kFileFailed;
        c.m_direct = false;
        break;

    //继续等与未知取值：不改
    default:
        break;
    }
    return c;
}

DirectBubbleChange ReceiverBubbleForAction(int recvAction, int folderState)
{
    DirectBubbleChange c;
    switch (recvAction)
    {
    //收完并校验通过：完成态
    case foldertransfer::kDirectRecvCompleted:
        c.m_fileState = kFileNormal;
        break;

    //本端拒绝：作废，原因「已拒绝」
    case foldertransfer::kDirectRecvRejected:
        c.m_fileState = kFileDirectToOffline;
        c.m_directEnd = folder_state_rejected;
        break;

    //发送端撤回：作废，原因「对方已取消」
    case foldertransfer::kDirectRecvSenderCanceled:
        c.m_fileState = kFileDirectToOffline;
        c.m_directEnd = folder_state_canceled;
        break;

    //改为离线文件：原因照推送的状态记；建不起 P2P 链路的失败（622）同样是发送端改为离线发送
    case foldertransfer::kDirectRecvToOffline:
        c.m_fileState = kFileDirectToOffline;
        c.m_directEnd = (folderState == folder_state_failed) ? folder_state_to_offline : folderState;
        break;

    //传输中途失败：作废，原因「传输失败」；发送端若在保留期内续传，中转参数一到气泡会回到接收中
    case foldertransfer::kDirectRecvFailed:
        c.m_fileState = kFileDirectToOffline;
        c.m_directEnd = folder_state_failed;
        break;

    //继续等与未知取值：不改
    default:
        break;
    }
    return c;
}

const wchar_t* DirectFileStatusKey(int fileState, int directEnd, bool fromMe, bool direct)
{
    if (!direct)
        return NULL;

    switch (fileState)
    {
    //发送方等对方接收
    case kFileDirectWaiting:
        return TR_NOOP(L"等待对方接收");

    //历史里本机找不到这份文件：服务器上本来就没有
    case kFileDeleted:
        return TR_NOOP(L"在线直传，服务器未保存");

    //接收中（调用方代入进度）
    case kFileDownloading:
        return TR_NOOP(L"接收中 %d%%");

    //发送方自己取消了这次直传（接收方一侧的取消走「已作废」，见下）
    case kFileCancelled:
        return fromMe ? TR_NOOP(L"已取消发送") : NULL;

    //作废：按原因、按收发两侧给出不同措辞
    case kFileDirectToOffline:
        switch (directEnd)
        {
        //发送端改为离线发送（含建不起 P2P 链路）
        case folder_state_to_offline:
            return fromMe ? TR_NOOP(L"已改为发送离线文件") : TR_NOOP(L"对方已改为发送离线文件");
        //接收端拒绝
        case folder_state_rejected:
            return fromMe ? TR_NOOP(L"对方已拒绝接收") : TR_NOOP(L"已拒绝接收");
        //发送端撤回
        case folder_state_canceled:
            return fromMe ? TR_NOOP(L"已取消发送") : TR_NOOP(L"对方已取消发送");
        //邀请超时：发送端随即改为离线发送
        case folder_state_timeout:
            return fromMe ? TR_NOOP(L"对方超时未接收，已改为发送离线文件") : TR_NOOP(L"超时未接收，对方将改为发送离线文件");
        //对方离线
        case folder_state_peer_offline:
            return TR_NOOP(L"对方已离线");
        //传输中途失败
        case folder_state_failed:
            return TR_NOOP(L"传输失败");
        //接收方自己在传输中途取消了接收
        case kDirectEndSelfCanceled:
            return TR_NOOP(L"已取消接收");
        //其余原因
        default:
            return TR_NOOP(L"已失效");
        }

    //其余状态沿用普通文件的文字
    default:
        return NULL;
    }
}

void DirectRecordBubble(int result, bool hasLocalFile, int& outState, int& outDirectEnd)
{
    outDirectEnd = 0;
    if (result == folder_state_completed)
    {
        //本机有文件可直接打开；没有就显示「在线直传，服务器未保存」，不提供下载
        outState = hasLocalFile ? kFileNormal : kFileDeleted;
        return;
    }
    if (result == folder_state_rejected)
    {
        outState = kFileDirectToOffline;
        outDirectEnd = folder_state_rejected;
        return;
    }
    //解析层只放过完成与拒绝两种结局，走到这里按本机没有文件处理
    outState = kFileDeleted;
}

int DirectRecordPeerState(int result, bool fromMe, bool hasLocalFile)
{
    //只有发送方、直传成功、本机仍有文件时写「对方已接收」；本机没有文件时状态行要说明服务器未保存
    if (fromMe && hasLocalFile && result == folder_state_completed)
    {
        return kFilePeerReceived;
    }
    return 0;
}

bool FolderBubbleShowsP2PBadge(int folderState, bool linkIsDirect)
{
    //正在传或已传完、且数据走的是直连链路时画；经中转的、失败的、还在邀请的都不画（方案决定 4）
    return linkIsDirect && (folderState == kFolderTransferring || folderState == kFolderCompleted);
}

bool DirectFileHasLocalCopy(const std::wstring& localPath, long long expectSize)
{
    if (localPath.empty())
    {
        return false;
    }
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!::GetFileAttributesExW(localPath.c_str(), GetFileExInfoStandard, &fad)
        || (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        return false;
    }
    //大小不符说明同名文件已被改写，转发出去的就不是原来那份
    const long long size = ((long long)fad.nFileSizeHigh << 32) | (long long)fad.nFileSizeLow;
    return expectSize <= 0 || size == expectSize;
}

bool DirectFileForwardBlocked(bool isFile, bool direct, bool hasLocalCopy)
{
    return isFile && direct && !hasLocalCopy;
}

} // namespace balloonchatwnd
