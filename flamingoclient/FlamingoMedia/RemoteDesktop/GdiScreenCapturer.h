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
 *  2026-10-12 起每次抓帧前比较主屏尺寸，变了（被控端改了分辨率）就按新尺寸重建 DIB，报告新尺寸；之前一直按 Init 时的
 *  尺寸抓，画面被截掉或留空。取主屏尺寸的函数可以替换（单元测试模拟分辨率变化，RES4）。
 *
 *  2026-10-17 起遵守 CaptureFrame 的等待时长：GDI 没有「画面变了」的通知，原先每次调用都立即拷屏，远程桌面的 H.264
 *  推流循环因此不停地拷屏。现在两次拷屏之间至少间隔等待时长（RES8）。画面与上一帧相同的判断不在这里，由上层
 *  RdScreenCapturer 对 DXGI、GDI 两种后端统一做（RdStoreFrameIfChanged）。拷屏函数可以替换（单元测试写入纯色画面）。
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
    //取主屏尺寸（像素）的函数
    typedef SIZE (*ScreenSizeProbe)();

    CGdiScreenCapturer();
    virtual ~CGdiScreenCapturer();

    /**
     *  替换取主屏尺寸的函数（单元测试用，2026-10-12）。对之后新建与已有的抓取器都生效。
     *  @param probe 取尺寸的函数；NULL 恢复默认（GetSystemMetrics(SM_CXSCREEN / SM_CYSCREEN)）
     */
    static void SetScreenSizeProbeForTest(ScreenSizeProbe probe);

    //把屏幕画面拷进内存 DC 的函数：memDC 已选入抓取目标 DIB，deskDC 为桌面 DC，width / height 为主屏尺寸（像素）；
    //成功返回非 0
    typedef BOOL (*ScreenBlit)(HDC memDC, HDC deskDC, int width, int height);

    /**
     *  替换拷屏函数（单元测试用，2026-10-17）：用例写入固定的画面，不依赖桌面，隐藏桌面上也能运行。
     *  对之后新建与已有的抓取器都生效。
     *  @param blit 拷屏函数；NULL 恢复默认（从桌面 DC BitBlt，SRCCOPY）
     */
    static void SetScreenBlitForTest(ScreenBlit blit);

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
    ULONGLONG m_lastBlitMs;         //上一次成功拷屏的时刻（GetTickCount64，毫秒），两次拷屏之间据它补足等待时长
    bool      m_hasBlit;            //Init 之后是否成功拷过屏（没有时不等待）
};

} // namespace remotedesktop
