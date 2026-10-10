/**
 *  ForwardDirectFileTests.cpp
 *
 *  转发在线直传文件消息的测试（标签 [forwarddirect]），规则见 docs/单聊文件P2P直传与P2P徽章方案.md 第 7 节
 *  （2026-10-05 用户确认）：在线直传的文件服务器上没有副本，按消息原样转发出去对方只拿到一条打不开的记录。
 *
 *    FWDD1 决策表：合并转发一律进入记录、对方看到说明文字；逐条转发本机有文件时改为以离线文件重新发送，
 *          本机没有时跳过并提示「在线直传的文件无法转发」。
 *    FWDD2 聊天窗多选（ChatMessageList::CollectSelectedForward）：逐条转发时本机有文件的直传文件记下路径、
 *          本机没有的单独计数，都不按消息转发；合并转发时照常进入记录。另测本机文件的判定与右键「转发」的拦截条件。
 *    FWDD3 消息记录窗：本地库的归一化 content 记下直传标记（ParseContentBrief 读得出来）；多选
 *          （HistorySelection::BuildForward）按转发模式整理，规则同 FWDD2；取消勾选与清空后不残留。
 *    FWDD4 选会话窗口的预览文字：逐条转发的条数含要重新发送的文件；只重新发送一个文件时显示文件名；
 *          跳过的直传文件、要重新发送的直传文件、合并转发时只留说明文字的直传文件各有一行说明。
 *    FWDD5 合并卡片的预览行：在线直传的文件写成「[文件] 文件名（在线直传的文件，服务器未保存，无法转发）」。
 *    FWDD6 合并记录查看窗：在线直传的文件是一行说明文字，不是可下载的文件卡片；普通文件照常是文件卡片。
 *    FWDD7 单聊发文件时要不要先试在线直传：要求按离线文件上传时不试。
 *
 *  用 Catch2 v3（amalgamated 形态）。FWDD2 在系统临时目录下建一个小文件、用完删除，其余为纯内存断言。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>

#include "../UI/BalloonMain/ForwardRequest.h"
#include "../UI/BalloonMain/ForwardTargetModel.h"
#include "../Feature/FolderTransfer/DirectFileTransfer.h"
#include "../UI/BalloonChatWnd/ChatBubble.h"
#include "../UI/BalloonChatWnd/ChatMessageList.h"
#include "../UI/BalloonChatWnd/ChatMockData.h"
#include "../UI/BalloonChatWnd/ChatDirectFileModel.h"
#include "../UI/BalloonChatWnd/ChatFileStateModel.h"
#include "../UI/BalloonChatWnd/ChatMergedCard.h"
#include "../UI/BalloonHistory/MergedRecordRows.h"
#include "../UI/BalloonHistory/HistorySelection.h"
#include "../UI/BalloonHistory/V2MsgQuery.h"     // BuildContentJson：本地库的归一化 content
#include "../UI/BalloonHistory/HistoryView.h"    // ParseContentBrief
#include "../net/MessageProtocolClient.h"   // net::MergedRecord / parseMergedContent
#include "../net/Protocol.h"                // ::message_type_*

using namespace balloonmain;

namespace {

// 测试用消息区宽度 / 高度（逻辑像素），足够放下本组用例里的全部消息行
const int kListWidth  = 400;
const int kListHeight = 800;
// 测试用本机文件的字节数
const long long kTempFileBytes = 1000;
// 消息时间轴的起点（Unix 毫秒）与间隔；间隔远小于插时间分隔行的阈值，列表只在开头有一行分隔
const long long kT0       = 1700000000000LL;
const long long kMinuteMs = 60LL * 1000;

// 在系统临时目录下建一个 kTempFileBytes 字节的文件，离开作用域时删除
class ScopedTempFile
{
public:
    explicit ScopedTempFile(const wchar_t* name)
    {
        wchar_t dir[MAX_PATH] = { 0 };
        ::GetTempPathW(MAX_PATH, dir);
        m_path = std::wstring(dir) + name;
        HANDLE h = ::CreateFileW(m_path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE)
        {
            std::string data((size_t)kTempFileBytes, 'x');
            DWORD written = 0;
            ::WriteFile(h, data.data(), (DWORD)data.size(), &written, NULL);
            ::CloseHandle(h);
        }
    }

    ~ScopedTempFile()
    {
        ::DeleteFileW(m_path.c_str());
    }

    const std::wstring& Path() const
    {
        return m_path;
    }

private:
    std::wstring m_path;   //文件完整路径
};

// 造一条对方发来的文字消息
balloonchatwnd::ChatMessage MakeText(long long msgid, long long timestampMs)
{
    balloonchatwnd::ChatMessage m = balloonchatwnd::MakeTextMessage(false, _T("转发测试"));
    m.msgid = msgid;
    m.timestampMs = timestampMs;
    return m;
}

// 取第 index 个子控件对应的气泡；不是气泡时测试失败
balloonchatwnd::ChatBubble* BubbleAt(balloonchatwnd::ChatMessageList& list, size_t index)
{
    REQUIRE(index < list.Children().size());
    balloonchatwnd::ChatBubble* b = dynamic_cast<balloonchatwnd::ChatBubble*>(list.Children()[index].get());
    REQUIRE(b != NULL);
    return b;
}

// 判断 text 里是否含有子串 part
bool Contains(const std::wstring& text, const wchar_t* part)
{
    return text.find(part) != std::wstring::npos;
}

// 测试用的两个本机文件路径（只当字符串用，不碰磁盘）
const wchar_t* const kLocalFileA = L"C:\\data\\报告.pdf";
const wchar_t* const kLocalFileB = L"C:\\data\\方案.docx";

} // anonymous namespace

TEST_CASE("FWDD1 转发在线直传文件的决策表", "[forwarddirect]")
{
    //合并转发：不管本机有没有文件，都照常进入记录、只显示说明文字
    CHECK(DecideDirectFileForward(true, true) == kDirectForwardPlaceholder);
    CHECK(DecideDirectFileForward(true, false) == kDirectForwardPlaceholder);
    //逐条转发：本机有文件改为以离线文件重新发送，没有就跳过
    CHECK(DecideDirectFileForward(false, true) == kDirectForwardReupload);
    CHECK(DecideDirectFileForward(false, false) == kDirectForwardSkip);
}

TEST_CASE("FWDD3 消息记录窗：本地库记下直传标记，多选按转发模式分开整理", "[forwarddirect]")
{
    using namespace balloonhistory;

    //本地库的归一化 content：在线直传的文件在 file 子对象里带 direct，普通文件不带
    net::MessageNotify direct;
    direct.m_msgtype = ::message_type_file;
    direct.m_fileName = "报告.pdf";
    direct.m_fileSize = kTempFileBytes;
    direct.m_fileMd5 = "0123456789abcdef0123456789abcdef";
    direct.m_fileDirect = true;
    CHECK(ParseContentBrief(::message_type_file, BuildContentJson(direct)).m_fileDirect);
    net::MessageNotify offline = direct;
    offline.m_fileDirect = false;
    offline.m_fileFileId = "77";
    CHECK_FALSE(ParseContentBrief(::message_type_file, BuildContentJson(offline)).m_fileDirect);

    //[文字31, 直传32（本机有）, 直传33（本机没有）, 离线文件34]
    const std::wstring localPath = kLocalFileA;
    HistorySelection sel(net::kForwardMaxMessages);
    sel.Toggle(31, ::message_type_text, NULL);
    sel.Toggle(32, ::message_type_file, true, localPath, NULL);
    sel.Toggle(33, ::message_type_file, true, std::wstring(), NULL);
    sel.Toggle(34, ::message_type_file, NULL);

    ForwardRequest each;
    sel.BuildForward(false, each);
    CHECK_FALSE(each.m_merge);
    REQUIRE(each.m_msgids.size() == 2);
    CHECK(each.m_msgids[0] == 31);
    CHECK(each.m_msgids[1] == 34);
    REQUIRE(each.m_reuploadFiles.size() == 1);
    CHECK(each.m_reuploadFiles[0] == localPath);
    CHECK(each.m_skippedDirect == 1);
    CHECK(each.m_skipped == 0);

    ForwardRequest merged;
    sel.BuildForward(true, merged);
    CHECK(merged.m_merge);
    CHECK(merged.m_msgids.size() == 4);
    CHECK(merged.m_reuploadFiles.empty());
    CHECK(merged.m_skippedDirect == 0);
    CHECK(merged.m_directPlaceholders == 2);

    //取消勾选后不再计入；清空后也不残留
    sel.Toggle(32, ::message_type_file, true, localPath, NULL);
    ForwardRequest after;
    sel.BuildForward(false, after);
    CHECK(after.m_reuploadFiles.empty());
    CHECK(after.m_skippedDirect == 1);
    sel.Clear();
    sel.Toggle(33, ::message_type_file, NULL);
    ForwardRequest cleared;
    sel.BuildForward(false, cleared);
    CHECK(cleared.m_skippedDirect == 0);
    CHECK(cleared.m_msgids.size() == 1);
}

TEST_CASE("FWDD4 选会话窗口的预览文字", "[forwarddirect]")
{
    SECTION("逐条转发：条数含要重新发送的文件，并说明会以离线文件重新发送")
    {
        ForwardRequest r;
        r.m_msgids.push_back(101);
        r.m_msgids.push_back(102);
        r.m_reuploadFiles.push_back(kLocalFileA);
        const std::wstring text = BuildForwardPreviewText(r);
        CHECK(Contains(text, L"逐条转发 3 条消息"));
        CHECK(Contains(text, L"1 个在线直传的文件将以离线文件重新发送"));
        CHECK_FALSE(Contains(text, L"无法转发"));
    }

    SECTION("只重新发送一个文件、没有别的消息：第一行显示文件名")
    {
        ForwardRequest r;
        r.m_reuploadFiles.push_back(kLocalFileA);
        const std::wstring text = BuildForwardPreviewText(r);
        CHECK(text.compare(0, wcslen(L"[文件] 报告.pdf"), L"[文件] 报告.pdf") == 0);
    }

    SECTION("本机已没有的直传文件：单独一行「在线直传的文件无法转发」，不混进已跳过的条数")
    {
        ForwardRequest r;
        r.m_msgids.push_back(101);
        r.m_reuploadFiles.push_back(kLocalFileA);
        r.m_reuploadFiles.push_back(kLocalFileB);
        r.m_skipped = 1;
        r.m_skippedDirect = 2;
        //每一行都要放得下选会话窗口右侧的说明区（2026-10-05 联调时较长的两行被截断），故逐字比较整段
        CHECK(BuildForwardPreviewText(r) == std::wstring(L"逐条转发 3 条消息\n"
                                                         L"已跳过 1 条不能转发的消息\n"
                                                         L"在线直传的文件无法转发：2 个\n"
                                                         L"2 个在线直传的文件将以离线文件重新发送"));
    }

    SECTION("合并转发：说明直传文件不随记录发出")
    {
        ForwardRequest r;
        r.m_merge = true;
        r.m_title = L"张三和李四的聊天记录";
        r.m_msgids.push_back(101);
        r.m_msgids.push_back(102);
        r.m_directPlaceholders = 1;
        CHECK(BuildForwardPreviewText(r) == std::wstring(L"[聊天记录] 张三和李四的聊天记录\n"
                                                         L"1 个在线直传的文件不随记录转发"));
    }

    SECTION("没有在线直传文件时与原来的输出相同")
    {
        ForwardRequest r;
        r.m_msgids.push_back(101);
        r.m_skipped = 2;
        CHECK(BuildForwardPreviewText(r) == std::wstring(L"逐条转发 1 条消息\n已跳过 2 条不能转发的消息"));
    }
}

TEST_CASE("FWDD2 聊天窗多选：在线直传的文件按转发模式分开整理", "[forwarddirect]")
{
    using namespace balloonchatwnd;
    ScopedTempFile local(L"fwdd2_direct_local.bin");

    //本端发出、直传成功、源文件还在
    ChatMessage directLocal = MakeFileMessage(true, _T("报告.pdf"), kTempFileBytes, CString(local.Path().c_str()),
                                              "cmid-1", kFileNormal);
    directLocal.fileDirect = true;
    directLocal.fileMsgid = 12;
    directLocal.timestampMs = kT0 + 2 * kMinuteMs;
    //对方发来、直传成功，但本机已没有这份文件（历史里显示「在线直传，服务器未保存」）
    ChatMessage directGone = MakeIncomingFileMessage(false, _T("方案.docx"), kTempFileBytes, 13, std::string(), "md5",
                                                     kFileDeleted);
    directGone.fileDirect = true;
    directGone.timestampMs = kT0 + 3 * kMinuteMs;
    //普通离线文件：照常按消息转发
    ChatMessage offline = MakeIncomingFileMessage(false, _T("排期表.xlsx"), kTempFileBytes, 14, "77", "md5", kFileNormal);
    offline.timestampMs = kT0 + 4 * kMinuteMs;

    //[分隔, 文字11, 直传12（本机有）, 直传13（本机没有）, 离线文件14]
    ChatMessageList list;
    list.AppendMessage(MakeText(11, kT0 + kMinuteMs));
    list.AppendMessage(directLocal);
    list.AppendMessage(directGone);
    list.AppendMessage(offline);
    RECT rc = { 0, 0, kListWidth, kListHeight };
    list.ForceLayout(rc);
    REQUIRE(list.Children().size() == 5);
    list.EnterMultiSelectWith(BubbleAt(list, 1));
    for (size_t i = 2; i <= 4; ++i)
    {
        BubbleAt(list, i)->SetSelected(true);
    }

    //逐条转发：直传文件不按消息转发；本机有的记下路径，本机没有的单独计数
    const ChatForwardRequest each = list.CollectSelectedForward(false);
    CHECK_FALSE(each.m_merge);
    REQUIRE(each.m_msgids.size() == 2);
    CHECK(each.m_msgids[0] == 11);
    CHECK(each.m_msgids[1] == 14);
    REQUIRE(each.m_reuploadFiles.size() == 1);
    CHECK(each.m_reuploadFiles[0] == local.Path());
    CHECK(each.m_skippedDirect == 1);
    CHECK(each.m_skipped == 0);
    CHECK(each.m_directPlaceholders == 0);

    //合并转发：直传文件照常进入记录，计入只留说明文字的条数
    const ChatForwardRequest merged = list.CollectSelectedForward(true);
    CHECK(merged.m_merge);
    REQUIRE(merged.m_msgids.size() == 4);
    CHECK(merged.m_msgids[1] == 12);
    CHECK(merged.m_msgids[2] == 13);
    CHECK(merged.m_reuploadFiles.empty());
    CHECK(merged.m_skippedDirect == 0);
    CHECK(merged.m_directPlaceholders == 2);

    //本机文件的判定：存在且大小相符才算；路径为空、文件不存在、大小不符都不算
    CHECK(DirectFileHasLocalCopy(local.Path(), kTempFileBytes));
    CHECK(DirectFileHasLocalCopy(local.Path(), 0));
    CHECK_FALSE(DirectFileHasLocalCopy(local.Path(), kTempFileBytes + 1));
    CHECK_FALSE(DirectFileHasLocalCopy(std::wstring(), kTempFileBytes));
    CHECK_FALSE(DirectFileHasLocalCopy(local.Path() + L".missing", kTempFileBytes));

    //右键「转发」：只有本机已没有文件的在线直传文件直接提示无法转发
    CHECK(DirectFileForwardBlocked(true, true, false));
    CHECK_FALSE(DirectFileForwardBlocked(true, true, true));
    CHECK_FALSE(DirectFileForwardBlocked(true, false, false));
    CHECK_FALSE(DirectFileForwardBlocked(false, true, false));
}

namespace {

// 一份合并记录：文字、在线直传的文件、普通离线文件各一条。直传文件的 content 与服务端留下的直传文件消息相同
const char* const kMergedWithDirect =
    "{\"title\":\"张三和李四的聊天记录\",\"messages\":["
    "{\"msgid\":\"21\",\"from\":\"100001\",\"msgtype\":1,\"content\":{\"text\":\"方案改好了\"},\"fromname\":\"张三\"},"
    "{\"msgid\":\"22\",\"from\":\"100001\",\"msgtype\":3,\"content\":{\"filename\":\"报告.pdf\",\"filesize\":1000,"
    "\"md5\":\"0123456789abcdef0123456789abcdef\",\"direct\":1,\"sessionid\":\"5127-5308-1-1\",\"result\":3,"
    "\"linkpath\":1},\"fromname\":\"张三\"},"
    "{\"msgid\":\"23\",\"from\":\"100002\",\"msgtype\":3,\"content\":{\"fileid\":\"77\",\"filename\":\"排期表.xlsx\","
    "\"filesize\":2048,\"md5\":\"fedcba9876543210fedcba9876543210\"},\"fromname\":\"李四\"}]}";

// 直传文件那一行应显示的说明文字
const wchar_t* const kDirectPlaceholder = L"[文件] 报告.pdf（在线直传的文件，服务器未保存，无法转发）";

} // anonymous namespace

TEST_CASE("FWDD5 合并卡片的预览：在线直传的文件写成说明文字", "[forwarddirect]")
{
    using namespace balloonchatwnd;
    CHECK(MergedDirectFilePlaceholder("报告.pdf") == CString(kDirectPlaceholder));

    MergedCardPreview preview;
    REQUIRE(BuildMergedCardPreview(kMergedWithDirect, preview));
    CHECK(preview.m_count == 3);
    REQUIRE(preview.m_lines.size() == 3);
    CHECK(preview.m_lines[0] == _T("张三：方案改好了"));
    CHECK(preview.m_lines[1] == CString(L"张三：") + kDirectPlaceholder);
    //普通离线文件不受影响，仍是类型占位
    CHECK(preview.m_lines[2] == _T("李四：[文件]"));
}

TEST_CASE("FWDD6 合并记录查看窗：在线直传的文件是一行说明文字，不是可下载的文件卡片", "[forwarddirect]")
{
    using namespace balloonhistory;
    net::MergedRecord record;
    REQUIRE(net::MessageProtocolClient::parseMergedContent(kMergedWithDirect, record));
    REQUIRE(record.m_messages.size() == 3);
    REQUIRE(record.m_messages[1].m_fileDirect);

    std::vector<HistoryRowView> rows;
    BuildMergedRecordRows(record, std::map<int64_t, CString>(), 0, rows);
    REQUIRE(rows.size() == 3);
    //直传文件：没有文件卡片、没有文件名与大小那一套，正文就是说明文字
    CHECK_FALSE(rows[1].m_chatFileCard);
    CHECK(rows[1].m_previewText == CString(kDirectPlaceholder));
    CHECK(rows[1].m_fullText == CString(kDirectPlaceholder));
    //普通离线文件照常画成可下载的文件卡片
    CHECK(rows[2].m_chatFileCard);
}

TEST_CASE("FWDD7 单聊发文件时要求按离线文件上传就不试在线直传", "[forwarddirect]")
{
    CHECK(foldertransfer::ShouldTryDirectFileSend(false, true, false));
    CHECK_FALSE(foldertransfer::ShouldTryDirectFileSend(false, true, true));
    CHECK_FALSE(foldertransfer::ShouldTryDirectFileSend(true, true, false));
    CHECK_FALSE(foldertransfer::ShouldTryDirectFileSend(false, false, false));
}
