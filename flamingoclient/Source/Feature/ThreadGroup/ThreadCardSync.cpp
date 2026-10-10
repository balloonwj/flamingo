/**
 *  帖子卡片改写后同步到本机，见 ThreadCardSync.h。
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "ThreadCardSync.h"

#include <stdlib.h>

#include "../../net/MessageProtocolClient.h"   // MessageNotify / parseThreadCardContent / threadCardDigest
#include "../../net/Protocol.h"                // message_type_thread_post / message_type_thread_reply / conversation_group
#include "../../UI/BalloonHistory/V2MsgQuery.h"   // HistoryRecord / BuildContentJson / BuildSearchText
#include "../../UI/BalloonHistory/V2MsgStore.h"   // CV2MsgStore::QueryRecord / Upsert
#include "../../third-party/jsoncpp-1.9.0/json.h"

namespace balloonthread {

namespace {

//把 JSON 文本解析为对象；失败或不是对象返回 false
bool ParseObject(const std::string& text, Json::Value& out)
{
    if (text.empty())
        return false;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &out, &errs);
    delete reader;
    return ok && out.isObject();
}

//紧凑写出 JSON（无缩进、无换行），与 MessageProtocolClient 保存原始 JSON 时的写法一致
std::string WriteCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//从 JSON 值里取 64 位 ID：十进制字符串与整数两种写法都接受，其余返回 0
int64_t ValueToId(const Json::Value& v)
{
    if (v.isString())
        return static_cast<int64_t>(_strtoi64(v.asString().c_str(), NULL, 10));
    if (v.isIntegral())
        return v.asInt64();
    return 0;
}

//是不是帖子卡片或帖子动态卡片
bool IsThreadCardType(int msgtype)
{
    return msgtype == message_type_thread_post || msgtype == message_type_thread_reply;
}

} // namespace

bool ParseThreadCards(const std::string& body, int64_t& groupId, std::vector<ThreadCardUpdate>& out)
{
    groupId = 0;
    out.clear();
    Json::Value root;
    if (!ParseObject(body, root))
        return false;
    //2721 推送不带 code；带 code 的是 2716 应答的格式（客户端已不再发 2716），不为 0 时整体不要
    if (root.isMember("code") && (!root["code"].isIntegral() || root["code"].asInt() != 0))
        return false;
    groupId = ValueToId(root["groupid"]);
    if (groupId <= 0)
        return false;
    const Json::Value& cards = root["cards"];
    if (!cards.isArray())
        return true;
    for (Json::ArrayIndex i = 0; i < cards.size(); ++i)
    {
        const Json::Value& item = cards[i];
        if (!item.isObject() || !item["content"].isObject() || !item["msgtype"].isIntegral())
            continue;
        ThreadCardUpdate card;
        card.m_msgid = ValueToId(item["msgid"]);
        card.m_msgtype = item["msgtype"].asInt();
        if (card.m_msgid <= 0 || !IsThreadCardType(card.m_msgtype))
            continue;
        card.m_content = WriteCompact(item["content"]);
        out.push_back(card);
    }
    return true;
}

std::string ThreadCardDigestOf(int msgtype, const std::string& content)
{
    std::string arg;
    int kind = 0;
    int64_t postId = 0;
    bool deleted = false;
    if (!net::MessageProtocolClient::parseThreadCardContent(msgtype, content, arg, kind, postId, deleted))
        return std::string();
    return net::MessageProtocolClient::threadCardDigest(msgtype, kind, arg, deleted);
}

bool ApplyThreadCardToStore(balloonhistory::CV2MsgStore& store, int64_t groupId, const ThreadCardUpdate& card,
                            bool& outHit)
{
    outHit = false;
    Json::Value content;
    if (groupId <= 0 || card.m_msgid <= 0 || !IsThreadCardType(card.m_msgtype) || !ParseObject(card.m_content, content))
        return false;

    //一、按主键取出本地那一行。本地没有：本机没同步过这段历史，属正常，以后同步时服务端给的就是改写后的卡片
    balloonhistory::HistoryRecord row;
    bool found = false;
    if (!store.QueryRecord(conversation_group, groupId, card.m_msgid, row, found))
        return false;
    if (!found)
        return true;
    if (!IsThreadCardType(row.m_msgtype))
        return false;

    //二、原始 JSON 里的 content 换成新卡片：从本地库还原消息时解析的就是它，原文不能留在这里
    Json::Value raw;
    if (ParseObject(row.m_raw, raw))
    {
        raw["content"] = content;
        row.m_raw = WriteCompact(raw);
    }

    //三、content 列与搜索文本按新卡片重新生成，格式与入库时相同（BuildContentJson / BuildSearchText）
    net::MessageNotify forColumn;
    forColumn.m_msgtype = row.m_msgtype;
    forColumn.m_threadCardContent = card.m_content;
    row.m_content = balloonhistory::BuildContentJson(forColumn);
    row.m_searchText = balloonhistory::BuildSearchText(forColumn);

    //四、整行写回：置顶、撤回、已听等其余列取自上面读出的那一行，原样保留
    if (!store.Upsert(row))
        return false;
    outHit = true;
    return true;
}

} // namespace balloonthread
