/**
 *  远程桌面新旧画面格式的协商规则，RdVideoNegotiation.h
 *
 *  2026-10-15 远程桌面改用 H.264（docs/远程桌面H264与共享画质提升方案.md 4.4）。不改服务端与信令，在引擎帧里双向握手：
 *    1. 控制端连上后发带能力位的关键帧请求（能解 H.264、能解的最大长边）；
 *    2. 新被控端在每个关键帧之前发能力帧；控制端收到它、而当前收到的仍是旧格式时，重发带能力位的请求（每秒最多一次），
 *       这样即使最初那条请求在被控端连上中转之前被丢掉，被控端的通告也会促使控制端重发；
 *    3. 被控端收到「能解 H.264」且本机能建起编码器时，下一帧起改发 H.264（第一帧为关键帧）；否则一直发旧格式；
 *    4. 被控端开始推流后最多等 kRdFormatWaitMs 毫秒的第一条请求，超时先按旧格式发，之后收到带能力位的请求再切换。
 *  本文件是这几条规则的纯逻辑：被控端的 RdFormatChooser、控制端的 RdCapsResendPacer，以及关键帧请求的限频 RdKeyRequestLimiter。
 *  2026-10-17 起另有被控端 H.264 推流循环的两条节奏规则：RdVideoRoundWaitMs（每轮补足的最短时长）与 RdVideoPacerHasNewFrame
 *  （交给发帧节奏的「有新画面」），抓屏引擎判断画面是否真的变了的 RdStoreFrameIfChanged，以及推流循环计时用的 RdStreamClockMs。
 *  不碰网络与编解码器，各类只在一条线程上使用（调用方负责），单元测试见 Source/Tests/RdVideoNegotiationTests.cpp（[rdvideo]）。
 *
 *  balloonwj@qq.com   2026-10-15
 */
#pragma once

#include <stdint.h>
#include <vector>
#include "RdVideoFrames.h"
#include "RdCaptureTypes.h"   // FrameView：RdStoreFrameIfChanged 的输入

namespace remotedesktop {

//被控端开始推流后等第一条关键帧请求的最长时间（毫秒），超时先按旧格式发（据推测足够，待实测）
const int64_t kRdFormatWaitMs = 2000;
//控制端重发带能力位的请求的最短间隔（毫秒）
const int64_t kRdCapsResendIntervalMs = 1000;
//关键帧请求的最短间隔（毫秒）：收帧队列溢出、解码出错时请求关键帧都经它限频
const int64_t kRdKeyRequestMinIntervalMs = 1000;
//H.264 推流循环：这一轮手里有画面、但没有抓到新画面时，一轮至少持续的时间（毫秒）。抓取后端不阻塞时（GDI、抓取出错）
//靠它让出 CPU；与每轮等新画面的时长（33 毫秒）相同，指针仍可每秒更新约 30 次（2026-10-17）
const int64_t kRdVideoIdleRoundMs = 33;
//H.264 推流循环：这一轮手里有画面、没有新画面，但指针动了时，一轮至少持续的时间（毫秒）。指针在动时每秒约更新 60 次，
//观看窗里画出的远端指针更跟手；指针不动时仍补足 kRdVideoIdleRoundMs，静止时的开销不变（2026-10-17）
const int64_t kRdVideoCursorRoundMs = 16;
//H.264 推流循环：一帧画面都没有（抓取后端都不可用、或从开始就一直抓取失败）时，一轮至少持续的时间（毫秒），
//与旧格式的抓屏周期相同（2026-10-17）
const int64_t kRdVideoNoFrameRoundMs = 200;

//被控端发的画面格式
enum rd_video_format
{
    rd_format_undecided = 0,   //还没收到控制端的请求，也没等满 kRdFormatWaitMs：这时不发画面
    rd_format_legacy    = 1,   //旧格式（16 位 DIB + DIFF，RdScreenCapturer 的 BITMAPINFO / 首屏 / 增量帧）
    rd_format_h264      = 2,   //H.264（4 号视频帧 + 5 号指针帧）
};

//被控端选格式；只在推流线程上使用（关键帧请求由接收线程经原子量转交）
class RdFormatChooser
{
public:
    /**
     *  @param canEncodeH264 本机能否建起 H.264 编码器（推流开始时试建一次）
     */
    explicit RdFormatChooser(bool canEncodeH264);

    //推流开始：回到「未定」，从这一刻起算等待时间
    void Start(int64_t nowMs);

    /**
     *  收到一条关键帧请求。
     *  @param info  解析出的请求（RdVideoFrames::ParseKeyframeRequest）
     *  @param nowMs 当前时刻（毫秒）
     *  @return 格式因此变了（例如由旧格式切到 H.264）返回 true：调用方据此让下一帧编成关键帧
     */
    bool OnKeyframeRequest(const RdKeyframeRequestInfo& info, int64_t nowMs);

    /**
     *  当前格式：未定且等满 kRdFormatWaitMs 时定为旧格式。
     *  @param nowMs 当前时刻（毫秒）
     *  @return rd_video_format 的取值
     */
    int Current(int64_t nowMs);

    //本机编码器在会话中途彻底失败（硬件、软件都建不起来）：回到旧格式，本次会话不再切换
    void OnEncoderFailed();

    //控制端能解的最大长边（最近一次带能力位的请求里的；没有时为 kRdDefaultControllerMaxLongSide）
    int ControllerMaxLongSide() const;

private:
    bool    m_canEncode;      //本机能否编 H.264（编码器中途失败后置为 false）
    int     m_format;         //当前格式，取值见 rd_video_format
    int64_t m_startMs;        //推流开始的时刻（毫秒）
    int     m_controllerMax;  //控制端能解的最大长边
};

//控制端：收到被控端的能力帧时决定要不要重发带能力位的请求；只在观看窗的 UI 线程上使用，不加锁
class RdCapsResendPacer
{
public:
    /**
     *  @param canDecodeH264 本机能否建起 H.264 解码器（会话开始时试建一次）
     */
    explicit RdCapsResendPacer(bool canDecodeH264);

    /**
     *  收到被控端的能力帧。
     *  @param caps           能力位
     *  @param receivingH264  当前是否已在收 H.264
     *  @param nowMs          当前时刻（毫秒）
     *  @return 应当重发带能力位的关键帧请求返回 true：仍在收旧格式、对方能编、本机能解，且距上次重发不少于 kRdCapsResendIntervalMs
     */
    bool OnCapsFrame(uint8_t caps, bool receivingH264, int64_t nowMs);

    /**
     *  别的途径发出了关键帧请求（连上时、H.264 需要关键帧时）：一秒之内的能力帧不再触发重发。
     *  连上时刚发过请求，被控端随即发「能力帧 + H.264 关键帧」，能力帧到达时控制端还没解出 H.264 画面，若不记下这一次，
     *  每次会话开始都会多要一个关键帧。
     *  @param nowMs 发出的时刻（毫秒）
     */
    void OnRequestSent(int64_t nowMs);

private:
    bool    m_canDecode;    //本机能否解 H.264
    bool    m_sent;         //是否发过请求（重发或经 OnRequestSent 登记的）
    int64_t m_lastSentMs;   //上次发请求的时刻（毫秒）
};

//关键帧请求的限频：两次请求之间不少于 kRdKeyRequestMinIntervalMs
class RdKeyRequestLimiter
{
public:
    RdKeyRequestLimiter();

    /**
     *  @param nowMs 当前时刻（毫秒）
     *  @return 这一次可以发请求返回 true（并记下时刻）；距上次不足间隔返回 false
     */
    bool Allow(int64_t nowMs);

private:
    bool    m_sent;         //是否发过
    int64_t m_lastSentMs;   //上次发的时刻（毫秒）
};

/**
 *  被控端 H.264 推流循环一轮结束后还应等待的时间（2026-10-17）。
 *  DXGI 在屏幕不变时阻塞到等待时长才返回，GDI 与抓取出错时立即返回；循环本身原先不等待，抓不到画面时（隐藏桌面、
 *  锁屏与 UAC 提示期间）推流线程空转、占满一个核。
 *  @param elapsedMs 这一轮已经用掉的时间（毫秒）；小于 0（时钟异常）时按 0 处理
 *  @param hasFrame  手里是否有画面（这一轮或之前抓到过）
 *  @param newFrame  这一轮是否抓到了新画面（hasFrame 为 false 时忽略）
 *  @param cursorMoved 这一轮指针的位置或类型是否变了（2026-10-17 起；hasFrame 为 false 时忽略）。默认 false，即原来的规则
 *  @return 应等待的毫秒数：抓到新画面时为 0，画面变化时的帧率不受影响；有画面而没有新画面时，指针动了补足
 *          kRdVideoCursorRoundMs，没动补足 kRdVideoIdleRoundMs；没有画面时补足 kRdVideoNoFrameRoundMs；已经够长时为 0
 */
int64_t RdVideoRoundWaitMs(int64_t elapsedMs, bool hasFrame, bool newFrame, bool cursorMoved = false);

/**
 *  交给发帧节奏（screenshare::ShareFramePacer::decide）的「这一轮有新画面」（2026-10-17）。
 *  发帧节奏在从没编过码时只看有没有新画面、不理会关键帧请求；会话中途由旧格式切到 H.264 时屏幕若恰好静止，
 *  第一帧要等到画面变化才编得出来。因此需要关键帧、而手里有画面时，也当作有新画面。
 *  @param hasFrame     手里是否有画面
 *  @param captureNew   这一轮是否抓到了新画面
 *  @param needKeyframe 这一轮是否需要关键帧（控制端请求、换了格式）
 *  @return 应当告诉发帧节奏「有新画面」时返回 true
 */
bool RdVideoPacerHasNewFrame(bool hasFrame, bool captureNew, bool needKeyframe);

/**
 *  把抓到的画面存成紧密排列的副本，同时判断它与原来存下的画面是否相同（2026-10-17）。
 *  DXGI 在可见画面没有变化时也会报告新帧（联调实测：整屏被静止窗口盖住时仍约每秒 20 帧），把这样的帧当作新画面编码只是
 *  浪费带宽；调用方据返回值把它当作「没有新画面」。逐行比较、遇到第一处不同之后逐行复制，整帧只扫一遍。
 *  @param src  抓到的画面（BGRA32，行跨度可能大于宽 × 4）；m_data 不能为 NULL，宽高须大于 0
 *  @param dst  [入出参] 原来存下的画面（BGRA32，行跨度 = 宽 × 4）；返回时为 src 的副本
 *  @param dstW [入出参] dst 的宽（像素）
 *  @param dstH [入出参] dst 的高（像素）
 *  @return 与原来的画面不同、尺寸变了或原来没有画面时返回 true；完全相同时返回 false（dst 不变）
 */
bool RdStoreFrameIfChanged(const FrameView& src, std::vector<BYTE>& dst, int& dstW, int& dstH);

/**
 *  被控端推流循环的单调时钟（毫秒，2026-10-17）。发帧节奏、每轮时长、视频帧的发送时刻与回执延迟都用它计时。
 *  @return 自某个固定起点以来的毫秒数，单调不减
 */
int64_t RdStreamClockMs();

} // namespace remotedesktop
