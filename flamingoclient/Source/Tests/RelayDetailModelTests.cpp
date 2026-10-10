/**
 *  接龙详情窗与发起窗口的纯逻辑单元测试，RelayDetailModelTests.cpp
 *
 *  覆盖 docs/群接龙方案.md 第 3 期：
 *    - RDM：UI/BalloonCard/RelayDetailModel（3b 详情窗）——状态行、完整名单、输入区与各按钮的显隐、
 *           提交前的文本检查与提示、结束接龙的确认文字；
 *    - RCM：UI/BalloonCard/RelayComposeModel（3d 发起窗口）——截止时间的年月日时分秒与换算（RCM-D，
 *           2026-10-03 起替换原先的下拉档位）、发起前的检查与提示；
 *    - RDW：详情窗的控件 id 不与标题栏按钮相同（点 X 关不掉窗口的回归用例）。
 *
 *  时刻按本机时区构造（mktime），写法与 ChatRelayCardTests 一致，用例在任何时区下都成立。
 *
 *  标签 [grouprelay]。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <time.h>
#include <string>
#include <vector>

#include "../UI/BalloonCard/RelayDetailModel.h"
#include "../UI/BalloonCard/RelayComposeModel.h"
#include "../net/CardProtocolClient.h"
#include "../net/Protocol.h"   // relay_state、group_member_role 的具体取值
#include "../third-party/jsoncpp-1.9.0/json.h"

using namespace ballooncard;

namespace {

//本组用例用到的发起人、参与者与查看者
const int64_t kCreator = 300001;
const int64_t kUserA = 300008;
const int64_t kUserB = 300009;
const int64_t kSelf = 300010;
//批量造满员名单时 userid 的起点，避开上面几个固定的 userid
const int64_t kBulkUserBase = 400000;

//全角空格 U+3000 的 UTF-8 编码。不写成 u8 字面量里的 \x 转义：MSVC 会把 u8 字面量里的 \xE3
//当成码点 U+00E3 再编码成两个字节，得到的不是全角空格
const std::string kFullWidthSpace = "\xE3\x80\x80";

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

//「现在」是 2026-10-02 15:30，截止在第二天 18:00
const int64_t kNowMs = LocalMs(2026, 10, 2, 15, 30);
const int64_t kDeadlineMs = LocalMs(2026, 10, 3, 18, 0);

//固定映射的名字解析：发起人、张三、李四、王五（自己）
class FakeNames : public balloonchatwnd::IRelayNameResolver
{
public:
    CString ResolveName(int64_t userid) const override
    {
        if (userid == kCreator)
            return L"赵六";
        if (userid == kUserA)
            return L"张三";
        if (userid == kUserB)
            return L"李四";
        if (userid == kSelf)
            return L"王五";
        return CString();
    }
};

net::RelayItem MakeItem(int64_t userid, const std::string& text)
{
    net::RelayItem item;
    item.m_userid = userid;
    item.m_text   = text;
    return item;
}

//进行中、两条接龙、有截止时间的卡片
net::RelayCardContent MakeCard()
{
    net::RelayCardContent card;
    card.m_title    = u8"周五团建报名";
    card.m_note     = u8"格式：姓名+是否带家属";
    card.m_deadline = kDeadlineMs;
    card.m_items.push_back(MakeItem(kUserA, u8"本人+1"));
    card.m_items.push_back(MakeItem(kUserB, u8"不带"));
    return card;
}

//生成详情窗内容的简写：普通成员、未禁言
RelayDetailView Build(const net::RelayCardContent& card, int64_t selfId, int role, bool muted, int64_t nowMs)
{
    FakeNames names;
    RelayDetailView view;
    BuildRelayDetailView(card, kCreator, selfId, role, muted, nowMs, names, view);
    return view;
}

//重复 n 次某个 UTF-8 片段
std::string Repeat(const std::string& piece, int n)
{
    std::string out;
    for (int i = 0; i < n; ++i)
        out += piece;
    return out;
}

} // anonymous namespace

TEST_CASE("RDM1 进行中、本人未参与：可以提交，没有撤回；普通成员看不到结束", "[grouprelay]")
{
    const RelayDetailView view = Build(MakeCard(), kSelf, group_role_member, false, kNowMs);
    CHECK(view.m_title == CString(L"周五团建报名"));
    CHECK(view.m_note == CString(L"格式：姓名+是否带家属"));
    CHECK(view.m_statusText == CString(L"共 2 人参与 · 截止 10-03 18:00"));
    REQUIRE(view.m_entries.size() == 2);
    CHECK(view.m_entries[0].m_text == CString(L"1. 张三\x3000本人+1"));
    CHECK_FALSE(view.m_entries[0].m_isSelf);
    CHECK(view.m_state == relay_state_open);
    CHECK(view.m_showInput);
    CHECK(view.m_inputEnabled);
    CHECK(view.m_inputHint.IsEmpty());
    CHECK(view.m_submitLabel == CString(L"提交"));
    CHECK_FALSE(view.m_showWithdraw);
    CHECK_FALSE(view.m_showClose);
    CHECK_FALSE(view.m_joined);
    CHECK(view.m_selfText.empty());
}

TEST_CASE("RDM2 本人已参与：按钮写「修改」、能撤回、预填本人那条并标出本人那一行", "[grouprelay]")
{
    net::RelayCardContent card = MakeCard();
    card.m_items.push_back(MakeItem(kSelf, u8"我来"));
    const RelayDetailView view = Build(card, kSelf, group_role_member, false, kNowMs);
    CHECK(view.m_joined);
    CHECK(view.m_submitLabel == CString(L"修改"));
    CHECK(view.m_showWithdraw);
    CHECK(view.m_selfText == std::string(u8"我来"));
    REQUIRE(view.m_entries.size() == 3);
    CHECK(view.m_entries[2].m_isSelf);
    CHECK(view.m_entries[2].m_text == CString(L"3. 王五\x3000我来"));
}

TEST_CASE("RDM3 被禁言：输入区置灰并说明原因，不能撤回；管理员仍能结束", "[grouprelay]")
{
    net::RelayCardContent card = MakeCard();
    card.m_items.push_back(MakeItem(kSelf, u8"我来"));
    const RelayDetailView view = Build(card, kSelf, group_role_admin, true, kNowMs);
    CHECK(view.m_showInput);
    CHECK_FALSE(view.m_inputEnabled);
    CHECK(view.m_inputHint == CString(L"你已被禁言，不能参与接龙。"));
    CHECK_FALSE(view.m_showWithdraw);
    CHECK(view.m_showClose);   //结束接龙是管理动作，不受禁言限制
}

TEST_CASE("RDM4 名额已满：未参与的人不能再参与，已参与的人仍可修改", "[grouprelay]")
{
    net::RelayCardContent card;
    card.m_title = u8"报名";
    for (size_t i = 0; i < net::kRelayMaxParticipants; ++i)
        card.m_items.push_back(MakeItem(kBulkUserBase + (int64_t)i, "x"));

    const RelayDetailView outsider = Build(card, kSelf, group_role_member, false, kNowMs);
    CHECK(outsider.m_showInput);
    CHECK_FALSE(outsider.m_inputEnabled);
    CHECK(outsider.m_inputHint == CString(L"接龙人数已满。"));
    CHECK(outsider.m_statusText == CString(L"共 200 人参与 · 已满"));

    card.m_items.back().m_userid = kSelf;
    const RelayDetailView insider = Build(card, kSelf, group_role_member, false, kNowMs);
    CHECK(insider.m_inputEnabled);
    CHECK(insider.m_inputHint.IsEmpty());
    CHECK(insider.m_submitLabel == CString(L"修改"));
    CHECK(insider.m_statusText == CString(L"共 200 人参与"));
}

TEST_CASE("RDM5 已结束：状态行写结束人与时间，输入区、撤回、结束都不显示", "[grouprelay]")
{
    net::RelayCardContent card = MakeCard();
    card.m_items.push_back(MakeItem(kSelf, u8"我来"));
    card.m_state     = relay_state_closed;
    card.m_closedBy  = kUserB;
    card.m_closeTime = LocalMs(2026, 10, 2, 16, 0);
    const RelayDetailView view = Build(card, kSelf, group_role_owner, false, kNowMs);
    CHECK(view.m_statusText == CString(L"共 3 人参与 · 李四 于 10-02 16:00 结束了接龙"));
    CHECK_FALSE(view.m_showInput);
    CHECK_FALSE(view.m_inputEnabled);
    CHECK_FALSE(view.m_showWithdraw);
    CHECK_FALSE(view.m_showClose);
    CHECK(view.m_inputHint.IsEmpty());

    //结束时间缺失：只写结束人；结束人也缺失：写「已结束」
    card.m_closeTime = 0;
    CHECK(Build(card, kSelf, group_role_member, false, kNowMs).m_statusText
          == CString(L"共 3 人参与 · 李四 结束了接龙"));
    card.m_closedBy = 0;
    CHECK(Build(card, kSelf, group_role_member, false, kNowMs).m_statusText
          == CString(L"共 3 人参与 · 已结束"));
}

TEST_CASE("RDM6 已截止：按本机时间判断，状态行写截止时刻，输入区隐藏", "[grouprelay]")
{
    const RelayDetailView view = Build(MakeCard(), kSelf, group_role_owner, false, kDeadlineMs);
    CHECK(view.m_state == relay_state_expired);
    CHECK(view.m_statusText == CString(L"共 2 人参与 · 已于 10-03 18:00 截止"));
    CHECK_FALSE(view.m_showInput);
    CHECK_FALSE(view.m_showClose);
}

TEST_CASE("RDM7 谁能看到「结束接龙」：发起人、群主、管理员，进行中才有", "[grouprelay]")
{
    FakeNames names;
    RelayDetailView view;
    BuildRelayDetailView(MakeCard(), kCreator, kCreator, group_role_member, false, kNowMs, names, view);
    CHECK(view.m_showClose);
    CHECK(Build(MakeCard(), kSelf, group_role_owner, false, kNowMs).m_showClose);
    CHECK(Build(MakeCard(), kSelf, group_role_admin, false, kNowMs).m_showClose);
    CHECK_FALSE(Build(MakeCard(), kSelf, group_role_member, false, kNowMs).m_showClose);
}

TEST_CASE("RDM8 CheckRelaySubmit 提交前检查", "[grouprelay]")
{
    std::string out;

    //合法：去掉首尾空白（含全角空格）
    REQUIRE(CheckRelaySubmit(kFullWidthSpace + u8" 本人+1  ", false, std::string(), out) == kRelaySubmitOk);
    CHECK(out == std::string(u8"本人+1"));

    //为空、只有空白
    CHECK(CheckRelaySubmit("", false, std::string(), out) == kRelaySubmitEmpty);
    CHECK(CheckRelaySubmit(std::string(" ") + kFullWidthSpace + "\t\r\n", false, std::string(), out) == kRelaySubmitEmpty);

    //中间含换行
    CHECK(CheckRelaySubmit("a\nb", false, std::string(), out) == kRelaySubmitMultiline);
    CHECK(CheckRelaySubmit("a\r\nb", false, std::string(), out) == kRelaySubmitMultiline);

    //长度：100 个字符可以，101 个不行
    CHECK(CheckRelaySubmit(Repeat(u8"字", (int)net::kRelayTextMaxChars), false, std::string(), out)
          == kRelaySubmitOk);
    CHECK(CheckRelaySubmit(Repeat(u8"字", (int)net::kRelayTextMaxChars + 1), false, std::string(), out)
          == kRelaySubmitTooLong);

    //已参与且内容与现有那条相同（去掉首尾空白后比较）：不必提交
    CHECK(CheckRelaySubmit(u8" 本人+1 ", true, u8"本人+1", out) == kRelaySubmitUnchanged);
    //未参与时不比较
    CHECK(CheckRelaySubmit(u8"本人+1", false, u8"本人+1", out) == kRelaySubmitOk);
    //已参与、内容改了
    CHECK(CheckRelaySubmit(u8"不带", true, u8"本人+1", out) == kRelaySubmitOk);
}

TEST_CASE("RDM9 提交前检查的提示文字与结束确认文字", "[grouprelay]")
{
    CHECK(RelaySubmitCheckText(kRelaySubmitOk).IsEmpty());
    CHECK(RelaySubmitCheckText(kRelaySubmitEmpty) == CString(L"请填写接龙内容。"));
    CHECK(RelaySubmitCheckText(kRelaySubmitMultiline) == CString(L"接龙内容不能换行。"));
    CHECK(RelaySubmitCheckText(kRelaySubmitTooLong) == CString(L"内容太长，每条最多 100 字。"));
    CHECK(RelaySubmitCheckText(kRelaySubmitUnchanged) == CString(L"内容没有变化。"));

    CHECK(BuildRelayCloseConfirmText(MakeCard())
          == CString(L"确定结束接龙「周五团建报名」吗？结束后不能再参与、修改或撤回。"));
    net::RelayCardContent untitled = MakeCard();
    untitled.m_title.clear();
    CHECK(BuildRelayCloseConfirmText(untitled) == CString(L"确定结束这个接龙吗？结束后不能再参与、修改或撤回。"));
}

TEST_CASE("RCM-D1 截止时间的年月日时分秒按本机时区换算成 Unix 毫秒", "[grouprelay]")
{
    RelayDeadlineFields f;
    f.m_year   = 2026;
    f.m_month  = 10;
    f.m_day    = 3;
    f.m_hour   = 18;
    f.m_minute = 30;
    f.m_second = 45;
    const int64_t kSecondsPart = 45LL * 1000;
    CHECK(RelayDeadlineFieldsToMs(f) == LocalMs(2026, 10, 3, 18, 30) + kSecondsPart);

    //闰年的 2 月 29 日是存在的
    f.m_year  = 2028;
    f.m_month = 2;
    f.m_day   = 29;
    f.m_second = 0;
    CHECK(RelayDeadlineFieldsToMs(f) == LocalMs(2028, 2, 29, 18, 30));
}

TEST_CASE("RCM-D2 不存在的日期与超出范围的取值换算失败", "[grouprelay]")
{
    RelayDeadlineFields base;
    base.m_year   = 2026;
    base.m_month  = 10;
    base.m_day    = 3;
    base.m_hour   = 18;
    base.m_minute = 30;
    base.m_second = 0;
    REQUIRE(RelayDeadlineFieldsToMs(base) > 0);

    //不存在的日期：mktime 会把它们顺延成别的日子，不能悄悄当成合法时间发出去
    RelayDeadlineFields f = base;
    f.m_month = 2;
    f.m_day   = 29;   //2026 年不是闰年
    CHECK(RelayDeadlineFieldsToMs(f) == 0);
    f.m_day = 30;
    CHECK(RelayDeadlineFieldsToMs(f) == 0);
    f = base;
    f.m_month = 4;
    f.m_day   = 31;
    CHECK(RelayDeadlineFieldsToMs(f) == 0);

    //各项超出范围
    const int kBadMonth[]  = { 0, 13 };
    const int kBadDay[]    = { 0, 32 };
    for (size_t i = 0; i < 2; ++i)
    {
        f = base;
        f.m_month = kBadMonth[i];
        CHECK(RelayDeadlineFieldsToMs(f) == 0);
        f = base;
        f.m_day = kBadDay[i];
        CHECK(RelayDeadlineFieldsToMs(f) == 0);
    }
    f = base;
    f.m_hour = 24;
    CHECK(RelayDeadlineFieldsToMs(f) == 0);
    f = base;
    f.m_minute = 60;
    CHECK(RelayDeadlineFieldsToMs(f) == 0);
    f = base;
    f.m_second = 60;
    CHECK(RelayDeadlineFieldsToMs(f) == 0);
    f = base;
    f.m_hour = -1;
    CHECK(RelayDeadlineFieldsToMs(f) == 0);
}

TEST_CASE("RCM-D3 截止时间早于当前时间 + 5 分钟时不能发起，恰好 5 分钟可以", "[grouprelay]")
{
    std::string content;
    std::string entry;
    const int64_t earliest = kNowMs + net::kRelayDeadlineMinLeadMs;
    CHECK(CheckRelayCompose(u8"报名", "", earliest, kNowMs, "", content, entry) == kRelayComposeOk);
    //微调框以秒为单位，早一秒就不行
    const int64_t kOneSecondMs = 1000;
    CHECK(CheckRelayCompose(u8"报名", "", earliest - kOneSecondMs, kNowMs, "", content, entry)
          == kRelayComposeDeadlineTooEarly);
    CHECK(CheckRelayCompose(u8"报名", "", kNowMs, kNowMs, "", content, entry) == kRelayComposeDeadlineTooEarly);
    //换算失败（日期不存在）时传进来的是 0
    CHECK(CheckRelayCompose(u8"报名", "", 0, kNowMs, "", content, entry) == kRelayComposeDeadlineInvalid);
    //最短提前量与服务端 RELAY_DEADLINE_MIN_LEAD_MS 相同
    CHECK(net::kRelayDeadlineMinLeadMs == 5LL * 60 * 1000);
}

TEST_CASE("RCM-D4 打开窗口时的初始值是当前时间加 1 小时、秒为 0，且可以直接发起", "[grouprelay]")
{
    const int64_t kSecondsPart = 37LL * 1000;
    const int64_t now = kNowMs + kSecondsPart;   //2026-10-02 15:30:37
    const RelayDeadlineFields f = DefaultRelayDeadlineFields(now);
    CHECK(f.m_year == 2026);
    CHECK(f.m_month == 10);
    CHECK(f.m_day == 2);
    CHECK(f.m_hour == 16);
    CHECK(f.m_minute == 30);
    CHECK(f.m_second == 0);

    std::string content;
    std::string entry;
    CHECK(CheckRelayCompose(u8"报名", "", RelayDeadlineFieldsToMs(f), now, "", content, entry) == kRelayComposeOk);

    //跨天、跨年
    const RelayDeadlineFields yearEnd = DefaultRelayDeadlineFields(LocalMs(2026, 12, 31, 23, 20));
    CHECK(yearEnd.m_year == 2027);
    CHECK(yearEnd.m_month == 1);
    CHECK(yearEnd.m_day == 1);
    CHECK(yearEnd.m_hour == 0);
    CHECK(yearEnd.m_minute == 20);
    CHECK(yearEnd.m_second == 0);
}

TEST_CASE("RCM-D5 截止时间不合要求时的提示文字", "[grouprelay]")
{
    CHECK(RelayComposeCheckText(kRelayComposeDeadlineInvalid) == CString(L"截止时间不是有效的日期，请重新填写。"));
    CHECK(RelayComposeCheckText(kRelayComposeDeadlineTooEarly) == CString(L"截止时间须晚于当前时间 5 分钟以上。"));
    CHECK(RelaySendFailureText(error_code_relay_deadline_invalid)
          == CString(L"截止时间须晚于当前时间 5 分钟以上（以服务器时间为准），未能发起。"));
}

TEST_CASE("RCM3 CheckRelayCompose 合法时给出 content 与去掉空白的「我的接龙」", "[grouprelay]")
{
    std::string content;
    std::string entry;
    REQUIRE(CheckRelayCompose(u8"  周五团建报名 ", u8"格式：姓名", kDeadlineMs, kNowMs, u8" 本人+1 ", content, entry)
            == kRelayComposeOk);
    CHECK(entry == std::string(u8"本人+1"));

    net::RelayCardContent card;
    REQUIRE(net::CardProtocolClient::parseRelayCardContent(content, card));
    CHECK(card.m_title == std::string(u8"周五团建报名"));
    CHECK(card.m_note == std::string(u8"格式：姓名"));
    CHECK(card.m_deadline == kDeadlineMs);

    //「我的接龙」只有空白：按没填处理
    REQUIRE(CheckRelayCompose(u8"报名", "", kDeadlineMs, kNowMs, std::string(" ") + kFullWidthSpace + " ", content, entry)
            == kRelayComposeOk);
    CHECK(entry.empty());
}

TEST_CASE("RCM4 CheckRelayCompose 各种不合法的情形", "[grouprelay]")
{
    std::string content;
    std::string entry;
    CHECK(CheckRelayCompose("", "", kDeadlineMs, kNowMs, "", content, entry) == kRelayComposeTitleEmpty);
    CHECK(CheckRelayCompose(kFullWidthSpace + " ", "", kDeadlineMs, kNowMs, "", content, entry) == kRelayComposeTitleEmpty);
    CHECK(CheckRelayCompose("a\nb", "", kDeadlineMs, kNowMs, "", content, entry) == kRelayComposeTitleMultiline);
    CHECK(CheckRelayCompose(Repeat(u8"标", (int)net::kRelayTitleMaxChars), "", kDeadlineMs, kNowMs, "", content, entry)
          == kRelayComposeOk);
    CHECK(CheckRelayCompose(Repeat(u8"标", (int)net::kRelayTitleMaxChars + 1), "", kDeadlineMs, kNowMs, "", content, entry)
          == kRelayComposeTitleTooLong);
    CHECK(CheckRelayCompose(u8"报名", Repeat(u8"说", (int)net::kRelayNoteMaxChars + 1), kDeadlineMs, kNowMs, "", content, entry)
          == kRelayComposeNoteTooLong);
    //说明可以多行
    CHECK(CheckRelayCompose(u8"报名", "line1\nline2", kDeadlineMs, kNowMs, "", content, entry) == kRelayComposeOk);
    CHECK(CheckRelayCompose(u8"报名", "", kDeadlineMs, kNowMs, "a\nb", content, entry) == kRelayComposeEntryMultiline);
    CHECK(CheckRelayCompose(u8"报名", "", kDeadlineMs, kNowMs, Repeat(u8"字", (int)net::kRelayTextMaxChars + 1), content, entry)
          == kRelayComposeEntryTooLong);
}

TEST_CASE("RCM6 发送回执失败时的提示文字", "[grouprelay]")
{
    CHECK(RelaySendFailureText(error_code_member_muted) == CString(L"你已被禁言，不能发起接龙。"));
    CHECK(RelaySendFailureText(error_code_not_group_member) == CString(L"你已不在该群聊中。"));
    CHECK(RelaySendFailureText(error_code_msg_too_large) == CString(L"标题或说明太长，未能发起。"));
    CHECK(RelaySendFailureText(error_code_bad_request) == CString(L"内容不合规范，未能发起。"));
    CHECK(RelaySendFailureText(error_code_sensitive_word) == CString(L"内容包含敏感词，未能发起。"));
    CHECK(RelaySendFailureText(error_code_server_internal).Find(L"发起接龙失败（错误码 ") == 0);
}

TEST_CASE("RCM5 发起前检查的提示文字", "[grouprelay]")
{
    CHECK(RelayComposeCheckText(kRelayComposeOk).IsEmpty());
    CHECK(RelayComposeCheckText(kRelayComposeTitleEmpty) == CString(L"请填写接龙标题。"));
    CHECK(RelayComposeCheckText(kRelayComposeTitleMultiline) == CString(L"接龙标题不能换行。"));
    CHECK(RelayComposeCheckText(kRelayComposeTitleTooLong) == CString(L"标题太长，最多 50 字。"));
    CHECK(RelayComposeCheckText(kRelayComposeNoteTooLong) == CString(L"说明太长，最多 200 字。"));
    CHECK(RelayComposeCheckText(kRelayComposeEntryMultiline) == CString(L"「我的接龙」不能换行。"));
    CHECK(RelayComposeCheckText(kRelayComposeEntryTooLong) == CString(L"「我的接龙」太长，最多 100 字。"));
}

TEST_CASE("RDW1 详情窗客户区的控件 id 都不与标题栏按钮相同", "[grouprelay]")
{
    //DuiFrameWindow 标题栏三个按钮固定使用 1 最小化、2 最大化、3 关闭（见 balloonui DuiFrameWindow.cpp
    //的 OnDuiChildNotify）。「撤回我的」曾经是 3，点 X 时通知被详情窗当成「撤回我的」处理，窗口关不掉
    const int kCaptionIdFirst = 1;
    const int kCaptionIdLast  = 3;
    const int ids[] = {
        ballooncard::kRelayDetailIdInput,
        ballooncard::kRelayDetailIdSubmit,
        ballooncard::kRelayDetailIdWithdraw,
        ballooncard::kRelayDetailIdCloseRelay,
        ballooncard::kRelayDetailIdCopy,
    };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        INFO("control id " << ids[i]);
        CHECK((ids[i] < kCaptionIdFirst || ids[i] > kCaptionIdLast));
        //窗口内唯一：与后面的每一个都不相同
        for (size_t j = i + 1; j < sizeof(ids) / sizeof(ids[0]); ++j)
            CHECK(ids[i] != ids[j]);
    }
}
