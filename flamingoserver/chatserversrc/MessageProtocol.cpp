/**
 *  v2 会话与消息报文编解码实现，MessageProtocol.cpp
 *  balloonwj@qq.com   2026-05-21
 */
#include "MessageProtocol.h"
#include "Protocol.h"   //enum file_ack_state（校验上报的收取状态取值）
#include "../jsoncpp1.9.0/json.h"

//消息内容类型 msgtype 取值（见协议设计-v2.md §9.4 / §10.4 的 content 表）
constexpr int MSGTYPE_TEXT     = 1;   //文本
constexpr int MSGTYPE_IMAGE    = 2;   //图片
constexpr int MSGTYPE_FILE     = 3;   //文件
constexpr int MSGTYPE_VOICE    = 4;   //语音
constexpr int MSGTYPE_EMOJI    = 6;   //表情
constexpr int MSGTYPE_LOCATION = 7;   //位置
constexpr int MSGTYPE_CARD     = 8;   //名片
constexpr int MSGTYPE_POLL     = 10;  //投票卡片
constexpr int MSGTYPE_RELAY    = 11;  //接龙卡片
constexpr int MSGTYPE_TODO     = 12;  //待办卡片
constexpr int MSGTYPE_SHAKE    = 13;  //抖一抖
constexpr int MSGTYPE_MERGED   = 15;  //合并转发的聊天记录
constexpr int MSGTYPE_SYSTEM   = 17;  //系统消息

//把 Json::Value 序列化为紧凑（无缩进、无换行）的 JSON 文本
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//把 UTF-8 JSON 文本解析为 Json::Value；解析失败或非对象返回 false
static bool parseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//把 JSON 里的字符串型 64 位 ID 取出；字段缺失或非字符串返回 0
static int64_t getStringId(const Json::Value& root, const char* key)
{
    if (!root[key].isString())
        return 0;
    return static_cast<int64_t>(atoll(root[key].asString().c_str()));
}

//把 JSON 数组（元素为字符串型id）读成 int64 列表；非字符串元素跳过
static void readMentions(const Json::Value& arr, std::vector<int64_t>& out)
{
    if (!arr.isArray())
        return;
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        if (arr[i].isString())
            out.push_back(static_cast<int64_t>(atoll(arr[i].asString().c_str())));
    }
}

//把 int64 列表写成 JSON 数组（元素为字符串型id），与协议"64位ID用字符串"一致
static Json::Value writeMentions(const std::vector<int64_t>& ids)
{
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < ids.size(); ++i)
        arr.append(std::to_string(ids[i]));
    return arr;
}

//把一段原始 JSON 文本解析回 Json::Value；空串或解析失败返回 null 值
static Json::Value rawToValue(const std::string& raw)
{
    if (raw.empty())
        return Json::Value(Json::nullValue);

    Json::Value v;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(raw.c_str(), raw.c_str() + raw.size(), &v, &errs);
    delete reader;
    if (!ok || !errs.empty())
        return Json::Value(Json::nullValue);
    return v;
}

bool MessageProtocol::parseSendMessageRequest(const std::string& json, SendMessageRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    //clientmsgid 必填且非空——发送回执与去重都依赖它
    if (!root["clientmsgid"].isString())
        return false;
    req.clientmsgid = root["clientmsgid"].asString();
    if (req.clientmsgid.empty())
        return false;

    //ctype 可缺省，默认单聊
    req.ctype = root["ctype"].isInt() ? root["ctype"].asInt() : 1;

    //cid 必填且为正
    req.cid = getStringId(root, "cid");
    if (req.cid <= 0)
        return false;

    //msgtype 必填且为正
    if (!root["msgtype"].isInt())
        return false;
    req.msgtype = root["msgtype"].asInt();
    if (req.msgtype <= 0)
        return false;

    //content 必填且为对象（允许空对象，如 msgtype=13 抖一抖的 content 为 {}）
    if (!root["content"].isObject())
        return false;
    req.content = writeCompact(root["content"]);

    //mentions / mentionall / quote 均可缺省
    readMentions(root["mentions"], req.mentions);
    req.mentionall = root["mentionall"].isBool() ? root["mentionall"].asBool() : false;
    if (root["quote"].isObject())
        req.quote = writeCompact(root["quote"]);
    else
        req.quote = "";

    return true;
}

bool MessageProtocol::parseReceivedAck(const std::string& json, int64_t& msgid)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    msgid = getStringId(root, "msgid");
    return msgid > 0;
}

std::string MessageProtocol::buildSendMessageResponse(int code, const std::string& msg,
                                                      const std::string& clientmsgid,
                                                      int64_t msgid, int64_t timestamp)
{
    Json::Value root;
    root["code"]        = code;
    root["msg"]         = msg;
    root["clientmsgid"] = clientmsgid;
    root["msgid"]       = std::to_string(msgid);
    root["timestamp"]   = static_cast<Json::Int64>(timestamp);
    return writeCompact(root);
}

//构造一条完整消息体的 Json::Value；cmd_message_notify 包体与 cmd_sync_messages
//应答里的 messages 数组元素共用本结构（见协议 §10.4）
static Json::Value messageBodyValue(const MessageRecord& rec, int64_t cid)
{
    Json::Value root;
    root["msgid"]       = std::to_string(rec.msgid);
    root["clientmsgid"] = rec.clientmsgid;
    root["ctype"]       = rec.ctype;
    root["cid"]         = std::to_string(cid);
    root["from"]        = std::to_string(rec.from);
    root["to"]          = std::to_string(rec.to);
    root["msgtype"]     = rec.msgtype;
    root["content"]     = rawToValue(rec.content);
    root["mentions"]    = writeMentions(rec.mentions);
    root["mentionall"]  = rec.mentionall;
    //quote 仅在确有引用时输出
    if (!rec.quote.empty())
        root["quote"] = rawToValue(rec.quote);
    root["timestamp"]   = static_cast<Json::Int64>(rec.timestamp);
    root["recalled"]    = rec.recalled;
    //文件收取状态仅在确有状态时输出（0 表示非文件消息、或接收方尚无动作）。
    //只在这里带出来，发送方离线期间对方的接收/拒绝才能在下次拉历史时被看到；
    //老版本客户端不认识本字段，忽略即可，不影响其余渲染。
    if (rec.fileackstate != 0)
        root["fileackstate"] = rec.fileackstate;
    return root;
}

std::string MessageProtocol::buildMessageNotify(const MessageRecord& rec, int64_t cid)
{
    return writeCompact(messageBodyValue(rec, cid));
}

std::string MessageProtocol::buildSimpleResponse(int code, const std::string& msg)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;
    return writeCompact(root);
}

std::string MessageProtocol::buildStoredContent(const std::string& content,
                                                const std::vector<int64_t>& mentions,
                                                bool mentionall, const std::string& quote)
{
    Json::Value root;
    root["content"]    = rawToValue(content);
    root["mentions"]   = writeMentions(mentions);
    root["mentionall"] = mentionall;
    //无引用时不写 quote 字段，保持落库内容精简
    if (!quote.empty())
        root["quote"] = rawToValue(quote);
    return writeCompact(root);
}

bool MessageProtocol::parseStoredContent(const std::string& blob, std::string& content,
                                         std::vector<int64_t>& mentions,
                                         bool& mentionall, std::string& quote)
{
    Json::Value root;
    if (!parseObject(blob, root))
        return false;

    content = root["content"].isObject() ? writeCompact(root["content"]) : "";
    mentions.clear();
    readMentions(root["mentions"], mentions);
    mentionall = root["mentionall"].isBool() ? root["mentionall"].asBool() : false;
    quote = root["quote"].isObject() ? writeCompact(root["quote"]) : "";
    return true;
}

bool MessageProtocol::parseSyncMessagesRequest(const std::string& json, SyncMessagesRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    //ctype 可缺省，默认单聊
    req.ctype = root["ctype"].isInt() ? root["ctype"].asInt() : 1;

    //cid 必填且为正
    req.cid = getStringId(root, "cid");
    if (req.cid <= 0)
        return false;

    //出现 aftermsgid 即为补齐模式；否则为翻历史模式（beforemsgid 缺省按 0=从最新开始）
    if (root["aftermsgid"].isString())
    {
        req.isAfterMode = true;
        req.aftermsgid  = getStringId(root, "aftermsgid");
    }
    else
    {
        req.isAfterMode = false;
        req.beforemsgid = getStringId(root, "beforemsgid");
    }

    //limit 可缺省，缺省时置 0，由会话层套用默认值
    req.limit = root["limit"].isInt() ? root["limit"].asInt() : 0;
    return true;
}

std::string MessageProtocol::buildTextContent(const std::string& text)
{
    Json::Value root;
    root["text"] = text;
    return writeCompact(root);
}

std::string MessageProtocol::extractText(const std::string& content)
{
    Json::Value c = rawToValue(content);
    if (c.isObject() && c["text"].isString())
        return c["text"].asString();
    return std::string();
}

std::string MessageProtocol::buildMessageDigest(int msgtype, const std::string& content)
{
    //文本与系统消息：摘要直接取其正文
    if (msgtype == MSGTYPE_TEXT || msgtype == MSGTYPE_SYSTEM)
    {
        const std::string text = extractText(content);
        if (!text.empty())
            return text;
        return u8"[消息]";
    }

    //其余类型返回占位文案（中文字面量用 u8"" 确保以 UTF-8 写入 JSON）
    switch (msgtype)
    {
    case MSGTYPE_IMAGE:
        return u8"[图片]";
    case MSGTYPE_FILE:
        return u8"[文件]";
    case MSGTYPE_VOICE:
        return u8"[语音]";
    case MSGTYPE_EMOJI:
        return u8"[表情]";
    case MSGTYPE_LOCATION:
        return u8"[位置]";
    case MSGTYPE_CARD:
        return u8"[名片]";
    case MSGTYPE_POLL:
        return u8"[投票]";
    case MSGTYPE_RELAY:
        return u8"[接龙]";
    case MSGTYPE_TODO:
        return u8"[待办]";
    case MSGTYPE_SHAKE:
        return u8"[抖一抖]";
    case MSGTYPE_MERGED:
        return u8"[聊天记录]";
    default:
        return u8"[消息]";
    }
}

std::string MessageProtocol::buildSyncConversationsResponse(int code, const std::string& msg,
                                                            int64_t synctime,
                                                            const std::vector<ConversationBrief>& items)
{
    Json::Value root;
    root["code"]     = code;
    root["msg"]      = msg;
    root["synctime"] = static_cast<Json::Int64>(synctime);

    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < items.size(); ++i)
    {
        const ConversationBrief& it = items[i];
        Json::Value jo;
        jo["ctype"]     = it.ctype;
        jo["cid"]       = std::to_string(it.cid);
        jo["unread"]    = it.unread;
        jo["pinned"]    = it.pinned;
        jo["mute"]      = it.mute;
        jo["lastmsgid"] = std::to_string(it.lastmsgid);

        Json::Value last;
        last["from"]      = std::to_string(it.lastfrom);
        last["msgtype"]   = it.lastmsgtype;
        last["digest"]    = buildMessageDigest(it.lastmsgtype, it.lastcontent);
        last["timestamp"] = static_cast<Json::Int64>(it.lasttimestamp);
        jo["lastmsg"] = last;

        arr.append(jo);
    }
    root["conversations"] = arr;
    return writeCompact(root);
}

std::string MessageProtocol::buildSyncMessagesResponse(int code, const std::string& msg,
                                                       int ctype, int64_t cid, bool hasmore,
                                                       const std::vector<MessageRecord>& messages)
{
    Json::Value root;
    root["code"]    = code;
    root["msg"]     = msg;
    root["ctype"]   = ctype;
    root["cid"]     = std::to_string(cid);
    root["hasmore"] = hasmore;

    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < messages.size(); ++i)
        arr.append(messageBodyValue(messages[i], cid));
    root["messages"] = arr;
    return writeCompact(root);
}

bool MessageProtocol::parseMarkReadRequest(const std::string& json, MarkReadRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.ctype = root["ctype"].isInt() ? root["ctype"].asInt() : 1;

    req.cid = getStringId(root, "cid");
    if (req.cid <= 0)
        return false;

    req.readmsgid = getStringId(root, "readmsgid");
    if (req.readmsgid <= 0)
        return false;

    return true;
}

bool MessageProtocol::parseRecallMessageRequest(const std::string& json, RecallMessageRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.ctype = root["ctype"].isInt() ? root["ctype"].asInt() : 1;

    req.cid = getStringId(root, "cid");
    if (req.cid <= 0)
        return false;

    req.msgid = getStringId(root, "msgid");
    if (req.msgid <= 0)
        return false;

    return true;
}

std::string MessageProtocol::buildMessageReadNotify(int ctype, int64_t cid,
                                                    int64_t readmsgid, int readcount)
{
    Json::Value root;
    root["ctype"]     = ctype;
    root["cid"]       = std::to_string(cid);
    root["readmsgid"] = std::to_string(readmsgid);
    root["readcount"] = readcount;
    return writeCompact(root);
}

std::string MessageProtocol::buildMessageRecalledNotify(int ctype, int64_t cid,
                                                        int64_t msgid, int64_t operatorId)
{
    Json::Value root;
    root["ctype"]    = ctype;
    root["cid"]      = std::to_string(cid);
    root["msgid"]    = std::to_string(msgid);
    root["operator"] = std::to_string(operatorId);
    return writeCompact(root);
}

bool MessageProtocol::parsePinMessageRequest(const std::string& json, PinMessageRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.ctype = root["ctype"].isInt() ? root["ctype"].asInt() : 1;

    req.cid = getStringId(root, "cid");
    if (req.cid <= 0)
        return false;

    req.msgid = getStringId(root, "msgid");
    if (req.msgid <= 0)
        return false;

    //pin 直接决定落库方向（写一行还是删一行），越界值不能当成"取消"糊弄过去，
    //故这里既要求字段存在、又要求恰为 0 或 1。
    if (!root["pin"].isInt())
        return false;

    req.pin = root["pin"].asInt();
    if (req.pin != 0 && req.pin != 1)
        return false;

    return true;
}

std::string MessageProtocol::buildPinMessageResponse(int code, const std::string& msg,
                                                     int ctype, int64_t cid,
                                                     int64_t msgid, int pin)
{
    Json::Value root;
    root["code"]  = code;
    root["msg"]   = msg;
    root["ctype"] = ctype;
    root["cid"]   = std::to_string(cid);
    root["msgid"] = std::to_string(msgid);
    root["pin"]   = pin;
    return writeCompact(root);
}

std::string MessageProtocol::buildMessagePinnedNotify(int ctype, int64_t cid, int64_t msgid,
                                                      int pin, int64_t operatorId,
                                                      int64_t pinTime)
{
    Json::Value root;
    root["ctype"]    = ctype;
    root["cid"]      = std::to_string(cid);
    root["msgid"]    = std::to_string(msgid);
    root["pin"]      = pin;
    root["operator"] = std::to_string(operatorId);
    root["pintime"]  = std::to_string(pinTime);
    return writeCompact(root);
}

bool MessageProtocol::parseGetPinnedMessagesRequest(const std::string& json,
                                                    GetPinnedMessagesRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.ctype = root["ctype"].isInt() ? root["ctype"].asInt() : 1;

    req.cid = getStringId(root, "cid");
    if (req.cid <= 0)
        return false;

    return true;
}

std::string MessageProtocol::buildGetPinnedMessagesResponse(int code, const std::string& msg,
                                                            int ctype, int64_t cid, int64_t total,
                                                            const std::vector<MessageRecord>& messages,
                                                            const std::vector<PinRecord>& pins)
{
    Json::Value root;
    root["code"]  = code;
    root["msg"]   = msg;
    root["ctype"] = ctype;
    root["cid"]   = std::to_string(cid);
    //total 是不受单次返回条数约束的真实总数，可能大于 msgs 的长度。客户端据它判断
    //本次是否被截断，进而决定本地对账的时间下界——详见 docs/消息Pin接入方案.md §4.3。
    root["total"] = static_cast<Json::Int64>(total);

    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < messages.size(); ++i)
    {
        Json::Value item = messageBodyValue(messages[i], cid);

        //挂上 pin 子对象说明这条是谁在什么时候钉的。pins 与 messages 一一对应；
        //调用方给短了就按 0 输出，宁可少一份元信息也不能让整个应答错位。
        Json::Value pinValue;
        if (i < pins.size())
        {
            pinValue["operator"] = std::to_string(pins[i].operatorId);
            pinValue["pintime"]  = std::to_string(pins[i].pinTime);
        }
        else
        {
            pinValue["operator"] = std::to_string(0);
            pinValue["pintime"]  = std::to_string(0);
        }
        item["pin"] = pinValue;

        arr.append(item);
    }
    root["msgs"] = arr;
    return writeCompact(root);
}

bool MessageProtocol::parseTypingNotifyRequest(const std::string& json, TypingNotifyRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.ctype = root["ctype"].isInt() ? root["ctype"].asInt() : 1;

    req.cid = getStringId(root, "cid");
    if (req.cid <= 0)
        return false;

    req.state = root["state"].isInt() ? root["state"].asInt() : 0;
    return true;
}

std::string MessageProtocol::buildTypingNotify(int ctype, int64_t cid, int state)
{
    Json::Value root;
    root["ctype"] = ctype;
    root["cid"]   = std::to_string(cid);
    root["state"] = state;
    return writeCompact(root);
}

bool MessageProtocol::parseFileTransferAckRequest(const std::string& json, FileTransferAckRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.ctype = root["ctype"].isInt() ? root["ctype"].asInt() : 1;

    req.cid = getStringId(root, "cid");
    if (req.cid <= 0)
        return false;

    req.msgid = getStringId(root, "msgid");
    if (req.msgid <= 0)
        return false;

    //state 直接落库并回显给发送方，越界值会让对端显示成莫名其妙的状态，故在此挡掉
    req.state = root["state"].isInt() ? root["state"].asInt() : 0;
    if (req.state != file_ack_receiving
        && req.state != file_ack_received
        && req.state != file_ack_rejected)
    {
        return false;
    }
    return true;
}

std::string MessageProtocol::buildFileTransferAckNotify(int ctype, int64_t cid,
                                                        int64_t msgid, int state)
{
    Json::Value root;
    root["ctype"] = ctype;
    root["cid"]   = std::to_string(cid);
    root["msgid"] = std::to_string(msgid);
    root["state"] = state;
    return writeCompact(root);
}

bool MessageProtocol::parseSetConversationRequest(const std::string& json, SetConversationRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.ctype = root["ctype"].isInt() ? root["ctype"].asInt() : 1;

    req.cid = getStringId(root, "cid");
    if (req.cid <= 0)
        return false;

    //pinned/mute/deleted 均为可选；仅出现的字段会被更新
    if (root["pinned"].isBool())
    {
        req.has_pinned = true;
        req.pinned     = root["pinned"].asBool();
    }
    if (root["mute"].isBool())
    {
        req.has_mute = true;
        req.mute     = root["mute"].asBool();
    }
    if (root["deleted"].isBool())
    {
        req.has_deleted = true;
        req.deleted     = root["deleted"].asBool();
    }
    return true;
}

std::string MessageProtocol::buildConversationChangedNotify(int ctype, int64_t cid,
                                                            bool pinned, bool mute, bool deleted)
{
    Json::Value root;
    root["ctype"]   = ctype;
    root["cid"]     = std::to_string(cid);
    root["pinned"]  = pinned;
    root["mute"]    = mute;
    root["deleted"] = deleted;
    return writeCompact(root);
}

bool MessageProtocol::parseSearchMessagesRequest(const std::string& json, SearchMessagesRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    req.ctype = root["ctype"].isInt() ? root["ctype"].asInt() : 1;

    req.cid = getStringId(root, "cid");
    if (req.cid <= 0)
        return false;

    if (!root["keyword"].isString())
        return false;
    req.keyword = root["keyword"].asString();
    if (req.keyword.empty())
        return false;

    req.beforemsgid = getStringId(root, "beforemsgid");
    req.limit = root["limit"].isInt() ? root["limit"].asInt() : 0;
    return true;
}

bool MessageProtocol::parseForwardMessageRequest(const std::string& json, ForwardMessageRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    if (!root["clientmsgid"].isString())
        return false;
    req.clientmsgid = root["clientmsgid"].asString();
    if (req.clientmsgid.empty())
        return false;

    req.toctype = root["toctype"].isInt() ? root["toctype"].asInt() : 1;

    req.tocid = getStringId(root, "tocid");
    if (req.tocid <= 0)
        return false;

    req.merge = root["merge"].isBool() ? root["merge"].asBool() : false;
    req.title = root["title"].isString() ? root["title"].asString() : "";

    //msgids 必填且非空
    const Json::Value& arr = root["msgids"];
    if (!arr.isArray() || arr.size() == 0)
        return false;
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        if (arr[i].isString())
            req.msgids.push_back(static_cast<int64_t>(atoll(arr[i].asString().c_str())));
    }
    if (req.msgids.empty())
        return false;
    return true;
}

std::string MessageProtocol::buildForwardMessageResponse(int code, const std::string& msg,
                                                         const std::vector<int64_t>& msgids)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;

    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < msgids.size(); ++i)
        arr.append(std::to_string(msgids[i]));
    root["msgids"] = arr;
    return writeCompact(root);
}

std::string MessageProtocol::buildMergedContent(const std::string& title,
                                                const std::vector<MessageRecord>& subs)
{
    Json::Value root;
    root["title"] = title;

    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < subs.size(); ++i)
        arr.append(messageBodyValue(subs[i], subs[i].from));
    root["messages"] = arr;
    return writeCompact(root);
}

bool MessageProtocol::parseDeleteMessageRequest(const std::string& json, int64_t& msgid)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    msgid = getStringId(root, "msgid");
    return msgid > 0;
}

bool MessageProtocol::parseMassSendRequest(const std::string& json, MassSendRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    if (!root["clientmsgid"].isString())
        return false;
    req.clientmsgid = root["clientmsgid"].asString();
    if (req.clientmsgid.empty())
        return false;

    if (!root["msgtype"].isInt())
        return false;
    req.msgtype = root["msgtype"].asInt();
    if (req.msgtype <= 0)
        return false;

    if (!root["content"].isObject())
        return false;
    req.content = writeCompact(root["content"]);

    //targets 必填且非空；超过单次上限的请求直接拒掉，理由见 MASS_SEND_MAX_TARGETS
    const Json::Value& arr = root["targets"];
    if (!arr.isArray() || arr.size() == 0)
        return false;
    if (arr.size() > MASS_SEND_MAX_TARGETS)
        return false;
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        if (!arr[i].isObject())
            continue;
        MassSendTarget t;
        t.ctype = arr[i]["ctype"].isInt() ? arr[i]["ctype"].asInt() : 1;
        t.cid   = getStringId(arr[i], "cid");
        if (t.cid > 0)
            req.targets.push_back(t);
    }
    if (req.targets.empty())
        return false;
    return true;
}

std::string MessageProtocol::buildMassSendResponse(int code, const std::string& msg,
                                                   const std::vector<MassSendResult>& results)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;

    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < results.size(); ++i)
    {
        Json::Value jo;
        jo["cid"]   = std::to_string(results[i].cid);
        jo["msgid"] = std::to_string(results[i].msgid);
        arr.append(jo);
    }
    root["results"] = arr;
    return writeCompact(root);
}
