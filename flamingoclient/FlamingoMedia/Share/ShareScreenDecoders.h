/**
 *  多路屏幕画面的解码（第 22 期多人共享），ShareScreenDecoders.h
 *
 *  多人同时共享时，一条中转连接上会收到几路屏幕画面（屏幕画面轨，按发送者座位区分）。本类按发送者座位各用一个 H.264 解码器
 *  （call::MfH264Decoder），各存「最新一帧」（BGRA，按载荷里的真实宽高裁掉解码补齐的部分），各记共享内容状态（最小化、暂停等）。
 *  观看者的收流管线与主持人的推流管线（主持人看别人的屏幕）共用它。
 *
 *  线程：OnScreenFrame、SetSourceState 在接收线程上调用；GetLatestFrame、Seats、SourceState 在 UI 线程上调用；内部以互斥量保护。
 *  解出新帧时在接收线程上回调 ShareScreenDecodersListener::OnScreenDecoded，实现方只做 PostMessage。
 *  解码器要求调用线程已初始化 COM 与 Media Foundation（接收线程由收流管线初始化）。
 *
 *  典型用法：
 *      m_screens.SetListener(this);
 *      ...（接收线程）if (frame.m_track == screenshare_track_screen) m_screens.OnScreenFrame(frame);
 *      ...（UI 线程）m_screens.GetLatestFrame(seat, bgra, w, h, serial);
 *
 *  单元测试见 Source/Tests/ShareMultiTests.cpp（SSMC4）。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <stdint.h>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include <atomic>

#include "ShareFraming.h"

namespace screenshare {

//Win7 自带 H.264 解码器能解的最大画面（宽、高，单位像素；据推测，来自微软对系统编解码器的说明，没有 Win7 实机核对）
const int kShareLegacyDecodeMaxW = 1920;
const int kShareLegacyDecodeMaxH = 1088;
//连续收到这么多个关键帧仍一帧也没解出时，才判为超出本机解码能力（第一个关键帧可能只是刚开始解码）
const int kShareUndecodableKeyframes = 2;

/**
 *  「这一路画面解不出来，是因为分辨率超出本机系统的解码能力」的判定（2026-10-15，docs/远程桌面H264与共享画质提升方案.md
 *  4.2「Win7 观看者」，Q6 取乙）：画面宽或高超过 kShareLegacyDecodeMaxW × kShareLegacyDecodeMaxH，且自上次解出画面以来已经收到
 *  kShareUndecodableKeyframes 个关键帧。不超过这个尺寸的画面解不出来是别的原因，不归到分辨率上。
 *  @param frameW / frameH          画面的编码宽高（载荷里带的），单位像素
 *  @param keyframesWithoutOutput   自上次解出画面以来收到的关键帧个数
 *  @return 判为超出本机解码能力返回 true
 */
bool ShareLooksUndecodable(int frameW, int frameH, int keyframesWithoutOutput);

//解出了某一路的新一帧（接收线程上调用）
class ShareScreenDecodersListener
{
public:
    virtual ~ShareScreenDecodersListener()
    {
    }

    /**
     *  某个座位的画面解出了新的一帧，可在 UI 线程上调用 GetLatestFrame 取走。
     *  @param seat 发送者座位（画面座位）
     */
    virtual void OnScreenDecoded(int seat) = 0;

    /**
     *  某个座位的画面「超出本机解码能力」的判定变了（2026-10-15，接收线程上调用；默认什么都不做）。
     *  @param seat        发送者座位
     *  @param undecodable true：判为超出本机解码能力（见 ShareLooksUndecodable）；false：又解出了画面，判定清除
     */
    virtual void OnScreenUndecodable(int seat, bool undecodable)
    {
        (void)seat;
        (void)undecodable;
    }
};

//多路屏幕画面的解码
class ShareScreenDecoders
{
public:
    ShareScreenDecoders();
    ~ShareScreenDecoders();

    //解出新帧时的回调（不拥有）；可为 NULL
    void SetListener(ShareScreenDecodersListener* listener);

    /**
     *  收到一帧（接收线程）：只处理屏幕画面轨，按发送者座位交给那一路的解码器。
     *  @param frame 重组好的一帧
     */
    void OnScreenFrame(const ShareMediaFrame& frame);

    //某个座位的共享内容状态（取值见 screenshare_source_state）变了（接收线程）
    void SetSourceState(int seat, int state);

    //某个座位的共享内容状态；没收到过时为正在共享
    int SourceState(int seat) const;

    /**
     *  取某个座位的最新一帧（UI 线程）。
     *  @param bgra   [出参] 自上而下的 BGRA，行跨度 = 宽 × 4
     *  @param serial [出参] 帧序号，每解出一帧加一（调用方据此判断是否有新帧）
     *  @return 这个座位解出过画面返回 true
     */
    bool GetLatestFrame(int seat, std::vector<uint8_t>& bgra, int& width, int& height, uint64_t& serial) const;

    /**
     *  只取某个座位最新一帧的帧序号，不复制画面（2026-10-08，云端录像合成据此判断画面有没有变）。
     *  @param serial [出参] 帧序号
     *  @return 这个座位解出过画面返回 true
     */
    bool LatestSerial(int seat, uint64_t& serial) const;

    //解出过画面的座位，升序
    std::vector<int> Seats() const;

    //去掉某一路（这个人停止共享或离开房间）
    void Remove(int seat);

    //全部去掉
    void Clear();

    //累计解出的帧数（各路合计，日志与测试用）
    uint64_t DecodedCount() const;

    //某个座位的画面是否判为超出本机解码能力（见 ShareLooksUndecodable）；任意线程
    bool IsUndecodable(int seat) const;

    //是否有任何一路判为超出本机解码能力；任意线程
    bool AnyUndecodable() const;

private:
    class Stream;

    ShareScreenDecodersListener*            m_listener;   //回调（不拥有）
    mutable std::mutex                      m_mutex;      //保护 m_streams 与各路的最新一帧
    std::map<int, std::shared_ptr<Stream> > m_streams;    //发送者座位 → 这一路的解码状态
    std::atomic<uint64_t>                   m_decoded;    //累计解出的帧数
};

} // namespace screenshare
