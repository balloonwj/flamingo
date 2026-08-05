/**
 *  v2 单聊消息报文编解码（客户端侧）单元测试（Suite MP），MessageProtocolTests.cpp
 *  覆盖：buildSendTextRequest 字段完整性与类型、parseSendResponse 成功/失败
 *       应答、parseMessageNotify 推送解析、64 位 ID 精度（字符串编码）、
 *       文本含中文/emoji/引号/换行的 round-trip、畸形输入安全失败；
 *       图片消息（MP-IMG 系列）：buildSendImageRequest 单聊/群聊 content、
 *       parseMessageNotify / parseSyncMessagesResponse 对 msgtype=2 的图片字段解析；
 *       消息 Pin（MP-PIN 系列）：请求构造、应答与推送解析、被 Pin 消息列表的
 *       total 与 pin 子对象（见 docs/消息Pin接入方案.md）。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-06-01
 */
#include "catch_amalgamated.hpp"
#include "../net/MessageProtocolClient.h"
#include "../net/Protocol.h"            // conversation_single（撤回推送缺 ctype 时的缺省值断言）
//见 MessageProtocolClient.cpp 同处说明：屏蔽 vendored json.h 的 C4828。
#pragma warning(push)
#pragma warning(disable: 4828)
#include "../jsoncpp-1.9.0/json.h"
#pragma warning(pop)

using namespace net;

//把 JSON 文本解析成 Json::Value，供断言检查 buildSendTextRequest 的产物
static bool parseJsonText(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

//把 Json::Value 序列化为紧凑 JSON 文本，供构造测试用的推送/应答报文
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

// =====================================================================
// MP-RC. 撤回消息（cmd_recall_message / cmd_message_recalled_notify）
// =====================================================================

TEST_CASE("MP-RC1 buildRecallMessageRequest 字段完整且 id 以字符串编码")
{
    //群聊撤回：ctype=2、cid 取群 id、msgid 为被撤回消息的服务器 ID。
    std::string json = MessageProtocolClient::buildRecallMessageRequest(2, 70001, 900000000123LL);
    REQUIRE_FALSE(json.empty());

    Json::Value root;
    REQUIRE(parseJsonText(json, root));

    CHECK(root["ctype"].isInt());
    CHECK(root["ctype"].asInt() == 2);

    //cid / msgid 必须是字符串，否则大整数在 JSON 数值里会丢精度（与服务端 getStringId 契约一致）。
    REQUIRE(root["cid"].isString());
    CHECK(root["cid"].asString() == "70001");
    REQUIRE(root["msgid"].isString());
    CHECK(root["msgid"].asString() == "900000000123");
}

TEST_CASE("MP-RC2 buildRecallMessageRequest 对非法 cid / msgid 返回空串")
{
    //服务端 parseRecallMessageRequest 对 cid / msgid <=0 判非法；客户端提前挡住，
    //返回空串让调用方放弃发送，省一次必然失败的往返。
    CHECK(MessageProtocolClient::buildRecallMessageRequest(1, 0, 100).empty());
    CHECK(MessageProtocolClient::buildRecallMessageRequest(1, -1, 100).empty());
    CHECK(MessageProtocolClient::buildRecallMessageRequest(1, 5002, 0).empty());
    CHECK(MessageProtocolClient::buildRecallMessageRequest(1, 5002, -8).empty());

    //两者都合法才产出报文。
    CHECK_FALSE(MessageProtocolClient::buildRecallMessageRequest(1, 5002, 100).empty());
}

TEST_CASE("MP-RC3 parseRecallResponse 区分成功与各类失败码")
{
    //成功
    Json::Value ok;
    ok["code"] = 0;
    ok["msg"]  = "ok";
    RecallMessageResponse rOk;
    REQUIRE(MessageProtocolClient::parseRecallResponse(writeCompact(ok), rOk));
    CHECK(rOk.m_code == 0);
    CHECK(rOk.m_msg == "ok");

    //超出可撤回时限（501）——上层据此提示"消息超过 2 分钟，无法撤回"
    Json::Value timeout;
    timeout["code"] = 501;
    timeout["msg"]  = "recall timeout";
    RecallMessageResponse rTimeout;
    REQUIRE(MessageProtocolClient::parseRecallResponse(writeCompact(timeout), rTimeout));
    CHECK(rTimeout.m_code == 501);

    //无权撤回（502）
    Json::Value denied;
    denied["code"] = 502;
    RecallMessageResponse rDenied;
    REQUIRE(MessageProtocolClient::parseRecallResponse(writeCompact(denied), rDenied));
    CHECK(rDenied.m_code == 502);
    CHECK(rDenied.m_msg.empty());   //msg 缺失时留空，不影响 code 判定

    //消息不存在（503）
    Json::Value missing;
    missing["code"] = 503;
    RecallMessageResponse rMissing;
    REQUIRE(MessageProtocolClient::parseRecallResponse(writeCompact(missing), rMissing));
    CHECK(rMissing.m_code == 503);
}

TEST_CASE("MP-RC4 parseRecallResponse 对畸形报文安全失败且不误判成功")
{
    RecallMessageResponse resp;

    CHECK_FALSE(MessageProtocolClient::parseRecallResponse("", resp));
    CHECK_FALSE(MessageProtocolClient::parseRecallResponse("not a json", resp));
    CHECK_FALSE(MessageProtocolClient::parseRecallResponse("[1,2,3]", resp));   //顶层非对象
    CHECK_FALSE(MessageProtocolClient::parseRecallResponse("{}", resp));        //缺 code
    CHECK_FALSE(MessageProtocolClient::parseRecallResponse("{\"code\":\"0\"}", resp));  //code 非整型

    //全程未被写成 0，避免"解析失败却被当成撤回成功"。
    CHECK(resp.m_code != 0);
}

TEST_CASE("MP-RC5 parseMessageRecalledNotify 取出四个字段")
{
    Json::Value n;
    n["ctype"]    = 2;
    n["cid"]      = "70001";
    n["msgid"]    = "900000000456";
    n["operator"] = "5002";

    MessageRecalledNotify notify;
    REQUIRE(MessageProtocolClient::parseMessageRecalledNotify(writeCompact(n), notify));
    CHECK(notify.m_ctype == 2);
    CHECK(notify.m_cid == 70001);
    CHECK(notify.m_msgid == 900000000456LL);   //64 位 ID 经字符串编码后精度无损
    CHECK(notify.m_operator == 5002);
}

TEST_CASE("MP-RC6 parseMessageRecalledNotify 缺 ctype 按单聊、缺 operator 取 0")
{
    //服务端对 ctype 的容错口径是"缺省按单聊"，客户端保持一致。
    Json::Value n;
    n["cid"]   = "5002";
    n["msgid"] = "123";

    MessageRecalledNotify notify;
    REQUIRE(MessageProtocolClient::parseMessageRecalledNotify(writeCompact(n), notify));
    CHECK(notify.m_ctype == conversation_single);
    CHECK(notify.m_cid == 5002);
    CHECK(notify.m_msgid == 123);
    //operator 缺失时取 0，上层据此按"他人撤回"退化处理（不会误判成自己撤回）。
    CHECK(notify.m_operator == 0);
}

TEST_CASE("MP-RC7 parseMessageRecalledNotify 对畸形报文与非正 id 安全失败")
{
    MessageRecalledNotify notify;

    CHECK_FALSE(MessageProtocolClient::parseMessageRecalledNotify("", notify));
    CHECK_FALSE(MessageProtocolClient::parseMessageRecalledNotify("{oops", notify));
    CHECK_FALSE(MessageProtocolClient::parseMessageRecalledNotify("[]", notify));

    //cid / msgid 是"定位哪个会话的哪条消息"的关键，缺失或非正一律整条丢弃。
    CHECK_FALSE(MessageProtocolClient::parseMessageRecalledNotify(
        "{\"ctype\":1,\"msgid\":\"123\"}", notify));                       //缺 cid
    CHECK_FALSE(MessageProtocolClient::parseMessageRecalledNotify(
        "{\"ctype\":1,\"cid\":\"5002\"}", notify));                        //缺 msgid
    CHECK_FALSE(MessageProtocolClient::parseMessageRecalledNotify(
        "{\"ctype\":1,\"cid\":\"0\",\"msgid\":\"123\"}", notify));         //cid 为 0
    CHECK_FALSE(MessageProtocolClient::parseMessageRecalledNotify(
        "{\"ctype\":1,\"cid\":\"5002\",\"msgid\":\"-1\"}", notify));       //msgid 为负
}

// =====================================================================
// MP-RT. 撤回时限判断（canRecallMessage）——决定右键菜单出不出"撤回"
// =====================================================================

TEST_CASE("MP-RT1 时限内可撤回，超时不可撤回，边界取严格大于")
{
    const int64_t kSent = 1700000000000LL;   //消息发出时刻（Unix 毫秒）

    //刚发出
    CHECK(MessageProtocolClient::canRecallMessage(100, kSent, kSent));
    //差 1 分 59 秒，仍在 2 分钟内
    CHECK(MessageProtocolClient::canRecallMessage(100, kSent, kSent + 119 * 1000));
    //正好卡在 2 分钟：服务端用 "now - timestamp > 限值" 判超时，等于不算超时，故仍可撤回
    CHECK(MessageProtocolClient::canRecallMessage(100, kSent, kSent + kRecallTimeLimitMs));
    //超出 1 毫秒即不可撤回
    CHECK_FALSE(MessageProtocolClient::canRecallMessage(100, kSent, kSent + kRecallTimeLimitMs + 1));
    //远超时限
    CHECK_FALSE(MessageProtocolClient::canRecallMessage(100, kSent, kSent + 24 * 60 * 60 * 1000LL));
}

TEST_CASE("MP-RT2 服务器 msgid 未回填的消息不可撤回")
{
    const int64_t kSent = 1700000000000LL;
    //本端刚发出、还在等回执的"发送中"气泡：服务端还不认识这条消息，
    //发撤回请求必然被判 message_not_exist，故菜单里不出现"撤回"。
    CHECK_FALSE(MessageProtocolClient::canRecallMessage(0, kSent, kSent));
    CHECK_FALSE(MessageProtocolClient::canRecallMessage(-1, kSent, kSent));
}

TEST_CASE("MP-RT3 时间戳无效不可撤回，时钟回拨不误判超时")
{
    const int64_t kSent = 1700000000000LL;

    //时间未知（0 或负）无法与时限比较，保守判为不可撤回。
    CHECK_FALSE(MessageProtocolClient::canRecallMessage(100, 0, kSent));
    CHECK_FALSE(MessageProtocolClient::canRecallMessage(100, -1, kSent));

    //系统时钟回拨导致 now 早于消息时间：差值为负，不应被当成超时。
    CHECK(MessageProtocolClient::canRecallMessage(100, kSent, kSent - 60 * 1000));
}

TEST_CASE("MP1 buildSendTextRequest 字段完整且类型正确")
{
    //对端 userid=5002，文本为普通 ASCII
    std::string json = MessageProtocolClient::buildSendTextRequest(
        5002, "cmid-0001", "hello world");

    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    CHECK(root["clientmsgid"].isString());
    CHECK(root["clientmsgid"].asString() == "cmid-0001");
    //会话类型单聊=1，且为整型
    CHECK(root["ctype"].isInt());
    CHECK(root["ctype"].asInt() == 1);
    //cid 必须为字符串编码（防大整数精度丢失），值为对端 userid
    CHECK(root["cid"].isString());
    CHECK(root["cid"].asString() == "5002");
    //文本消息 msgtype=1，且为整型
    CHECK(root["msgtype"].isInt());
    CHECK(root["msgtype"].asInt() == 1);
    //正文承载在 content.text
    CHECK(root["content"].isObject());
    CHECK(root["content"]["text"].isString());
    CHECK(root["content"]["text"].asString() == "hello world");
}

TEST_CASE("MP2 parseSendResponse 解析成功回执")
{
    std::string body =
        "{\"code\":0,\"msg\":\"ok\",\"clientmsgid\":\"cmid-0001\","
        "\"msgid\":\"10086\",\"timestamp\":1716268800000}";
    SendMessageResponse resp;
    REQUIRE(MessageProtocolClient::parseSendResponse(body, resp));
    CHECK(resp.m_code == 0);
    CHECK(resp.m_msg == "ok");
    CHECK(resp.m_clientmsgid == "cmid-0001");
    CHECK(resp.m_msgid == 10086);
    CHECK(resp.m_timestamp == 1716268800000LL);
}

TEST_CASE("MP2b parseSendResponse 解析失败回执（仅 code/msg 有效）")
{
    //对端不存在等失败：服务端只回 code/msg（见 buildSimpleResponse）
    std::string body = "{\"code\":201,\"msg\":\"target user not found\"}";
    SendMessageResponse resp;
    REQUIRE(MessageProtocolClient::parseSendResponse(body, resp));
    CHECK(resp.m_code == 201);
    CHECK(resp.m_msg == "target user not found");
    //失败应答不带 msgid，保持默认 0
    CHECK(resp.m_msgid == 0);
}

TEST_CASE("MP3 parseMessageNotify 解析文本推送")
{
    //收件人视角：cid=发送方 userid；from/to/cid 均字符串编码
    std::string body =
        "{\"msgid\":\"20001\",\"clientmsgid\":\"cmid-abc\",\"ctype\":1,"
        "\"cid\":\"5001\",\"from\":\"5001\",\"to\":\"5002\",\"msgtype\":1,"
        "\"content\":{\"text\":\"hi there\"},\"mentionall\":false,"
        "\"timestamp\":1716268800001,\"recalled\":false}";
    MessageNotify notify;
    REQUIRE(MessageProtocolClient::parseMessageNotify(body, notify));
    CHECK(notify.m_msgid == 20001);
    CHECK(notify.m_clientmsgid == "cmid-abc");
    CHECK(notify.m_ctype == 1);
    CHECK(notify.m_cid == 5001);
    CHECK(notify.m_from == 5001);
    CHECK(notify.m_to == 5002);
    CHECK(notify.m_msgtype == 1);
    CHECK(notify.m_text == "hi there");
    CHECK(notify.m_timestamp == 1716268800001LL);
    CHECK(notify.m_recalled == false);
}

TEST_CASE("MP4 大整数 ID 不丢精度（字符串编码）")
{
    //msgid 与 cid 取超过 2^53 的值，验证字符串编码路径精确还原
    const int64_t bigMsgId = 9007199254740993LL;   // 2^53 + 1
    const int64_t bigCid   = 9223372036854775807LL; // int64 max
    Json::Value root;
    root["msgid"]   = std::to_string(bigMsgId);
    root["from"]    = std::to_string(bigCid);
    root["cid"]     = std::to_string(bigCid);
    root["to"]      = "5002";
    root["ctype"]   = 1;
    root["msgtype"] = 1;
    Json::Value content;
    content["text"] = "x";
    root["content"] = content;
    root["timestamp"] = static_cast<Json::Int64>(0);

    MessageNotify notify;
    REQUIRE(MessageProtocolClient::parseMessageNotify(writeCompact(root), notify));
    CHECK(notify.m_msgid == bigMsgId);
    CHECK(notify.m_cid == bigCid);
    CHECK(notify.m_from == bigCid);
}

TEST_CASE("MP5 文本含中文/emoji/引号/换行 round-trip 正确")
{
    //真实运行期窗口会把 CString 转 UTF-8 再交给本类，故此处用 u8 字面量
    const std::string tricky = u8"你好\"世界\"\n第二行 😀 \\tab";

    //发送侧：构造请求后反解，content.text 应原样还原（验证 JSON 转义）
    std::string req = MessageProtocolClient::buildSendTextRequest(5002, "cmid-x", tricky);
    Json::Value reqRoot;
    REQUIRE(parseJsonText(req, reqRoot));
    CHECK(reqRoot["content"]["text"].asString() == tricky);

    //接收侧：把同一文本塞进推送报文再 parse，m_text 应原样还原（验证反转义）
    Json::Value n;
    n["msgid"]   = "30001";
    n["from"]    = "5001";
    n["cid"]     = "5001";
    n["to"]      = "5002";
    n["ctype"]   = 1;
    n["msgtype"] = 1;
    Json::Value content;
    content["text"] = tricky;
    n["content"] = content;
    n["timestamp"] = static_cast<Json::Int64>(0);

    MessageNotify notify;
    REQUIRE(MessageProtocolClient::parseMessageNotify(writeCompact(n), notify));
    CHECK(notify.m_text == tricky);
}

TEST_CASE("MP6 畸形输入安全失败、不崩溃")
{
    SendMessageResponse resp;
    //非 JSON / 空串 / 合法 JSON 但非对象
    CHECK_FALSE(MessageProtocolClient::parseSendResponse("not a json", resp));
    CHECK_FALSE(MessageProtocolClient::parseSendResponse("", resp));
    CHECK_FALSE(MessageProtocolClient::parseSendResponse("[1,2,3]", resp));
    //缺 code / code 类型错误
    CHECK_FALSE(MessageProtocolClient::parseSendResponse("{\"msg\":\"x\"}", resp));
    CHECK_FALSE(MessageProtocolClient::parseSendResponse("{\"code\":\"0\"}", resp));

    MessageNotify notify;
    //非 JSON / 空串
    CHECK_FALSE(MessageProtocolClient::parseMessageNotify("not a json", notify));
    CHECK_FALSE(MessageProtocolClient::parseMessageNotify("", notify));
    //缺 msgid / 缺 from（关键字段）
    CHECK_FALSE(MessageProtocolClient::parseMessageNotify("{\"from\":\"5001\"}", notify));
    CHECK_FALSE(MessageProtocolClient::parseMessageNotify("{\"msgid\":\"1\"}", notify));
    //content 非对象时不应崩溃，且因关键字段齐备仍可解析成功、m_text 为空
    MessageNotify n2;
    std::string body =
        "{\"msgid\":\"1\",\"from\":\"5001\",\"to\":\"5002\",\"ctype\":1,"
        "\"msgtype\":1,\"content\":\"oops\"}";
    REQUIRE(MessageProtocolClient::parseMessageNotify(body, n2));
    CHECK(n2.m_text.empty());
}

TEST_CASE("MP7 buildSendShakeRequest msgtype=13 且 content 为空对象")
{
    std::string json = MessageProtocolClient::buildSendShakeRequest(5002, "cmid-shake");
    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    CHECK(root["clientmsgid"].asString() == "cmid-shake");
    //会话类型单聊=1
    CHECK(root["ctype"].isInt());
    CHECK(root["ctype"].asInt() == 1);
    //cid 字符串编码，值为对端 userid
    CHECK(root["cid"].isString());
    CHECK(root["cid"].asString() == "5002");
    //抖动 msgtype=13
    CHECK(root["msgtype"].isInt());
    CHECK(root["msgtype"].asInt() == 13);
    //content 为对象且为空（不是 null、不带任何字段）
    CHECK(root["content"].isObject());
    CHECK(root["content"].size() == 0);
}

TEST_CASE("MP8 buildSendShakeRequest 大 cid 不丢精度")
{
    const int64_t bigCid = 9007199254740993LL;   // 2^53 + 1
    std::string json = MessageProtocolClient::buildSendShakeRequest(bigCid, "cmid-x");
    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    CHECK(root["cid"].isString());
    CHECK(root["cid"].asString() == "9007199254740993");
}

TEST_CASE("MP10 buildSendGroupTextRequest ctype=2 且 cid 为群id字符串")
{
    std::string json = MessageProtocolClient::buildSendGroupTextRequest(
        700001, "cmid-g", "hi group");
    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    CHECK(root["clientmsgid"].asString() == "cmid-g");
    //群聊 ctype=2
    CHECK(root["ctype"].isInt());
    CHECK(root["ctype"].asInt() == 2);
    //cid 字符串编码，值为群id
    CHECK(root["cid"].isString());
    CHECK(root["cid"].asString() == "700001");
    //文本 msgtype=1，正文在 content.text
    CHECK(root["msgtype"].asInt() == 1);
    CHECK(root["content"]["text"].asString() == "hi group");
}

TEST_CASE("MP11 buildSendGroupTextRequest 大群id不丢精度")
{
    std::string json = MessageProtocolClient::buildSendGroupTextRequest(
        9007199254740993LL, "cmid-x", "x");
    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    CHECK(root["cid"].asString() == "9007199254740993");
}

TEST_CASE("MP12 buildSyncMessagesRequest 字段与大 id 字符串编码")
{
    //拉群最近 100 条：ctype=2、beforemsgid=0、limit=100
    std::string json = MessageProtocolClient::buildSyncMessagesRequest(2, 700001, 0, 100);
    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    CHECK(root["ctype"].asInt() == 2);
    CHECK(root["cid"].isString());
    CHECK(root["cid"].asString() == "700001");
    CHECK(root["beforemsgid"].isString());
    CHECK(root["beforemsgid"].asString() == "0");
    CHECK(root["limit"].asInt() == 100);

    //大 cid / beforemsgid 不丢精度
    std::string j2 = MessageProtocolClient::buildSyncMessagesRequest(
        1, 9007199254740993LL, 9223372036854775807LL, 20);
    Json::Value r2;
    REQUIRE(parseJsonText(j2, r2));
    CHECK(r2["cid"].asString() == "9007199254740993");
    CHECK(r2["beforemsgid"].asString() == "9223372036854775807");
}

TEST_CASE("MP13 parseSyncMessagesResponse 解析多条历史 + hasmore")
{
    std::string body =
        "{\"code\":0,\"msg\":\"ok\",\"ctype\":1,\"cid\":\"5001\",\"hasmore\":true,"
        "\"messages\":["
        "{\"msgid\":\"20002\",\"from\":\"5001\",\"to\":\"5002\",\"ctype\":1,\"cid\":\"5001\","
        "\"msgtype\":1,\"content\":{\"text\":\"newer\"},\"timestamp\":1716268800002},"
        "{\"msgid\":\"20001\",\"from\":\"5002\",\"to\":\"5001\",\"ctype\":1,\"cid\":\"5001\","
        "\"msgtype\":1,\"content\":{\"text\":\"older\"},\"timestamp\":1716268800001}"
        "]}";
    SyncMessagesResult out;
    REQUIRE(MessageProtocolClient::parseSyncMessagesResponse(body, out));
    CHECK(out.m_code == 0);
    CHECK(out.m_ctype == 1);
    CHECK(out.m_cid == 5001);
    CHECK(out.m_hasmore == true);
    REQUIRE(out.m_messages.size() == 2);
    //本用例验证的是「解析器保序」：解析结果的次序与 JSON 数组的次序一一对应，
    //解析器自己不做任何重排。故这里断言 messages[0] 就是数组里的第一个元素。
    //注意别把本用例的取数当成服务端的真实排序 —— 真实的 cmd_sync_messages 应答
    //是按 msgid 升序（最旧在前）给的，见 MessageManager.cpp 翻历史模式的反转。
    CHECK(out.m_messages[0].m_msgid == 20002);
    CHECK(out.m_messages[0].m_from == 5001);
    CHECK(out.m_messages[0].m_text == "newer");
    CHECK(out.m_messages[1].m_msgid == 20001);
    CHECK(out.m_messages[1].m_text == "older");
}

TEST_CASE("MP14 parseSyncMessagesResponse 大 id/空/跳过脏条目/失败/畸形")
{
    //大 msgid 不丢精度。
    std::string big =
        "{\"code\":0,\"ctype\":2,\"cid\":\"700001\",\"hasmore\":false,\"messages\":["
        "{\"msgid\":\"9007199254740993\",\"from\":\"9007199254740993\",\"ctype\":2,"
        "\"cid\":\"700001\",\"msgtype\":1,\"content\":{\"text\":\"x\"},\"timestamp\":0}]}";
    SyncMessagesResult b;
    REQUIRE(MessageProtocolClient::parseSyncMessagesResponse(big, b));
    REQUIRE(b.m_messages.size() == 1);
    CHECK(b.m_messages[0].m_msgid == 9007199254740993LL);

    //空 messages：成功但 0 条。
    SyncMessagesResult empty;
    REQUIRE(MessageProtocolClient::parseSyncMessagesResponse(
        "{\"code\":0,\"ctype\":1,\"cid\":\"1\",\"hasmore\":false,\"messages\":[]}", empty));
    CHECK(empty.m_messages.empty());

    //缺 msgid/from 的脏条目被跳过。
    SyncMessagesResult skip;
    REQUIRE(MessageProtocolClient::parseSyncMessagesResponse(
        "{\"code\":0,\"cid\":\"1\",\"messages\":["
        "{\"from\":\"5\"},{\"msgid\":\"9\",\"from\":\"5\",\"content\":{\"text\":\"ok\"}}]}", skip));
    REQUIRE(skip.m_messages.size() == 1);
    CHECK(skip.m_messages[0].m_msgid == 9);

    //失败应答 + 畸形输入。
    SyncMessagesResult fail;
    REQUIRE(MessageProtocolClient::parseSyncMessagesResponse("{\"code\":2,\"msg\":\"no\"}", fail));
    CHECK(fail.m_code == 2);
    CHECK(fail.m_messages.empty());
    SyncMessagesResult bad;
    CHECK_FALSE(MessageProtocolClient::parseSyncMessagesResponse("nope", bad));
}

TEST_CASE("MP-IMG1 buildSendImageRequest 单聊 content 字段完整")
{
    //单聊图片：ctype=1、cid=对端 userid，content 含 fileid/md5/width/height/size
    std::string json = MessageProtocolClient::buildSendImageRequest(
        1, 5002, "cmid-img", "10005", "9b1c0000000000000000000000000e7a", 240, 140, 18324);
    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    CHECK(root["clientmsgid"].asString() == "cmid-img");
    CHECK(root["ctype"].asInt() == 1);
    CHECK(root["cid"].isString());
    CHECK(root["cid"].asString() == "5002");
    //图片 msgtype=2
    CHECK(root["msgtype"].asInt() == 2);
    //content 各字段：fileid 字符串编码、宽高为整型、size 为数值
    CHECK(root["content"]["fileid"].isString());
    CHECK(root["content"]["fileid"].asString() == "10005");
    CHECK(root["content"]["md5"].asString() == "9b1c0000000000000000000000000e7a");
    CHECK(root["content"]["width"].asInt() == 240);
    CHECK(root["content"]["height"].asInt() == 140);
    CHECK(root["content"]["size"].asInt64() == 18324);
    //本轮不带缩略图
    CHECK_FALSE(root["content"].isMember("thumbfileid"));
}

TEST_CASE("MP-IMG2 buildSendImageRequest 群聊 ctype=2 且 cid 为群id字符串")
{
    std::string json = MessageProtocolClient::buildSendImageRequest(
        2, 700001, "cmid-gimg", "10006", "0123456789abcdef0123456789abcdef", 320, 320, 40960);
    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    CHECK(root["ctype"].asInt() == 2);
    CHECK(root["cid"].isString());
    CHECK(root["cid"].asString() == "700001");
    CHECK(root["msgtype"].asInt() == 2);
    CHECK(root["content"]["fileid"].asString() == "10006");
}

TEST_CASE("MP-IMG3 parseMessageNotify 解析图片推送")
{
    //收到图片消息：msgtype=2，content 为图片元信息
    std::string body =
        "{\"msgid\":\"20100\",\"clientmsgid\":\"cmid-img\",\"ctype\":1,"
        "\"cid\":\"5001\",\"from\":\"5001\",\"to\":\"5002\",\"msgtype\":2,"
        "\"content\":{\"fileid\":\"10005\",\"md5\":\"9b1c0000000000000000000000000e7a\","
        "\"width\":240,\"height\":140,\"size\":18324},\"timestamp\":1716268800002}";
    MessageNotify notify;
    REQUIRE(MessageProtocolClient::parseMessageNotify(body, notify));
    CHECK(notify.m_msgid == 20100);
    CHECK(notify.m_msgtype == 2);
    CHECK(notify.m_imageFileId == "10005");
    CHECK(notify.m_imageMd5 == "9b1c0000000000000000000000000e7a");
    CHECK(notify.m_imageWidth == 240);
    CHECK(notify.m_imageHeight == 140);
    CHECK(notify.m_imageSize == 18324);
    //图片消息不应误填文本字段
    CHECK(notify.m_text.empty());
}

TEST_CASE("MP-IMG4 图片 content build→parse round-trip 一致")
{
    //构造发送请求 → 把其 content 原样塞进一条推送 → parse 回来字段相等
    std::string req = MessageProtocolClient::buildSendImageRequest(
        1, 5002, "cmid-rt", "99999", "abcdefabcdefabcdefabcdefabcdef12", 1920, 1080, 2097152);
    Json::Value reqRoot;
    REQUIRE(parseJsonText(req, reqRoot));

    Json::Value n;
    n["msgid"]     = "31000";
    n["from"]      = "5001";
    n["cid"]       = "5001";
    n["to"]        = "5002";
    n["ctype"]     = 1;
    n["msgtype"]   = 2;
    n["content"]   = reqRoot["content"];   //复用 build 出来的 content 子树
    n["timestamp"] = static_cast<Json::Int64>(0);

    MessageNotify notify;
    REQUIRE(MessageProtocolClient::parseMessageNotify(writeCompact(n), notify));
    CHECK(notify.m_imageFileId == "99999");
    CHECK(notify.m_imageMd5 == "abcdefabcdefabcdefabcdefabcdef12");
    CHECK(notify.m_imageWidth == 1920);
    CHECK(notify.m_imageHeight == 1080);
    CHECK(notify.m_imageSize == 2097152);
}

TEST_CASE("MP-IMG6 parseSyncMessagesResponse 历史里夹图片消息解析正确")
{
    //历史含一条文本 + 一条图片，验证图片字段在历史拉取路径也解析到位
    std::string body =
        "{\"code\":0,\"ctype\":1,\"cid\":\"5001\",\"hasmore\":false,\"messages\":["
        "{\"msgid\":\"20002\",\"from\":\"5001\",\"to\":\"5002\",\"ctype\":1,\"cid\":\"5001\","
        "\"msgtype\":1,\"content\":{\"text\":\"hi\"},\"timestamp\":2},"
        "{\"msgid\":\"20001\",\"from\":\"5002\",\"to\":\"5001\",\"ctype\":1,\"cid\":\"5001\","
        "\"msgtype\":2,\"content\":{\"fileid\":\"10005\",\"md5\":\"m\",\"width\":100,"
        "\"height\":80,\"size\":2048},\"timestamp\":1}"
        "]}";
    SyncMessagesResult out;
    REQUIRE(MessageProtocolClient::parseSyncMessagesResponse(body, out));
    REQUIRE(out.m_messages.size() == 2);
    //messages[0] 文本
    CHECK(out.m_messages[0].m_msgtype == 1);
    CHECK(out.m_messages[0].m_text == "hi");
    //messages[1] 图片
    CHECK(out.m_messages[1].m_msgtype == 2);
    CHECK(out.m_messages[1].m_imageFileId == "10005");
    CHECK(out.m_messages[1].m_imageWidth == 100);
    CHECK(out.m_messages[1].m_imageHeight == 80);
    CHECK(out.m_messages[1].m_imageSize == 2048);
}

TEST_CASE("MP-FILE1 buildSendFileRequest 单聊 content 字段完整")
{
    //单聊文件：ctype=1、cid=对端 userid，content 含 fileid/filename/filesize/md5
    std::string json = MessageProtocolClient::buildSendFileRequest(
        1, 5002, "cmid-file", "20005", u8"banner-final.fig", 4404019,
        "9b1c0a7f2e3d4c5b6a7980e1f2a3b4c5");
    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    CHECK(root["clientmsgid"].asString() == "cmid-file");
    CHECK(root["ctype"].asInt() == 1);
    CHECK(root["cid"].isString());
    CHECK(root["cid"].asString() == "5002");
    //文件 msgtype=3
    CHECK(root["msgtype"].asInt() == 3);
    //content 各字段：fileid 字符串编码、filename 字符串、filesize 数值、md5 字符串
    CHECK(root["content"]["fileid"].isString());
    CHECK(root["content"]["fileid"].asString() == "20005");
    CHECK(root["content"]["filename"].asString() == u8"banner-final.fig");
    CHECK(root["content"]["filesize"].asInt64() == 4404019);
    CHECK(root["content"]["md5"].asString() == "9b1c0a7f2e3d4c5b6a7980e1f2a3b4c5");
}

TEST_CASE("MP-FILE2 buildSendFileRequest 群聊 ctype=2 且 cid 为群id字符串")
{
    std::string json = MessageProtocolClient::buildSendFileRequest(
        2, 700001, "cmid-gfile", "20006", u8"design.zip", 1048576,
        "0123456789abcdef0123456789abcdef");
    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    CHECK(root["ctype"].asInt() == 2);
    CHECK(root["cid"].isString());
    CHECK(root["cid"].asString() == "700001");
    CHECK(root["msgtype"].asInt() == 3);
    CHECK(root["content"]["fileid"].asString() == "20006");
    CHECK(root["content"]["filename"].asString() == u8"design.zip");
}

TEST_CASE("MP-FILE3 中文文件名 round-trip UTF-8 编码正确")
{
    //真实窗口会把 CString 转 UTF-8 再交给本类，故此处用 u8 字面量构造中文文件名
    const std::string cnName = u8"季度报告（终稿）.docx";
    std::string json = MessageProtocolClient::buildSendFileRequest(
        1, 5002, "cmid-cn", "20007", cnName, 65536, "abcdefabcdefabcdefabcdefabcdef12");
    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    //构造侧：filename 应原样还原（验证 JSON 转义与 UTF-8 透传）
    CHECK(root["content"]["filename"].asString() == cnName);

    //接收侧：把同一 content 塞进推送报文再 parse，m_fileName 应原样还原
    Json::Value n;
    n["msgid"]     = "32001";
    n["from"]      = "5001";
    n["cid"]       = "5001";
    n["to"]        = "5002";
    n["ctype"]     = 1;
    n["msgtype"]   = 3;
    n["content"]   = root["content"];   //复用 build 出来的 content 子树
    n["timestamp"] = static_cast<Json::Int64>(0);

    MessageNotify notify;
    REQUIRE(MessageProtocolClient::parseMessageNotify(writeCompact(n), notify));
    CHECK(notify.m_fileName == cnName);
}

TEST_CASE("MP-FILE4 parseMessageNotify 解析文件推送")
{
    //收到文件消息：msgtype=3，content 为文件元信息
    std::string body =
        "{\"msgid\":\"20200\",\"clientmsgid\":\"cmid-file\",\"ctype\":1,"
        "\"cid\":\"5001\",\"from\":\"5001\",\"to\":\"5002\",\"msgtype\":3,"
        "\"content\":{\"fileid\":\"20005\",\"filename\":\"report.pdf\","
        "\"filesize\":1887436,\"md5\":\"9b1c0a7f2e3d4c5b6a7980e1f2a3b4c5\"},"
        "\"timestamp\":1716268800003}";
    MessageNotify notify;
    REQUIRE(MessageProtocolClient::parseMessageNotify(body, notify));
    CHECK(notify.m_msgid == 20200);
    CHECK(notify.m_msgtype == 3);
    CHECK(notify.m_fileFileId == "20005");
    CHECK(notify.m_fileName == "report.pdf");
    CHECK(notify.m_fileSize == 1887436);
    CHECK(notify.m_fileMd5 == "9b1c0a7f2e3d4c5b6a7980e1f2a3b4c5");
    //文件消息不应误填文本 / 图片字段
    CHECK(notify.m_text.empty());
    CHECK(notify.m_imageFileId.empty());
}

TEST_CASE("MP-FILE5 fileid 整型编码兼容 + 历史拉取路径解析文件消息")
{
    //有的服务端实现把 fileid 直接编码成整型，解析须兼容（与图片侧同口径）
    std::string body =
        "{\"msgid\":\"20201\",\"from\":\"5001\",\"to\":\"5002\",\"ctype\":1,\"cid\":\"5001\","
        "\"msgtype\":3,\"content\":{\"fileid\":20009,\"filename\":\"a.bin\","
        "\"filesize\":2048,\"md5\":\"m\"},\"timestamp\":1}";
    MessageNotify notify;
    REQUIRE(MessageProtocolClient::parseMessageNotify(body, notify));
    CHECK(notify.m_fileFileId == "20009");   //整型 fileid 还原为字符串

    //历史拉取路径（parseSyncMessagesResponse）也应解析出文件字段
    std::string hist =
        "{\"code\":0,\"ctype\":1,\"cid\":\"5001\",\"hasmore\":false,\"messages\":["
        "{\"msgid\":\"20002\",\"from\":\"5001\",\"to\":\"5002\",\"ctype\":1,\"cid\":\"5001\","
        "\"msgtype\":3,\"content\":{\"fileid\":\"20005\",\"filename\":\"q4.xlsx\","
        "\"filesize\":51200,\"md5\":\"mm\"},\"timestamp\":2}]}";
    SyncMessagesResult out;
    REQUIRE(MessageProtocolClient::parseSyncMessagesResponse(hist, out));
    REQUIRE(out.m_messages.size() == 1);
    CHECK(out.m_messages[0].m_msgtype == 3);
    CHECK(out.m_messages[0].m_fileFileId == "20005");
    CHECK(out.m_messages[0].m_fileName == "q4.xlsx");
    CHECK(out.m_messages[0].m_fileSize == 51200);
}

TEST_CASE("MP9 digestForMsgType 各类型预览文字")
{
    //文本：取正文；空正文回退到通用占位
    CHECK(MessageProtocolClient::digestForMsgType(1, "hello") == "hello");
    CHECK(MessageProtocolClient::digestForMsgType(1, "") == u8"[消息]");
    //系统消息同文本：取正文
    CHECK(MessageProtocolClient::digestForMsgType(17, u8"欢迎加入") == u8"欢迎加入");
    //表情(6) / 抖动(13) 占位文案
    CHECK(MessageProtocolClient::digestForMsgType(6, "") == u8"[表情]");
    CHECK(MessageProtocolClient::digestForMsgType(13, "") == u8"[抖一抖]");
    //图片/文件/语音占位
    CHECK(MessageProtocolClient::digestForMsgType(2, "") == u8"[图片]");
    CHECK(MessageProtocolClient::digestForMsgType(3, "") == u8"[文件]");
    CHECK(MessageProtocolClient::digestForMsgType(4, "") == u8"[语音]");
    //未知类型回退到通用占位
    CHECK(MessageProtocolClient::digestForMsgType(999, "") == u8"[消息]");
}

TEST_CASE("MP9b digestForMsgType 文本内联表情净化")
{
    //含 /f["N"] 标签：标签→[表情]、转义的 // 还原成 /
    CHECK(MessageProtocolClient::digestForMsgType(1, u8"hi/f[\"3\"]a//b")
          == u8"hi[表情]a/b");
    //只有表情
    CHECK(MessageProtocolClient::digestForMsgType(1, "/f[\"0\"]") == u8"[表情]");
    //无标签的普通文本：原样保留（他端发来的 // 不能被误还原）
    CHECK(MessageProtocolClient::digestForMsgType(1, "http://a.com") == "http://a.com");
    //残缺标签（缺右半）不触发净化
    CHECK(MessageProtocolClient::digestForMsgType(1, "/f[\"oops") == "/f[\"oops");
    //UTF-8 中文与表情混排
    CHECK(MessageProtocolClient::digestForMsgType(1, u8"你好/f[\"12\"]世界")
          == u8"你好[表情]世界");
}

TEST_CASE("MP9c tryNormalizeInlineFaceText 返回值语义与出参写入时机")
{
    //会话列表拿服务端 digest 时正是按返回值决定要不要替换，故这里直接测这层语义：
    //只有确实含表情标签才返回 true 并写 out，否则 out 必须原封不动。
    std::string out;

    //含标签：返回 true，out 写入净化结果（标签→[表情]，转义 // 还原成 /）
    out = "SENTINEL";
    CHECK(MessageProtocolClient::tryNormalizeInlineFaceText(u8"hi/f[\"3\"]a//b", out));
    CHECK(out == u8"hi[表情]a/b");

    //多个标签都要替换
    out = "SENTINEL";
    CHECK(MessageProtocolClient::tryNormalizeInlineFaceText(u8"/f[\"1\"]x/f[\"22\"]", out));
    CHECK(out == u8"[表情]x[表情]");

    //无标签的普通文本：返回 false 且不碰 out —— 调用方据此保留原 digest。
    //这里的 http:// 是关键：若无条件还原 // 会把链接改成 http:/a.com。
    out = "SENTINEL";
    CHECK(MessageProtocolClient::tryNormalizeInlineFaceText("http://a.com", out) == false);
    CHECK(out == "SENTINEL");

    //纯转义斜杠但没有表情：同样不动（无从判断是转义还是字面）
    out = "SENTINEL";
    CHECK(MessageProtocolClient::tryNormalizeInlineFaceText("a//b", out) == false);
    CHECK(out == "SENTINEL");

    //残缺标签不算表情
    out = "SENTINEL";
    CHECK(MessageProtocolClient::tryNormalizeInlineFaceText("/f[\"oops", out) == false);
    CHECK(out == "SENTINEL");

    //空串
    out = "SENTINEL";
    CHECK(MessageProtocolClient::tryNormalizeInlineFaceText("", out) == false);
    CHECK(out == "SENTINEL");

    //中文与表情混排（UTF-8 多字节不受按字节扫描影响）
    out.clear();
    CHECK(MessageProtocolClient::tryNormalizeInlineFaceText(u8"你好/f[\"12\"]世界", out));
    CHECK(out == u8"你好[表情]世界");
}

TEST_CASE("MP-FACK1 buildFileTransferAckRequest 字段完整且大整数字符串编码")
{
    std::string json = MessageProtocolClient::buildFileTransferAckRequest(
        100001, 9007199254740993LL, file_ack_received);
    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    CHECK(root["ctype"].asInt() == 1);
    //cid 取对端（文件发送方）userid
    CHECK(root["cid"].asString() == "100001");
    //超出 double 精度的 msgid 也不能丢位
    CHECK(root["msgid"].asString() == "9007199254740993");
    CHECK(root["state"].asInt() == file_ack_received);
}

TEST_CASE("MP-FACK2 buildFileTransferAckRequest 对非法参数返回空串")
{
    //peer / msgid 非正：服务端必判非法，不如不发
    CHECK(MessageProtocolClient::buildFileTransferAckRequest(0, 501, file_ack_received).empty());
    CHECK(MessageProtocolClient::buildFileTransferAckRequest(100001, 0, file_ack_received).empty());
    CHECK(MessageProtocolClient::buildFileTransferAckRequest(-1, 501, file_ack_received).empty());
    //state 越界同理
    CHECK(MessageProtocolClient::buildFileTransferAckRequest(100001, 501, 0).empty());
    CHECK(MessageProtocolClient::buildFileTransferAckRequest(100001, 501, 4).empty());
    //三个合法状态都要能构造出来
    CHECK_FALSE(MessageProtocolClient::buildFileTransferAckRequest(100001, 501, file_ack_receiving).empty());
    CHECK_FALSE(MessageProtocolClient::buildFileTransferAckRequest(100001, 501, file_ack_received).empty());
    CHECK_FALSE(MessageProtocolClient::buildFileTransferAckRequest(100001, 501, file_ack_rejected).empty());
}

TEST_CASE("MP-FACK3 parseFileTransferAckNotify 解析推送")
{
    net::FileTransferAckNotify out;
    REQUIRE(MessageProtocolClient::parseFileTransferAckNotify(
        "{\"ctype\":1,\"cid\":\"100001\",\"msgid\":\"501\",\"state\":3}", out));
    CHECK(out.m_ctype == 1);
    CHECK(out.m_cid == 100001);
    CHECK(out.m_msgid == 501);
    CHECK(out.m_state == file_ack_rejected);

    //msgid 非正 / 缺失：无从定位气泡，判为非法
    CHECK_FALSE(MessageProtocolClient::parseFileTransferAckNotify(
        "{\"cid\":\"100001\",\"state\":1}", out));
    CHECK_FALSE(MessageProtocolClient::parseFileTransferAckNotify(
        "{\"cid\":\"100001\",\"msgid\":\"0\",\"state\":1}", out));
    //state 越界：直接决定气泡显示什么文字，放进来会显示成莫名其妙的状态
    CHECK_FALSE(MessageProtocolClient::parseFileTransferAckNotify(
        "{\"cid\":\"100001\",\"msgid\":\"501\",\"state\":9}", out));
    CHECK_FALSE(MessageProtocolClient::parseFileTransferAckNotify(
        "{\"cid\":\"100001\",\"msgid\":\"501\"}", out));
    //畸形 JSON 安全失败
    CHECK_FALSE(MessageProtocolClient::parseFileTransferAckNotify("not json", out));
}

TEST_CASE("MP-FACK4 历史消息带出 fileackstate")
{
    //服务端只对"拉取者自己发出的文件消息"回填该字段，客户端据它在气泡上显示对方收没收
    std::string body =
        "{\"code\":0,\"ctype\":1,\"cid\":\"100001\",\"hasmore\":false,\"messages\":["
        "{\"msgid\":\"601\",\"ctype\":1,\"cid\":\"100001\",\"from\":\"100000\",\"to\":\"100001\","
        "\"msgtype\":3,\"content\":{\"fileid\":\"77\",\"name\":\"a.zip\",\"size\":\"1024\",\"md5\":\"abc\"},"
        "\"timestamp\":1700000000000,\"fileackstate\":2}]}";
    net::SyncMessagesResult out;
    REQUIRE(MessageProtocolClient::parseSyncMessagesResponse(body, out));
    REQUIRE(out.m_messages.size() == 1);
    CHECK(out.m_messages[0].m_fileAckState == file_ack_received);

    //没有该字段（对方还没动作 / 非本人发出 / 老服务端）时保持 0，按"无状态"渲染
    std::string body2 =
        "{\"code\":0,\"ctype\":1,\"cid\":\"100001\",\"hasmore\":false,\"messages\":["
        "{\"msgid\":\"602\",\"ctype\":1,\"cid\":\"100001\",\"from\":\"100000\",\"to\":\"100001\","
        "\"msgtype\":3,\"content\":{\"fileid\":\"78\",\"name\":\"b.zip\",\"size\":\"2048\",\"md5\":\"def\"},"
        "\"timestamp\":1700000000001}]}";
    net::SyncMessagesResult out2;
    REQUIRE(MessageProtocolClient::parseSyncMessagesResponse(body2, out2));
    REQUIRE(out2.m_messages.size() == 1);
    CHECK(out2.m_messages[0].m_fileAckState == 0);
}

TEST_CASE("MP10 buildMarkReadRequest 单聊已读上报字段完整且类型正确")
{
    //单聊：ctype=1，cid=对端 userid=5001，已读至 msgid=20002
    std::string json = MessageProtocolClient::buildMarkReadRequest(1, 5001, 20002);

    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    //会话类型为整型，单聊=1
    CHECK(root["ctype"].isInt());
    CHECK(root["ctype"].asInt() == 1);
    //cid 必须为字符串编码（与服务端 parseMarkReadRequest 的 getStringId 契约一致）
    CHECK(root["cid"].isString());
    CHECK(root["cid"].asString() == "5001");
    //readmsgid 同样字符串编码，承载已读到的最大消息 msgid
    CHECK(root["readmsgid"].isString());
    CHECK(root["readmsgid"].asString() == "20002");
}

TEST_CASE("MP10b buildMarkReadRequest 群聊 ctype=2")
{
    //群聊：ctype=2，cid=群 id
    std::string json = MessageProtocolClient::buildMarkReadRequest(2, 100001, 30005);

    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    CHECK(root["ctype"].asInt() == 2);
    CHECK(root["cid"].asString() == "100001");
    CHECK(root["readmsgid"].asString() == "30005");
}

TEST_CASE("MP10c buildMarkReadRequest 大整数 ID 不丢精度")
{
    //cid 与 readmsgid 取超过 2^53 的值，验证字符串编码路径精确还原
    const int64_t bigCid   = 9223372036854775807LL; // int64 max
    const int64_t bigMsgId = 9007199254740993LL;     // 2^53 + 1
    std::string json = MessageProtocolClient::buildMarkReadRequest(1, bigCid, bigMsgId);

    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    //字符串原样保留，下游用 strtoll 还原即不丢精度
    CHECK(root["cid"].asString() == "9223372036854775807");
    CHECK(root["readmsgid"].asString() == "9007199254740993");
}

// ---------------------------------------------------------------------------
// MP-PIN 系列  消息 Pin 的客户端侧报文编解码（cmd_pin_message 1518 /
//              cmd_message_pinned_notify 1519 / cmd_get_pinned_messages 1520）。
//              方案见 docs/消息Pin接入方案.md §6.1。
// ---------------------------------------------------------------------------

TEST_CASE("MP-PIN1 buildPinMessageRequest 字段齐全且 64 位 ID 为字符串")
{
    std::string json = MessageProtocolClient::buildPinMessageRequest(2, 268435456, 91002, true);
    Json::Value root;
    REQUIRE(parseJsonText(json, root));
    CHECK(root["ctype"].asInt() == 2);
    CHECK(root["cid"].asString() == "268435456");
    CHECK(root["msgid"].asString() == "91002");
    CHECK(root["pin"].asInt() == 1);

    //取消方向
    Json::Value off;
    REQUIRE(parseJsonText(
        MessageProtocolClient::buildPinMessageRequest(1, 5081, 7, false), off));
    CHECK(off["pin"].asInt() == 0);

    //大整数不丢精度
    Json::Value big;
    REQUIRE(parseJsonText(
        MessageProtocolClient::buildPinMessageRequest(1, 9223372036854775807LL,
                                                      9007199254740993LL, true), big));
    CHECK(big["cid"].asString() == "9223372036854775807");
    CHECK(big["msgid"].asString() == "9007199254740993");

    //cid / msgid 非正时返回空串，调用方据此放弃发送，省一次必然失败的往返
    CHECK(MessageProtocolClient::buildPinMessageRequest(1, 0, 7, true).empty());
    CHECK(MessageProtocolClient::buildPinMessageRequest(1, 5081, 0, true).empty());
    CHECK(MessageProtocolClient::buildPinMessageRequest(1, -1, -1, true).empty());
}

TEST_CASE("MP-PIN2 parsePinResponse 取出五个字段")
{
    PinMessageResponse resp;
    REQUIRE(MessageProtocolClient::parsePinResponse(
        "{\"code\":0,\"msg\":\"ok\",\"ctype\":2,\"cid\":\"268435456\","
        "\"msgid\":\"91002\",\"pin\":1}", resp));
    CHECK(resp.m_code == 0);
    CHECK(resp.m_msg == "ok");
    CHECK(resp.m_ctype == 2);
    CHECK(resp.m_cid == 268435456);
    CHECK(resp.m_msgid == 91002);
    CHECK(resp.m_pin == 1);

    //失败应答同样回带定位信息，客户端才能指出是哪一条钉失败了
    PinMessageResponse denied;
    REQUIRE(MessageProtocolClient::parsePinResponse(
        "{\"code\":506,\"msg\":\"no permission to unpin\",\"ctype\":2,"
        "\"cid\":\"268435456\",\"msgid\":\"91002\",\"pin\":0}", denied));
    CHECK(denied.m_code == error_code_pin_not_allowed);
    CHECK(denied.m_msgid == 91002);
    CHECK(denied.m_pin == 0);

    //没有整型 code 的报文不可用；默认的 m_code 为 -1，不会被误判成功
    PinMessageResponse bad;
    CHECK_FALSE(MessageProtocolClient::parsePinResponse("", bad));
    CHECK_FALSE(MessageProtocolClient::parsePinResponse("{oops", bad));
    CHECK_FALSE(MessageProtocolClient::parsePinResponse("{\"msg\":\"ok\"}", bad));
    CHECK(bad.m_code == -1);
}

TEST_CASE("MP-PIN3 parseMessagePinnedNotify 取出六个字段，缺 ctype 按单聊、缺 operator 取 0")
{
    MessagePinnedNotify notify;
    REQUIRE(MessageProtocolClient::parseMessagePinnedNotify(
        "{\"ctype\":2,\"cid\":\"268435456\",\"msgid\":\"91002\",\"pin\":1,"
        "\"operator\":\"3001\",\"pintime\":\"1754300000000\"}", notify));
    CHECK(notify.m_ctype == 2);
    CHECK(notify.m_cid == 268435456);
    CHECK(notify.m_msgid == 91002);
    CHECK(notify.m_pin == 1);
    CHECK(notify.m_operator == 3001);
    CHECK(notify.m_pinTime == 1754300000000LL);

    //缺 ctype 按单聊、缺 operator / pintime 取 0，都不影响本条推送可用
    MessagePinnedNotify lean;
    REQUIRE(MessageProtocolClient::parseMessagePinnedNotify(
        "{\"cid\":\"5081\",\"msgid\":\"7\",\"pin\":0}", lean));
    CHECK(lean.m_ctype == conversation_single);
    CHECK(lean.m_pin == 0);
    CHECK(lean.m_operator == 0);
    CHECK(lean.m_pinTime == 0);
}

TEST_CASE("MP-PIN4 parseMessagePinnedNotify 对畸形报文与越界 pin 安全失败")
{
    MessagePinnedNotify notify;
    CHECK_FALSE(MessageProtocolClient::parseMessagePinnedNotify("", notify));
    CHECK_FALSE(MessageProtocolClient::parseMessagePinnedNotify("{oops", notify));
    CHECK_FALSE(MessageProtocolClient::parseMessagePinnedNotify("[]", notify));
    //cid / msgid 缺失或非正
    CHECK_FALSE(MessageProtocolClient::parseMessagePinnedNotify(
        "{\"msgid\":\"7\",\"pin\":1}", notify));
    CHECK_FALSE(MessageProtocolClient::parseMessagePinnedNotify(
        "{\"cid\":\"5081\",\"pin\":1}", notify));
    CHECK_FALSE(MessageProtocolClient::parseMessagePinnedNotify(
        "{\"cid\":\"0\",\"msgid\":\"7\",\"pin\":1}", notify));
    CHECK_FALSE(MessageProtocolClient::parseMessagePinnedNotify(
        "{\"cid\":\"5081\",\"msgid\":\"-1\",\"pin\":1}", notify));
    //pin 缺失 / 越界 / 非整数：一律丢弃整条。缺省成"取消"会把别人刚钉上的消息
    //在本端悄悄抹掉，比丢弃这条推送糟得多
    CHECK_FALSE(MessageProtocolClient::parseMessagePinnedNotify(
        "{\"cid\":\"5081\",\"msgid\":\"7\"}", notify));
    CHECK_FALSE(MessageProtocolClient::parseMessagePinnedNotify(
        "{\"cid\":\"5081\",\"msgid\":\"7\",\"pin\":2}", notify));
    CHECK_FALSE(MessageProtocolClient::parseMessagePinnedNotify(
        "{\"cid\":\"5081\",\"msgid\":\"7\",\"pin\":\"1\"}", notify));
}

TEST_CASE("MP-PIN5 buildGetPinnedMessagesRequest 与空应答")
{
    Json::Value root;
    REQUIRE(parseJsonText(
        MessageProtocolClient::buildGetPinnedMessagesRequest(2, 268435456), root));
    CHECK(root["ctype"].asInt() == 2);
    CHECK(root["cid"].asString() == "268435456");
    CHECK(MessageProtocolClient::buildGetPinnedMessagesRequest(2, 0).empty());

    //不含 msgs 字段的应答（该会话一条都没钉）：空列表、不算失败
    PinnedMessagesResult empty;
    REQUIRE(MessageProtocolClient::parseGetPinnedMessagesResponse(
        "{\"code\":0,\"msg\":\"ok\",\"ctype\":2,\"cid\":\"268435456\",\"total\":0}", empty));
    CHECK(empty.m_code == 0);
    CHECK(empty.m_cid == 268435456);
    CHECK(empty.m_total == 0);
    CHECK(empty.m_items.empty());

    //没有整型 code 的报文不可用
    PinnedMessagesResult bad;
    CHECK_FALSE(MessageProtocolClient::parseGetPinnedMessagesResponse("", bad));
    CHECK_FALSE(MessageProtocolClient::parseGetPinnedMessagesResponse("{\"msg\":\"ok\"}", bad));
}

TEST_CASE("MP-PIN6 parseGetPinnedMessagesResponse 解析消息体与 pin 子对象，截断时 total 大于条数")
{
    const std::string body =
        "{\"code\":0,\"msg\":\"ok\",\"ctype\":2,\"cid\":\"268435456\",\"total\":37,\"msgs\":["
        "{\"msgid\":\"91002\",\"ctype\":2,\"cid\":\"268435456\",\"from\":\"3001\","
        "\"to\":\"268435456\",\"msgtype\":1,\"content\":{\"text\":\"周会链接\"},"
        "\"timestamp\":1754200000000,\"pin\":{\"operator\":\"3001\",\"pintime\":\"1754300000000\"}},"
        "{\"msgid\":\"91001\",\"ctype\":2,\"cid\":\"268435456\",\"from\":\"3002\","
        "\"to\":\"268435456\",\"msgtype\":1,\"content\":{\"text\":\"定稿\"},"
        "\"timestamp\":1754100000000,\"pin\":{\"operator\":\"3002\",\"pintime\":\"1754310000000\"}}"
        "]}";

    PinnedMessagesResult out;
    REQUIRE(MessageProtocolClient::parseGetPinnedMessagesResponse(body, out));
    CHECK(out.m_code == 0);
    CHECK(out.m_ctype == 2);
    CHECK(out.m_cid == 268435456);
    REQUIRE(out.m_items.size() == 2);

    //消息体与 sync_messages 的元素同结构
    CHECK(out.m_items[0].m_msg.m_msgid == 91002);
    CHECK(out.m_items[0].m_msg.m_text == "周会链接");
    CHECK(out.m_items[1].m_msg.m_msgid == 91001);

    //每条挂着的 pin 子对象
    CHECK(out.m_items[0].m_operator == 3001);
    CHECK(out.m_items[0].m_pinTime == 1754300000000LL);
    CHECK(out.m_items[1].m_operator == 3002);

    //本次被截断：total 是会话里真实的 Pin 总数，大于返回条数。客户端据此把本地对账
    //的时间下界收窄到"返回集合里最老一条的时间"，否则那些更老的 Pin 会被误清
    CHECK(out.m_total == 37);
    CHECK(out.m_total > (int64_t)out.m_items.size());
}

TEST_CASE("MP-PIN7 parseGetPinnedMessagesResponse 跳过脏条目，缺 total 时按返回条数兜底")
{
    //第二条缺 msgid，属于脏数据：跳过它，其余照常解析
    const std::string dirty =
        "{\"code\":0,\"msg\":\"ok\",\"ctype\":1,\"cid\":\"5081\",\"msgs\":["
        "{\"msgid\":\"7\",\"ctype\":1,\"cid\":\"5081\",\"from\":\"3001\",\"to\":\"5081\","
        "\"msgtype\":1,\"content\":{\"text\":\"hi\"},\"timestamp\":100,"
        "\"pin\":{\"operator\":\"3001\",\"pintime\":\"200\"}},"
        "{\"ctype\":1,\"from\":\"3001\",\"msgtype\":1,\"content\":{\"text\":\"broken\"}},"
        "\"not-an-object\""
        "]}";

    PinnedMessagesResult out;
    REQUIRE(MessageProtocolClient::parseGetPinnedMessagesResponse(dirty, out));
    REQUIRE(out.m_items.size() == 1);
    CHECK(out.m_items[0].m_msg.m_msgid == 7);

    //应答里没有 total（老服务端）：按"返回多少就是多少"兜底，使调用方走全量对账
    //那条路。若照原样留 0，会被当成截断、下界算不出来
    CHECK(out.m_total == 1);

    //某条缺 pin 子对象时该条的元信息取 0，不影响其余
    const std::string noPin =
        "{\"code\":0,\"ctype\":1,\"cid\":\"5081\",\"total\":1,\"msgs\":["
        "{\"msgid\":\"9\",\"ctype\":1,\"cid\":\"5081\",\"from\":\"3001\",\"to\":\"5081\","
        "\"msgtype\":1,\"content\":{\"text\":\"x\"},\"timestamp\":100}"
        "]}";
    PinnedMessagesResult lean;
    REQUIRE(MessageProtocolClient::parseGetPinnedMessagesResponse(noPin, lean));
    REQUIRE(lean.m_items.size() == 1);
    CHECK(lean.m_items[0].m_operator == 0);
    CHECK(lean.m_items[0].m_pinTime == 0);
}

TEST_CASE("MP-PIN8 非 0 结果码的应答不解析消息列表")
{
    PinnedMessagesResult out;
    REQUIRE(MessageProtocolClient::parseGetPinnedMessagesResponse(
        "{\"code\":302,\"msg\":\"not a group member\"}", out));
    CHECK(out.m_code == error_code_not_group_member);
    CHECK(out.m_items.empty());
    CHECK(out.m_total == 0);
}
