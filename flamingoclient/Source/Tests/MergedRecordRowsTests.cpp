/**
 *  MergedRecordRowsTests.cpp
 *
 *  合并记录查看窗的行数据（balloonhistory::BuildMergedRecordRows）测试，标签 [forward]，
 *  见 docs/聊天消息转发方案.md §5.6。
 *
 *    FWD-C16a 发送者名字：fromname 优先，没有时查回退名字表，再没有写「用户」+ userid。
 *    FWD-C16b 各类子消息的行内容：文本保留内联表情标记；文件行有类型色块、文件名与大小；
 *             图片行画预览图并带原图宽高；嵌套记录写「[聊天记录] 标题」，没有标题时只写
 *             「[聊天记录]」；每行带原消息 msgid，行数与子消息数相同。
 *    FWD-C16c 日期分组头：第一行有，同一天的后续行没有，换了一天的行再有；"今天"写成「今天」。
 *    FWD-C16d 聊天记录卡片的行控件：嵌套记录行有编号为 kIdHistoryCardBase + 行下标的标题链接，
 *             其余行没有。
 *    FWD-C16e 列表不预留操作列（查看窗用）：行内没有 Pin / 定位按钮，正文列更宽，分到的
 *             行高仍等于按实际宽度排出的高度。
 *    FWD-C18a 查看窗文件行的初始状态（BuildMergedFileCard）：本地有完整文件为已完成，给打开 /
 *             打开所在文件夹 / 另存为；没有为未下载，只给下载；行数据的文件行改用聊天文件卡片。
 *    FWD-C18b 查看窗文件下载的状态表（MergedFileStateTable）：下载中、进度、完成、失败、取消。
 *    FWD-C18d 查看窗文件行的行高：卡片控件的宽高与卡片应有的尺寸一致，完整落在行内。
 *    FWD-C17  消息记录窗识别合并记录（类型 15）：预览与正文写「[聊天记录] 标题」；从本地库
 *             content 里取出卡片要用的标题、条数与可交给查看窗的原文。
 *
 *  用 Catch2 v3（amalgamated 形态）。纯内存：不创建窗口、不要 HWND。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <map>
#include <memory>
#include <vector>

#include "../UI/BalloonHistory/MergedRecordRows.h"
#include "../UI/BalloonHistory/HistoryRowFormat.h"     // V2V-SYS2：系统提示行的发送人与头像底色
#include "../net/Protocol.h"                           // V2V-SYS2：::message_type_*
#include "../UI/BalloonHistory/HistoryWindowLayout.h"   // BuildListContent / kIdHistoryCardBase
#include "../UI/BalloonHistory/HistoryContentView.h"    // 行内正文内容块（量正文宽度与高度）
#include "../UI/BalloonChatWnd/ChatFileCard.h"          // FileCardHeight / FileCardSkipsReject / kFileCardWidth
#include "../UI/BalloonChatWnd/ChatFileCardControl.h"   // 查看窗文件行里的聊天文件卡片控件
#include "../UI/BalloonChatWnd/ChatFileAction.h"        // kChatFileOpen 等动作取值
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../UI/BalloonHistory/HistoryRowFormat.h"
#include "../UI/BalloonHistory/HistoryView.h"     // FormatFileSize / FormatDateGroupLabel
#include "../net/MessageProtocolClient.h"         // net::MergedRecord / parseMergedContent

using namespace balloonhistory;

namespace {

// 子消息的时间（Unix 毫秒）。前三条在同一天、相隔一分钟；后四条在两天之后，同样相隔一分钟。
const int64_t kDay1Ms = 1700000000000LL;
const int64_t kDay3Ms = 1700172800000LL;

// 一份含各类子消息的合并记录 content。时间写成字面量，必须与上面两个常量一致。
const char* const kRowsContent =
    "{\"title\":\"项目周会的聊天记录\",\"messages\":["
    "{\"msgid\":\"11\",\"from\":\"100001\",\"msgtype\":1,\"timestamp\":1700000000000,"
    "\"content\":{\"text\":\"你好/f[\\\"5\\\"]\"},\"fromname\":\"张三\"},"
    "{\"msgid\":\"12\",\"from\":\"100004\",\"msgtype\":1,\"timestamp\":1700000060000,"
    "\"content\":{\"text\":\"没有名字\"}},"
    "{\"msgid\":\"13\",\"from\":\"100005\",\"msgtype\":1,\"timestamp\":1700000120000,"
    "\"content\":{\"text\":\"查回退名字\"}},"
    "{\"msgid\":\"14\",\"from\":\"100002\",\"msgtype\":3,\"timestamp\":1700172800000,"
    "\"content\":{\"fileid\":\"7\",\"filename\":\"周报.xlsx\",\"filesize\":2048,\"md5\":\"abc\"},"
    "\"fromname\":\"李四\"},"
    "{\"msgid\":\"15\",\"from\":\"100002\",\"msgtype\":2,\"timestamp\":1700172860000,"
    "\"content\":{\"fileid\":\"9\",\"md5\":\"def\",\"width\":640,\"height\":480},\"fromname\":\"李四\"},"
    "{\"msgid\":\"16\",\"from\":\"100001\",\"msgtype\":15,\"timestamp\":1700172920000,"
    "\"content\":{\"title\":\"内层记录\",\"messages\":[]},\"fromname\":\"张三\"},"
    "{\"msgid\":\"17\",\"from\":\"100001\",\"msgtype\":15,\"timestamp\":1700172980000,"
    "\"content\":{\"messages\":[]},\"fromname\":\"张三\"}]}";

// 解析上面那份 content 并生成各行。fallbackNames：回退名字表；todayKey："今天"的日期键。
std::vector<HistoryRowView> BuildRows(const std::map<int64_t, CString>& fallbackNames,
                                      int64_t todayKey)
{
    net::MergedRecord record;
    REQUIRE(net::MessageProtocolClient::parseMergedContent(kRowsContent, record));
    std::vector<HistoryRowView> rows;
    BuildMergedRecordRows(record, fallbackNames, todayKey, rows);
    return rows;
}

// UTF-8 转 CString，用来拼期望值。
CString Utf8ToCStr(const std::string& utf8)
{
    return CString(CA2W(utf8.c_str(), CP_UTF8));
}

} // anonymous namespace

TEST_CASE("FWD-C16a 合并记录查看窗的发送者名字：fromname 优先，再查回退表，最后写用户 id", "[forward]")
{
    std::map<int64_t, CString> fallback;
    fallback[100005] = _T("王五");
    // fromname 有值时不看回退表，即便回退表里也有这个人
    fallback[100001] = _T("回退表里的张三");

    const std::vector<HistoryRowView> rows = BuildRows(fallback, LocalDayKey(kDay3Ms));
    REQUIRE(rows.size() == 7);
    CHECK(rows[0].m_senderName == _T("张三"));
    CHECK(rows[1].m_senderName == _T("用户100004"));
    CHECK(rows[2].m_senderName == _T("王五"));

    // 单独核对名字函数：回退表里的空名字等同于没有
    std::map<int64_t, CString> emptyName;
    emptyName[100009] = CString();
    CHECK(ResolveMergedSenderName(std::string(), 100009, emptyName) == _T("用户100009"));
}

TEST_CASE("FWD-C16b 合并记录查看窗各类子消息的行内容", "[forward]")
{
    const std::vector<HistoryRowView> rows =
        BuildRows(std::map<int64_t, CString>(), LocalDayKey(kDay3Ms));
    REQUIRE(rows.size() == 7);

    // 每行带原消息 msgid，查看窗不涉及 Pin
    CHECK(rows[0].m_msgid == 11);
    CHECK(rows[6].m_msgid == 17);
    CHECK_FALSE(rows[0].m_pinned);

    // 文本：正文保留内联表情标记，交给行内容块画成表情
    CHECK(rows[0].m_fullText == _T("你好/f[\"5\"]"));
    CHECK(rows[0].m_thumbFileExt.IsEmpty());
    CHECK_FALSE(rows[0].m_hasImageThumb);

    // 文件：类型色块、文件名、大小，正文写成"名字 · 大小"
    const CString sizeText = Utf8ToCStr(FormatFileSize(2048));
    CHECK(rows[3].m_thumbFileExt == _T("xlsx"));
    CHECK(rows[3].m_fileName == _T("周报.xlsx"));
    CHECK(rows[3].m_fileSizeText == sizeText);
    CHECK(rows[3].m_fullText == CString(_T("周报.xlsx · ")) + sizeText);

    // 图片：画预览图，带原图宽高；本地路径由查看窗补，这里为空
    CHECK(rows[4].m_hasImageThumb);
    CHECK(rows[4].m_imageWidth == 640);
    CHECK(rows[4].m_imageHeight == 480);
    CHECK(rows[4].m_thumbImagePath.IsEmpty());

    // 嵌套记录：画成聊天记录卡片，带标题与条数；正文写「[聊天记录] 标题」，没有标题时只写
    //「[聊天记录]」
    CHECK(rows[5].m_isMergedCard);
    CHECK(rows[5].m_mergedTitle == _T("内层记录"));
    CHECK(rows[5].m_mergedCount == 0);
    CHECK(rows[5].m_fullText == _T("[聊天记录] 内层记录"));
    CHECK(rows[6].m_isMergedCard);
    CHECK(rows[6].m_mergedTitle.IsEmpty());
    CHECK(rows[6].m_fullText == _T("[聊天记录]"));
    CHECK(MergedNestedRowText("not json") == _T("[聊天记录]"));
    // 其余类型的行不是卡片
    CHECK_FALSE(rows[0].m_isMergedCard);
    CHECK_FALSE(rows[3].m_isMergedCard);
}

TEST_CASE("FWD-C16d 聊天记录卡片的行控件：只有嵌套记录行有可点击的标题链接", "[forward]")
{
    const std::vector<HistoryRowView> rows =
        BuildRows(std::map<int64_t, CString>(), LocalDayKey(kDay3Ms));
    REQUIRE(rows.size() == 7);

    // 列表宽度取一个中等窗口的宽度即可，本用例只看控件有没有建出来
    const int kListWidthPx = 600;
    std::unique_ptr<balloonwjui::DuiControl> list =
        BuildListContent(rows, _T(""), _T(""), kListWidthPx);
    REQUIRE(list);

    // 第 5、6 行是嵌套记录：各有一个编号为「基址 + 行下标」的标题链接，标题缺失时写「聊天记录」
    balloonwjui::DuiLabel* title5 = dynamic_cast<balloonwjui::DuiLabel*>(
        list->FindCtrlById((UINT)(kIdHistoryCardBase + 5)));
    REQUIRE(title5 != NULL);
    CHECK(CString(title5->GetText()) == _T("内层记录"));
    balloonwjui::DuiLabel* title6 = dynamic_cast<balloonwjui::DuiLabel*>(
        list->FindCtrlById((UINT)(kIdHistoryCardBase + 6)));
    REQUIRE(title6 != NULL);
    CHECK(CString(title6->GetText()) == _T("聊天记录"));
    // 其余行没有
    CHECK(list->FindCtrlById((UINT)(kIdHistoryCardBase + 0)) == NULL);
    CHECK(list->FindCtrlById((UINT)(kIdHistoryCardBase + 3)) == NULL);
}

TEST_CASE("FWD-C16c 合并记录查看窗的日期分组头", "[forward]")
{
    const int64_t todayKey = LocalDayKey(kDay3Ms);
    const std::vector<HistoryRowView> rows = BuildRows(std::map<int64_t, CString>(), todayKey);
    REQUIRE(rows.size() == 7);

    // 第一行恒有分组头，写的是那一天的日期（不是今天、也不是昨天）
    CHECK(rows[0].m_groupLabel == Utf8ToCStr(FormatDateGroupLabel(LocalDayKey(kDay1Ms), todayKey)));
    CHECK(rows[0].m_groupLabel != _T("今天"));
    CHECK(rows[0].m_groupLabel != _T("昨天"));
    // 同一天的后续行没有
    CHECK(rows[1].m_groupLabel.IsEmpty());
    CHECK(rows[2].m_groupLabel.IsEmpty());
    // 换了一天的第一行再有，且这一天就是今天
    CHECK(rows[3].m_groupLabel == _T("今天"));
    CHECK(rows[4].m_groupLabel.IsEmpty());
    CHECK(rows[6].m_groupLabel.IsEmpty());
}

// 本地库里一条合并记录的 content：转发时服务端下发的 content 原文整体放在 merged 子对象里
//（见 V2MsgQuery.cpp 的 BuildContentJson）。
const char* const kLocalMergedContent =
    "{\"merged\":{\"title\":\"项目周会的聊天记录\",\"messages\":["
    "{\"msgid\":\"1\",\"from\":\"100001\",\"msgtype\":1,\"content\":{\"text\":\"第一条\"},\"fromname\":\"张三\"},"
    "{\"msgid\":\"2\",\"from\":\"100002\",\"msgtype\":1,\"content\":{\"text\":\"第二条\"},\"fromname\":\"李四\"}]}}";

TEST_CASE("FWD-C17 消息记录窗识别合并记录：预览文字与卡片所需的标题、条数、原文", "[forward]")
{
    const int kMsgTypeMerged = 15;

    // 预览与完整正文都写「[聊天记录] 标题」；标题缺失或 content 不可用时只写「[聊天记录]」
    CHECK(PreviewFromContent(kMsgTypeMerged, kLocalMergedContent) == "[聊天记录] 项目周会的聊天记录");
    CHECK(FullTextFromContent(kMsgTypeMerged, kLocalMergedContent) == "[聊天记录] 项目周会的聊天记录");
    CHECK(PreviewFromContent(kMsgTypeMerged, "{\"merged\":{}}") == "[聊天记录]");
    CHECK(PreviewFromContent(kMsgTypeMerged, "not json") == "[聊天记录]");

    // 卡片所需：标题、子消息条数，以及可交给查看窗的 content 原文
    const HistoryContentBrief brief = ParseContentBrief(kMsgTypeMerged, kLocalMergedContent);
    CHECK(brief.m_mergedTitle == "项目周会的聊天记录");
    CHECK(brief.m_mergedCount == 2);
    net::MergedRecord record;
    REQUIRE(net::MessageProtocolClient::parseMergedContent(brief.m_mergedContent, record));
    CHECK(record.m_title == "项目周会的聊天记录");
    REQUIRE(record.m_messages.size() == 2);
    CHECK(record.m_messages[1].m_fromName == "李四");

    // 不是合并记录的消息，这几项都为空
    const HistoryContentBrief text = ParseContentBrief(1, "{\"text\":\"你好\"}");
    CHECK(text.m_mergedTitle.empty());
    CHECK(text.m_mergedCount == 0);
    CHECK(text.m_mergedContent.empty());
}

namespace {

// 在控件树里找第一个正文内容块（行内显示正文的 HistoryContentView）；找不到返回 NULL。
const HistoryContentView* FindFirstContentView(const balloonwjui::DuiControl* node)
{
    if (node == NULL)
    {
        return NULL;
    }
    const HistoryContentView* view = dynamic_cast<const HistoryContentView*>(node);
    if (view != NULL)
    {
        return view;
    }
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = node->Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        const HistoryContentView* found = FindFirstContentView(kids[i].get());
        if (found != NULL)
        {
            return found;
        }
    }
    return NULL;
}

// 按给定的列表宽度建一行长文并排版，返回行容器（列表由 holder 持有）。
//   reserveRowActions：是否给 Pin / 定位两个按钮预留位置
const balloonwjui::DuiControl* BuildLongTextRow(std::unique_ptr<balloonwjui::DuiControl>& holder,
                                                bool reserveRowActions)
{
    // 列表宽度与排版高度（像素）：宽度取查看窗的默认宽度，高度足够放下这一行
    const int kListWidthPx  = 600;
    const int kLayoutHeight = 2000;
    HistoryRowView row;
    row.m_senderName = _T("张三");
    row.m_timeText   = _T("09:30");
    row.m_fullText   = _T("今天的安排：上午把接口联调完，下午提测。测试环境的数据库昨晚重置过，")
                       _T("账号还是原来那几个。如果联调时发现字段对不上，先在群里说一声。");
    row.m_previewText = row.m_fullText;
    std::vector<HistoryRowView> rows(1, row);
    holder = BuildListContent(rows, _T(""), _T(""), kListWidthPx, reserveRowActions);
    RECT rc = { 0, 0, kListWidthPx, kLayoutHeight };
    holder->ForceLayout(rc);
    return holder->FindCtrlById((UINT)kIdHistoryRowBase);
}

} // anonymous namespace

TEST_CASE("FWD-C16e 不预留操作列：没有 Pin / 定位按钮，正文更宽，行高与实际排版一致", "[forward]")
{
    std::unique_ptr<balloonwjui::DuiControl> withActions;
    std::unique_ptr<balloonwjui::DuiControl> withoutActions;
    const balloonwjui::DuiControl* rowA = BuildLongTextRow(withActions, true);
    const balloonwjui::DuiControl* rowB = BuildLongTextRow(withoutActions, false);
    REQUIRE(rowA != NULL);
    REQUIRE(rowB != NULL);

    // 预留时两个按钮都在，不预留时都不建
    CHECK(withActions->FindCtrlById((UINT)kIdHistoryPinBase) != NULL);
    CHECK(withActions->FindCtrlById((UINT)kIdHistoryLocateBase) != NULL);
    CHECK(withoutActions->FindCtrlById((UINT)kIdHistoryPinBase) == NULL);
    CHECK(withoutActions->FindCtrlById((UINT)kIdHistoryLocateBase) == NULL);

    const HistoryContentView* viewA = FindFirstContentView(rowA);
    const HistoryContentView* viewB = FindFirstContentView(rowB);
    REQUIRE(viewA != NULL);
    REQUIRE(viewB != NULL);
    const RECT rcA = viewA->GetRect();
    const RECT rcB = viewB->GetRect();
    const int widthA = rcA.right - rcA.left;
    const int widthB = rcB.right - rcB.left;
    // 不预留时正文列拿到了那两列的宽度
    CHECK(widthB > widthA);
    // 两种情况下，分到的正文高度都等于按实际宽度排出的高度：测量宽度与实际宽度一致
    CHECK((rcA.bottom - rcA.top) == viewA->MeasureHeight(widthA));
    CHECK((rcB.bottom - rcB.top) == viewB->MeasureHeight(widthB));
}

namespace {

// 查看窗文件卡片用例的子消息：一条文件消息（msgid 14，fileid 7）
const int64_t   kCardFileMsgid   = 14;
const long long kCardFileBytes   = 2048;
// 下载进度（百分比）
const int       kCardProgressPct = 37;

// 造一条文件子消息（取 kRowsContent 里的第 4 条，即 msgid 14 的周报.xlsx）。
net::MessageNotify CardFileSub()
{
    net::MergedRecord record;
    REQUIRE(net::MessageProtocolClient::parseMergedContent(kRowsContent, record));
    REQUIRE(record.m_messages.size() > 3);
    return record.m_messages[3];
}

// 取卡片当前给出的操作。out 须有 kMaxChatFileActions 个元素，返回个数。
int CardActions(const balloonchatwnd::ChatMessage& card, int out[balloonchatwnd::kMaxChatFileActions])
{
    return balloonchatwnd::FileButtonActions(card.fileState,
                                             balloonchatwnd::FileCardSkipsReject(card), out);
}

} // anonymous namespace

TEST_CASE("FWD-C18a 查看窗文件行的初始状态：本地有完整文件为已完成，没有为未下载", "[forward][filecard]")
{
    const net::MessageNotify sub = CardFileSub();
    int acts[balloonchatwnd::kMaxChatFileActions] = { 0 };

    // 本地没有：「未下载」，只给「下载」；下载所需的 msgid / fileid / md5 / 大小都带上
    const balloonchatwnd::ChatMessage pending = BuildMergedFileCard(sub, std::wstring());
    CHECK(pending.kind == balloonchatwnd::kMsgFile);
    CHECK_FALSE(pending.fromMe);
    CHECK(pending.fileDirectDownload);
    CHECK(pending.fileState == balloonchatwnd::kFilePendingAccept);
    CHECK(pending.fileMsgid == kCardFileMsgid);
    CHECK(pending.fileFileId == "7");
    CHECK(pending.fileMd5 == "abc");
    CHECK(pending.fileSizeBytes == kCardFileBytes);
    CHECK(pending.fileName == _T("周报.xlsx"));
    REQUIRE(CardActions(pending, acts) == 1);
    CHECK(acts[0] == balloonchatwnd::kChatFileAccept);

    // 本地已有：「已完成」，给打开 / 打开所在文件夹 / 另存为，本地路径填好
    const balloonchatwnd::ChatMessage done = BuildMergedFileCard(sub, L"C:\\down\\周报.xlsx");
    CHECK(done.fileState == balloonchatwnd::kFileNormal);
    CHECK(done.fileLocalPath == _T("C:\\down\\周报.xlsx"));
    CHECK(done.fileMsgid == kCardFileMsgid);
    REQUIRE(CardActions(done, acts) == 3);
    CHECK(acts[0] == balloonchatwnd::kChatFileOpen);
    CHECK(acts[1] == balloonchatwnd::kChatFileOpenFolder);
    CHECK(acts[2] == balloonchatwnd::kChatFileSaveAs);

    // 行数据：文件行改用聊天文件卡片，卡片初始为「未下载」（本地路径由查看窗补）
    const std::vector<HistoryRowView> rows =
        BuildRows(std::map<int64_t, CString>(), LocalDayKey(kDay3Ms));
    REQUIRE(rows.size() == 7);
    CHECK(rows[3].m_chatFileCard);
    CHECK(rows[3].m_fileCard.fileMsgid == kCardFileMsgid);
    CHECK(rows[3].m_fileCard.fileState == balloonchatwnd::kFilePendingAccept);
    CHECK_FALSE(rows[0].m_chatFileCard);
    CHECK_FALSE(rows[4].m_chatFileCard);
}

TEST_CASE("FWD-C18b 查看窗文件下载的状态切换：下载中、进度、完成、失败、取消", "[forward][filecard]")
{
    const net::MessageNotify sub = CardFileSub();
    MergedFileStateTable table;
    int acts[balloonchatwnd::kMaxChatFileActions] = { 0 };

    // 没发起过下载：卡片保持原样
    balloonchatwnd::ChatMessage card = BuildMergedFileCard(sub, std::wstring());
    table.Apply(card);
    CHECK(card.fileState == balloonchatwnd::kFilePendingAccept);
    CHECK_FALSE(table.IsDownloading(kCardFileMsgid));

    // 点了「下载」：下载中、进度 0，只给「取消」
    table.Start(kCardFileMsgid);
    CHECK(table.IsDownloading(kCardFileMsgid));
    card = BuildMergedFileCard(sub, std::wstring());
    table.Apply(card);
    CHECK(card.fileState == balloonchatwnd::kFileDownloading);
    CHECK(card.fileProgress == 0);
    REQUIRE(CardActions(card, acts) == 1);
    CHECK(acts[0] == balloonchatwnd::kChatFileCancel);

    // 进度更新；别的 msgid 的进度不影响本条，也不会凭空记一条
    table.Progress(kCardFileMsgid, kCardProgressPct);
    table.Progress(kCardFileMsgid + 1, kCardProgressPct);
    CHECK_FALSE(table.IsDownloading(kCardFileMsgid + 1));
    card = BuildMergedFileCard(sub, std::wstring());
    table.Apply(card);
    CHECK(card.fileProgress == kCardProgressPct);

    // 完成：已完成、本地路径填好，不再是下载中
    table.Finish(kCardFileMsgid, true, L"C:\\down\\周报.xlsx", false);
    CHECK_FALSE(table.IsDownloading(kCardFileMsgid));
    card = BuildMergedFileCard(sub, std::wstring());
    table.Apply(card);
    CHECK(card.fileState == balloonchatwnd::kFileNormal);
    CHECK(card.fileLocalPath == _T("C:\\down\\周报.xlsx"));

    // 失败：传输失败，进度停在中断处，只给「重新下载」
    table.Start(kCardFileMsgid);
    table.Progress(kCardFileMsgid, kCardProgressPct);
    table.Finish(kCardFileMsgid, false, std::wstring(), false);
    card = BuildMergedFileCard(sub, std::wstring());
    table.Apply(card);
    CHECK(card.fileState == balloonchatwnd::kFileFailed);
    CHECK(card.fileProgress == kCardProgressPct);
    REQUIRE(CardActions(card, acts) == 1);
    CHECK(acts[0] == balloonchatwnd::kChatFileRetry);

    // 取消：已取消，只给「重新下载」
    table.Start(kCardFileMsgid);
    table.Finish(kCardFileMsgid, false, std::wstring(), true);
    card = BuildMergedFileCard(sub, std::wstring());
    table.Apply(card);
    CHECK(card.fileState == balloonchatwnd::kFileCancelled);
    REQUIRE(CardActions(card, acts) == 1);
    CHECK(acts[0] == balloonchatwnd::kChatFileRetry);

    // 别的文件的卡片不受这张表里本条记录的影响
    balloonchatwnd::ChatMessage other = BuildMergedFileCard(sub, std::wstring());
    other.fileMsgid = kCardFileMsgid + 1;
    table.Apply(other);
    CHECK(other.fileState == balloonchatwnd::kFilePendingAccept);
}

TEST_CASE("FWD-C18d 查看窗文件行的行高：名字行加完整卡片，各状态都不被裁切", "[forward][filecard]")
{
    // 列表宽度与排版高度（像素）
    const int kListWidthPx  = 600;
    const int kLayoutHeight = 2000;
    const net::MessageNotify sub = CardFileSub();

    const int states[] = { balloonchatwnd::kFilePendingAccept, balloonchatwnd::kFileDownloading,
                           balloonchatwnd::kFileNormal, balloonchatwnd::kFileFailed };
    for (size_t k = 0; k < sizeof(states) / sizeof(states[0]); ++k)
    {
        INFO("state " << states[k]);
        HistoryRowView row;
        row.m_senderName   = _T("李四");
        row.m_timeText     = _T("10:05");
        row.m_chatFileCard = true;
        row.m_fileCard     = BuildMergedFileCard(sub, std::wstring());
        row.m_fileCard.fileState    = states[k];
        row.m_fileCard.fileProgress = kCardProgressPct;
        std::vector<HistoryRowView> rows(1, row);

        std::unique_ptr<balloonwjui::DuiControl> list =
            BuildListContent(rows, _T(""), _T(""), kListWidthPx, false);
        RECT rc = { 0, 0, kListWidthPx, kLayoutHeight };
        list->ForceLayout(rc);

        // 卡片控件按「基址 + 行下标」编号，矩形与卡片应有的宽高完全一致
        const balloonchatwnd::ChatFileCardControl* card =
            dynamic_cast<const balloonchatwnd::ChatFileCardControl*>(
                list->FindCtrlById((UINT)kIdHistoryFileCardBase));
        REQUIRE(card != NULL);
        const RECT rcCard = card->GetRect();
        CHECK((rcCard.right - rcCard.left) == balloonchatwnd::kFileCardWidth);
        CHECK((rcCard.bottom - rcCard.top) == balloonchatwnd::FileCardHeight(row.m_fileCard));

        // 卡片完整落在行内
        const balloonwjui::DuiControl* rowCtrl = list->FindCtrlById((UINT)kIdHistoryRowBase);
        REQUIRE(rowCtrl != NULL);
        const RECT rcRow = rowCtrl->GetRect();
        CHECK(rcCard.top >= rcRow.top);
        CHECK(rcCard.bottom <= rcRow.bottom);
    }
}

// ---- 系统提示行的发送人与头像底色（2026-10-03 起，HistoryRowFormat）----

TEST_CASE("V2V-SYS2 系统提示行的发送人与头像底色", "[history][systemtip]")
{
    const int64_t kSomeUser = 5127;
    //系统提示：不管服务端把它记在谁名下（群提示为 0，单聊的屏幕共享结束提示记在发起人名下）
    CHECK(HistorySenderName(::message_type_system, CString(_T("用户0"))) == CString(_T("系统消息")));
    CHECK(HistorySenderName(::message_type_system, CString(_T("张勇"))) == CString(_T("系统消息")));
    CHECK(HistoryAvatarColor(::message_type_system, 0) == kHistorySystemAvatarColor);
    CHECK(HistoryAvatarColor(::message_type_system, kSomeUser) == kHistorySystemAvatarColor);

    //其余消息：名字与底色照旧
    CHECK(HistorySenderName(::message_type_text, CString(_T("张勇"))) == CString(_T("张勇")));
    CHECK(HistoryAvatarColor(::message_type_text, kSomeUser) == ColorForId(kSomeUser));
    CHECK(HistoryAvatarColor(::message_type_relay, kSomeUser) == ColorForId(kSomeUser));
}
