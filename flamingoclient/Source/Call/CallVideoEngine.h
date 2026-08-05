/**
 *  通话视频引擎（采集→编码→送出 / 收包→解码→显示），CallVideoEngine.h
 *
 *  把第三期视频管线收口成一个可复用引擎（仿音频的 CallAudioEngine）：MF 摄像头采集 →
 *  H.264 编码 → 经 sink 回调把编码帧交给上层（上层打视频标记经中转发对端）；上层把对端
 *  来的视频包喂进 OnRemoteEncodedVideo → H.264 解码 → 经 sink 回调把远端 NV12 帧交给上层
 *  显示。本地视频回环（VideoLoopback，把自己编码的包喂回 OnRemoteEncodedVideo）与真实
 *  通话（CallMediaSession，把包经中转收发）共用本引擎。
 *
 *  线程与并发：采集线程回调 OnVideoFrame→编码→sink->OnEncodedVideo；喂解码的线程
 *  （回环=采集线程；真实通话=中转接收线程）调 OnRemoteEncodedVideo→解码→
 *  sink->OnRemoteVideoFrame。编码器只在采集线程用、解码器只在喂解码线程用，二者独立 MFT。
 *  **Stop 安全前提**：调用方须先停掉会调用 OnRemoteEncodedVideo 的来源（如中转接收）再
 *  调本引擎 Stop()。
 *
 *  分辨率取摄像头协商到的尺寸（编解码同此尺寸）。MF 初始化由 MfCameraCapture 的采集线程
 *  负责（MFStartup 进程级），编解码 MFT 在 Start 中创建（此时 MF 已就绪）。
 *
 *  balloonwj@qq.com   2026-06-16
 */
#pragma once

#include "MfCameraCapture.h"
#include "MfH264Codec.h"

#include <stdint.h>
#include <atomic>

namespace call {

//视频引擎事件去向。
class CallVideoEngineSink
{
public:
    virtual ~CallVideoEngineSink() {}

    /**
     *  本端采集并编码出一个 H.264 帧（采集线程上下文）。实现方通常打视频标记经中转发出。
     *  @param data     H.264 帧字节起始；仅本次回调内有效，需留存须自行拷贝
     *  @param len      字节数
     *  @param keyframe 是否关键帧
     *  @param ptsHns   时间戳（100ns）
     */
    virtual void OnEncodedVideo(const uint8_t* data, size_t len, bool keyframe, int64_t ptsHns) = 0;

    /**
     *  本端采集到一帧原始画面（采集线程上下文，编码之前）。实现方通常转 BGRA 显示到本地预览
     *  小窗（PiP）。与编码 / 发送相互独立——本地预览即便编码器未就绪也能出画。
     *  @param nv12   NV12 帧（Y 平面起点；UV 平面位于 nv12 + stride*height）；仅本次回调内有效
     *  @param width/height/stride 尺寸与行跨度
     */
    virtual void OnLocalVideoFrame(const uint8_t* nv12, int width, int height, int stride) = 0;

    /**
     *  解码出一帧远端视频（喂解码的线程上下文）。实现方通常转 BGRA 显示到通话窗。
     *  @param nv12   NV12 帧（Y 平面起点；UV 平面位于 nv12 + stride*height）；仅本次回调内有效
     *  @param width/height/stride 尺寸与行跨度
     */
    virtual void OnRemoteVideoFrame(const uint8_t* nv12, int width, int height, int stride) = 0;

    /**
     *  本端摄像头不可用：采集中途断掉（采集线程上下文）。之后本端不再有画面、也不再发视频给对端，
     *  实现方通常把它转成界面上的一句提示。默认实现为空（回环 / 调试入口可以不关心）。
     *  注意：**引擎启动阶段**（Start 返回 false）的失败不走本回调，由调用方直接查 CameraFailReason。
     *  @param reason 失败原因，取 CameraFailReason（此处一般是 kCameraFailStream）
     */
    virtual void OnLocalVideoUnavailable(int /*reason*/)
    {
    }
};

//通话视频引擎：采集+编码经 sink 送出；收到的视频包解码后经 sink 交付显示。
class CallVideoEngine : public MfVideoFrameHandler, public MfH264EncoderSink, public MfH264DecoderSink
{
public:
    CallVideoEngine();
    ~CallVideoEngine();

    CallVideoEngine(const CallVideoEngine&) = delete;
    CallVideoEngine& operator=(const CallVideoEngine&) = delete;

    /**
     *  启动引擎：打开摄像头、按其分辨率初始化 H.264 编解码、起采集→编码。
     *  @param sink    事件去向（非空；生命周期须长于引擎，至少持续到 Stop 返回）
     *  @param fps     编码帧率（如 30）
     *  @param bitrate 编码目标码率（bit/s，如 2000000）
     *  @return 成功（摄像头打开且编解码就绪）返回 true；否则 false（见日志）
     */
    bool Start(CallVideoEngineSink* sink, int fps, int bitrate);

    //停止引擎并释放摄像头与编解码器。幂等。调用前须先停掉 OnRemoteEncodedVideo 的来源。
    void Stop();

    /**
     *  喂入一个收到的远端 H.264 包：解码后经 sink->OnRemoteVideoFrame 交付。引擎未运行时丢弃。
     *  @param data 包字节起始
     *  @param len  字节数
     *  @param ptsHns 时间戳（100ns）
     */
    void OnRemoteEncodedVideo(const uint8_t* data, size_t len, int64_t ptsHns);

    bool IsRunning() const
    {
        return m_ready.load();
    }

    int Width() const
    {
        return m_width;
    }

    int Height() const
    {
        return m_height;
    }

    /**
     *  取摄像头最近一次失败原因，取值见 CameraFailReason。Start 返回 false 后用它区分
     *  "机器上没有摄像头"与"摄像头打不开（多半被其它程序占用）"，好给用户不同的提示措辞。
     *  @return CameraFailReason 之一；从未失败过返回 kCameraFailNone
     */
    int CameraFailReason() const
    {
        return m_capture.FailReason();
    }

private:
    //MfVideoFrameHandler：采集线程——把采集帧送编码。
    virtual void OnVideoFrame(const uint8_t* data, int width, int height, int stride) override;
    //MfVideoFrameHandler：采集线程——采集意外中断，转报给 sink（本端从此没有画面）。
    virtual void OnVideoCaptureStopped(int reason) override;
    //MfH264EncoderSink：采集线程——编码出帧经 sink 送出。
    virtual void OnEncodedH264(const uint8_t* data, size_t len, bool keyframe, int64_t ptsHns) override;
    //MfH264DecoderSink：喂解码线程——解码出帧经 sink 交付显示。
    virtual void OnDecodedNv12(const uint8_t* nv12, int width, int height, int stride, int64_t ptsHns) override;

private:
    MfCameraCapture      m_capture;          //摄像头采集
    MfH264Encoder        m_encoder;          //H.264 编码（仅采集线程用；首帧时在采集线程懒创建）
    MfH264Decoder        m_decoder;          //H.264 解码（仅喂解码线程用；首包时在喂解码线程懒创建）
    CallVideoEngineSink* m_sink;             //事件去向（不拥有）
    std::atomic<bool>    m_ready;            //是否就绪
    int                  m_width;            //摄像头/编解码 宽
    int                  m_height;           //摄像头/编解码 高
    int                  m_fps;              //编码帧率
    int                  m_bitrate;          //编码码率（懒创建编码器用）
    int64_t              m_frameIndex;       //采集帧序号（算时间戳用；仅采集线程）
    bool                 m_encoderInitTried; //编码器是否已尝试创建（仅采集线程访问，避免每帧重试）
    bool                 m_decoderInitTried; //解码器是否已尝试创建（仅喂解码线程访问）

    //MFT 须在「使用它的线程」上创建（跨线程用会失效），故编/解码器分别延迟到采集线程 /
    //喂解码线程首次用时再创建，而非在 Start（调用方线程，可能是 STA）里建。
};

} // namespace call
