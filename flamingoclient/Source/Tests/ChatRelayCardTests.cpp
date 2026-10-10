/**
 *  群接龙卡片显示模型单元测试（Suite CRC），ChatRelayCardTests.cpp
 *
 *  覆盖 UI/BalloonChatWnd/ChatRelayCard（docs/群接龙方案.md 第 2 期 2b）：
 *    - 合并接龙变更推送、按本机时间换算接龙状态、时刻的显示格式；
 *    - 卡片上的预览行、状态行与底部按钮（含名额已满、被禁言、已结束、已截止）；
 *    - 能否提前结束的判定（与服务端 CardProtocol::canCloseRelay 规则相同）；
 *    - 「复制为文本」的完整内容。
 *  以及第 3 期 3a 的卡片气泡：
 *    - 把卡片内容填进气泡数据、合并推送、到截止时间后重填、复制与摘要（CRC12~CRC17）；
 *    - 气泡行高随说明行数、接龙条数与有无按钮变化（CRB1）；
 *    - 隐藏用例 CRSNAP（标签 [.][snapshot][relaycardsnap]）把各状态的卡片画成 PNG 供人工核对。
 *  第 3 期 3c、3e：
 *    - 卡片与带 cardmsgid 的提示行可以点（CRB2）；
 *    - 摘要「[接龙] 标题」在会话列表、消息记录窗、引用、合并卡片里的写法（CRE1~CRE4）；
 *    - 接龙卡片不能单条、逐条转发，可以合并转发（CRE5、CRE6）。
 *
 *  时刻按本机时区构造（mktime），写法与 ChatTimeSeparatorTests 一致，用例在任何时区下都成立。
 *  名字经 IRelayNameResolver 传入，这里换成固定映射的假实现。
 *
 *  标签 [grouprelay]。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <time.h>
#include <vector>
#include <gdiplus.h>

#include "../UI/BalloonChatWnd/ChatRelayCard.h"
#include "../UI/BalloonChatWnd/ChatBubble.h"        // CRB1 / CRSNAP：测量与绘制卡片气泡
#include "../UI/BalloonChatWnd/ChatMessageList.h"   // CRSNAP：把多张卡片排进一个列表里画
#include "../UI/BalloonChatWnd/ChatMessageMenu.h"   // CRE5：右键菜单与转发规则
#include "../UI/BalloonChatWnd/ChatQuoteModel.h"    // CRE3：引用接龙卡片
#include "../UI/BalloonChatWnd/ChatMergedCard.h"    // CRE4：合并卡片里的接龙子消息
#include "../UI/BalloonHistory/HistoryView.h"       // CRE2：消息记录窗的预览文字
#include "../UI/BalloonHistory/HistoryRowMenu.h"    // CRE5：消息记录窗的「转发」
#include "../UI/BalloonHistory/V2MsgQuery.h"   // CRC10：RecordFromNotify / RelayContentFromRecord / NotifyFromRecord
#include "../UI/BalloonHistory/V2MsgStore.h"   // CRC10：真的读写本地库
#include "../net/MessageProtocolClient.h"      // CRC10：parseMessageNotify
#include "../net/Protocol.h"   // relay_state、group_member_role 的具体取值
#include "../third-party/jsoncpp-1.9.0/json.h"

using namespace balloonchatwnd;

namespace {

//本组用例用到的发起人与参与者
const int64_t kCreator = 100001;
const int64_t kUserA = 100008;
const int64_t kUserB = 100009;
const int64_t kStranger = 100099;   //名字解析查不到的用户

//批量造满员名单时 userid 的起点，避开上面几个固定的 userid
const int64_t kBulkUserBase = 200000;

//本机时区的某个时刻（Unix 毫秒），按年月日时分构造
int64_t LocalMs(int year, int month, int day, int hour, int minute)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year  = year - 1900;
    t.tm_mon   = month - 1;
    t.tm_mday  = day;
    t.tm_hour  = hour;
    t.tm_min   = minute;
    t.tm_isdst = -1;
    return (int64_t)mktime(&t) * 1000;
}

//「现在」与截止时间：现在是 2026-10-02 15:30，截止在第二天 18:00
const int64_t kNowMs = LocalMs(2026, 10, 2, 15, 30);
const int64_t kDeadlineMs = LocalMs(2026, 10, 3, 18, 0);

//固定映射的名字解析：张三、李四，其余查不到
class FakeNames : public IRelayNameResolver
{
public:
    CString ResolveName(int64_t userid) const override
    {
        if (userid == kUserA)
            return L"张三";
        if (userid == kUserB)
            return L"李四";
        return CString();
    }
};

//构造一条名单条目
net::RelayItem MakeItem(int64_t userid, const std::string& text)
{
    net::RelayItem item;
    item.m_userid = userid;
    item.m_text   = text;
    return item;
}

//构造一张进行中、带两条接龙的卡片
net::RelayCardContent MakeCard()
{
    net::RelayCardContent card;
    card.m_title = u8"周五团建报名";
    card.m_note  = u8"格式：姓名+是否带家属";
    card.m_items.push_back(MakeItem(kUserA, u8"本人+1"));
    card.m_items.push_back(MakeItem(kUserB, u8"不带"));
    return card;
}

//把名单填满到上限（每人一条），用于名额已满的用例
void FillToCapacity(net::RelayCardContent& card)
{
    card.m_items.clear();
    for (size_t i = 0; i < net::kRelayMaxParticipants; ++i)
        card.m_items.push_back(MakeItem(kBulkUserBase + (int64_t)i, "x"));
}

} // anonymous namespace

TEST_CASE("CRC1 ApplyRelayNotify 用推送替换名单与状态，标题、说明、截止时间不变", "[grouprelay]")
{
    net::RelayCardContent card = MakeCard();
    card.m_deadline = kDeadlineMs;

    net::RelayChangedNotify notify;
    notify.m_items.push_back(MakeItem(kUserB, u8"改过"));
    notify.m_state     = relay_state_closed;
    notify.m_closedBy  = kCreator;
    notify.m_closeTime = kNowMs;

    ApplyRelayNotify(card, notify);
    REQUIRE(card.m_items.size() == 1);
    CHECK(card.m_items[0].m_userid == kUserB);
    CHECK(card.m_items[0].m_text == u8"改过");
    CHECK(card.m_state == relay_state_closed);
    CHECK(card.m_closedBy == kCreator);
    CHECK(card.m_closeTime == kNowMs);

    //推送不带这三项，保持原值
    CHECK(card.m_title == u8"周五团建报名");
    CHECK(card.m_note == u8"格式：姓名+是否带家属");
    CHECK(card.m_deadline == kDeadlineMs);
}

TEST_CASE("CRC2 EffectiveRelayState 按本机时间判断截止，服务端的结论优先", "[grouprelay]")
{
    net::RelayCardContent card = MakeCard();

    //不限时、截止时间未到：进行中
    CHECK(EffectiveRelayState(card, kNowMs) == relay_state_open);
    card.m_deadline = kDeadlineMs;
    CHECK(EffectiveRelayState(card, kNowMs) == relay_state_open);

    //当前时间等于或晚于截止时间：已截止
    CHECK(EffectiveRelayState(card, kDeadlineMs) == relay_state_expired);
    CHECK(EffectiveRelayState(card, kDeadlineMs + 1) == relay_state_expired);

    //服务端已标为已结束：截止时间过了也仍是已结束（手动结束优先）
    card.m_state = relay_state_closed;
    CHECK(EffectiveRelayState(card, kDeadlineMs + 1) == relay_state_closed);

    //服务端已标为已截止：即使本机时钟还没到截止时间，也按已截止
    card.m_state = relay_state_expired;
    CHECK(EffectiveRelayState(card, kNowMs) == relay_state_expired);
}

TEST_CASE("CRC3 FormatRelayTime 同一年写月日，跨年写完整日期", "[grouprelay]")
{
    CHECK(FormatRelayTime(kDeadlineMs, kNowMs) == CString(L"10-03 18:00"));
    CHECK(FormatRelayTime(LocalMs(2027, 1, 5, 9, 7), kNowMs) == CString(L"2027-01-05 09:07"));
    CHECK(FormatRelayTime(0, kNowMs).IsEmpty());
    CHECK(FormatRelayTime(-1, kNowMs).IsEmpty());
}

TEST_CASE("CRC4 预览行只取前几条，序号从 1 起，查不到名字写「用户<id>」", "[grouprelay]")
{
    FakeNames names;
    net::RelayCardContent card = MakeCard();
    card.m_items.push_back(MakeItem(kStranger, "third"));

    RelayCardPreview preview;
    BuildRelayCardPreview(card, kCreator, false, kNowMs, names, preview);
    REQUIRE(preview.m_lines.size() == 3);
    CHECK(preview.m_lines[0] == CString(L"1. 张三\x3000本人+1"));
    CHECK(preview.m_lines[1] == CString(L"2. 李四\x3000不带"));
    CHECK(preview.m_lines[2] == CString(L"3. 用户100099\x3000third"));
    CHECK(preview.m_count == 3);

    //名单超过 kRelayCardPreviewLines 条时只取前面的
    for (int i = 0; i < 10; ++i)
        card.m_items.push_back(MakeItem(kBulkUserBase + i, "more"));
    BuildRelayCardPreview(card, kCreator, false, kNowMs, names, preview);
    CHECK(preview.m_lines.size() == kRelayCardPreviewLines);
    CHECK(preview.m_count == 13);
}

TEST_CASE("CRC5 状态行", "[grouprelay]")
{
    FakeNames names;
    RelayCardPreview preview;
    net::RelayCardContent card = MakeCard();

    //进行中、不限时
    BuildRelayCardPreview(card, kCreator, false, kNowMs, names, preview);
    CHECK(preview.m_statusText == CString(L"共 2 人参与"));

    //进行中、有截止时间
    card.m_deadline = kDeadlineMs;
    BuildRelayCardPreview(card, kCreator, false, kNowMs, names, preview);
    CHECK(preview.m_statusText == CString(L"共 2 人参与 · 截止 10-03 18:00"));

    //已截止（本机时间已过截止时间）
    BuildRelayCardPreview(card, kCreator, false, kDeadlineMs, names, preview);
    CHECK(preview.m_statusText == CString(L"共 2 人参与 · 已截止"));
    CHECK(preview.m_state == relay_state_expired);

    //已结束
    card.m_state = relay_state_closed;
    BuildRelayCardPreview(card, kCreator, false, kNowMs, names, preview);
    CHECK(preview.m_statusText == CString(L"共 2 人参与 · 已结束"));

    //名额已满、本人未参与：只写「已满」，不再写截止时间
    net::RelayCardContent full;
    full.m_title    = "t";
    full.m_deadline = kDeadlineMs;
    FillToCapacity(full);
    BuildRelayCardPreview(full, kCreator, false, kNowMs, names, preview);
    CHECK(preview.m_statusText == CString(L"共 200 人参与 · 已满"));

    //名额已满、本人已参与：照常显示截止时间
    BuildRelayCardPreview(full, kBulkUserBase, false, kNowMs, names, preview);
    CHECK(preview.m_statusText == CString(L"共 200 人参与 · 截止 10-03 18:00"));
}

TEST_CASE("CRC6 卡片底部按钮", "[grouprelay]")
{
    net::RelayCardContent card = MakeCard();

    //未参与：参与接龙；已参与：修改我的
    CHECK(DecideRelayButton(card, kCreator, false, kNowMs) == kRelayButtonJoin);
    CHECK(DecideRelayButton(card, kUserA, false, kNowMs) == kRelayButtonModify);

    //被禁言：参与与修改都不显示
    CHECK(DecideRelayButton(card, kCreator, true, kNowMs) == kRelayButtonNone);
    CHECK(DecideRelayButton(card, kUserA, true, kNowMs) == kRelayButtonNone);

    //已截止、已结束：不显示
    card.m_deadline = kDeadlineMs;
    CHECK(DecideRelayButton(card, kUserA, false, kDeadlineMs) == kRelayButtonNone);
    card.m_deadline = 0;
    card.m_state = relay_state_closed;
    CHECK(DecideRelayButton(card, kUserA, false, kNowMs) == kRelayButtonNone);

    //名额已满：未参与的人不显示按钮，已参与的人仍可修改
    net::RelayCardContent full;
    FillToCapacity(full);
    CHECK(DecideRelayButton(full, kCreator, false, kNowMs) == kRelayButtonNone);
    CHECK(DecideRelayButton(full, kBulkUserBase, false, kNowMs) == kRelayButtonModify);

    //还差一个名额时新人仍可参与
    full.m_items.pop_back();
    CHECK(DecideRelayButton(full, kCreator, false, kNowMs) == kRelayButtonJoin);
}

TEST_CASE("CRC7 CanCloseRelay 判断谁能提前结束", "[grouprelay]")
{
    net::RelayCardContent card = MakeCard();

    //群卡片：发起人（哪怕只是普通成员）、群主、管理员可以，其他普通成员不行
    CHECK(CanCloseRelay(card, true, kCreator, kCreator, group_role_member, kNowMs));
    CHECK(CanCloseRelay(card, true, kCreator, kUserA, group_role_owner, kNowMs));
    CHECK(CanCloseRelay(card, true, kCreator, kUserA, group_role_admin, kNowMs));
    CHECK_FALSE(CanCloseRelay(card, true, kCreator, kUserA, group_role_member, kNowMs));

    //单聊卡片：只有发起人可以
    CHECK(CanCloseRelay(card, false, kCreator, kCreator, group_role_member, kNowMs));
    CHECK_FALSE(CanCloseRelay(card, false, kCreator, kUserA, group_role_owner, kNowMs));

    //已截止、已结束：谁都不能再结束
    card.m_deadline = kDeadlineMs;
    CHECK_FALSE(CanCloseRelay(card, true, kCreator, kCreator, group_role_owner, kDeadlineMs));
    card.m_deadline = 0;
    card.m_state = relay_state_closed;
    CHECK_FALSE(CanCloseRelay(card, true, kCreator, kCreator, group_role_owner, kNowMs));
}

TEST_CASE("CRC8 复制为文本", "[grouprelay]")
{
    FakeNames names;
    net::RelayCardContent card = MakeCard();

    //进行中：末行写复制时的时刻
    CHECK(BuildRelayCopyText(card, kNowMs, names) == CString(
        L"接龙：周五团建报名\r\n"
        L"格式：姓名+是否带家属\r\n"
        L"1. 张三\x3000本人+1\r\n"
        L"2. 李四\x3000不带\r\n"
        L"（共 2 人，截至 10-02 15:30）"));

    //说明为空时省略那一行；已结束、已截止写状态
    card.m_note.clear();
    card.m_state = relay_state_closed;
    CHECK(BuildRelayCopyText(card, kNowMs, names) == CString(
        L"接龙：周五团建报名\r\n"
        L"1. 张三\x3000本人+1\r\n"
        L"2. 李四\x3000不带\r\n"
        L"（共 2 人，已结束）"));
    card.m_state = relay_state_expired;
    const CString expiredFooter = L"（共 2 人，已截止）";
    const CString expiredText = BuildRelayCopyText(card, kNowMs, names);
    CHECK(expiredText.Right(expiredFooter.GetLength()) == expiredFooter);

    //说明里的换行统一成 \r\n
    card.m_state = relay_state_open;
    card.m_note = "line1\nline2";
    card.m_items.clear();
    CHECK(BuildRelayCopyText(card, kNowMs, names) == CString(
        L"接龙：周五团建报名\r\n"
        L"line1\r\nline2\r\n"
        L"（共 0 人，截至 10-02 15:30）"));
}

TEST_CASE("CRC9 标题为空时显示「接龙」，说明原样带出", "[grouprelay]")
{
    FakeNames names;
    net::RelayCardContent card;
    card.m_note = "a\nb";

    RelayCardPreview preview;
    BuildRelayCardPreview(card, kCreator, false, kNowMs, names, preview);
    CHECK(preview.m_title == CString(L"接龙"));
    CHECK(preview.m_note == CString(L"a\nb"));
    CHECK(preview.m_count == 0);
    CHECK(preview.m_lines.empty());
    CHECK(preview.m_button == kRelayButtonJoin);
    CHECK(BuildRelayCopyText(card, kNowMs, names).Left(5) == CString(L"接龙：接龙"));
}

TEST_CASE("CRC10 本地库整条链路：写入卡片、合并变更推送、写回 content 列、还原出新名单", "[grouprelay]")
{
    //临时库：路径带进程号，避免两份测试进程同时跑时共用一个文件
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    wchar_t name[64] = { 0 };
    ::swprintf_s(name, L"%u_flamingo_crc10.db", static_cast<unsigned int>(::GetCurrentProcessId()));
    std::wstring path = dir;
    path += name;
    ::DeleteFileW(path.c_str());
    //本地库是 WAL 模式：上次异常退出残留的日志文件会被套用到同名新库上，一并删掉
    ::DeleteFileW((path + L"-wal").c_str());
    ::DeleteFileW((path + L"-shm").c_str());

    balloonhistory::CV2MsgStore store;
    REQUIRE(store.Open(path.c_str()));

    //1. 收到一张接龙卡片（名单里只有张三），照常入库
    Json::Value item;
    item["userid"] = std::to_string(kUserA);
    item["text"]   = u8"本人+1";
    Json::Value content;
    content["title"]    = u8"周五团建报名";
    content["note"]     = "";
    content["deadline"] = static_cast<Json::Int64>(kDeadlineMs);
    content["items"]    = Json::Value(Json::arrayValue);
    content["items"].append(item);
    Json::Value msg;
    msg["msgid"]     = "9007199254741011";
    msg["ctype"]     = conversation_group;
    msg["cid"]       = "600001";
    msg["from"]      = std::to_string(kCreator);
    msg["to"]        = "600001";
    msg["msgtype"]   = message_type_relay;
    msg["content"]   = content;
    msg["timestamp"] = static_cast<Json::Int64>(kNowMs);
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    net::MessageNotify received;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(Json::writeString(wb, msg), received));
    REQUIRE(store.Upsert(balloonhistory::RecordFromNotify(received, conversation_group, 600001)));

    //2. 收到变更推送：李四参与，发起人随即结束了接龙
    net::RelayChangedNotify notify;
    notify.m_ctype = conversation_group;
    notify.m_cid   = 600001;
    notify.m_msgid = 9007199254741011LL;
    notify.m_items.push_back(MakeItem(kUserA, u8"本人+1"));
    notify.m_items.push_back(MakeItem(kUserB, u8"不带"));
    notify.m_state     = relay_state_closed;
    notify.m_closedBy  = kCreator;
    notify.m_closeTime = kNowMs;

    //3. 主面板要做的事：取出本地那一行、取出卡片内容、合并推送、写回 content 列
    balloonhistory::HistoryRecord row;
    bool found = false;
    REQUIRE(store.QueryRecord(conversation_group, 600001, notify.m_msgid, row, found));
    REQUIRE(found);
    std::string relayJson;
    REQUIRE(balloonhistory::RelayContentFromRecord(row, relayJson));
    net::RelayCardContent card;
    REQUIRE(net::CardProtocolClient::parseRelayCardContent(relayJson, card));
    ApplyRelayNotify(card, notify);
    net::MessageNotify merged;
    merged.m_msgtype      = message_type_relay;
    merged.m_relayContent = net::CardProtocolClient::serializeRelayCardContent(card);
    bool hit = false;
    REQUIRE(store.UpdateContent(conversation_group, 600001, notify.m_msgid,
                                balloonhistory::BuildContentJson(merged), hit));
    REQUIRE(hit);

    //4. 下次从本地库还原：拿到的是合并后的名单与状态，标题与截止时间不变
    balloonhistory::HistoryRecord again;
    REQUIRE(store.QueryRecord(conversation_group, 600001, notify.m_msgid, again, found));
    REQUIRE(found);
    net::MessageNotify restored;
    REQUIRE(balloonhistory::NotifyFromRecord(again, restored));
    net::RelayCardContent restoredCard;
    REQUIRE(net::CardProtocolClient::parseRelayCardContent(restored.m_relayContent, restoredCard));
    REQUIRE(restoredCard.m_items.size() == 2);
    CHECK(restoredCard.m_items[1].m_userid == kUserB);
    CHECK(restoredCard.m_items[1].m_text == u8"不带");
    CHECK(restoredCard.m_state == relay_state_closed);
    CHECK(restoredCard.m_closedBy == kCreator);
    CHECK(restoredCard.m_title == u8"周五团建报名");
    CHECK(restoredCard.m_deadline == kDeadlineMs);
    //搜索文本是入库时按标题生成的，合并推送不改它
    CHECK(again.m_searchText == std::string(u8"周五团建报名"));

    store.Close();
    ::DeleteFileW(path.c_str());
}

TEST_CASE("CRC11 RelayOpErrorText 接龙操作失败时的提示", "[grouprelay]")
{
    //各操作共用的具体原因
    CHECK(RelayOpErrorText(cmd_join_relay, error_code_card_closed) == CString(L"接龙已结束或已截止。"));
    CHECK(RelayOpErrorText(cmd_close_relay, error_code_card_closed) == CString(L"接龙已结束或已截止。"));
    CHECK(RelayOpErrorText(cmd_join_relay, error_code_card_full) == CString(L"接龙人数已满。"));
    CHECK(RelayOpErrorText(cmd_withdraw_relay, error_code_member_muted) == CString(L"你已被禁言，不能参与接龙。"));
    CHECK(RelayOpErrorText(cmd_join_relay, error_code_message_not_exist) == CString(L"这条接龙已被撤回。"));
    CHECK(RelayOpErrorText(cmd_join_relay, error_code_not_group_member) == CString(L"你已不在该群聊中。"));

    //无权限：提前结束与参与、撤回的含义不同
    CHECK(RelayOpErrorText(cmd_close_relay, error_code_permission_denied)
          == CString(L"只有发起人、群主或管理员可以结束接龙。"));
    CHECK(RelayOpErrorText(cmd_join_relay, error_code_permission_denied) == CString(L"你已不在该群聊中。"));
    CHECK(RelayOpErrorText(cmd_withdraw_relay, error_code_permission_denied) == CString(L"你已不在该群聊中。"));

    //与内容有关的三种只在参与接龙时给出具体原因
    CHECK(RelayOpErrorText(cmd_join_relay, error_code_msg_too_large) == CString(L"内容太长，每条最多 100 字。"));
    CHECK(RelayOpErrorText(cmd_join_relay, error_code_sensitive_word) == CString(L"接龙内容包含敏感词，未能提交。"));
    CHECK(RelayOpErrorText(cmd_join_relay, error_code_bad_request) == CString(L"内容不能为空，也不能换行。"));
    CHECK(RelayOpErrorText(cmd_withdraw_relay, error_code_bad_request) == CString(L"撤回失败，请稍后重试。"));

    //认不出的错误码（含服务端内部错误）：按操作给出通用提示
    CHECK(RelayOpErrorText(cmd_join_relay, error_code_server_internal) == CString(L"参与接龙失败，请稍后重试。"));
    CHECK(RelayOpErrorText(cmd_withdraw_relay, error_code_server_internal) == CString(L"撤回失败，请稍后重试。"));
    CHECK(RelayOpErrorText(cmd_close_relay, error_code_server_internal) == CString(L"结束接龙失败，请稍后重试。"));
}

// =====================================================================
// 3a：卡片气泡（docs/群接龙方案.md 第 3 期 3a）
//   CRC12~CRC17 把卡片内容填进气泡数据、合并推送、截止重填、复制与摘要（纯逻辑）；
//   CRB1 气泡行高随说明行数、接龙条数与有无按钮变化；
//   CRSNAP 隐藏用例，把各状态的卡片画成一张 PNG 供人工核对。
// =====================================================================

namespace {

//当前登录用户（卡片的查看者），名字解析里叫「王五」
const int64_t kSelf = 100010;
//卡片消息的服务器 msgid
const long long kCardMsgid = 880001;

//在 FakeNames 基础上认得当前登录用户
class FakeNamesWithSelf : public IRelayNameResolver
{
public:
    CString ResolveName(int64_t userid) const override
    {
        if (userid == kSelf)
            return L"王五";
        return m_base.ResolveName(userid);
    }

private:
    FakeNames m_base;   //张三、李四两人的固定映射
};

//把卡片内容序列化成 content 原文，再套到一条普通文字气泡上
ChatMessage MakeRelayMessage(const net::RelayCardContent& card, int64_t nowMs, bool fromMe)
{
    ChatMessage msg = MakeTextMessage(fromMe, L"[接龙]");
    msg.msgid = kCardMsgid;
    FakeNamesWithSelf names;
    ApplyRelayCard(msg, net::CardProtocolClient::serializeRelayCardContent(card), kSelf, false, nowMs, names);
    return msg;
}

//构造一条推送：msgid 指向 kCardMsgid，名单与状态由调用方给出
net::RelayChangedNotify MakeNotify(const std::vector<net::RelayItem>& items, int state)
{
    net::RelayChangedNotify notify;
    notify.m_ctype = conversation_group;
    notify.m_cid   = 9001;
    notify.m_msgid = kCardMsgid;
    notify.m_items = items;
    notify.m_state = state;
    return notify;
}

} // anonymous namespace

TEST_CASE("CRC12 ApplyRelayCard 把卡片内容填进气泡数据", "[grouprelay]")
{
    net::RelayCardContent card = MakeCard();
    card.m_deadline = kDeadlineMs;
    ChatMessage msg = MakeRelayMessage(card, kNowMs, false);

    CHECK(msg.kind == (int)kMsgRelay);
    CHECK(msg.msgid == kCardMsgid);   //构造气泡时填好的字段保持不变
    CHECK(msg.text == CString(L"[接龙] 周五团建报名"));
    CHECK(msg.relayTitle == CString(L"周五团建报名"));
    CHECK(msg.relayNote == CString(L"格式：姓名+是否带家属"));
    REQUIRE(msg.relayLines.size() == 2);
    CHECK(msg.relayLines[0] == CString(L"1. 张三\x3000本人+1"));
    CHECK(msg.relayStatus == CString(L"共 2 人参与 · 截止 10-03 18:00"));
    CHECK(msg.relayCount == 2);
    CHECK(msg.relayState == relay_state_open);
    CHECK(msg.relayButton == kRelayButtonJoin);
    CHECK(msg.relayDeadline == kDeadlineMs);
    CHECK(msg.relayCopyBody == CString(
        L"接龙：周五团建报名\r\n"
        L"格式：姓名+是否带家属\r\n"
        L"1. 张三\x3000本人+1\r\n"
        L"2. 李四\x3000不带"));

    //relayContent 存的是原文，解析回来与原卡片一致
    net::RelayCardContent back;
    REQUIRE(net::CardProtocolClient::parseRelayCardContent(msg.relayContent, back));
    CHECK(back.m_title == card.m_title);
    CHECK(back.m_items.size() == 2);
}

TEST_CASE("CRC13 content 解析不了时仍是一张卡片，但不显示按钮", "[grouprelay]")
{
    ChatMessage msg = MakeTextMessage(false, L"[接龙]");
    msg.msgid = kCardMsgid;
    FakeNamesWithSelf names;
    ApplyRelayCard(msg, "not json", kSelf, false, kNowMs, names);

    CHECK(msg.kind == (int)kMsgRelay);
    CHECK(msg.relayTitle == CString(L"接龙"));
    CHECK(msg.text == CString(L"[接龙]"));
    CHECK(msg.relayLines.empty());
    CHECK(msg.relayCount == 0);
    CHECK(msg.relayButton == kRelayButtonNone);
    CHECK(msg.relayContent == "not json");
}

TEST_CASE("CRC14 ApplyRelayNotifyToMessage 合并推送并重填，msgid 不符或已不是卡片时不动", "[grouprelay]")
{
    ChatMessage msg = MakeRelayMessage(MakeCard(), kNowMs, false);
    REQUIRE(msg.relayButton == kRelayButtonJoin);

    //推送：本人排到第 3 位
    std::vector<net::RelayItem> items = MakeCard().m_items;
    items.push_back(MakeItem(kSelf, u8"我来"));
    FakeNamesWithSelf names;
    REQUIRE(ApplyRelayNotifyToMessage(msg, MakeNotify(items, relay_state_open), kSelf, false, kNowMs, names));
    CHECK(msg.relayCount == 3);
    REQUIRE(msg.relayLines.size() == 3);
    CHECK(msg.relayLines[2] == CString(L"3. 王五\x3000我来"));
    CHECK(msg.relayButton == kRelayButtonModify);
    CHECK(msg.relayTitle == CString(L"周五团建报名"));   //推送不带标题，标题保持不变

    //合并结果写回了 relayContent，下次推送在它的基础上合并
    net::RelayCardContent back;
    REQUIRE(net::CardProtocolClient::parseRelayCardContent(msg.relayContent, back));
    CHECK(back.m_items.size() == 3);
    CHECK(back.m_title == std::string(u8"周五团建报名"));

    //提前结束：状态行与按钮随之变化
    REQUIRE(ApplyRelayNotifyToMessage(msg, MakeNotify(items, relay_state_closed), kSelf, false, kNowMs, names));
    CHECK(msg.relayState == relay_state_closed);
    CHECK(msg.relayButton == kRelayButtonNone);
    CHECK(msg.relayStatus == CString(L"共 3 人参与 · 已结束"));

    //msgid 不符：不动
    net::RelayChangedNotify other = MakeNotify(std::vector<net::RelayItem>(), relay_state_open);
    other.m_msgid = kCardMsgid + 1;
    CHECK_FALSE(ApplyRelayNotifyToMessage(msg, other, kSelf, false, kNowMs, names));
    CHECK(msg.relayCount == 3);

    //卡片被撤回后变成了提示行：不动
    ChatMessage tip = msg;
    tip.kind = kMsgTime;
    CHECK_FALSE(ApplyRelayNotifyToMessage(tip, MakeNotify(std::vector<net::RelayItem>(), relay_state_open),
                                          kSelf, false, kNowMs, names));
    CHECK(tip.relayCount == 3);
}

TEST_CASE("CRC15 到了截止时间才需要重填，重填后显示已截止", "[grouprelay]")
{
    net::RelayCardContent card = MakeCard();
    card.m_deadline = kDeadlineMs;
    ChatMessage msg = MakeRelayMessage(card, kNowMs, false);

    CHECK_FALSE(RelayCardDueForExpiry(msg, kNowMs));
    CHECK_FALSE(RelayCardDueForExpiry(msg, kDeadlineMs - 1));
    CHECK(RelayCardDueForExpiry(msg, kDeadlineMs));

    FakeNamesWithSelf names;
    RefreshRelayCard(msg, kSelf, false, kDeadlineMs, names);
    CHECK(msg.relayState == relay_state_expired);
    CHECK(msg.relayButton == kRelayButtonNone);
    CHECK(msg.relayStatus == CString(L"共 2 人参与 · 已截止"));
    CHECK_FALSE(RelayCardDueForExpiry(msg, kDeadlineMs));   //已经重填过，不再需要

    //不限时、已结束、不是卡片的都不需要
    ChatMessage noDeadline = MakeRelayMessage(MakeCard(), kNowMs, false);
    CHECK_FALSE(RelayCardDueForExpiry(noDeadline, kDeadlineMs));
    net::RelayCardContent closed = card;
    closed.m_state = relay_state_closed;
    CHECK_FALSE(RelayCardDueForExpiry(MakeRelayMessage(closed, kNowMs, false), kDeadlineMs));
    ChatMessage text = MakeTextMessage(false, L"hello");
    text.relayDeadline = kDeadlineMs;
    CHECK_FALSE(RelayCardDueForExpiry(text, kDeadlineMs));

    //RefreshRelayCard 对不是卡片的消息什么都不做
    RefreshRelayCard(text, kSelf, false, kDeadlineMs, names);
    CHECK(text.kind == (int)kMsgText);
    CHECK(text.text == CString(L"hello"));
}

TEST_CASE("CRC16 复制卡片：与「复制为文本」同一份文字，末行按复制时刻写", "[grouprelay]")
{
    net::RelayCardContent card = MakeCard();
    card.m_deadline = kDeadlineMs;
    ChatMessage msg = MakeRelayMessage(card, kNowMs, false);
    FakeNames names;

    //填写之后过了一会儿再复制：末行写复制的时刻，与 BuildRelayCopyText 完全一致
    const int64_t laterMs = LocalMs(2026, 10, 2, 17, 45);
    CHECK(RelayCopyTextForMessage(msg, laterMs) == BuildRelayCopyText(card, laterMs, names));

    //到了截止时间但卡片还没来得及重填：末行按已截止写
    const CString expiredFooter = L"（共 2 人，已截止）";
    const CString copied = RelayCopyTextForMessage(msg, kDeadlineMs);
    CHECK(copied.Right(expiredFooter.GetLength()) == expiredFooter);
}

TEST_CASE("CRC17 BuildRelayDigest 摘要「[接龙] 标题」", "[grouprelay]")
{
    CHECK(BuildRelayDigest(net::CardProtocolClient::serializeRelayCardContent(MakeCard()))
          == CString(L"[接龙] 周五团建报名"));
    net::RelayCardContent untitled = MakeCard();
    untitled.m_title.clear();
    CHECK(BuildRelayDigest(net::CardProtocolClient::serializeRelayCardContent(untitled)) == CString(L"[接龙]"));
    CHECK(BuildRelayDigest("") == CString(L"[接龙]"));
    CHECK(BuildRelayDigest("[1,2]") == CString(L"[接龙]"));
}

namespace {

//气泡测量用的消息区宽度（像素），与聊天窗默认宽度相近
const int kBubbleAvailWidth = 600;

//把一条卡片气泡数据放进一个独立的气泡里测量，返回行高
int RelayRowHeight(const ChatMessage& msg)
{
    ChatBubble bubble;
    bubble.SetMessage(msg);
    bubble.Measure(kBubbleAvailWidth);
    return bubble.GetRowHeight();
}

//构造一张没有说明、没有接龙、进行中的卡片
net::RelayCardContent MakeBareCard()
{
    net::RelayCardContent card;
    card.m_title = u8"周五团建报名";
    return card;
}

} // anonymous namespace

TEST_CASE("CRB1 卡片气泡的行高随说明行数、接龙条数与有无按钮变化", "[grouprelay]")
{
    const int bare = RelayRowHeight(MakeRelayMessage(MakeBareCard(), kNowMs, false));

    //没有接龙时占一行写「还没有人参与」，所以一条接龙与没有接龙同高，两条更高
    net::RelayCardContent one = MakeBareCard();
    one.m_items.push_back(MakeItem(kUserA, "a"));
    CHECK(RelayRowHeight(MakeRelayMessage(one, kNowMs, false)) == bare);
    net::RelayCardContent two = one;
    two.m_items.push_back(MakeItem(kUserB, "b"));
    const int twoH = RelayRowHeight(MakeRelayMessage(two, kNowMs, false));
    CHECK(twoH > bare);

    //卡片上最多显示 kRelayCardPreviewLines 条，再多高度不变
    net::RelayCardContent many = MakeBareCard();
    for (size_t i = 0; i < kRelayCardPreviewLines + 3; ++i)
        many.m_items.push_back(MakeItem(kBulkUserBase + (int64_t)i, "x"));
    net::RelayCardContent exactly = MakeBareCard();
    for (size_t i = 0; i < kRelayCardPreviewLines; ++i)
        exactly.m_items.push_back(MakeItem(kBulkUserBase + (int64_t)i, "x"));
    CHECK(RelayRowHeight(MakeRelayMessage(many, kNowMs, false))
          == RelayRowHeight(MakeRelayMessage(exactly, kNowMs, false)));

    //没有按钮（已结束）比有按钮矮
    net::RelayCardContent closed = MakeBareCard();
    closed.m_state = relay_state_closed;
    CHECK(RelayRowHeight(MakeRelayMessage(closed, kNowMs, false)) < bare);

    //说明：一行比没有高，两行更高，再长也只显示两行
    net::RelayCardContent shortNote = MakeBareCard();
    shortNote.m_note = u8"请写姓名";
    const int oneLineH = RelayRowHeight(MakeRelayMessage(shortNote, kNowMs, false));
    CHECK(oneLineH > bare);
    net::RelayCardContent twoLines = MakeBareCard();
    twoLines.m_note = u8"第一行\n第二行";
    const int twoLinesH = RelayRowHeight(MakeRelayMessage(twoLines, kNowMs, false));
    CHECK(twoLinesH > oneLineH);
    net::RelayCardContent longNote = MakeBareCard();
    longNote.m_note = u8"第一行\n第二行\n第三行\n第四行";
    CHECK(RelayRowHeight(MakeRelayMessage(longNote, kNowMs, false)) == twoLinesH);
    //没有换行、但长到一行放不下的说明，同样折成两行
    net::RelayCardContent wideNote = MakeBareCard();
    std::string wide;
    for (int i = 0; i < 80; ++i)
        wide += u8"长";
    wideNote.m_note = wide;
    CHECK(RelayRowHeight(MakeRelayMessage(wideNote, kNowMs, false)) == twoLinesH);
    //空行不占行
    net::RelayCardContent blankLines = MakeBareCard();
    blankLines.m_note = u8"\n\n请写姓名\n\n";
    CHECK(RelayRowHeight(MakeRelayMessage(blankLines, kNowMs, false)) == oneLineH);
}

// =====================================================================
// 效果图（默认不运行）：各状态的接龙卡片
//
// 用与界面相同的绘制代码，把进行中（未参与 / 已参与）、名额已满、已结束、已截止、本人发起、
// 说明很长等几种卡片画到一张 PNG 上，供人工核对版式。标签带 [.]，默认不运行；输出路径取自
// 环境变量 FLAMINGO_SNAPSHOT_PATH，没设时直接跳过。用法：
//     set FLAMINGO_SNAPSHOT_PATH=E:\flamingo\snapshots\relay_cards.png
//     FlamingoUnit.exe "[relaycardsnap]"
// =====================================================================

namespace {

//效果图的尺寸（像素）：宽度与聊天窗消息区相近，高度放得下全部卡片
const int kRelaySnapWidth  = 620;
const int kRelaySnapHeight = 1700;

//在 GDI+ 已注册的图像编码器里找 PNG 编码器的 CLSID
bool FindRelaySnapPngEncoder(CLSID& out)
{
    UINT count = 0;
    UINT bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0)
        return false;
    std::vector<BYTE> buf(bytes);
    Gdiplus::ImageCodecInfo* codecs = (Gdiplus::ImageCodecInfo*)&buf[0];
    if (Gdiplus::GetImageEncoders(count, bytes, codecs) != Gdiplus::Ok)
        return false;
    for (UINT i = 0; i < count; ++i)
    {
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0)
        {
            out = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}

//效果图里别人发起的卡片：带发送人名与头像字
ChatMessage SnapOthersCard(const net::RelayCardContent& card, long long msgid)
{
    ChatMessage msg = MakeRelayMessage(card, kNowMs, false);
    msg.msgid = msgid;
    msg.senderName        = L"张三";
    msg.senderAvatarText  = L"张";
    msg.senderAvatarColor = RGB(0x4F, 0x8E, 0xF7);
    msg.senderUserId      = kUserA;
    return msg;
}

} // anonymous namespace

TEST_CASE("CRSNAP 各状态接龙卡片的效果图", "[.][snapshot][relaycardsnap]")
{
    TCHAR path[MAX_PATH] = { 0 };
    if (::GetEnvironmentVariable(_T("FLAMINGO_SNAPSHOT_PATH"), path, MAX_PATH) == 0)
    {
        SUCCEED("FLAMINGO_SNAPSHOT_PATH not set, snapshot skipped");
        return;
    }

    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartupInput gdipInput;
    REQUIRE(Gdiplus::GdiplusStartup(&gdipToken, &gdipInput, NULL) == Gdiplus::Ok);
    {
        ChatMessageList list;
        long long msgid = kCardMsgid;

        //1. 刚发出、还没人参与、有说明与截止时间：显示「参与接龙」
        net::RelayCardContent fresh = MakeBareCard();
        fresh.m_note = u8"格式：姓名+是否带家属";
        fresh.m_deadline = kDeadlineMs;
        list.AppendMessage(SnapOthersCard(fresh, msgid++));

        //2. 两人参与、本人未参与
        net::RelayCardContent two = MakeCard();
        two.m_deadline = kDeadlineMs;
        list.AppendMessage(SnapOthersCard(two, msgid++));

        //3. 本人已参与、超过 5 条、说明很长：显示「修改我的」，说明折两行带省略号
        net::RelayCardContent many = MakeCard();
        many.m_note = u8"请在周四中午前填写：姓名 + 是否带家属 + 是否需要停车位，饮食忌口也一并写上，方便统一订餐和安排车辆。";
        many.m_items.push_back(MakeItem(kSelf, u8"本人，不带家属，需要车位"));
        for (int i = 0; i < 4; ++i)
            many.m_items.push_back(MakeItem(kStranger + i, u8"参加"));
        list.AppendMessage(SnapOthersCard(many, msgid++));

        //4. 本人发起、已提前结束
        net::RelayCardContent closed = MakeCard();
        closed.m_state = relay_state_closed;
        ChatMessage mine = MakeRelayMessage(closed, kNowMs, true);
        mine.msgid = msgid++;
        list.AppendMessage(mine);

        //5. 已过截止时间
        net::RelayCardContent expired = MakeCard();
        expired.m_deadline = LocalMs(2026, 10, 1, 18, 0);
        list.AppendMessage(SnapOthersCard(expired, msgid++));

        //6. 名额已满、本人未参与：状态行写「已满」，不显示按钮
        net::RelayCardContent full = MakeBareCard();
        FillToCapacity(full);
        list.AppendMessage(SnapOthersCard(full, msgid++));

        RECT all = { 0, 0, kRelaySnapWidth, kRelaySnapHeight };
        list.ForceLayout(all);

        HDC screenDC = ::GetDC(NULL);
        REQUIRE(screenDC != NULL);
        HDC memDC = ::CreateCompatibleDC(screenDC);
        HBITMAP bmp = ::CreateCompatibleBitmap(screenDC, kRelaySnapWidth, kRelaySnapHeight);
        ::ReleaseDC(NULL, screenDC);
        REQUIRE(memDC != NULL);
        REQUIRE(bmp != NULL);
        HGDIOBJ oldBmp = ::SelectObject(memDC, bmp);
        HBRUSH bg = ::CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
        ::FillRect(memDC, &all, bg);
        ::DeleteObject(bg);
        list.OnPaint(memDC, all);
        ::GdiFlush();
        ::SelectObject(memDC, oldBmp);

        CLSID png;
        REQUIRE(FindRelaySnapPngEncoder(png));
        {
            Gdiplus::Bitmap image(bmp, NULL);
            CHECK(image.Save(path, &png, NULL) == Gdiplus::Ok);
        }
        ::DeleteObject(bmp);
        ::DeleteDC(memDC);
    }
    Gdiplus::GdiplusShutdown(gdipToken);
}

// =====================================================================
// 3c：卡片与接龙提示行的点击；3e：各处摘要、右键菜单与转发规则
//   CRB2 卡片与提示行的命中判断（手形光标、按下即开始一次单击）；
//   CRE1~CRE6 摘要「[接龙] 标题」在会话列表、消息记录窗、引用、合并卡片里的写法，
//   以及接龙卡片不能单条 / 逐条转发、可以合并转发。
// =====================================================================

namespace {

//把一个消息列表排版并画一遍：卡片的命中矩形在绘制时才记下来
void LayoutAndPaint(ChatMessageList& list, int width, int height)
{
    RECT all = { 0, 0, width, height };
    list.ForceLayout(all);
    HDC screenDC = ::GetDC(NULL);
    REQUIRE(screenDC != NULL);
    HDC memDC = ::CreateCompatibleDC(screenDC);
    HBITMAP bmp = ::CreateCompatibleBitmap(screenDC, width, height);
    ::ReleaseDC(NULL, screenDC);
    REQUIRE(memDC != NULL);
    REQUIRE(bmp != NULL);
    HGDIOBJ oldBmp = ::SelectObject(memDC, bmp);
    list.OnPaint(memDC, all);
    ::SelectObject(memDC, oldBmp);
    ::DeleteObject(bmp);
    ::DeleteDC(memDC);
}

//取列表里第 index 个子控件对应的气泡
ChatBubble* RelayBubbleAt(ChatMessageList& list, size_t index)
{
    REQUIRE(index < list.Children().size());
    ChatBubble* b = dynamic_cast<ChatBubble*>(list.Children()[index].get());
    REQUIRE(b != NULL);
    return b;
}

//列表尺寸（像素），放得下一张卡片与一条提示行
const int kClickListW = 600;
const int kClickListH = 900;

} // anonymous namespace

TEST_CASE("CRB2 卡片与带 cardmsgid 的提示行可以点，普通提示行不行", "[grouprelay]")
{
    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartupInput gdipInput;
    REQUIRE(Gdiplus::GdiplusStartup(&gdipToken, &gdipInput, NULL) == Gdiplus::Ok);
    {
        ChatMessageList list;
        list.AppendMessage(MakeRelayMessage(MakeCard(), kNowMs, false));
        ChatMessage tip = MakeTextMessage(false, L"「张三」参与了接龙「周五团建报名」");
        tip.kind = kMsgTime;
        tip.msgid = kCardMsgid + 1;
        tip.relayCardMsgid = kCardMsgid;
        list.AppendMessage(tip);
        ChatMessage plainTip = MakeTextMessage(false, L"「李四」加入了群聊");
        plainTip.kind = kMsgTime;
        plainTip.msgid = kCardMsgid + 2;
        list.AppendMessage(plainTip);
        LayoutAndPaint(list, kClickListW, kClickListH);

        //找出三行：卡片、可点的提示行、普通提示行（列表开头可能还有一行时间分隔）
        ChatBubble* card = NULL;
        ChatBubble* clickableTip = NULL;
        ChatBubble* normalTip = NULL;
        for (size_t i = 0; i < list.Children().size(); ++i)
        {
            ChatBubble* b = RelayBubbleAt(list, i);
            if (b->Msgid() == kCardMsgid)
                card = b;
            else if (b->Msgid() == kCardMsgid + 1)
                clickableTip = b;
            else if (b->Msgid() == kCardMsgid + 2)
                normalTip = b;
        }
        REQUIRE(card != NULL);
        REQUIRE(clickableTip != NULL);
        REQUIRE(normalTip != NULL);

        //卡片：气泡在头像右侧，取卡片区域中部一点
        const RECT rcCard = card->GetRect();
        POINT inCard = { rcCard.left + 120, (rcCard.top + rcCard.bottom) / 2 };
        CHECK(card->OnSetCursor(inCard));
        CHECK(card->OnLButtonDown(inCard, 0));
        CHECK(card->OnLButtonUp(inCard, 0));
        //卡片右侧的空白（气泡外）不可点
        POINT besideCard = { rcCard.right - 4, (rcCard.top + rcCard.bottom) / 2 };
        CHECK_FALSE(card->OnSetCursor(besideCard));

        //可点的提示行：文字正中可点，行两端的空白不可点
        const RECT rcTip = clickableTip->GetRect();
        POINT tipCenter = { (rcTip.left + rcTip.right) / 2, (rcTip.top + rcTip.bottom) / 2 };
        POINT tipEdge = { rcTip.left + 2, (rcTip.top + rcTip.bottom) / 2 };
        CHECK(clickableTip->OnSetCursor(tipCenter));
        CHECK_FALSE(clickableTip->OnSetCursor(tipEdge));
        CHECK(clickableTip->OnLButtonDown(tipCenter, 0));
        CHECK(clickableTip->OnLButtonUp(tipCenter, 0));

        //普通提示行：不可点
        const RECT rcPlain = normalTip->GetRect();
        POINT plainCenter = { (rcPlain.left + rcPlain.right) / 2, (rcPlain.top + rcPlain.bottom) / 2 };
        CHECK_FALSE(normalTip->OnSetCursor(plainCenter));
        CHECK_FALSE(normalTip->OnLButtonDown(plainCenter, 0));
    }
    Gdiplus::GdiplusShutdown(gdipToken);
}

TEST_CASE("CRE1 会话列表摘要：接龙卡片写成「[接龙] 标题」", "[grouprelay]")
{
    CHECK(net::MessageProtocolClient::digestForMsgType(message_type_relay, std::string())
          == std::string(u8"[接龙]"));

    net::MessageNotify notify;
    notify.m_msgtype = message_type_relay;
    notify.m_relayContent = net::CardProtocolClient::serializeRelayCardContent(MakeCard());
    CHECK(net::MessageProtocolClient::digestForNotify(notify) == std::string(u8"[接龙] 周五团建报名"));

    //标题为空、content 不可用：只写「[接龙]」
    net::RelayCardContent untitled = MakeCard();
    untitled.m_title.clear();
    notify.m_relayContent = net::CardProtocolClient::serializeRelayCardContent(untitled);
    CHECK(net::MessageProtocolClient::digestForNotify(notify) == std::string(u8"[接龙]"));
    notify.m_relayContent = "not json";
    CHECK(net::MessageProtocolClient::digestForNotify(notify) == std::string(u8"[接龙]"));

    //其余类型与 digestForMsgType 相同
    net::MessageNotify text;
    text.m_msgtype = message_type_text;
    text.m_text = u8"你好";
    CHECK(net::MessageProtocolClient::digestForNotify(text) == std::string(u8"你好"));
    net::MessageNotify image;
    image.m_msgtype = message_type_image;
    CHECK(net::MessageProtocolClient::digestForNotify(image) == std::string(u8"[图片]"));
}

TEST_CASE("CRE2 消息记录窗与导出：按本地库 content 列写成「[接龙] 标题」", "[grouprelay]")
{
    net::MessageNotify notify;
    notify.m_msgtype = message_type_relay;
    notify.m_relayContent = net::CardProtocolClient::serializeRelayCardContent(MakeCard());
    const std::string content = balloonhistory::BuildContentJson(notify);
    CHECK(balloonhistory::PreviewFromContent(message_type_relay, content) == std::string(u8"[接龙] 周五团建报名"));
    CHECK(balloonhistory::FullTextFromContent(message_type_relay, content) == std::string(u8"[接龙] 周五团建报名"));

    //content 列没有 relay 子对象（异常数据）：只写「[接龙]」
    CHECK(balloonhistory::PreviewFromContent(message_type_relay, "{}") == std::string(u8"[接龙]"));
    CHECK(balloonhistory::PreviewFromContent(message_type_relay, "{\"relay\":{}}") == std::string(u8"[接龙]"));
}

TEST_CASE("CRE3 引用接龙卡片：类型写 11，摘要为「[接龙] 标题」", "[grouprelay]")
{
    const ChatMessage card = MakeRelayMessage(MakeCard(), kNowMs, false);
    CHECK(MsgKindToProtocolType(kMsgRelay) == message_type_relay);
    CHECK(BuildQuoteDigest(card) == CString(L"[接龙] 周五团建报名"));
    const net::ReplyQuote quote = BuildQuoteFromMessage(card, L"张三", CString(), CString());
    CHECK(quote.m_valid);
    CHECK(quote.m_msgtype == message_type_relay);
    CHECK(quote.m_digest == std::string(u8"[接龙] 周五团建报名"));
}

TEST_CASE("CRE4 合并卡片里的接龙子消息显示「[接龙] 标题」", "[grouprelay]")
{
    const std::string content =
        "{\"title\":\"群聊的聊天记录\",\"messages\":["
        "{\"msgid\":\"1\",\"from\":\"100008\",\"msgtype\":11,"
        "\"content\":{\"title\":\"周五团建报名\",\"note\":\"\",\"deadline\":\"0\"},\"fromname\":\"张三\"}]}";
    MergedCardPreview preview;
    REQUIRE(BuildMergedCardPreview(content, preview));
    REQUIRE(preview.m_lines.size() == 1);
    CHECK(preview.m_lines[0] == CString(L"张三：[接龙] 周五团建报名"));
}

TEST_CASE("CRE5 接龙卡片：右键菜单有「复制」没有「转发」，消息记录窗也不给「转发」", "[grouprelay]")
{
    CHECK(kChatMenuKindRelay == (int)kMsgRelay);
    CHECK_FALSE(CanForwardMessage(kMsgRelay, kCardMsgid, 0));
    CHECK(CanMergeForwardMessage(kMsgRelay, kCardMsgid, 0));
    //还没有服务器 msgid 的卡片两种都不行
    CHECK_FALSE(CanMergeForwardMessage(kMsgRelay, 0, 0));
    //其余类型两个判定一致
    CHECK(CanMergeForwardMessage(kMsgText, kCardMsgid, 0));
    CHECK_FALSE(CanMergeForwardMessage(kMsgFolder, kCardMsgid, 0));

    const std::vector<ChatMessageMenuEntry> model = BuildChatMessageMenuModel(
        kChatMenuKindRelay, kChatImageStateReady, false, false, true, false,
        CanForwardMessage(kMsgRelay, kCardMsgid, 0));
    bool hasCopy = false;
    bool hasForward = false;
    for (size_t i = 0; i < model.size(); ++i)
    {
        if (model[i].isSeparator)
            continue;
        if (model[i].id == kChatMsgMenuCopy)
            hasCopy = true;
        if (model[i].id == kChatMsgMenuForward)
            hasForward = true;
    }
    CHECK(hasCopy);
    CHECK_FALSE(hasForward);

    CHECK_FALSE(balloonhistory::CanForwardHistoryRow(message_type_relay, kCardMsgid));
}

TEST_CASE("CRE6 多选转发：逐条转发跳过接龙卡片，合并转发收下", "[grouprelay]")
{
    const long long kTextMsgid = 880101;
    ChatMessageList list;
    ChatMessage text = MakeTextMessage(false, L"普通消息");
    text.msgid = kTextMsgid;
    list.AppendMessage(text);
    list.AppendMessage(MakeRelayMessage(MakeCard(), kNowMs, false));
    RECT all = { 0, 0, kClickListW, kClickListH };
    list.ForceLayout(all);

    //全选两条消息行
    for (size_t i = 0; i < list.Children().size(); ++i)
    {
        ChatBubble* b = RelayBubbleAt(list, i);
        if (b->Msgid() == kTextMsgid)
            list.EnterMultiSelectWith(b);
    }
    REQUIRE(list.IsMultiSelectMode());
    for (size_t i = 0; i < list.Children().size(); ++i)
    {
        ChatBubble* b = RelayBubbleAt(list, i);
        if (b->Msgid() == kCardMsgid)
            b->SetSelected(true);
    }

    size_t skipped = 0;
    const std::vector<long long> each = list.GetSelectedForwardMsgids(false, &skipped);
    REQUIRE(each.size() == 1);
    CHECK(each[0] == kTextMsgid);
    CHECK(skipped == 1);
    //不带模式的版本等同于逐条转发
    CHECK(list.GetSelectedForwardMsgids(NULL) == each);

    //结果按 msgid 升序：卡片的 msgid 比文字消息小，排在前面
    const std::vector<long long> merged = list.GetSelectedForwardMsgids(true, &skipped);
    REQUIRE(merged.size() == 2);
    CHECK(merged[0] == kCardMsgid);
    CHECK(merged[1] == kTextMsgid);
    CHECK(skipped == 0);
}
