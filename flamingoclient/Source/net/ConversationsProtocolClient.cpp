/**
 *  v2 会话列表报文编解码（客户端侧）实现，ConversationsProtocolClient.cpp
 *  balloonwj@qq.com   2026-06-02
 */
#include "ConversationsProtocolClient.h"
#include <stdlib.h>                     // strtoll
#include "../third-party/jsoncpp-1.9.0/json.h"

namespace net {

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
//务必用本函数读 pinned / mute / deleted 这类开关字段，不要写成
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
            //pinned / mute 服务端写的是 bool（ConversationBrief 的字段类型即 bool），
            //必须走 jsonToFlag —— 用 isInt() 读会恒得 0，置顶 / 免打扰重登后就丢了。
            item.m_pinned    = jsonToFlag(jc["pinned"]);
            item.m_mute      = jsonToFlag(jc["mute"]);
            item.m_lastmsgid = jsonToInt64(jc["lastmsgid"]);

            //最后一条消息（lastmsg 对象）：发送方 / 类型 / 预览 / 时间。
            const Json::Value& last = jc["lastmsg"];
            if (last.isObject())
            {
                item.m_lastFrom    = jsonToInt64(last["from"]);
                item.m_lastMsgType = last["msgtype"].isInt() ? last["msgtype"].asInt() : 0;
                item.m_digest      = last["digest"].isString() ? last["digest"].asString() : std::string();
                item.m_timestamp   = last["timestamp"].isNumeric() ? last["timestamp"].asInt64() : 0;
            }

            //cid 非法（缺失/为 0）的会话跳过
            if (item.m_cid <= 0)
                continue;
            out.m_items.push_back(item);
        }
    }

    return true;
}

std::string ConversationsProtocolClient::buildSetConversationRequest(int ctype, int64_t cid,
                                                                     bool hasPinned, bool pinned,
                                                                     bool hasMute, bool mute)
{
    Json::Value root;
    root["ctype"] = ctype;
    //cid 以字符串编码，避免大整数精度丢失。
    root["cid"] = std::to_string(cid);
    //pinned / mute 仅在本次要改时写入，与服务端"只更新出现的字段"语义对齐；
    //值为 bool（服务端按 isBool 取值）。
    if (hasPinned)
        root["pinned"] = pinned;
    if (hasMute)
        root["mute"] = mute;
    return writeCompact(root);
}

std::string ConversationsProtocolClient::buildDeleteConversationRequest(int ctype, int64_t cid)
{
    Json::Value root;
    root["ctype"] = ctype;
    //cid 以字符串编码，避免大整数精度丢失（与 buildSetConversationRequest 同口径）。
    root["cid"] = std::to_string(cid);
    //只写 deleted 一个字段：服务端按"只更新出现的字段"处理，故置顶 / 免打扰不受影响。
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
    out.m_pinned  = jsonToFlag(root["pinned"]);
    out.m_mute    = jsonToFlag(root["mute"]);
    out.m_deleted = jsonToFlag(root["deleted"]);
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

bool ConversationsProtocolClient::shouldCountAsUnread(unsigned int fromUserId, unsigned int selfId)
{
    //取不到本端 userid（未登录 / 兜底态）时不敢断言消息来自他人，一律不计未读。
    if (selfId == 0)
    {
        return false;
    }

    //自己发出、又被服务端回推回来的消息（群消息广播给全体成员、单聊多端同步）不计未读。
    return fromUserId != selfId;
}

} // namespace net
