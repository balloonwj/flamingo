/**
 *  ChatCloudRecordCard.cpp
 *  云端录像卡片的解析与套用，说明见 ChatCloudRecordCard.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "ChatCloudRecordCard.h"

#include "ChatCardModel.h"   // SetCardChip / ChatCardChipTone：卡片的状态标签

#include <stdlib.h>
#include <memory>

#include "../../third-party/jsoncpp-1.9.0/json.h"
#include "../../net/SystemTipText.h"             // net::FormatDurationClock：时长的时钟写法
#include "FileBubbleFormat.h"                    // FormatFileSize：文件大小的写法
#include "../../Common/I18n/I18n.h"              // TR / TRF：界面文字的多语言查找

namespace balloonchatwnd {

namespace {

//毫秒换算成秒的除数
const int64_t kMsPerSecond = 1000;

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

} // namespace

bool ParseCloudRecordCard(const std::string& contentJson, CloudRecordCardInfo& out)
{
    out = CloudRecordCardInfo();
    if (contentJson.empty())
        return false;
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    Json::Value root;
    JSONCPP_STRING errs;
    if (!reader->parse(contentJson.data(), contentJson.data() + contentJson.size(), &root, &errs) || !root.isObject())
        return false;
    out.m_recordId = ToInt64(root["recordid"]);
    if (out.m_recordId <= 0)
        return false;
    out.m_owner = ToInt64(root["owner"]);
    out.m_title = root["title"].isString() ? Utf8ToWide(root["title"].asString()) : CString();
    out.m_durationMs = ToInt64(root["durationms"]);
    out.m_bytes = ToInt64(root["bytes"]);
    out.m_audioOnly = root["audioonly"].isInt() && root["audioonly"].asInt() != 0;
    if (out.m_durationMs < 0)
        out.m_durationMs = 0;
    if (out.m_bytes < 0)
        out.m_bytes = 0;
    return true;
}

CString CloudRecordCardSubtitle(const CString& senderName, bool fromMe)
{
    if (fromMe)
        return TR(L"我分享的云端录像");
    if (senderName.IsEmpty())
        return TR(L"云端录像");
    return CString(TRF(L"{0}分享的云端录像", senderName).c_str());
}

CString CloudRecordCardStatusText(const CloudRecordCardInfo& info)
{
    //按段拼接，段与段之间用「 · 」隔开；时长、大小为 0 时省去那一段
    CString text;
    if (info.m_audioOnly)
        text = TR(L"录音");
    if (info.m_durationMs > 0)
    {
        const std::string clock = net::FormatDurationClock(info.m_durationMs / kMsPerSecond);
        const CString part = TRF(L"时长 {0}", Utf8ToWide(clock)).c_str();
        if (!text.IsEmpty())
            text += L" · ";
        text += part;
    }
    if (info.m_bytes > 0)
    {
        if (!text.IsEmpty())
            text += L" · ";
        text += FormatFileSize(static_cast<long long>(info.m_bytes)).c_str();
    }
    return text;
}

void ApplyCloudRecordCard(ChatMessage& msg, const std::string& contentJson, const CString& senderName)
{
    CloudRecordCardInfo info;
    const bool ok = ParseCloudRecordCard(contentJson, info);

    msg.kind = kMsgScreenShare;
    msg.screenShareIsCloudRecord = true;
    msg.screenShareIsAnnouncement = false;
    msg.screenShareIsMemo = false;
    msg.screenShareIsGroupCall = false;
    msg.screenShareIsCallCard = false;
    msg.groupCallMedia = 0;
    msg.screenShareContent = contentJson;
    msg.screenShareRoomId.clear();
    msg.screenShareForced = false;
    msg.screenShareState = 0;
    msg.screenShareNotice.Empty();
    msg.screenShareDesc.Empty();
    msg.infoCardRefId = ok ? info.m_recordId : 0;
    msg.cloudRecordAudioOnly = ok && info.m_audioOnly;

    if (!ok)
    {
        //内容不可用：只写类型，不可点
        msg.text = TR(L"[云端录像]");
        msg.screenShareTitle = TR(L"云端录像");
        msg.screenShareHostName.Empty();
        msg.screenShareSubtitle.Empty();
        msg.screenShareStatus = TR(L"录像内容不可用");
        msg.screenShareHasButton = false;
        msg.screenShareButtonEnabled = false;
        SetCardChip(msg, msg.screenShareStatus, kCardChipEnded);
        return;
    }

    msg.text = info.m_title.IsEmpty() ? CString(TR(L"[云端录像]"))
                                      : CString(TRF(L"[云端录像] {0}", info.m_title).c_str());
    msg.screenShareTitle = info.m_title.IsEmpty() ? CString(TR(L"云端录像")) : info.m_title;
    msg.screenShareHostName = senderName;
    msg.screenShareSubtitle = CloudRecordCardSubtitle(senderName, msg.fromMe);
    msg.screenShareStatus = CloudRecordCardStatusText(info);
    //录像没有状态可言，不画标签，时长与大小写在信息行（docs/聊天卡片美化方案.md 5.2）
    SetCardChip(msg, CString(), kCardChipNone, msg.screenShareStatus);
    //本人发的卡片同样可以播放：开启者在别的设备上也可能想直接从聊天里打开
    msg.screenShareHasButton = true;
    msg.screenShareButtonEnabled = true;
}

bool IsCloudRecordCardOpenable(const ChatMessage& msg)
{
    return msg.kind == kMsgScreenShare && msg.screenShareIsCloudRecord && msg.infoCardRefId > 0
           && msg.screenShareButtonEnabled;
}

} // namespace balloonchatwnd
