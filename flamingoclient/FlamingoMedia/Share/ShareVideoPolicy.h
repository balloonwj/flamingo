/**
 *  屏幕共享画面编码的纯逻辑：编码尺寸、发帧节奏、带缩放的 BGRA→NV12 转换，ShareVideoPolicy.h
 *
 *  方案（docs/屏幕共享与录制方案.md 5.6）：继续用系统自带的 H.264 编码器；长边不超过 1920（2026-10-15 起按发起人选的
 *  清晰度为 1920 / 2560 / 3840，码率按编码像素数换算，见 ComputeScreenVideoProfile 与 docs/远程桌面H264与共享画质提升方案.md）；画面不变不编码；
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
#include <vector>

namespace screenshare {

//编码画面长边的上限，单位像素。2026-10-15 起这是「1080p」档的上限，另有 2K、4K 两档（ShareMaxLongSideForQuality）
const int kShareMaxLongSide = 1920;
//「2K」「4K」档的编码长边上限，单位像素（docs/远程桌面H264与共享画质提升方案.md 4.1）
const int kShare2kLongSide = 2560;
const int kShare4kLongSide = 3840;
//发帧帧率上限
const int kShareMaxFps = 15;
//画面被别人控制键鼠期间的发帧帧率上限（用户 2026-10-17 定，与远程桌面的 kRdVideoMaxFps 相同）：共享画面里的指针是画进视频的，
//帧率决定控制者看到的指针每秒能动几次，15 帧时明显跟不上手。控制结束后回到 kShareMaxFps
const int kShareControlledFps = 30;
//兜底关键帧间隔，单位毫秒
const int64_t kShareKeyFrameIntervalMs = 4000;
//屏幕画面的目标码率，单位 bit/s（方案 1.5~2.5 Mbps 取中间值）。2026-10-15 起是编码像素数不超过 1920×1080 时的最高码率，
//更大的编码尺寸按像素数换算（ComputeScreenVideoProfile）
const int kShareBitrateBps = 2000000;
//远程桌面改用 H.264 后的编码参数（方案 4.6，用户 2026-10-14 定）：长边上限、帧率上限、1080p 时的最高码率（bit/s）
const int kRdVideoMaxLongSide = 2560;
const int kRdVideoMaxFps = 30;
const int kRdVideoBitrateBps = 3000000;
//码率换算的基准像素数：1920×1080
const int64_t kScreenRefPixels = 1920LL * 1080;
//换算后的码率按这个粒度四舍五入，单位 bit/s
const int kScreenBitrateRoundBps = 100000;

//屏幕共享清晰度档位（发起人在系统设置「音视频」页选，存进用户配置的数值）
enum ShareQualityLevel
{
    share_quality_1080p = 0,   //1080p：编码长边不超过 1920，与 2026-10-15 之前相同
    share_quality_2k    = 1,   //2K（默认）：编码长边不超过 2560，2K 屏原尺寸编码
    share_quality_4k    = 2,   //4K：编码长边不超过 3840，4K 屏原尺寸编码，对观看者的电脑与网络要求更高
};
//默认档位
const int kShareQualityDefault = share_quality_2k;

//屏幕画面编码的一组参数（共享与远程桌面各有一组）
struct ScreenVideoParams
{
    int m_maxLongSide;   //编码长边上限，单位像素
    int m_fps;           //帧率上限
    int m_baseBps;       //编码像素数不超过 1920×1080 时的最高码率（bit/s）
    int m_floorBps;      //编码像素数不超过 1920×1080 时的最低码率（bit/s）

    ScreenVideoParams();
};

//按源尺寸算出的编码档位
struct ScreenVideoProfile
{
    int m_encW;          //编码宽，单位像素
    int m_encH;          //编码高，单位像素
    int m_maxBps;        //最高码率（码率自适应的上限，也是初始码率），bit/s
    int m_minBps;        //最低码率（码率自适应的下限），bit/s

    ScreenVideoProfile();
};

/**
 *  按源尺寸算出编码档位：编码尺寸沿用 ComputeShareEncodeSize；码率按编码像素数线性换算
 *  （基准 × 编码宽 × 编码高 / (1920 × 1080)，按 100 kbps 四舍五入），编码像素数不超过 1920×1080 时不降，仍取基准值。
 *  @param srcW / srcH 源画面尺寸，单位像素
 *  @param params      编码参数（ShareVideoParamsForQuality 或 RdScreenVideoParams）
 *  @param out         [出参] 编码档位；源尺寸非法时全为 0
 */
void ComputeScreenVideoProfile(int srcW, int srcH, const ScreenVideoParams& params, ScreenVideoProfile& out);

/**
 *  码率范围变化时（源尺寸变了），把当前码率按「当前码率 / 旧上限」的比例换到新范围，避免网络正拥塞时一换窗口就跳回最高码率。
 *  @param currentBps 当前目标码率，bit/s
 *  @param oldMaxBps  旧范围的上限；不大于 0 时直接取新上限
 *  @param newMaxBps  新范围的上限
 *  @param newMinBps  新范围的下限；大于新上限时按新上限
 *  @return 新范围里的目标码率，夹在新下限与新上限之间
 */
int ScaleBitrateToRange(int currentBps, int oldMaxBps, int newMaxBps, int newMinBps);

/**
 *  清晰度档位对应的编码长边上限。
 *  @param quality ShareQualityLevel 的取值；未知取值（配置损坏、旧配置没有这一项）按默认的 2K 档
 *  @return 1920 / 2560 / 3840
 */
int ShareMaxLongSideForQuality(int quality);

/**
 *  屏幕共享某一档清晰度的编码参数：长边按档位，帧率 kShareMaxFps，1080p 基准 kShareBitrateBps，下限 kShareMinBitrateBps。
 *  @param quality ShareQualityLevel 的取值；未知取值按 2K 档
 */
ScreenVideoParams ShareVideoParamsForQuality(int quality);

/**
 *  屏幕共享推流实际使用的编码参数（2026-10-17）：在 ShareVideoParamsForQuality 的基础上，画面被别人控制期间帧率换成
 *  kShareControlledFps，长边上限与码率不变。帧率变化时编码器会重建（ShareVideoEncoder::SetParams），下一帧为关键帧。
 *  @param quality    ShareQualityLevel 的取值；未知取值按 2K 档
 *  @param controlled 这块画面是否正被别人控制键鼠
 */
ScreenVideoParams ShareVideoParamsForControl(int quality, bool controlled);

//远程桌面的编码参数（固定，不提供设置）：长边 2560、30 fps、1080p 基准 3 Mbps、下限 300 kbps
ScreenVideoParams RdScreenVideoParams();
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
 *  BGRA 画面的双线性缩放（2026-10-15，docs/远程桌面H264与共享画质提升方案.md 4.2「发送端缩放」）。
 *  像素中心对齐；权重为 7 位定点（0~128）：先把两个源行按纵向权重混合成 16 位的中间行，再按横向权重混合两个中间值，
 *  加 8192 后右移 14 位。这样纵、横两步都能用 SSE2 的 16 位乘法与 madd 实现，逐像素实现与 SSE2 实现做完全相同的运算，
 *  结果逐字节一致（单元测试 SSVP14）。列映射表与中间行按尺寸复用，同一尺寸反复缩放不再分配内存。
 *  一个实例只在一条线程上使用。
 */
class BgraScaler
{
public:
    BgraScaler();

    /**
     *  缩放一帧：Release 版用 SSE2 实现，Debug 版用逐像素实现（与 ConvertBgraToNv12 相同的取舍）。
     *  @param src       源画面第一行（自上而下）
     *  @param srcStride 源行跨度，单位字节
     *  @param srcW / srcH 源尺寸，须为正
     *  @param dst       输出缓冲，容量至少 dstStride × dstH
     *  @param dstStride 输出行跨度，单位字节，不小于 dstW × 4
     *  @param dstW / dstH 输出尺寸，须为正
     */
    void Scale(const uint8_t* src, int srcStride, int srcW, int srcH, uint8_t* dst, int dstStride, int dstW, int dstH);

    //Scale 的两种实现，参数与结果都与 Scale 相同；供单元测试与性能对比直接调用
    void ScaleSse2(const uint8_t* src, int srcStride, int srcW, int srcH, uint8_t* dst, int dstStride, int dstW,
                   int dstH);
    void ScaleReference(const uint8_t* src, int srcStride, int srcW, int srcH, uint8_t* dst, int dstStride, int dstW,
                        int dstH);

private:
    //按源宽与目标宽准备列映射表，并把中间行放大到源宽 × 4 个值；尺寸与上次相同时什么都不做
    void prepare(int srcW, int dstW);

    std::vector<int>     m_x0;         //每个目标列取样的左边源列
    std::vector<int>     m_x1;         //每个目标列取样的右边源列（右边缘时与左边相同）
    std::vector<int>     m_wx;         //每个目标列右边源列的权重（0~128）
    std::vector<int32_t> m_wpair;      //SSE2 用的权重对：低 16 位为左边权重（128 - wx），高 16 位为右边权重 wx
    std::vector<int16_t> m_row;        //纵向混合后的中间行（每个值不超过 255 × 128）
    int                  m_tableSrcW;  //列映射表对应的源宽；0 表示还没准备
    int                  m_tableDstW;  //列映射表对应的目标宽
};

/**
 *  带缩放的 BGRA → NV12 转换器（2026-10-15）：结果与 ConvertBgraToNv12 相同，但缩放缓冲与 BgraScaler 的表按尺寸复用，
 *  编码器每帧调用时不再分配约 14 MB 的临时缓冲（4K 源缩到 2K 时）。一个实例只在一条线程上使用。
 */
class Nv12FrameConverter
{
public:
    /**
     *  转换一帧，参数含义与 ConvertBgraToNv12 相同。
     */
    void Convert(const uint8_t* bgra, int stride, int srcW, int srcH, uint8_t* nv12, int dstW, int dstH);

private:
    BgraScaler           m_scaler;   //缩放器（复用列映射表与中间行）
    std::vector<uint8_t> m_scaled;   //缩放后的 BGRA 画面（按目标尺寸复用）
};

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

    /**
     *  运行中更换帧率上限（2026-10-17，屏幕共享被控制期间提到 kShareControlledFps）。只改最小发帧间隔，
     *  上次编码与上次关键帧的时刻保留，兜底关键帧的计时不受影响。
     *  @param maxFps 新的帧率上限；不大于 0 时按 kShareMaxFps
     */
    void SetMaxFps(int maxFps);

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
