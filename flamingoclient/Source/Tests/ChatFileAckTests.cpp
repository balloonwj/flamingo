/**
 *  ChatFileAckTests.cpp
 *
 *  文件下载结果回填与「已接收」回执的判定（标签 [fileack]）。
 *
 *  主面板把每一笔文件下载结果按 msgid 广播给所有已打开的单聊窗；单聊窗原先不论列表里
 *  有没有这条消息都向自己的对端上报「已接收」。改为只有回填确实命中了本窗口里对方发来
 *  的文件时才上报，判定依据是 ChatMessageList::SetFileDownloadedByMsgid 的返回值与出参：
 *
 *    FACK1 命中对方发来的文件：返回 true，出参为"对方发来"，气泡变为完成态并带上本地路径；
 *    FACK2 命中自己发出的文件：返回 true，出参为"自己发出"（单聊窗据此不上报回执）；
 *    FACK3 msgid 不在列表里、或为 0：返回 false，列表里其他气泡不受影响。
 *
 *  用 Catch2 v3（amalgamated 形态）。纯内存：不创建窗口、不要 HWND。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"
#include "../UI/BalloonChatWnd/ChatFileStateModel.h"

using namespace balloonchatwnd;

namespace {

// 两条文件消息的服务器 msgid，以及一个不在列表里的 msgid
const long long kPeerFileMsgid  = 500;
const long long kOwnFileMsgid   = 600;
const long long kAbsentMsgid    = 700;
// 文件字节数，只用于构造气泡
const long long kFileBytes      = 2048;

// 造一个含两条待下载文件的列表：一条对方发来、一条自己发出（历史里自己发过、本地已无副本）。
void FillList(ChatMessageList& list)
{
    list.AppendMessage(MakeIncomingFileMessage(false, _T("对方的文件.docx"), kFileBytes,
                                               kPeerFileMsgid, "11", "md5a", kFilePendingAccept));
    list.AppendMessage(MakeIncomingFileMessage(true, _T("自己的文件.docx"), kFileBytes,
                                               kOwnFileMsgid, "12", "md5b", kFilePendingAccept));
}

// 按 fileMsgid 取气泡数据；找不到时测试失败。
ChatMessage FileMessageOf(const ChatMessageList& list, long long fileMsgid)
{
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = list.Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        const ChatBubble* b = dynamic_cast<const ChatBubble*>(kids[i].get());
        if (b != NULL && b->FileMsgid() == fileMsgid)
        {
            return b->GetMessage();
        }
    }
    FAIL("file bubble not found");
    return ChatMessage();
}

} // anonymous namespace

TEST_CASE("FACK1 下载结果命中对方发来的文件：返回命中且出参为对方发来，气泡变为完成态", "[fileack]")
{
    ChatMessageList list;
    FillList(list);

    bool incoming = false;
    CHECK(list.SetFileDownloadedByMsgid(kPeerFileMsgid, _T("C:\\down\\对方的文件.docx"), &incoming));
    CHECK(incoming);

    const ChatMessage m = FileMessageOf(list, kPeerFileMsgid);
    CHECK(m.fileState == kFileNormal);
    CHECK(m.fileLocalPath == _T("C:\\down\\对方的文件.docx"));
}

TEST_CASE("FACK2 下载结果命中自己发出的文件：返回命中但出参为自己发出", "[fileack]")
{
    ChatMessageList list;
    FillList(list);

    bool incoming = true;
    CHECK(list.SetFileDownloadedByMsgid(kOwnFileMsgid, _T("C:\\down\\自己的文件.docx"), &incoming));
    CHECK_FALSE(incoming);
    CHECK(FileMessageOf(list, kOwnFileMsgid).fileState == kFileNormal);
    // 出参可以不传
    CHECK(list.SetFileDownloadedByMsgid(kOwnFileMsgid, _T("C:\\down\\自己的文件.docx"), NULL));
}

TEST_CASE("FACK3 下载结果不属于本窗口：返回未命中，列表不受影响", "[fileack]")
{
    ChatMessageList list;
    FillList(list);

    bool incoming = true;
    CHECK_FALSE(list.SetFileDownloadedByMsgid(kAbsentMsgid, _T("C:\\down\\x.docx"), &incoming));
    CHECK_FALSE(list.SetFileDownloadedByMsgid(0, _T("C:\\down\\x.docx"), &incoming));
    // 两条文件都还是待下载
    CHECK(FileMessageOf(list, kPeerFileMsgid).fileState == kFilePendingAccept);
    CHECK(FileMessageOf(list, kOwnFileMsgid).fileState == kFilePendingAccept);
}
