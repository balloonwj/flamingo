/**
 *  摄像头采集（Media Foundation Source Reader），MfCameraCapture.h
 *
 *  第三期视频管线的采集层：用 Media Foundation 打开系统默认摄像头，采集线程持续读帧，
 *  把每帧转成 NV12 经回调交给上层（预览 / H.264 编码）。对上层只暴露「NV12 帧 + 宽高 +
 *  行跨度」这一种形态——设备原始格式（YUY2 / MJPG 等）到 NV12 的转换由 MF Source Reader
 *  内部完成（设其输出子类型为 NV12，MF 自动插入转换器）。
 *
 *  线程模型：仿 WasapiCapture——采集在自己的工作线程里完成全部 MF/COM 初始化（线程内
 *  MFStartup + CoInitializeEx MTA），Start 阻塞等工作线程报告初始化结果（含协商到的宽高）
 *  后返回；不触碰调用方(UI 线程)的 COM 套间。OnVideoFrame 在采集线程上触发，实现方须自保
 *  线程安全、勿阻塞（典型做法：拷出帧后转投 UI 线程绘制 / 投编码队列）。
 *
 *  本模块碰真实设备，不在单测覆盖范围（靠 --camerapreview 运行期目视验证）；其依赖的
 *  NV12→BGRA 转换是 VideoFrameConvert，已有单测。
 *
 *  balloonwj@qq.com   2026-06-15
 */
#pragma once

#include <stdint.h>

namespace call {

//摄像头采集失败原因。Start 返回 false 后可用 MfCameraCapture::FailReason 查询；采集中途断掉时
//随 MfVideoFrameHandler::OnVideoCaptureStopped 上报。上层据此给用户不同措辞的提示。
enum CameraFailReason
{
    kCameraFailNone     = 0,   //没有失败：采集正常运行中，或尚未启动过
    kCameraFailNoDevice = 1,   //枚举不到任何视频采集设备：机器没接摄像头 / 设备被禁用 / 系统隐私设置禁止访问摄像头
    kCameraFailOpen     = 2,   //枚举到设备但打开、协商格式失败：常见于设备已被其它程序独占，也可能是驱动异常
    kCameraFailStream   = 3,   //已打开并出过帧，中途读帧失败断流：常见于另一个程序抢走了摄像头，或设备被拔出
};

//采集帧回调。OnVideoFrame 在采集线程上下文触发。
class MfVideoFrameHandler
{
public:
    virtual ~MfVideoFrameHandler() {}

    /**
     *  采集到一帧（NV12）。
     *  @param data   NV12 帧缓冲起始（Y 平面起点；UV 平面位于 data + stride*height）；
     *                仅本次回调内有效，需留存须自行拷贝
     *  @param width  画面宽（像素）
     *  @param height 画面高（像素）
     *  @param stride 行跨度（字节，Y/UV 同跨度；通常 == width）
     */
    virtual void OnVideoFrame(const uint8_t* data, int width, int height, int stride) = 0;

    /**
     *  采集意外中断：读帧失败 / 设备断流，采集线程随后退出，之后不会再有 OnVideoFrame。
     *  本端主动 Stop 导致的正常退出**不会**触发本回调。在采集线程上下文触发。
     *  默认实现为空——只关心画面的调试入口可以不覆写；正式通话链路覆写它把"摄像头没了"报给界面。
     *  @param reason 中断原因，取 CameraFailReason（此处一般是 kCameraFailStream）
     */
    virtual void OnVideoCaptureStopped(int /*reason*/)
    {
    }
};

//摄像头采集：MF Source Reader 打开默认摄像头、采集线程读 NV12 帧回调上层。
class MfCameraCapture
{
public:
    MfCameraCapture();
    ~MfCameraCapture();

    MfCameraCapture(const MfCameraCapture&) = delete;
    MfCameraCapture& operator=(const MfCameraCapture&) = delete;

    /**
     *  打开默认摄像头并起采集线程。阻塞直到设备就绪 / 失败。
     *  @param handler   采集回调（非空；生命周期须长于采集，至少持续到 Stop 返回）
     *  @param outWidth  [出参] 协商到的画面宽（像素）
     *  @param outHeight [出参] 协商到的画面高（像素）
     *  @return 成功返回 true；无摄像头 / 打开失败返回 false
     */
    bool Start(MfVideoFrameHandler* handler, int& outWidth, int& outHeight);

    //停止采集并结束采集线程。幂等。
    void Stop();

    /**
     *  取最近一次失败原因，取值见 CameraFailReason。Start 返回 false 后调用可区分"机器上没有
     *  摄像头"与"摄像头打不开（多半被占用）"；采集中途断流后同样会被更新为 kCameraFailStream。
     *  @return CameraFailReason 之一；从未失败过返回 kCameraFailNone
     */
    int FailReason() const;

private:
    //采集线程体：MF 初始化、循环 ReadSample、转 NV12 回调、清理。
    void CaptureProc();

private:
    struct Impl;    //隐藏 MF/COM 细节，避免本头泄漏 Media Foundation 头
    Impl* m_impl;
};

} // namespace call
