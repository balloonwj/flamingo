/**
 *  本地音频回环 PoC 实现，AudioLoopback.cpp
 *  balloonwj@qq.com   2026-06-12
 */
#include "AudioLoopback.h"

#include <windows.h>
#include <tchar.h>

namespace call {

//一帧的采样数（48 kHz 下 20 毫秒，与音频引擎一致）：回环的媒体时间戳 = 采集帧序号 × 本值
static const uint64_t kLoopbackFrameSamples = 960;

AudioLoopback::AudioLoopback()
{
}

AudioLoopback::~AudioLoopback()
{
    Stop();
}

bool AudioLoopback::Start()
{
    //回环不启用 AEC：回环播放的就是自己刚才的声音，开 AEC 反而会把它当回声消掉。
    return m_engine.Start(this, false);
}

void AudioLoopback::Stop()
{
    m_engine.Stop();
}

void AudioLoopback::OnEncodedAudio(const unsigned char* data, size_t len, uint64_t frameIndex)
{

    //回环：把刚编码的包直接喂回引擎解码播放（本机闭环）。本回调同步发生在采集线程上，
    //故引擎 Stop 时采集线程 join 后自然不再有解码，无 Stop 竞态。
    //回环：媒体时间戳就是本端的采集帧序号 × 每帧采样数
    m_engine.OnEncodedPacket(data, len, static_cast<uint32_t>(frameIndex * kLoopbackFrameSamples));
}

bool RunAudioLoopbackDebug()
{
    AudioLoopback loop;
    if (!loop.Start())
    {
        ::MessageBox(NULL,
                     _T("音频回环启动失败：请确认有可用的麦克风与扬声器，且采集采样率为 Opus 支持（常见为 48kHz）。详见日志。"),
                     _T("音频回环自测"), MB_OK | MB_ICONWARNING);
        return false;
    }

    //回环已在采集 / 渲染线程上持续运行；用模态框阻塞主线程，点确定后停止。
    ::MessageBox(NULL,
                 _T("音频回环运行中：对着麦克风说话，应能从扬声器/耳机听到自己（有少许延迟）。\n\n点“确定”停止回环。"),
                 _T("音频回环自测"), MB_OK | MB_ICONINFORMATION);

    loop.Stop();
    return true;
}

} // namespace call
