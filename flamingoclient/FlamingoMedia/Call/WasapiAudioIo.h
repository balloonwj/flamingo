/**
 *  WASAPI 音频采集与播放（共享模式、事件驱动），WasapiAudioIo.h
 *
 *  第二期音频管线的设备层：用 WASAPI 共享模式打开系统默认麦克风 / 扬声器，事件驱动地
 *  采集 / 播放音频。对上层只暴露「单声道 16 位 PCM（设备采样率）」这一种简单形态——
 *  设备真实格式（常是 48kHz 双声道 32 位浮点）与单声道 int16 之间的转换在本模块内用
 *  AudioPcmUtils 完成，上层（回环 PoC / 通话音频管线）不必关心设备格式。
 *
 *  两个类各自起一条工作线程：
 *    - WasapiCapture：采集线程等设备事件，取到一包音频转成单声道 int16，经 handler 回调
 *      交给上层（回调在采集线程上触发，实现方须自保线程安全、勿阻塞）。
 *    - WasapiRender：渲染线程等设备事件，向 source 回调要单声道 int16 填进设备缓冲播放
 *      （回调在渲染线程上触发，缺数据须补静音）。
 *
 *  本模块碰真实设备，不在单测覆盖范围（靠 --callaudioloop 运行期「自己听自己」验证）；
 *  其依赖的格式转换 / 环形缓冲是 AudioPcmUtils，已有单测。
 *
 *  balloonwj@qq.com   2026-06-12
 */
#pragma once

#include <stdint.h>

namespace call {

//采集事件回调。OnAudioCaptured 在采集线程上下文触发。
class WasapiCaptureHandler
{
public:
    virtual ~WasapiCaptureHandler() {}

    /**
     *  采集到一段音频（已转成单声道 int16，采样率为设备采样率）。
     *  @param mono   指向 frames 个单声道 16 位采样；仅本次回调内有效，需留存须自行拷贝
     *  @param frames 采样数
     */
    virtual void OnAudioCaptured(const int16_t* mono, int frames) = 0;
};

//渲染数据源回调。OnAudioNeeded 在渲染线程上下文触发。
class WasapiRenderSource
{
public:
    virtual ~WasapiRenderSource() {}

    /**
     *  渲染需要 frames 个单声道 int16 采样：实现方把数据填入 mono。
     *  数据不足时须把不足部分补静音（填 0），不可少填——否则设备缓冲会有未初始化噪声。
     *  @param mono   [出参] 指向 frames 个单声道 16 位采样的缓冲，由实现方填满
     *  @param frames 需要的采样数
     */
    virtual void OnAudioNeeded(int16_t* mono, int frames) = 0;
};

//WASAPI 采集：共享模式事件驱动打开默认麦克风，采集线程把音频转单声道 int16 回调给上层。
class WasapiCapture
{
public:
    WasapiCapture();
    ~WasapiCapture();

    WasapiCapture(const WasapiCapture&) = delete;
    WasapiCapture& operator=(const WasapiCapture&) = delete;

    /**
     *  打开默认麦克风并起采集线程。阻塞直到设备就绪 / 失败。
     *  @param handler       采集回调（非空；生命周期须长于本采集，至少持续到 Stop 返回）
     *  @param outSampleRate [出参] 设备采样率（Hz），上层据此定 Opus 采样率与帧长
     *  @return 成功返回 true；设备打开 / 初始化失败返回 false
     */
    bool Start(WasapiCaptureHandler* handler, int& outSampleRate);

    //停止采集并结束采集线程。幂等。
    void Stop();

private:
    //采集线程体：循环等事件、取包、转单声道 int16、回调。
    void CaptureProc();

private:
    struct Impl;       //隐藏 WASAPI/COM 细节，避免本头泄漏 Windows 音频头
    Impl* m_impl;      //实现体（构造分配、析构释放）
};

//WASAPI 播放：共享模式事件驱动打开默认扬声器，渲染线程向上层要单声道 int16 播放。
class WasapiRender
{
public:
    WasapiRender();
    ~WasapiRender();

    WasapiRender(const WasapiRender&) = delete;
    WasapiRender& operator=(const WasapiRender&) = delete;

    /**
     *  打开默认扬声器并起渲染线程。阻塞直到设备就绪 / 失败。
     *  @param source        渲染数据源回调（非空；生命周期须长于本渲染，至少持续到 Stop 返回）
     *  @param outSampleRate [出参] 设备采样率（Hz）
     *  @return 成功返回 true；设备打开 / 初始化失败返回 false
     */
    bool Start(WasapiRenderSource* source, int& outSampleRate);

    //停止播放并结束渲染线程。幂等。
    void Stop();

private:
    //渲染线程体：循环等事件、按可写空间向 source 要数据、转设备格式、写入。
    void RenderProc();

private:
    struct Impl;
    Impl* m_impl;
};

} // namespace call
