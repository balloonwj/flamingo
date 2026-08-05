/**
 *  远程桌面 GDI 抓取器（被控端，降级后端），GdiScreenCapturer.h
 *
 *  IScreenCapturer 的 GDI 实现：用 GDI BitBlt 把主显示器桌面拷进一张 32 位 top-down
 *  DIBSection，产出 BGRA32（与 DXGI 后端同格式）。作为 DXGI Desktop Duplication 的
 *  运行期降级——DXGI 初始化失败 / ACCESS_LOST 时切到本后端，保证不黑屏。
 *
 *  与旧引擎一致：用 SRCCOPY（不带 CAPTUREBLT），故抓出的画面不含鼠标光标，光标由
 *  上层单独发位置 + 类型索引、观看窗本地绘制。
 *
 *  balloonwj@qq.com   2026-06-11
 */
#pragma once

#include <windows.h>
#include "IScreenCapturer.h"

namespace remotedesktop {

//GDI BitBlt 抓取器（降级后端）。
class CGdiScreenCapturer : public IScreenCapturer
{
public:
    CGdiScreenCapturer();
    virtual ~CGdiScreenCapturer();

    // ==== IScreenCapturer ====
    virtual bool Init();
    virtual CaptureResult CaptureFrame(UINT timeoutMs, FrameView& out);
    virtual int ScreenWidth()  const;
    virtual int ScreenHeight() const;

private:
    void cleanup();

    HDC     m_deskDC;   //桌面 DC（GetDC(NULL)）
    HDC     m_memDC;    //内存 DC（选入 m_dib）
    HBITMAP m_dib;      //32 位 top-down DIBSection（抓取目标）
    HBITMAP m_oldBmp;   //m_memDC 原选入位图（清理时还原）
    void*   m_bits;     //m_dib 的像素地址（BGRA32，top-down）
    int     m_width;    //主屏宽（像素）
    int     m_height;   //主屏高（像素）
};

} // namespace remotedesktop
