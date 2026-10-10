/**
 *  v2 会话列表报文编解码（客户端侧）实现，ConversationsProtocolClient.cpp
 *  balloonwj@qq.com   2026-06-02
 */
#include "ConversationsProtocolClient.h"
#include "Protocol.h"                     // message_type_system、enum conversation_notify_mode
#include <stdlib.h>                     // strtoll
#include "../third-party/jsoncpp-1.9.0/json.h"
#include "MessageProtocolClient.h"   // digestForMsgType：类型摘要的本地文字
#include "SystemTipText.h"           // FormatSystemTip：系统提示按当前界面语言生成文字
#include "../Common/I18n/I18n.h"     // TR8

namespace net {

//把 JSON 值当开关取成 0 / 1（与下面的 jsonToFlag 相同，LocalizeDigest 定义在它之前，故另写一份）
static int jsonToFlagLocal(const Json::Value& v)
{
    if (v.isBool())
        return v.asBool() ? 1 : 0;
    if (v.isIntegral())
        return (v.asInt64() != 0) ? 1 : 0;
    return 0;
}

//按当前界面语言在本地生成会话列表摘要（多语言第 5 期）。服务端的 digest 是拼好的中文，这里能在本地生成的
//类型都换成当前语言：结构化系统提示按 tipkind / tipargs 生成；图片、文件等只写类型的摘要按类型生成；接龙、
//文件共享与入群邀请在类型后接 digestarg（标题 / 共享名 / 群名）。文字消息是用户写的内容、认不出的类型与没有结构化字段的系统
//提示保持服务端的 digest 不变。已撤回的摘要另由主面板按撤回者改写，调用方不对它调用本函数
static std::string LocalizeDigest(int msgtype, const std::string& serverDigest, const Json::Value& last)
{
    switch (msgtype)
    {
    //系统提示：只有带结构化字段的才能在本地生成
    case message_type_system:
    {
        std::string localized;
        if (last["tipkind"].isString() && last["tipargs"].isObject()
            && FormatSystemTip(last["tipkind"].asString(), last["tipargs"], localized))
        {
            return localized;
        }
        return serverDigest;
    }
    //接龙、文件共享、入群邀请、群公告、群备忘、云端录像：类型文字后接标题 / 共享名 / 群名，与 MessageProtocolClient::digestForNotify
    //的写法一致
    case message_type_relay:
    case message_type_fileshare:
    case message_type_group_invite:
    case message_type_announcement:
    case message_type_memo:
    case message_type_cloudrecord:
    {
        std::string digest = MessageProtocolClient::digestForMsgType(msgtype, std::string());
        if (last["digestarg"].isString() && !last["digestarg"].asString().empty())
        {
            digest += " ";
            digest += last["digestarg"].asString();
        }
        return digest;
    }
    //帖子卡片、帖子动态卡片：「发帖：标题」「回复：内容」等（docs/帖子群方案.md 6.2），参数取 digestarg、threadkind、threaddeleted
    case message_type_thread_post:
    case message_type_thread_reply:
        return MessageProtocolClient::threadCardDigest(msgtype, last["threadkind"].isIntegral() ? last["threadkind"].asInt() : 0,
                                                       last["digestarg"].isString() ? last["digestarg"].asString() : std::string(),
                                                       jsonToFlagLocal(last["threaddeleted"]) != 0);
    //只写类型的摘要：与服务端 buildMessageDigest 的措辞一致，按当前界面语言生成
    case message_type_image:
    case message_type_file:
    case message_type_voice:
    case message_type_emoji:
    case message_type_location:
    case message_type_card:
    case message_type_poll:
    case message_type_todo:
    case message_type_shake:
    case message_type_merged:
    case message_type_screenshare:
        return MessageProtocolClient::digestForMsgType(msgtype, std::string());
    //文字消息与其余类型：保持服务端的 digest
    default:
        return serverDigest;
    }
}

//把 JSON 值转成 int64：字符串按十进制解析，整型直接取，其它返回 0。
//v2 约定 64 位 ID（cid/msgid/from）以字符串编码进 JSON，避免精度丢失。
static int64_t jsonToInt64(const Json::Value& v)
{
    if (v.isString())
        return static_cast<int64_t>(strtoll(v.asString().c_str(), NULL, 10));
    if (v.isIntegral())
        return v.asInt64();
    return 0;
}

//把 JSON 值当"开关"取成 0/1：服务端写的是 bool，这里同时容忍整数 0/1 的写法，
//缺字段 / 类型不认识时取 0。
//
//务必用本函数读 pinned / deleted 这类开关字段，不要写成
//`v.isInt() ? v.asInt() : 0` —— jsoncpp 对布尔值的 isInt() 返回 false（实测
//`{"pinned":true}` 得 isBool=1、isInt=0、isIntegral=0），那种写法会把服务端发来的
//true 一律读成 0。曾因此出过 bug：置顶当场生效（走本地乐观更新 + 变更推送，推送
//这条路径用的就是本函数），重新登录拉会话列表却全部丢失。
static int jsonToFlag(const Json::Value& v)
{
    if (v.isBool())
        return v.asBool() ? 1 : 0;
    if (v.isIntegral())
        return (v.asInt64() != 0) ? 1 : 0;
    return 0;
}

//把 JSON 值当消息提醒模式取出：只认整数，且须在 enum conversation_notify_mode 的取值范围内；
//缺字段、类型不对或越界一律按 notify_mode_allow（允许提醒）处理 —— 不认识的模式宁可多提醒，
//也不能把用户可能关心的消息悄悄压下去。
static int jsonToNotifyMode(const Json::Value& v)
{
    if (!v.isInt())
        return notify_mode_allow;
    const int mode = v.asInt();
    if (mode < notify_mode_allow || mode > notify_mode_mention_follow)
        return notify_mode_allow;
    return mode;
}

//解析一条 {ctype, cid, mode}。ctype 不是单聊或群聊、cid 非正、mode 不是合法整数时返回 false，
//调用方跳过该条。1501 应答的 notifymodes 与 1525 推送的 items 共用。
static bool parseNotifyModeEntry(const Json::Value& jo, NotifyModeEntry& out)
{
    if (!jo.isObject())
        return false;
    if (!jo["ctype"].isInt() || !jo["mode"].isInt())
        return false;

    out.m_ctype = jo["ctype"].asInt();
    out.m_cid   = jsonToInt64(jo["cid"]);
    out.m_mode  = jo["mode"].asInt();

    if (out.m_ctype != conversation_single && out.m_ctype != conversation_group)
        return false;
    if (out.m_cid <= 0)
        return false;
    if (out.m_mode < notify_mode_allow || out.m_mode > notify_mode_mention_follow)
        return false;
    return true;
}

//解析一份群关注名单 {groupid, targets:[...]}。groupid 非正或 targets 不是数组时返回 false；
//targets 里非正的元素跳过。1501 应答的 groupfollows 与 1331 推送共用
static bool parseGroupFollowEntry(const Json::Value& jo, GroupFollowEntry& out)
{
    if (!jo.isObject())
        return false;

    out.m_groupid = jsonToInt64(jo["groupid"]);
    if (out.m_groupid <= 0)
        return false;

    const Json::Value& arr = jo["targets"];
    if (!arr.isArray())
        return false;

    out.m_targets.clear();
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        const int64_t id = jsonToInt64(arr[i]);
        if (id > 0)
            out.m_targets.push_back(static_cast<uint32_t>(id));
    }
    return true;
}

//把 JSON 文本解析成对象；不是合法的 JSON 对象时返回 false。
static bool parseObject(const std::string& jsonbody, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(jsonbody.c_str(), jsonbody.c_str() + jsonbody.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//把 Json::Value 序列化为紧凑（无缩进、无换行）的 JSON 文本。
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

bool ConversationsProtocolClient::parseSyncConversationsResponse(const std::string& jsonbody,
                                                                 ConversationsResult& out)
{
    Json::CharReaderBuilder b;
    Json::Value root;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(jsonbody.c_str(), jsonbody.c_str() + jsonbody.size(), &root, &errs);
    delete reader;
    if (!ok || !errs.empty() || !root.isObject())
        return false;

    //code 为必有字段，缺失或非整型视为非法应答
    if (!root["code"].isInt())
        return false;

    out.m_code = root["code"].asInt();
    out.m_msg  = root["msg"].isString() ? root["msg"].asString() : std::string();

    //失败应答到此为止
    if (out.m_code != 0)
        return true;

    out.m_synctime = root["synctime"].isNumeric() ? root["synctime"].asInt64() : 0;

    out.m_items.clear();
    const Json::Value& arr = root["conversations"];
    if (arr.isArray())
    {
        for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
        {
            const Json::Value& jc = arr[i];
            if (!jc.isObject())
                continue;

            ConversationItem item;
            item.m_ctype     = jc["ctype"].isInt()  ? jc["ctype"].asInt()  : 1;
            item.m_cid       = jsonToInt64(jc["cid"]);
            item.m_unread    = jc["unread"].isInt() ? jc["unread"].asInt() : 0;
            //pinned 服务端写的是 bool（ConversationBrief 的字段类型即 bool），
            //必须走 jsonToFlag —— 用 isInt() 读会恒得 0，置顶重登后就丢了。
            item.m_pinned    = jsonToFlag(jc["pinned"]);
            item.m_notifyMode = jsonToNotifyMode(jc["notifymode"]);
            //手动标为未读：服务端写 0 / 1，也接受布尔值；老服务端不带时为 false
            item.m_unreadMark = (jsonToFlag(jc["unreadmark"]) != 0);
            item.m_lastmsgid = jsonToInt64(jc["lastmsgid"]);

            //最后一条消息（lastmsg 对象）：发送方 / 类型 / 预览 / 时间。
            const Json::Value& last = jc["lastmsg"];
            if (last.isObject())
            {
                item.m_lastFrom    = jsonToInt64(last["from"]);
                item.m_lastMsgType = last["msgtype"].isInt() ? last["msgtype"].asInt() : 0;
                item.m_digest      = last["digest"].isString() ? last["digest"].asString() : std::string();
                item.m_timestamp   = last["timestamp"].isNumeric() ? last["timestamp"].asInt64() : 0;
                //服务端只在已撤回时写 recalled（bool），老服务端不带该字段，按未撤回处理
                item.m_lastRecalled = (jsonToFlag(last["recalled"]) != 0);
                //撤回者有记录时才带；群主 / 管理员撤回别人的消息时与 from 不同（2026-10-03 起）
                item.m_lastRecalledBy = jsonToInt64(last["recalledby"]);
                //帖子卡片、帖子动态卡片：记下帖子 ID，点会话时帖子群窗口定位到它
                item.m_threadPostId = jsonToInt64(last["threadpostid"]);
                //未撤回时按当前界面语言在本地生成摘要（多语言第 5 期）；已撤回的由主面板按撤回者改写
                if (!item.m_lastRecalled)
                    item.m_digest = LocalizeDigest(item.m_lastMsgType, item.m_digest, last);
            }

            //cid 非法（缺失/为 0）的会话跳过
            if (item.m_cid <= 0)
                continue;
            out.m_items.push_back(item);
        }
    }

    //本人全部非默认提醒模式的会话。坏条目逐条跳过，不影响其余条目
    out.m_notifyModes.clear();
    const Json::Value& modes = root["notifymodes"];
    if (modes.isArray())
    {
        for (Json::ArrayIndex i = 0; i < modes.size(); ++i)
        {
            NotifyModeEntry entry;
            if (parseNotifyModeEntry(modes[i], entry))
                out.m_notifyModes.push_back(entry);
        }
    }

    //本人在各群关注的成员。坏条目逐条跳过；名单为空的群服务端本来就不发，这里也不保留
    out.m_groupFollows.clear();
    const Json::Value& follows = root["groupfollows"];
    if (follows.isArray())
    {
        for (Json::ArrayIndex i = 0; i < follows.size(); ++i)
        {
            GroupFollowEntry entry;
            if (parseGroupFollowEntry(follows[i], entry) && !entry.m_targets.empty())
                out.m_groupFollows.push_back(entry);
        }
    }

    return true;
}

std::string ConversationsProtocolClient::buildSetConversationRequest(int ctype, int64_t cid,
                                                                     bool hasPinned, bool pinned,
                                                                     bool hasNotifyMode, int notifyMode)
{
    Json::Value root;
    root["ctype"] = ctype;
    //cid 以字符串编码，避免大整数精度丢失。
    root["cid"] = std::to_string(cid);
    //pinned / notifymode 仅在本次要改时写入，与服务端"只更新出现的字段"语义对齐。
    //pinned 为 bool（服务端按 isBool 取值），notifymode 为整数（服务端按 isInt 取值）。
    if (hasPinned)
        root["pinned"] = pinned;
    if (hasNotifyMode)
        root["notifymode"] = notifyMode;
    return writeCompact(root);
}

std::string ConversationsProtocolClient::buildDeleteConversationRequest(int ctype, int64_t cid)
{
    Json::Value root;
    root["ctype"] = ctype;
    //cid 以字符串编码，避免大整数精度丢失（与 buildSetConversationRequest 同口径）。
    root["cid"] = std::to_string(cid);
    //只写 deleted 一个字段：服务端按"只更新出现的字段"处理，故置顶 / 提醒模式不受影响。
    //值必须是 bool——服务端用 root["deleted"].isBool() 取值，写成数字会被整条忽略。
    root["deleted"] = true;
    return writeCompact(root);
}

bool ConversationsProtocolClient::parseConversationChangedNotify(const std::string& jsonbody,
                                                                 ConversationChangedNotify& out)
{
    Json::CharReaderBuilder b;
    Json::Value root;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(jsonbody.c_str(), jsonbody.c_str() + jsonbody.size(), &root, &errs);
    delete reader;
    if (!ok || !errs.empty() || !root.isObject())
        return false;

    out.m_cid = jsonToInt64(root["cid"]);
    //cid 非法（缺失 / 为 0 / 为负）的推送无从对应到任何会话，直接丢弃。
    if (out.m_cid <= 0)
        return false;

    //ctype 缺失时按单聊处理，与 parseSyncConversationsResponse 同口径。
    out.m_ctype   = root["ctype"].isInt() ? root["ctype"].asInt() : 1;
    out.m_pinned     = jsonToFlag(root["pinned"]);
    out.m_notifyMode = jsonToNotifyMode(root["notifymode"]);
    out.m_deleted    = jsonToFlag(root["deleted"]);
    out.m_unreadMark = (jsonToFlag(root["unreadmark"]) != 0);
    return true;
}

std::string ConversationsProtocolClient::buildSetUnreadMarkRequest(int ctype, int64_t cid, bool mark)
{
    Json::Value root;
    root["ctype"] = ctype;
    root["cid"] = std::to_string(cid);
    //只写 unreadmark 一个字段，值必须是 bool：服务端用 isBool 取值，写成数字会被整条忽略
    root["unreadmark"] = mark;
    return writeCompact(root);
}

std::string ConversationsProtocolClient::buildSetGroupFollowRequest(int64_t groupid,
                                                                    const std::vector<uint32_t>& targets)
{
    Json::Value root;
    root["groupid"] = std::to_string(groupid);
    //targets 必须出现且为数组，清空名单时发空数组
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < targets.size(); ++i)
        arr.append(std::to_string(targets[i]));
    root["targets"] = arr;
    return writeCompact(root);
}

bool ConversationsProtocolClient::parseGroupFollowChangedNotify(const std::string& jsonbody,
                                                                GroupFollowEntry& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;
    return parseGroupFollowEntry(root, out);
}

std::string ConversationsProtocolClient::buildSetNotifyModesRequest(const std::vector<NotifyModeEntry>& items)
{
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < items.size(); ++i)
    {
        Json::Value jo;
        jo["ctype"] = items[i].m_ctype;
        //cid 以字符串编码，避免大整数精度丢失（与 buildSetConversationRequest 同口径）
        jo["cid"]   = std::to_string(items[i].m_cid);
        jo["mode"]  = items[i].m_mode;
        arr.append(jo);
    }

    Json::Value root;
    root["items"] = arr;
    return writeCompact(root);
}

void ConversationsProtocolClient::splitNotifyModeBatches(const std::vector<NotifyModeEntry>& items,
                                                         size_t batchMax,
                                                         std::vector<std::vector<NotifyModeEntry> >& out)
{
    out.clear();
    if (batchMax == 0)
        batchMax = kNotifyModeBatchMax;

    for (size_t begin = 0; begin < items.size(); begin += batchMax)
    {
        size_t end = begin + batchMax;
        if (end > items.size())
            end = items.size();
        out.push_back(std::vector<NotifyModeEntry>(items.begin() + begin, items.begin() + end));
    }
}

bool ConversationsProtocolClient::parseSetNotifyModesResponse(const std::string& jsonbody,
                                                              SetNotifyModesResult& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    //code 为必有字段，缺失或非整型视为非法应答
    if (!root["code"].isInt())
        return false;

    out.m_code = root["code"].asInt();
    out.m_msg  = root["msg"].isString() ? root["msg"].asString() : std::string();

    //逐条结果与请求条目一一对应，缺字段的条目也保留（取默认值），以免错位
    out.m_results.clear();
    const Json::Value& arr = root["results"];
    if (arr.isArray())
    {
        for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
        {
            const Json::Value& jo = arr[i];
            NotifyModeResult result;
            if (jo.isObject())
            {
                result.m_ctype = jo["ctype"].isInt() ? jo["ctype"].asInt() : 0;
                result.m_cid   = jsonToInt64(jo["cid"]);
                result.m_code  = jo["code"].isInt() ? jo["code"].asInt() : 0;
            }
            out.m_results.push_back(result);
        }
    }
    return true;
}

bool ConversationsProtocolClient::parseNotifyModesChangedNotify(const std::string& jsonbody,
                                                                NotifyModesChangedNotify& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root))
        return false;

    const Json::Value& arr = root["items"];
    if (!arr.isArray())
        return false;

    out.m_items.clear();
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        NotifyModeEntry entry;
        if (parseNotifyModeEntry(arr[i], entry))
            out.m_items.push_back(entry);
    }
    return true;
}

int ConversationsProtocolClient::computeTotalUnread(const std::vector<ConversationItem>& items)
{
    int total = 0;
    for (size_t i = 0; i < items.size(); ++i)
    {
        //未读数理论上非负；防御性地把负值（脏数据）按 0 处理，避免冲减总数。
        if (items[i].m_unread > 0)
        {
            total += items[i].m_unread;
        }
    }
    return total;
}

bool ConversationsProtocolClient::shouldCountAsUnread(unsigned int fromUserId, unsigned int selfId,
                                                      int msgtype)
{
    //取不到本端 userid（未登录 / 兜底态）时不敢断言消息来自他人，一律不计未读。
    if (selfId == 0)
    {
        return false;
    }

    //服务端生成的系统消息是群里的变动通知，不计未读（与服务端算会话未读数的口径一致）。
    if (msgtype == message_type_system)
    {
        return false;
    }

    //自己发出、又被服务端回推回来的消息（群消息广播给全体成员、单聊多端同步）不计未读。
    return fromUserId != selfId;
}

bool ConversationsProtocolClient::shouldCountAsUnread(unsigned int fromUserId, unsigned int selfId, int msgtype,
                                                      const std::string& tipKind, const std::string& tipArgs)
{
    //新成员本人的入群提示：系统消息里唯一计未读的一种
    if (selfId != 0 && msgtype == message_type_system && IsMemberJoinTipForUser(tipKind, tipArgs, selfId))
    {
        return true;
    }
    return shouldCountAsUnread(fromUserId, selfId, msgtype);
}

} // namespace net
