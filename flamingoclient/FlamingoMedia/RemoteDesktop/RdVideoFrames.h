/**
 *  远程桌面 H.264 引擎帧的编解码，RdVideoFrames.h
 *
 *  2026-10-15 远程桌面改用屏幕共享的 H.264 编码管线（docs/远程桌面H264与共享画质提升方案.md 4.3、4.4）。沿用「首字节为类型」
 *  的引擎帧格式，新增的帧与负载（多字节整数一律小端，与现有引擎帧一致）：
 *    被控端 → 控制端
 *      4 rd_frame_video ：源宽 2B、源高 2B、编码宽 2B、编码高 2B、标志 1B（位 0：关键帧）、发送时刻 4B、H.264 访问单元
 *      5 rd_frame_cursor：指针横坐标 4B、纵坐标 4B（被控端主屏坐标，可为负）、指针类型 1B
 *      6 rd_frame_caps  ：能力位 1B（位 0：能编 H.264）
 *    控制端 → 被控端
 *      3 rd_frame_request_keyframe：能力位 1B（可选；位 0：能解 H.264）、能解的最大长边 2B（可选）
 *      4 rd_ctl_video_ack         ：最近收到的 4 号帧的发送时刻 4B（原样回送）
 *  解析一律校验长度与尺寸：中转在对端报到前按分片丢数据，控制端在一个多分片引擎帧中途连上时，重组器会交出缺头的帧，首字节可能是
 *  任意值（RdRelayFraming.cpp），长度不足、尺寸为 0 或超出上限的一律丢弃。另有回执延迟与编码长边两个纯函数。
 *  纯逻辑，不碰网络与编解码器，单元测试见 Source/Tests/RdVideoFramesTests.cpp（[rdvideo]）。
 *
 *  balloonwj@qq.com   2026-10-15
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace remotedesktop {

//能力位：位 0 为能编（被控端）/ 能解（控制端）H.264
const uint8_t kRdCapH264 = 0x01;
//控制端没有报能解的最大长边时按这个值（据推测 Win7 自带的 H.264 解码器最大 1920×1088）
const int kRdDefaultControllerMaxLongSide = 1920;
//视频帧里宽高的上限（防御：超出视为损坏的帧），单位像素
const int kRdVideoMaxDimension = 8192;
//各种帧的长度：视频帧头（类型 1 + 四个尺寸各 2 + 标志 1 + 发送时刻 4）、指针帧、能力帧、带能力位的关键帧请求、回执
const size_t kRdVideoHeaderLen = 14;
const size_t kRdCursorFrameLen = 10;
const size_t kRdCapsFrameLen = 2;
const size_t kRdKeyRequestFullLen = 4;
const size_t kRdVideoAckLen = 5;
//视频帧标志位：位 0 为关键帧
const uint8_t kRdVideoFlagKeyframe = 0x01;

//4 号视频帧的帧头
struct RdVideoFrameHeader
{
    int      m_srcW;       //源宽（被控端主屏的物理像素），控制端用它换算鼠标坐标与指针位置
    int      m_srcH;       //源高
    int      m_encW;       //编码宽（裁掉解码补齐的部分、决定绘制的宽高比）
    int      m_encH;       //编码高
    bool     m_keyframe;   //是否关键帧
    uint32_t m_sendMs;     //发送时刻（被控端单调时钟的低 32 位，毫秒；回绕不影响差值计算）

    RdVideoFrameHeader();
};

//解析出的关键帧请求
struct RdKeyframeRequestInfo
{
    bool m_hasCaps;         //请求里是否带了能力位（旧控制端只发 1 字节，为 false）
    bool m_canDecodeH264;   //控制端能解 H.264
    int  m_maxLongSide;     //控制端能解的最大长边；请求里没有时为 kRdDefaultControllerMaxLongSide

    RdKeyframeRequestInfo();
};

//引擎帧的编解码；全部为静态方法
class RdVideoFrames
{
public:
    /**
     *  组 4 号视频帧。
     *  @param h       帧头
     *  @param h264    H.264 访问单元（含起始码），不拥有
     *  @param len     字节数
     *  @param out     [出参] 引擎帧
     */
    static void BuildVideo(const RdVideoFrameHeader& h, const uint8_t* h264, size_t len, std::vector<unsigned char>& out);

    /**
     *  解析 4 号视频帧。
     *  @param data / len 引擎帧
     *  @param h          [出参] 帧头
     *  @param h264       [出参] 指向 data 里的 H.264 访问单元
     *  @param h264Len    [出参] 访问单元的字节数
     *  @return 类型是 4、长度足够、四个尺寸都在 1~kRdVideoMaxDimension 之内且负载非空时返回 true
     */
    static bool ParseVideo(const unsigned char* data, size_t len, RdVideoFrameHeader& h, const unsigned char*& h264,
                           size_t& h264Len);

    //组 5 号指针帧（x、y 为被控端主屏坐标，type 为指针类型索引）
    static void BuildCursor(int32_t x, int32_t y, uint8_t type, std::vector<unsigned char>& out);

    //解析 5 号指针帧；类型不对或长度不足返回 false
    static bool ParseCursor(const unsigned char* data, size_t len, int32_t& x, int32_t& y, uint8_t& type);

    //组 6 号能力帧
    static void BuildCaps(uint8_t caps, std::vector<unsigned char>& out);

    //解析 6 号能力帧；类型不对或长度不足返回 false
    static bool ParseCaps(const unsigned char* data, size_t len, uint8_t& caps);

    /**
     *  组带能力位的 3 号关键帧请求（新控制端用）。
     *  @param canDecodeH264 本机能否建起 H.264 解码器
     *  @param maxLongSide   能解的最大长边；不大于 0 时写 0（被控端按 kRdDefaultControllerMaxLongSide）
     */
    static void BuildKeyframeRequest(bool canDecodeH264, int maxLongSide, std::vector<unsigned char>& out);

    /**
     *  解析 3 号关键帧请求（调用方已确认首字节是 3）：只有 1 字节时视为旧控制端、不能解 H.264；
     *  有能力位而没有最大长边（或最大长边为 0）时按 kRdDefaultControllerMaxLongSide。
     */
    static void ParseKeyframeRequest(const unsigned char* data, size_t len, RdKeyframeRequestInfo& info);

    //组 4 号回执（控制端 → 被控端）
    static void BuildVideoAck(uint32_t sendMs, std::vector<unsigned char>& out);

    //解析 4 号回执；类型不对或长度不足返回 false
    static bool ParseVideoAck(const unsigned char* data, size_t len, uint32_t& sendMs);
};

/**
 *  由回执算往返加排队的延迟（被控端用，方案 4.5「拥塞信号」）。三个时刻都是被控端单调时钟的低 32 位（毫秒），差值按 32 位
 *  无符号运算，回绕不影响结果。
 *  @param nowMs        当前时刻
 *  @param echoedMs     回执里回送的发送时刻
 *  @param firstSentMs  本会话发出第一个视频帧的时刻：早于它的回执是上一会话的残留，忽略
 *  @param delayMs      [出参] 延迟（毫秒）
 *  @return 回执有效返回 true；早于本会话第一帧或晚于当前时刻时返回 false
 */
bool RdAckDelayMs(uint32_t nowMs, uint32_t echoedMs, uint32_t firstSentMs, int64_t& delayMs);

/**
 *  控制端定回执里回送的时刻：最近收到的 4 号帧的发送时刻，加上该帧在控制端停留的时长（收到到回执的间隔）。
 *  2026-10-15 实施时修正方案 4.5 的「原样回送」：画面静止时被控端每 4 秒才发一帧兜底关键帧，而控制端每 500 毫秒回执一次，
 *  原样回送会让被控端把停留时长也算成延迟（最长约 4 秒，超过 1000 毫秒的拥塞门限）；加上停留时长后，被控端用
 *  RdAckDelayMs 算出的只有往返与排队的时间。帧格式不变（仍是 4 字节时刻）。
 *  @param sendMs       最近收到的 4 号帧里的发送时刻（被控端时钟）
 *  @param receivedAtMs 收到这一帧的时刻（控制端单调时钟，毫秒）
 *  @param nowMs        回执的时刻（控制端单调时钟，毫秒）；早于 receivedAtMs 时按停留 0
 *  @return 回送的时刻（被控端时钟的低 32 位，加法按 32 位无符号回绕）
 */
uint32_t RdAckEchoMs(uint32_t sendMs, int64_t receivedAtMs, int64_t nowMs);

/**
 *  被控端定编码长边（方案 4.4 第 3 条）：取远程桌面参数的上限与控制端能解的最大长边中较小的一个。
 *  @param paramsMaxLongSide     远程桌面参数的长边上限（kRdVideoMaxLongSide）
 *  @param controllerMaxLongSide 控制端报的能解的最大长边；不大于 0 时按 kRdDefaultControllerMaxLongSide
 */
int RdChooseEncodeLongSide(int paramsMaxLongSide, int controllerMaxLongSide);

} // namespace remotedesktop
