/**
 *  ChatAnnouncementCard.cpp
 *  群公告卡片的解析与套用，说明见 ChatAnnouncementCard.h。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "ChatAnnouncementCard.h"

#include "ChatCardModel.h"   // SetCardChip / ChatCardChipTone：卡片的状态标签

#include <stdlib.h>

#include "../../third-party/jsoncpp-1.9.0/json.h"
#include "../../net/Protocol.h"               // group_announcement_severity
#include "../../Common/I18n/I18n.h"           // TR / TRF：界面文字的多语言查找
#include "GroupMustRead.h"                    // MustReadSenderName / MustReadCardSubtitle：入群必读卡片的说法

namespace balloonchatwnd {

namespace {

CString Utf8ToWide(const std::string& s)
{
    if (s.empty())
        return CString();
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), NULL, 0);
    if (n <= 0)
        return CString();
    CString out;
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.GetBuffer(n), n);
    out.ReleaseBuffer(n);
    return out;
}

//取字符串或数值形式的 64 位整数；缺失或非法时为 0
long long ToInt64(const Json::Value& v)
{
    if (v.isString())
        return static_cast<long long>(_strtoi64(v.asCString(), NULL, 10));
    if (v.isInt64())
        return static_cast<long long>(v.asInt64());
    return 0;
}

//取字符串字段并转成 UTF-16；缺失或不是字符串时为空
CString StringField(const Json::Value& root, const char* key)
{
    return root[key].isString() ? Utf8ToWide(root[key].asString()) : CString();
}

} // namespace

bool ParseAnnouncementCard(const std::string& contentJson, AnnouncementCardInfo& out)
{
    if (contentJson.empty())
        return false;
    Json::Value root;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool parsed = reader->parse(contentJson.data(), contentJson.data() + contentJson.size(), &root, &errs);
    delete reader;
    if (!parsed || !root.isObject())
        return false;

    out.m_announcementId = ToInt64(root["announcementid"]);
    if (out.m_announcementId <= 0)
        return false;
    out.m_groupId = ToInt64(root["groupid"]);
    out.m_title = StringField(root, "title");
    out.m_body = StringField(root, "body");
    out.m_severity = root["severity"].isInt() ? root["severity"].asInt() : group_announcement_normal;
    out.m_publisherId = ToInt64(root["publisherid"]);
    out.m_publisherName = StringField(root, "publisher");
    out.m_publishTimeMs = ToInt64(root["publishtime"]);
    out.m_republish = root["republish"].isBool() && root["republish"].asBool();
    out.m_deleted = root["deleted"].isBool() && root["deleted"].asBool();
    out.m_mustRead = root["mustread"].isBool() && root["mustread"].asBool();
    //已删除形态按协议没有 body；即便服务端带了也不显示
    if (out.m_deleted)
        out.m_body.Empty();
    return true;
}

CString AnnouncementSeverityText(int severity)
{
    switch (severity)
    {
    //重要：群管理页与边栏的橙色徽章
    case group_announcement_important:
        return TR(L"重要");
    //紧急：群管理页与边栏的红色徽章
    case group_announcement_urgent:
        return TR(L"紧急");
    //通知（默认），以及认不出的取值
    default:
        return TR(L"通知");
    }
}

CString AnnouncementCardSubtitle(const CString& publisherName, bool fromMe, bool republish)
{
    if (fromMe)
        return republish ? TR(L"我重新发布的群公告") : TR(L"我发布的群公告");
    if (publisherName.IsEmpty())
        return TR(L"群公告");
    if (republish)
        return TRF(L"{0}重新发布的群公告", publisherName).c_str();
    return TRF(L"{0}发布的群公告", publisherName).c_str();
}

void ApplyAnnouncementCard(ChatMessage& msg, const std::string& contentJson, const CString& publisherName)
{
    AnnouncementCardInfo info;
    const bool ok = ParseAnnouncementCard(contentJson, info);

    msg.kind = kMsgScreenShare;
    msg.screenShareIsAnnouncement = true;
    msg.screenShareIsMemo = false;
    msg.screenShareIsGroupCall = false;
    msg.screenShareIsCallCard = false;
    msg.groupCallMedia = 0;
    msg.screenShareContent = contentJson;
    msg.screenShareRoomId.clear();
    msg.screenShareForced = false;
    msg.screenShareState = 0;
    msg.screenShareNotice.Empty();
    msg.infoCardRefId = ok ? info.m_announcementId : 0;

    if (!ok)
    {
        //内容不可用：只写类型，不可点
        msg.text = TR(L"[群公告]");
        msg.screenShareTitle = TR(L"群公告");
        msg.screenShareHostName.Empty();
        msg.screenShareSubtitle.Empty();
        msg.screenShareDesc.Empty();
        msg.screenShareStatus = TR(L"公告内容不可用");
        msg.screenShareHasButton = false;
        msg.screenShareButtonEnabled = false;
        SetCardChip(msg, msg.screenShareStatus, kCardChipEnded);
        return;
    }

    const CString name = !publisherName.IsEmpty() ? publisherName : info.m_publisherName;
    msg.text = info.m_title.IsEmpty() ? CString(TR(L"[群公告]"))
                                      : CString(TRF(L"[群公告] {0}", info.m_title).c_str());
    msg.screenShareTitle = info.m_title.IsEmpty() ? CString(TR(L"群公告")) : info.m_title;
    msg.screenShareHostName = name;
    msg.screenShareSubtitle = AnnouncementCardSubtitle(name, msg.fromMe, info.m_republish);
    //入群必读卡片以系统名义发出：发送人显示「入群必读」，副标题说明阅读后才能发言
    if (info.m_mustRead)
    {
        msg.screenShareHostName = MustReadSenderName();
        msg.screenShareSubtitle = MustReadCardSubtitle();
    }
    msg.screenShareHasButton = true;
    if (info.m_deleted)
    {
        //公告已被删除：正文已去掉，只剩标题；不能再点开
        msg.screenShareDesc.Empty();
        msg.screenShareStatus = TR(L"该公告已被删除");
        msg.screenShareButtonEnabled = false;
        SetCardChip(msg, msg.screenShareStatus, kCardChipEnded);
        return;
    }
    msg.screenShareDesc = info.m_body;
    msg.screenShareStatus = TRF(L"优先级：{0}", AnnouncementSeverityText(info.m_severity)).c_str();
    msg.screenShareButtonEnabled = true;
    //标签只写优先级本身：重要、紧急为红，通知为灰（docs/聊天卡片美化方案.md 5.2）
    const bool severe = (info.m_severity == group_announcement_important || info.m_severity == group_announcement_urgent);
    SetCardChip(msg, AnnouncementSeverityText(info.m_severity), severe ? kCardChipBad : kCardChipEnded);
}

bool IsAnnouncementCardOpenable(const ChatMessage& msg)
{
    return msg.kind == kMsgScreenShare && msg.screenShareIsAnnouncement && msg.infoCardRefId > 0
           && msg.screenShareButtonEnabled;
}

} // namespace balloonchatwnd
