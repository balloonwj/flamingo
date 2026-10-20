/**
 *  ChatGroupCallCard.cpp
 *  群通话卡片的解析与套用，说明见 ChatGroupCallCard.h。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "ChatGroupCallCard.h"

#include "ChatCardModel.h"                                   // ChatCardChipTone：卡片状态标签的语义

#include <stdlib.h>
#include "../../third-party/jsoncpp-1.9.0/json.h"
#include "../../Feature/GroupCall/GroupCallStatusCache.h"   // GroupCallRoomState
#include "../../Common/I18n/I18n.h"                         // TR / TRF：界面文字的多语言查找

namespace balloonchatwnd {

namespace {

//content 里 kind 字段的取值与群视频的媒体类型（docs/协议设计-v2.md 21.5）
const char* const kGroupCallKind = "groupcall";
const int kMediaVideo = 2;
//秒、分钟、小时之间的换算
const int64_t kSecondsPerMinute = 60;
const int64_t kSecondsPerHour = 3600;

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
int64_t ToInt64(const Json::Value& v)
{
    if (v.isString())
        return static_cast<int64_t>(_strtoi64(v.asCString(), NULL, 10));
    if (v.isInt64())
        return static_cast<int64_t>(v.asInt64());
    return 0;
}

//按卡片 content 与缓存状态写状态行，并决定按钮可不可点
void FillState(ChatMessage& msg, const GroupCallCardInfo& info, bool ok, int roomState, int joinedCount)
{
    msg.screenShareState = roomState;
    //卡片的状态标签与信息行（docs/聊天卡片美化方案.md 5.2）：正在通话为绿、信息行写人数，其余为灰、结束后信息行写时长
    msg.cardMetaText.Empty();
    msg.cardChipTone = kCardChipEnded;
    //服务端改写过的卡片：一定已结束，写上时长
    if (ok && info.m_ended)
    {
        const CString duration = FormatGroupCallCardDuration(info.m_durationSeconds);
        msg.screenShareStatus = TRF(L"通话已结束 · 时长 {0}", duration).c_str();
        msg.screenShareButtonEnabled = false;
        msg.cardChipText = TR(L"通话已结束");
        msg.cardMetaText = TRF(L"时长 {0}", duration).c_str();
        return;
    }
    switch (roomState)
    {
    //这一场就是该群进行中的群通话
    case balloongroupcall::kGroupCallRoomActive:
        msg.screenShareStatus = joinedCount > 0 ? CString(TRF(L"正在通话 · {0} 人", joinedCount).c_str())
                                                : CString(TR(L"正在通话"));
        msg.cardChipText = TR(L"正在通话");
        msg.cardChipTone = kCardChipLive;
        if (joinedCount > 0)
            msg.cardMetaText = TRF(L"{0} 人", joinedCount).c_str();
        break;
    //该群已没有群通话，或进行中的是另一场
    case balloongroupcall::kGroupCallRoomEnded:
        msg.screenShareStatus = TR(L"通话已结束");
        msg.cardChipText = msg.screenShareStatus;
        break;
    //本次登录还没有收到该群的状态
    default:
        msg.screenShareStatus = TR(L"正在获取状态");
        msg.cardChipText = msg.screenShareStatus;
        break;
    }
    //还不知道状态时也允许点：通话若已结束，服务端拒绝加入，DLL 随即回报「没有群通话」，卡片被重填为已结束
    msg.screenShareButtonEnabled = msg.screenShareHasButton && roomState != balloongroupcall::kGroupCallRoomEnded;
}

} // namespace

bool ParseGroupCallCard(const std::string& contentJson, GroupCallCardInfo& out)
{
    if (contentJson.empty())
        return false;
    Json::Value root;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(contentJson.c_str(), contentJson.c_str() + contentJson.size(), &root, &errs);
    delete reader;
    if (!ok || !root.isObject() || !root["kind"].isString() || root["kind"].asString() != kGroupCallKind)
        return false;
    out = GroupCallCardInfo();
    out.m_roomId = root["roomid"].isString() ? root["roomid"].asString() : std::string();
    if (out.m_roomId.empty())
        return false;
    out.m_media = root["media"].isInt() ? root["media"].asInt() : 0;
    out.m_initiatorId = ToInt64(root["initiator"]);
    out.m_initiatorName = Utf8ToWide(root["initiatorname"].isString() ? root["initiatorname"].asString() : std::string());
    out.m_ended = root.isMember("duration");
    out.m_durationSeconds = ToInt64(root["duration"]);
    return true;
}

std::string GroupCallRoomIdOf(const std::string& contentJson)
{
    GroupCallCardInfo info;
    if (!ParseGroupCallCard(contentJson, info))
        return std::string();
    return info.m_roomId;
}

CString FormatGroupCallCardDuration(int64_t seconds)
{
    if (seconds < 0)
        seconds = 0;
    const int hours = static_cast<int>(seconds / kSecondsPerHour);
    const int minutes = static_cast<int>((seconds % kSecondsPerHour) / kSecondsPerMinute);
    const int secs = static_cast<int>(seconds % kSecondsPerMinute);
    CString text;
    if (hours > 0)
        text.Format(_T("%d:%02d:%02d"), hours, minutes, secs);
    else
        text.Format(_T("%02d:%02d"), minutes, secs);
    return text;
}

void ApplyGroupCallCard(ChatMessage& msg, const std::string& contentJson, const CString& initiatorName, int roomState,
                        int joinedCount)
{
    GroupCallCardInfo info;
    const bool ok = ParseGroupCallCard(contentJson, info);
    msg.kind = kMsgScreenShare;
    msg.screenShareIsGroupCall = true;
    msg.groupCallMedia = ok ? info.m_media : 0;
    msg.screenShareContent = contentJson;
    const bool video = ok && info.m_media == kMediaVideo;
    msg.text = video ? TR(L"[群视频通话]") : TR(L"[群语音通话]");
    msg.screenShareTitle = video ? TR(L"群视频通话") : TR(L"群语音通话");
    msg.screenShareRoomId = ok ? info.m_roomId : std::string();
    msg.screenShareHostName = !initiatorName.IsEmpty() ? initiatorName : info.m_initiatorName;
    if (msg.fromMe)
        msg.screenShareSubtitle = TR(L"我发起的通话");
    else if (!msg.screenShareHostName.IsEmpty())
        msg.screenShareSubtitle = TRF(L"{0}发起的通话", msg.screenShareHostName).c_str();
    else
        msg.screenShareSubtitle = TR(L"群成员发起的通话");
    msg.screenShareForced = false;
    msg.screenShareNotice.Empty();
    msg.screenShareDesc.Empty();
    //本人发起的卡片也有「加入」：发起人中途离开后还可以回到通话里；已结束、内容不可用的卡片没有按钮
    msg.screenShareHasButton = ok && !info.m_ended && !info.m_roomId.empty();
    FillState(msg, info, ok, roomState, joinedCount);
}

void ApplyGroupCallState(ChatMessage& msg, int roomState, int joinedCount)
{
    if (msg.kind != kMsgScreenShare || !msg.screenShareIsGroupCall)
        return;
    GroupCallCardInfo info;
    const bool ok = ParseGroupCallCard(msg.screenShareContent, info);
    FillState(msg, info, ok, roomState, joinedCount);
}

} // namespace balloonchatwnd
