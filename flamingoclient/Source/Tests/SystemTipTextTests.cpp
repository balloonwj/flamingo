/**
 *  系统提示与会话摘要按当前界面语言在本地生成（多语言第 5 期）的单元测试，标签 [systemtip]，用例 STT1 ~ STT9。
 *
 *    - STT1 简体中文：十种提示类型生成的文字与服务端拼的 text 逐字相同（对照 flamingoserver/tests 的 TIP / K28 用例）
 *    - STT2 参数缺失或类型不对时按缺省写法生成
 *    - STT3 认不出的类型、缺 tipargs、content 不是对象：返回 false，出参不变
 *    - STT4 非简体中文：整句模板与 TRC8 带上下文的短语都经宿主取到译文
 *    - STT5 时长的时钟写法
 *    - STT6 解析消息：带 tipkind 的系统提示按本地模板改写正文并保存类型与参数；认不出的类型保留服务端正文
 *    - STT7 落本地库再读回：content 带 tipkind / tipargs / cardmsgid，消息记录窗按本地模板重新生成
 *    - STT8 会话列表：可在本地生成的摘要换成本地文字，文字消息、认不出的类型、已撤回的保持服务端的 digest
 *    - STT9 TRC8：简体中文返回原文，上下文为 NULL 时等同 TR8
 *
 *  入群提示 member_join（docs/邀请入群方案.md 第 1 期，2026-10-04 起）：
 *    - JT1 本人不涉及时与服务端 text 逐字相同（一人、多人、超过 10 人）
 *    - JT2 本人是邀请人：「你邀请…」
 *    - JT3 本人是新成员：「…邀请你…」，「你」排在最前、其余人顺序不变
 *    - JT4 本人排在第 10 位之后、members 里没有昵称时仍写「邀请你」
 *    - JT5 没有邀请人：「「李四」加入了群聊」「你加入了群聊」
 *    - JT6 参数缺失时的缺省写法；members 缺失或为空时返回 false
 *    - JT7 没有设置本人 userid 时不出现「你」
 *    - JT9 解析实时消息与会话摘要时都按本地模板改写
 *    - JT10 计未读：入群提示的新成员名单里有本人时计未读（2026-10-07 用户确认），邀请人、旁观者与其它系统提示不计
 *    （JT8 是本人入群后补拉群列表的判断，在 GroupRoleRefreshTests.cpp）
 *
 *  框架 Catch2 v3。用例里的 i18n 全局状态在结束时恢复为未初始化。
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string.h>
#include <string>

#include "../net/SystemTipText.h"
#include "../net/MessageProtocolClient.h"
#include "../net/ConversationsProtocolClient.h"
#include "../net/Protocol.h"
#include "../UI/BalloonHistory/V2MsgQuery.h"     // BuildContentJson
#include "../UI/BalloonHistory/HistoryView.h"    // PreviewFromContent
#include "../Common/I18n/I18n.h"
#include "../third-party/jsoncpp-1.9.0/json.h"

namespace {

//用例结束时把 i18n 模块恢复为未初始化（简体中文），不影响后面的用例
struct ScopedTipI18nReset
{
    ScopedTipI18nReset()
    {
        i18n::ResetForTest();
    }
    ~ScopedTipI18nReset()
    {
        i18n::ResetForTest();
    }
};

//把 JSON 文本解析成对象，供用例构造 tipargs
Json::Value ParseJsonObject(const std::string& text)
{
    Json::Value root;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return root;
}

//按类型与参数 JSON 文本生成文字；不认得类型时返回 "<false>"
std::string Tip(const char* kind, const std::string& argsJson)
{
    std::string out;
    if (!net::FormatSystemTip(kind, ParseJsonObject(argsJson), out))
        return "<false>";
    return out;
}

//STT4 的宿主查找函数：只认识几条，其余返回 NULL（表示没有译文）
const wchar_t* FakeTipHost(const wchar_t* context, const wchar_t* source)
{
    if (context == NULL && wcscmp(source, L"「{0}」退出了群聊") == 0)
        return L"\"{0}\" left the group";
    if (context == NULL && wcscmp(source, L"{0}参与了{1}") == 0)
        return L"{0} joined {1}";
    if (context != NULL && wcscmp(context, L"接龙系统提示里取不到名字的操作者") == 0 && wcscmp(source, L"有成员") == 0)
        return L"A member";
    if (context == NULL && wcscmp(source, L"接龙「{0}」") == 0)
        return L"the relay \"{0}\"";
    if (context != NULL && wcscmp(context, L"入群欢迎语里新成员昵称之间的分隔符") == 0 && wcscmp(source, L"、") == 0)
        return L", ";
    return NULL;
}

} // namespace

TEST_CASE("STT1 简体中文：生成的文字与服务端拼的 text 逐字相同", "[systemtip]")
{
    ScopedTipI18nReset reset;

    CHECK(Tip(kSystemTipMemberLeave, u8"{\"nickname\":\"乙\"}") == u8"「乙」退出了群聊");
    CHECK(Tip(kSystemTipMemberLeave, "{}") == u8"有成员退出了群聊");

    CHECK(Tip(kSystemTipOwnerTransfer, u8"{\"from\":\"甲\",\"to\":\"乙\"}") == u8"「甲」已将群主转让给「乙」");
    CHECK(Tip(kSystemTipOwnerTransfer, u8"{\"to\":\"乙\"}") == u8"群主已转让给「乙」");
    CHECK(Tip(kSystemTipOwnerTransfer, u8"{\"from\":\"甲\"}") == u8"「甲」已转让群主");
    CHECK(Tip(kSystemTipOwnerTransfer, "{}") == u8"群主已变更");

    CHECK(Tip(kSystemTipAnnouncement, u8"{\"publisher\":\"张三\",\"title\":\"周会改到周五\"}")
          == u8"「张三」发布了群公告：周会改到周五");
    CHECK(Tip(kSystemTipAnnouncement, u8"{\"title\":\"周会改到周五\"}") == u8"「有人」发布了群公告：周会改到周五");

    CHECK(Tip(kSystemTipGroupWelcome, u8"{\"names\":[\"张三\",\"李四\"],\"welcome\":\"欢迎加入本群\"}")
          == u8"张三、李四 欢迎加入本群");
    CHECK(Tip(kSystemTipGroupWelcome, u8"{\"names\":[],\"welcome\":\"欢迎加入本群\"}") == u8"欢迎加入本群");

    const std::string relayArgs = u8"{\"nickname\":\"张三\",\"title\":\"周五团建报名\"}";
    CHECK(Tip(kSystemTipRelayJoin, relayArgs) == u8"「张三」参与了接龙「周五团建报名」");
    CHECK(Tip(kSystemTipRelayModify, relayArgs) == u8"「张三」修改了在接龙「周五团建报名」中的内容");
    CHECK(Tip(kSystemTipRelayWithdraw, relayArgs) == u8"「张三」撤回了在接龙「周五团建报名」中的内容");
    CHECK(Tip(kSystemTipRelayClose, u8"{\"nickname\":\"李四\",\"title\":\"周五团建报名\",\"count\":12}")
          == u8"「李四」结束了接龙「周五团建报名」，共 12 人参与");
    CHECK(Tip(kSystemTipRelayUpdate, relayArgs) == u8"「张三」更新了接龙「周五团建报名」");
    //昵称为空写成「有成员」，标题为空只写「接龙」
    CHECK(Tip(kSystemTipRelayJoin, u8"{\"title\":\"周五团建报名\"}") == u8"有成员参与了接龙「周五团建报名」");
    CHECK(Tip(kSystemTipRelayJoin, u8"{\"nickname\":\"张三\"}") == u8"「张三」参与了接龙");
    CHECK(Tip(kSystemTipRelayClose, u8"{\"nickname\":\"张三\",\"count\":0}") == u8"「张三」结束了接龙，共 0 人参与");

    CHECK(Tip(kSystemTipScreenShareEnd, "{\"duration\":65}") == u8"屏幕共享已结束，时长 01:05");
    CHECK(Tip(kSystemTipScreenShareEnd, "{\"duration\":3725}") == u8"屏幕共享已结束，时长 1:02:05");
}

TEST_CASE("STT2 参数缺失或类型不对时按缺省写法生成", "[systemtip]")
{
    ScopedTipI18nReset reset;

    //昵称不是字符串：按没有昵称处理
    CHECK(Tip(kSystemTipMemberLeave, "{\"nickname\":123}") == u8"有成员退出了群聊");
    //人数不是整数、或为负数：按 0
    CHECK(Tip(kSystemTipRelayClose, "{\"count\":\"12\"}") == u8"有成员结束了接龙，共 0 人参与");
    CHECK(Tip(kSystemTipRelayClose, "{\"count\":-3}") == u8"有成员结束了接龙，共 0 人参与");
    //名单里的空串与非字符串跳过
    CHECK(Tip(kSystemTipGroupWelcome, u8"{\"names\":[\"\",7,\"张三\"],\"welcome\":\"欢迎\"}") == u8"张三 欢迎");
    //时长缺失按 0
    CHECK(Tip(kSystemTipScreenShareEnd, "{}") == u8"屏幕共享已结束，时长 00:00");

    //tipargs 不是对象：FormatSystemTip 仍按缺省写法生成
    std::string out;
    REQUIRE(net::FormatSystemTip(kSystemTipOwnerTransfer, Json::Value("oops"), out));
    CHECK(out == u8"群主已变更");
    //JSON 文本版：解析失败按空对象
    REQUIRE(net::FormatSystemTipJson(kSystemTipMemberLeave, "not json", out));
    CHECK(out == u8"有成员退出了群聊");
    REQUIRE(net::FormatSystemTipJson(kSystemTipMemberLeave, u8"{\"nickname\":\"乙\"}", out));
    CHECK(out == u8"「乙」退出了群聊");
}

TEST_CASE("STT3 认不出的类型与不完整的 content：返回 false，出参不变", "[systemtip]")
{
    ScopedTipI18nReset reset;

    std::string out = "unchanged";
    CHECK_FALSE(net::FormatSystemTip("future_kind", Json::Value(Json::objectValue), out));
    CHECK_FALSE(net::FormatSystemTip("", Json::Value(Json::objectValue), out));
    CHECK(out == "unchanged");

    //content 缺 tipargs、tipkind 不是字符串、content 不是对象
    CHECK_FALSE(net::FormatSystemTipFromContent(ParseJsonObject(u8"{\"text\":\"t\",\"tipkind\":\"member_leave\"}"), out));
    CHECK_FALSE(net::FormatSystemTipFromContent(ParseJsonObject("{\"text\":\"t\",\"tipkind\":1,\"tipargs\":{}}"), out));
    CHECK_FALSE(net::FormatSystemTipFromContent(Json::Value("text"), out));
    CHECK(out == "unchanged");

    REQUIRE(net::FormatSystemTipFromContent(
        ParseJsonObject(u8"{\"text\":\"t\",\"tipkind\":\"member_leave\",\"tipargs\":{\"nickname\":\"乙\"}}"), out));
    CHECK(out == u8"「乙」退出了群聊");
}

TEST_CASE("STT4 非简体中文：整句模板与带上下文的短语都经宿主取到译文", "[systemtip]")
{
    ScopedTipI18nReset reset;
    i18n::InitFromHost(&FakeTipHost, L"en-US", L"Segoe UI");
    REQUIRE_FALSE(i18n::IsSourceLanguage());

    CHECK(Tip(kSystemTipMemberLeave, "{\"nickname\":\"Bob\"}") == "\"Bob\" left the group");
    //接龙：操作者短语用 TRC8 的上下文取到「A member」，接龙短语与整句模板各自取译文
    CHECK(Tip(kSystemTipRelayJoin, "{\"title\":\"Friday\"}") == "A member joined the relay \"Friday\"");
    //入群欢迎语：分隔符按上下文换成英文逗号，欢迎词原样
    CHECK(Tip(kSystemTipGroupWelcome, u8"{\"names\":[\"Ann\",\"Bob\"],\"welcome\":\"欢迎\"}") == u8"Ann, Bob 欢迎");
    //宿主没有译文的模板退回中文原文
    CHECK(Tip(kSystemTipOwnerTransfer, "{}") == u8"群主已变更");
}

TEST_CASE("STT5 时长的时钟写法", "[systemtip]")
{
    CHECK(net::FormatDurationClock(0) == "00:00");
    CHECK(net::FormatDurationClock(65) == "01:05");
    CHECK(net::FormatDurationClock(3599) == "59:59");
    CHECK(net::FormatDurationClock(3600) == "1:00:00");
    CHECK(net::FormatDurationClock(37230) == "10:20:30");
    CHECK(net::FormatDurationClock(-5) == "00:00");
}

TEST_CASE("STT6 解析消息：结构化系统提示按本地模板改写正文", "[systemtip]")
{
    ScopedTipI18nReset reset;

    //服务端 text 故意写成别的文字，用来确认正文是按本地模板重新生成的
    const std::string body =
        u8"{\"msgid\":\"101\",\"clientmsgid\":\"sys-leave-1\",\"ctype\":2,\"from\":\"0\",\"to\":\"900001\","
        u8"\"msgtype\":17,\"timestamp\":1700000000000,"
        u8"\"content\":{\"text\":\"SERVER TEXT\",\"tipkind\":\"member_leave\",\"tipargs\":{\"nickname\":\"乙\"}}}";
    net::MessageNotify notify;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(body, notify));
    CHECK(notify.m_text == u8"「乙」退出了群聊");
    CHECK(notify.m_tipKind == "member_leave");
    CHECK(ParseJsonObject(notify.m_tipArgs)["nickname"].asString() == u8"乙");

    //认不出的类型：保留服务端的 text，类型与参数照样保存
    const std::string future =
        u8"{\"msgid\":\"102\",\"ctype\":2,\"from\":\"0\",\"to\":\"900001\",\"msgtype\":17,\"timestamp\":1,"
        u8"\"content\":{\"text\":\"SERVER TEXT\",\"tipkind\":\"future_kind\",\"tipargs\":{}}}";
    net::MessageNotify unknown;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(future, unknown));
    CHECK(unknown.m_text == "SERVER TEXT");
    CHECK(unknown.m_tipKind == "future_kind");

    //老服务端的系统提示：只有 text
    const std::string legacy =
        u8"{\"msgid\":\"103\",\"ctype\":2,\"from\":\"0\",\"to\":\"900001\",\"msgtype\":17,\"timestamp\":1,"
        u8"\"content\":{\"text\":\"「乙」退出了群聊\"}}";
    net::MessageNotify old;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(legacy, old));
    CHECK(old.m_text == u8"「乙」退出了群聊");
    CHECK(old.m_tipKind.empty());
    CHECK(old.m_tipArgs.empty());
}

TEST_CASE("STT7 落本地库再读回：消息记录窗按本地模板重新生成", "[systemtip]")
{
    ScopedTipI18nReset reset;

    net::MessageNotify notify;
    notify.m_msgtype = message_type_system;
    notify.m_text = "STALE";
    notify.m_tipKind = "relay_close";
    notify.m_tipArgs = u8"{\"nickname\":\"李四\",\"title\":\"周五团建报名\",\"count\":3}";
    notify.m_cardMsgId = 9007199254741011LL;

    const std::string content = balloonhistory::BuildContentJson(notify);
    const Json::Value root = ParseJsonObject(content);
    CHECK(root["text"].asString() == "STALE");
    CHECK(root["tipkind"].asString() == "relay_close");
    CHECK(root["tipargs"]["count"].asInt() == 3);
    CHECK(root["cardmsgid"].asString() == "9007199254741011");

    CHECK(balloonhistory::PreviewFromContent(message_type_system, content)
          == u8"「李四」结束了接龙「周五团建报名」，共 3 人参与");

    //没有结构化字段的旧行：取落库时的文字
    CHECK(balloonhistory::PreviewFromContent(message_type_system, u8"{\"text\":\"「乙」退出了群聊\"}")
          == u8"「乙」退出了群聊");

    //位置、名片、投票、待办补上了类型文字
    CHECK(balloonhistory::PreviewFromContent(message_type_location, "{}") == u8"[位置]");
    CHECK(balloonhistory::PreviewFromContent(message_type_card, "{}") == u8"[名片]");
    CHECK(balloonhistory::PreviewFromContent(message_type_poll, "{}") == u8"[投票]");
    CHECK(balloonhistory::PreviewFromContent(message_type_todo, "{}") == u8"[待办]");
}

TEST_CASE("STT8 会话列表：能在本地生成的摘要换成本地文字", "[systemtip]")
{
    ScopedTipI18nReset reset;

    //服务端 digest 故意写成别的文字，用来区分哪些是本地生成的
    const std::string body =
        u8"{\"code\":0,\"msg\":\"ok\",\"synctime\":1,\"conversations\":["
        u8"{\"ctype\":2,\"cid\":\"1\",\"lastmsgid\":\"1\",\"lastmsg\":{\"from\":\"0\",\"msgtype\":17,\"digest\":\"S1\","
        u8"\"tipkind\":\"owner_transfer\",\"tipargs\":{\"from\":\"甲\",\"to\":\"乙\"},\"timestamp\":1}},"
        u8"{\"ctype\":2,\"cid\":\"2\",\"lastmsgid\":\"2\",\"lastmsg\":{\"from\":\"0\",\"msgtype\":17,\"digest\":\"S2\",\"timestamp\":1}},"
        u8"{\"ctype\":1,\"cid\":\"3\",\"lastmsgid\":\"3\",\"lastmsg\":{\"from\":\"3\",\"msgtype\":2,\"digest\":\"S3\",\"timestamp\":1}},"
        u8"{\"ctype\":2,\"cid\":\"4\",\"lastmsgid\":\"4\",\"lastmsg\":{\"from\":\"3\",\"msgtype\":11,\"digest\":\"S4\","
        u8"\"digestarg\":\"周五团建报名\",\"timestamp\":1}},"
        u8"{\"ctype\":2,\"cid\":\"5\",\"lastmsgid\":\"5\",\"lastmsg\":{\"from\":\"3\",\"msgtype\":20,\"digest\":\"S5\",\"timestamp\":1}},"
        u8"{\"ctype\":1,\"cid\":\"6\",\"lastmsgid\":\"6\",\"lastmsg\":{\"from\":\"3\",\"msgtype\":1,\"digest\":\"hello\",\"timestamp\":1}},"
        u8"{\"ctype\":1,\"cid\":\"7\",\"lastmsgid\":\"7\",\"lastmsg\":{\"from\":\"3\",\"msgtype\":5,\"digest\":\"S7\",\"timestamp\":1}},"
        u8"{\"ctype\":1,\"cid\":\"8\",\"lastmsgid\":\"8\",\"lastmsg\":{\"from\":\"3\",\"msgtype\":2,\"digest\":\"S8\","
        u8"\"recalled\":true,\"timestamp\":1}},"
        u8"{\"ctype\":1,\"cid\":\"9\",\"lastmsgid\":\"9\",\"lastmsg\":{\"from\":\"3\",\"msgtype\":7,\"digest\":\"S9\",\"timestamp\":1}}"
        u8"]}";
    net::ConversationsResult out;
    REQUIRE(net::ConversationsProtocolClient::parseSyncConversationsResponse(body, out));
    REQUIRE(out.m_items.size() == 9);

    //结构化系统提示：本地生成
    CHECK(out.m_items[0].m_digest == u8"「甲」已将群主转让给「乙」");
    //没有结构化字段的系统提示：保持服务端
    CHECK(out.m_items[1].m_digest == "S2");
    //图片：本地类型文字
    CHECK(out.m_items[2].m_digest == u8"[图片]");
    //接龙：类型文字接标题
    CHECK(out.m_items[3].m_digest == u8"[接龙] 周五团建报名");
    //文件共享没有 digestarg：只写类型
    CHECK(out.m_items[4].m_digest == u8"[共享]");
    //文字消息：用户写的内容，保持服务端
    CHECK(out.m_items[5].m_digest == "hello");
    //短视频（客户端没有类型文字）：保持服务端
    CHECK(out.m_items[6].m_digest == "S7");
    //已撤回：保持服务端，由主面板按撤回者改写
    CHECK(out.m_items[7].m_digest == "S8");
    //位置：本地类型文字
    CHECK(out.m_items[8].m_digest == u8"[位置]");
}

TEST_CASE("STT9 TRC8：简体中文返回原文，上下文为 NULL 时等同 TR8", "[systemtip]")
{
    ScopedTipI18nReset reset;
    CHECK(TRC8(u8"任意上下文", u8"有成员") == std::string(u8"有成员"));
    CHECK(i18n::TranslateUtf8(NULL, u8"有成员") == std::string(u8"有成员"));
    CHECK(i18n::TranslateUtf8(u8"上下文", NULL).empty());

    i18n::InitFromHost(&FakeTipHost, L"en-US", L"Segoe UI");
    CHECK(TRC8(u8"接龙系统提示里取不到名字的操作者", u8"有成员") == std::string("A member"));
    //同一原文换个上下文：宿主没有译文，返回原文
    CHECK(TRC8(u8"别的上下文", u8"有成员") == std::string(u8"有成员"));
}

//==================== 入群提示 member_join（docs/邀请入群方案.md 第 1 期，用例 JT1 ~ JT7、JT9）====================

namespace {

//入群提示用例里的本人 userid：构造时设置，结束时恢复为 0（未登录），不影响后面的用例
struct ScopedSelfUserId
{
    explicit ScopedSelfUserId(int64_t userid)
    {
        net::SetSystemTipSelfUserId(userid);
    }
    ~ScopedSelfUserId()
    {
        net::SetSystemTipSelfUserId(0);
    }
};

//样例 userid：邀请人张三、新成员李四 / 王五 / 赵六，以及一个与提示无关的旁观者
const int64_t kJtInviter   = 5127;
const int64_t kJtLisi      = 5308;
const int64_t kJtWangwu    = 5309;
const int64_t kJtBystander = 9999;

//一次邀请超过 10 人时的人数，以及这批新成员 userid 的起点
const int kJtManyMembers = 12;
const int64_t kJtManyFirstId = 6000;

//服务端只给前几位新成员带昵称，与服务端 kMemberJoinTipMaxNames 相同
const int kJtMaxNames = 10;

//一人邀请一人：张三邀请李四
const char* kJtOneArgs =
    u8"{\"inviter\":\"张三\",\"inviterid\":\"5127\",\"members\":[{\"userid\":\"5308\",\"nickname\":\"李四\"}]}";

//一人邀请三人：张三邀请李四、王五、赵六
const char* kJtThreeArgs =
    u8"{\"inviter\":\"张三\",\"inviterid\":\"5127\",\"members\":[{\"userid\":\"5308\",\"nickname\":\"李四\"},"
    u8"{\"userid\":\"5309\",\"nickname\":\"王五\"},{\"userid\":\"5310\",\"nickname\":\"赵六\"}]}";

//张三一次邀请 12 人：前 10 位带昵称 m0 ~ m9，后两位只有 userid（与服务端的写法相同）
std::string ManyArgs()
{
    std::string s = u8"{\"inviter\":\"张三\",\"inviterid\":\"5127\",\"members\":[";
    for (int i = 0; i < kJtManyMembers; ++i)
    {
        if (i > 0)
            s += ",";
        s += "{\"userid\":\"" + std::to_string(kJtManyFirstId + i) + "\"";
        if (i < kJtMaxNames)
            s += ",\"nickname\":\"m" + std::to_string(i) + "\"";
        s += "}";
    }
    s += "]}";
    return s;
}

//前 10 位新成员的名字串：「m0」、「m1」……「m9」
std::string ManyNames()
{
    std::string s;
    for (int i = 0; i < kJtMaxNames; ++i)
    {
        if (i > 0)
            s += u8"、";
        s += u8"「m" + std::to_string(i) + u8"」";
    }
    return s;
}

} // namespace

TEST_CASE("JT1 入群提示：本人不涉及时与服务端 text 逐字相同", "[systemtip]")
{
    ScopedTipI18nReset reset;
    ScopedSelfUserId self(kJtBystander);

    CHECK(Tip("member_join", kJtOneArgs) == u8"「张三」邀请「李四」加入了群聊");
    CHECK(Tip("member_join", kJtThreeArgs) == u8"「张三」邀请「李四」、「王五」、「赵六」加入了群聊");
    //超过 10 人：只列带昵称的前 10 位，末尾写「等 N 人」（对照服务端 GJ3）
    CHECK(Tip("member_join", ManyArgs()) == u8"「张三」邀请" + ManyNames() + u8"等 12 人加入了群聊");
}

TEST_CASE("JT2 入群提示：本人是邀请人时写「你邀请」", "[systemtip]")
{
    ScopedTipI18nReset reset;
    ScopedSelfUserId self(kJtInviter);

    CHECK(Tip("member_join", kJtOneArgs) == u8"你邀请「李四」加入了群聊");
    CHECK(Tip("member_join", kJtThreeArgs) == u8"你邀请「李四」、「王五」、「赵六」加入了群聊");
}

TEST_CASE("JT3 入群提示：本人是新成员时「你」排在最前，其余人顺序不变", "[systemtip]")
{
    ScopedTipI18nReset reset;
    {
        //只有本人
        ScopedSelfUserId self(kJtLisi);
        CHECK(Tip("member_join", kJtOneArgs) == u8"「张三」邀请你加入了群聊");
    }
    {
        //本人排在中间：「你」提到最前，李四、赵六保持原来的先后
        ScopedSelfUserId self(kJtWangwu);
        CHECK(Tip("member_join", kJtThreeArgs) == u8"「张三」邀请你和「李四」、「赵六」加入了群聊");
    }
}

TEST_CASE("JT4 入群提示：本人排在第 10 位之后（没有昵称）仍写「邀请你」", "[systemtip]")
{
    ScopedTipI18nReset reset;
    //最后一位新成员：members 里那一项只有 userid
    ScopedSelfUserId self(kJtManyFirstId + kJtManyMembers - 1);

    CHECK(Tip("member_join", ManyArgs()) == u8"「张三」邀请你和" + ManyNames() + u8"等 12 人加入了群聊");
}

TEST_CASE("JT5 入群提示：没有邀请人（自由加群、本人申请经审批）", "[systemtip]")
{
    ScopedTipI18nReset reset;
    const std::string args = u8"{\"members\":[{\"userid\":\"5308\",\"nickname\":\"李四\"}]}";
    {
        ScopedSelfUserId self(kJtBystander);
        CHECK(Tip("member_join", args) == u8"「李四」加入了群聊");
    }
    {
        ScopedSelfUserId self(kJtLisi);
        CHECK(Tip("member_join", args) == u8"你加入了群聊");
    }
}

TEST_CASE("JT6 入群提示：参数缺失时按缺省写法生成，members 缺失或为空时返回 false", "[systemtip]")
{
    ScopedTipI18nReset reset;
    ScopedSelfUserId self(kJtBystander);

    //邀请人昵称缺失：写「有人」（对照服务端 GJ5）
    CHECK(Tip("member_join", u8"{\"inviterid\":\"5127\",\"members\":[{\"userid\":\"5308\",\"nickname\":\"李四\"}]}")
          == u8"「有人」邀请「李四」加入了群聊");
    //部分新成员昵称缺失：跳过空名字，人数照算（对照服务端 GJ6）
    CHECK(Tip("member_join", u8"{\"inviter\":\"张三\",\"inviterid\":\"5127\","
                             u8"\"members\":[{\"userid\":\"5308\",\"nickname\":\"李四\"},{\"userid\":\"5309\"}]}")
          == u8"「张三」邀请「李四」等 2 人加入了群聊");

    //新成员昵称全缺：只写人数（对照服务端 GJ7）
    const std::string nameless = u8"\"members\":[{\"userid\":\"5308\"},{\"userid\":\"5309\"},{\"userid\":\"5310\"}]";
    CHECK(Tip("member_join", u8"{\"inviter\":\"张三\",\"inviterid\":\"5127\"," + nameless + "}")
          == u8"「张三」邀请 3 位新成员加入了群聊");
    CHECK(Tip("member_join", "{" + nameless + "}") == u8"3 位新成员加入了群聊");
    {
        ScopedSelfUserId inviter(kJtInviter);
        CHECK(Tip("member_join", u8"{\"inviter\":\"张三\",\"inviterid\":\"5127\"," + nameless + "}")
              == u8"你邀请 3 位新成员加入了群聊");
    }

    //members 缺失、为空、不是数组：没有可生成的名单，返回 false，调用方显示服务端的 text
    CHECK(Tip("member_join", u8"{\"inviter\":\"张三\",\"inviterid\":\"5127\"}") == "<false>");
    CHECK(Tip("member_join", u8"{\"inviter\":\"张三\",\"inviterid\":\"5127\",\"members\":[]}") == "<false>");
    CHECK(Tip("member_join", u8"{\"members\":\"5308\"}") == "<false>");
}

TEST_CASE("JT7 入群提示：没有设置本人 userid 时不出现「你」", "[systemtip]")
{
    ScopedTipI18nReset reset;
    ScopedSelfUserId self(0);

    CHECK(net::GetSystemTipSelfUserId() == 0);
    CHECK(Tip("member_join", kJtOneArgs) == u8"「张三」邀请「李四」加入了群聊");
}

TEST_CASE("JT9 入群提示：解析实时消息与会话摘要时都按本地模板改写", "[systemtip]")
{
    ScopedTipI18nReset reset;
    ScopedSelfUserId self(kJtLisi);

    //实时消息：服务端 text 故意写成别的文字，用来确认正文是按本地模板重新生成的
    const std::string body =
        u8"{\"msgid\":\"201\",\"clientmsgid\":\"sys-join-1\",\"ctype\":2,\"from\":\"0\",\"to\":\"900001\","
        u8"\"msgtype\":17,\"timestamp\":1700000000000,"
        u8"\"content\":{\"text\":\"SERVER TEXT\",\"tipkind\":\"member_join\",\"tipargs\":" + std::string(kJtOneArgs) + "}}";
    net::MessageNotify notify;
    REQUIRE(net::MessageProtocolClient::parseMessageNotify(body, notify));
    CHECK(notify.m_text == u8"「张三」邀请你加入了群聊");
    CHECK(notify.m_tipKind == "member_join");

    //会话列表：最后一条是入群提示时，摘要同样在本地生成
    const std::string conv =
        u8"{\"code\":0,\"msg\":\"ok\",\"synctime\":1,\"conversations\":["
        u8"{\"ctype\":2,\"cid\":\"900001\",\"lastmsgid\":\"201\",\"lastmsg\":{\"from\":\"0\",\"msgtype\":17,\"digest\":\"S1\","
        u8"\"tipkind\":\"member_join\",\"tipargs\":" + std::string(kJtOneArgs) + u8",\"timestamp\":1}}]}";
    net::ConversationsResult out;
    REQUIRE(net::ConversationsProtocolClient::parseSyncConversationsResponse(conv, out));
    REQUIRE(out.m_items.size() == 1);
    CHECK(out.m_items[0].m_digest == u8"「张三」邀请你加入了群聊");
}

TEST_CASE("JT10 计未读：入群提示的新成员名单里有本人时计未读，其余系统提示仍不计", "[systemtip]")
{
    //新成员本人（李四）：计未读，加群申请审批通过、被人拉进群时会话行有红点（与服务端 CVU9 同一口径）
    CHECK(net::IsMemberJoinTipForUser(kSystemTipMemberJoin, kJtOneArgs, kJtLisi));
    CHECK(net::ConversationsProtocolClient::shouldCountAsUnread(0, (unsigned int)kJtLisi, message_type_system,
                                                               kSystemTipMemberJoin, kJtOneArgs));
    //一次入群多人：每一位都算，排在后面没有昵称的也算
    CHECK(net::IsMemberJoinTipForUser(kSystemTipMemberJoin, kJtThreeArgs, kJtWangwu));
    const std::string many = ManyArgs();
    CHECK(net::IsMemberJoinTipForUser(kSystemTipMemberJoin, many, kJtManyFirstId + kJtManyMembers - 1));

    //邀请人（张三）与旁观者：不计
    CHECK_FALSE(net::IsMemberJoinTipForUser(kSystemTipMemberJoin, kJtOneArgs, kJtInviter));
    CHECK_FALSE(net::ConversationsProtocolClient::shouldCountAsUnread(0, (unsigned int)kJtInviter, message_type_system,
                                                                     kSystemTipMemberJoin, kJtOneArgs));
    CHECK_FALSE(net::ConversationsProtocolClient::shouldCountAsUnread(0, (unsigned int)kJtBystander,
                                                                     message_type_system, kSystemTipMemberJoin,
                                                                     kJtOneArgs));
    //别的系统提示（如有人退群），即使参数里有本人的 userid，也不计
    CHECK_FALSE(net::ConversationsProtocolClient::shouldCountAsUnread(0, (unsigned int)kJtLisi, message_type_system,
                                                                     "member_leave", kJtOneArgs));
    //参数不是合法 JSON、没有登录（本人 userid 为 0）：不计
    CHECK_FALSE(net::IsMemberJoinTipForUser(kSystemTipMemberJoin, "not json", kJtLisi));
    CHECK_FALSE(net::ConversationsProtocolClient::shouldCountAsUnread(0, 0, message_type_system,
                                                                     kSystemTipMemberJoin, kJtOneArgs));

    //普通消息的口径不变：别人发的计，自己发的不计
    CHECK(net::ConversationsProtocolClient::shouldCountAsUnread((unsigned int)kJtInviter, (unsigned int)kJtLisi,
                                                               message_type_text, "", ""));
    CHECK_FALSE(net::ConversationsProtocolClient::shouldCountAsUnread((unsigned int)kJtLisi, (unsigned int)kJtLisi,
                                                                     message_type_text, "", ""));
}
