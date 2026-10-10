/**
 *  ChatCallCard.cpp
 *  一对一通话卡片的解析与套用，说明见 ChatCallCard.h。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "ChatCallCard.h"

#include <stdlib.h>

#include "../../third-party/jsoncpp-1.9.0/json.h"
#include "ChatGroupCallCard.h"                // FormatGroupCallCardDuration：时长的写法与群通话卡片相同
#include "../../Common/I18n/I18n.h"           // TR / TRF：界面文字的多语言查找

namespace balloonchatwnd {

namespace {

//content 里 kind 字段的取值（docs/协议设计-v2.md 7.8「一对一通话卡片」）
const char* const kCallKind = "call";
//result 的取值：与协议 call_state 一致（exe 里没有这个枚举，按协议取值写在这里）
const int kResultRejected = 2;   //被叫拒绝
const int kResultCanceled = 3;   //主叫在接通前取消
const int kResultTimeout = 5;    //振铃超时无人接听
const int kResultEnded = 7;      //通话结束（接通后挂断，或参与方掉线超过宽限期）

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

} // namespace

bool ParseCallCard(const std::string& contentJson, CallCardInfo& out)
{
    if (contentJson.empty())
        return false;
    Json::Value root;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool parsed = reader->parse(contentJson.data(), contentJson.data() + contentJson.size(), &root, &errs);
    delete reader;
    if (!parsed || !root.isObject() || !root["kind"].isString() || root["kind"].asString() != kCallKind)
        return false;
    out.m_sessionId = root["sessionid"].isString() ? root["sessionid"].asString() : std::string();
    out.m_media = (root["media"].isInt() && root["media"].asInt() == kCallCardVideo) ? kCallCardVideo : kCallCardVoice;
    out.m_callerId = ToInt64(root["caller"]);
    out.m_calleeId = ToInt64(root["callee"]);
    out.m_callerName = Utf8ToWide(root["callername"].isString() ? root["callername"].asString() : std::string());
    out.m_ended = root["result"].isInt();
    out.m_result = out.m_ended ? root["result"].asInt() : 0;
    out.m_connected = root.isMember("duration");
    out.m_durationSeconds = ToInt64(root["duration"]);
    return true;
}

CString CallCardStatusText(const CallCardInfo& info, bool selfIsCaller, int inCallMedia)
{
    if (!info.m_ended)
    {
        if (inCallMedia != 0)
            return TR(L"通话中");
        return selfIsCaller ? TR(L"正在呼叫") : TR(L"邀请你通话");
    }
    //接通过：写时长（结束方式只能是「通话结束」）
    if (info.m_connected)
        return TRF(L"通话时长 {0}", FormatGroupCallCardDuration(info.m_durationSeconds)).c_str();
    switch (info.m_result)
    {
    //被叫拒绝：主叫看到「对方已拒绝」，被叫看到「已拒绝」
    case kResultRejected:
        return selfIsCaller ? TR(L"对方已拒绝") : TR(L"已拒绝");
    //主叫在接通前取消
    case kResultCanceled:
        return selfIsCaller ? TR(L"已取消") : TR(L"对方已取消");
    //振铃超时无人接听
    case kResultTimeout:
        return selfIsCaller ? TR(L"对方未接听") : TR(L"未接听");
    //没接通就结束（如响铃中参与方掉线超过宽限期）
    case kResultEnded:
        return TR(L"未接通");
    //未知的结束方式
    default:
        return TR(L"通话已结束");
    }
}

void ApplyCallCard(ChatMessage& msg, const std::string& contentJson, const CString& peerName, int inCallMedia)
{
    CallCardInfo info;
    const bool ok = ParseCallCard(contentJson, info);
    msg.kind = kMsgScreenShare;
    msg.screenShareIsCallCard = true;
    msg.screenShareIsGroupCall = false;
    msg.groupCallMedia = ok ? info.m_media : kCallCardVoice;
    msg.screenShareContent = contentJson;
    const bool video = ok && info.m_media == kCallCardVideo;
    msg.text = video ? TR(L"[视频通话]") : TR(L"[语音通话]");
    msg.screenShareTitle = video ? TR(L"视频通话") : TR(L"语音通话");
    msg.screenShareRoomId.clear();
    msg.screenShareHostName = !peerName.IsEmpty() ? peerName : info.m_callerName;
    if (msg.fromMe)
        msg.screenShareSubtitle = TR(L"我发起的通话");
    else if (!msg.screenShareHostName.IsEmpty())
        msg.screenShareSubtitle = TRF(L"{0}发起的通话", msg.screenShareHostName).c_str();
    else
        msg.screenShareSubtitle = TR(L"对方发起的通话");
    msg.screenShareForced = false;
    msg.screenShareNotice.Empty();
    msg.screenShareDesc.Empty();
    msg.screenShareHasButton = false;
    msg.screenShareButtonEnabled = false;
    msg.screenShareState = 0;
    //内容不可用：当作已结束的卡片
    msg.screenShareStatus = ok ? CallCardStatusText(info, msg.fromMe, inCallMedia) : CString(TR(L"通话已结束"));
}

bool IsCallCardEnded(const std::string& contentJson)
{
    CallCardInfo info;
    return ParseCallCard(contentJson, info) && info.m_ended;
}

} // namespace balloonchatwnd
