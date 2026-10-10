/**
 *  屏幕共享「共享电脑声音」的回环采集，ShareSystemAudioCapture.h
 *
 *  优先用「排除本进程树的回环采集」（ActivateAudioInterfaceAsync + VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK +
 *  PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE）：只采 Flamingo 以外所有程序的声音，我们自己播放的他人说话声
 *  从源头上就不会被采进去（第 0 期 0e 在 Win10 19045 上实测可用，人声衰减 24.6 dB）。接口不可用时退回默认播放设备的
 *  普通回环，由调用方按 IsExcludingSelf 决定是否闪避（docs/屏幕共享与录制方案.md 5.7、第 2 节第 22 条）。
 *
 *  两种方式都按 48 kHz、16 位立体声初始化（交给系统转换格式），再混成单声道交给回调。ActivateAudioInterfaceAsync
 *  是 Win10 才有的接口，运行时从 Mmdevapi.dll 取，DLL 在 Win7 上仍能加载。
 *
 *  线程：Start / Stop 在 UI 线程调用；采集在本类自己的线程上，回调在该线程上触发。
 *
 *  典型用法：
 *      if (m_capture.Start(this)) excludingSelf = m_capture.IsExcludingSelf();
 *      ...
 *      m_capture.Stop();
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <atomic>
#include <memory>
#include <thread>

namespace screenshare {

//回环采集到一段电脑声音（采集线程上回调）
class ShareSystemAudioHandler
{
public:
    virtual ~ShareSystemAudioHandler()
    {
    }

    /**
     *  @param mono    48 kHz 单声道 16 位采样，只在本次回调期间有效
     *  @param samples 采样数
     */
    virtual void OnSystemAudio(const int16_t* mono, int samples) = 0;
};

//电脑声音的回环采集
class ShareSystemAudioCapture
{
public:
    ShareSystemAudioCapture();
    ~ShareSystemAudioCapture();

    ShareSystemAudioCapture(const ShareSystemAudioCapture&) = delete;
    ShareSystemAudioCapture& operator=(const ShareSystemAudioCapture&) = delete;

    /**
     *  启动采集线程并等它打开回环（最长约 3 秒）。
     *  @param handler 回调，不拥有，须在 Stop 返回之前一直有效
     *  @return 两种回环方式都打不开时返回 false（线程已退出）
     */
    bool Start(ShareSystemAudioHandler* handler);

    //停止采集并等待线程结束；可重复调用
    void Stop();

    //是否在采集
    bool IsRunning() const;

    //当前是否为「排除本进程树」方式；false 表示退回了普通回环
    bool IsExcludingSelf() const;

private:
    void threadProc();

    ShareSystemAudioHandler*     m_handler;         //回调（不拥有）
    std::unique_ptr<std::thread> m_thread;          //采集线程
    std::atomic<bool>            m_stop;            //是否要求停止
    std::atomic<bool>            m_running;         //回环是否已打开并在采集
    std::atomic<bool>            m_excludingSelf;   //是否为排除本进程树的方式
    void*                        m_readyEvent;      //线程打开回环（或失败）后置位的事件句柄
};

} // namespace screenshare
