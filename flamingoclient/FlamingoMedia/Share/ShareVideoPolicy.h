/**
 *  屏幕共享画面编码的纯逻辑：编码尺寸、发帧节奏、带缩放的 BGRA→NV12 转换，ShareVideoPolicy.h
 *
 *  方案（docs/屏幕共享与录制方案.md 5.6）：继续用系统自带的 H.264 编码器；长边不超过 1920；画面不变不编码；
 *  帧率上限 15 fps；每 4 秒一个兜底关键帧；收到关键帧请求时立即把最近一帧编成关键帧（画面没变也编，
 *  后加入的观看者才能马上看到画面）。这几条规则与颜色转换抽成纯逻辑，便于单元测试，见
 *  flamingoclient/Source/Tests/ShareVideoPolicyTests.cpp。
 *
 *  典型用法（发起人的采集编码线程里）：
 *      ShareFramePacer pacer(kShareMaxFps, kShareKeyFrameIntervalMs);
 *      ShareFramePacer::Decision d = pacer.decide(nowMs, gotNewFrame, keyFrameRequested);
 *      if (d.m_encode)
 *      {
 *          ConvertBgraToNv12(frame.m_data, frame.m_stride, frame.m_width, frame.m_height, nv12, encW, encH);
 *          if (d.m_keyframe) encoder.ForceKeyFrame();
 *          encoder.EncodeNv12(...);
 *          pacer.onEncoded(nowMs, d.m_keyframe);
 *      }
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace screenshare {

//编码画面长边的上限，单位像素
const int kShareMaxLongSide = 1920;
//发帧帧率上限
const int kShareMaxFps = 15;
//兜底关键帧间隔，单位毫秒
const int64_t kShareKeyFrameIntervalMs = 4000;
//屏幕画面的目标码率，单位 bit/s（方案 1.5~2.5 Mbps 取中间值）
const int kShareBitrateBps = 2000000;
//摄像头画面：编码尺寸的上限（640×360）、帧率、码率与关键帧间隔（帧数，4 秒），docs/屏幕共享与录制方案.md 5.8
const int kShareCameraMaxW = 640;
const int kShareCameraMaxH = 360;
const int kShareCameraFps = 15;
const int kShareCameraBitrateBps = 400000;
const int kShareCameraGopFrames = 60;
//同屏最多解码、显示的摄像头路数（超出的人只显示名字、不解码）
const int kShareMaxCameraTiles = 9;

/**
 *  计算编码尺寸：长边超过 maxLongSide 时按比例缩小，然后宽高都取偶数（H.264 的 4:2:0 要求），最小 2×2。
 *  @param srcW / srcH   源画面尺寸，单位像素
 *  @param maxLongSide   长边上限，单位像素；不大于 0 时不限
 *  @param outW / outH   [出参] 编码尺寸；源尺寸非法时为 0×0
 */
void ComputeShareEncodeSize(int srcW, int srcH, int maxLongSide, int& outW, int& outH);

/**
 *  把自上而下的 BGRA 画面转成连续的 NV12（BT.601 limited range，与 call::Nv12ToBgra 互逆），必要时用双线性插值缩放。
 *  @param bgra      源画面第一行
 *  @param stride    源行跨度，单位字节
 *  @param srcW / srcH 源尺寸
 *  @param nv12      输出缓冲，容量至少 dstW * dstH * 3 / 2 字节：先 Y 平面（行跨度 dstW），紧接 UV 平面
 *  @param dstW / dstH 输出尺寸，须为正偶数
 */
void ConvertBgraToNv12(const uint8_t* bgra, int stride, int srcW, int srcH, uint8_t* nv12, int dstW, int dstH);

/**
 *  ConvertBgraToNv12 的两种实现，参数与结果都与 ConvertBgraToNv12 相同（逐字节一致，单元测试 SSVP7）。
 *  ConvertBgraToNv12 在 Release 版用 SSE2 实现、在 Debug 版用逐像素实现：实测 1080p 每帧 Release 版由约 8 ms 降到
 *  约 2.7 ms，而 Debug 版不优化向量指令，SSE2 实现反而由约 18 ms 升到约 35 ms（第 9 期 9a）。
 *  这两个函数供单元测试与性能对比直接调用，产品代码请用 ConvertBgraToNv12。
 */
void ConvertBgraToNv12Sse2(const uint8_t* bgra, int stride, int srcW, int srcH, uint8_t* nv12, int dstW, int dstH);

void ConvertBgraToNv12Reference(const uint8_t* bgra, int stride, int srcW, int srcH, uint8_t* nv12, int dstW,
                                int dstH);

/**
 *  计算摄像头的编码尺寸：按比例缩进 kShareCameraMaxW × kShareCameraMaxH 的框内（只缩不放），宽高取偶数，最小 2×2。
 *  @param srcW / srcH 摄像头输出尺寸
 *  @param outW / outH [出参] 编码尺寸；源尺寸非法时为 0×0
 */
void ComputeCameraEncodeSize(int srcW, int srcH, int& outW, int& outH);

/**
 *  缩放 NV12 画面（最近邻取样，摄像头小画面够用）。尺寸相同时直接拷贝。
 *  @param src       源画面：Y 平面行跨度为 srcStride，UV 平面紧接在 Y 平面（srcStride × srcH）之后、行跨度同为 srcStride
 *  @param srcStride 源行跨度，单位字节
 *  @param srcW / srcH 源尺寸，须为正偶数
 *  @param dst       输出缓冲，容量至少 dstW * dstH * 3 / 2：先 Y 平面（行跨度 dstW），紧接 UV 平面
 *  @param dstW / dstH 输出尺寸，须为正偶数
 */
void ScaleNv12(const uint8_t* src, int srcStride, int srcW, int srcH, uint8_t* dst, int dstW, int dstH);

//发帧节奏：决定这一轮要不要编码、要不要编成关键帧
class ShareFramePacer
{
public:
    //一轮的决定
    struct Decision
    {
        bool m_encode;     //是否编码这一帧（没有新画面但收到关键帧请求时，编码最近一帧）
        bool m_keyframe;   //是否编成关键帧

        Decision();
    };

    /**
     *  @param maxFps            帧率上限；不大于 0 时按 kShareMaxFps
     *  @param keyFrameIntervalMs 兜底关键帧间隔，单位毫秒；不大于 0 时按 kShareKeyFrameIntervalMs
     */
    ShareFramePacer(int maxFps, int64_t keyFrameIntervalMs);

    /**
     *  决定这一轮怎么做。规则：
     *    - 从没编过码：有新画面就编，并编成关键帧；
     *    - 收到关键帧请求：立即编（没有新画面就编最近一帧），编成关键帧，不受帧率上限约束；
     *    - 有新画面且距上次编码不少于 1000 / maxFps 毫秒：编；距上次关键帧满兜底间隔则编成关键帧；
     *    - 其余情况不编（画面没变、或帧率已到上限）。
     *  @param nowMs             当前时间，单位毫秒（单调递增）
     *  @param hasNewFrame       这一轮是否采到了新画面
     *  @param keyFrameRequested 是否收到了关键帧请求（服务端发来，或画面尺寸变化需要重新开始）
     *  @param hasLastFrame      手里是否有可重编的最近一帧（没有时关键帧请求要等下一帧画面）
     */
    Decision decide(int64_t nowMs, bool hasNewFrame, bool keyFrameRequested, bool hasLastFrame) const;

    /**
     *  编码完一帧后调用，更新计时。
     *  @param nowMs    当前时间，单位毫秒
     *  @param keyframe 这一帧是否关键帧
     */
    void onEncoded(int64_t nowMs, bool keyframe);

    //最小发帧间隔，单位毫秒
    int64_t minIntervalMs() const;

private:
    int64_t m_minIntervalMs;      //最小发帧间隔，单位毫秒
    int64_t m_keyIntervalMs;      //兜底关键帧间隔，单位毫秒
    bool    m_everEncoded;        //是否编过至少一帧
    int64_t m_lastEncodeMs;       //上次编码的时刻
    int64_t m_lastKeyFrameMs;     //上次编关键帧的时刻
};

} // namespace screenshare
