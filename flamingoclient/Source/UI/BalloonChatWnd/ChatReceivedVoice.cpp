/**
 *  ChatReceivedVoice.cpp
 *
 *  单聊窗与群聊窗共用的语音消息接收处理，见 ChatReceivedVoice.h。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "ChatReceivedVoice.h"

#include "ChatVoiceBubble.h"                   // ApplyVoiceMessage、ChatVoiceState
#include "ChatMessageList.h"                   // FindVoiceByClientMsgId 遍历列表
#include "ChatBubble.h"                        // 同上，取每一行的消息
#include "../../net/MessageProtocolClient.h"   // net::MessageNotify
#include "../../Feature/Voice/VoiceHub.h"      // VoiceCachePath
#include "../../Session/UserSessionData.h"     // FMG_MSG_QUERY_VOICE_PLAYED

namespace balloonchatwnd {

namespace {

//本机文件能否当作这段语音的完整副本：存在、不是目录、大小与消息里的相符（消息里没有大小时只判断存在）。
//下载是先写临时文件、校验后改名，正常不会留下半截的 <md5>.ogg；大小比对用来挡住被改写过的同名文件。
//只取文件属性、不读内容
bool voiceFileUsable(const std::wstring& path, long long expectSize)
{
    if (path.empty())
    {
        return false;
    }
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad))
    {
        return false;
    }
    if ((fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        return false;
    }
    if (expectSize <= 0)
    {
        return true;
    }
    const ULONGLONG actualBytes = (static_cast<ULONGLONG>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
    return actualBytes == static_cast<ULONGLONG>(expectSize);
}

} // namespace

bool ResolveVoicePlayed(bool fromMe, bool storeReady, bool storedPlayed)
{
    if (fromMe)
    {
        return true;
    }
    //本地库不可用：已听状态无处记录，按已听处理，免得每次打开窗口都出现一片无法清除的红点
    if (!storeReady)
    {
        return true;
    }
    return storedPlayed;
}

ChatMessage BuildReceivedVoiceMessage(bool fromMe, const net::MessageNotify& notify, bool played,
                                      const std::wstring& voiceDir, bool* needDownload)
{
    ChatMessage msg;
    msg.fromMe = fromMe;
    msg.msgid = (long long)notify.m_msgid;
    msg.timestampMs = (long long)notify.m_timestamp;
    msg.clientMsgId = notify.m_clientmsgid;
    ApplyVoiceMessage(msg, notify.m_voiceFileId, notify.m_voiceMd5, notify.m_voiceDuration,
                      (long long)notify.m_voiceSize, notify.m_voiceWave, played);

    //收发两端的缓存文件名相同（ChatVoice\<md5>.ogg，方案 V5）：本人在本机录的语音不必再下载
    const std::wstring path = (voiceDir.empty() || notify.m_voiceMd5.empty())
        ? std::wstring()
        : balloonvoice::VoiceCachePath(voiceDir, notify.m_voiceMd5);
    bool download = false;
    if (voiceFileUsable(path, msg.voiceSize))
    {
        msg.voiceState = kVoiceStateReady;
        msg.voiceLocalPath = CString(path.c_str());
    }
    else if (path.empty() || notify.m_voiceFileId.empty())
    {
        //缺下载所需的信息（消息内容不完整，或还没登录拿不到缓存目录）：无法下载，直接标为下载失败
        msg.voiceState = kVoiceStateDownloadFailed;
    }
    else
    {
        msg.voiceState = kVoiceStateDownloading;
        download = true;
    }
    if (needDownload != NULL)
    {
        *needDownload = download;
    }
    return msg;
}

void QueryVoicePlayed(HWND hostPanel, VoicePlayedQuery& query)
{
    query.m_played.clear();
    query.m_storeReady = false;
    //没有主面板（调试入口、主面板已销毁）：查不到，按本地库不可用处理
    if (!::IsWindow(hostPanel))
    {
        return;
    }
    ::SendMessage(hostPanel, FMG_MSG_QUERY_VOICE_PLAYED, 0, (LPARAM)&query);
}

bool FindVoiceByClientMsgId(const ChatMessageList& list, const std::string& clientMsgId, ChatMessage& out)
{
    if (clientMsgId.empty())
    {
        return false;
    }
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = list.Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        const ChatBubble* b = dynamic_cast<const ChatBubble*>(kids[i].get());
        if (b != NULL && b->GetMessage().kind == kMsgVoice && b->GetMessage().clientMsgId == clientMsgId)
        {
            out = b->GetMessage();
            return true;
        }
    }
    return false;
}

ChatMessage BuildStoredVoiceMessage(long long msgid, bool fromMe, const std::string& fileId, const std::string& md5,
                                    int durationSeconds, long long sizeBytes, const std::string& waveBase64,
                                    bool played, const std::wstring& voiceDir)
{
    ChatMessage msg;
    msg.fromMe = fromMe;
    msg.msgid = msgid;
    ApplyVoiceMessage(msg, fileId, md5, durationSeconds, sizeBytes, waveBase64, played);
    const std::wstring path = (voiceDir.empty() || md5.empty()) ? std::wstring()
                                                                : balloonvoice::VoiceCachePath(voiceDir, md5);
    if (voiceFileUsable(path, sizeBytes))
    {
        msg.voiceState = kVoiceStateReady;
        msg.voiceLocalPath = CString(path.c_str());
    }
    else
    {
        //本机没有：借用下载失败态表示「点一下就下载」（DecideVoiceClick 据此返回重新下载）
        msg.voiceState = kVoiceStateDownloadFailed;
    }
    return msg;
}

} // namespace balloonchatwnd
