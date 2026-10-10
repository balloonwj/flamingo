/**
 *  「共享电脑声音」回环采集的设备检查（Suite SSSYS，标签 [.device][sysaudio]），ShareSystemAudioDeviceTests.cpp
 *
 *  打开 FlamingoMedia/Share/ShareSystemAudioCapture 一秒钟：确认本机能用「排除本进程树」的方式打开回环（第 0 期 0e 在
 *  Win10 19045 上实测可用），采集线程正常收发、Stop 能干净退出。不播放任何声音，采到的内容只计数、不保存。
 *  依赖真实的声音设备，默认不运行：FlamingoUnit.exe "[sysaudio]"。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <atomic>

#include "../../FlamingoMedia/Share/ShareSystemAudioCapture.h"
#include "../../FlamingoMedia/Common/OsVersionPolicy.h"

using namespace screenshare;

namespace {

//采集时长（毫秒）
const DWORD kSyssCaptureMs = 1000;

//只数采样数的回调
class SyssCounter : public ShareSystemAudioHandler
{
public:
    SyssCounter()
        : m_samples(0)
    {
    }

    virtual void OnSystemAudio(const int16_t* mono, int samples) override
    {
        (void)mono;
        m_samples += samples;
    }

    std::atomic<long long> m_samples;   //收到的采样数
};

} // namespace

TEST_CASE("SSSYS1 排除本进程树的回环能打开并干净地停下", "[.device][sysaudio]")
{
    uint32_t major = 0;
    uint32_t minor = 0;
    uint32_t build = 0;
    if (!flamingomedia::GetOsVersion(major, minor, build) ||
        !flamingomedia::IsScreenCaptureSupportedBuild(major, minor, build))
    {
        SKIP("process loopback needs Windows 10 2004 or later");
    }
    SyssCounter counter;
    ShareSystemAudioCapture capture;
    REQUIRE(capture.Start(&counter));
    CHECK(capture.IsRunning());
    CHECK(capture.IsExcludingSelf());
    ::Sleep(kSyssCaptureMs);
    capture.Stop();
    CHECK_FALSE(capture.IsRunning());
    WARN("SSSYS1: exclude self=1, samples in " << kSyssCaptureMs << " ms: " << counter.m_samples.load());
    //重复停止安全
    capture.Stop();
}
