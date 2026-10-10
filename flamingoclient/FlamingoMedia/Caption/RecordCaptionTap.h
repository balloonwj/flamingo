/**
 *  录屏的语音字幕旁路（docs/语音字幕方案.md 6.1）：实现录制混音器的分路旁路 IRecordAudioTap，把写入线程取出的
 *  本人麦克风、电脑声音连同录制时间轴上的时刻交给字幕草稿会话（ICaptionRecording）。房间里其他人的声音不识别
 *  （共享录制时他们的字幕取自收到的字幕控制帧）。
 *
 *  Attach / Detach 在 UI 线程调用，OnRecordAudio 在写入线程调用；两者由互斥量隔开：Detach 返回之后写入线程不会再
 *  使用被解除的草稿会话，调用方随后即可对它调 Finish 或 Cancel（字幕变为不可用时，录制可以继续而识别停止）。
 *
 *  录制中关闭字幕（录制条「字幕：关」，docs/语音字幕方案.md 14.12）时调 SetSuspended(true)：此后交给草稿会话的是同样长度、
 *  同样时刻的静音。识别流靠静音的端点检测把关闭前说到一半的那句定稿保留，关闭期间不会再出现新的字幕；时间轴不中断，
 *  重新打开后接着识别。
 *
 *  典型用法：
 *      m_captionTap.Attach(recording, true, false);
 *      m_session.Audio().SetTap(&m_captionTap);
 *      ...
 *      caption::ICaptionRecording* r = m_captionTap.Detach();
 *      if (r != NULL) r->Finish(durationMs);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>

#include <mutex>
#include <vector>

#include "../Record/RecordAudioMixer.h"
#include "../../FlamingoCaption/CaptionApi.h"

namespace flamingomedia {

/**
 *  CRecordCaptionTap —— 录制混音器到字幕草稿会话的旁路。
 */
class CRecordCaptionTap : public screenrecord::IRecordAudioTap
{
public:
    CRecordCaptionTap();

    /**
     *  接上一个字幕草稿会话。已接着别的会话时先解除（不结束它，调用方应先自行 Detach）。
     *  @param recording 草稿会话（不拥有；在 Detach 之前须一直有效）
     *  @param mic       是否识别本人麦克风
     *  @param system    是否识别电脑声音
     */
    void Attach(caption::ICaptionRecording* recording, bool mic, bool system);

    /**
     *  解除草稿会话。返回之后写入线程不再使用它。
     *  @return 解除前接着的草稿会话；没有接着时返回 NULL
     */
    caption::ICaptionRecording* Detach();

    /**
     *  是否接着草稿会话。
     *  @return 接着时返回 true
     */
    bool IsAttached() const;

    /**
     *  取当前接着的草稿会话（UI 线程用，如同步画中画位置）。只有 UI 线程会 Detach，所以在 UI 线程上取到的指针
     *  在下一次 Detach 之前一直有效。
     *  @return 草稿会话；没有接着时返回 NULL
     */
    caption::ICaptionRecording* Current() const;

    /**
     *  录制中改变要识别的路（如用户在录制条上关掉了电脑声音时，这一路此后送来的都是静音，不必再识别）。
     *  @param mic    是否识别本人麦克风
     *  @param system 是否识别电脑声音
     */
    void SetSources(bool mic, bool system);

    /**
     *  录制中关闭或重新打开字幕（UI 线程）。关闭期间交给草稿会话的一律是静音，见文件头说明。
     *  @param suspended true 表示关闭（送静音），false 表示照常送原声
     */
    void SetSuspended(bool suspended);

    //当前是否处于关闭状态
    bool IsSuspended() const;

    //IRecordAudioTap：写入线程上把本人麦克风、电脑声音交给草稿会话
    virtual void OnRecordAudio(int source, const int16_t* pcm, size_t samples, int64_t ptsMs) override;

private:
    mutable std::mutex           m_mutex;       //保护以下三项（UI 线程写，写入线程读）
    caption::ICaptionRecording*  m_recording;   //接着的草稿会话（不拥有）；为 NULL 时旁路不做事
    bool                         m_mic;         //是否识别本人麦克风
    bool                         m_system;      //是否识别电脑声音
    bool                         m_suspended;   //录制中是否关闭了字幕：为 true 时交给草稿会话的是静音
    std::vector<int16_t>         m_silence;     //关闭期间用的静音缓冲（写入线程使用，按需加长）
};

} // namespace flamingomedia
