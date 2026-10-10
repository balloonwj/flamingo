/**
 *  单聊文件「重新发送」的单元测试（标签 [fileresend]），FileResendTests.cpp
 *
 *  背景：此前文件发送失败后点「重新发送」，即使这次发送成功，气泡上的失败原因与红色感叹号也不消失，会话列表也不冒泡。
 *  覆盖：
 *    FRS1 被服务端拒绝的本人文件（带失败原因、文件态为失败）重发：失败标记与原因清掉，文件态回到上传中、进度归零；
 *    FRS2 上传失败（没有失败原因）的本人文件重发：同样回到上传中，失败标记保持为否；
 *    FRS3 重发后回执成功（回填 msgid、上传完成）：气泡带服务器 msgid、已完成、没有失败标记；
 *    FRS4 发文件登记的冒泡内容（MakeFileSendBump）：单聊、文件消息类型、摘要「[文件]」，首次发送与重发共用。
 *  单聊窗里「重发时登记冒泡」那一行编不进本工程，留给实机核对。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatFileStateModel.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"
#include "../UI/BalloonChatWnd/ChatSendBumpQueue.h"

using namespace balloonchatwnd;

namespace {

//本人那条文件消息的客户端消息 ID
const char* const kFrsClientMsgId = "c-frs-1";
//文件字节数，只用于构造气泡
const long long kFrsFileBytes = 4096;
//回执回填的服务器 msgid 与时间（Unix 毫秒）
const long long kFrsServerMsgid = 7001;
const long long kFrsAckTimeMs = 1791172800000LL;
//对端 userid（冒泡的会话 id）
const long long kFrsPeerUserId = 5308;
//上传进度：重发前停在半截的进度（百分比）
const int kFrsHalfProgress = 40;

//本人刚发出、正在上传的一条文件消息
void FrsAppendUploading(ChatMessageList& list)
{
    list.AppendMessage(MakeFileMessage(true, _T("report.pdf"), kFrsFileBytes, _T("C:\\docs\\report.pdf"),
                                       kFrsClientMsgId, kFileUploading));
}

//按客户端消息 ID 取文件气泡的数据；找不到时测试失败
ChatMessage FrsFileMessageOf(const ChatMessageList& list)
{
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = list.Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        const ChatBubble* b = dynamic_cast<const ChatBubble*>(kids[i].get());
        if (b != NULL && b->GetMessage().fileClientMsgId == kFrsClientMsgId)
            return b->GetMessage();
    }
    FAIL("file bubble not found");
    return ChatMessage();
}

} // namespace

TEST_CASE("FRS1 被服务端拒绝的本人文件重发：失败标记与原因清掉，回到上传中", "[fileresend]")
{
    ChatMessageList list;
    FrsAppendUploading(list);
    list.UpdateFileUploadProgress(kFrsClientMsgId, kFrsHalfProgress);
    REQUIRE(list.MarkSendFailed(kFrsClientMsgId, _T("包含敏感词")));
    const ChatMessage failed = FrsFileMessageOf(list);
    REQUIRE(failed.sendFailed);
    REQUIRE(failed.fileState == kFileFailed);

    list.MarkFileUploadStartedByClientMsgId(kFrsClientMsgId);
    const ChatMessage restarted = FrsFileMessageOf(list);
    CHECK_FALSE(restarted.sendFailed);
    CHECK(restarted.failReason.IsEmpty());
    CHECK(restarted.fileState == kFileUploading);
    CHECK(restarted.fileProgress == 0);
}

TEST_CASE("FRS2 上传失败的本人文件重发：回到上传中，失败标记保持为否", "[fileresend]")
{
    ChatMessageList list;
    FrsAppendUploading(list);
    list.MarkFileUploadFailed(kFrsClientMsgId);
    const ChatMessage failed = FrsFileMessageOf(list);
    REQUIRE(failed.fileState == kFileFailed);
    REQUIRE_FALSE(failed.sendFailed);

    list.MarkFileUploadStartedByClientMsgId(kFrsClientMsgId);
    const ChatMessage restarted = FrsFileMessageOf(list);
    CHECK_FALSE(restarted.sendFailed);
    CHECK(restarted.failReason.IsEmpty());
    CHECK(restarted.fileState == kFileUploading);
}

TEST_CASE("FRS3 重发后回执成功：带服务器 msgid、已完成、没有失败标记", "[fileresend]")
{
    ChatMessageList list;
    FrsAppendUploading(list);
    REQUIRE(list.MarkSendFailed(kFrsClientMsgId, _T("包含敏感词")));

    //重发：回到上传中；上传完成、回执成功
    list.MarkFileUploadStartedByClientMsgId(kFrsClientMsgId);
    list.MarkFileUploadDone(kFrsClientMsgId);
    list.BackfillSentMessage(kFrsClientMsgId, kFrsServerMsgid, kFrsAckTimeMs);

    const ChatMessage done = FrsFileMessageOf(list);
    CHECK(done.fileMsgid == kFrsServerMsgid);
    CHECK(done.fileState == kFileNormal);
    CHECK_FALSE(done.sendFailed);
    CHECK(done.failReason.IsEmpty());
}

TEST_CASE("FRS4 发文件登记的冒泡内容：单聊、文件消息类型、摘要「[文件]」", "[fileresend]")
{
    const ConvSendBumpData bump = MakeFileSendBump(1, kFrsPeerUserId);
    CHECK(bump.ctype == 1);
    CHECK(bump.cid == kFrsPeerUserId);
    CHECK(bump.msgType == 3);
    CHECK(bump.digest == "[文件]");

    //登记进队列后，回执成功时能原样取出
    ChatSendBumpQueue q;
    q.Add(kFrsClientMsgId, bump);
    ConvSendBumpData out;
    REQUIRE(q.TakeOnSuccess(kFrsClientMsgId, out));
    CHECK(out.cid == kFrsPeerUserId);
    CHECK(out.digest == "[文件]");
}
