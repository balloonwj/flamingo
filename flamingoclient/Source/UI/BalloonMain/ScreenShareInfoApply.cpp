/**
 *  ScreenShareInfoApply.cpp —— 共享标题与描述变更推送（1734）写进本地消息库，见 ScreenShareInfoApply.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用。
 *  在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "ScreenShareInfoApply.h"

#include <stdlib.h>                              // strtoll
#include <memory>
#include "../BalloonHistory/V2MsgQuery.h"        // BuildContentJson
#include "../BalloonHistory/V2MsgStore.h"        // CV2MsgStore::UpdateContent
#include "../../net/MessageProtocolClient.h"     // net::MessageNotify（拼 content 列用）
#include "../../net/Protocol.h"                  // message_type_screenshare、conversation_single / conversation_group
#include "../../third-party/jsoncpp-1.9.0/json.h"

namespace balloonmain {

namespace {

//十进制
const int kDecimalBase = 10;

//读一个 64 位 id：字符串（服务端的写法）或整数都接受；空串、带非数字字符或不是这两种类型时返回 false
bool readInt64(const Json::Value& v, int64_t& out)
{
    if (v.isString())
    {
        const std::string text = v.asString();
        if (text.empty())
            return false;
        char* end = NULL;
        const long long n = strtoll(text.c_str(), &end, kDecimalBase);
        if (end == NULL || *end != '\0')
            return false;
        out = static_cast<int64_t>(n);
        return true;
    }
    if (v.isInt64())
    {
        out = static_cast<int64_t>(v.asInt64());
        return true;
    }
    return false;
}

} // namespace

ScreenShareInfoUpdate::ScreenShareInfoUpdate()
    : m_ctype(0)
    , m_cid(0)
    , m_msgid(0)
    , m_hostId(0)
{
}

bool ParseScreenShareInfoUpdate(const std::string& json, ScreenShareInfoUpdate& out)
{
    Json::Value root;
    Json::CharReaderBuilder builder;
    JSONCPP_STRING errs;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs) || !root.isObject())
        return false;
    if (!root["roomid"].isString() || root["roomid"].asString().empty() || !root["ctype"].isInt())
        return false;
    out.m_roomId = root["roomid"].asString();
    out.m_ctype = root["ctype"].asInt();
    if (out.m_ctype != conversation_single && out.m_ctype != conversation_group)
        return false;
    if (!readInt64(root["cid"], out.m_cid) || out.m_cid <= 0)
        return false;
    //新的共享标题：群聊横幅据此刷新；不是字符串时按空处理（横幅沿用状态缓存里原来的标题）
    out.m_title = root["title"].isString() ? root["title"].asString() : std::string();
    //改写后的卡片：服务端没能改写时不带 msgid 与 content，仍算有效的推送（标题与描述照样在 DLL 里刷新）
    out.m_msgid = 0;
    out.m_content.clear();
    int64_t msgid = 0;
    if (readInt64(root["msgid"], msgid) && msgid > 0 && root["content"].isString())
    {
        out.m_msgid = msgid;
        out.m_content = root["content"].asString();
    }
    //新主持人：移交主持人后的那条推送才带；不是正数时按不带处理，推送本身仍有效
    out.m_hostId = 0;
    out.m_hostName.clear();
    int64_t hostId = 0;
    if (readInt64(root["hostid"], hostId) && hostId > 0)
    {
        out.m_hostId = hostId;
        out.m_hostName = root["hostname"].isString() ? root["hostname"].asString() : std::string();
    }
    return true;
}

bool ApplyScreenShareInfoToStore(balloonhistory::CV2MsgStore& store, const ScreenShareInfoUpdate& update, bool& outHit)
{
    outHit = false;
    //推送不带卡片：本地没有可改的内容
    if (update.m_msgid <= 0)
        return true;
    //新内容须是一张能解析的卡片，否则宁可保留旧内容
    net::ScreenShareCardContent card;
    if (!net::MessageProtocolClient::parseScreenShareCardContent(update.m_content, card))
        return false;
    //写回 content 列，格式与入库时相同（BuildContentJson 的 screenshare 子对象），原始JSON不动。
    //本地没有这张卡片时命中 0 行，属正常：下次同步会带回服务端改写后的内容
    net::MessageNotify forColumn;
    forColumn.m_msgtype = message_type_screenshare;
    forColumn.m_screenShareContent = update.m_content;
    bool hit = false;
    if (!store.UpdateContent(update.m_ctype, update.m_cid, update.m_msgid, balloonhistory::BuildContentJson(forColumn),
                             hit))
        return false;
    outHit = hit;
    return true;
}

} // namespace balloonmain
