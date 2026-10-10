/**
 *  语音气泡的纯逻辑，说明见 ChatVoiceBubble.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "ChatVoiceBubble.h"

#include "ChatMockData.h"
#include "../../net/VoiceContent.h"
#include "../../Common/I18n/I18n.h"

#include <stdio.h>

namespace balloonchatwnd {

namespace {

//每分钟的秒数
const int kSecondsPerMinute = 60;
//时长文字的缓冲区长度（字符），最长如「999\u203259\u2033」
const int kDurationTextChars = 16;

} // namespace

std::wstring FormatVoiceDuration(int seconds)
{
    if (seconds < 1)
        seconds = 1;
    wchar_t buf[kDurationTextChars] = { 0 };
    //秒用双撇号（U+2033），分用单撇号（U+2032）
    if (seconds < kSecondsPerMinute)
        swprintf_s(buf, L"%d\u2033", seconds);
    else
        swprintf_s(buf, L"%d\u2032%02d\u2033", seconds / kSecondsPerMinute, seconds % kSecondsPerMinute);
    return std::wstring(buf);
}

int VoiceBubbleWidth(int seconds)
{
    if (seconds <= 1)
        return kVoiceBubbleMinWidth;
    if (seconds >= kVoiceBubbleFullSeconds)
        return kVoiceBubbleMaxWidth;
    //1 秒到 kVoiceBubbleFullSeconds 秒之间按秒线性增长
    return kVoiceBubbleMinWidth
        + (kVoiceBubbleMaxWidth - kVoiceBubbleMinWidth) * (seconds - 1) / (kVoiceBubbleFullSeconds - 1);
}

bool ShowVoiceUnplayedDot(bool fromMe, bool played)
{
    return !fromMe && !played;
}

void ApplyVoiceMessage(ChatMessage& msg, const std::string& fileId, const std::string& md5, int durationSeconds,
                       long long sizeBytes, const std::string& waveBase64, bool played)
{
    msg.kind = kMsgVoice;
    //复制与引用退回到这段文字；摘要、会话列表另有各自的「[语音]」
    msg.text = TR(L"[语音]");
    msg.voiceFileId = fileId;
    msg.voiceMd5 = md5;
    msg.voiceDuration = (durationSeconds > 0) ? durationSeconds : 1;
    msg.voiceSize = sizeBytes;
    msg.voiceWave = net::VoiceWaveForDisplay(waveBase64);
    //本人发的一律算已听（方案 V7）
    msg.voicePlayed = msg.fromMe ? true : played;
    msg.voiceState = kVoiceStateReady;
    msg.voicePlaying = false;
    msg.voicePlayFrame = 0;
}

ChatMessage MakeSendingVoiceMessage(const std::string& clientMsgId, int64_t durationMs,
                                    const std::vector<unsigned char>& wave, const std::wstring& localPath,
                                    const std::string& md5, long long sizeBytes)
{
    ChatMessage msg;
    msg.fromMe = true;
    msg.clientMsgId = clientMsgId;
    //fileid 要等上传成功才有，本地气泡用不到它；波形先按「没有」填，下面换成录音器算出的原值
    ApplyVoiceMessage(msg, std::string(), md5, net::VoiceDurationSeconds(durationMs), sizeBytes, std::string(), true);
    if (wave.size() == static_cast<size_t>(net::kVoiceWaveBars))
    {
        msg.voiceWave = wave;
    }
    msg.voiceLocalPath = CString(localPath.c_str());
    msg.voiceState = kVoiceStateSending;
    return msg;
}

bool CanResendVoice(const ChatMessage& msg)
{
    if (msg.kind != kMsgVoice || !msg.fromMe || !msg.sendFailed || msg.clientMsgId.empty()
        || msg.voiceLocalPath.IsEmpty())
    {
        return false;
    }
    //录音文件可能已被「清理缓存」删掉：那样重发注定失败
    const DWORD attrs = ::GetFileAttributesW((LPCWSTR)msg.voiceLocalPath);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring VoiceFileMissingHint()
{
    return TR(L"语音文件已不存在，无法重新发送");
}

std::wstring VoiceSendFailureText(int code)
{
    //语音没有文字内容、也没有用户起的文件名，不会命中敏感词，不必像文件那样区分敏感词的说法
    if (code == 0)
    {
        return TR(L"语音发送失败");
    }
    return TRF(L"语音发送失败（错误码 {0}）", code);
}

int DecideVoiceClick(const ChatMessage& msg)
{
    //正在播放：再点一下停止
    if (msg.voicePlaying)
    {
        return kVoiceClickStop;
    }
    if (msg.voiceState == kVoiceStateDownloading)
    {
        return kVoiceClickWaitDownload;
    }
    if (msg.voiceState == kVoiceStateDownloadFailed)
    {
        return kVoiceClickRetryDownload;
    }
    //就绪、发送中（本人刚录的）、发送失败（本人的）：文件都在本机，有路径就能播；没有路径按需要下载处理
    if (msg.voiceLocalPath.IsEmpty())
    {
        return kVoiceClickRetryDownload;
    }
    return kVoiceClickPlay;
}

int VoicePlayFrame(int64_t playedMs)
{
    if (playedMs <= 0)
    {
        return 0;
    }
    return static_cast<int>((playedMs / kVoicePlayFrameMs) % kVoicePlayFrameCount);
}

int PickNextAutoPlayVoice(const std::vector<ChatMessage>& rows, size_t current)
{
    for (size_t i = current + 1; i < rows.size(); ++i)
    {
        const ChatMessage& r = rows[i];
        //时间分隔行（kMsgTime 且没有 msgid）不算把两条语音隔开；撤回提示行带原消息的 msgid，算作别的消息
        if (r.kind == kMsgTime && r.msgid == 0)
        {
            continue;
        }
        //紧挨着的下一条：对方发来、还没听过、文件已在本机，才接着播
        if (r.kind == kMsgVoice && !r.fromMe && !r.voicePlayed && r.voiceState == kVoiceStateReady
            && !r.voiceLocalPath.IsEmpty())
        {
            return static_cast<int>(i);
        }
        return -1;
    }
    return -1;
}

std::wstring VoiceDownloadingHint()
{
    return TR(L"语音正在下载，请稍候");
}

std::wstring VoicePlayFailedHint()
{
    return TR(L"语音播放失败");
}

} // namespace balloonchatwnd
