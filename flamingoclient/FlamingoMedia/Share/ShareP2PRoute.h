/**
 *  单聊屏幕共享走 P2P 时发起人一侧要接管的服务端职责（纯逻辑，docs/P2P通信方案.md 第 6 期），ShareP2PRoute.h
 *
 *  经房间中转时，imgserver 替发起人做了两件事：把发送者座位号改写成发送连接登记的座位（客户端填错或冒充都无效），
 *  以及给积压过多的「慢观看者」丢非关键帧、直到下一个关键帧，并向视频发送者请求关键帧（docs/协议设计-v2.md 17.8）。
 *  单聊共享里观看者改经 P2P 帧通道与发起人直连之后，这两件事由发起人一侧自己做：
 *    - ShareRewriteSenderSeat：把观看者经 P2P 发来的包体里的发送者座位号改写为服务端分配给他的座位；
 *    - ShareP2PVideoGate：每帧视频发出之前看一眼链路的发送积压，超过 kShareP2PViewerBacklogBytes 就进入「等关键帧」，
 *      其后的非关键帧都不发，直到下一个关键帧；等关键帧期间每 kShareP2PKeyRequestIntervalMs 至多请求一次关键帧
 *      （请求本端编码器，并计入码率自适应）。关键帧一律照发；音频、标注、控制帧不经过它，一律照发。
 *
 *  纯逻辑，不碰网络，单测见 Source/Tests/ShareP2PRouteTests.cpp。
 *
 *  典型用法（ShareHostStreamer，工作线程上）：
 *      bool requestKey = false;
 *      if (!m_p2pGate.OnVideoFrame(keyframe, viewer->QueuedBytes(), nowMs, requestKey))
 *          丢弃这一帧;
 *      if (requestKey) 请求关键帧;
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>

namespace screenshare {

//发给 P2P 观看者的数据在链路上积压超过这么多字节（字节）时，这路视频进入「等关键帧」：与 imgserver 给每个观看者的
//积压上限 screenshareviewerbufferbytes 的默认值相同（2026-10-15 两处一起由 1 MiB 调到 4 MiB，
//docs/远程桌面H264与共享画质提升方案.md 4.8）
const uint64_t kShareP2PViewerBacklogBytes = 4 * 1024 * 1024;

//等关键帧期间请求关键帧的最短间隔（毫秒）：与 imgserver 的限频相同
const int64_t kShareP2PKeyRequestIntervalMs = 2000;

/**
 *  把房间包体里的发送者座位号改写为指定座位（包体以 11 字节帧头开头，第 2 个字节是发送者座位号）。
 *  @param body 房间包体（就地修改）
 *  @param seat 座位号（0~255）
 *  @return 包体不足一个帧头时不改并返回 false
 */
bool ShareRewriteSenderSeat(std::string& body, int seat);

/**
 *  发起人一侧能否经 P2P 给观看者发数据（docs/屏幕共享与录制方案.md 第 12.4.2、12.6.2 节）。P2P 帧通道打开后发起人只经 P2P
 *  发、不再经房间中转发，所以只有房间里除发起人外恰好一名观看者、且没有服务器在录这个房间时才能用 P2P；凭分享链接加入的
 *  第三人（第 25 期）或云端录制（第 26 期）一出现，就要关掉 P2P、回到房间中转，否则他们收不到任何帧。
 *  @param memberCount     房间快照里的成员数（含发起人）
 *  @param cloudRecording  服务器是否在录这个房间（第 26 期云端录制）
 *  @return 可以用 P2P 返回 true
 */
bool ShareHostP2PAllowed(size_t memberCount, bool cloudRecording);

/**
 *  发给 P2P 观看者的视频的丢帧规则（代替 imgserver 的「慢观看者丢帧」）。只在发视频的那个线程上使用，不加锁。
 */
class ShareP2PVideoGate
{
public:
    ShareP2PVideoGate();

    //回到初始状态（换了观看者、或通道重新打开时）
    void Reset();

    /**
     *  一帧视频发出之前调用。
     *  @param keyframe    是否关键帧
     *  @param queuedBytes 链路当前的发送积压（字节）
     *  @param nowMs       当前时间（单调时钟毫秒）
     *  @param requestKey  [出参] 是否应当现在请求一个关键帧
     *  @return true 发出；false 丢弃（在等关键帧）
     */
    bool OnVideoFrame(bool keyframe, uint64_t queuedBytes, int64_t nowMs, bool& requestKey);

    //是否在等关键帧
    bool WaitingKeyframe() const { return m_waitingKey; }

    //累计丢弃的帧数
    uint64_t DroppedFrames() const { return m_dropped; }

private:
    bool     m_waitingKey;      //是否在等关键帧
    bool     m_requested;       //等关键帧期间是否已经请求过
    int64_t  m_lastRequestMs;   //上次请求的时刻；m_requested 为 false 时无意义
    uint64_t m_dropped;         //累计丢弃的帧数
};

} // namespace screenshare
