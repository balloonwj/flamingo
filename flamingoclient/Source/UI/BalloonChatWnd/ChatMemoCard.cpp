/**
 *  ChatMemoCard.cpp
 *  群备忘卡片的解析与套用，说明见 ChatMemoCard.h。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "ChatMemoCard.h"

#include "ChatCardModel.h"   // SetCardChip / ChatCardChipTone：卡片的状态标签

#include <stdlib.h>

#include "../../third-party/jsoncpp-1.9.0/json.h"
#include "../../net/Protocol.h"                  // memo_card_change
#include "../../net/MemoProtocolClient.h"        // net::MemoItem / MemoProtocolClient::parseStart
#include "../../Feature/Memo/MemoRule.h"         // memo::LocalMinuteFromStart
#include "../BalloonMemo/MemoViewModel.h"        // EventTimeText / RepeatText / TargetsText
#include "../../Common/I18n/I18n.h"              // TR / TRF：界面文字的多语言查找

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

//取整数字段；缺失或不是整数时为 fallback
int IntField(const Json::Value& root, const char* key, int fallback)
{
    return root[key].isInt() ? root[key].asInt() : fallback;
}

//取字符串字段并转成 UTF-16；缺失或不是字符串时为空
CString StringField(const Json::Value& root, const char* key)
{
    return root[key].isString() ? Utf8ToWide(root[key].asString()) : CString();
}

//把卡片内容填成一个群备忘对象，供 MemoViewModel 的文字函数使用（它们都以 net::MemoItem 为参数）。
//卡片只带被提醒的人数、不带名单，m_targets 填同样多个占位的 0，人数相同即可
net::MemoItem ToMemoItem(const MemoCardInfo& info)
{
    net::MemoItem item;
    item.m_memoid = info.m_memoId;
    item.m_ctype = net::kMemoCtypeGroup;
    item.m_groupid = info.m_groupId;
    item.m_start = info.m_start;
    item.m_advance = info.m_advance;
    item.m_repeat = info.m_repeat;
    item.m_weekdays = info.m_weekdays;
    item.m_count = info.m_count;
    item.m_targetAll = info.m_targetAll;
    if (!info.m_targetAll && info.m_targetCount > 0)
        item.m_targets.assign(static_cast<size_t>(info.m_targetCount), 0);
    return item;
}

} // namespace

bool ParseMemoCard(const std::string& contentJson, MemoCardInfo& out)
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

    out.m_memoId = ToInt64(root["memoid"]);
    if (out.m_memoId <= 0)
        return false;
    out.m_groupId = ToInt64(root["groupid"]);
    out.m_change = IntField(root, "change", memo_card_modified);
    out.m_title = StringField(root, "title");
    out.m_note = StringField(root, "note");
    out.m_start = root["start"].isString() ? net::MemoProtocolClient::parseStart(root["start"].asString()) : 0;
    out.m_advance = IntField(root, "advance", 0);
    out.m_repeat = IntField(root, "repeat", 0);
    out.m_weekdays = IntField(root, "weekdays", 0);
    out.m_count = IntField(root, "count", 1);
    out.m_targetAll = root["targetall"].isBool() && root["targetall"].asBool();
    out.m_targetCount = IntField(root, "targetcount", 0);
    out.m_editorId = ToInt64(root["editorid"]);
    out.m_editorName = StringField(root, "editor");
    return true;
}

CString MemoCardSubtitle(const CString& editorName, bool fromMe, int change)
{
    const bool created = (change == memo_card_created);
    if (fromMe)
        return created ? TR(L"我新建了群备忘") : TR(L"我修改了群备忘");
    if (editorName.IsEmpty())
        return created ? TR(L"新建了一条群备忘") : TR(L"修改了一条群备忘");
    if (created)
        return TRF(L"{0}新建了群备忘", editorName).c_str();
    return TRF(L"{0}修改了群备忘", editorName).c_str();
}

CString MemoCardTimeLine(const MemoCardInfo& info, int64_t now)
{
    const net::MemoItem item = ToMemoItem(info);
    const CString repeat = balloonmemo::RepeatText(item).c_str();
    memo::LocalMinute event = 0;
    if (info.m_start <= 0 || !memo::LocalMinuteFromStart(info.m_start, event))
        return repeat;
    //分隔符与群「备忘」页签卡片的规则行相同（MemoViewModel 里的 L" · "），不经翻译
    CString line = balloonmemo::EventTimeText(event, static_cast<memo::LocalMinute>(now)).c_str();
    line += L" · ";
    line += repeat;
    return line;
}

CString MemoCardTargetsText(const MemoCardInfo& info)
{
    return balloonmemo::TargetsText(ToMemoItem(info)).c_str();
}

void ApplyMemoCard(ChatMessage& msg, const std::string& contentJson, const CString& editorName, int64_t now)
{
    MemoCardInfo info;
    const bool ok = ParseMemoCard(contentJson, info);

    msg.kind = kMsgScreenShare;
    msg.screenShareIsMemo = true;
    msg.screenShareIsAnnouncement = false;
    msg.screenShareIsGroupCall = false;
    msg.screenShareIsCallCard = false;
    msg.groupCallMedia = 0;
    msg.screenShareContent = contentJson;
    msg.screenShareRoomId.clear();
    msg.screenShareForced = false;
    msg.screenShareState = 0;
    msg.screenShareNotice.Empty();
    msg.infoCardRefId = ok ? info.m_memoId : 0;

    if (!ok)
    {
        //内容不可用：只写类型，不可点
        msg.text = TR(L"[群备忘]");
        msg.screenShareTitle = TR(L"群备忘");
        msg.screenShareHostName.Empty();
        msg.screenShareSubtitle.Empty();
        msg.screenShareDesc.Empty();
        msg.screenShareStatus = TR(L"备忘内容不可用");
        msg.screenShareHasButton = false;
        msg.screenShareButtonEnabled = false;
        SetCardChip(msg, msg.screenShareStatus, kCardChipEnded);
        return;
    }

    const CString name = !editorName.IsEmpty() ? editorName : info.m_editorName;
    msg.text = info.m_title.IsEmpty() ? CString(TR(L"[群备忘]"))
                                      : CString(TRF(L"[群备忘] {0}", info.m_title).c_str());
    msg.screenShareTitle = info.m_title.IsEmpty() ? CString(TR(L"群备忘")) : info.m_title;
    msg.screenShareHostName = name;
    msg.screenShareSubtitle = MemoCardSubtitle(name, msg.fromMe, info.m_change);
    //描述块：第一行时间与重复规则，第二行备注（ChatBubble 按换行分段、至多两行）
    CString desc = MemoCardTimeLine(info, now);
    if (!info.m_note.IsEmpty())
    {
        desc += L"\n";
        desc += info.m_note;
    }
    msg.screenShareDesc = desc;
    msg.screenShareStatus = MemoCardTargetsText(info);
    msg.screenShareHasButton = true;
    msg.screenShareButtonEnabled = true;
    //提醒对象用蓝色的中性标签（docs/聊天卡片美化方案.md 5.2）
    SetCardChip(msg, msg.screenShareStatus, kCardChipNeutral);
}

bool IsMemoCardAvailable(bool bookLoaded, bool inBook)
{
    return !bookLoaded || inBook;
}

bool SetMemoCardAvailable(ChatMessage& msg, bool available)
{
    if (msg.kind != kMsgScreenShare || !msg.screenShareIsMemo || msg.infoCardRefId <= 0)
        return false;
    if (msg.screenShareButtonEnabled == available)
        return false;

    msg.screenShareButtonEnabled = available;
    if (!available)
    {
        //本机的备忘簿里没有这一条：备忘被删除，与本人被移出提醒对象在客户端分不出来，合用一句
        msg.screenShareStatus = TR(L"备忘已删除，或你已不在提醒对象中");
        SetCardChip(msg, msg.screenShareStatus, kCardChipEnded);
        return true;
    }
    MemoCardInfo info;
    msg.screenShareStatus = ParseMemoCard(msg.screenShareContent, info) ? MemoCardTargetsText(info) : CString();
    SetCardChip(msg, msg.screenShareStatus, kCardChipNeutral);
    return true;
}

bool IsMemoCardOpenable(const ChatMessage& msg)
{
    return msg.kind == kMsgScreenShare && msg.screenShareIsMemo && msg.infoCardRefId > 0 && msg.screenShareButtonEnabled;
}

} // namespace balloonchatwnd
