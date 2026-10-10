/**
 *  一次录制的字幕草稿（docs/语音字幕方案.md 6.1），CaptionRecording.h
 *
 *  ICaptionRecording 的实现：按参数建麦克风、电脑声音两路识别流（CaptionWorker），定稿结果经 CaptionAssembler
 *  装配进字幕轨（整理空白、替换表、行首标点并回、切成字幕条），并写录像旁的草稿「原名.mp4.caption.json」。
 *  录制共享时别人的字幕经 AddRemoteCaption 加入。草稿在有新字幕后最多每 kDraftSaveIntervalMs 写一次。
 *
 *  结束是异步的：Finish 让两路流处理完剩余声音（CloseWithFlush），全部处理完后在识别线程上写最终草稿并释放本对象；
 *  Cancel 静默关闭两路流、删除草稿并释放本对象。
 *
 *  线程：PushMic / PushSystem 在录制写入线程；AddRemoteCaption / SetPip / Finish / Cancel 在 UI 线程；
 *  识别结果在识别线程上回调。字幕轨与草稿由 m_mutex 保护。
 *
 *  典型用法（模块内部）：
 *      CaptionRecording* r = CaptionRecording::Create(params, settings);
 *      r->PushMic(pcm, n, ptsMs);
 *      r->Finish(durationMs);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <mutex>
#include <string>

#include "../CaptionApi.h"
#include "../Engine/CaptionWorker.h"
#include "../Model/CaptionDraft.h"
#include "../Model/CaptionSegmenter.h"
#include "../Model/CaptionTrack.h"

namespace caption {

//草稿两次写盘之间的最短间隔（毫秒）
const int64_t kDraftSaveIntervalMs = 2000;

class CaptionRecording;

/**
 *  录制草稿里的一路识别（麦克风或电脑声音）：把识别流的回调转给 CaptionRecording，并接收处理完的通知。
 */
class CaptionRecordingChannel : public ICaptionStreamListener, public ICaptionFlushListener
{
public:
    CaptionRecordingChannel();

    //见 ICaptionStreamListener：中间结果不进草稿，只转给所属录制草稿的实时观察者（录制时屏幕上的字幕）
    virtual void OnCaptionPartial(const wchar_t* text) override;

    //见 ICaptionStreamListener：定稿结果交给所属的录制草稿
    virtual void OnCaptionFinal(const wchar_t* text, int64_t startMs, int64_t endMs) override;

    //见 ICaptionFlushListener：这一路处理完
    virtual void OnStreamFlushed() override;

    CaptionRecording*  m_owner;     //所属的录制草稿（不拥有）
    CaptionStreamImpl* m_stream;    //识别流；关闭后置为 NULL
    int                m_source;    //来源，取值见 CaptionSource
    std::wstring       m_speaker;   //说话人
};

/**
 *  一次录制的字幕草稿。
 */
class CaptionRecording : public ICaptionRecording
{
public:
    /**
     *  创建并开始。
     *  @param params   录制参数（路径不能为空）
     *  @param replace  自动替换表（复制保存）
     *  @return 参数不对或两路都不识别时返回 NULL
     */
    static CaptionRecording* Create(const CaptionRecordingParams* params, const CaptionReplaceTable& replace);

    //见 ICaptionRecording
    virtual void PushMic(const int16_t* mono48k, uint32_t samples, int64_t ptsMs) override;
    virtual void PushSystem(const int16_t* mono48k, uint32_t samples, int64_t ptsMs) override;
    virtual void AddRemoteCaption(const wchar_t* speaker, const wchar_t* text, int64_t startMs, int64_t endMs) override;
    virtual void SetPip(bool present, double left, double top, double right, double bottom) override;
    virtual void Finish(int64_t durationMs) override;
    virtual void Cancel() override;
    virtual void SetObserver(ICaptionRecordingObserver* observer) override;

    //一路识别的中间结果（识别线程）：只转给实时观察者
    void OnChannelPartial(CaptionRecordingChannel* ch, const wchar_t* text);

    //一路识别的定稿结果（识别线程）
    void OnChannelFinal(CaptionRecordingChannel* ch, const wchar_t* text, int64_t startMs, int64_t endMs);

    //一路识别处理完（识别线程）；两路都处理完且已 Finish 时写最终草稿并释放本对象
    void OnChannelFlushed(CaptionRecordingChannel* ch);

    //当前字幕条数（测试用）
    size_t CueCount();

private:
    CaptionRecording(const std::wstring& videoPath, const CaptionReplaceTable& replace);
    virtual ~CaptionRecording();

    //写草稿（调用方持有 m_mutex）；force 为 false 时距上次写盘不足 kDraftSaveIntervalMs 则跳过
    void SaveDraftLocked(bool force);

    std::mutex              m_mutex;           //保护下面的字幕轨、草稿与计数
    std::wstring            m_videoPath;       //录像完整路径
    std::wstring            m_draftPath;       //草稿完整路径
    CaptionReplaceTable     m_replace;         //自动替换表
    CaptionTrack            m_track;           //字幕轨
    CaptionAssembler        m_assembler;       //识别结果装配
    CaptionDraft            m_draft;           //草稿里除字幕外的部分（录像信息、样式、画中画）
    CaptionRecordingChannel m_mic;             //麦克风一路
    CaptionRecordingChannel m_system;          //电脑声音一路
    int                     m_pendingFlush;    //Finish 后尚未处理完的识别流个数
    bool                    m_finishing;       //已调用 Finish
    int64_t                 m_lastSaveMs;      //上次写草稿的时刻（GetTickCount64）
    bool                    m_dirty;           //有尚未写盘的修改
    std::mutex              m_observerMutex;   //保护 m_observer；调用观察者时一直持有，SetObserver 因此能保证返回后不再调用旧观察者
    ICaptionRecordingObserver* m_observer;     //实时识别结果的观察者（不拥有）；为 NULL 时不通知
};

} // namespace caption
