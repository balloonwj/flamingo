/**
 *  v2 会话列表报文编解码（客户端侧）单元测试（Suite CV），ConversationsProtocolTests.cpp
 *  覆盖：parseSyncConversationsResponse 成功/失败应答、会话字段、64 位 ID 精度、
 *       空列表、畸形输入、跳过非法 cid。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-06-02
 */
#include "catch_amalgamated.hpp"
#include "../net/ConversationsProtocolClient.h"
#include "../net/Protocol.h"   // message_type_text / message_type_system
#include "../third-party/jsoncpp-1.9.0/json.h"

using namespace net;

//把 JSON 文本解析成 Json::Value，供断言检查 buildSetConversationRequest 的产物。
static bool parseJsonText(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

TEST_CASE("CV1 解析成功应答（含会话字段与 lastmsg）")
{
    // pinned / mute 写成 true / false —— 这是服务端<u>真实</u>的报文形态：
    // ConversationBrief 的这两个字段类型就是 bool（chatserversrc/MessageProtocol.h），
    // 序列化出来即 JSON 布尔值。本用例原先喂的是整数 1 / 0，与线上报文对不上，
    // 结果客户端把 bool 读成 0 的 bug（置顶重登后丢失）一直没被测出来。
    std::string body =
        "{\"code\":0,\"msg\":\"ok\",\"synctime\":1716268800000,"
        "\"conversations\":["
        "{\"ctype\":1,\"cid\":\"5001\",\"unread\":3,\"pinned\":true,\"mute\":false,"
        "\"lastmsgid\":\"20050\",\"lastmsg\":{\"from\":\"5001\",\"msgtype\":1,"
        "\"digest\":\"hello\",\"timestamp\":1716268800001}},"
        "{\"ctype\":1,\"cid\":\"5002\",\"unread\":0,\"pinned\":false,\"mute\":true,"
        "\"lastmsgid\":\"20040\",\"lastmsg\":{\"from\":\"8\",\"msgtype\":2,"
        "\"digest\":\"[\xE5\x9B\xBE\xE7\x89\x87]\",\"timestamp\":1716268700000}}"
        "]}";
    ConversationsResult out;
    REQUIRE(ConversationsProtocolClient::parseSyncConversationsResponse(body, out));
    CHECK(out.m_code == 0);
    CHECK(out.m_synctime == 1716268800000LL);
    REQUIRE(out.m_items.size() == 2);

    CHECK(out.m_items[0].m_ctype == 1);
    CHECK(out.m_items[0].m_cid == 5001);
    CHECK(out.m_items[0].m_unread == 3);
    CHECK(out.m_items[0].m_pinned == 1);
    CHECK(out.m_items[0].m_mute == 0);
    CHECK(out.m_items[0].m_lastmsgid == 20050);
    CHECK(out.m_items[0].m_lastFrom == 5001);
    CHECK(out.m_items[0].m_lastMsgType == 1);
    CHECK(out.m_items[0].m_digest == "hello");
    CHECK(out.m_items[0].m_timestamp == 1716268800001LL);

    CHECK(out.m_items[1].m_cid == 5002);
    CHECK(out.m_items[1].m_pinned == 0);
    CHECK(out.m_items[1].m_mute == 1);
    CHECK(out.m_items[1].m_lastMsgType == 2);
}

TEST_CASE("CV1b 置顶 / 免打扰同时兼容整数 1/0 写法")
{
    // 服务端当前发的是 bool（见 CV1），但解析必须同时容忍整数写法：一是别的端
    // （Android / 将来的 web）可能写 1/0，二是服务端字段类型若改回 int 也不该再断。
    std::string body =
        "{\"code\":0,\"synctime\":0,\"conversations\":["
        "{\"ctype\":2,\"cid\":\"7001\",\"unread\":0,\"pinned\":1,\"mute\":0,"
        "\"lastmsgid\":\"1\",\"lastmsg\":{\"from\":\"8\",\"msgtype\":1,\"digest\":\"x\",\"timestamp\":0}}"
        "]}";
    ConversationsResult out;
    REQUIRE(ConversationsProtocolClient::parseSyncConversationsResponse(body, out));
    REQUIRE(out.m_items.size() == 1);
    CHECK(out.m_items[0].m_pinned == 1);
    CHECK(out.m_items[0].m_mute == 0);
}

TEST_CASE("CV1c 缺失或类型异常的置顶 / 免打扰按未开启处理")
{
    // 字段缺失、或写成字符串这类不认识的类型，一律取 0 —— 宁可当作没置顶，
    // 也不要因为解析异常把一条普通会话顶到列表最前面。
    std::string body =
        "{\"code\":0,\"synctime\":0,\"conversations\":["
        "{\"ctype\":1,\"cid\":\"5003\",\"unread\":0,"
        "\"lastmsgid\":\"1\",\"lastmsg\":{\"from\":\"8\",\"msgtype\":1,\"digest\":\"x\",\"timestamp\":0}},"
        "{\"ctype\":1,\"cid\":\"5004\",\"unread\":0,\"pinned\":\"yes\",\"mute\":\"no\","
        "\"lastmsgid\":\"2\",\"lastmsg\":{\"from\":\"8\",\"msgtype\":1,\"digest\":\"x\",\"timestamp\":0}}"
        "]}";
    ConversationsResult out;
    REQUIRE(ConversationsProtocolClient::parseSyncConversationsResponse(body, out));
    REQUIRE(out.m_items.size() == 2);
    CHECK(out.m_items[0].m_pinned == 0);
    CHECK(out.m_items[0].m_mute == 0);
    CHECK(out.m_items[1].m_pinned == 0);
    CHECK(out.m_items[1].m_mute == 0);
}

TEST_CASE("CV2 大整数 cid/msgid 不丢精度")
{
    std::string body =
        "{\"code\":0,\"synctime\":0,\"conversations\":["
        "{\"ctype\":1,\"cid\":\"9007199254740993\",\"unread\":0,"
        "\"lastmsgid\":\"9223372036854775807\","
        "\"lastmsg\":{\"from\":\"9007199254740993\",\"msgtype\":1,\"digest\":\"x\",\"timestamp\":0}}"
        "]}";
    ConversationsResult out;
    REQUIRE(ConversationsProtocolClient::parseSyncConversationsResponse(body, out));
    REQUIRE(out.m_items.size() == 1);
    CHECK(out.m_items[0].m_cid == 9007199254740993LL);
    CHECK(out.m_items[0].m_lastmsgid == 9223372036854775807LL);
    CHECK(out.m_items[0].m_lastFrom == 9007199254740993LL);
}

TEST_CASE("CV3 空会话列表：成功但 0 条")
{
    std::string body = "{\"code\":0,\"msg\":\"ok\",\"synctime\":1,\"conversations\":[]}";
    ConversationsResult out;
    REQUIRE(ConversationsProtocolClient::parseSyncConversationsResponse(body, out));
    CHECK(out.m_code == 0);
    CHECK(out.m_items.empty());
}

TEST_CASE("CV4 失败应答：仅 code/msg 有效")
{
    std::string body = "{\"code\":2,\"msg\":\"server error\"}";
    ConversationsResult out;
    REQUIRE(ConversationsProtocolClient::parseSyncConversationsResponse(body, out));
    CHECK(out.m_code == 2);
    CHECK(out.m_msg == "server error");
    CHECK(out.m_items.empty());
}

TEST_CASE("CV5 畸形输入安全失败、跳过非法 cid 条目")
{
    ConversationsResult out;
    CHECK_FALSE(ConversationsProtocolClient::parseSyncConversationsResponse("not json", out));
    CHECK_FALSE(ConversationsProtocolClient::parseSyncConversationsResponse("", out));
    CHECK_FALSE(ConversationsProtocolClient::parseSyncConversationsResponse("[1,2]", out));
    CHECK_FALSE(ConversationsProtocolClient::parseSyncConversationsResponse("{\"msg\":\"x\"}", out));

    //cid 缺失或为 0 的会话应被跳过。
    std::string body =
        "{\"code\":0,\"conversations\":["
        "{\"ctype\":1,\"unread\":1},"
        "{\"ctype\":1,\"cid\":\"0\",\"unread\":1},"
        "{\"ctype\":1,\"cid\":\"5005\",\"unread\":2,\"lastmsg\":{\"from\":\"5005\",\"digest\":\"ok\"}}"
        "]}";
    ConversationsResult out2;
    REQUIRE(ConversationsProtocolClient::parseSyncConversationsResponse(body, out2));
    REQUIRE(out2.m_items.size() == 1);
    CHECK(out2.m_items[0].m_cid == 5005);
}

TEST_CASE("CV6 computeTotalUnread 求和未读消息总数")
{
    //空列表 → 0。
    std::vector<ConversationItem> empty;
    CHECK(ConversationsProtocolClient::computeTotalUnread(empty) == 0);

    //多条求和，含 0 未读项不影响。
    std::vector<ConversationItem> items;
    ConversationItem a;
    a.m_cid = 1;
    a.m_unread = 3;
    items.push_back(a);
    ConversationItem b;
    b.m_cid = 2;
    b.m_unread = 0;     //无未读，不计
    items.push_back(b);
    ConversationItem c;
    c.m_cid = 3;
    c.m_unread = 5;
    items.push_back(c);
    CHECK(ConversationsProtocolClient::computeTotalUnread(items) == 8);

    //防御：负值（脏数据）按 0 处理，不冲减总数。
    std::vector<ConversationItem> dirty;
    ConversationItem d;
    d.m_cid = 4;
    d.m_unread = -7;
    dirty.push_back(d);
    ConversationItem e;
    e.m_cid = 5;
    e.m_unread = 2;
    dirty.push_back(e);
    CHECK(ConversationsProtocolClient::computeTotalUnread(dirty) == 2);
}

TEST_CASE("CV7 buildSetConversationRequest 只写本次要改的字段")
{
    //仅改免打扰：含 ctype/cid/mute，不含 pinned。
    Json::Value onlyMute;
    REQUIRE(parseJsonText(
        ConversationsProtocolClient::buildSetConversationRequest(
            2, 700001, /*hasPinned*/false, false, /*hasMute*/true, true),
        onlyMute));
    CHECK(onlyMute["ctype"].isInt());
    CHECK(onlyMute["ctype"].asInt() == 2);
    //cid 以字符串编码（防大整数精度丢失）。
    CHECK(onlyMute["cid"].isString());
    CHECK(onlyMute["cid"].asString() == "700001");
    //mute 为 bool true。
    REQUIRE(onlyMute["mute"].isBool());
    CHECK(onlyMute["mute"].asBool() == true);
    //未要改置顶 → 不出现 pinned 字段。
    CHECK_FALSE(onlyMute.isMember("pinned"));

    //仅改置顶：含 pinned，不含 mute。
    Json::Value onlyPin;
    REQUIRE(parseJsonText(
        ConversationsProtocolClient::buildSetConversationRequest(
            2, 700001, /*hasPinned*/true, false, /*hasMute*/false, false),
        onlyPin));
    REQUIRE(onlyPin["pinned"].isBool());
    CHECK(onlyPin["pinned"].asBool() == false);
    CHECK_FALSE(onlyPin.isMember("mute"));

    //两者都改：pinned 与 mute 同在。
    Json::Value both;
    REQUIRE(parseJsonText(
        ConversationsProtocolClient::buildSetConversationRequest(
            1, 5001, /*hasPinned*/true, true, /*hasMute*/true, false),
        both));
    REQUIRE(both["pinned"].isBool());
    REQUIRE(both["mute"].isBool());
    CHECK(both["pinned"].asBool() == true);
    CHECK(both["mute"].asBool() == false);
    CHECK(both["cid"].asString() == "5001");
    CHECK(both["ctype"].asInt() == 1);
}

TEST_CASE("CV8 buildSetConversationRequest 大 cid 不丢精度")
{
    Json::Value root;
    REQUIRE(parseJsonText(
        ConversationsProtocolClient::buildSetConversationRequest(
            2, 9007199254740993LL, false, false, true, true),
        root));
    CHECK(root["cid"].asString() == "9007199254740993");
}

TEST_CASE("CV9 buildDeleteConversationRequest 只写 deleted，不碰置顶 / 免打扰")
{
    Json::Value root;
    REQUIRE(parseJsonText(
        ConversationsProtocolClient::buildDeleteConversationRequest(1, 5001),
        root));
    CHECK(root["ctype"].isInt());
    CHECK(root["ctype"].asInt() == 1);
    CHECK(root["cid"].isString());
    CHECK(root["cid"].asString() == "5001");
    //deleted 必须是 bool：服务端 parseSetConversationRequest 用 isBool() 取值，
    //写成数字 1 会被当成"本次不改删除标记"整条忽略，删除就静默失效了。
    REQUIRE(root["deleted"].isBool());
    CHECK(root["deleted"].asBool() == true);
    //删除会话不应顺带改置顶 / 免打扰——这两个字段不出现，服务端才会保持原值。
    CHECK_FALSE(root.isMember("pinned"));
    CHECK_FALSE(root.isMember("mute"));
}

TEST_CASE("CV10 buildDeleteConversationRequest 大 cid 不丢精度")
{
    //群 id 落在高位 id 空间，超过 double 能精确表示的范围时必须靠字符串编码。
    Json::Value root;
    REQUIRE(parseJsonText(
        ConversationsProtocolClient::buildDeleteConversationRequest(2, 9007199254740993LL),
        root));
    CHECK(root["ctype"].asInt() == 2);
    CHECK(root["cid"].isString());
    CHECK(root["cid"].asString() == "9007199254740993");
}

// =====================================================================
// CC. parseConversationChangedNotify —— 会话属性多端同步推送(1514)
// =====================================================================

TEST_CASE("CC1 解析服务端推送：bool 形态的 pinned / mute / deleted")
{
    //服务端 MessageProtocol::buildConversationChangedNotify 写的就是这个形状：
    //ctype 整数、cid 字符串、三个开关是 JSON bool。
    const std::string body =
        "{\"ctype\":2,\"cid\":\"700001\",\"pinned\":true,\"mute\":false,\"deleted\":false}";

    ConversationChangedNotify n;
    REQUIRE(ConversationsProtocolClient::parseConversationChangedNotify(body, n));
    CHECK(n.m_ctype == 2);
    CHECK(n.m_cid == 700001);
    CHECK(n.m_pinned == 1);
    CHECK(n.m_mute == 0);
    CHECK(n.m_deleted == 0);
}

TEST_CASE("CC2 删除推送三个开关都解析正确")
{
    const std::string body =
        "{\"ctype\":1,\"cid\":\"5001\",\"pinned\":false,\"mute\":true,\"deleted\":true}";

    ConversationChangedNotify n;
    REQUIRE(ConversationsProtocolClient::parseConversationChangedNotify(body, n));
    CHECK(n.m_ctype == 1);
    CHECK(n.m_cid == 5001);
    CHECK(n.m_pinned == 0);
    CHECK(n.m_mute == 1);
    CHECK(n.m_deleted == 1);
}

TEST_CASE("CC3 开关写成整数 0/1 也能解析")
{
    //防御性兼容：哪天服务端把 bool 改成 0/1，本端不该静默失效（那会表现为
    //"另一端删了会话，这一端毫无反应"，极难排查）。
    const std::string body =
        "{\"ctype\":1,\"cid\":\"5001\",\"pinned\":1,\"mute\":0,\"deleted\":1}";

    ConversationChangedNotify n;
    REQUIRE(ConversationsProtocolClient::parseConversationChangedNotify(body, n));
    CHECK(n.m_pinned == 1);
    CHECK(n.m_mute == 0);
    CHECK(n.m_deleted == 1);
}

TEST_CASE("CC4 大 cid 用字符串编码不丢精度")
{
    const std::string body =
        "{\"ctype\":2,\"cid\":\"9007199254740993\",\"pinned\":false,\"mute\":false,\"deleted\":true}";

    ConversationChangedNotify n;
    REQUIRE(ConversationsProtocolClient::parseConversationChangedNotify(body, n));
    CHECK(n.m_cid == 9007199254740993LL);
}

TEST_CASE("CC5 缺字段按默认值兜底，ctype 缺失按单聊处理")
{
    //只给 cid：其余按"未置顶、未免打扰、未删除、单聊"处理，不至于把一条推送
    //误读成删除指令。
    ConversationChangedNotify n;
    REQUIRE(ConversationsProtocolClient::parseConversationChangedNotify(
        "{\"cid\":\"5001\"}", n));
    CHECK(n.m_ctype == 1);
    CHECK(n.m_cid == 5001);
    CHECK(n.m_pinned == 0);
    CHECK(n.m_mute == 0);
    CHECK(n.m_deleted == 0);
}

TEST_CASE("CC6 非法 JSON 与非法 cid 一律拒绝")
{
    ConversationChangedNotify n;
    //非法 JSON。
    CHECK_FALSE(ConversationsProtocolClient::parseConversationChangedNotify("{oops", n));
    //JSON 合法但不是对象。
    CHECK_FALSE(ConversationsProtocolClient::parseConversationChangedNotify("[1,2]", n));
    //缺 cid：无从对应到任何会话。
    CHECK_FALSE(ConversationsProtocolClient::parseConversationChangedNotify(
        "{\"ctype\":1,\"deleted\":true}", n));
    //cid 为 0 / 负数：同上，且若当成有效值会去删一条不存在的会话。
    CHECK_FALSE(ConversationsProtocolClient::parseConversationChangedNotify(
        "{\"ctype\":1,\"cid\":\"0\",\"deleted\":true}", n));
    CHECK_FALSE(ConversationsProtocolClient::parseConversationChangedNotify(
        "{\"ctype\":1,\"cid\":\"-5\",\"deleted\":true}", n));
}

// =====================================================================
// UN. shouldCountAsUnread —— 决定收到的推送要不要给会话未读数 +1
// =====================================================================

TEST_CASE("UN1 他人发来的消息计未读")
{
    const unsigned int kSelf = 5001;
    const unsigned int kPeer = 5002;
    CHECK(ConversationsProtocolClient::shouldCountAsUnread(kPeer, kSelf, message_type_text));
}

TEST_CASE("UN2 自己发出、又被服务端回推回来的消息不计未读")
{
    //服务端把群消息推给包括发送者在内的全体成员，单聊多端登录时也会把自己发的
    //消息同步回本端。这类推送不能给自己点亮红点。
    const unsigned int kSelf = 5001;
    CHECK_FALSE(ConversationsProtocolClient::shouldCountAsUnread(kSelf, kSelf, message_type_text));
}

TEST_CASE("UN3 取不到本端 userid 时一律不计未读")
{
    //未登录 / 兜底态下无法断言消息来自他人，宁可不加，也不要误点红点。
    CHECK_FALSE(ConversationsProtocolClient::shouldCountAsUnread(5002, 0, message_type_text));
    CHECK_FALSE(ConversationsProtocolClient::shouldCountAsUnread(0, 0, message_type_text));
}

TEST_CASE("UN-SYS 服务端生成的群系统消息不计未读，他人的普通消息照常计", "[groupexit][sysmsg]")
{
    //系统消息（发布公告、入群欢迎语、退出群聊、转让群主）是群里的变动通知，不是有人对你说话；
    //服务端算会话未读数时同样不计（MessageManager 的群未读查询），两边口径一致
    const unsigned int kSelf = 5001;
    const unsigned int kSystemSender = 0;
    CHECK_FALSE(ConversationsProtocolClient::shouldCountAsUnread(kSystemSender, kSelf, message_type_system));
    //即便发送者字段不是 0，只要类型是系统消息也不计
    CHECK_FALSE(ConversationsProtocolClient::shouldCountAsUnread(5002, kSelf, message_type_system));
    //他人的普通消息、图片照常计；自己发的照旧不计
    CHECK(ConversationsProtocolClient::shouldCountAsUnread(5002, kSelf, message_type_text));
    CHECK(ConversationsProtocolClient::shouldCountAsUnread(5002, kSelf, message_type_image));
    CHECK_FALSE(ConversationsProtocolClient::shouldCountAsUnread(kSelf, kSelf, message_type_image));
}
