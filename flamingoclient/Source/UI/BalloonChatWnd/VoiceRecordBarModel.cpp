/**
 *  聊天窗录音条的纯逻辑，说明见 VoiceRecordBarModel.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "VoiceRecordBarModel.h"

#include <stdio.h>
#include "../../Common/I18n/I18n.h"
#include "../../../FlamingoMedia/Api/FlamingoMediaApi.h"

namespace balloonchatwnd {

namespace {

//每秒、每分钟的换算
const int64_t kMillisPerSecond = 1000;
const int64_t kSecondsPerMinute = 60;
//计时文字的缓冲区长度（字符），「分:秒」最长如 "999:59"
const int kElapsedTextChars = 16;
//音量的上限
const int kLevelMax = 255;

} // namespace

std::wstring FormatVoiceElapsed(int64_t elapsedMs)
{
    if (elapsedMs < 0)
        elapsedMs = 0;
    if (elapsedMs > kVoiceElapsedCapMs)
        elapsedMs = kVoiceElapsedCapMs;
    const int64_t seconds = elapsedMs / kMillisPerSecond;
    wchar_t buf[kElapsedTextChars] = { 0 };
    swprintf_s(buf, L"%lld:%02lld", static_cast<long long>(seconds / kSecondsPerMinute),
               static_cast<long long>(seconds % kSecondsPerMinute));
    return std::wstring(buf);
}

VoiceLevelHistory::VoiceLevelHistory()
    : m_levels(static_cast<size_t>(kVoiceBarCount), 0)
{
}

void VoiceLevelHistory::Push(int level)
{
    if (level < 0)
        level = 0;
    if (level > kLevelMax)
        level = kLevelMax;
    //最旧的从左边移出，新的放在最右边
    m_levels.erase(m_levels.begin());
    m_levels.push_back(static_cast<uint8_t>(level));
}

void VoiceLevelHistory::Reset()
{
    m_levels.assign(static_cast<size_t>(kVoiceBarCount), 0);
}

int VoiceBarInset(int level, int rowHeight, int minBarPx)
{
    if (rowHeight <= 0)
        return 0;
    if (level < 0)
        level = 0;
    if (level > kLevelMax)
        level = kLevelMax;
    if (minBarPx < 0)
        minBarPx = 0;
    if (minBarPx > rowHeight)
        minBarPx = rowHeight;
    //柱高在 minBarPx 与整行高之间按音量线性变化，上下各内缩一半的空余
    const int barHeight = minBarPx + (rowHeight - minBarPx) * level / kLevelMax;
    return (rowHeight - barHeight) / 2;
}

std::wstring VoiceRecordStartHint(int startResult)
{
    switch (startResult)
    {
    //已开始录音：不提示
    case flamingomedia::media_voice_record_ok:
        return std::wstring();

    //麦克风正被一对一通话或屏幕共享使用（方案 V6）
    case flamingomedia::media_voice_record_mic_busy:
        return TR(L"通话中不能录制语音消息");

    //麦克风打不开：没有设备、被其它程序独占、或系统隐私设置禁止
    case flamingomedia::media_voice_record_device_error:
        return TR(L"麦克风打不开，请检查麦克风是否连接，以及系统设置里是否允许使用麦克风");

    //另一个聊天窗正在录音
    case flamingomedia::media_voice_record_already:
        return TR(L"正在另一个会话里录音");

    //模块没有加载或已经关闭，以及其它未知的结果
    default:
        return TR(L"语音消息暂时不可用");
    }
}

std::wstring VoiceTooShortHint()
{
    return TR(L"说话时间太短");
}

} // namespace balloonchatwnd
