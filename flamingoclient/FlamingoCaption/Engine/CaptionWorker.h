/**
 *  识别线程与识别流（docs/语音字幕方案.md 5.1、5.2），CaptionWorker.h
 *
 *  一个后台识别线程服务全部识别流：调用方在任意线程用 PushPcm 推入 48 kHz 声音（只入队，不阻塞），识别线程把它
 *  重采样到 16 kHz、送入 sherpa-onnx、解码，按端点给出中间结果（OnCaptionPartial）与定稿结果（OnCaptionFinal）。
 *  定稿结果的起止时刻按「段起点 start_time + 词片时间戳」算出在识别流里的采样位置，再经 CaptionTimeMap 换算回
 *  推入时给的时间轴（录像时刻或本机时钟）。文字先整理空白，再应用自动替换表。
 *
 *  模型加载期间推入的声音先缓存，每路最多缓存 kMaxQueuedSeconds 秒（超出时丢弃最早的）；引擎确定不可用时直接丢弃。
 *  所有识别流都关闭超过 kCaptionIdleUnloadMs 后，识别线程卸载模型。
 *
 *  关闭是异步的，界面线程不会因为模型加载或声音积压而卡住：
 *    - Close（对外接口，实时字幕用）立即返回，返回后不再回调（若此刻正在回调，等这次回调结束）；
 *    - CloseWithFlush（模块内部，录制草稿用）同样立即返回，但识别线程会把剩余声音识别完、照常回调，
 *      最后调用 ICaptionFlushListener::OnStreamFlushed；
 *  两种情况下都由识别线程在收尾后释放识别流对象。
 *
 *  典型用法（模块内部）：
 *      CaptionStreamImpl* s = CaptionWorker::Instance().CreateStream(purpose, listener);
 *      s->PushPcm(pcm, n, ptsMs);
 *      s->Close();             //或 s->CloseWithFlush(listener)
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../CaptionApi.h"
#include "../Model/CaptionSegmenter.h"
#include "CaptionAudio.h"

namespace caption {

//模型加载期间每路最多缓存的声音（秒）
const int kMaxQueuedSeconds = 60;

class CaptionWorker;

/**
 *  识别流处理完的通知（模块内部用）：CloseWithFlush 之后，识别线程把剩余声音识别完、回调完最后一句，再调用它。
 *  在识别线程上调用；调用之后识别流对象即被释放。
 */
class ICaptionFlushListener
{
public:
    virtual void OnStreamFlushed() = 0;

protected:
    virtual ~ICaptionFlushListener()
    {
    }
};

/**
 *  一路识别流。由 CaptionWorker::CreateStream 创建；调用方用完后调用 Close 或 CloseWithFlush，之后不得再使用本对象。
 */
class CaptionStreamImpl : public ICaptionStream
{
public:
    //见 ICaptionStream::PushPcm
    virtual void PushPcm(const int16_t* mono48k, uint32_t samples, int64_t ptsMs) override;

    //见 ICaptionStream::Close：立即返回，之后不再回调；识别线程收尾后释放本对象
    virtual void Close() override;

    /**
     *  关闭但处理完剩余声音：立即返回；识别线程继续回调直到最后一句定稿，然后调用 flush->OnStreamFlushed，
     *  再释放本对象。
     *  @param flush 处理完的通知（不拥有，须在通知之前一直有效），可为 NULL
     */
    void CloseWithFlush(ICaptionFlushListener* flush);

    /**
     *  还在排队等待识别的采样数（48 kHz）。事后识别据此控制送入速度，免得超过排队上限被丢弃。任意线程。
     *  @return 采样数
     */
    size_t QueuedSamples();

private:
    friend class CaptionWorker;

    CaptionStreamImpl(CaptionWorker* worker, int purpose, ICaptionStreamListener* listener);
    virtual ~CaptionStreamImpl();

    //一段待识别的声音
    struct Chunk
    {
        std::vector<int16_t> m_pcm;     //48 kHz 单声道采样
        int64_t              m_ptsMs;   //第一个采样的时刻（毫秒）
    };

    CaptionWorker*          m_worker;        //所属的识别线程（不拥有）
    int                     m_purpose;       //用途，取值见 CaptionStreamPurpose
    ICaptionStreamListener* m_listener;      //结果回调（不拥有）

    std::mutex              m_queueMutex;    //保护 m_queue、m_queuedSamples
    std::deque<Chunk>       m_queue;         //待识别的声音
    size_t                  m_queuedSamples; //m_queue 里的采样总数（48 kHz）

    std::atomic<bool>       m_closing;       //调用方已调用 Close 或 CloseWithFlush
    std::mutex              m_cbMutex;       //回调期间持有；Close 借它保证返回后不再回调
    bool                    m_silent;        //不再回调（Close 之后为 true，受 m_cbMutex 保护）
    ICaptionFlushListener*  m_flush;         //处理完的通知（CloseWithFlush 时设置，不拥有）

    //以下只在识别线程上使用
    const void*             m_sherpaStream;  //SherpaOnnxOnlineStream*，第一次送声音时创建
    CaptionResampler        m_resampler;     //48 kHz → 16 kHz
    CaptionTimeMap          m_timeMap;       //识别流采样序号 → 推入时的时刻
    int64_t                 m_fedSamples;    //已送入识别流的 16 kHz 采样数
    std::wstring            m_lastPartial;   //上一次回调的中间结果，相同时不重复回调
};

/**
 *  识别线程（进程内唯一）。
 */
class CaptionWorker
{
public:
    static CaptionWorker& Instance();

    /**
     *  建一路识别流（必要时启动识别线程，并让引擎开始加载）。
     *  @return 识别流；引擎已确定不可用时返回 NULL
     */
    CaptionStreamImpl* CreateStream(int purpose, ICaptionStreamListener* listener);

    //设置自动替换表（复制保存）；任意线程
    void SetReplaceTable(const CaptionReplaceTable& table);

    //唤醒识别线程（有新声音或有流要关闭时）
    void Wake();

    //停止识别线程：仍未关闭的流按关闭处理（不再回调），然后等线程退出；模块 Shutdown 时在 UI 线程调用
    void Shutdown();

private:
    friend class CaptionStreamImpl;

    CaptionWorker();
    ~CaptionWorker();
    CaptionWorker(const CaptionWorker&);
    CaptionWorker& operator=(const CaptionWorker&);

    //识别线程主体
    void Run();

    /**
     *  处理一路流：送声音、解码、回调；流已关闭且声音处理完时收尾、通知并释放。
     *  @return 这一路已处理完（已从列表移除并释放），调用方不得再访问它
     */
    bool ProcessStream(CaptionStreamImpl* s, int engineState);

    /**
     *  取当前结果并回调。
     *  @param force 为 true 时不论是否端点都把当前文字定稿（流关闭时用）
     */
    void CheckResult(CaptionStreamImpl* s, bool force);

    //识别流处理完：从列表移除、发处理完的通知、释放对象（调用后不得再访问 s）
    void FinishStream(CaptionStreamImpl* s);

    //回调中间结果（流已静默时不回调）
    void EmitPartial(CaptionStreamImpl* s, const std::wstring& text);

    //回调定稿结果（流已静默时不回调）
    void EmitFinal(CaptionStreamImpl* s, const std::wstring& text, int64_t startMs, int64_t endMs);

    //整理文字并应用替换表
    std::wstring PostProcess(const std::wstring& text);

    std::mutex                       m_mutex;        //保护 m_streams、m_replace、m_started
    std::condition_variable          m_cv;           //唤醒识别线程
    std::vector<CaptionStreamImpl*>  m_streams;      //全部未处理完的流（处理完后由识别线程释放）
    CaptionReplaceTable              m_replace;      //自动替换表
    std::thread                      m_thread;       //识别线程
    bool                             m_started;      //识别线程是否已启动
    std::atomic<bool>                m_stop;         //请识别线程退出
    bool                             m_wakePending;  //有唤醒请求尚未处理（受 m_mutex 保护）
};

} // namespace caption
