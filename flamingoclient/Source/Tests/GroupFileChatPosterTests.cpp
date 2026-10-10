/**
 *  群里发送录像「上传群文件 + 聊天里发文件消息」登记的单元测试（标签 [groupfilepost]），GroupFileChatPosterTests.cpp
 *  背景：2026-10-05 用户反馈在群聊里录完屏点「发送」看不到效果（录像只进了群文件库），定为上传群文件并在群里发一条文件消息。
 *  覆盖：
 *    - GFP1 登记过的上传成功：给出文件消息（本地路径、fileid、文件名、大小、md5），之后不再登记；
 *    - GFP2 没登记过的上传（普通「上传群文件」按钮）成功：不发消息；
 *    - GFP3 登记过的上传失败或取消：不发消息，并取消登记；上传 id 不大于 0 时不登记。
 *  群文件消息的协议包体见 MessageProtocolTests.cpp 的 MP-FILE2（群聊 ctype=2）。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include "../UI/BalloonChatWnd/GroupFileChatPoster.h"
#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatFileStateModel.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"

namespace {

const int kGfpUpload = 7;
const int kGfpOtherUpload = 8;
const int64_t kGfpGroup = 268435463;
const int64_t kGfpSize = 15555677;

net::GroupFileUploadResult GfpResult(int uploadId, bool ok)
{
    net::GroupFileUploadResult r;
    r.m_groupid = kGfpGroup;
    r.m_uploadid = uploadId;
    r.m_ok = ok;
    r.m_fileid = "8800123";
    r.m_filename = "20261005_131807.mp4";
    r.m_size = kGfpSize;
    r.m_md5 = "0123456789abcdef0123456789abcdef";
    return r;
}

} // namespace

TEST_CASE("GFP1 a marked upload posts a file message", "[groupfilepost]")
{
    balloonchatwnd::GroupFileChatPoster poster;
    poster.Mark(kGfpUpload, L"C:\\Videos\\20261005_131807.mp4");
    CHECK(poster.IsMarked(kGfpUpload));
    balloonchatwnd::GroupFileChatPost post;
    REQUIRE(poster.Take(GfpResult(kGfpUpload, true), post));
    CHECK(post.m_localPath == L"C:\\Videos\\20261005_131807.mp4");
    CHECK(post.m_fileid == "8800123");
    CHECK(post.m_filename == "20261005_131807.mp4");
    CHECK(post.m_size == kGfpSize);
    CHECK(post.m_md5 == "0123456789abcdef0123456789abcdef");
    CHECK_FALSE(poster.IsMarked(kGfpUpload));
    //同一结果再来一次：已不登记，不重复发
    CHECK_FALSE(poster.Take(GfpResult(kGfpUpload, true), post));
}

TEST_CASE("GFP2 an ordinary group file upload posts nothing", "[groupfilepost]")
{
    balloonchatwnd::GroupFileChatPoster poster;
    poster.Mark(kGfpUpload, L"C:\\a.mp4");
    balloonchatwnd::GroupFileChatPost post;
    CHECK_FALSE(poster.Take(GfpResult(kGfpOtherUpload, true), post));
    CHECK(poster.IsMarked(kGfpUpload));
}

TEST_CASE("GFP3 a failed marked upload posts nothing and is forgotten", "[groupfilepost]")
{
    balloonchatwnd::GroupFileChatPoster poster;
    poster.Mark(kGfpUpload, L"C:\\a.mp4");
    balloonchatwnd::GroupFileChatPost post;
    CHECK_FALSE(poster.Take(GfpResult(kGfpUpload, false), post));
    CHECK_FALSE(poster.IsMarked(kGfpUpload));
    //上传 id 不大于 0：不登记
    poster.Mark(0, L"C:\\b.mp4");
    CHECK_FALSE(poster.IsMarked(0));
}

// ---- 2026-10-05：被服务端拒绝的群文件消息可以重新发送 ----

namespace {

//本人那条群文件消息的客户端消息 ID，以及一条不在列表里的
const char* const kGfpClientMsgId = "c-gfp-1";
const char* const kGfpAbsentClientMsgId = "c-gfp-absent";
//一条已有服务器 msgid 的文件消息
const long long kGfpServerMsgid = 9001;

balloonchatwnd::GroupFileChatPost GfpPost()
{
    balloonchatwnd::GroupFileChatPost post;
    post.m_localPath = L"C:\\Videos\\20261005_131807.mp4";
    post.m_fileid = "8800123";
    post.m_filename = "20261005_131807.mp4";
    post.m_size = kGfpSize;
    post.m_md5 = "0123456789abcdef0123456789abcdef";
    return post;
}

//按客户端消息 ID 取文件气泡的数据；找不到时测试失败
balloonchatwnd::ChatMessage GfpFileMessageOf(const balloonchatwnd::ChatMessageList& list, const std::string& clientMsgId)
{
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = list.Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        const balloonchatwnd::ChatBubble* b = dynamic_cast<const balloonchatwnd::ChatBubble*>(kids[i].get());
        if (b != NULL && b->GetMessage().fileClientMsgId == clientMsgId)
            return b->GetMessage();
    }
    FAIL("file bubble not found");
    return balloonchatwnd::ChatMessage();
}

} // namespace

TEST_CASE("GFP4 群聊文件气泡重试按钮的去向", "[groupfilepost]")
{
    using namespace balloonchatwnd;
    //本人发出、被拒绝（没有服务器 msgid）、有 fileid：重发文件消息
    CHECK(ChooseGroupFileRetry(true, 0, "8800123") == group_file_retry_resend);
    //已有服务器 msgid：重新下载（本人的与别人的都一样）
    CHECK(ChooseGroupFileRetry(true, kGfpServerMsgid, "8800123") == group_file_retry_download);
    CHECK(ChooseGroupFileRetry(false, kGfpServerMsgid, "8800123") == group_file_retry_download);
    //没有 fileid：什么都做不了
    CHECK(ChooseGroupFileRetry(true, 0, "") == group_file_retry_none);
    CHECK(ChooseGroupFileRetry(false, kGfpServerMsgid, "") == group_file_retry_none);
    //别人的消息却没有服务器 msgid：不处理
    CHECK(ChooseGroupFileRetry(false, 0, "8800123") == group_file_retry_none);
}

TEST_CASE("GFP5 重发被拒绝的本人文件消息：清掉失败标记，状态回到已完成", "[groupfilepost]")
{
    using namespace balloonchatwnd;
    ChatMessageList list;
    list.AppendMessage(MakeGroupFileEcho(GfpPost(), kGfpClientMsgId));
    //别人发来的一条文件消息（已有服务器 msgid）
    list.AppendMessage(MakeIncomingFileMessage(false, _T("other.mp4"), kGfpSize, kGfpServerMsgid, "8800999", "md5x",
                                               kFilePendingAccept));

    //还没失败：不改
    CHECK_FALSE(list.RestartFileMessageSendByClientMsgId(kGfpClientMsgId));

    REQUIRE(list.MarkSendFailed(kGfpClientMsgId, _T("全员禁言中")));
    ChatMessage failed = GfpFileMessageOf(list, kGfpClientMsgId);
    REQUIRE(failed.sendFailed);
    REQUIRE(failed.fileState == kFileFailed);

    REQUIRE(list.RestartFileMessageSendByClientMsgId(kGfpClientMsgId));
    const ChatMessage restarted = GfpFileMessageOf(list, kGfpClientMsgId);
    CHECK_FALSE(restarted.sendFailed);
    CHECK(restarted.failReason.IsEmpty());
    CHECK(restarted.fileState == kFileNormal);
    //其余字段不变
    CHECK(restarted.fileFileId == "8800123");
    CHECK(restarted.fileLocalPath == _T("C:\\Videos\\20261005_131807.mp4"));

    //已经不是失败状态、找不到这条、空串：都返回 false
    CHECK_FALSE(list.RestartFileMessageSendByClientMsgId(kGfpClientMsgId));
    CHECK_FALSE(list.RestartFileMessageSendByClientMsgId(kGfpAbsentClientMsgId));
    CHECK_FALSE(list.RestartFileMessageSendByClientMsgId(""));
}

TEST_CASE("GFP6 群里本人刚发出的文件气泡带上本地路径、fileid 与 md5", "[groupfilepost]")
{
    using namespace balloonchatwnd;
    const ChatMessage echo = MakeGroupFileEcho(GfpPost(), kGfpClientMsgId);
    CHECK(echo.kind == kMsgFile);
    CHECK(echo.fromMe);
    CHECK(echo.fileName == _T("20261005_131807.mp4"));
    CHECK(echo.fileSizeBytes == kGfpSize);
    CHECK(echo.fileLocalPath == _T("C:\\Videos\\20261005_131807.mp4"));
    CHECK(echo.fileFileId == "8800123");
    CHECK(echo.fileMd5 == "0123456789abcdef0123456789abcdef");
    CHECK(echo.fileClientMsgId == kGfpClientMsgId);
    CHECK(echo.fileState == kFileNormal);
    CHECK_FALSE(echo.sendFailed);
}
