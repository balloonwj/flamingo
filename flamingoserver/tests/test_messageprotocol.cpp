/**
 *  v2 会话与消息报文编解码单元测试（Suite S），test_messageprotocol.cpp
 *  覆盖：send_message 请求解析（基本/带@与引用/校验失败），发送回执构造，
 *       message_notify 完整消息体构造，received_ack 解析，
 *       f_content 落库内容的打包/解包往返；sync_messages 请求解析与应答构造，
 *       消息摘要 digest 生成，sync_conversations 应答构造；
 *       mark_read / recall_message 请求解析，已读/撤回通知构造；
 *       typing/set_conversation/search/forward/delete/mass_send 报文编解码；
 *       转发的单次条数上限与合并标题的截断、缺省值（FWD-S1、FWD-S2），逐条转发
 *       去掉 at 与 quote、合并转发保留原样（FWD-S4、FWD-S5），
 *       转发应答的完整消息体（FWD-S8），合并子消息的发送者
 *       名字（FWD-S9），合并记录与各类应答的体积上限（FWD-S10a～c），
 *       见 docs/聊天消息转发方案.md；
 *       普通消息 content 与 quote 合计的体积上限（MSG-S1～S5）：
 *       边界、合计口径、中文按重新序列化后的字节数计量、最大消息组成推送帧后仍小于
 *       单帧上限、逐条转发复制出的内容同样受限；
 *       消息 Pin 的请求解析、应答与推送构造，以及被 Pin 消息列表应答里
 *       total 与 pin 子对象的正确性（S23-S28，见 docs/消息Pin接入方案.md）；
 *       从消息 content 中取所引用文件的 fileid（CF1-CF5，服务端据此把上传者
 *       本人的文件记录标记为已完成，使同内容再次上传能命中秒传）；
 *       发送被拒的回执带回 clientmsgid、从一条消息里取出需要做敏感词检查的文字
 *       （SW-P1～SW-P7，见 docs/敏感词方案.md 第 6.3.1 节；SW-P7 是代码片段的批注；原先的 FWD-S7 测的是
 *       已被 collectSensitiveTexts 取代的 collectForwardTexts，2026-10-02 删除）；
 *       语音消息 content 的校验（VMS1～VMS6，见 docs/语音消息方案.md 第 5、9 节）。
 *  balloonwj@qq.com   2026-05-21
 */
#include <map>
#include <sstream>
#include "doctest.h"
#include "../chatserversrc/MessageProtocol.h"
#include "../chatserversrc/Protocol.h"   //enum file_ack_state / message_type_file
#include "../chatserversrc/GroupProtocol.h"      //TIP2~TIP4：群系统提示的 content
#include "../chatserversrc/ScreenShareRules.h"   //TIP5：屏幕共享结束提示的 content
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

//2026-09-30：客户端发送方要靠这个字段才能在重开聊天窗、或离线期间对方读过之后，
//看到正确的「已读 / 未读」。实时的 cmd_message_read_notify 只推给在线的发送方。
TEST_CASE("S9c buildSyncMessagesResponse 单聊带出对方已读位点")
{
    std::vector<MessageRecord> msgs;

    //单聊：带上对方已读位点，字符串编码（超过 2^53 也不丢精度）
    Json::Value root;
    REQUIRE(parseJson(MessageProtocol::buildSyncMessagesResponse(
        0, "ok", 1, 100001, false, msgs, 9007199254741001LL), root));
    CHECK(root["peerreadmsgid"].isString());
    CHECK(root["peerreadmsgid"].asString() == "9007199254741001");

    //对方从没读过：位点为 0，照样输出 —— 客户端据此知道「查过了，一条都没读」
    Json::Value rootZero;
    REQUIRE(parseJson(MessageProtocol::buildSyncMessagesResponse(
        0, "ok", 1, 100001, false, msgs, 0), rootZero));
    CHECK(rootZero["peerreadmsgid"].asString() == "0");

    //群聊：按协议不做已读回执，即便传了位点也不输出
    Json::Value rootGroup;
    REQUIRE(parseJson(MessageProtocol::buildSyncMessagesResponse(
        0, "ok", 2, 600001, false, msgs, 123), rootGroup));
    CHECK_FALSE(rootGroup.isMember("peerreadmsgid"));

    //不传位点（cmd_search_messages 复用本函数）：不输出
    Json::Value rootSearch;
    REQUIRE(parseJson(MessageProtocol::buildSyncMessagesResponse(
        0, "ok", 1, 100001, false, msgs), rootSearch));
    CHECK_FALSE(rootSearch.isMember("peerreadmsgid"));
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

    //接龙卡片带上标题；没有标题、标题为空串或内容不是 JSON 时只显示类型（docs/群接龙方案.md §3.7）
    CHECK(MessageProtocol::buildMessageDigest(11,
              u8"{\"title\":\"周五团建报名\",\"note\":\"\",\"deadline\":0}") == u8"[接龙] 周五团建报名");
    CHECK(MessageProtocol::buildMessageDigest(11, "{}") == u8"[接龙]");
    CHECK(MessageProtocol::buildMessageDigest(11, "{\"title\":\"\"}") == u8"[接龙]");
    CHECK(MessageProtocol::buildMessageDigest(11, "not json") == u8"[接龙]");

    //接龙系统提示的 content 多带一个 cardmsgid，摘要仍取正文
    CHECK(MessageProtocol::buildMessageDigest(17,
              "{\"text\":\"system notice\",\"cardmsgid\":\"9007199254741011\"}") == "system notice");
}

TEST_CASE("S11 buildSyncConversationsResponse")
{
    ConversationBrief c1;
    c1.ctype         = 1;
    c1.cid           = 100001;
    c1.unread        = 3;
    c1.pinned        = true;
    c1.notifymode    = notify_mode_silent;
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
    CHECK(root["conversations"][0]["notifymode"].asInt() == notify_mode_silent);
    CHECK_FALSE(root["conversations"][0].isMember("mute"));
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
    CHECK(req.has_notifymode == false);
    CHECK(req.has_deleted == false);
    //给 notifymode + deleted
    SetConversationRequest req2;
    REQUIRE(MessageProtocol::parseSetConversationRequest(
        "{\"cid\":\"100001\",\"notifymode\":1,\"deleted\":false}", req2));
    CHECK(req2.has_notifymode == true);
    CHECK(req2.notifymode == notify_mode_silent);
    CHECK(req2.has_deleted == true);
    CHECK(req2.deleted == false);
    CHECK(req2.has_pinned == false);
    //cid 缺失被拒
    CHECK_FALSE(MessageProtocol::parseSetConversationRequest("{\"pinned\":true}", req));

    Json::Value root;
    REQUIRE(parseJson(
        MessageProtocol::buildConversationChangedNotify(1, 100001, true, notify_mode_allow, false, false), root));
    CHECK(root["cid"].asString() == "100001");
    CHECK(root["pinned"].asBool() == true);
    CHECK(root["notifymode"].asInt() == notify_mode_allow);
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

    std::vector<MessageRecord> created(2);
    created[0].msgid = 9007199254741001LL;
    created[1].msgid = 9007199254741002LL;
    Json::Value root;
    REQUIRE(parseJson(MessageProtocol::buildForwardMessageResponse(0, "ok", "fwd-1", created, 100002), root));
    REQUIRE(root["msgids"].size() == 2);
    CHECK(root["msgids"][0].asString() == "9007199254741001");
}

//拼一个带 count 个 msgid 的 cmd_forward_message 请求体，msgid 从 1 起连续编号
static std::string buildForwardRequestWithCount(size_t count)
{
    std::ostringstream os;
    os << "{\"clientmsgid\":\"fwd-n\",\"toctype\":1,\"tocid\":\"100002\",\"merge\":true,\"msgids\":[";
    for (size_t i = 0; i < count; ++i)
    {
        if (i > 0)
            os << ",";
        os << "\"" << (i + 1) << "\"";
    }
    os << "]}";
    return os.str();
}

//数 UTF-8 串里的字符（码点）个数：不是续字节（10xxxxxx）的字节各算一个字符
static size_t countUtf8Chars(const std::string& s)
{
    size_t n = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80)
            ++n;
    }
    return n;
}

//把同一个字符串重复 times 次
static std::string repeatText(const std::string& unit, size_t times)
{
    std::string out;
    for (size_t i = 0; i < times; ++i)
        out += unit;
    return out;
}

TEST_CASE("FWD-S1 forward_message 单次转发条数上限")
{
    //恰好等于上限：通过，条数原样保留
    ForwardMessageRequest req;
    REQUIRE(MessageProtocol::parseForwardMessageRequest(
        buildForwardRequestWithCount(FORWARD_MAX_MESSAGES), req));
    CHECK(req.msgids.size() == FORWARD_MAX_MESSAGES);

    //超过上限一条：在解析阶段拒绝，不让它进入逐条落库的循环
    ForwardMessageRequest over;
    CHECK_FALSE(MessageProtocol::parseForwardMessageRequest(
        buildForwardRequestWithCount(FORWARD_MAX_MESSAGES + 1), over));
}

TEST_CASE("FWD-S2 合并转发标题的截断与缺省值")
{
    const std::string han = u8"字";

    //超长的纯中文标题：截到上限字符数，且是原串的前缀
    const std::string longTitle = repeatText(han, FORWARD_TITLE_MAX_CHARS + 6);
    const std::string cut = MessageProtocol::normalizeForwardTitle(longTitle);
    CHECK(countUtf8Chars(cut) == FORWARD_TITLE_MAX_CHARS);
    CHECK(cut == repeatText(han, FORWARD_TITLE_MAX_CHARS));

    //截断点落在多字节字符上时，整个字符一起舍去，不留半个字符
    const std::string mixed = std::string(FORWARD_TITLE_MAX_CHARS - 1, 'a') + han + han;
    const std::string mixedCut = MessageProtocol::normalizeForwardTitle(mixed);
    CHECK(mixedCut == std::string(FORWARD_TITLE_MAX_CHARS - 1, 'a') + han);

    //不超长的标题原样保留
    const std::string shortTitle = u8"张三和李四的聊天记录";
    CHECK(MessageProtocol::normalizeForwardTitle(shortTitle) == shortTitle);

    //空标题取缺省值
    CHECK(MessageProtocol::normalizeForwardTitle("") == std::string(u8"聊天记录"));

    //解析请求时同样按上述规则处理 title 字段
    ForwardMessageRequest req;
    REQUIRE(MessageProtocol::parseForwardMessageRequest(
        "{\"clientmsgid\":\"fwd-t\",\"toctype\":1,\"tocid\":\"100002\",\"merge\":true,"
        "\"title\":\"" + longTitle + "\",\"msgids\":[\"1\"]}", req));
    CHECK(req.title == repeatText(han, FORWARD_TITLE_MAX_CHARS));

    ForwardMessageRequest noTitle;
    REQUIRE(MessageProtocol::parseForwardMessageRequest(
        "{\"clientmsgid\":\"fwd-t\",\"toctype\":1,\"tocid\":\"100002\",\"merge\":true,"
        "\"msgids\":[\"1\"]}", noTitle));
    CHECK(noTitle.title == std::string(u8"聊天记录"));
}

TEST_CASE("FWD-S4 逐条转发去掉文字消息 content 里的 at 与 quote")
{
    //带字体、@ 与引用的文字消息：转发后 text 与 font 不变，at 与 quote 被去掉
    const std::string src =
        "{\"text\":\"@张三 看这里\","
        "\"font\":{\"size\":12,\"bold\":true,\"italic\":false,\"underline\":false,"
        "\"strike\":false,\"color\":\"FF0000\"},"
        "\"at\":{\"all\":false,\"users\":[{\"uid\":\"100003\",\"off\":0,\"len\":7}]},"
        "\"quote\":{\"msgid\":\"501\",\"from\":\"100003\",\"name\":\"张三\",\"msgtype\":1,"
        "\"digest\":\"原文\"}}";
    Json::Value out;
    REQUIRE(parseJson(MessageProtocol::buildForwardedContent(1, src), out));
    CHECK(out["text"].asString() == std::string(u8"@张三 看这里"));
    CHECK(out["font"]["size"].asInt() == 12);
    CHECK(out["font"]["bold"].asBool() == true);
    CHECK(out["font"]["color"].asString() == "FF0000");
    CHECK_FALSE(out.isMember("at"));
    CHECK_FALSE(out.isMember("quote"));

    //@全体成员同样去掉：否则转到另一个群，会给那个群的全体成员发出 @ 提醒
    Json::Value outAll;
    REQUIRE(parseJson(MessageProtocol::buildForwardedContent(
        1, "{\"text\":\"@所有人 开会\",\"at\":{\"all\":true,\"users\":[]}}"), outAll));
    CHECK(outAll["text"].asString() == std::string(u8"@所有人 开会"));
    CHECK_FALSE(outAll.isMember("at"));

    //没有 at 与 quote 的文字消息：内容保持不变
    Json::Value plain;
    REQUIRE(parseJson(MessageProtocol::buildForwardedContent(1, "{\"text\":\"hi\"}"), plain));
    CHECK(plain["text"].asString() == "hi");
    CHECK(plain.size() == 1);

    //非文字消息原样返回，逐字节相同
    const std::string image =
        "{\"fileid\":\"123\",\"md5\":\"abc\",\"width\":10,\"height\":20,\"size\":300}";
    CHECK(MessageProtocol::buildForwardedContent(2, image) == image);
    const std::string merged = "{\"title\":\"t\",\"messages\":[]}";
    CHECK(MessageProtocol::buildForwardedContent(15, merged) == merged);

    //文字消息的 content 解析失败时原样返回，不丢内容
    const std::string broken = "{\"text\":";
    CHECK(MessageProtocol::buildForwardedContent(1, broken) == broken);
}

//构造一条转发后新建的消息记录，字段取值带上序号 i，便于断言顺序
static MessageRecord makeCreatedRecord(int i)
{
    MessageRecord rec;
    rec.msgid = 9007199254742000LL + i;
    rec.clientmsgid = "fwd-8-" + std::to_string(i);
    rec.ctype = 2;
    rec.from = 100001;
    rec.to = 900001;
    rec.msgtype = 1;
    rec.content = "{\"text\":\"m" + std::to_string(i) + "\"}";
    rec.timestamp = 1716268800000LL + i;
    return rec;
}

TEST_CASE("FWD-S8 转发应答带 clientmsgid 与新建消息的完整消息体")
{
    std::vector<MessageRecord> created;
    for (int i = 0; i < 3; ++i)
        created.push_back(makeCreatedRecord(i));

    Json::Value root;
    REQUIRE(parseJson(MessageProtocol::buildForwardMessageResponse(0, "ok", "fwd-8", created, 900001), root));
    CHECK(root["code"].asInt() == 0);
    CHECK(root["clientmsgid"].asString() == "fwd-8");

    //msgids 保留，老客户端照旧可用；messages 与 msgids 条数、顺序一致
    REQUIRE(root["msgids"].size() == 3);
    REQUIRE(root["messages"].size() == 3);
    for (Json::ArrayIndex i = 0; i < 3; ++i)
    {
        const Json::Value& m = root["messages"][i];
        CHECK(m["msgid"].asString() == root["msgids"][i].asString());
        CHECK(m["msgid"].asString() == std::to_string(9007199254742000LL + i));
        CHECK(m["clientmsgid"].asString() == "fwd-8-" + std::to_string(i));
        CHECK(m["ctype"].asInt() == 2);
        CHECK(m["cid"].asString() == "900001");
        CHECK(m["from"].asString() == "100001");
        CHECK(m["msgtype"].asInt() == 1);
        CHECK(m["content"]["text"].asString() == "m" + std::to_string(i));
        CHECK(m["timestamp"].asInt64() == 1716268800000LL + i);
    }

    //一条都没建成（全部落库失败）时，两个数组都为空数组而不是缺失
    Json::Value empty;
    REQUIRE(parseJson(MessageProtocol::buildForwardMessageResponse(
        0, "ok", "fwd-9", std::vector<MessageRecord>(), 900001), empty));
    CHECK(empty["msgids"].isArray());
    CHECK(empty["msgids"].size() == 0);
    CHECK(empty["messages"].isArray());
    CHECK(empty["messages"].size() == 0);
}

TEST_CASE("FWD-S9 合并转发的子消息带发送者名字 fromname")
{
    MessageRecord a;
    a.msgid = 701;
    a.from = 100001;
    a.msgtype = 1;
    a.content = "{\"text\":\"a\"}";
    MessageRecord b;
    b.msgid = 702;
    b.from = 100002;
    b.msgtype = 1;
    b.content = "{\"text\":\"b\"}";
    MessageRecord c;
    c.msgid = 703;
    c.from = 100003;
    c.msgtype = 1;
    c.content = "{\"text\":\"c\"}";
    std::vector<MessageRecord> subs;
    subs.push_back(a);
    subs.push_back(b);
    subs.push_back(c);

    //第三个发送者查不到名字（例如账号已被删除）
    std::map<int64_t, std::string> names;
    names[100001] = u8"张三";
    names[100002] = u8"李四";

    Json::Value root;
    REQUIRE(parseJson(MessageProtocol::buildMergedContent("t", subs, names), root));
    REQUIRE(root["messages"].size() == 3);
    CHECK(root["messages"][0]["fromname"].asString() == std::string(u8"张三"));
    CHECK(root["messages"][1]["fromname"].asString() == std::string(u8"李四"));
    //查不到名字的不写 fromname，客户端据此改用本地通讯录解析
    CHECK_FALSE(root["messages"][2].isMember("fromname"));
    CHECK(root["messages"][2]["from"].asString() == "100003");
}

//构造一条正文为 textBytes 个 'a' 的文字消息，msgid 取 msgid
static MessageRecord makeSizedRecord(int64_t msgid, size_t textBytes)
{
    MessageRecord rec;
    rec.msgid = msgid;
    rec.clientmsgid = "sized";
    rec.ctype = 1;
    rec.from = 100001;
    rec.to = 100002;
    rec.msgtype = 1;
    rec.content = "{\"text\":\"" + std::string(textBytes, 'a') + "\"}";
    rec.timestamp = 1716268800000LL;
    return rec;
}

//取一组记录的 msgid，便于断言截断后留下的是哪几条
static std::vector<int64_t> msgidsOf(const std::vector<MessageRecord>& records)
{
    std::vector<int64_t> ids;
    for (size_t i = 0; i < records.size(); ++i)
        ids.push_back(records[i].msgid);
    return ids;
}

TEST_CASE("FWD-S10a 合并记录的内容体积上限")
{
    CHECK_FALSE(MessageProtocol::isMergedContentTooLarge(std::string(FORWARD_MERGED_CONTENT_MAX_BYTES, 'x')));
    CHECK(MessageProtocol::isMergedContentTooLarge(std::string(FORWARD_MERGED_CONTENT_MAX_BYTES + 1, 'x')));
    //上限必须明显小于客户端能收下的单帧原始长度，给消息体的其余字段留出余量
    CHECK(FORWARD_MERGED_CONTENT_MAX_BYTES < MAX_BODY_CHATSERVER / 2);
}

TEST_CASE("FWD-S10b 转发应答的消息体总量超过预算时，后面的只给 msgid")
{
    //每条正文约 200 KiB：预算 512 KiB 只装得下前两条
    const size_t textBytes = 200 * 1024;
    std::vector<MessageRecord> created;
    for (int64_t i = 1; i <= 4; ++i)
        created.push_back(makeSizedRecord(i, textBytes));

    const std::string resp = MessageProtocol::buildForwardMessageResponse(0, "ok", "fwd-b", created, 100002);
    Json::Value root;
    REQUIRE(parseJson(resp, root));
    REQUIRE(root["msgids"].size() == 4);
    REQUIRE(root["messages"].size() == 2);
    //messages 是 msgids 的前缀，两者逐条对应
    CHECK(root["messages"][0]["msgid"].asString() == root["msgids"][0].asString());
    CHECK(root["messages"][1]["msgid"].asString() == root["msgids"][1].asString());
    //整个应答不超过客户端能收下的单帧原始长度
    CHECK(resp.size() < MAX_BODY_CHATSERVER);
}

TEST_CASE("FWD-S10c 同步类应答按体积截断")
{
    //每条消息体序列化后的字节数都相同，取其 3.5 倍作预算：恰好装下 3 条
    const size_t textBytes = 1000;
    std::vector<MessageRecord> base;
    for (int64_t i = 1; i <= 10; ++i)
        base.push_back(makeSizedRecord(i, textBytes));
    const size_t one = MessageProtocol::buildMessageNotify(base[0], 100002).size();
    const size_t budget = one * 3 + one / 2;

    //保留末尾（往前翻页、搜索：锚点一侧是最新的那几条）
    std::vector<MessageRecord> tail = base;
    CHECK(MessageProtocol::trimMessagesToBudget(tail, 100002, true, budget) == 7);
    std::vector<int64_t> tailIds;
    tailIds.push_back(8);
    tailIds.push_back(9);
    tailIds.push_back(10);
    CHECK(msgidsOf(tail) == tailIds);

    //保留开头（补齐模式、Pin 列表：锚点一侧是开头的那几条）
    std::vector<MessageRecord> head = base;
    CHECK(MessageProtocol::trimMessagesToBudget(head, 100002, false, budget) == 7);
    std::vector<int64_t> headIds;
    headIds.push_back(1);
    headIds.push_back(2);
    headIds.push_back(3);
    CHECK(msgidsOf(head) == headIds);

    //不超预算：一条都不删
    std::vector<MessageRecord> small = base;
    CHECK(MessageProtocol::trimMessagesToBudget(small, 100002, true, one * 20) == 0);
    CHECK(small.size() == 10);

    //单条就超预算：仍保留一条，否则客户端永远翻不过这一条
    std::vector<MessageRecord> huge;
    huge.push_back(makeSizedRecord(1, textBytes));
    huge.push_back(makeSizedRecord(2, textBytes));
    CHECK(MessageProtocol::trimMessagesToBudget(huge, 100002, true, one / 2) == 1);
    REQUIRE(huge.size() == 1);
    CHECK(huge[0].msgid == 2);

    //空列表：什么都不做
    std::vector<MessageRecord> none;
    CHECK(MessageProtocol::trimMessagesToBudget(none, 100002, true, budget) == 0);
}

// ---- SB：按体积预算组装同步类应答（先整页组装，超过预算才截断后重新组装）----

//原先的做法：先 trimMessagesToBudget，截断了就把 hasmore 置真，再组装。
//messages 与 hasMore 都按值传入，不影响调用方手里的那一份；结果与留下的消息经出参带回
static std::string buildSyncReference(std::vector<MessageRecord> messages, bool hasMore, int64_t cid,
                                      int64_t peerReadMsgid, const SyncMessagesRequest* anchorReq,
                                      bool keepTail, size_t budget, std::vector<int64_t>& keptIds,
                                      bool& hasMoreOut)
{
    if (MessageProtocol::trimMessagesToBudget(messages, cid, keepTail, budget) > 0)
        hasMore = true;
    keptIds = msgidsOf(messages);
    hasMoreOut = hasMore;
    return MessageProtocol::buildSyncMessagesResponse(0, "ok", 1, cid, hasMore, messages,
                                                      peerReadMsgid, anchorReq);
}

//Pin 列表原先的做法：保留开头截断，pins 截到不比 messages 长，再组装
static std::string buildPinnedReference(std::vector<MessageRecord> messages, std::vector<PinRecord> pins,
                                        int64_t cid, int64_t total, size_t budget,
                                        std::vector<int64_t>& keptIds, size_t& pinCount)
{
    MessageProtocol::trimMessagesToBudget(messages, cid, false, budget);
    if (pins.size() > messages.size())
        pins.resize(messages.size());
    keptIds = msgidsOf(messages);
    pinCount = pins.size();
    return MessageProtocol::buildGetPinnedMessagesResponse(0, "ok", 2, cid, total, messages, pins);
}

//构造 count 条正文各 textBytes 字节的文字消息，msgid 从 1 起
static std::vector<MessageRecord> makeSizedRecords(int count, size_t textBytes)
{
    std::vector<MessageRecord> records;
    for (int64_t i = 1; i <= count; ++i)
        records.push_back(makeSizedRecord(i, textBytes));
    return records;
}

//构造与 messages 一一对应的 Pin 元信息，另外多出 extra 条（模拟 pins 比 messages 长）
static std::vector<PinRecord> makePinsFor(const std::vector<MessageRecord>& messages, size_t extra)
{
    std::vector<PinRecord> pins;
    for (size_t i = 0; i < messages.size() + extra; ++i)
    {
        PinRecord pin;
        pin.msgid = static_cast<int64_t>(i + 1);
        pin.operatorId = 100001;
        pin.pinTime = 1716268800000LL + static_cast<int64_t>(i);
        pins.push_back(pin);
    }
    return pins;
}

TEST_CASE("SB1 整页没超过预算：一次组装，结果与原做法相同，不截断")
{
    const int64_t cid = 100002;
    const std::vector<MessageRecord> base = makeSizedRecords(10, 1000);
    SyncMessagesRequest anchor;
    anchor.cid = cid;
    anchor.beforemsgid = 11;

    std::vector<int64_t> refIds;
    bool refHasMore = false;
    const std::string expected = buildSyncReference(base, false, cid, 5, &anchor, true,
                                                    RESPONSE_MESSAGES_BUDGET_BYTES, refIds, refHasMore);

    std::vector<MessageRecord> messages = base;
    bool hasMore = false;
    size_t trimmed = 99;
    const std::string actual = MessageProtocol::buildSyncMessagesResponseWithinBudget(
        0, "ok", 1, cid, hasMore, messages, 5, &anchor, true, RESPONSE_MESSAGES_BUDGET_BYTES, trimmed);

    CHECK(actual == expected);
    CHECK(trimmed == 0);
    CHECK(hasMore == false);
    CHECK(msgidsOf(messages) == refIds);
    CHECK(messages.size() == 10);
}

TEST_CASE("SB2 整页超过预算：两种保留方向的结果都与原做法相同，hasmore 置真")
{
    const int64_t cid = 100002;
    const std::vector<MessageRecord> base = makeSizedRecords(10, 1000);
    //与 FWD-S10c 相同的取法：预算为单条消息体的 3.5 倍，恰好装下 3 条
    const size_t one = MessageProtocol::buildMessageNotify(base[0], cid).size();
    const size_t budget = one * 3 + one / 2;
    SyncMessagesRequest anchor;
    anchor.cid = cid;
    anchor.isAfterMode = true;
    anchor.aftermsgid = 0;

    const bool directions[] = { true, false };
    for (size_t d = 0; d < sizeof(directions) / sizeof(directions[0]); ++d)
    {
        const bool keepTail = directions[d];
        CAPTURE(keepTail);

        std::vector<int64_t> refIds;
        bool refHasMore = false;
        const std::string expected = buildSyncReference(base, false, cid, 7, &anchor, keepTail, budget,
                                                        refIds, refHasMore);

        std::vector<MessageRecord> messages = base;
        bool hasMore = false;
        size_t trimmed = 0;
        const std::string actual = MessageProtocol::buildSyncMessagesResponseWithinBudget(
            0, "ok", 1, cid, hasMore, messages, 7, &anchor, keepTail, budget, trimmed);

        CHECK(actual == expected);
        CHECK(trimmed == 7);
        CHECK(hasMore == true);
        CHECK(refHasMore == true);
        CHECK(msgidsOf(messages) == refIds);
        REQUIRE(messages.size() == 3);
        CHECK(messages.front().msgid == (keepTail ? 8 : 1));
    }

    //搜索消息的调用方式：不带已读位点与锚点（取头文件里的默认值）
    std::vector<int64_t> refIds;
    bool refHasMore = false;
    const std::string expected = buildSyncReference(base, false, cid, MessageProtocol::kPeerReadMsgidOmitted,
                                                    NULL, true, budget, refIds, refHasMore);
    std::vector<MessageRecord> messages = base;
    bool hasMore = false;
    size_t trimmed = 0;
    const std::string actual = MessageProtocol::buildSyncMessagesResponseWithinBudget(
        0, "ok", 1, cid, hasMore, messages, MessageProtocol::kPeerReadMsgidOmitted, NULL, true, budget,
        trimmed);
    CHECK(actual == expected);
    CHECK(msgidsOf(messages) == refIds);
}

TEST_CASE("SB3 整个应答超过预算、消息体之和没超过：不截断，返回第一次组装的结果")
{
    const int64_t cid = 100002;
    const std::vector<MessageRecord> base = makeSizedRecords(10, 1000);
    const std::string full = MessageProtocol::buildSyncMessagesResponse(0, "ok", 1, cid, false, base);

    //恰好等于整个应答的长度：走「没超过预算」那条路径
    {
        std::vector<MessageRecord> messages = base;
        bool hasMore = false;
        size_t trimmed = 99;
        const std::string actual = MessageProtocol::buildSyncMessagesResponseWithinBudget(
            0, "ok", 1, cid, hasMore, messages, MessageProtocol::kPeerReadMsgidOmitted, NULL, true,
            full.size(), trimmed);
        CHECK(actual == full);
        CHECK(trimmed == 0);
        CHECK(hasMore == false);
    }

    //比整个应答少一个字节：应答超过了预算，但外层字段与分隔逗号不计入，逐条累计仍在预算之内。
    //先用原截断函数确认这个前提成立，再看新函数是否同样不截断、不改 hasmore
    const size_t budget = full.size() - 1;
    std::vector<MessageRecord> probe = base;
    REQUIRE(MessageProtocol::trimMessagesToBudget(probe, cid, true, budget) == 0);

    std::vector<MessageRecord> messages = base;
    bool hasMore = false;
    size_t trimmed = 99;
    const std::string actual = MessageProtocol::buildSyncMessagesResponseWithinBudget(
        0, "ok", 1, cid, hasMore, messages, MessageProtocol::kPeerReadMsgidOmitted, NULL, true, budget,
        trimmed);
    CHECK(actual == full);
    CHECK(trimmed == 0);
    CHECK(hasMore == false);
    CHECK(messages.size() == 10);

    //只有一条且单条就超预算：原做法保留这一条、不置 hasmore，新函数相同
    std::vector<MessageRecord> single = makeSizedRecords(1, 1000);
    std::vector<int64_t> refIds;
    bool refHasMore = false;
    const std::string expected = buildSyncReference(single, false, cid, MessageProtocol::kPeerReadMsgidOmitted,
                                                    NULL, true, 100, refIds, refHasMore);
    hasMore = false;
    const std::string singleActual = MessageProtocol::buildSyncMessagesResponseWithinBudget(
        0, "ok", 1, cid, hasMore, single, MessageProtocol::kPeerReadMsgidOmitted, NULL, true, 100, trimmed);
    CHECK(singleActual == expected);
    CHECK(trimmed == 0);
    CHECK(hasMore == refHasMore);
    CHECK(single.size() == 1);
}

TEST_CASE("SB4 Pin 列表：没超过与超过预算两种情况都与原做法相同，pins 同步截短")
{
    const int64_t cid = 900001;
    const int64_t total = 12;
    const std::vector<MessageRecord> base = makeSizedRecords(10, 1000);
    const size_t one = MessageProtocol::buildMessageNotify(base[0], cid).size();
    const size_t budgets[] = { RESPONSE_MESSAGES_BUDGET_BYTES, one * 3 + one / 2 };
    const size_t expectedKept[] = { 10, 3 };

    for (size_t b = 0; b < sizeof(budgets) / sizeof(budgets[0]); ++b)
    {
        CAPTURE(budgets[b]);
        //pins 比 messages 多两条：原做法在组装前截到与 messages 一样长
        const std::vector<PinRecord> basePins = makePinsFor(base, 2);

        std::vector<int64_t> refIds;
        size_t refPinCount = 0;
        const std::string expected = buildPinnedReference(base, basePins, cid, total, budgets[b], refIds,
                                                          refPinCount);

        std::vector<MessageRecord> messages = base;
        std::vector<PinRecord> pins = basePins;
        const std::string actual = MessageProtocol::buildGetPinnedMessagesResponseWithinBudget(
            0, "ok", 2, cid, total, messages, pins, budgets[b]);

        CHECK(actual == expected);
        CHECK(msgidsOf(messages) == refIds);
        CHECK(messages.size() == expectedKept[b]);
        CHECK(pins.size() == refPinCount);
        CHECK(pins.size() == messages.size());
    }
}

// ---- MSG-S：普通消息的内容体积上限 ----

//把 count 个"张"（U+5F20）拼成 UTF-8 串
static std::string repeatZhang(size_t count)
{
    std::string s;
    s.reserve(count * 3);
    for (size_t i = 0; i < count; ++i)
        s += u8"张";
    return s;
}

TEST_CASE("MSG-S1 单条消息内容的体积上限：边界与口径")
{
    //正好等于上限放行，多一个字节拒绝
    CHECK_FALSE(MessageProtocol::isMessageContentTooLarge(std::string(MESSAGE_CONTENT_MAX_BYTES, 'x'), ""));
    CHECK(MessageProtocol::isMessageContentTooLarge(std::string(MESSAGE_CONTENT_MAX_BYTES + 1, 'x'), ""));
    //普通的短消息
    CHECK_FALSE(MessageProtocol::isMessageContentTooLarge("{\"text\":\"hello\"}", ""));
    //与合并记录同一口径
    CHECK(MESSAGE_CONTENT_MAX_BYTES == FORWARD_MERGED_CONTENT_MAX_BYTES);
}

TEST_CASE("MSG-S2 content 与 quote 合计计算")
{
    const size_t half = MESSAGE_CONTENT_MAX_BYTES / 2;
    //合计正好等于上限：放行
    CHECK_FALSE(MessageProtocol::isMessageContentTooLarge(std::string(half, 'c'),
                                                          std::string(MESSAGE_CONTENT_MAX_BYTES - half, 'q')));
    //合计多一个字节：拒绝（单看任何一项都没超）
    CHECK(MessageProtocol::isMessageContentTooLarge(std::string(half, 'c'),
                                                    std::string(MESSAGE_CONTENT_MAX_BYTES - half + 1, 'q')));
    //只有 quote 超限也要拒绝
    CHECK(MessageProtocol::isMessageContentTooLarge("{\"text\":\"hi\"}",
                                                    std::string(MESSAGE_CONTENT_MAX_BYTES + 1, 'q')));
}

TEST_CASE("MSG-S3 中文按服务端重新序列化后的字节数计量")
{
    //请求里的中文是原始 UTF-8（每字 3 字节），服务端解析后重新序列化，jsoncpp 把它写成
    //\uXXXX（每字 6 字节）。上限按重新序列化后的结果计，与推送帧里实际占的字节数一致。
    const size_t kBytesPerEscapedChar = 6;
    const std::string emptyContent = "{\"text\":\"\"}";
    const size_t count = (MESSAGE_CONTENT_MAX_BYTES - emptyContent.size()) / kBytesPerEscapedChar + 1;

    const std::string json = std::string("{\"clientmsgid\":\"c-zh\",\"ctype\":1,\"cid\":\"100002\",\"msgtype\":1,")
                           + "\"content\":{\"text\":\"" + repeatZhang(count) + "\"}}";
    SendMessageRequest req;
    REQUIRE(MessageProtocol::parseSendMessageRequest(json, req));

    //重新序列化后每个汉字占 6 字节
    CHECK(req.content.size() == emptyContent.size() + count * kBytesPerEscapedChar);
    //按原始 UTF-8 算只有上限的一半左右，按重新序列化的结果算则已超限
    CHECK(count * 3 < MESSAGE_CONTENT_MAX_BYTES);
    CHECK(MessageProtocol::isMessageContentTooLarge(req.content, req.quote));

    //少一个字就回到上限以内
    const std::string json2 = std::string("{\"clientmsgid\":\"c-zh2\",\"ctype\":1,\"cid\":\"100002\",\"msgtype\":1,")
                            + "\"content\":{\"text\":\"" + repeatZhang(count - 1) + "\"}}";
    SendMessageRequest req2;
    REQUIRE(MessageProtocol::parseSendMessageRequest(json2, req2));
    CHECK_FALSE(MessageProtocol::isMessageContentTooLarge(req2.content, req2.quote));
}

TEST_CASE("MSG-S4 正好在上限的消息组成推送帧后仍小于单帧上限")
{
    //content 与 quote 各占一半、合计正好等于上限：这是能通过检查的最大消息
    const size_t half = MESSAGE_CONTENT_MAX_BYTES / 2;
    const std::string prefix = "{\"text\":\"";
    const std::string suffix = "\"}";
    MessageRecord rec = makeSizedRecord(1, half - prefix.size() - suffix.size());
    const std::string quotePrefix = "{\"digest\":\"";
    const std::string quoteSuffix = "\"}";
    rec.quote = quotePrefix
              + std::string(MESSAGE_CONTENT_MAX_BYTES - half - quotePrefix.size() - quoteSuffix.size(), 'q')
              + quoteSuffix;
    REQUIRE(rec.content.size() + rec.quote.size() == MESSAGE_CONTENT_MAX_BYTES);
    REQUIRE_FALSE(MessageProtocol::isMessageContentTooLarge(rec.content, rec.quote));

    //推送给对方的帧原始长度必须小于客户端能收下的单帧上限，否则对方断线
    const std::string notify = MessageProtocol::buildMessageNotify(rec, 100001);
    CHECK(notify.size() < MAX_BODY_CHATSERVER);
    //同步页按体积截断时单条至少保留一条：一条最大消息本身也要放得进同步预算
    CHECK(notify.size() < RESPONSE_MESSAGES_BUDGET_BYTES);
}

TEST_CASE("MSG-S5 逐条转发复制出的内容按同一上限检查")
{
    //上限生效之前存进库的超大文字消息，经逐条转发重新组装后仍超限，应被拒绝
    const std::string hugeStored = "{\"text\":\"" + std::string(MESSAGE_CONTENT_MAX_BYTES, 'a') + "\"}";
    const std::string forwardedHuge = MessageProtocol::buildForwardedContent(1, hugeStored);
    CHECK(MessageProtocol::isMessageContentTooLarge(forwardedHuge, ""));

    //普通长度的消息转发后照常放行
    const std::string forwardedSmall = MessageProtocol::buildForwardedContent(1, "{\"text\":\"hello\"}");
    CHECK_FALSE(MessageProtocol::isMessageContentTooLarge(forwardedSmall, ""));
}

TEST_CASE("FWD-S5 合并转发的子消息保留原 content 里的 at 与 quote")
{
    MessageRecord m;
    m.msgid = 601;
    m.from = 100001;
    m.to = 900001;
    m.ctype = 2;
    m.msgtype = 1;
    m.content = "{\"text\":\"@张三 看这里\","
                "\"at\":{\"all\":false,\"users\":[{\"uid\":\"100003\",\"off\":0,\"len\":7}]},"
                "\"quote\":{\"msgid\":\"501\",\"from\":\"100003\",\"msgtype\":1,\"digest\":\"原文\"}}";
    m.timestamp = 1716268800000LL;
    std::vector<MessageRecord> subs;
    subs.push_back(m);

    Json::Value root;
    REQUIRE(parseJson(MessageProtocol::buildMergedContent("t", subs), root));
    REQUIRE(root["messages"].size() == 1);
    const Json::Value& content = root["messages"][0]["content"];
    CHECK(content.isMember("at"));
    CHECK(content.isMember("quote"));
    CHECK(content["quote"]["msgid"].asString() == "501");
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

// =====================================================================
// S29~S35 —— 按时间范围清理聊天记录（cmd_delete_messages_by_time 1521、
// 推送 cmd_messages_deleted_notify 1522）与同步应答回带锚点，见
// docs/清理聊天记录方案.md §4
// =====================================================================

//S29~S34 用例使用的区间端点（Unix毫秒）。取两个互不相同的固定值，便于核对取值原样。
static const int64_t S_RANGE_BEGIN = 1754300000000LL;
static const int64_t S_RANGE_END   = 1754399999999LL;

TEST_CASE("S29 parseDeleteMessagesByTimeRequest 两个字段都是非负整数时原样取出")
{
    DeleteMessagesByTimeRequest req;
    REQUIRE(MessageProtocol::parseDeleteMessagesByTimeRequest(
        "{\"begintime\":1754300000000,\"endtime\":1754399999999}", req));
    CHECK(req.begintime == S_RANGE_BEGIN);
    CHECK(req.endtime == S_RANGE_END);
}

TEST_CASE("S30 parseDeleteMessagesByTimeRequest begintime 为 0 表示从最早开始")
{
    DeleteMessagesByTimeRequest req;
    REQUIRE(MessageProtocol::parseDeleteMessagesByTimeRequest(
        "{\"begintime\":0,\"endtime\":1754399999999}", req));
    CHECK(req.begintime == 0);
    CHECK(req.endtime == S_RANGE_END);
}

TEST_CASE("S31 parseDeleteMessagesByTimeRequest 格式不合法一律拒绝")
{
    const char* const kBad[] =
    {
        "{\"endtime\":1754399999999}",                         //缺 begintime
        "{\"begintime\":0}",                                   //缺 endtime
        "{\"begintime\":\"0\",\"endtime\":1754399999999}",     //begintime 是字符串
        "{\"begintime\":0,\"endtime\":\"1754399999999\"}",     //endtime 是字符串
        "{\"begintime\":0.5,\"endtime\":1754399999999}",       //不是整数
        "{\"begintime\":-1,\"endtime\":1754399999999}",        //开始为负
        "{\"begintime\":0,\"endtime\":-1}",                    //结束为负
        "{\"begintime\":0,\"endtime\":1754399999999",          //JSON 不完整
        "[0,1754399999999]",                                   //不是对象
        ""                                                     //空串
    };
    for (size_t i = 0; i < sizeof(kBad) / sizeof(kBad[0]); ++i)
    {
        INFO("case #" << i << ": " << kBad[i]);
        DeleteMessagesByTimeRequest req;
        CHECK_FALSE(MessageProtocol::parseDeleteMessagesByTimeRequest(kBad[i], req));
    }
}

TEST_CASE("S32 buildDeleteMessagesByTimeResponse 四个字段齐全，时间为数字")
{
    Json::Value root;
    REQUIRE(parseJson(MessageProtocol::buildDeleteMessagesByTimeResponse(
        0, "ok", 0, S_RANGE_END), root));
    CHECK(root["code"].asInt() == 0);
    CHECK(root["msg"].asString() == "ok");
    //时间与消息体的 timestamp 同一口径，按数字输出（毫秒值远小于 2^53，不会丢精度）
    REQUIRE(root["begintime"].isInt64());
    REQUIRE(root["endtime"].isInt64());
    CHECK(root["begintime"].asInt64() == 0);
    CHECK(root["endtime"].asInt64() == S_RANGE_END);
}

TEST_CASE("S33 buildMessagesDeletedNotify 单条形式：ctype、cid、msgids，不带时间")
{
    std::vector<int64_t> msgids;
    msgids.push_back(9007199254741001LL);   //超过 2^53，按数字输出会丢精度
    Json::Value root;
    REQUIRE(parseJson(MessageProtocol::buildMessagesDeletedNotify(2, 268435456, msgids), root));
    CHECK(root["ctype"].asInt() == 2);
    CHECK(root["cid"].asString() == "268435456");
    REQUIRE(root["msgids"].isArray());
    REQUIRE(root["msgids"].size() == 1);
    CHECK(root["msgids"][0].asString() == "9007199254741001");
    CHECK_FALSE(root.isMember("begintime"));
    CHECK_FALSE(root.isMember("endtime"));
}

TEST_CASE("S34 buildMessagesDeletedRangeNotify 区间形式：只有 begintime、endtime")
{
    Json::Value root;
    REQUIRE(parseJson(MessageProtocol::buildMessagesDeletedRangeNotify(S_RANGE_BEGIN, S_RANGE_END),
                      root));
    REQUIRE(root["begintime"].isInt64());
    REQUIRE(root["endtime"].isInt64());
    CHECK(root["begintime"].asInt64() == S_RANGE_BEGIN);
    CHECK(root["endtime"].asInt64() == S_RANGE_END);
    //客户端以是否带 msgids 区分两种形式
    CHECK_FALSE(root.isMember("msgids"));
    CHECK_FALSE(root.isMember("ctype"));
    CHECK_FALSE(root.isMember("cid"));
}

TEST_CASE("S35 buildSyncMessagesResponse 原样回带请求锚点")
{
    const std::vector<MessageRecord> none;

    //翻历史模式、从最新开始：回带 beforemsgid "0"
    {
        SyncMessagesRequest req;
        req.ctype = 1;
        req.cid = 100001;
        req.beforemsgid = 0;
        Json::Value root;
        REQUIRE(parseJson(MessageProtocol::buildSyncMessagesResponse(
            0, "ok", 1, 100001, false, none, 0, &req), root));
        CHECK(root["beforemsgid"].asString() == "0");
        CHECK_FALSE(root.isMember("aftermsgid"));
    }

    //翻历史模式、锚点非 0：原样回带，按字符串输出
    {
        SyncMessagesRequest req;
        req.ctype = 2;
        req.cid = 268435456;
        req.beforemsgid = 9007199254741001LL;
        Json::Value root;
        REQUIRE(parseJson(MessageProtocol::buildSyncMessagesResponse(
            0, "ok", 2, 268435456, true, none,
            MessageProtocol::kPeerReadMsgidOmitted, &req), root));
        CHECK(root["beforemsgid"].asString() == "9007199254741001");
        CHECK_FALSE(root.isMember("aftermsgid"));
    }

    //补齐模式：回带 aftermsgid，不带 beforemsgid
    {
        SyncMessagesRequest req;
        req.ctype = 1;
        req.cid = 100001;
        req.isAfterMode = true;
        req.aftermsgid = 501;
        Json::Value root;
        REQUIRE(parseJson(MessageProtocol::buildSyncMessagesResponse(
            0, "ok", 1, 100001, false, none, 0, &req), root));
        CHECK(root["aftermsgid"].asString() == "501");
        CHECK_FALSE(root.isMember("beforemsgid"));
    }

    //不传请求（搜索应答复用本结构时就是这样）：两者都不输出
    {
        Json::Value root;
        REQUIRE(parseJson(MessageProtocol::buildSyncMessagesResponse(
            0, "ok", 1, 100001, false, none), root));
        CHECK_FALSE(root.isMember("beforemsgid"));
        CHECK_FALSE(root.isMember("aftermsgid"));
    }
}

//CF 组用例使用的文件ID：取自 2026-09-30 实测时服务端分配的真实值
static const int64_t CF_FILE_ID = 117;

TEST_CASE("CF1 图片、文件、语音消息的 content 带 fileid 时能取出")
{
    int64_t fileid = 0;

    //图片：字段与客户端 SendChatImageMessage 发出的一致
    CHECK(MessageProtocol::extractContentFileId(message_type_image,
        "{\"fileid\":\"117\",\"height\":870,\"md5\":\"e17efa5d322c5215671cc31bf668ac0e\","
        "\"size\":210616,\"width\":1408}", fileid));
    CHECK(fileid == CF_FILE_ID);

    //文件
    fileid = 0;
    CHECK(MessageProtocol::extractContentFileId(message_type_file,
        "{\"fileid\":\"117\",\"md5\":\"abc\",\"name\":\"PRD-v3.2.docx\",\"size\":90112,\"ext\":\"docx\"}",
        fileid));
    CHECK(fileid == CF_FILE_ID);

    //语音
    fileid = 0;
    CHECK(MessageProtocol::extractContentFileId(message_type_voice,
        "{\"fileid\":\"117\",\"md5\":\"abc\",\"duration\":8,\"size\":6144}", fileid));
    CHECK(fileid == CF_FILE_ID);
}

TEST_CASE("CF2 文本消息不解析，content 里出现 fileid 字样也不取")
{
    //文本消息不引用任何文件；即便 content 里恰好有同名字段，也不应据此去改文件记录
    int64_t fileid = 0;
    CHECK_FALSE(MessageProtocol::extractContentFileId(message_type_text,
        "{\"text\":\"hello\",\"fileid\":\"117\"}", fileid));
}

TEST_CASE("CF3 大表情只带 emoji、没有 fileid 时返回未取到")
{
    //大表情有两种形态：内置表情只有 emoji 字段，自定义表情才带 fileid
    int64_t fileid = 0;
    CHECK_FALSE(MessageProtocol::extractContentFileId(message_type_emoji,
        "{\"emoji\":\"rose\"}", fileid));
}

TEST_CASE("CF4 fileid 不是正整数时返回未取到")
{
    int64_t fileid = 0;
    //协议文档示例里的占位写法，不是服务端真实分配的形态
    CHECK_FALSE(MessageProtocol::extractContentFileId(message_type_image,
        "{\"fileid\":\"img_8f3c\"}", fileid));
    CHECK_FALSE(MessageProtocol::extractContentFileId(message_type_image,
        "{\"fileid\":\"0\"}", fileid));
    CHECK_FALSE(MessageProtocol::extractContentFileId(message_type_image,
        "{\"fileid\":\"-5\"}", fileid));
    //协议约定 64 位 ID 一律编码为字符串，数值形态不认
    CHECK_FALSE(MessageProtocol::extractContentFileId(message_type_image,
        "{\"fileid\":117}", fileid));
}

TEST_CASE("CF5 content 不是合法 JSON 时返回未取到")
{
    int64_t fileid = 0;
    CHECK_FALSE(MessageProtocol::extractContentFileId(message_type_image, "{\"fileid\":", fileid));
    CHECK_FALSE(MessageProtocol::extractContentFileId(message_type_image, "", fileid));
    CHECK_FALSE(MessageProtocol::extractContentFileId(message_type_image, "[\"117\"]", fileid));
}

// =====================================================================
// S36-S39：cmd_clear_conversation（1523）按会话本人视角清空聊天记录（2026-10-02）
// =====================================================================

TEST_CASE("S36 parseClearConversationRequest 取出会话类型与会话ID")
{
    ClearConversationRequest req;
    REQUIRE(MessageProtocol::parseClearConversationRequest("{\"ctype\":2,\"cid\":\"268435456\"}", req));
    CHECK(req.ctype == 2);
    CHECK(req.cid == 268435456LL);

    REQUIRE(MessageProtocol::parseClearConversationRequest("{\"ctype\":1,\"cid\":\"100002\"}", req));
    CHECK(req.ctype == 1);
    CHECK(req.cid == 100002LL);
}

TEST_CASE("S37 parseClearConversationRequest 格式不合法一律拒绝")
{
    ClearConversationRequest req;
    //会话类型只能是 1 单聊、2 群聊；缺了也不行（不默认成单聊，免得清错会话）
    CHECK_FALSE(MessageProtocol::parseClearConversationRequest("{\"ctype\":3,\"cid\":\"100002\"}", req));
    CHECK_FALSE(MessageProtocol::parseClearConversationRequest("{\"cid\":\"100002\"}", req));
    //会话ID须为正
    CHECK_FALSE(MessageProtocol::parseClearConversationRequest("{\"ctype\":1,\"cid\":\"0\"}", req));
    CHECK_FALSE(MessageProtocol::parseClearConversationRequest("{\"ctype\":1}", req));
    CHECK_FALSE(MessageProtocol::parseClearConversationRequest("not json", req));
    CHECK_FALSE(MessageProtocol::parseClearConversationRequest("[1,2]", req));
}

TEST_CASE("S38 buildClearConversationResponse 带回会话与生效的锚点，ID 按字符串输出")
{
    Json::Value root;
    REQUIRE(parseJson(MessageProtocol::buildClearConversationResponse(
        0, "ok", 2, 268435456, 9007199254741001LL), root));
    CHECK(root["code"].asInt() == 0);
    CHECK(root["msg"].asString() == "ok");
    CHECK(root["ctype"].asInt() == 2);
    CHECK(root["cid"].asString() == "268435456");
    //msgid 可能超过 2^53，与其它 ID 一样按字符串输出
    CHECK(root["clearedmsgid"].asString() == "9007199254741001");
}

TEST_CASE("S39 buildMessagesClearedNotify 清空形式：ctype、cid、clearedmsgid，不带 msgids 与时间")
{
    Json::Value root;
    REQUIRE(parseJson(MessageProtocol::buildMessagesClearedNotify(1, 100002, 1234), root));
    CHECK(root["ctype"].asInt() == 1);
    CHECK(root["cid"].asString() == "100002");
    CHECK(root["clearedmsgid"].asString() == "1234");
    //客户端以字段区分三种形式：msgids 为单条形式，begintime / endtime 为区间形式，clearedmsgid 为清空形式
    CHECK_FALSE(root.isMember("msgids"));
    CHECK_FALSE(root.isMember("begintime"));
    CHECK_FALSE(root.isMember("endtime"));
}

// ============================================================================
// 企业级敏感词：发送被拒的回执与取文字规则（SW-P1～SW-P6，docs/敏感词方案.md 第 6.3.1、6.4 节）
// ============================================================================

TEST_CASE("SW-P1 发送被拒的回执带回 clientmsgid，msgid 为 \"0\"")
{
    Json::Value root;
    REQUIRE(parseJson(MessageProtocol::buildSendMessageResponse(error_code_sensitive_word,
                                                                "message contains sensitive word",
                                                                "c-507", 0, 0), root));
    CHECK(root["code"].asInt() == error_code_sensitive_word);
    //客户端据 clientmsgid 找到被拒的那一条气泡并标为失败
    CHECK(root["clientmsgid"].asString() == "c-507");
    CHECK(root["msgid"].asString() == "0");
    CHECK(root["timestamp"].asInt64() == 0);
}

TEST_CASE("SW-P2 文字消息：取正文与两处引用的 digest，不取引用人名字，空串不取")
{
    std::vector<std::string> out;
    //调用前已有的内容保留，本函数只追加
    out.push_back(u8"既有");
    MessageProtocol::collectSensitiveTexts(1,
        u8"{\"text\":\"正文\",\"quote\":{\"msgid\":\"1\",\"name\":\"引用人甲\",\"digest\":\"内嵌引用\"}}",
        u8"{\"msgid\":\"1\",\"from\":\"100001\",\"name\":\"引用人乙\",\"digest\":\"顶层引用\"}",
        out);
    REQUIRE(out.size() == 4);
    CHECK(out[0] == std::string(u8"既有"));
    CHECK(out[1] == std::string(u8"正文"));
    CHECK(out[2] == std::string(u8"内嵌引用"));
    CHECK(out[3] == std::string(u8"顶层引用"));

    //正文为空、引用的 digest 为空、没有顶层引用：一段都不取
    std::vector<std::string> none;
    MessageProtocol::collectSensitiveTexts(1, "{\"text\":\"\",\"quote\":{\"msgid\":\"1\",\"digest\":\"\"}}",
                                           "", none);
    CHECK(none.empty());
}

TEST_CASE("SW-P3 文件消息只取文件名；图片、语音、表情、位置、名片、抖一抖什么都不取")
{
    std::vector<std::string> out;
    MessageProtocol::collectSensitiveTexts(3,
        u8"{\"fileid\":\"f1\",\"md5\":\"m\",\"name\":\"季度报表.xlsx\",\"size\":1,\"ext\":\"xlsx\"}", "", out);
    REQUIRE(out.size() == 1);
    CHECK(out[0] == std::string(u8"季度报表.xlsx"));

    std::vector<std::string> none;
    MessageProtocol::collectSensitiveTexts(2, "{\"fileid\":\"1\",\"md5\":\"m\",\"width\":1,\"height\":1,\"size\":1}",
                                           "", none);
    MessageProtocol::collectSensitiveTexts(4, "{\"fileid\":\"v\",\"md5\":\"m\",\"duration\":3,\"size\":1}", "", none);
    MessageProtocol::collectSensitiveTexts(6, u8"{\"emoji\":\"表情\"}", "", none);
    MessageProtocol::collectSensitiveTexts(7,
        u8"{\"lat\":31.2,\"lng\":121.4,\"title\":\"地点名\",\"address\":\"详细地址\"}", "", none);
    MessageProtocol::collectSensitiveTexts(8, u8"{\"cardtype\":1,\"targetid\":\"100002\",\"name\":\"联系人\"}",
                                           "", none);
    MessageProtocol::collectSensitiveTexts(13, "{}", "", none);
    CHECK(none.empty());
}

TEST_CASE("SW-P4 卡片：投票取标题与字符串选项，接龙取标题与说明，待办取标题")
{
    std::vector<std::string> poll;
    //第二个选项不是字符串：跳过，其余选项照常取
    MessageProtocol::collectSensitiveTexts(10,
        u8"{\"title\":\"投票标题\",\"options\":[\"选项一\",2,\"选项二\"],\"multi\":false,"
        u8"\"anonymous\":true,\"expiretime\":0}", "", poll);
    REQUIRE(poll.size() == 3);
    CHECK(poll[0] == std::string(u8"投票标题"));
    CHECK(poll[1] == std::string(u8"选项一"));
    CHECK(poll[2] == std::string(u8"选项二"));

    std::vector<std::string> relay;
    MessageProtocol::collectSensitiveTexts(11,
        u8"{\"title\":\"接龙标题\",\"note\":\"接龙说明\",\"deadline\":0}", "", relay);
    REQUIRE(relay.size() == 2);
    CHECK(relay[0] == std::string(u8"接龙标题"));
    CHECK(relay[1] == std::string(u8"接龙说明"));

    std::vector<std::string> todo;
    MessageProtocol::collectSensitiveTexts(12,
        u8"{\"title\":\"待办标题\",\"assignees\":[\"100000\",\"100002\"],\"duetime\":0,\"done\":false}", "", todo);
    REQUIRE(todo.size() == 1);
    CHECK(todo[0] == std::string(u8"待办标题"));
}

TEST_CASE("SW-P5 合并记录：标题与各子消息逐条按规则取，嵌套记录向里取，不取 fromname")
{
    std::vector<std::string> out;
    MessageProtocol::collectSensitiveTexts(15,
        u8"{\"title\":\"外层标题\",\"messages\":["
        u8"{\"msgtype\":1,\"content\":{\"text\":\"子一\",\"quote\":{\"digest\":\"子一内嵌引用\"}},"
        u8"\"quote\":{\"msgid\":\"9\",\"digest\":\"子一引用\"},\"fromname\":\"发送人甲\"},"
        u8"{\"msgtype\":3,\"content\":{\"fileid\":\"f\",\"name\":\"子二文件.pdf\"},\"fromname\":\"发送人乙\"},"
        u8"{\"msgtype\":2,\"content\":{\"fileid\":\"i\"}},"
        u8"{\"msgtype\":15,\"content\":{\"title\":\"内层标题\",\"messages\":["
        u8"{\"msgtype\":1,\"content\":{\"text\":\"子三\"}}]}}]}",
        "", out);
    REQUIRE(out.size() == 7);
    CHECK(out[0] == std::string(u8"外层标题"));
    CHECK(out[1] == std::string(u8"子一"));
    CHECK(out[2] == std::string(u8"子一内嵌引用"));
    CHECK(out[3] == std::string(u8"子一引用"));
    CHECK(out[4] == std::string(u8"子二文件.pdf"));
    CHECK(out[5] == std::string(u8"内层标题"));
    CHECK(out[6] == std::string(u8"子三"));
}

TEST_CASE("SW-P7 代码片段的批注说明一并取；notes 或条目格式不对时跳过；合并记录里的批注同样取")
{
    //正文之后依次取各条批注的说明；from / to 不影响
    std::vector<std::string> out;
    MessageProtocol::collectSensitiveTexts(1,
        u8"{\"text\":\"int a;\",\"format\":\"code\",\"lang\":\"cpp\","
        u8"\"notes\":[{\"from\":1,\"to\":1,\"text\":\"批注一\"},{\"from\":3,\"to\":4,\"text\":\"批注二\"}]}",
        "", out);
    REQUIRE(out.size() == 3);
    CHECK(out[0] == std::string(u8"int a;"));
    CHECK(out[1] == std::string(u8"批注一"));
    CHECK(out[2] == std::string(u8"批注二"));

    //notes 不是数组、条目不是对象、text 不是字符串或为空：跳过，正文照常取
    std::vector<std::string> textOnly;
    MessageProtocol::collectSensitiveTexts(1, u8"{\"text\":\"x\",\"notes\":{\"text\":\"不是数组\"}}", "", textOnly);
    MessageProtocol::collectSensitiveTexts(1,
        u8"{\"text\":\"y\",\"notes\":[\"字符串条目\",{\"text\":5},{\"text\":\"\"}]}", "", textOnly);
    REQUIRE(textOnly.size() == 2);
    CHECK(textOnly[0] == std::string(u8"x"));
    CHECK(textOnly[1] == std::string(u8"y"));

    //合并转发记录里的代码片段：批注同样取
    std::vector<std::string> merged;
    MessageProtocol::collectSensitiveTexts(15,
        u8"{\"title\":\"标题\",\"messages\":[{\"msgtype\":1,\"content\":{\"text\":\"代码\",\"format\":\"code\","
        u8"\"notes\":[{\"from\":1,\"to\":1,\"text\":\"子消息批注\"}]}}]}",
        "", merged);
    REQUIRE(merged.size() == 3);
    CHECK(merged[0] == std::string(u8"标题"));
    CHECK(merged[1] == std::string(u8"代码"));
    CHECK(merged[2] == std::string(u8"子消息批注"));
}

TEST_CASE("SW-P6 内容不合法、类型未知、子消息残缺：不崩溃，能取的照常取")
{
    std::vector<std::string> none;
    //content 不是合法 JSON、不是对象
    MessageProtocol::collectSensitiveTexts(1, "{\"text\":", "", none);
    MessageProtocol::collectSensitiveTexts(1, "[\"a\",\"b\"]", "", none);
    MessageProtocol::collectSensitiveTexts(1, "\"text\"", "", none);
    //未知的消息类型
    MessageProtocol::collectSensitiveTexts(99, "{\"text\":\"x\",\"title\":\"y\",\"name\":\"z\"}", "", none);
    //字段类型不对：text 是数字、options 不是数组
    MessageProtocol::collectSensitiveTexts(1, "{\"text\":123}", "", none);
    MessageProtocol::collectSensitiveTexts(10, "{\"title\":7,\"options\":\"a\"}", "", none);
    CHECK(none.empty());

    //quote 不是合法 JSON：忽略 quote，正文照常取
    std::vector<std::string> textOnly;
    MessageProtocol::collectSensitiveTexts(1, u8"{\"text\":\"正文\"}", "{\"digest\":", textOnly);
    REQUIRE(textOnly.size() == 1);
    CHECK(textOnly[0] == std::string(u8"正文"));

    //合并记录里缺 msgtype、msgtype 不是整数、content 不是对象的子消息跳过，其余照常取
    std::vector<std::string> merged;
    MessageProtocol::collectSensitiveTexts(15,
        u8"{\"title\":\"\",\"messages\":["
        u8"{\"content\":{\"text\":\"缺类型\"}},"
        u8"{\"msgtype\":\"1\",\"content\":{\"text\":\"类型是字符串\"}},"
        u8"{\"msgtype\":1,\"content\":\"不是对象\"},"
        u8"5,"
        u8"{\"msgtype\":1,\"content\":{\"text\":\"正常子消息\"}}]}",
        "", merged);
    REQUIRE(merged.size() == 1);
    CHECK(merged[0] == std::string(u8"正常子消息"));
}

TEST_CASE("CVR1 会话列表应答：已撤回的最后一条带 recalled、摘要不含原文，其余会话照旧")
{
    std::vector<ConversationBrief> items;
    ConversationBrief recalled;
    recalled.ctype         = 2;
    recalled.cid           = 900001;
    recalled.lastmsgid     = 12;
    recalled.lastfrom      = 100001;
    recalled.lastmsgtype   = 1;
    recalled.lastcontent   = "{\"text\":\"secret text\"}";
    recalled.lasttimestamp = 1700000000000LL;
    recalled.lastrecalled  = true;
    items.push_back(recalled);

    ConversationBrief normal = recalled;
    normal.cid          = 900002;
    normal.lastcontent  = "{\"text\":\"hello\"}";
    normal.lastrecalled = false;
    items.push_back(normal);

    const std::string body = MessageProtocol::buildSyncConversationsResponse(0, "ok", 0, items);
    //原文一个字都不能出现在应答里
    CHECK(body.find("secret text") == std::string::npos);

    Json::Value root;
    REQUIRE(parseJson(body, root));
    REQUIRE(root["conversations"].isArray());
    REQUIRE(root["conversations"].size() == 2);
    const Json::Value& lastRecalled = root["conversations"][0]["lastmsg"];
    CHECK(lastRecalled["recalled"].isBool());
    CHECK(lastRecalled["recalled"].asBool());
    CHECK(lastRecalled["digest"].asString() == std::string(MessageProtocol::kRecalledDigest));
    //发送方、类型、时间照旧，客户端据发送方拼「你 / 某某撤回了一条消息」
    CHECK(lastRecalled["from"].asString() == "100001");
    CHECK(lastRecalled["msgtype"].asInt() == 1);
    CHECK(lastRecalled["timestamp"].asInt64() == 1700000000000LL);

    const Json::Value& lastNormal = root["conversations"][1]["lastmsg"];
    CHECK_FALSE(lastNormal.isMember("recalled"));
    CHECK(lastNormal["digest"].asString() == "hello");
}

TEST_CASE("RBY2 消息体：已撤回且撤回者已知时带 recalledby，未撤回或撤回者未知时不带")
{
    MessageRecord rec;
    rec.msgid      = 12;
    rec.ctype      = 2;
    rec.from       = 100001;
    rec.to         = 900001;
    rec.msgtype    = 1;
    rec.content    = "{\"text\":\"x\"}";
    rec.timestamp  = 1700000000000LL;
    rec.recalled   = true;
    rec.recalledby = 100007;

    //群主 100007 撤回了 100001 发的消息：撤回者与发送方各自照实输出，id 以字符串编码
    Json::Value root;
    REQUIRE(parseJson(MessageProtocol::buildMessageNotify(rec, 900001), root));
    CHECK(root["recalled"].asBool());
    CHECK(root["recalledby"].isString());
    CHECK(root["recalledby"].asString() == "100007");
    CHECK(root["from"].asString() == "100001");

    //2026-10-03 之前撤回的存量数据：撤回者未知（0）时不输出，客户端按发送方处理
    rec.recalledby = 0;
    Json::Value unknown;
    REQUIRE(parseJson(MessageProtocol::buildMessageNotify(rec, 900001), unknown));
    CHECK(unknown["recalled"].asBool());
    CHECK_FALSE(unknown.isMember("recalledby"));

    //没撤回的消息不输出
    rec.recalled = false;
    Json::Value normal;
    REQUIRE(parseJson(MessageProtocol::buildMessageNotify(rec, 900001), normal));
    CHECK_FALSE(normal.isMember("recalledby"));
}

TEST_CASE("CVR2 会话列表应答：已撤回的最后一条带撤回者，撤回者未知时不带该字段")
{
    std::vector<ConversationBrief> items;
    ConversationBrief byAdmin;
    byAdmin.ctype          = 2;
    byAdmin.cid            = 900001;
    byAdmin.lastmsgid      = 12;
    byAdmin.lastfrom       = 100001;
    byAdmin.lastmsgtype    = 1;
    byAdmin.lastcontent    = "{\"text\":\"secret\"}";
    byAdmin.lasttimestamp  = 1700000000000LL;
    byAdmin.lastrecalled   = true;
    byAdmin.lastrecalledby = 100007;
    items.push_back(byAdmin);

    ConversationBrief unknown = byAdmin;
    unknown.cid            = 900002;
    unknown.lastrecalledby = 0;
    items.push_back(unknown);

    Json::Value root;
    REQUIRE(parseJson(MessageProtocol::buildSyncConversationsResponse(0, "ok", 0, items), root));
    REQUIRE(root["conversations"].size() == 2);
    const Json::Value& lastAdmin = root["conversations"][0]["lastmsg"];
    CHECK(lastAdmin["recalled"].asBool());
    CHECK(lastAdmin["recalledby"].asString() == "100007");
    CHECK(lastAdmin["from"].asString() == "100001");

    const Json::Value& lastUnknown = root["conversations"][1]["lastmsg"];
    CHECK(lastUnknown["recalled"].asBool());
    CHECK_FALSE(lastUnknown.isMember("recalledby"));
}

// ---- 多语言第 5 期：系统提示的结构化字段（tipkind / tipargs）与会话列表摘要参数 ----

TEST_CASE("TIP2 转让群主提示的 content：两端昵称按有无分别写进 tipargs")
{
    Json::Value both;
    REQUIRE(parseJson(GroupProtocol::buildOwnerTransferTipContent(u8"甲", u8"乙"), both));
    CHECK(both["text"].asString() == u8"「甲」已将群主转让给「乙」");
    CHECK(both["tipkind"].asString() == "owner_transfer");
    CHECK(both["tipargs"]["from"].asString() == u8"甲");
    CHECK(both["tipargs"]["to"].asString() == u8"乙");

    Json::Value toOnly;
    REQUIRE(parseJson(GroupProtocol::buildOwnerTransferTipContent("", u8"乙"), toOnly));
    CHECK(toOnly["text"].asString() == u8"群主已转让给「乙」");
    CHECK_FALSE(toOnly["tipargs"].isMember("from"));
    CHECK(toOnly["tipargs"]["to"].asString() == u8"乙");

    Json::Value none;
    REQUIRE(parseJson(GroupProtocol::buildOwnerTransferTipContent("", ""), none));
    CHECK(none["text"].asString() == u8"群主已变更");
    CHECK(none["tipargs"].size() == 0);
}

//TIP3（发布群公告提示的 content）随 buildAnnouncementTip / buildAnnouncementTipContent 于 2026-10-05 删除：
//发布群公告改为在群里发群公告卡片，用例见 test_announcecard.cpp（AC-S*）

TEST_CASE("TIP4 入群欢迎语：昵称用顿号连接、跳过空昵称，content 带名单与欢迎词")
{
    std::vector<std::string> names;
    names.push_back(u8"张三");
    names.push_back("");
    names.push_back(u8"李四");
    CHECK(GroupProtocol::buildWelcomeTip(names, u8"欢迎加入本群") == std::string(u8"张三、李四 欢迎加入本群"));
    //没有可用的昵称时只有欢迎词
    std::vector<std::string> empty;
    CHECK(GroupProtocol::buildWelcomeTip(empty, u8"欢迎加入本群") == std::string(u8"欢迎加入本群"));

    Json::Value root;
    REQUIRE(parseJson(GroupProtocol::buildWelcomeTipContent(names, u8"欢迎加入本群"), root));
    CHECK(root["text"].asString() == u8"张三、李四 欢迎加入本群");
    CHECK(root["tipkind"].asString() == "group_welcome");
    REQUIRE(root["tipargs"]["names"].isArray());
    REQUIRE(root["tipargs"]["names"].size() == 2);
    CHECK(root["tipargs"]["names"][0].asString() == u8"张三");
    CHECK(root["tipargs"]["names"][1].asString() == u8"李四");
    CHECK(root["tipargs"]["welcome"].asString() == u8"欢迎加入本群");
}

TEST_CASE("TIP5 屏幕共享结束提示的 content：中文与 formatEndTip 相同，另带时长秒数")
{
    const int64_t kDurationSeconds = 3725;
    Json::Value root;
    REQUIRE(parseJson(ScreenShareRules::buildEndTipContent(kDurationSeconds), root));
    CHECK(root["text"].asString() == ScreenShareRules::formatEndTip(kDurationSeconds));
    CHECK(root["tipkind"].asString() == "screenshare_end");
    CHECK(root["tipargs"]["duration"].asInt64() == kDurationSeconds);

    //负数按 0 处理，与 formatEndTip 一致
    Json::Value negative;
    REQUIRE(parseJson(ScreenShareRules::buildEndTipContent(-5), negative));
    CHECK(negative["tipargs"]["duration"].asInt64() == 0);
    CHECK(negative["text"].asString() == ScreenShareRules::formatEndTip(0));
}

TEST_CASE("TIP6 会话列表 lastmsg：系统提示带 tipkind / tipargs，接龙与文件共享带 digestarg")
{
    std::vector<ConversationBrief> items;
    ConversationBrief tip;
    tip.ctype         = 2;
    tip.cid           = 900001;
    tip.lastmsgid     = 1;
    tip.lastmsgtype   = message_type_system;
    //库里已有的退群提示（2026-10-04 之前落库，此后服务端不再产生）：会话列表照样带 tipkind / tipargs
    tip.lastcontent   = u8"{\"text\":\"「乙」退出了群聊\",\"tipkind\":\"member_leave\",\"tipargs\":{\"nickname\":\"乙\"}}";
    tip.lasttimestamp = 1700000000000LL;
    items.push_back(tip);

    //老服务端落库的系统提示只有 text：不带 tipkind
    ConversationBrief legacy = tip;
    legacy.cid         = 900002;
    legacy.lastcontent = u8"{\"text\":\"「乙」退出了群聊\"}";
    items.push_back(legacy);

    ConversationBrief relay = tip;
    relay.cid         = 900003;
    relay.lastmsgtype = message_type_relay;
    relay.lastcontent = u8"{\"title\":\"周五团建报名\",\"note\":\"\",\"deadline\":0}";
    items.push_back(relay);

    ConversationBrief share = tip;
    share.cid         = 900004;
    share.lastmsgtype = message_type_fileshare;
    share.lastcontent = "{\"shareid\":\"s1\",\"name\":\"Projects\"}";
    items.push_back(share);

    ConversationBrief image = tip;
    image.cid         = 900005;
    image.lastmsgtype = message_type_image;
    image.lastcontent = "{\"url\":\"x\"}";
    items.push_back(image);

    //已撤回的系统提示：不下发原文，也不带 tipkind
    ConversationBrief recalled = tip;
    recalled.cid          = 900006;
    recalled.lastrecalled = true;
    items.push_back(recalled);

    Json::Value root;
    REQUIRE(parseJson(MessageProtocol::buildSyncConversationsResponse(0, "ok", 0, items), root));
    REQUIRE(root["conversations"].size() == 6);

    const Json::Value& lastTip = root["conversations"][0]["lastmsg"];
    CHECK(lastTip["digest"].asString() == u8"「乙」退出了群聊");
    CHECK(lastTip["tipkind"].asString() == "member_leave");
    CHECK(lastTip["tipargs"]["nickname"].asString() == u8"乙");
    CHECK_FALSE(lastTip.isMember("digestarg"));

    const Json::Value& lastLegacy = root["conversations"][1]["lastmsg"];
    CHECK(lastLegacy["digest"].asString() == u8"「乙」退出了群聊");
    CHECK_FALSE(lastLegacy.isMember("tipkind"));
    CHECK_FALSE(lastLegacy.isMember("tipargs"));

    const Json::Value& lastRelay = root["conversations"][2]["lastmsg"];
    CHECK(lastRelay["digest"].asString() == u8"[接龙] 周五团建报名");
    CHECK(lastRelay["digestarg"].asString() == u8"周五团建报名");

    const Json::Value& lastShare = root["conversations"][3]["lastmsg"];
    CHECK(lastShare["digest"].asString() == u8"[共享] Projects");
    CHECK(lastShare["digestarg"].asString() == "Projects");

    const Json::Value& lastImage = root["conversations"][4]["lastmsg"];
    CHECK(lastImage["digest"].asString() == u8"[图片]");
    CHECK_FALSE(lastImage.isMember("digestarg"));
    CHECK_FALSE(lastImage.isMember("tipkind"));

    const Json::Value& lastRecalled = root["conversations"][5]["lastmsg"];
    CHECK(lastRecalled["recalled"].asBool());
    CHECK_FALSE(lastRecalled.isMember("tipkind"));
}

//语音消息测试用的 content：fileid、md5、duration、size 都合格；wave 为空串时不带该字段
static std::string makeVoiceContent(const std::string& fileid, const std::string& md5, const std::string& duration,
                                    const std::string& size, const std::string& wave)
{
    std::string json = "{\"fileid\":" + fileid + ",\"md5\":" + md5 + ",\"duration\":" + duration
        + ",\"size\":" + size;
    if (!wave.empty())
        json += ",\"wave\":" + wave;
    return json + "}";
}

//n 个 0 字节的标准 Base64（带填充），用来构造指定解码长度的 wave
static std::string zeroBytesBase64(size_t n)
{
    const size_t kGroupBytes = 3;
    const size_t kGroupChars = 4;
    std::string out((n / kGroupBytes) * kGroupChars, 'A');
    const size_t rest = n % kGroupBytes;
    if (rest == 1)
        out += "AA==";
    else if (rest == 2)
        out += "AAA=";
    return out;
}

TEST_CASE("VMS1 语音 content：字段齐全且取值合格时通过，带不带 wave 都可以")
{
    CHECK(MessageProtocol::isValidVoiceContent(makeVoiceContent("\"1234567\"", "\"d41d8cd98f00b204e9800998ecf8427e\"",
                                                                "8", "25600", "")));
    CHECK(MessageProtocol::isValidVoiceContent(makeVoiceContent("\"1234567\"", "\"d41d8cd98f00b204e9800998ecf8427e\"",
                                                                "8", "25600", "\"" + zeroBytesBase64(40) + "\"")));
}

TEST_CASE("VMS2 语音 content：fileid、md5、duration、size 缺任何一个或类型不对都拒绝")
{
    const std::string md5 = "\"d41d8cd98f00b204e9800998ecf8427e\"";
    //逐个去掉一个字段
    CHECK_FALSE(MessageProtocol::isValidVoiceContent("{\"md5\":" + md5 + ",\"duration\":8,\"size\":25600}"));
    CHECK_FALSE(MessageProtocol::isValidVoiceContent("{\"fileid\":\"1234567\",\"duration\":8,\"size\":25600}"));
    CHECK_FALSE(MessageProtocol::isValidVoiceContent("{\"fileid\":\"1234567\",\"md5\":" + md5 + ",\"size\":25600}"));
    CHECK_FALSE(MessageProtocol::isValidVoiceContent("{\"fileid\":\"1234567\",\"md5\":" + md5 + ",\"duration\":8}"));
    //类型或取值不对：fileid 不是正整数的十进制字符串、md5 为空、duration 与 size 写成字符串
    CHECK_FALSE(MessageProtocol::isValidVoiceContent(makeVoiceContent("1234567", md5, "8", "25600", "")));
    CHECK_FALSE(MessageProtocol::isValidVoiceContent(makeVoiceContent("\"0\"", md5, "8", "25600", "")));
    CHECK_FALSE(MessageProtocol::isValidVoiceContent(makeVoiceContent("\"voice_1\"", md5, "8", "25600", "")));
    CHECK_FALSE(MessageProtocol::isValidVoiceContent(makeVoiceContent("\"1234567\"", "\"\"", "8", "25600", "")));
    CHECK_FALSE(MessageProtocol::isValidVoiceContent(makeVoiceContent("\"1234567\"", md5, "\"8\"", "25600", "")));
    CHECK_FALSE(MessageProtocol::isValidVoiceContent(makeVoiceContent("\"1234567\"", md5, "8", "\"25600\"", "")));
    //不是 JSON 对象
    CHECK_FALSE(MessageProtocol::isValidVoiceContent("[]"));
    CHECK_FALSE(MessageProtocol::isValidVoiceContent("not json"));
}

TEST_CASE("VMS3 语音 content：时长 1~120 秒通过，0 秒、121 秒、负数拒绝")
{
    const std::string fileid = "\"1234567\"";
    const std::string md5 = "\"d41d8cd98f00b204e9800998ecf8427e\"";
    CHECK(MessageProtocol::isValidVoiceContent(makeVoiceContent(fileid, md5, "1", "25600", "")));
    CHECK(MessageProtocol::isValidVoiceContent(makeVoiceContent(fileid, md5, "120", "25600", "")));
    CHECK_FALSE(MessageProtocol::isValidVoiceContent(makeVoiceContent(fileid, md5, "0", "25600", "")));
    CHECK_FALSE(MessageProtocol::isValidVoiceContent(makeVoiceContent(fileid, md5, "121", "25600", "")));
    CHECK_FALSE(MessageProtocol::isValidVoiceContent(makeVoiceContent(fileid, md5, "-5", "25600", "")));
}

TEST_CASE("VMS4 语音 content：大小 1~1048576 字节通过，0 与 1048577 拒绝")
{
    const std::string fileid = "\"1234567\"";
    const std::string md5 = "\"d41d8cd98f00b204e9800998ecf8427e\"";
    CHECK(MessageProtocol::isValidVoiceContent(makeVoiceContent(fileid, md5, "8", "1", "")));
    CHECK(MessageProtocol::isValidVoiceContent(makeVoiceContent(fileid, md5, "8", "1048576", "")));
    CHECK_FALSE(MessageProtocol::isValidVoiceContent(makeVoiceContent(fileid, md5, "8", "0", "")));
    CHECK_FALSE(MessageProtocol::isValidVoiceContent(makeVoiceContent(fileid, md5, "8", "1048577", "")));
}

TEST_CASE("VMS5 语音 content：wave 解码后不超过 64 字节通过；非法 Base64、超长、不是字符串都拒绝")
{
    const std::string fileid = "\"1234567\"";
    const std::string md5 = "\"d41d8cd98f00b204e9800998ecf8427e\"";
    CHECK(MessageProtocol::isValidVoiceContent(makeVoiceContent(fileid, md5, "8", "25600",
                                                                "\"" + zeroBytesBase64(64) + "\"")));
    CHECK_FALSE(MessageProtocol::isValidVoiceContent(makeVoiceContent(fileid, md5, "8", "25600",
                                                                      "\"" + zeroBytesBase64(65) + "\"")));
    CHECK_FALSE(MessageProtocol::isValidVoiceContent(makeVoiceContent(fileid, md5, "8", "25600", "\"@@@@\"")));
    CHECK_FALSE(MessageProtocol::isValidVoiceContent(makeVoiceContent(fileid, md5, "8", "25600", "\"AAA\"")));
    CHECK_FALSE(MessageProtocol::isValidVoiceContent(makeVoiceContent(fileid, md5, "8", "25600", "123")));
}

TEST_CASE("VMS6 发送前检查：不合格的语音回请求格式错误，合格的语音与其它类型放行")
{
    const std::string good = makeVoiceContent("\"1234567\"", "\"d41d8cd98f00b204e9800998ecf8427e\"", "8", "25600", "");
    const std::string bad = makeVoiceContent("\"1234567\"", "\"d41d8cd98f00b204e9800998ecf8427e\"", "0", "25600", "");
    CHECK(MessageProtocol::checkSendContent(message_type_voice, good) == error_code_ok);
    CHECK(MessageProtocol::checkSendContent(message_type_voice, bad) == error_code_bad_request);
    //其它类型不在这里检查，即使 content 不像语音也放行
    CHECK(MessageProtocol::checkSendContent(message_type_text, "{\"text\":\"hi\"}") == error_code_ok);
    CHECK(MessageProtocol::checkSendContent(message_type_image, bad) == error_code_ok);
    //被拒时的回执带回 clientmsgid，客户端据此知道是哪一条没有发出去
    const std::string resp = MessageProtocol::buildSendMessageResponse(error_code_bad_request, "bad voice content",
                                                                       "c-voice-1", 0, 0);
    CHECK(resp.find("\"c-voice-1\"") != std::string::npos);
    CHECK(resp.find("\"code\":3") != std::string::npos);
}


TEST_CASE("S40 buildMessageDigest：一对一通话卡片按媒体类型显示「[语音通话]」「[视频通话]」，群通话卡片不受影响")
{
    CHECK(MessageProtocol::buildMessageDigest(16, "{\"kind\":\"call\",\"media\":1}") == u8"[语音通话]");
    CHECK(MessageProtocol::buildMessageDigest(16, "{\"kind\":\"call\",\"media\":2,\"result\":7,\"duration\":12}") == u8"[视频通话]");
    CHECK(MessageProtocol::buildMessageDigest(16, "{\"kind\":\"groupcall\",\"media\":2}") == u8"[群视频通话]");
    CHECK(MessageProtocol::buildMessageDigest(16, "{\"kind\":\"call\",\"media\":5}") == u8"[通话]");
    CHECK(MessageProtocol::buildMessageDigest(16, "not json") == u8"[通话]");
}

TEST_CASE("MA1 contentMentionsAll：content.at.all 为 1 或 true 时算 @所有人，其余都不算")
{
    //PC 客户端的写法：整数 1，带 users
    CHECK(MessageProtocol::contentMentionsAll("{\"text\":\"@所有人 开会\",\"at\":{\"all\":1,\"users\":[]}}"));
    //布尔值同样认
    CHECK(MessageProtocol::contentMentionsAll("{\"text\":\"x\",\"at\":{\"all\":true}}"));
    //all 为 0 / false、只 @ 了具体的人
    CHECK_FALSE(MessageProtocol::contentMentionsAll(
        "{\"text\":\"@张三 \",\"at\":{\"all\":0,\"users\":[{\"uid\":\"5\",\"off\":0,\"len\":7}]}}"));
    CHECK_FALSE(MessageProtocol::contentMentionsAll("{\"text\":\"x\",\"at\":{\"all\":false}}"));
    //没有 at、at 不是对象、all 是字符串
    CHECK_FALSE(MessageProtocol::contentMentionsAll("{\"text\":\"x\"}"));
    CHECK_FALSE(MessageProtocol::contentMentionsAll("{\"text\":\"x\",\"at\":1}"));
    CHECK_FALSE(MessageProtocol::contentMentionsAll("{\"text\":\"x\",\"at\":{\"all\":\"1\"}}"));
    //content 不是 JSON 对象
    CHECK_FALSE(MessageProtocol::contentMentionsAll("not json"));
    CHECK_FALSE(MessageProtocol::contentMentionsAll("[1,2]"));
    CHECK_FALSE(MessageProtocol::contentMentionsAll(""));
}

TEST_CASE("MA2 stripContentMentions：群发投递前去掉 content 里的 at，其余字段不变")
{
    std::string content = "{\"text\":\"@所有人 开会\",\"at\":{\"all\":1,\"users\":[]},\"font\":{\"size\":10}}";
    CHECK(MessageProtocol::stripContentMentions(content));
    Json::Value out;
    REQUIRE(parseJson(content, out));
    CHECK_FALSE(out.isMember("at"));
    CHECK(out["text"].asString() == u8"@所有人 开会");
    CHECK(out["font"]["size"].asInt() == 10);
    CHECK_FALSE(MessageProtocol::contentMentionsAll(content));

    //本来没有 at：不改动、不重新序列化（字段顺序与写法保持原样）
    const std::string plain = "{ \"text\" : \"hi\" , \"font\":{\"size\":10}}";
    std::string copy = plain;
    CHECK_FALSE(MessageProtocol::stripContentMentions(copy));
    CHECK(copy == plain);
    //content 不是 JSON 对象：不改动
    std::string broken = "not json";
    CHECK_FALSE(MessageProtocol::stripContentMentions(broken));
    CHECK(broken == "not json");
}
