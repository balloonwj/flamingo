/**
 *  v2 会话与消息报文编解码实现，MessageProtocol.cpp
 *  balloonwj@qq.com   2026-05-21
 */
#include "MessageProtocol.h"
#include "Protocol.h"   //enum file_ack_state（校验上报的收取状态取值）
#include "GroupCallProtocol.h"   //cardMedia：通话记录（群通话卡片）的会话列表摘要
#include "CallProtocol.h"        //cardMedia：通话记录（一对一通话卡片）的会话列表摘要
#include "../jsoncpp1.9.0/json.h"
#include <sodium.h>   //sodium_base642bin：严格校验语音波形 wave 的 Base64

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

//合并转发标题的缺省值：客户端没有给出标题时使用
constexpr const char* DEFAULT_MERGED_TITLE = u8"聊天记录";

//UTF-8 续字节的判定：字节与 0xC0 相与后等于 0x80（即 10xxxxxx）的是多字节字符的
//后续字节，不是一个新字符的开始。截断标题时据此找字符边界。
constexpr unsigned char UTF8_CONTINUATION_MASK = 0xC0;
constexpr unsigned char UTF8_CONTINUATION_BITS = 0x80;

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

//int64 正数最多 19 位十进制数字（上限 9223372036854775807），超过即视为非法 fileid
constexpr size_t MAX_INT64_DECIMAL_DIGITS = 19;

//该消息类型的 content 是否可能引用一个上传过的文件（带 fileid 字段）
static bool msgtypeMayReferenceFile(int msgtype)
{
    switch (msgtype)
    {
    //图片：{ fileid, md5, width, height, size }
    case message_type_image:
    //文件：{ fileid, md5, name, size, ext }
    case message_type_file:
    //语音：{ fileid, md5, duration, size }
    case message_type_voice:
    //短视频：与语音同样以 fileid 引用上传的文件
    case message_type_video:
    //大表情：自定义表情带 fileid，内置表情只有 emoji 字段（后者由调用方按字段缺失处理）
    case message_type_emoji:
    //截图：本质为图片子类，content 与图片一致
    case message_type_screenshot:
        return true;

    //其余类型（文本、名片、卡片、系统消息等）不引用上传的文件
    default:
        return false;
    }
}

bool MessageProtocol::parseFileIdText(const std::string& text, int64_t& fileid)
{
    //只接受纯十进制数字：atoll 会把 "117abc" 读成 117，这里要求整串都是数字
    if (text.empty() || text.size() > MAX_INT64_DECIMAL_DIGITS)
        return false;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] < '0' || text[i] > '9')
            return false;
    }

    const int64_t value = static_cast<int64_t>(strtoll(text.c_str(), NULL, 10));
    if (value <= 0)
        return false;

    fileid = value;
    return true;
}

bool MessageProtocol::extractContentFileId(int msgtype, const std::string& content, int64_t& fileid)
{
    //先按类型筛掉：文本消息占绝大多数，不必为它们解析 JSON
    if (!msgtypeMayReferenceFile(msgtype))
        return false;

    Json::Value root;
    if (!parseObject(content, root))
        return false;

    //协议约定 64 位 ID 一律编码为字符串，数值形态不认
    if (!root["fileid"].isString())
        return false;
    return parseFileIdText(root["fileid"].asString(), fileid);
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
    //撤回者仅在已撤回且有记录时输出（2026-10-03 之前撤回的消息没有记录撤回者，为 0），id 以字符串编码。
    //客户端据它写「某某撤回了一条消息」：群主 / 管理员撤回别人的群消息时它与 from 不同；
    //缺这个字段时按 from 处理，老客户端不认识本字段，忽略即可
    if (rec.recalled && rec.recalledby > 0)
        root["recalledby"] = std::to_string(rec.recalledby);
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

const char* const MessageProtocol::kRecalledDigest = u8"撤回了一条消息";

//帖子卡片（25）与帖子动态卡片（26）摘要里接在类型后面的那段文字（docs/帖子群方案.md 6.2）：
//帖子卡片取标题，没有标题时取正文摘要，已删除时为空；帖子动态卡片取留言、回复或补充的内容摘要，为空时取帖子标题
static std::string threadCardDigestArg(int msgtype, const Json::Value& c)
{
    if (!c.isObject())
        return std::string();
    //已删除的卡片（帖子卡片与动态卡片都可能被改写，docs/帖子群方案.md 14.6）不带任何原文
    if (c["deleted"].isIntegral() && c["deleted"].asInt() != 0)
        return std::string();
    if (msgtype == message_type_thread_post)
    {
        if (c["title"].isString() && !c["title"].asString().empty())
            return c["title"].asString();
        return c["digest"].isString() ? c["digest"].asString() : std::string();
    }
    if (c["digest"].isString() && !c["digest"].asString().empty())
        return c["digest"].asString();
    return c["posttitle"].isString() ? c["posttitle"].asString() : std::string();
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
    //接龙卡片：带上标题，会话列表显示为「[接龙] 周五团建报名」（docs/群接龙方案.md §3.7）；
    //没有标题（旧数据）或内容解析失败时只显示类型
    case MSGTYPE_RELAY:
    {
        Json::Value c = rawToValue(content);
        if (c.isObject() && c["title"].isString() && !c["title"].asString().empty())
            return std::string(u8"[接龙] ") + c["title"].asString();
        return u8"[接龙]";
    }
    case MSGTYPE_TODO:
        return u8"[待办]";
    case MSGTYPE_SHAKE:
        return u8"[抖一抖]";
    case MSGTYPE_MERGED:
        return u8"[聊天记录]";
    //文件共享卡片：带上共享名，会话列表显示为「[共享] Projects」（docs/文件共享方案.md 6.1）；
    //正文解析失败或没有名称时只显示类型
    case message_type_fileshare:
    {
        Json::Value c = rawToValue(content);
        if (c.isObject() && c["name"].isString() && !c["name"].asString().empty())
            return std::string(u8"[共享] ") + c["name"].asString();
        return u8"[共享]";
    }
    //屏幕共享卡片：会话列表显示为「[屏幕共享]」，不带发起人名字（会话列表本身已显示发送者）
    case message_type_screenshare:
        return u8"[屏幕共享]";
    //通话记录：群通话卡片按媒体类型显示「[群语音通话]」「[群视频通话]」（docs/协议设计-v2.md 21.5），一对一通话卡片显示
    //「[语音通话]」「[视频通话]」（7.8 节「一对一通话卡片」）；都不是或正文解析失败时显示「[通话]」
    case message_type_rtc:
    {
        const int media = GroupCallProtocol::cardMedia(content);
        if (media == groupcall_media_video)
            return u8"[群视频通话]";
        if (media == groupcall_media_voice)
            return u8"[群语音通话]";
        const int callMedia = CallProtocol::cardMedia(content);
        if (callMedia == call_media_video)
            return u8"[视频通话]";
        if (callMedia == call_media_audio)
            return u8"[语音通话]";
        return u8"[通话]";
    }
    //入群邀请卡片：带上群名，会话列表显示为「[入群邀请] 产品研发部周会群」（docs/邀请入群方案.md §3.3）；
    //正文解析失败或没有群名时只显示类型
    case message_type_group_invite:
    {
        Json::Value c = rawToValue(content);
        if (c.isObject() && c["groupname"].isString() && !c["groupname"].asString().empty())
            return std::string(u8"[入群邀请] ") + c["groupname"].asString();
        return u8"[入群邀请]";
    }
    //群公告卡片：带上标题，会话列表显示为「[群公告] v3.2 发布周五上线」（docs/群公告接入方案.md §13.2）；
    //已删除的卡片同样只写标题；正文解析失败或没有标题时只显示类型
    case message_type_announcement:
    {
        Json::Value c = rawToValue(content);
        if (c.isObject() && c["title"].isString() && !c["title"].asString().empty())
            return std::string(u8"[群公告] ") + c["title"].asString();
        return u8"[群公告]";
    }
    //群备忘卡片：带上标题，会话列表显示为「[群备忘] 周会」（docs/备忘与提醒方案.md §7.2）
    case message_type_memo:
    {
        Json::Value c = rawToValue(content);
        if (c.isObject() && c["title"].isString() && !c["title"].asString().empty())
            return std::string(u8"[群备忘] ") + c["title"].asString();
        return u8"[群备忘]";
    }
    //云端录像卡片：带上标题，会话列表显示为「[云端录像] 周会」（docs/屏幕共享与录制方案.md 12.6.5）
    case message_type_cloudrecord:
    {
        Json::Value c = rawToValue(content);
        if (c.isObject() && c["title"].isString() && !c["title"].asString().empty())
            return std::string(u8"[云端录像] ") + c["title"].asString();
        return u8"[云端录像]";
    }
    //帖子卡片：会话列表显示为「[帖子] 标题」，已删除时只写「[帖子]」（docs/帖子群方案.md 6.2）
    case message_type_thread_post:
    {
        const std::string arg = threadCardDigestArg(msgtype, rawToValue(content));
        return arg.empty() ? std::string(u8"[帖子]") : std::string(u8"[帖子] ") + arg;
    }
    //帖子动态卡片：按 kind 写成「[留言] 内容」「[回复] 内容」「[补充] 内容」
    case message_type_thread_reply:
    {
        const Json::Value c = rawToValue(content);
        const int kind = (c.isObject() && c["kind"].isIntegral()) ? c["kind"].asInt() : 0;
        //已删除的动态卡片：「[留言已删除]」等，不带原文（docs/帖子群方案.md 14.6）
        const bool deleted = c.isObject() && c["deleted"].isIntegral() && c["deleted"].asInt() != 0;
        std::string head;
        if (kind == thread_reply_comment)
            head = deleted ? u8"[留言已删除]" : u8"[留言]";
        else if (kind == thread_reply_reply)
            head = deleted ? u8"[回复已删除]" : u8"[回复]";
        else if (kind == thread_reply_addendum)
            head = deleted ? u8"[补充已删除]" : u8"[补充]";
        else
            head = u8"[帖子动态]";
        const std::string arg = threadCardDigestArg(msgtype, c);
        return arg.empty() ? head : head + " " + arg;
    }
    default:
        return u8"[消息]";
    }
}

//把一组 NotifyModeItem 写成 [ {ctype, cid, mode} ] 数组，cid 以字符串编码。
//cmd_sync_conversations 应答的 notifymodes 与 cmd_notify_modes_changed_notify 的 items 共用。
static Json::Value notifyModeItemsToJson(const std::vector<NotifyModeItem>& items)
{
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < items.size(); ++i)
    {
        Json::Value jo;
        jo["ctype"] = items[i].ctype;
        jo["cid"]   = std::to_string(items[i].cid);
        jo["mode"]  = items[i].mode;
        arr.append(jo);
    }
    return arr;
}

//会话列表 lastmsg 里补上客户端按界面语言在本地生成摘要所需的参数（多语言第 5 期）：系统提示带 tipkind / tipargs
//（与消息 content 里的同名字段相同）；接龙卡片带标题、文件共享卡片带共享名，写在 digestarg。content 解析失败或
//没有这些字段时什么都不写，客户端仍用 digest。老客户端不认识这几个字段，显示不变
static void appendDigestArgs(Json::Value& last, int msgtype, const std::string& content)
{
    Json::Value c = rawToValue(content);
    if (!c.isObject())
        return;

    if (msgtype == MSGTYPE_SYSTEM)
    {
        if (c["tipkind"].isString() && c["tipargs"].isObject())
        {
            last["tipkind"] = c["tipkind"];
            last["tipargs"] = c["tipargs"];
        }
        return;
    }
    if (msgtype == MSGTYPE_RELAY && c["title"].isString() && !c["title"].asString().empty())
    {
        last["digestarg"] = c["title"];
        return;
    }
    if (msgtype == message_type_fileshare && c["name"].isString() && !c["name"].asString().empty())
    {
        last["digestarg"] = c["name"];
        return;
    }
    //入群邀请卡片：带群名，客户端生成「[入群邀请] 群名」（docs/邀请入群方案.md §4.3）
    if (msgtype == message_type_group_invite && c["groupname"].isString() && !c["groupname"].asString().empty())
    {
        last["digestarg"] = c["groupname"];
        return;
    }
    //群公告卡片、群备忘卡片、云端录像卡片：带标题，客户端生成「[群公告] 标题」「[群备忘] 标题」「[云端录像] 标题」
    if ((msgtype == message_type_announcement || msgtype == message_type_memo || msgtype == message_type_cloudrecord)
        && c["title"].isString() && !c["title"].asString().empty())
    {
        last["digestarg"] = c["title"];
        return;
    }
    //帖子卡片、帖子动态卡片：带摘要参数、帖子号（客户端点会话时定位到这条帖子），动态卡片另带 kind，
    //已删除的帖子卡片带 threaddeleted；客户端据此生成「发帖：标题」「回复：内容」等（docs/帖子群方案.md 6.2）
    if (msgtype == message_type_thread_post || msgtype == message_type_thread_reply)
    {
        const std::string arg = threadCardDigestArg(msgtype, c);
        if (!arg.empty())
            last["digestarg"] = arg;
        if (c["postid"].isString())
            last["threadpostid"] = c["postid"];
        if (msgtype == message_type_thread_reply && c["kind"].isIntegral())
            last["threadkind"] = c["kind"].asInt();
        //帖子卡片与动态卡片被改写为已删除形态后都带 threaddeleted（docs/帖子群方案.md 14.6）
        if (c["deleted"].isIntegral() && c["deleted"].asInt() != 0)
            last["threaddeleted"] = 1;
    }
}

std::string MessageProtocol::buildSyncConversationsResponse(int code, const std::string& msg,
                                                            int64_t synctime,
                                                            const std::vector<ConversationBrief>& items,
                                                            const std::vector<NotifyModeItem>& notifyModes,
                                                            const std::vector<GroupFollowList>& groupFollows)
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
        jo["notifymode"] = it.notifymode;
        //手动标为未读：老客户端不认识这个字段，照常忽略（docs/会话标为未读方案.md）
        jo["unreadmark"] = it.unreadmark ? 1 : 0;
        jo["lastmsgid"] = std::to_string(it.lastmsgid);

        Json::Value last;
        last["from"]      = std::to_string(it.lastfrom);
        last["msgtype"]   = it.lastmsgtype;
        if (it.lastrecalled)
        {
            //已撤回：只说明「撤回了一条消息」，不能用原文生成摘要
            last["recalled"] = true;
            last["digest"]   = kRecalledDigest;
            //撤回者：口径同消息体的 recalledby，撤回者未知时不输出
            if (it.lastrecalledby > 0)
                last["recalledby"] = std::to_string(it.lastrecalledby);
        }
        else
        {
            last["digest"] = buildMessageDigest(it.lastmsgtype, it.lastcontent);
            appendDigestArgs(last, it.lastmsgtype, it.lastcontent);
        }
        last["timestamp"] = static_cast<Json::Int64>(it.lasttimestamp);
        jo["lastmsg"] = last;

        arr.append(jo);
    }
    root["conversations"] = arr;
    root["notifymodes"]   = notifyModeItemsToJson(notifyModes);

    //本人在各群的关注名单：只输出名单不为空的群，id 一律写成字符串
    Json::Value follows(Json::arrayValue);
    for (size_t i = 0; i < groupFollows.size(); ++i)
    {
        const GroupFollowList& gf = groupFollows[i];
        if (gf.targets.empty())
            continue;

        Json::Value jo;
        jo["groupid"] = std::to_string(gf.groupid);
        Json::Value targets(Json::arrayValue);
        for (size_t k = 0; k < gf.targets.size(); ++k)
            targets.append(std::to_string(gf.targets[k]));
        jo["targets"] = targets;
        follows.append(jo);
    }
    root["groupfollows"] = follows;
    return writeCompact(root);
}

std::string MessageProtocol::buildSyncMessagesResponse(int code, const std::string& msg,
                                                       int ctype, int64_t cid, bool hasmore,
                                                       const std::vector<MessageRecord>& messages,
                                                       int64_t peerReadMsgid,
                                                       const SyncMessagesRequest* anchorReq)
{
    Json::Value root;
    root["code"]    = code;
    root["msg"]     = msg;
    root["ctype"]   = ctype;
    root["cid"]     = std::to_string(cid);
    root["hasmore"] = hasmore;

    //对方已读位点只对单聊有意义（群聊按协议不做已读回执）；调用方不给（搜索应答）时不输出。
    //位点为 0（对方从没读过）照样输出，客户端据此知道「查过了，一条都没读」。
    //以字符串编码，避免超过 2^53 的 msgid 在客户端丢精度。
    if (ctype == conversation_single && peerReadMsgid >= 0)
    {
        root["peerreadmsgid"] = std::to_string(peerReadMsgid);
    }

    //原样回带请求锚点，客户端据此算出本页覆盖了哪段 msgid（docs/清理聊天记录方案.md §7.2）。
    //两种模式只回带各自的那一个；搜索应答不传请求，两者都不输出
    if (anchorReq != NULL)
    {
        if (anchorReq->isAfterMode)
            root["aftermsgid"] = std::to_string(anchorReq->aftermsgid);
        else
            root["beforemsgid"] = std::to_string(anchorReq->beforemsgid);
    }

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

    //pinned/notifymode/deleted 均为可选；仅出现的字段会被更新。2026-10-03 之前的布尔字段
    //mute 已由整数型 notifymode 取代，不再识别（服务端与客户端同时升级，不保留兼容）
    if (root["pinned"].isBool())
    {
        req.has_pinned = true;
        req.pinned     = root["pinned"].asBool();
    }
    if (root["notifymode"].isInt())
    {
        req.has_notifymode = true;
        req.notifymode     = root["notifymode"].asInt();
    }
    if (root["deleted"].isBool())
    {
        req.has_deleted = true;
        req.deleted     = root["deleted"].asBool();
    }
    //手动标为未读（docs/会话标为未读方案.md）：只认布尔值
    if (root["unreadmark"].isBool())
    {
        req.has_unreadmark = true;
        req.unreadmark     = root["unreadmark"].asBool();
    }
    return true;
}

bool MessageProtocol::isValidNotifyMode(int ctype, int mode)
{
    if (ctype != conversation_single && ctype != conversation_group)
        return false;

    if (mode < notify_mode_allow || mode > notify_mode_mention_follow)
        return false;

    //屏蔽只对群聊开放：屏蔽某位同事的单聊消息，对方完全不知情，容易漏掉工作消息
    //（2026-10-03 与需求方确认，见 docs/消息提醒模式与消息盒子方案.md 第 1 节）
    if (ctype == conversation_single && mode == notify_mode_block)
        return false;

    //「只提醒 @我和关注的人」只有群里才有意义：单聊没有 @，也没有可关注的其他成员
    //（docs/消息提醒模式与消息盒子方案.md §12.1）
    if (ctype == conversation_single && mode == notify_mode_mention_follow)
        return false;

    return true;
}

std::string MessageProtocol::buildConversationChangedNotify(int ctype, int64_t cid,
                                                            bool pinned, int notifymode, bool deleted,
                                                            bool unreadmark)
{
    Json::Value root;
    root["ctype"]      = ctype;
    root["cid"]        = std::to_string(cid);
    root["pinned"]     = pinned;
    root["notifymode"] = notifymode;
    root["deleted"]    = deleted;
    root["unreadmark"] = unreadmark;
    return writeCompact(root);
}

bool MessageProtocol::parseSetNotifyModesRequest(const std::string& json,
                                                 std::vector<SetNotifyModeEntry>& entries)
{
    entries.clear();

    Json::Value root;
    if (!parseObject(json, root))
        return false;

    const Json::Value& arr = root["items"];
    if (!arr.isArray())
        return false;
    if (arr.empty() || arr.size() > NOTIFY_MODE_BATCH_MAX)
        return false;

    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        const Json::Value& jo = arr[i];
        SetNotifyModeEntry entry;
        if (jo.isObject())
        {
            //能取出的部分先取出来，供逐条结果原样回带，客户端据此对上号
            if (jo["ctype"].isInt())
                entry.item.ctype = jo["ctype"].asInt();
            entry.item.cid = getStringId(jo, "cid");
            if (jo["mode"].isInt())
                entry.item.mode = jo["mode"].asInt();

            entry.wellformed = jo["ctype"].isInt() && jo["mode"].isInt() && entry.item.cid > 0;
        }
        entries.push_back(entry);
    }
    return true;
}

std::string MessageProtocol::buildSetNotifyModesResponse(int code, const std::string& msg,
                                                         const std::vector<NotifyModeResult>& results)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;

    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < results.size(); ++i)
    {
        Json::Value jo;
        jo["ctype"] = results[i].ctype;
        jo["cid"]   = std::to_string(results[i].cid);
        jo["code"]  = results[i].code;
        arr.append(jo);
    }
    root["results"] = arr;
    return writeCompact(root);
}

std::string MessageProtocol::buildNotifyModesChangedNotify(const std::vector<NotifyModeItem>& items)
{
    Json::Value root;
    root["items"] = notifyModeItemsToJson(items);
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
    req.title = normalizeForwardTitle(root["title"].isString() ? root["title"].asString() : "");

    //msgids 必填且非空；超过单次上限的请求直接拒掉，理由见 FORWARD_MAX_MESSAGES
    const Json::Value& arr = root["msgids"];
    if (!arr.isArray() || arr.size() == 0)
        return false;
    if (arr.size() > FORWARD_MAX_MESSAGES)
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

std::string MessageProtocol::normalizeForwardTitle(const std::string& raw)
{
    if (raw.empty())
        return std::string(DEFAULT_MERGED_TITLE);

    //逐字节扫描，遇到字符的首字节（不是 10xxxxxx 形式的续字节）就计一个字符。
    //第 FORWARD_TITLE_MAX_CHARS + 1 个字符的首字节位置就是截断点：在首字节处截断，
    //前一个字符的续字节已经全部保留，不会留下半个字符。
    size_t chars = 0;
    for (size_t i = 0; i < raw.size(); ++i)
    {
        const unsigned char b = static_cast<unsigned char>(raw[i]);
        if ((b & UTF8_CONTINUATION_MASK) == UTF8_CONTINUATION_BITS)
            continue;
        if (chars == FORWARD_TITLE_MAX_CHARS)
            return raw.substr(0, i);
        ++chars;
    }
    return raw;
}

std::string MessageProtocol::buildForwardedContent(int msgtype, const std::string& content)
{
    if (msgtype != MSGTYPE_TEXT)
        return content;

    Json::Value root;
    if (!parseObject(content, root))
        return content;

    //几项都不存在时原样返回，免得重新序列化改变字段顺序与数字格式
    if (!root.isMember("at") && !root.isMember("quote") && !root.isMember("bot") && !root.isMember("autoreply"))
        return content;

    root.removeMember("at");
    root.removeMember("quote");
    //转发出去的是转发人发的消息：机器人标记与自动回复标记不再适用（docs/机器人方案.md §5.6）
    root.removeMember("bot");
    root.removeMember("autoreply");
    return writeCompact(root);
}

bool MessageProtocol::contentMentionsAll(const std::string& content)
{
    Json::Value root;
    if (!parseObject(content, root))
        return false;
    const Json::Value& at = root["at"];
    if (!at.isObject())
        return false;
    const Json::Value& all = at["all"];
    //PC 客户端写整数 1 / 0，也接受布尔值
    if (all.isBool())
        return all.asBool();
    return all.isIntegral() && all.asInt64() != 0;
}

bool MessageProtocol::stripContentMentions(std::string& content)
{
    Json::Value root;
    if (!parseObject(content, root) || !root.isMember("at"))
        return false;
    root.removeMember("at");
    content = writeCompact(root);
    return true;
}

//取 JSON 对象里的一个字符串字段，非空时追加到 out；字段缺失或不是字符串时忽略。
//调用方须保证 obj 是对象（jsoncpp 对非对象调用 operator[](const char*) 会触发断言）
static void appendStringField(const Json::Value& obj, const char* key, std::vector<std::string>& out)
{
    const Json::Value& value = obj[key];
    if (value.isString() && !value.asString().empty())
        out.push_back(value.asString());
}

//取引用对象里的 digest（被引用消息的文字摘要）。name 是被引用人的名字，不取
static void appendQuoteDigest(const Json::Value& quote, std::vector<std::string>& out)
{
    if (quote.isObject())
        appendStringField(quote, "digest", out);
}

//从一条消息的 content 对象里取出需要做敏感词检查的文字，追加到 out，规则见
//MessageProtocol::collectSensitiveTexts 的注释。
//合并记录向里递归。递归深度不另设上限：content 由 jsoncpp 解析而来，而 jsoncpp 默认
//把嵌套深度限制在 1000 层（json_reader.cpp 的 stackLimit），更深的内容根本解析不出来。
static void collectTextsFromContent(int msgtype, const Json::Value& content,
                                    std::vector<std::string>& out)
{
    if (!content.isObject())
        return;

    switch (msgtype)
    {
    //文字消息：正文；代码片段另有批注（content.notes，每项的 text 是说明文字，见 docs/代码片段方案.md 第 10.5 节），
    //说明会显示在对方的代码查看窗里，一并检查。notes 不是数组、条目不是对象的跳过
    case MSGTYPE_TEXT:
    {
        appendStringField(content, "text", out);
        const Json::Value& notes = content["notes"];
        if (notes.isArray())
        {
            for (Json::ArrayIndex i = 0; i < notes.size(); ++i)
            {
                if (notes[i].isObject())
                    appendStringField(notes[i], "text", out);
            }
        }
        break;
    }

    //文件消息：文件名由发送方填写，会原样显示在对方的聊天窗口里
    case MSGTYPE_FILE:
        appendStringField(content, "name", out);
        break;

    //投票卡片：标题与每个选项；不是字符串的选项跳过
    case MSGTYPE_POLL:
    {
        appendStringField(content, "title", out);
        const Json::Value& options = content["options"];
        if (options.isArray())
        {
            for (Json::ArrayIndex i = 0; i < options.size(); ++i)
            {
                if (options[i].isString() && !options[i].asString().empty())
                    out.push_back(options[i].asString());
            }
        }
        break;
    }

    //接龙卡片：标题与说明。各成员参与接龙的文字走 cmd_join_relay，在那里单独检查
    case MSGTYPE_RELAY:
        appendStringField(content, "title", out);
        appendStringField(content, "note", out);
        break;

    //待办卡片：标题；负责人是 userid 列表，不是文字
    case MSGTYPE_TODO:
        appendStringField(content, "title", out);
        break;

    //合并记录：先取标题，再逐条取子消息，子消息上的引用一并取；fromname 是发送人昵称，不取。
    //缺 msgtype 或 msgtype 不是整数的子消息无法判断类型，跳过
    case MSGTYPE_MERGED:
    {
        appendStringField(content, "title", out);
        const Json::Value& subs = content["messages"];
        if (subs.isArray())
        {
            for (Json::ArrayIndex i = 0; i < subs.size(); ++i)
            {
                const Json::Value& sub = subs[i];
                if (!sub.isObject() || !sub["msgtype"].isInt())
                    continue;
                collectTextsFromContent(sub["msgtype"].asInt(), sub["content"], out);
                appendQuoteDigest(sub["quote"], out);
            }
        }
        break;
    }

    //其余类型（图片、语音、表情、位置、名片、抖一抖等）：content 是结构化字段或从通讯录
    //选来的名称，不是发送方输入的文字，不检查
    default:
        break;
    }

    //PC 客户端把引用放在 content.quote 里（协议文档写的是请求顶层的 quote），两处都要取，
    //否则换一个位置写引用就能绕过检查
    appendQuoteDigest(content["quote"], out);
}

void MessageProtocol::collectSensitiveTexts(int msgtype, const std::string& content, const std::string& quote,
                                            std::vector<std::string>& out)
{
    Json::Value root;
    if (parseObject(content, root))
        collectTextsFromContent(msgtype, root, out);

    if (quote.empty())
        return;
    Json::Value quoteRoot;
    if (parseObject(quote, quoteRoot))
        appendQuoteDigest(quoteRoot, out);
}

std::string MessageProtocol::buildForwardMessageResponse(int code, const std::string& msg,
                                                         const std::string& clientmsgid,
                                                         const std::vector<MessageRecord>& created,
                                                         int64_t cid)
{
    Json::Value root;
    root["code"]        = code;
    root["msg"]         = msg;
    root["clientmsgid"] = clientmsgid;

    //msgids 总是完整的；messages 按顺序放入，累计体积超过预算后不再放，
    //使 messages 始终是 msgids 的前缀，客户端按下标即可对应
    Json::Value ids(Json::arrayValue);
    Json::Value bodies(Json::arrayValue);
    size_t usedBytes = 0;
    bool budgetExhausted = false;
    for (size_t i = 0; i < created.size(); ++i)
    {
        ids.append(std::to_string(created[i].msgid));
        if (budgetExhausted)
            continue;

        Json::Value body = messageBodyValue(created[i], cid);
        const size_t bodyBytes = writeCompact(body).size();
        if (usedBytes + bodyBytes > FORWARD_RESPONSE_MESSAGES_BUDGET_BYTES)
        {
            budgetExhausted = true;
            continue;
        }
        usedBytes += bodyBytes;
        bodies.append(body);
    }
    root["msgids"]   = ids;
    root["messages"] = bodies;
    return writeCompact(root);
}

bool MessageProtocol::isMergedContentTooLarge(const std::string& mergedContent)
{
    return mergedContent.size() > FORWARD_MERGED_CONTENT_MAX_BYTES;
}

bool MessageProtocol::isMessageContentTooLarge(const std::string& content, const std::string& quote)
{
    //content 与 quote 都随消息落库、推送、同步，按两者合计计量
    return content.size() + quote.size() > MESSAGE_CONTENT_MAX_BYTES;
}

bool MessageProtocol::isClientOperableMsgType(int msgtype)
{
    //群公告卡片、群备忘卡片（2026-10-05 起）、云端录像卡片（2026-10-06 起）与其余服务端生成的卡片一样，
    //客户端不得发送、撤回、置顶、转发
    return msgtype != message_type_fileshare && msgtype != message_type_screenshare
           && msgtype != message_type_group_invite && msgtype != message_type_rtc
           && msgtype != message_type_announcement && msgtype != message_type_memo
           && msgtype != message_type_cloudrecord;
}

bool MessageProtocol::isClientSendableMsgType(int msgtype)
{
    return isClientOperableMsgType(msgtype) && msgtype != message_type_system;
}

bool MessageProtocol::isValidVoiceContent(const std::string& content)
{
    Json::Value root;
    if (!parseObject(content, root))
        return false;

    //fileid：十进制字符串形式的正整数文件ID（与其它带文件的消息相同）
    const Json::Value& fileidValue = root["fileid"];
    int64_t fileid = 0;
    if (!fileidValue.isString() || !parseFileIdText(fileidValue.asString(), fileid))
        return false;

    //md5：非空字符串，接收方下载后据此校验文件
    const Json::Value& md5Value = root["md5"];
    if (!md5Value.isString() || md5Value.asString().empty())
        return false;

    //duration：整秒，1~120
    const Json::Value& durationValue = root["duration"];
    if (!durationValue.isInt())
        return false;
    const int duration = durationValue.asInt();
    if (duration < VOICE_DURATION_MIN_SECONDS || duration > VOICE_DURATION_MAX_SECONDS)
        return false;

    //size：文件字节数，1~VOICE_FILE_MAX_BYTES
    const Json::Value& sizeValue = root["size"];
    if (!sizeValue.isInt64())
        return false;
    const int64_t size = sizeValue.asInt64();
    if (size < 1 || size > VOICE_FILE_MAX_BYTES)
        return false;

    //wave：可选；有则必须是标准 Base64（带填充），解码后不超过 VOICE_WAVE_MAX_BYTES 字节。
    //sodium_base642bin 遇到非法字符、填充不对或解码结果放不进缓冲区时都返回 -1
    if (root.isMember("wave"))
    {
        const Json::Value& waveValue = root["wave"];
        if (!waveValue.isString())
            return false;
        const std::string wave = waveValue.asString();
        unsigned char decoded[VOICE_WAVE_MAX_BYTES];
        size_t decodedLen = 0;
        if (sodium_base642bin(decoded, sizeof(decoded), wave.c_str(), wave.size(), NULL, &decodedLen, NULL,
                              sodium_base64_VARIANT_ORIGINAL) != 0)
            return false;
    }
    return true;
}

int MessageProtocol::checkSendContent(int msgtype, const std::string& content)
{
    //语音：字段与取值范围不合格的一律按请求格式错误处理
    if (msgtype == MSGTYPE_VOICE && !isValidVoiceContent(content))
        return error_code_bad_request;
    return error_code_ok;
}

bool MessageProtocol::isForwardableSource(const MessageRecord& src)
{
    if (src.recalled)
        return false;
    return isClientOperableMsgType(src.msgtype);
}

size_t MessageProtocol::trimMessagesToBudget(std::vector<MessageRecord>& messages, int64_t cid,
                                             bool keepTail, size_t budgetBytes)
{
    const size_t total = messages.size();
    if (total == 0)
        return 0;

    //从锚点一侧逐条累计，数出能留下几条；第一条无论多大都留下
    size_t usedBytes = 0;
    size_t keep = 0;
    for (size_t n = 0; n < total; ++n)
    {
        const size_t idx = keepTail ? (total - 1 - n) : n;
        const size_t bodyBytes = writeCompact(messageBodyValue(messages[idx], cid)).size();
        if (keep > 0 && usedBytes + bodyBytes > budgetBytes)
            break;
        usedBytes += bodyBytes;
        ++keep;
    }

    const size_t removed = total - keep;
    if (removed == 0)
        return 0;

    if (keepTail)
        messages.erase(messages.begin(), messages.begin() + static_cast<std::ptrdiff_t>(removed));
    else
        messages.resize(keep);
    return removed;
}

std::string MessageProtocol::buildSyncMessagesResponseWithinBudget(int code, const std::string& msg,
                                                                   int ctype, int64_t cid, bool& hasMore,
                                                                   std::vector<MessageRecord>& messages,
                                                                   int64_t peerReadMsgid,
                                                                   const SyncMessagesRequest* anchorReq,
                                                                   bool keepTail, size_t budgetBytes,
                                                                   size_t& trimmed)
{
    trimmed = 0;
    std::string response = buildSyncMessagesResponse(code, msg, ctype, cid, hasMore, messages,
                                                      peerReadMsgid, anchorReq);
    //整个应答都没超过预算：各条消息体按同一种紧凑格式序列化后的体积之和比整个应答还小，
    //trimMessagesToBudget 一条也不会删，这一份就是最终结果
    if (response.size() <= budgetBytes)
        return response;

    trimmed = trimMessagesToBudget(messages, cid, keepTail, budgetBytes);
    if (trimmed == 0)
        return response;

    hasMore = true;
    return buildSyncMessagesResponse(code, msg, ctype, cid, hasMore, messages, peerReadMsgid, anchorReq);
}

bool MessageProtocol::parseSyncRewrittenRequest(const std::string& json, SyncRewrittenRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    //since：字符串编码的 64 位序号；缺省、类型不对或为负时从头取
    req.since = getStringId(root, "since");
    if (req.since < 0)
        req.since = 0;

    //limit：缺省或不大于 0 时取上限，超过上限时截到上限
    req.limit = root["limit"].isInt() ? root["limit"].asInt() : 0;
    if (req.limit <= 0 || req.limit > REWRITTEN_SYNC_LIMIT_MAX)
        req.limit = REWRITTEN_SYNC_LIMIT_MAX;
    return true;
}

int64_t MessageProtocol::conversationIdForViewer(const MessageRecord& rec, int64_t viewerUserid)
{
    if (rec.ctype == conversation_group)
        return rec.to;
    return (rec.from == viewerUserid) ? rec.to : rec.from;
}

std::string MessageProtocol::buildSyncRewrittenResponseWithinBudget(int code, const std::string& msg,
                                                                    int64_t viewerUserid, int64_t since,
                                                                    std::vector<RewrittenMessage>& items,
                                                                    bool& more, int64_t currev,
                                                                    size_t budgetBytes, size_t& trimmed)
{
    trimmed = 0;
    Json::Value arr(Json::arrayValue);
    size_t usedBytes = 0;
    size_t keep = 0;
    for (size_t i = 0; i < items.size(); ++i)
    {
        Json::Value body = messageBodyValue(items[i].record, conversationIdForViewer(items[i].record, viewerUserid));
        body["rev"] = std::to_string(items[i].rev);
        //从序号小的一端逐条累计；第一项无论多大都保留
        const size_t bodyBytes = writeCompact(body).size();
        if (keep > 0 && usedBytes + bodyBytes > budgetBytes)
            break;
        usedBytes += bodyBytes;
        arr.append(body);
        ++keep;
    }
    if (keep < items.size())
    {
        trimmed = items.size() - keep;
        items.resize(keep);
        more = true;
    }

    Json::Value root;
    root["code"]   = code;
    root["msg"]    = msg;
    root["items"]  = arr;
    root["rev"]    = std::to_string(items.empty() ? since : items.back().rev);
    root["more"]   = more;
    root["currev"] = std::to_string(currev);
    return writeCompact(root);
}

std::string MessageProtocol::buildGetPinnedMessagesResponseWithinBudget(int code, const std::string& msg,
                                                                        int ctype, int64_t cid, int64_t total,
                                                                        std::vector<MessageRecord>& messages,
                                                                        std::vector<PinRecord>& pins,
                                                                        size_t budgetBytes)
{
    //pins 与 messages 一一对应，不应比 messages 长；原先的做法在截断之后才把 pins 截短，
    //没有发生截断时那一步同样会生效，所以这里先截一次，保证两种做法的结果在任何输入下都一致
    if (pins.size() > messages.size())
        pins.resize(messages.size());

    std::string response = buildGetPinnedMessagesResponse(code, msg, ctype, cid, total, messages, pins);
    //理由同 buildSyncMessagesResponseWithinBudget：每一项都不比单独的消息体小，整页没超过预算就不会截断
    if (response.size() <= budgetBytes)
        return response;

    const size_t trimmed = trimMessagesToBudget(messages, cid, false, budgetBytes);
    if (trimmed == 0)
        return response;

    if (pins.size() > messages.size())
        pins.resize(messages.size());
    return buildGetPinnedMessagesResponse(code, msg, ctype, cid, total, messages, pins);
}

std::string MessageProtocol::buildMergedContent(const std::string& title,
                                                const std::vector<MessageRecord>& subs,
                                                const std::map<int64_t, std::string>& fromNames)
{
    Json::Value root;
    root["title"] = title;

    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < subs.size(); ++i)
    {
        Json::Value body = messageBodyValue(subs[i], subs[i].from);
        std::map<int64_t, std::string>::const_iterator it = fromNames.find(subs[i].from);
        if (it != fromNames.end() && !it->second.empty())
            body["fromname"] = it->second;
        arr.append(body);
    }
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

bool MessageProtocol::parseDeleteMessagesByTimeRequest(const std::string& json,
                                                       DeleteMessagesByTimeRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    //两个时间都必须是 JSON 整数。jsoncpp 的 isInt64 对缺失字段、字符串、带小数的
    //数字都返回 false，三种不合法情形由这一项判断一并排除
    const Json::Value& beginValue = root["begintime"];
    const Json::Value& endValue = root["endtime"];
    if (!beginValue.isInt64() || !endValue.isInt64())
        return false;

    const int64_t begintime = beginValue.asInt64();
    const int64_t endtime = endValue.asInt64();
    if (begintime < 0 || endtime < 0)
        return false;

    req.begintime = begintime;
    req.endtime = endtime;
    return true;
}

std::string MessageProtocol::buildDeleteMessagesByTimeResponse(int code, const std::string& msg,
                                                               int64_t begintime, int64_t endtime)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;
    //时间与消息体的 timestamp 同一口径按数字输出：毫秒值远小于 2^53，客户端不会丢精度
    root["begintime"] = static_cast<Json::Int64>(begintime);
    root["endtime"]   = static_cast<Json::Int64>(endtime);
    return writeCompact(root);
}

std::string MessageProtocol::buildMessagesDeletedNotify(int ctype, int64_t cid,
                                                        const std::vector<int64_t>& msgids)
{
    Json::Value root;
    root["ctype"] = ctype;
    root["cid"]   = std::to_string(cid);

    //msgid 可能超过 2^53，与协议里其它 64 位 ID 一样按字符串输出
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < msgids.size(); ++i)
        arr.append(std::to_string(msgids[i]));
    root["msgids"] = arr;
    return writeCompact(root);
}

std::string MessageProtocol::buildMessagesDeletedRangeNotify(int64_t begintime, int64_t endtime)
{
    //不带 msgids：客户端以是否带 msgids 区分单条形式与区间形式
    Json::Value root;
    root["begintime"] = static_cast<Json::Int64>(begintime);
    root["endtime"]   = static_cast<Json::Int64>(endtime);
    return writeCompact(root);
}

bool MessageProtocol::parseClearConversationRequest(const std::string& json,
                                                    ClearConversationRequest& req)
{
    Json::Value root;
    if (!parseObject(json, root))
        return false;

    //会话类型必须明确给出，只接受单聊与群聊
    if (!root["ctype"].isInt())
        return false;
    const int ctype = root["ctype"].asInt();
    if (ctype != conversation_single && ctype != conversation_group)
        return false;

    const int64_t cid = getStringId(root, "cid");
    if (cid <= 0)
        return false;

    req.ctype = ctype;
    req.cid = cid;
    return true;
}

std::string MessageProtocol::buildClearConversationResponse(int code, const std::string& msg,
                                                            int ctype, int64_t cid,
                                                            int64_t clearedmsgid)
{
    Json::Value root;
    root["code"]  = code;
    root["msg"]   = msg;
    root["ctype"] = ctype;
    //ID 一律按字符串输出，避免超过 2^53 时在客户端丢精度
    root["cid"]          = std::to_string(cid);
    root["clearedmsgid"] = std::to_string(clearedmsgid);
    return writeCompact(root);
}

std::string MessageProtocol::buildMessagesClearedNotify(int ctype, int64_t cid, int64_t clearedmsgid)
{
    //不带 msgids 与时间：客户端以 clearedmsgid 字段识别清空形式
    Json::Value root;
    root["ctype"]        = ctype;
    root["cid"]          = std::to_string(cid);
    root["clearedmsgid"] = std::to_string(clearedmsgid);
    return writeCompact(root);
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
