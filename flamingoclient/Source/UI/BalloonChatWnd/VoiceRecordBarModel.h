/**
 *  VoiceRecordBarModel.h
 *
 *  聊天窗录音条的纯逻辑（语音消息第 3b 步，交互见 docs/语音消息方案.md 第 3.1 节）：计时文字、滚动的音量柱、
 *  开始录音失败与说话太短时的提示文字。不碰窗口与控件，可以直接做单元测试；录音条的控件见 VoiceRecordBar。
 *
 *  典型用法：
 *      balloonchatwnd::VoiceLevelHistory levels;
 *      levels.Push(level);                                   //每 100 毫秒一次
 *      for (size_t i = 0; i < levels.Levels().size(); ++i)
 *          bar[i]->SetInset(balloonchatwnd::VoiceBarInset(levels.Levels()[i], rowHeight, kMinBarPx));
 *      label->SetText(balloonchatwnd::FormatVoiceElapsed(elapsedMs).c_str());
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace balloonchatwnd {

//录音条上音量柱的根数
const int kVoiceBarCount = 24;
//计时文字的上限（毫秒）：录满 2 分钟后停在 2:00
const int64_t kVoiceElapsedCapMs = 120000;

/**
 *  录音条的计时文字。
 *  @param elapsedMs 已录的毫秒数；小于 0 按 0，超过 kVoiceElapsedCapMs 按它
 *  @return 「分:秒」，秒补足两位，如 L"0:07"、L"1:05"、L"2:00"（按整秒向下取整）
 */
std::wstring FormatVoiceElapsed(int64_t elapsedMs);

/**
 *  录音条上滚动显示的音量：固定 kVoiceBarCount 个，新的音量从右边进来，最旧的从左边移出。
 */
class VoiceLevelHistory
{
public:
    //全部为 0
    VoiceLevelHistory();

    /**
     *  追加一个音量。
     *  @param level 0~255，越界时夹到范围内
     */
    void Push(int level);

    //清回全部为 0（开始一次新的录音时调用）
    void Reset();

    //从左到右的音量，个数为 kVoiceBarCount
    const std::vector<uint8_t>& Levels() const
    {
        return m_levels;
    }

private:
    std::vector<uint8_t> m_levels;   //从左到右的音量，最右边是最新的
};

/**
 *  音量柱的上下内缩（音量柱用竖向的 DuiSeparator，内缩越大柱子越矮）。
 *  @param level     音量 0~255
 *  @param rowHeight 音量柱所在行的高度（像素）
 *  @param minBarPx  最矮的柱子高度（像素），音量为 0 时取它
 *  @return 上下各内缩的像素数；音量 255 时为 0（满高），音量 0 时柱高为 minBarPx
 */
int VoiceBarInset(int level, int rowHeight, int minBarPx);

/**
 *  开始录音失败时录音条上的提示文字。
 *  @param startResult flamingomedia::MediaVoiceRecordStart 的取值
 *  @return 麦克风被占用：「通话中不能录制语音消息」；麦克风打不开：提示检查设备与系统的麦克风权限；
 *          已经在录：「正在另一个会话里录音」；模块不可用：「语音消息暂时不可用」；成功时返回空串
 */
std::wstring VoiceRecordStartHint(int startResult);

//不到 1 秒点「发送」时的提示文字：「说话时间太短」
std::wstring VoiceTooShortHint();

} // namespace balloonchatwnd
