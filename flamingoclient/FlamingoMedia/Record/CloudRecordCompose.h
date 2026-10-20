/**
 *  云端录像离线合成的纯逻辑部分，CloudRecordCompose.h
 *
 *  第 27a 期（docs/屏幕共享与录制方案.md 12.6.3）。原始帧文件（.frec）里每条记录带「相对开始时刻的毫秒数」（imgserver 收到这一帧的
 *  时刻，含暂停的时长），合成要把它们排到一条「输出时间轴」上。这里放不依赖解码器与 GDI 的几部分，单元测试见
 *  Source/Tests/CloudRecordClientTests.cpp（标签 [cloudrec]）：
 *    - CloudComposeTimeline：去掉暂停的时段，把记录的相对毫秒换成输出时间轴上的毫秒；
 *    - CloudAudioPlacer：声音按发送端的时间戳（每 20 毫秒一包，间隔均匀）排位，第一包按到达时刻定锚，之后与到达时刻偏离超过
 *      kReanchorMs 时重新定锚（发送端重启采集、暂停之后）。到达时刻有网络抖动，直接按到达时刻排会让声音断续；
 *    - CloudAudioMixBuffer：各路声音按样点位置叠加（32 位累加后截到 16 位），按顺序取走；
 *    - ChooseCloudPipSeat：画中画取谁的摄像头，规则与共享中本地录制相同（第 6.1 节）；
 *    - CloudVisibleScreenSeats：哪些画面还在共享：发起人在前，其余按座位升序，超过 kScreenStaleMs 没有新帧的不再显示；
 *    - 合成提速（2026-10-08）：CloudFitRect / CloudFitIsOneToOne / CloudCopyFrameRows
 *      让原尺寸的画面直接逐行复制进画布、不经 GDI 缩放；CloudCanvasKey / CloudCanvasReusable / CloudHasTimedStrokes
 *      判断这一帧的画布能否沿用上一帧；
 *    - 2K、4K 画面的快速缩小（2026-10-15）：CloudFitUsesFastScale / CloudScaleFrameInto。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <map>
#include <set>
#include <vector>
#include <windows.h>   // RECT：画面在画布里所占的矩形

namespace screenshare {
struct AnnotationStroke;
class BgraDownscaler;   //2K、4K 画面缩小进画布的快速缩放（CloudScaleFrameInto，2026-10-15）
}

namespace screenrecord {

//合成的输出参数
const int kCloudComposeWidth = 1920;          //输出宽（像素）
const int kCloudComposeHeight = 1080;         //输出高（像素）
const int kCloudComposeFps = 15;              //输出帧率（与屏幕共享的采集帧率一致）
const int kCloudAudioRate = 48000;            //声音采样率
const int kCloudAudioFrameSamples = 960;      //每次写入的样点数（20 毫秒）
const int64_t kCloudAudioFrameMs = 20;        //每包声音的时长（毫秒），与发送端的 Opus 帧长一致
const int64_t kCloudReanchorMs = 1000;        //声音按时间戳排的位置与到达时刻偏离超过这么多就重新定锚（毫秒）
const int64_t kCloudScreenStaleMs = 10000;    //一块画面超过这么久没有新帧就不再显示（屏幕共享每 4 秒至少一个关键帧）
const int64_t kCloudCameraStaleMs = 2000;     //摄像头超过这么久没有新帧视为已关闭（毫秒）
const int64_t kCloudSpeakHoldMs = 400;        //说话的人在最后一次超过阈值之后还算说话中的时长（毫秒）
const int kCloudSpeakRms = 600;               //说话检测的 RMS 阈值（16 位 PCM），与共享中的说话检测一致
const int kCloudBoardAspectW = 16;            //共享白板世界的宽高比：宽（2026-10-07，与观看窗的共享白板一致）
const int kCloudBoardAspectH = 9;             //同上：高

//输出时间轴：去掉暂停的时段
class CloudComposeTimeline
{
public:
    CloudComposeTimeline();

    //开始暂停 / 继续（记录的相对毫秒）
    void OnPause(int64_t relMs);
    void OnResume(int64_t relMs);

    /**
     *  记录的相对毫秒换成输出时间轴上的毫秒：减去此前全部暂停的时长；暂停中的时刻按暂停开始处算。
     *  不会比之前给出的值更小（时间轴只往前走）。
     */
    int64_t OutMs(int64_t relMs);

    //是否暂停中
    bool Paused() const;

private:
    int64_t m_pausedTotal;   //已结束的暂停的总时长（毫秒）
    int64_t m_pauseStart;    //这次暂停开始的相对毫秒；没在暂停时为 -1
    int64_t m_lastOut;       //最近一次给出的输出毫秒
};

//声音按发送端时间戳排位
class CloudAudioPlacer
{
public:
    /**
     *  一包声音在输出时间轴上的起点（样点）。
     *  @param streamId   哪一路声音（座位 × 8 + 轨道）
     *  @param senderTsUs 发送端时间戳（微秒，帧头里的值）
     *  @param arrivalMs  这一包到达的输出毫秒
     *  @return 起点样点（48 kHz）；不小于 0
     */
    int64_t Place(int streamId, uint64_t senderTsUs, int64_t arrivalMs);

private:
    //一路声音的锚：发送端时间戳 anchorTs 对应输出毫秒 anchorMs
    struct Anchor
    {
        uint64_t m_anchorTsUs;   //定锚那一包的发送端时间戳
        int64_t  m_anchorMs;     //定锚那一包的输出毫秒

        Anchor();
    };

    std::map<int, Anchor> m_anchors;   //每路声音的锚
};

//各路声音叠加的缓冲
class CloudAudioMixBuffer
{
public:
    CloudAudioMixBuffer();

    /**
     *  把一段单声道 PCM 叠加到 startSample 起的位置。落在已经取走的部分之前的样点丢弃。
     *  @param startSample 起点样点
     *  @param pcm         单声道 16 位 PCM
     *  @param count       样点数
     */
    void Add(int64_t startSample, const int16_t* pcm, size_t count);

    /**
     *  取走从「上次取到的位置」起的 count 个样点（没有声音的地方为 0），截到 16 位。
     *  @param out [出参] count 个样点
     */
    void Take(size_t count, std::vector<int16_t>& out);

    //下一次 Take 从哪个样点开始
    int64_t Position() const;

private:
    int64_t              m_base;   //m_acc[0] 对应的样点
    std::vector<int32_t> m_acc;    //累加值
};

/**
 *  画中画取谁的摄像头：说话中、摄像头开着的人里座位号最小的；没人说话时沿用上一位（他的摄像头还开着时）；否则不显示。
 *  @param speakingSeats 说话中的座位
 *  @param cameraSeats   摄像头开着的座位
 *  @param prevSeat      上一帧用的座位；没有为 -1
 *  @return 座位号；不显示画中画时为 -1
 */
int ChooseCloudPipSeat(const std::set<int>& speakingSeats, const std::set<int>& cameraSeats, int prevSeat);

/**
 *  现在还在共享的画面：发起人（座位 0）在前，其余按座位升序；超过 kCloudScreenStaleMs 没有新帧的不算。
 *  @param lastFrameMs 每个画面座位最近一帧的输出毫秒
 *  @param nowMs       当前输出毫秒
 */
std::vector<int> CloudVisibleScreenSeats(const std::map<int, int64_t>& lastFrameMs, int64_t nowMs);

/**
 *  记下收到了某块白板的白板视野（2026-10-07，方案第 14.1 节 ④-2）：白板没有视频帧，建立者每 3 秒广播一次白板视野，
 *  合成时据此把白板与屏幕画面一样算作「还在」，超过 kCloudScreenStaleMs 没有视野时不再显示。
 *  @param boardSeat    白板的画面座位
 *  @param outMs        收到时的输出毫秒
 *  @param lastFrameMs  [入出参] 每个画面座位最近一帧（白板为最近一次视野）的输出毫秒，交给 CloudVisibleScreenSeats
 *  @param boards       [入出参] 是白板的画面座位，画的时候据此画白板而不是取视频帧
 */
void CloudNoteBoardView(int boardSeat, int64_t outMs, std::map<int, int64_t>& lastFrameMs, std::set<int>& boards);

/**
 *  白板图在一块画面里的尺寸（2026-10-07）：共享白板的世界固定为 16:9，在 tileW × tileH 里按 16:9 尽量放大。
 *  @param tileW、tileH 这块画面的宽高（像素）
 *  @param w、h         [出参] 白板图的宽高（像素）；画面尺寸不合法时为 0
 */
void CloudBoardImageSize(int tileW, int tileH, int& w, int& h);

//16 位 PCM 的均方根（说话检测用）
int CloudPcmRms(const int16_t* pcm, size_t count);

/**
 *  合成输出的视频码率（比特 / 秒）：按录制的「标准」画质与输出尺寸换算，1920×1080 时为 4 Mbps。
 *  2026-10-07 联调实测「高」画质（8 Mbps）每小时约 3 GB，云端空间按企业配额计，经用户确认改用「标准」。
 *  @return 码率，单位比特 / 秒
 */
uint32_t CloudComposeVideoBitrate();

/**
 *  按比例把 srcW × srcH 的画面放进 cell，四周留黑（2026-10-08，从合成器的画布里提出来以便测试，算法不变）。
 *  @param srcW、srcH 画面的宽高（像素）
 *  @param cell       这块画面的格子（画布坐标）
 *  @return 画面实际所占的矩形；尺寸不合法时为 cell 本身
 */
RECT CloudFitRect(int srcW, int srcH, const RECT& cell);

/**
 *  画面放进 dst 之后是不是原尺寸（2026-10-08）：是时可以直接逐行复制，不经 GDI 缩放。
 *  @return dst 的宽高与 srcW × srcH 相同时返回 true
 */
bool CloudFitIsOneToOne(int srcW, int srcH, const RECT& dst);

/**
 *  把 srcW × srcH 的 BGRA 逐行复制到画布的 (left, top) 处（2026-10-08）；超出画布的部分不复制。
 *  @param dst        画布像素（自上而下的 BGRA，行跨度 = dstW × 4）
 *  @param dstW、dstH 画布的宽高
 *  @param src        画面像素（自上而下的 BGRA，行跨度 = srcW × 4）
 *  @param srcW、srcH 画面的宽高
 *  @param left、top  画面左上角在画布里的位置
 */
void CloudCopyFrameRows(uint8_t* dst, int dstW, int dstH, const uint8_t* src, int srcW, int srcH, int left, int top);

/**
 *  画面放进 dst 时能否用快速缩放（2026-10-15，docs/远程桌面H264与共享画质提升方案.md A7）：两个方向都不放大、且不是原尺寸时
 *  返回 true，规则与观看窗的 ShareUsesFastDownscale 相同。2K、4K 的共享画面缩进 1080p 画布时，GDI 的 HALFTONE 缩放每帧要
 *  30～50 毫秒，4K 录像合成得比实时还慢（CCB2 实测）。快速缩放用 BgraDownscaler：比例不超过 2 时一次 SSE2 双线性，超过 2 时
 *  （多人共享时 4K 画面缩进小格子）先按 2×2 平均逐次减半，不会跳过像素。放大与原尺寸不走这条路径。
 *  @param srcW、srcH 画面的宽高（像素）
 *  @param dst        画面在画布里所占的矩形（CloudFitRect 的结果）
 */
bool CloudFitUsesFastScale(int srcW, int srcH, const RECT& dst);

/**
 *  用 BgraDownscaler 把画面缩小进画布的 dst 矩形（2026-10-15，A7）。dst 须完全落在画布内，否则什么都不写、返回 false，
 *  由调用方改用 GDI（GDI 自己裁剪）。调用方在此之前应先 GdiFlush，免得之前排队的 GDI 绘制覆盖这里写的像素。
 *  @param scaler       缩放器（调用方持有并复用，一条线程上使用）
 *  @param canvas       画布像素（自上而下的 BGRA，行跨度 = canvasW × 4）
 *  @param canvasW、canvasH 画布的宽高
 *  @param src          画面像素（自上而下的 BGRA，行跨度 = srcW × 4）
 *  @param srcW、srcH   画面的宽高
 *  @param dst          目标矩形（画布坐标）
 *  @return 写进了画布返回 true
 */
bool CloudScaleFrameInto(screenshare::BgraDownscaler& scaler, uint8_t* canvas, int canvasW, int canvasH, const uint8_t* src,
                         int srcW, int srcH, const RECT& dst);

//合成画布内容的签名（2026-10-08）：签名相同、又没有按时间变化的笔迹时，这一帧的画布与上一帧相同，可以不重画
struct CloudCanvasKey
{
    std::vector<int>      m_seats;          //显示的画面座位，按显示顺序
    std::vector<uint64_t> m_serials;        //与 m_seats 一一对应的帧序号；白板为 0（白板的内容随标注版本变化）
    uint64_t              m_annotVersion;   //标注模型的变化计数
    uint64_t              m_seatVersion;    //座位记录的变化计数（名字标签据此变化）

    CloudCanvasKey();
    bool operator==(const CloudCanvasKey& rhs) const;
    bool operator!=(const CloudCanvasKey& rhs) const;
};

/**
 *  笔迹里有没有按时间变化的（2026-10-08）：激光笔画完后按时间淡出，标注版本不变时画出来的样子也在变。
 *  为简单起见不分画面，房间里有任何一笔激光笔都算。
 */
bool CloudHasTimedStrokes(const std::vector<screenshare::AnnotationStroke>& strokes);

/**
 *  这一帧的画布能否沿用上一帧（2026-10-08）。
 *  @param haveCanvas 画布上是否已经画过一帧
 *  @param timed      笔迹里是否有按时间变化的（CloudHasTimedStrokes）
 *  @param last       上一次画画布时的签名
 *  @param now        这一帧的签名
 *  @return 画过、没有按时间变化的笔迹、签名相同时返回 true
 */
bool CloudCanvasReusable(bool haveCanvas, bool timed, const CloudCanvasKey& last, const CloudCanvasKey& now);

} // namespace screenrecord
