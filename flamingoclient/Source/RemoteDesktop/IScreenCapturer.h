/**
 *  远程桌面抓屏抽象接口，IScreenCapturer.h
 *
 *  把"抓一帧桌面像素"抽象成统一接口，让 DXGI Desktop Duplication 与 GDI BitBlt
 *  两种后端可互换：主路径用 DXGI（CDxgiScreenCapturer），出问题运行期降级到 GDI
 *  （CGdiScreenCapturer，后续增量）。两个实现都统一产出 BGRA32 top-down 帧
 *  （FrameView），交由 CScreenFrameEncoder 转成现有协议引擎帧。
 *
 *  抓取与编码解耦：本接口只负责"抓"，不碰协议、不碰网络。
 *
 *  balloonwj@qq.com   2026-06-11
 */
#pragma once

#include <windows.h>
#include "RdCaptureTypes.h"

namespace remotedesktop {

//抓一帧的结果。区分"无新帧"与"需重建"是降级与边界处理的命门（见改造方案 §7）。
enum CaptureResult
{
    kCaptureOk         = 0,   //抓到新帧，out 视图有效
    kCaptureTimeout    = 1,   //本次无新帧（屏幕没变 / AcquireNextFrame 超时）—— 不是错误
    kCaptureAccessLost = 2,   //duplication 失效（锁屏 / UAC / 切用户 / 分辨率变 / 驱动重置）—— 需销毁重建
    kCaptureFatal      = 3,   //不可恢复错误 —— 调用方应降级到 GDI 后端
};

//抓屏抓取器抽象：DXGI / GDI 各一实现，统一产出 BGRA32 top-down 帧。
class IScreenCapturer
{
public:
    virtual ~IScreenCapturer() {}

    /**
     *  初始化抓取器（DXGI：建 device / duplication；GDI：建桌面 DC / DIB）。
     *  @return 成功 true；失败 false（调用方据此降级到另一后端）
     */
    virtual bool Init() = 0;

    /**
     *  抓取一帧。
     *  @param timeoutMs 无新帧时最长阻塞毫秒（建议 = 抓屏周期，既限帧又让出 CPU）
     *  @param out       [出参] 返回 kCaptureOk 时填充内部缓冲视图（零拷贝，下次抓帧前有效）
     *  @return 见 CaptureResult
     */
    virtual CaptureResult CaptureFrame(UINT timeoutMs, FrameView& out) = 0;

    //目标屏幕宽 / 高（像素）。
    virtual int ScreenWidth()  const = 0;
    virtual int ScreenHeight() const = 0;
};

} // namespace remotedesktop
