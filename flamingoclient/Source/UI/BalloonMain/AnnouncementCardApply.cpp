/**
 *  AnnouncementCardApply.cpp —— 群公告卡片被服务端改写为已删除形态后写进本地消息库，见 AnnouncementCardApply.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-05
 */

#include "AnnouncementCardApply.h"
#include "../BalloonHistory/V2MsgQuery.h"            // HistoryRecord / BuildContentJson / BuildSearchText
#include "../BalloonHistory/V2MsgStore.h"            // CV2MsgStore::QueryRecord / Upsert
#include "../../net/MessageProtocolClient.h"         // net::MessageNotify（拼 content 列与搜索文本用）
#include "../../net/Protocol.h"                      // message_type_announcement、conversation_group
#include "../../third-party/jsoncpp-1.9.0/json.h"

namespace balloonmain {

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

//紧凑写出 JSON，与 MessageProtocolClient 保存原始JSON时的写法一致（无缩进、无换行）
std::string WriteCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

} // namespace

bool ApplyAnnouncementCardToStore(balloonhistory::CV2MsgStore& store, int64_t groupId, int64_t msgid,
                                  const std::string& cardContent, bool& outHit)
{
    outHit = false;
    Json::Value card;
    if (groupId <= 0 || msgid <= 0 || !ParseObject(cardContent, card))
        return false;

    //一、按主键取出本地那一行。本地没有：本机没同步过这段历史，属正常，下次同步会带回改写后的卡片
    balloonhistory::HistoryRecord row;
    bool found = false;
    if (!store.QueryRecord(conversation_group, groupId, msgid, row, found))
        return false;
    if (!found)
        return true;
    if (row.m_msgtype != message_type_announcement)
        return false;

    //二、原始JSON里的 content 换成新卡片：从本地库还原时 NotifyFromRecord 解析的就是它，正文不能留在这里
    Json::Value raw;
    if (ParseObject(row.m_raw, raw))
    {
        raw["content"] = card;
        row.m_raw = WriteCompact(raw);
    }

    //三、content 列与搜索文本按新卡片重新生成，格式与入库时相同（BuildContentJson / BuildSearchText）
    net::MessageNotify forColumn;
    forColumn.m_msgtype             = message_type_announcement;
    forColumn.m_announcementContent = WriteCompact(card);
    row.m_content    = balloonhistory::BuildContentJson(forColumn);
    row.m_searchText = balloonhistory::BuildSearchText(forColumn);

    //四、整行写回：置顶、撤回、已听等其余列取自上面读出的那一行，原样保留
    if (!store.Upsert(row))
        return false;
    outHit = true;
    return true;
}

} // namespace balloonmain
