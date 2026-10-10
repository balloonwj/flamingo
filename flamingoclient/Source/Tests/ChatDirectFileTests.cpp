/**
 *  ChatDirectFileTests.cpp
 *
 *  单聊单文件 P2P 直传在聊天窗一侧的测试，标签 [directfilechat]，方案见 docs/单聊文件P2P直传与P2P徽章方案.md 第 3 节：
 *    - DFC1：消息推送里的直传文件消息（msgtype 3、content 带 direct=1）：解析出直传标记、会话号、结局、链路路径、MD5，
 *            没有 fileid；
 *    - DFC2：普通离线文件消息不是直传：直传标记为假，链路路径为 0；
 *    - DFC3：文件夹传输记录条带回链路路径；缺省为 0；
 *    - DFC4：发送方气泡按直传处置的变化：继续等不改；改为离线文件时转「上传中」、不再是直传并立即上传；对方拒绝时
 *            停在完成态、对方状态为已拒绝；完成时对方状态为已接收；失败、撤回、敏感词各自对应；
 *    - DFC5：接收方气泡按直传处置的变化：完成转完成态；拒绝、对方撤回、改为离线、失败都转「已作废」并记下原因；
 *    - DFC6：直传特有的状态文字：等待对方接收、各种作废原因（收发两侧措辞不同）、历史里本机没有文件、接收中；
 *            其余状态返回空，沿用普通文件的文字；
 *    - DFC7：历史里的直传文件消息：完成且本机有文件为完成态，本机没有为已失效；拒绝为已作废（原因 3）；
 *    - DFC8：文件夹气泡的「P2P」徽章：只有直连的传输在传输中、已完成时画；
 *    - DFC11：历史里直传成功的文件，发送方本机有文件时写「对方已接收」，与实时传完时一致（DFC9、DFC10 在 ChatFileCardTests.cpp）；
 *    - DFC12：发送方取消直传时记下取消原因，状态行写「已取消发送」，卡片不画进度条（卡片层面的 DFC13 在 ChatFileCardTests.cpp）。
 *
 *  框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-04
 */

#include "stdafx.h"

#include <string.h>
#include <string>

#include "catch_amalgamated.hpp"

#include "../net/MessageProtocolClient.h"
#include "../net/Protocol.h"
#include "../Feature/FolderTransfer/DirectFileTransfer.h"
#include "../UI/BalloonChatWnd/ChatDirectFileModel.h"
#include "../UI/BalloonChatWnd/ChatFileStateModel.h"
#include "../UI/BalloonChatWnd/ChatFolderStateModel.h"

using namespace balloonchatwnd;

namespace {

//字符串相等（两个都为 NULL 也算相等）
bool SameText(const wchar_t* a, const wchar_t* b)
{
    if (a == NULL || b == NULL)
        return a == b;
    return wcscmp(a, b) == 0;
}

} // namespace

TEST_CASE("DFC1 直传文件消息：解析出直传标记、会话号、结局、链路路径与 MD5，没有 fileid", "[directfilechat]")
{
    const std::string body =
        "{\"msgid\":\"90002\",\"clientmsgid\":\"folder-5127-5308-1-1\",\"ctype\":1,"
        "\"cid\":\"5308\",\"from\":\"5127\",\"to\":\"5308\",\"msgtype\":3,\"timestamp\":1755100000000,"
        "\"content\":{\"filename\":\"a.bin\",\"filesize\":3145851,\"md5\":\"0123456789abcdef0123456789abcdef\","
        "\"direct\":1,\"sessionid\":\"5127-5308-1-1\",\"result\":8,\"linkpath\":1}}";
    net::MessageNotify notify;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(body, notify));
    CHECK(notify.m_msgtype == message_type_file);
    CHECK(notify.m_fileName == "a.bin");
    CHECK(notify.m_fileSize == 3145851);
    CHECK(notify.m_fileMd5 == "0123456789abcdef0123456789abcdef");
    CHECK(notify.m_fileFileId.empty());
    CHECK(notify.m_fileDirect);
    CHECK(notify.m_fileDirectSessionId == "5127-5308-1-1");
    CHECK(notify.m_fileDirectResult == folder_state_completed);
    CHECK(notify.m_fileLinkPath == 1);

    //拒绝：没有 MD5、没有链路路径
    net::MessageNotify rejected;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(
        "{\"msgid\":\"90003\",\"from\":\"5127\",\"msgtype\":3,"
        "\"content\":{\"filename\":\"a.bin\",\"filesize\":10,\"direct\":1,\"sessionid\":\"s-2\",\"result\":3}}",
        rejected));
    CHECK(rejected.m_fileDirect);
    CHECK(rejected.m_fileDirectResult == folder_state_rejected);
    CHECK(rejected.m_fileLinkPath == 0);
    CHECK(rejected.m_fileMd5.empty());
}

TEST_CASE("DFC2 普通离线文件消息不是直传", "[directfilechat]")
{
    net::MessageNotify notify;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(
        "{\"msgid\":\"90004\",\"from\":\"5127\",\"msgtype\":3,"
        "\"content\":{\"fileid\":\"77\",\"filename\":\"b.txt\",\"filesize\":5,\"md5\":\"0123456789abcdef0123456789abcdef\"}}",
        notify));
    CHECK(notify.m_fileFileId == "77");
    CHECK_FALSE(notify.m_fileDirect);
    CHECK(notify.m_fileDirectSessionId.empty());
    CHECK(notify.m_fileDirectResult == 0);
    CHECK(notify.m_fileLinkPath == 0);
}

TEST_CASE("DFC3 文件夹传输记录条带回链路路径，缺省为 0", "[directfilechat]")
{
    net::MessageNotify withPath;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(
        "{\"msgid\":\"1\",\"from\":\"10001\",\"msgtype\":18,"
        "\"content\":{\"sessionid\":\"s\",\"foldername\":\"f\",\"totaldirs\":0,"
        "\"totalfiles\":1,\"totalsize\":1,\"result\":8,\"errcode\":0,\"linkpath\":2}}", withPath));
    CHECK(withPath.m_folderResult == folder_state_completed);
    CHECK(withPath.m_folderLinkPath == 2);

    net::MessageNotify noPath;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(
        "{\"msgid\":\"1\",\"from\":\"10001\",\"msgtype\":18,"
        "\"content\":{\"sessionid\":\"s\",\"foldername\":\"f\",\"totaldirs\":0,"
        "\"totalfiles\":1,\"totalsize\":1,\"result\":8,\"errcode\":0}}", noPath));
    CHECK(noPath.m_folderLinkPath == 0);
}

TEST_CASE("DFC4 发送方气泡按直传处置的变化", "[directfilechat]")
{
    //继续等：什么都不改
    DirectBubbleChange wait = SenderBubbleForAction(foldertransfer::kDirectSendWait);
    CHECK(wait.m_fileState == kDirectKeepState);
    CHECK(wait.m_peerState == kDirectKeepState);
    CHECK_FALSE(wait.m_startUpload);
    CHECK(wait.m_direct);

    //改为离线文件：转上传中、不再是直传、立即上传
    DirectBubbleChange offline = SenderBubbleForAction(foldertransfer::kDirectSendToOffline);
    CHECK(offline.m_fileState == kFileUploading);
    CHECK_FALSE(offline.m_direct);
    CHECK(offline.m_startUpload);
    CHECK(offline.m_peerState == kFilePeerNone);

    //对方拒绝：停在完成态（本地源文件仍可打开），对方状态为已拒绝
    DirectBubbleChange rejected = SenderBubbleForAction(foldertransfer::kDirectSendRejected);
    CHECK(rejected.m_fileState == kFileNormal);
    CHECK(rejected.m_peerState == kFilePeerRejected);
    CHECK(rejected.m_direct);
    CHECK_FALSE(rejected.m_startUpload);

    //完成：完成态，对方状态为已接收
    DirectBubbleChange done = SenderBubbleForAction(foldertransfer::kDirectSendCompleted);
    CHECK(done.m_fileState == kFileNormal);
    CHECK(done.m_peerState == kFilePeerReceived);
    CHECK(done.m_direct);

    //传输中途失败：失败态，仍是直传（「重新发送」先试续传）
    DirectBubbleChange failed = SenderBubbleForAction(foldertransfer::kDirectSendFailed);
    CHECK(failed.m_fileState == kFileFailed);
    CHECK(failed.m_direct);
    CHECK_FALSE(failed.m_startUpload);

    //撤回：已取消
    DirectBubbleChange canceled = SenderBubbleForAction(foldertransfer::kDirectSendCanceled);
    CHECK(canceled.m_fileState == kFileCancelled);
    CHECK_FALSE(canceled.m_startUpload);

    //文件名含敏感词：失败态，不上传
    DirectBubbleChange blocked = SenderBubbleForAction(foldertransfer::kDirectSendBlocked);
    CHECK(blocked.m_fileState == kFileFailed);
    CHECK_FALSE(blocked.m_startUpload);
}

TEST_CASE("DFC5 接收方气泡按直传处置的变化", "[directfilechat]")
{
    DirectBubbleChange wait = ReceiverBubbleForAction(foldertransfer::kDirectRecvWait, folder_state_transferring);
    CHECK(wait.m_fileState == kDirectKeepState);
    CHECK(wait.m_directEnd == 0);

    DirectBubbleChange done = ReceiverBubbleForAction(foldertransfer::kDirectRecvCompleted, folder_state_completed);
    CHECK(done.m_fileState == kFileNormal);
    CHECK(done.m_direct);

    DirectBubbleChange rejected = ReceiverBubbleForAction(foldertransfer::kDirectRecvRejected, folder_state_rejected);
    CHECK(rejected.m_fileState == kFileDirectToOffline);
    CHECK(rejected.m_directEnd == folder_state_rejected);

    DirectBubbleChange canceled = ReceiverBubbleForAction(foldertransfer::kDirectRecvSenderCanceled, folder_state_canceled);
    CHECK(canceled.m_fileState == kFileDirectToOffline);
    CHECK(canceled.m_directEnd == folder_state_canceled);

    //改为离线：原因照推送里的状态记；失败且原因是 622 的按「已改为离线发送」记
    DirectBubbleChange timeout = ReceiverBubbleForAction(foldertransfer::kDirectRecvToOffline, folder_state_timeout);
    CHECK(timeout.m_fileState == kFileDirectToOffline);
    CHECK(timeout.m_directEnd == folder_state_timeout);
    DirectBubbleChange toOffline = ReceiverBubbleForAction(foldertransfer::kDirectRecvToOffline, folder_state_failed);
    CHECK(toOffline.m_directEnd == folder_state_to_offline);

    DirectBubbleChange failed = ReceiverBubbleForAction(foldertransfer::kDirectRecvFailed, folder_state_failed);
    CHECK(failed.m_fileState == kFileDirectToOffline);
    CHECK(failed.m_directEnd == folder_state_failed);

    //接收方的气泡从不自己上传
    CHECK_FALSE(done.m_startUpload);
    CHECK_FALSE(toOffline.m_startUpload);
}

TEST_CASE("DFC6 直传特有的状态文字", "[directfilechat]")
{
    CHECK(SameText(DirectFileStatusKey(kFileDirectWaiting, 0, true, true), L"等待对方接收"));
    //作废原因：收发两侧措辞不同
    CHECK(SameText(DirectFileStatusKey(kFileDirectToOffline, folder_state_to_offline, false, true), L"对方已改为发送离线文件"));
    CHECK(SameText(DirectFileStatusKey(kFileDirectToOffline, folder_state_rejected, false, true), L"已拒绝接收"));
    CHECK(SameText(DirectFileStatusKey(kFileDirectToOffline, folder_state_rejected, true, true), L"对方已拒绝接收"));
    CHECK(SameText(DirectFileStatusKey(kFileDirectToOffline, folder_state_canceled, false, true), L"对方已取消发送"));
    CHECK(SameText(DirectFileStatusKey(kFileDirectToOffline, folder_state_timeout, false, true),
                   L"超时未接收，对方将改为发送离线文件"));
    CHECK(SameText(DirectFileStatusKey(kFileDirectToOffline, folder_state_peer_offline, false, true), L"对方已离线"));
    CHECK(SameText(DirectFileStatusKey(kFileDirectToOffline, folder_state_failed, false, true), L"传输失败"));
    //接收方自己在传输中途点了「取消」
    CHECK(SameText(DirectFileStatusKey(kFileDirectToOffline, kDirectEndSelfCanceled, false, true), L"已取消接收"));
    //历史里本机没有这份直传的文件
    CHECK(SameText(DirectFileStatusKey(kFileDeleted, 0, false, true), L"在线直传，服务器未保存"));
    //接收中（调用方把进度代入 %d）
    CHECK(SameText(DirectFileStatusKey(kFileDownloading, 0, false, true), L"接收中 %d%%"));
    //其余状态、以及普通文件：沿用普通文件的文字
    CHECK(DirectFileStatusKey(kFileNormal, 0, true, true) == NULL);
    CHECK(DirectFileStatusKey(kFileUploading, 0, true, true) == NULL);
    CHECK(DirectFileStatusKey(kFileDeleted, 0, false, false) == NULL);
    CHECK(DirectFileStatusKey(kFileDownloading, 0, false, false) == NULL);
}

TEST_CASE("DFC7 历史里的直传文件消息", "[directfilechat]")
{
    int state = -1;
    int end = -1;
    DirectRecordBubble(folder_state_completed, true, state, end);
    CHECK(state == kFileNormal);
    CHECK(end == 0);
    DirectRecordBubble(folder_state_completed, false, state, end);
    CHECK(state == kFileDeleted);
    CHECK(end == 0);
    DirectRecordBubble(folder_state_rejected, true, state, end);
    CHECK(state == kFileDirectToOffline);
    CHECK(end == folder_state_rejected);
    DirectRecordBubble(folder_state_rejected, false, state, end);
    CHECK(state == kFileDirectToOffline);
    CHECK(end == folder_state_rejected);
}

TEST_CASE("DFC12 发送方取消直传：记下取消原因，状态行写「已取消发送」，卡片不画进度条", "[directfilechat]")
{
    //撤回：仍是已取消、仍是直传，另记下取消原因，状态行据此写「已取消发送」
    const DirectBubbleChange c = SenderBubbleForAction(foldertransfer::kDirectSendCanceled);
    CHECK(c.m_fileState == kFileCancelled);
    CHECK(c.m_directEnd == folder_state_canceled);
    CHECK(c.m_direct);
    CHECK_FALSE(c.m_startUpload);
    const wchar_t* key = DirectFileStatusKey(kFileCancelled, folder_state_canceled, true, true);
    REQUIRE(key != NULL);
    CHECK(std::wstring(key) == L"已取消发送");
    //普通文件的已取消不受影响：沿用普通文件的文字
    CHECK(DirectFileStatusKey(kFileCancelled, 0, true, false) == NULL);

    //进度条：直传被取消后不画；普通文件的已取消、直传的传输中与失败照旧画
    CHECK_FALSE(FileCardShowsProgress(kFileCancelled, true));
    CHECK(FileCardShowsProgress(kFileCancelled, false));
    CHECK(FileCardShowsProgress(kFileUploading, true));
    CHECK(FileCardShowsProgress(kFileFailed, true));
    CHECK_FALSE(FileCardShowsProgress(kFileNormal, false));
}

TEST_CASE("DFC11 历史里直传成功的文件：发送方写「对方已接收」，与实时传完时一致", "[directfilechat]")
{
    //发送方、成功、本机有文件：对方已接收
    CHECK(DirectRecordPeerState(folder_state_completed, true, true) == kFilePeerReceived);
    //本机没有文件：不写对方进展，保留「在线直传，服务器未保存」
    CHECK(DirectRecordPeerState(folder_state_completed, true, false) == 0);
    //接收方没有「对方进展」
    CHECK(DirectRecordPeerState(folder_state_completed, false, true) == 0);
    //拒绝由作废原因说明，这里不重复
    CHECK(DirectRecordPeerState(folder_state_rejected, true, true) == 0);
}

TEST_CASE("DFC8 文件夹气泡的「P2P」徽章：只有直连的传输在传输中、已完成时画", "[directfilechat]")
{
    CHECK(FolderBubbleShowsP2PBadge(kFolderTransferring, true));
    CHECK(FolderBubbleShowsP2PBadge(kFolderCompleted, true));
    CHECK_FALSE(FolderBubbleShowsP2PBadge(kFolderTransferring, false));
    CHECK_FALSE(FolderBubbleShowsP2PBadge(kFolderCompleted, false));
    CHECK_FALSE(FolderBubbleShowsP2PBadge(kFolderInviting, true));
    CHECK_FALSE(FolderBubbleShowsP2PBadge(kFolderFailed, true));
    CHECK_FALSE(FolderBubbleShowsP2PBadge(kFolderRejected, true));
}
