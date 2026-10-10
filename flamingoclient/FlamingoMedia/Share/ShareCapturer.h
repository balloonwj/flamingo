/**
 *  屏幕共享的采集器接口，ShareCapturer.h
 *
 *  共享内容有两类：整块显示器（DxgiOutputCapturer，DXGI Desktop Duplication，可指定显卡与输出，自行画上鼠标指针）
 *  与单个窗口（WgcWindowCapturer，Windows.Graphics.Capture，被遮挡、GPU 渲染的窗口都能采到，指针由系统画上）。
 *  两者统一产出自上而下的 BGRA32 帧，交给编码器。采集与编码解耦：本接口只负责「抓」，不碰编码、网络与界面。
 *  设计见 docs/屏幕共享与录制方案.md 5.5。
 *
 *  所有方法都只在同一条采集线程上调用（Start 也在该线程上调用，以便系统对象与线程的套间一致）。
 *
 *  典型用法（采集线程里）：
 *      std::unique_ptr<IShareCapturer> cap(CreateMonitorCapturer(monitor));
 *      if (cap->Start())
 *          while (运行中)
 *          {
 *              ShareCaptureFrame f;
 *              int r = cap->Capture(timeoutMs, f);
 *              if (r == share_capture_ok) 编码 f;
 *          }
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <windows.h>
#include <stdint.h>

namespace screenshare {

//取一帧的结果
enum share_capture_result
{
    share_capture_ok       = 0,   //取到新帧，出参有效（到下一次 Capture 或 Stop 之前有效）
    share_capture_no_frame = 1,   //本次没有新帧（画面没变、或窗口已最小化）—— 不是错误
    share_capture_lost     = 2,   //采集对象失效（锁屏、UAC、分辨率变化、显卡重置），调用方 Stop 后重新 Start
    share_capture_closed   = 3,   //被共享的窗口已关闭，采集不能再继续
    share_capture_fatal    = 4,   //不可恢复的错误
};

//一帧画面：自上而下的 BGRA32
struct ShareCaptureFrame
{
    const uint8_t* m_data;     //第一行像素
    int            m_stride;   //行跨度，单位字节（可能大于 m_width * 4）
    int            m_width;    //宽，单位像素
    int            m_height;   //高，单位像素

    ShareCaptureFrame()
        : m_data(NULL)
        , m_stride(0)
        , m_width(0)
        , m_height(0)
    {
    }
};

//采集器接口
class IShareCapturer
{
public:
    virtual ~IShareCapturer()
    {
    }

    /**
     *  打开采集对象。
     *  @return 成功返回 true
     */
    virtual bool Start() = 0;

    /**
     *  取一帧。
     *  @param timeoutMs 没有新帧时最多等多久，单位毫秒
     *  @param out       [出参] 结果为 share_capture_ok 时有效
     *  @return 取值见 enum share_capture_result
     */
    virtual int Capture(UINT timeoutMs, ShareCaptureFrame& out) = 0;

    //关闭采集对象；可重复调用
    virtual void Stop() = 0;

    //被共享的窗口是否处于最小化（显示器采集恒为 false）；观看端据此提示「共享的窗口已最小化」
    virtual bool IsMinimized() const = 0;

    //被采集内容在虚拟桌面上的当前矩形（物理像素）；边框窗口、标注层据此跟随
    virtual RECT SourceRect() const = 0;
};

/**
 *  建一个显示器采集器（DXGI Desktop Duplication）。
 *  @param adapter     DXGI 显卡序号（ShareMonitor::m_adapter）
 *  @param output      该显卡上的输出序号（ShareMonitor::m_output）
 *  @param monitorRect 显示器在虚拟桌面上的矩形（用于换算鼠标指针位置）
 *  @return 新建的采集器，调用方负责 delete
 */
IShareCapturer* CreateMonitorCapturer(int adapter, int output, const RECT& monitorRect);

/**
 *  建一个窗口采集器（Windows.Graphics.Capture）。系统不支持时 Start 返回 false。
 *  @param hwnd 被共享的顶层窗口
 *  @return 新建的采集器，调用方负责 delete
 */
IShareCapturer* CreateWindowCapturer(HWND hwnd);

/**
 *  系统是否支持窗口采集（Windows.Graphics.Capture 的激活工厂能取到）。可在任意线程调用。
 */
bool IsWindowCaptureSupported();

} // namespace screenshare
