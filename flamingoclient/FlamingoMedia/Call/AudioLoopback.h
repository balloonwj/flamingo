/**
 *  本地音频回环 PoC（基于 CallAudioEngine），AudioLoopback.h
 *
 *  第二期音频管线的技术验证：单机内把麦克风采集的音频用 Opus 编码、立刻解码再播放——
 *  对着麦克风说话应能听到自己（有少许延迟）。改为复用 CallAudioEngine：本类作引擎的
 *  编码包去向(sink)，把刚编码的包直接喂回引擎 OnEncodedPacket 形成本机闭环；这样回环
 *  与真实通话(CallMediaSession)共用同一套引擎，回环验证即验证引擎本体。
 *
 *  balloonwj@qq.com   2026-06-12
 */
#pragma once

#include "CallAudioEngine.h"

namespace call {

//本地音频回环：作引擎的编码包去向，把包喂回引擎解码播放（本机闭环）。
class AudioLoopback : public CallAudioEngineSink
{
public:
    AudioLoopback();
    ~AudioLoopback();

    AudioLoopback(const AudioLoopback&) = delete;
    AudioLoopback& operator=(const AudioLoopback&) = delete;

    //启动回环（启动内部引擎）。成功返回 true。
    bool Start();

    //停止回环。幂等。
    void Stop();

private:
    //采集线程上下文：把刚编码的包直接喂回引擎解码播放（本机闭环）。
    virtual void OnEncodedAudio(const unsigned char* data, size_t len, uint64_t frameIndex) override;

private:
    CallAudioEngine m_engine;   //音频引擎（采集→编码→本 sink；OnEncodedPacket→解码→播放）
};

/**
 *  运行一次音频回环自测（--callaudioloop 调试入口）：启动回环，弹框阻塞期间持续运行，
 *  用户点确定后停止。需默认麦克风/扬声器可用且采集率为 Opus 支持率（常 48kHz）。
 *  @return 回环成功启动并运行过返回 true；启动失败返回 false
 */
bool RunAudioLoopbackDebug();

} // namespace call
