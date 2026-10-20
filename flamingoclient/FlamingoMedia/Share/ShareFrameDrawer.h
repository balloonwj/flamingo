/**
 *  把一帧 BGRA 画面画到窗口上的绘制器，ShareFrameDrawer.h
 *
 *  2026-10-15（docs/远程桌面H264与共享画质提升方案.md A7）。观看窗原来一律用 HALFTONE 模式的 StretchDIBits 缩放绘制，A0 实测
 *  2560×1440 缩到 1920×1080 每次约 29.5 毫秒、3840×2160 约 43 毫秒；屏幕共享 15 fps、远程桌面 30 fps 时，UI 线程大半时间花在绘制上。
 *  本类在缩小时先用 BgraDownscaler 缩到目标尺寸，再用 SetDIBitsToDevice 原样贴上，不经 GDI 缩放；原尺寸时直接
 *  SetDIBitsToDevice；放大时仍用 HALFTONE。BgraDownscaler 在比例不超过 2 时做一次 SSE2 双线性（BgraScaler）；超过 2 时先按
 *  2×2 平均逐次减半，直到剩下的比例不超过 2，再做一次双线性：只做一次双线性在大比例下会跳过像素，文字出现锯齿（SSFD3）。
 *  远程桌面观看窗默认宽 1042，显示 2560 宽的画面约 2.46 倍，需要这一步（A0 实测 HALFTONE 缩到 1280×720 每次约 19 毫秒）。
 *  判断规则 ShareUsesFastDownscale 与云端合成（CloudFitUsesFastScale）共用。
 *  一个实例只在一条线程（UI 线程）上使用；缩放器的列映射表与缩放缓冲按尺寸复用。
 *  典型用法：m_drawer.Draw(hdc, fitRect, &frame[0], width, height, width * 4);
 *  单元测试见 Source/Tests/ShareFrameDrawerTests.cpp（[screenshare]），耗时对比见 ShareVideoPerfTests.cpp 的 SSPERF2。
 *
 *  balloonwj@qq.com   2026-10-15
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <vector>

#include "ShareVideoPolicy.h"   // BgraScaler

namespace screenshare {

//绘制所走的路径（单元测试与日志用）
enum share_draw_path
{
    share_draw_none       = 0,   //没有画（画面或目标矩形不合法）
    share_draw_copy       = 1,   //原尺寸：SetDIBitsToDevice 直接贴
    share_draw_fast_scale = 2,   //缩小：BgraDownscaler 缩好后 SetDIBitsToDevice 贴
    share_draw_gdi        = 3,   //放大（或一个方向放大）：HALFTONE 模式的 StretchDIBits
};

/**
 *  srcW × srcH 的画面画成 dstW × dstH 时能否用快速缩放（BgraDownscaler）：两个方向都不放大、且不是原尺寸时返回 true。
 *  @param srcW、srcH 画面的宽高（像素）
 *  @param dstW、dstH 目标的宽高（像素）
 */
bool ShareUsesFastDownscale(int srcW, int srcH, int dstW, int dstH);

/**
 *  任意比例的 BGRA 缩小：比例不超过 2 时做一次双线性（BgraScaler，与它逐字节一致）；超过 2 时先按 2×2 平均逐次减半
 *  （用 BgraScaler 缩到正好一半：像素中心对齐时正好一半的双线性就是 2×2 平均），直到剩下的比例不超过 2，再做一次双线性。
 *  每一步各用一个 BgraScaler，列映射表与中间缓冲按尺寸复用。一个实例只在一条线程上使用。
 */
class BgraDownscaler
{
public:
    BgraDownscaler();

    /**
     *  缩小一帧（调用方保证两个方向都不放大，见 ShareUsesFastDownscale）。参数含义与 BgraScaler::Scale 相同。
     *  @param src       源画面第一行（自上而下）
     *  @param srcStride 源行跨度（字节）
     *  @param srcW、srcH 源尺寸，须为正
     *  @param dst       输出缓冲，容量至少 dstStride × dstH
     *  @param dstStride 输出行跨度（字节），不小于 dstW × 4
     *  @param dstW、dstH 输出尺寸，须为正
     */
    void Scale(const uint8_t* src, int srcStride, int srcW, int srcH, uint8_t* dst, int dstStride, int dstW, int dstH);

    /**
     *  缩到 dstW × dstH 之前要先减半几次：某个方向的目标边长的 2 倍仍小于当前边长，就把这个方向减半，直到两个方向都不超过 2 倍。
     *  @return 减半的次数（每次可能同时减两个方向）；不超过 kMaxHalvingSteps
     */
    static int HalvingSteps(int srcW, int srcH, int dstW, int dstH);

    //减半的最多次数：64 倍以上的缩小不再继续减半（远超过实际用到的比例）
    static const int kMaxHalvingSteps = 6;

private:
    BgraScaler           m_halvers[kMaxHalvingSteps];   //每次减半各用一个缩放器（尺寸固定时列映射表不重建）
    BgraScaler           m_final;                       //最后一次双线性
    std::vector<uint8_t> m_work[kMaxHalvingSteps];      //每次减半的输出（行跨度 = 宽 × 4），按尺寸复用
};

//BGRA 画面的窗口绘制器
class ShareFrameDrawer
{
public:
    ShareFrameDrawer();

    /**
     *  把画面画到 hdc 的 dest 矩形（调用方已按宽高比算好矩形，本函数不再留黑边）。
     *  @param hdc    目标设备上下文
     *  @param dest   目标矩形（hdc 的逻辑坐标）
     *  @param bgra   画面像素，自上而下的 BGRA；不拥有
     *  @param width、height 画面的宽高（像素）
     *  @param stride 画面的行跨度（字节），不小于 width × 4
     *  @return 实际走的路径，取值见 share_draw_path
     */
    int Draw(HDC hdc, const RECT& dest, const uint8_t* bgra, int width, int height, int stride);

    //最近一次 Draw 走的路径（单元测试用）
    int LastPath() const;

private:
    //把一块紧凑排列（行跨度 = w × 4）的 BGRA 原样贴到 hdc 的 (x, y)
    static void blit(HDC hdc, int x, int y, const uint8_t* bgra, int w, int h);

    BgraDownscaler       m_scaler;    //快速缩小（列映射表与中间缓冲按尺寸复用）
    std::vector<uint8_t> m_scaled;    //缩放结果（行跨度 = 目标宽 × 4），按目标尺寸复用
    std::vector<uint8_t> m_packed;    //源行跨度不等于 width × 4 时，原尺寸绘制前先紧凑排列到这里
    int                  m_lastPath;  //最近一次 Draw 走的路径，取值见 share_draw_path
};

} // namespace screenshare
