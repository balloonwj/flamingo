/**
 *  v2 会话与消息报文编解码单元测试（Suite S），test_messageprotocol.cpp
 *  覆盖：send_message 请求解析（基本/带@与引用/校验失败），发送回执构造，
 *       message_notify 完整消息体构造，received_ack 解析，
 *       f_content 落库内容的打包/解包往返；sync_messages 请求解析与应答构造，
 *       消息摘要 digest 生成，sync_conversations 应答构造；
 *       mark_read / recall_message 请求解析，已读/撤回通知构造；
 *       typing/set_conversation/search/forward/delete/mass_send 报文编解码；
 *       消息 Pin 的请求解析、应答与推送构造，以及被 Pin 消息列表应答里
 *       total 与 pin 子对象的正确性（S23-S28，见 docs/消息Pin接入方案.md）。
 *  balloonwj@qq.com   2026-05-21
 */
#include "doctest.h"
#include "../chatserversrc/MessageProtocol.h"
#include "../chatserversrc/Protocol.h"   //enum file_ack_state / message_type_file
#include "../jsoncpp1.9.0/json.h"

//把 JSON 文本解析成 Json::Value，供断言检查
static bool parseJson(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

TEST_CASE("S1 parseSendMessageRequest 文本消息基本字段")
{
    SendMessageRequest req;
    REQUIRE(MessageProtocol::parseSendMessageRequest(
        "{\"clientmsgid\":\"uuid-1\",\"ctype\":1,\"cid\":\"100001\","
        "\"msgtype\":1,\"content\":{\"text\":\"你好\"}}", req));
    CHECK(req.clientmsgid == "uuid-1");
    CHECK(req.ctype == 1);
    CHECK(req.cid == 100001);
    CHECK(req.msgtype == 1);
    //content 原样保留为紧凑JSON文本
    Json::Value content;
    REQUIRE(parseJson(req.content, content));
    CHECK(content["text"].asString() == "你好");
    //缺省项
    CHECK(req.mentions.empty());
    CHECK(req.mentionall == false);
    CHECK(req.quote == "");
    //ctype 缺省时默认为 1（单聊）
    REQUIRE(MessageProtocol::parseSendMessageRequest(
        "{\"clientmsgid\":\"u\",\"cid\":\"100001\",\"msgtype\":1,\"content\":{}}", req));
    CHECK(req.ctype == 1);
}

TEST_CASE("S2 parseSendMessageRequest 带 mentions/mentionall/quote")
{
    SendMessageRequest req;
    REQUIRE(MessageProtocol::parseSendMessageRequest(
        "{\"clientmsgid\":\"uuid-2\",\"ctype\":1,\"cid\":\"600001\",\"msgtype\":1,"
        "\"content\":{\"text\":\"收到\"},"
        "\"mentions\":[\"100007\",\"100008\"],\"mentionall\":true,"
        "\"quote\":{\"msgid\":\"9007199254740993\",\"from\":\"100007\",\"digest\":\"确认下\"}}", req));
    REQUIRE(req.mentions.size() == 2);
    CHECK(req.mentions[0] == 100007);
    CHECK(req.mentions[1] == 100008);
    CHECK(req.mentionall == true);
    //quote 原样保留为紧凑JSON文本
    Json::Value quote;
    REQUIRE(parseJson(req.quote, quote));
    CHECK(quote["msgid"].asString() == "9007199254740993");
    CHECK(quote["from"].asString() == "100007");
}

TEST_CASE("S3 parseSendMessageRequest 校验失败")
{
    SendMessageRequest req;
    //clientmsgid 缺失或为空被拒
    CHECK_FALSE(MessageProtocol::parseSendMessageRequest(
        "{\"cid\":\"100001\",\"msgtype\":1,\"content\":{}}", req));
    CHECK_FALSE(MessageProtocol::parseSendMessageRequest(
        "{\"clientmsgid\":\"\",\"cid\":\"100001\",\"msgtype\":1,\"content\":{}}", req));
    //cid 缺失或非正被拒
    CHECK_FALSE(MessageProtocol::parseSendMessageRequest(
        "{\"clientmsgid\":\"u\",\"msgtype\":1,\"content\":{}}", req));
    CHECK_FALSE(MessageProtocol::parseSendMessageRequest(
        "{\"clientmsgid\":\"u\",\"cid\":\"0\",\"msgtype\":1,\"content\":{}}", req));
    //msgtype 缺失被拒
    CHECK_FALSE(MessageProtocol::parseSendMessageRequest(
        "{\"clientmsgid\":\"u\",\"cid\":\"100001\",\"content\":{}}", req));
    //content 缺失或非对象被拒
    CHECK_FALSE(MessageProtocol::parseSendMessageRequest(
        "{\"clientmsgid\":\"u\",\"cid\":\"100001\",\"msgtype\":1}", req));
    CHECK_FALSE(MessageProtocol::parseSendMessageRequest(
        "{\"clientmsgid\":\"u\",\"cid\":\"100001\",\"msgtype\":1,\"content\":\"x\"}", req));
}

TEST_CASE("S4 buildSendMessageResponse 回执")
{
    Json::Value root;
    REQUIRE(parseJson(
        MessageProtocol::buildSendMessageResponse(0, "ok", "uuid-1",
                                                  9007199254741001LL, 1716268800000LL),
        root));
    CHECK(root["code"].asInt() == 0);
    CHECK(root["clientmsgid"].asString() == "uuid-1");
    //msgid 以字符串形式落入 JSON
    CHECK(root["msgid"].asString() == "9007199254741001");
    CHECK(root["timestamp"].asInt64() == 1716268800000LL);
}

TEST_CASE("S5 buildMessageNotify 完整消息体")
{
    MessageRecord rec;
    rec.msgid       = 9007199254741001LL;
    rec.clientmsgid = "uuid-1";
    rec.ctype       = 1;
    rec.from        = 100000;
    rec.to          = 100001;
    rec.msgtype     = 1;
    rec.content     = "{\"text\":\"你好\"}";
    rec.mentions.push_back(100007);
    rec.mentionall  = false;
    rec.quote       = "";
    rec.timestamp   = 1716268800000LL;
    rec.recalled    = false;

    Json::Value root;
    //推给收件人 100001 时，会话ID cid = 发送方 100000
    REQUIRE(parseJson(MessageProtocol::buildMessageNotify(rec, 100000), root));
    CHECK(root["msgid"].asString() == "9007199254741001");
    CHECK(root["cid"].asString() == "100000");
    CHECK(root["from"].asString() == "100000");
    CHECK(root["to"].asString() == "100001");
    CHECK(root["msgtype"].asInt() == 1);
    CHECK(root["content"]["text"].asString() == "你好");
    REQUIRE(root["mentions"].size() == 1);
    CHECK(root["mentions"][0].asString() == "100007");
    CHECK(root["recalled"].asBool() == false);
    //无引用时不输出 quote 字段
    CHECK_FALSE(root.isMember("quote"));
}

TEST_CASE("S6 parseReceivedAck")
{
    int64_t msgid = 0;
    REQUIRE(MessageProtocol::parseReceivedAck("{\"msgid\":\"9007199254741001\"}", msgid));
    CHECK(msgid == 9007199254741001LL);
    //msgid 缺失或非正被拒
    CHECK_FALSE(MessageProtocol::parseReceivedAck("{}", msgid));
    CHECK_FALSE(MessageProtocol::parseReceivedAck("{\"msgid\":\"0\"}", msgid));
}

TEST_CASE("S7 buildStoredContent / parseStoredContent 往返一致")
{
    std::vector<int64_t> mentions;
    mentions.push_back(100007);
    mentions.push_back(100008);
    std::string blob = MessageProtocol::buildStoredContent(
        "{\"text\":\"今晚之前给反馈\"}", mentions, true,
        "{\"msgid\":\"900\",\"from\":\"100007\",\"digest\":\"确认下\"}");

    std::string content, quote;
    std::vector<int64_t> outMentions;
    bool mentionall = false;
    REQUIRE(MessageProtocol::parseStoredContent(blob, content, outMentions, mentionall, quote));

    Json::Value c;
    REQUIRE(parseJson(content, c));
    CHECK(c["text"].asString() == "今晚之前给反馈");
    REQUIRE(outMentions.size() == 2);
    CHECK(outMentions[0] == 100007);
    CHECK(outMentions[1] == 100008);
    CHECK(mentionall == true);
    Json::Value q;
    REQUIRE(parseJson(quote, q));
    CHECK(q["from"].asString() == "100007");

    //无引用的往返：quote 解包后应为空串、mentions 为空、mentionall 为 false
    std::vector<int64_t> noMentions;
    std::string blob2 = MessageProtocol::buildStoredContent(
        "{\"text\":\"hi\"}", noMentions, false, "");
    std::string content2, quote2;
    std::vector<int64_t> m2;
    bool ma2 = true;
    REQUIRE(MessageProtocol::parseStoredContent(blob2, content2, m2, ma2, quote2));
    CHECK(quote2 == "");
    CHECK(m2.empty());
    CHECK(ma2 == false);
}

TEST_CASE("S7b 服务器自造的系统提示消息落库口径")
{
    //群公告提示、入群欢迎语这类系统消息由服务器自己拼正文（buildTextContent），
    //但落库前<u>同样要过一遍 buildStoredContent</u>，与客户端发来的消息共用一种
    //存储格式。这个用例把这条约定钉死：曾经这两处漏了外壳，直接把裸 content
    //存进 f_content，导致重新登录同步会话列表时摘要退化成"[消息]"。
    const std::string text    = u8"「张三」发布了群公告：明天九点开会";
    const std::string content = MessageProtocol::buildTextContent(text);

    std::vector<int64_t> noMentions;
    const std::string blob =
        MessageProtocol::buildStoredContent(content, noMentions, false, "");

    //读回：正文完好，摘要就是这句提示语本身，而不是通用占位
    std::string outContent, outQuote;
    std::vector<int64_t> outMentions;
    bool outMentionAll = true;
    REQUIRE(MessageProtocol::parseStoredContent(blob, outContent, outMentions,
                                                outMentionAll, outQuote));
    CHECK(MessageProtocol::extractText(outContent) == text);
    CHECK(MessageProtocol::buildMessageDigest(17, outContent) == text);
    //系统提示不带 @ 与引用
    CHECK(outMentions.empty());
    CHECK(outMentionAll == false);
    CHECK(outQuote == "");

    //反面用例：漏掉外壳直接落库时会发生什么 —— parseStoredContent 找不到最外层的
    //content 字段，解出空串，摘要随之退化成"[消息]"。留着它是为了让后来者一眼看出
    //少那层外壳的代价，不是在容忍这种存法。
    std::string bareContent, bareQuote;
    std::vector<int64_t> bareMentions;
    bool bareMentionAll = false;
    REQUIRE(MessageProtocol::parseStoredContent(content, bareContent, bareMentions,
                                                bareMentionAll, bareQuote));
    CHECK(bareContent == "");
    CHECK(MessageProtocol::buildMessageDigest(17, bareContent) == u8"[消息]");
}

TEST_CASE("S8 parseSyncMessagesRequest 翻历史/补齐两模式")
{
    SyncMessagesRequest req;
    //翻历史模式：beforemsgid="0" 表示从最新开始
    REQUIRE(MessageProtocol::parseSyncMessagesRequest(
        "{\"ctype\":1,\"cid\":\"100001\",\"beforemsgid\":\"0\",\"limit\":20}", req));
    CHECK(req.ctype == 1);
    CHECK(req.cid == 100001);
    CHECK(req.isAfterMode == false);
    CHECK(req.beforemsgid == 0);
    CHECK(req.limit == 20);
    //翻历史模式：beforemsgid 为具体锚点
    REQUIRE(MessageProtocol::parseSyncMessagesRequest(
        "{\"ctype\":1,\"cid\":\"100001\",\"beforemsgid\":\"500\"}", req));
    CHECK(req.isAfterMode == false);
    CHECK(req.beforemsgid == 500);
    //补齐模式：出现 aftermsgid 即按补齐处理
    REQUIRE(MessageProtocol::parseSyncMessagesRequest(
        "{\"ctype\":1,\"cid\":\"100001\",\"aftermsgid\":\"900\"}", req));
    CHECK(req.isAfterMode == true);
    CHECK(req.aftermsgid == 900);
    //cid 缺失或非正被拒
    CHECK_FALSE(MessageProtocol::parseSyncMessagesRequest("{\"ctype\":1}", req));
    CHECK_FALSE(MessageProtocol::parseSyncMessagesRequest("{\"cid\":\"0\"}", req));
}

TEST_CASE("S9 buildSyncMessagesResponse")
{
    MessageRecord m1;
    m1.msgid       = 501;
    m1.clientmsgid = "u-1";
    m1.ctype       = 1;
    m1.from        = 100001;
    m1.to          = 100000;
    m1.msgtype     = 1;
    m1.content     = "{\"text\":\"hello\"}";
    m1.timestamp   = 1716268800000LL;
    std::vector<MessageRecord> msgs;
    msgs.push_back(m1);

    Json::Value root;
    REQUIRE(parseJson(
        MessageProtocol::buildSyncMessagesResponse(0, "ok", 1, 100001, true, msgs), root));
    CHECK(root["code"].asInt() == 0);
    CHECK(root["ctype"].asInt() == 1);
    CHECK(root["cid"].asString() == "100001");
    CHECK(root["hasmore"].asBool() == true);
    REQUIRE(root["messages"].size() == 1);
    //messages 元素与 message_notify 包体同构，cid 取本次会话ID
    CHECK(root["messages"][0]["msgid"].asString() == "501");
    CHECK(root["messages"][0]["cid"].asString() == "100001");
    CHECK(root["messages"][0]["content"]["text"].asString() == "hello");
}

TEST_CASE("S10 buildMessageDigest 各消息类型")
{
    //文本/系统消息取其正文
    CHECK(MessageProtocol::buildMessageDigest(1, "{\"text\":\"hello world\"}") == "hello world");
    CHECK(MessageProtocol::buildMessageDigest(17, "{\"text\":\"system notice\"}") == "system notice");
    //非文本类型返回占位文案（UTF-8）
    CHECK(MessageProtocol::buildMessageDigest(2, "{}") == u8"[图片]");
    CHECK(MessageProtocol::buildMessageDigest(3, "{}") == u8"[文件]");
    CHECK(MessageProtocol::buildMessageDigest(4, "{}") == u8"[语音]");
    //未知类型回落到通用占位
    CHECK(MessageProtocol::buildMessageDigest(99, "{}") == u8"[消息]");
    //文本消息但 content 无 text 字段，同样回落到通用占位
    CHECK(MessageProtocol::buildMessageDigest(1, "{}") == u8"[消息]");
}

TEST_CASE("S11 buildSyncConversationsResponse")
{
    ConversationBrief c1;
    c1.ctype         = 1;
    c1.cid           = 100001;
    c1.unread        = 3;
    c1.pinned        = true;
    c1.mute          = false;
    c1.lastmsgid     = 9007199254740993LL;
    c1.lastfrom      = 100001;
    c1.lastmsgtype   = 1;
    c1.lastcontent   = "{\"text\":\"good night\"}";
    c1.lasttimestamp = 1716267660000LL;

    ConversationBrief c2;
    c2.ctype         = 1;
    c2.cid           = 100002;
    c2.unread        = 0;
    c2.lastmsgid     = 9007199254740990LL;
    c2.lastfrom      = 100000;
    c2.lastmsgtype   = 2;  //图片消息
    c2.lastcontent   = "{\"fileid\":\"img_1\"}";
    c2.lasttimestamp = 1716267000000LL;

    std::vector<ConversationBrief> items;
    items.push_back(c1);
    items.push_back(c2);

    Json::Value root;
    REQUIRE(parseJson(
        MessageProtocol::buildSyncConversationsResponse(0, "ok", 1716268800000LL, items), root));
    CHECK(root["synctime"].asInt64() == 1716268800000LL);
    REQUIRE(root["conversations"].size() == 2);
    //会话1：文本消息，digest 即正文
    CHECK(root["conversations"][0]["cid"].asString() == "100001");
    CHECK(root["conversations"][0]["unread"].asInt() == 3);
    CHECK(root["conversations"][0]["pinned"].asBool() == true);
    CHECK(root["conversations"][0]["lastmsgid"].asString() == "9007199254740993");
    CHECK(root["conversations"][0]["lastmsg"]["digest"].asString() == "good night");
    CHECK(root["conversations"][0]["lastmsg"]["from"].asString() == "100001");
    //会话2：图片消息，digest 为占位文案
    CHECK(root["conversations"][1]["lastmsg"]["digest"].asString() == u8"[图片]");
}

TEST_CASE("S12 parseMarkReadRequest")
{
    MarkReadRequest req;
    REQUIRE(MessageProtocol::parseMarkReadRequest(
        "{\"ctype\":1,\"cid\":\"100001\",\"readmsgid\":\"501\"}", req));
    CHECK(req.ctype == 1);
    CHECK(req.cid == 100001);
    CHECK(req.readmsgid == 501);
    //cid / readmsgid 缺失或非正被拒
    CHECK_FALSE(MessageProtocol::parseMarkReadRequest("{\"readmsgid\":\"501\"}", req));
    CHECK_FALSE(MessageProtocol::parseMarkReadRequest("{\"cid\":\"100001\"}", req));
    CHECK_FALSE(MessageProtocol::parseMarkReadRequest(
        "{\"cid\":\"100001\",\"readmsgid\":\"0\"}", req));
}

TEST_CASE("S13 parseRecallMessageRequest")
{
    RecallMessageRequest req;
    REQUIRE(MessageProtocol::parseRecallMessageRequest(
        "{\"ctype\":1,\"cid\":\"100001\",\"msgid\":\"501\"}", req));
    CHECK(req.ctype == 1);
    CHECK(req.cid == 100001);
    CHECK(req.msgid == 501);
    //cid / msgid 缺失或非正被拒
    CHECK_FALSE(MessageProtocol::parseRecallMessageRequest("{\"msgid\":\"501\"}", req));
    CHECK_FALSE(MessageProtocol::parseRecallMessageRequest("{\"cid\":\"100001\"}", req));
    CHECK_FALSE(MessageProtocol::parseRecallMessageRequest(
        "{\"cid\":\"100001\",\"msgid\":\"0\"}", req));
}

TEST_CASE("S14 buildMessageReadNotify")
{
    Json::Value root;
    REQUIRE(parseJson(MessageProtocol::buildMessageReadNotify(1, 100000, 501, 1), root));
    CHECK(root["ctype"].asInt() == 1);
    CHECK(root["cid"].asString() == "100000");
    CHECK(root["readmsgid"].asString() == "501");
    CHECK(root["readcount"].asInt() == 1);
}

TEST_CASE("S15 buildMessageRecalledNotify")
{
    Json::Value root;
    REQUIRE(parseJson(MessageProtocol::buildMessageRecalledNotify(1, 100001, 501, 100000), root));
    CHECK(root["ctype"].asInt() == 1);
    CHECK(root["cid"].asString() == "100001");
    CHECK(root["msgid"].asString() == "501");
    //operator 字段为撤回者userid（字符串）
    CHECK(root["operator"].asString() == "100000");
}

TEST_CASE("S16 typing_notify 解析与构造")
{
    TypingNotifyRequest req;
    REQUIRE(MessageProtocol::parseTypingNotifyRequest(
        "{\"ctype\":1,\"cid\":\"100001\",\"state\":1}", req));
    CHECK(req.ctype == 1);
    CHECK(req.cid == 100001);
    CHECK(req.state == 1);
    //cid 缺失被拒
    CHECK_FALSE(MessageProtocol::parseTypingNotifyRequest("{\"state\":1}", req));

    Json::Value root;
    REQUIRE(parseJson(MessageProtocol::buildTypingNotify(1, 100000, 0), root));
    CHECK(root["cid"].asString() == "100000");
    CHECK(root["state"].asInt() == 0);
}

TEST_CASE("S16b file_transfer_ack 解析与构造")
{
    FileTransferAckRequest req;
    REQUIRE(MessageProtocol::parseFileTransferAckRequest(
        "{\"ctype\":1,\"cid\":\"100001\",\"msgid\":\"501\",\"state\":2}", req));
    CHECK(req.ctype == 1);
    CHECK(req.cid == 100001);
    CHECK(req.msgid == 501);
    CHECK(req.state == file_ack_received);

    //三个合法状态都要认
    REQUIRE(MessageProtocol::parseFileTransferAckRequest(
        "{\"cid\":\"100001\",\"msgid\":\"501\",\"state\":1}", req));
    CHECK(req.state == file_ack_receiving);
    REQUIRE(MessageProtocol::parseFileTransferAckRequest(
        "{\"cid\":\"100001\",\"msgid\":\"501\",\"state\":3}", req));
    CHECK(req.state == file_ack_rejected);

    //cid / msgid 缺失或非正被拒
    CHECK_FALSE(MessageProtocol::parseFileTransferAckRequest("{\"msgid\":\"501\",\"state\":1}", req));
    CHECK_FALSE(MessageProtocol::parseFileTransferAckRequest("{\"cid\":\"100001\",\"state\":1}", req));
    CHECK_FALSE(MessageProtocol::parseFileTransferAckRequest(
        "{\"cid\":\"100001\",\"msgid\":\"0\",\"state\":1}", req));

    //state 越界一律拒收 —— 它会被原样落库并回显给发送方，放进来就会显示成莫名其妙的状态
    CHECK_FALSE(MessageProtocol::parseFileTransferAckRequest(
        "{\"cid\":\"100001\",\"msgid\":\"501\",\"state\":0}", req));
    CHECK_FALSE(MessageProtocol::parseFileTransferAckRequest(
        "{\"cid\":\"100001\",\"msgid\":\"501\",\"state\":4}", req));
    CHECK_FALSE(MessageProtocol::parseFileTransferAckRequest(
        "{\"cid\":\"100001\",\"msgid\":\"501\"}", req));

    Json::Value root;
    REQUIRE(parseJson(MessageProtocol::buildFileTransferAckNotify(1, 100000, 501, file_ack_rejected), root));
    CHECK(root["ctype"].asInt() == 1);
    //大整数一律字符串编码，避免 JSON 精度丢失
    CHECK(root["cid"].asString() == "100000");
    CHECK(root["msgid"].asString() == "501");
    CHECK(root["state"].asInt() == file_ack_rejected);
}

TEST_CASE("S9b buildSyncMessagesResponse 带出文件收取状态")
{
    //fileackstate 非 0 才写进 JSON —— 老版本客户端不认识它，没有状态时不该平白多一个字段
    std::vector<MessageRecord> msgs;
    MessageRecord rec;
    rec.msgid        = 601;
    rec.ctype        = 1;
    rec.from         = 100000;
    rec.to           = 100001;
    rec.msgtype      = message_type_file;
    rec.content      = "{}";
    rec.timestamp    = 1700000000000LL;
    rec.fileackstate = file_ack_received;
    msgs.push_back(rec);

    Json::Value root;
    REQUIRE(parseJson(MessageProtocol::buildSyncMessagesResponse(0, "ok", 1, 100001, false, msgs), root));
    REQUIRE(root["messages"].isArray());
    REQUIRE(root["messages"].size() == 1);
    CHECK(root["messages"][0]["fileackstate"].asInt() == file_ack_received);

    //状态为 0（非文件消息 / 对方还没动作）时字段整个不出现
    msgs[0].fileackstate = 0;
    Json::Value root2;
    REQUIRE(parseJson(MessageProtocol::buildSyncMessagesResponse(0, "ok", 1, 100001, false, msgs), root2));
    CHECK_FALSE(root2["messages"][0].isMember("fileackstate"));
}

TEST_CASE("S17 set_conversation 解析(可选字段) 与 changed_notify 构造")
{
    //只给 pinned
    SetConversationRequest req;
    REQUIRE(MessageProtocol::parseSetConversationRequest(
        "{\"ctype\":1,\"cid\":\"100001\",\"pinned\":true}", req));
    CHECK(req.has_pinned == true);
    CHECK(req.pinned == true);
    CHECK(req.has_mute == false);
    CHECK(req.has_deleted == false);
    //给 mute + deleted
    SetConversationRequest req2;
    REQUIRE(MessageProtocol::parseSetConversationRequest(
        "{\"cid\":\"100001\",\"mute\":true,\"deleted\":false}", req2));
    CHECK(req2.has_mute == true);
    CHECK(req2.mute == true);
    CHECK(req2.has_deleted == true);
    CHECK(req2.deleted == false);
    CHECK(req2.has_pinned == false);
    //cid 缺失被拒
    CHECK_FALSE(MessageProtocol::parseSetConversationRequest("{\"pinned\":true}", req));

    Json::Value root;
    REQUIRE(parseJson(
        MessageProtocol::buildConversationChangedNotify(1, 100001, true, false, false), root));
    CHECK(root["cid"].asString() == "100001");
    CHECK(root["pinned"].asBool() == true);
    CHECK(root["mute"].asBool() == false);
    CHECK(root["deleted"].asBool() == false);
}

TEST_CASE("S18 parseSearchMessagesRequest")
{
    SearchMessagesRequest req;
    REQUIRE(MessageProtocol::parseSearchMessagesRequest(
        "{\"ctype\":1,\"cid\":\"100001\",\"keyword\":\"hello\","
        "\"beforemsgid\":\"500\",\"limit\":20}", req));
    CHECK(req.cid == 100001);
    CHECK(req.keyword == "hello");
    CHECK(req.beforemsgid == 500);
    CHECK(req.limit == 20);
    //keyword 缺失或为空被拒
    CHECK_FALSE(MessageProtocol::parseSearchMessagesRequest("{\"cid\":\"100001\"}", req));
    CHECK_FALSE(MessageProtocol::parseSearchMessagesRequest(
        "{\"cid\":\"100001\",\"keyword\":\"\"}", req));
    //cid 缺失被拒
    CHECK_FALSE(MessageProtocol::parseSearchMessagesRequest("{\"keyword\":\"x\"}", req));
}

TEST_CASE("S19 forward_message 解析与应答构造")
{
    ForwardMessageRequest req;
    REQUIRE(MessageProtocol::parseForwardMessageRequest(
        "{\"clientmsgid\":\"fwd-1\",\"toctype\":1,\"tocid\":\"100002\","
        "\"merge\":false,\"msgids\":[\"501\",\"502\"]}", req));
    CHECK(req.clientmsgid == "fwd-1");
    CHECK(req.tocid == 100002);
    CHECK(req.merge == false);
    REQUIRE(req.msgids.size() == 2);
    CHECK(req.msgids[0] == 501);
    CHECK(req.msgids[1] == 502);
    //clientmsgid / tocid / msgids 缺失或为空被拒
    CHECK_FALSE(MessageProtocol::parseForwardMessageRequest(
        "{\"tocid\":\"100002\",\"msgids\":[\"1\"]}", req));
    CHECK_FALSE(MessageProtocol::parseForwardMessageRequest(
        "{\"clientmsgid\":\"x\",\"tocid\":\"100002\",\"msgids\":[]}", req));

    std::vector<int64_t> ids;
    ids.push_back(9007199254741001LL);
    ids.push_back(9007199254741002LL);
    Json::Value root;
    REQUIRE(parseJson(MessageProtocol::buildForwardMessageResponse(0, "ok", ids), root));
    REQUIRE(root["msgids"].size() == 2);
    CHECK(root["msgids"][0].asString() == "9007199254741001");
}

TEST_CASE("S20 buildMergedContent")
{
    MessageRecord m1;
    m1.msgid = 501;
    m1.from = 100001;
    m1.to = 100000;
    m1.msgtype = 1;
    m1.content = "{\"text\":\"hi\"}";
    m1.timestamp = 1716268800000LL;
    MessageRecord m2;
    m2.msgid = 502;
    m2.from = 100000;
    m2.to = 100001;
    m2.msgtype = 1;
    m2.content = "{\"text\":\"yo\"}";
    m2.timestamp = 1716268900000LL;
    std::vector<MessageRecord> subs;
    subs.push_back(m1);
    subs.push_back(m2);

    Json::Value root;
    REQUIRE(parseJson(MessageProtocol::buildMergedContent("chat log", subs), root));
    CHECK(root["title"].asString() == "chat log");
    REQUIRE(root["messages"].size() == 2);
    CHECK(root["messages"][0]["msgid"].asString() == "501");
    CHECK(root["messages"][0]["content"]["text"].asString() == "hi");
    CHECK(root["messages"][1]["content"]["text"].asString() == "yo");
}

TEST_CASE("S21 parseDeleteMessageRequest")
{
    int64_t msgid = 0;
    REQUIRE(MessageProtocol::parseDeleteMessageRequest("{\"msgid\":\"501\"}", msgid));
    CHECK(msgid == 501);
    //msgid 缺失或非正被拒
    CHECK_FALSE(MessageProtocol::parseDeleteMessageRequest("{}", msgid));
    CHECK_FALSE(MessageProtocol::parseDeleteMessageRequest("{\"msgid\":\"0\"}", msgid));
}

TEST_CASE("S22 mass_send 解析与应答构造")
{
    MassSendRequest req;
    REQUIRE(MessageProtocol::parseMassSendRequest(
        "{\"clientmsgid\":\"ms-1\",\"msgtype\":1,\"content\":{\"text\":\"hi all\"},"
        "\"targets\":[{\"ctype\":1,\"cid\":\"100001\"},{\"ctype\":1,\"cid\":\"100002\"}]}", req));
    CHECK(req.clientmsgid == "ms-1");
    CHECK(req.msgtype == 1);
    REQUIRE(req.targets.size() == 2);
    CHECK(req.targets[0].cid == 100001);
    CHECK(req.targets[1].cid == 100002);
    //clientmsgid 缺失、targets 为空被拒
    CHECK_FALSE(MessageProtocol::parseMassSendRequest(
        "{\"msgtype\":1,\"content\":{},\"targets\":[{\"cid\":\"1\"}]}", req));
    CHECK_FALSE(MessageProtocol::parseMassSendRequest(
        "{\"clientmsgid\":\"x\",\"msgtype\":1,\"content\":{},\"targets\":[]}", req));

    //targets 条数超过 MASS_SEND_MAX_TARGETS 被拒 —— onMassSend 是在 EventLoop 线程上
    //串行落库的，目标一多会卡住该 loop 上的所有连接，故在解析阶段就挡掉。
    //正好卡在上限上仍应通过。
    std::string head = "{\"clientmsgid\":\"ms-limit\",\"msgtype\":1,\"content\":{\"text\":\"hi\"},\"targets\":[";
    std::string atLimit = head;
    for (size_t i = 0; i < MASS_SEND_MAX_TARGETS; ++i)
    {
        if (i > 0)
            atLimit += ",";
        atLimit += "{\"ctype\":1,\"cid\":\"" + std::to_string(100000 + i) + "\"}";
    }
    atLimit += "]}";
    MassSendRequest atLimitReq;
    REQUIRE(MessageProtocol::parseMassSendRequest(atLimit, atLimitReq));
    CHECK(atLimitReq.targets.size() == MASS_SEND_MAX_TARGETS);

    std::string overLimit = head;
    for (size_t i = 0; i <= MASS_SEND_MAX_TARGETS; ++i)
    {
        if (i > 0)
            overLimit += ",";
        overLimit += "{\"ctype\":1,\"cid\":\"" + std::to_string(100000 + i) + "\"}";
    }
    overLimit += "]}";
    MassSendRequest overLimitReq;
    CHECK_FALSE(MessageProtocol::parseMassSendRequest(overLimit, overLimitReq));

    std::vector<MassSendResult> results;
    MassSendResult r1;
    r1.cid = 100001;
    r1.msgid = 9007199254741001LL;
    MassSendResult r2;
    r2.cid = 100002;
    r2.msgid = 0;  //该目标被跳过
    results.push_back(r1);
    results.push_back(r2);
    Json::Value root;
    REQUIRE(parseJson(MessageProtocol::buildMassSendResponse(0, "ok", results), root));
    REQUIRE(root["results"].size() == 2);
    CHECK(root["results"][0]["cid"].asString() == "100001");
    CHECK(root["results"][0]["msgid"].asString() == "9007199254741001");
    CHECK(root["results"][1]["msgid"].asString() == "0");
}

// ---------------------------------------------------------------------------
// S23-S28  消息 Pin（cmd_pin_message / cmd_message_pinned_notify /
//          cmd_get_pinned_messages），方案见 docs/消息Pin接入方案.md
// ---------------------------------------------------------------------------

TEST_CASE("S23 parsePinMessageRequest 取出 ctype / cid / msgid / pin")
{
    PinMessageRequest req;
    REQUIRE(MessageProtocol::parsePinMessageRequest(
        "{\"ctype\":2,\"cid\":\"268435456\",\"msgid\":\"91002\",\"pin\":1}", req));
    CHECK(req.ctype == 2);
    CHECK(req.cid == 268435456);
    CHECK(req.msgid == 91002);
    CHECK(req.pin == 1);

    //取消方向同样要能解出来
    PinMessageRequest off;
    REQUIRE(MessageProtocol::parsePinMessageRequest(
        "{\"ctype\":1,\"cid\":\"5081\",\"msgid\":\"7\",\"pin\":0}", off));
    CHECK(off.ctype == 1);
    CHECK(off.pin == 0);

    //ctype 缺省按单聊处理，与撤回请求的容错口径一致
    PinMessageRequest noctype;
    REQUIRE(MessageProtocol::parsePinMessageRequest(
        "{\"cid\":\"5081\",\"msgid\":\"7\",\"pin\":1}", noctype));
    CHECK(noctype.ctype == 1);
}

TEST_CASE("S24 parsePinMessageRequest 对缺字段与越界 pin 一律判非法")
{
    PinMessageRequest req;
    //畸形报文
    CHECK_FALSE(MessageProtocol::parsePinMessageRequest("", req));
    CHECK_FALSE(MessageProtocol::parsePinMessageRequest("{oops", req));
    CHECK_FALSE(MessageProtocol::parsePinMessageRequest("[]", req));
    //cid / msgid 缺失或非正
    CHECK_FALSE(MessageProtocol::parsePinMessageRequest("{\"msgid\":\"7\",\"pin\":1}", req));
    CHECK_FALSE(MessageProtocol::parsePinMessageRequest("{\"cid\":\"5081\",\"pin\":1}", req));
    CHECK_FALSE(MessageProtocol::parsePinMessageRequest(
        "{\"cid\":\"0\",\"msgid\":\"7\",\"pin\":1}", req));
    CHECK_FALSE(MessageProtocol::parsePinMessageRequest(
        "{\"cid\":\"5081\",\"msgid\":\"-1\",\"pin\":1}", req));
    //pin 缺失、越界、或不是整数：都不能被当成"取消"糊弄过去
    CHECK_FALSE(MessageProtocol::parsePinMessageRequest(
        "{\"cid\":\"5081\",\"msgid\":\"7\"}", req));
    CHECK_FALSE(MessageProtocol::parsePinMessageRequest(
        "{\"cid\":\"5081\",\"msgid\":\"7\",\"pin\":2}", req));
    CHECK_FALSE(MessageProtocol::parsePinMessageRequest(
        "{\"cid\":\"5081\",\"msgid\":\"7\",\"pin\":-1}", req));
    CHECK_FALSE(MessageProtocol::parsePinMessageRequest(
        "{\"cid\":\"5081\",\"msgid\":\"7\",\"pin\":\"1\"}", req));
}

TEST_CASE("S25 buildPinMessageResponse 回带四个字段，失败分支 code 与 msg 非空")
{
    Json::Value root;
    REQUIRE(parseJson(MessageProtocol::buildPinMessageResponse(
        0, "ok", 2, 268435456, 91002, 1), root));
    CHECK(root["code"].asInt() == 0);
    CHECK(root["msg"].asString() == "ok");
    CHECK(root["ctype"].asInt() == 2);
    //64 位 ID 一律以字符串编码，避免大整数在 JSON 里丢精度
    CHECK(root["cid"].asString() == "268435456");
    CHECK(root["msgid"].asString() == "91002");
    CHECK(root["pin"].asInt() == 1);

    //无权取消：code 为 error_code_pin_not_allowed，且照样回带定位信息，
    //客户端才能知道是哪一条钉失败了
    Json::Value denied;
    REQUIRE(parseJson(MessageProtocol::buildPinMessageResponse(
        error_code_pin_not_allowed, "no permission to unpin", 2, 268435456, 91002, 0), denied));
    CHECK(denied["code"].asInt() == error_code_pin_not_allowed);
    CHECK_FALSE(denied["msg"].asString().empty());
    CHECK(denied["msgid"].asString() == "91002");
    CHECK(denied["pin"].asInt() == 0);
}

TEST_CASE("S26 buildMessagePinnedNotify 六个字段齐全且 64 位 ID 为字符串")
{
    Json::Value root;
    REQUIRE(parseJson(MessageProtocol::buildMessagePinnedNotify(
        1, 5081, 91002, 1, 3001, 1754300000000LL), root));
    CHECK(root["ctype"].asInt() == 1);
    CHECK(root["cid"].asString() == "5081");
    CHECK(root["msgid"].asString() == "91002");
    CHECK(root["pin"].asInt() == 1);
    CHECK(root["operator"].asString() == "3001");
    CHECK(root["pintime"].asString() == "1754300000000");

    //取消方向：pin 为 0、pintime 传 0
    Json::Value off;
    REQUIRE(parseJson(MessageProtocol::buildMessagePinnedNotify(
        2, 268435456, 91002, 0, 3001, 0), off));
    CHECK(off["pin"].asInt() == 0);
    CHECK(off["pintime"].asString() == "0");
}

TEST_CASE("S27 parseGetPinnedMessagesRequest 与空列表应答")
{
    GetPinnedMessagesRequest req;
    REQUIRE(MessageProtocol::parseGetPinnedMessagesRequest(
        "{\"ctype\":2,\"cid\":\"268435456\"}", req));
    CHECK(req.ctype == 2);
    CHECK(req.cid == 268435456);
    //cid 缺失 / 非正被拒
    CHECK_FALSE(MessageProtocol::parseGetPinnedMessagesRequest("{\"ctype\":2}", req));
    CHECK_FALSE(MessageProtocol::parseGetPinnedMessagesRequest("{\"cid\":\"0\"}", req));

    //一条都没钉：msgs 是空数组而不是 null，total 为 0
    std::vector<MessageRecord> noMessages;
    std::vector<PinRecord> noPins;
    Json::Value root;
    REQUIRE(parseJson(MessageProtocol::buildGetPinnedMessagesResponse(
        0, "ok", 2, 268435456, 0, noMessages, noPins), root));
    CHECK(root["code"].asInt() == 0);
    CHECK(root["cid"].asString() == "268435456");
    CHECK(root["total"].asInt64() == 0);
    REQUIRE(root["msgs"].isArray());
    CHECK(root["msgs"].size() == 0);
}

TEST_CASE("S28 buildGetPinnedMessagesResponse 每条带 pin 子对象；截断时 total 大于返回条数")
{
    MessageRecord m1;
    m1.msgid     = 91002;
    m1.ctype     = 2;
    m1.from      = 3001;
    m1.to        = 268435456;
    m1.msgtype   = 1;
    m1.content   = "{\"text\":\"周会链接\"}";
    m1.timestamp = 1754200000000LL;

    MessageRecord m2 = m1;
    m2.msgid     = 91001;
    m2.timestamp = 1754100000000LL;

    PinRecord p1;
    p1.msgid      = 91002;
    p1.operatorId = 3001;
    p1.pinTime    = 1754300000000LL;

    PinRecord p2;
    p2.msgid      = 91001;
    p2.operatorId = 3002;
    p2.pinTime    = 1754310000000LL;

    std::vector<MessageRecord> messages;
    messages.push_back(m1);
    messages.push_back(m2);
    std::vector<PinRecord> pins;
    pins.push_back(p1);
    pins.push_back(p2);

    //total 传 37：会话里共钉了 37 条，本次只返回了 2 条（发生截断）。
    //客户端就是靠 total > msgs.size() 判断截断，进而决定本地对账的时间下界
    Json::Value root;
    REQUIRE(parseJson(MessageProtocol::buildGetPinnedMessagesResponse(
        0, "ok", 2, 268435456, 37, messages, pins), root));
    CHECK(root["total"].asInt64() == 37);
    REQUIRE(root["msgs"].size() == 2);
    CHECK(root["total"].asInt64() > static_cast<Json::Int64>(root["msgs"].size()));

    //消息体本身与 sync_messages 同结构
    CHECK(root["msgs"][0]["msgid"].asString() == "91002");
    CHECK(root["msgs"][0]["content"]["text"].asString() == "周会链接");
    //额外挂着的 pin 子对象说明这条是谁在什么时候钉的
    CHECK(root["msgs"][0]["pin"]["operator"].asString() == "3001");
    CHECK(root["msgs"][0]["pin"]["pintime"].asString() == "1754300000000");
    CHECK(root["msgs"][1]["pin"]["operator"].asString() == "3002");

    //pins 给短了：不足的部分按 0 输出，宁可少一份元信息也不能让整个应答错位
    std::vector<PinRecord> shortPins;
    shortPins.push_back(p1);
    Json::Value partial;
    REQUIRE(parseJson(MessageProtocol::buildGetPinnedMessagesResponse(
        0, "ok", 2, 268435456, 2, messages, shortPins), partial));
    REQUIRE(partial["msgs"].size() == 2);
    CHECK(partial["msgs"][0]["pin"]["operator"].asString() == "3001");
    CHECK(partial["msgs"][1]["pin"]["operator"].asString() == "0");
    CHECK(partial["msgs"][1]["pin"]["pintime"].asString() == "0");
}
