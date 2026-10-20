/**
 *  屏幕共享房间中转的慢观看者丢帧策略，RoomForwardPolicy.h
 *
 *  房间里任一成员发来的帧要转发给其余所有成员。某个观看者网络慢、发给他的数据在服务端积压时，继续往他的
 *  输出缓冲里塞视频帧只会让积压越来越多、画面越来越滞后。规则（docs/屏幕共享与录制方案.md 5.4，2026-10-02
 *  与用户确认的用例 RRP1~6）：
 *    - 只丢视频帧（屏幕画面、摄像头画面）的非关键帧；音频、标注、控制帧一律不丢；关键帧也一律照发；
 *    - 发给某接收者的数据积压超过上限时，这路视频对他进入「等关键帧」状态：其后的非关键帧都丢弃，
 *      直到这路视频的下一个关键帧到达，即使积压已经消退也不提前恢复（缺了中间帧的画面解不出来）；
 *    - 进入「等关键帧」时向这路视频的发送者请求关键帧，同一路视频 2 秒内至多请求一次；
 *    - 有新成员加入时向所有已知的视频发送者各请求一次关键帧，不受限频约束；
 *    - 状态按「接收者 × 发送者 × 视频轨」分别记，屏幕画面与各路摄像头画面互不影响。
 *  一个大视频帧会被拆成多个分片，每个分片各占一帧 1860；是否丢弃只在首分片上决定，其余分片跟随首分片。
 *
 *  每个房间一个实例。本类不加锁，调用方负责串行化（同一房间的转发决定须在同一把锁下做）。本类为纯逻辑，
 *  积压字节数与当前时间都由调用方传入，便于单元测试，见 tests/test_roomforwardpolicy.cpp（Suite RRP）。
 *
 *  典型用法（转发一帧时，对每个接收者）：
 *      bool requestKeyFrame = false;
 *      if (policy.decide(receiverSeat, header, pendingBytes, nowMs, requestKeyFrame) == room_forward_send)
 *          发给该接收者;
 *      if (requestKeyFrame)
 *          向 header.m_senderSeat 发 RoomRelayFrame::buildKeyFrameRequest(header.m_track);
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <map>
#include <set>
#include <utility>
#include <vector>
#include "RoomRelayFrame.h"

//一路视频流：发送者座位号 + 视频轨
struct RoomVideoStream
{
    int m_senderSeat;   //发送者座位号（0~254）
    int m_track;        //视频轨：screenshare_track_screen 或 screenshare_track_camera

    RoomVideoStream(int senderSeat, int track);

    bool operator<(const RoomVideoStream& rhs) const;
    bool operator==(const RoomVideoStream& rhs) const;
};

//对「一帧发给某个接收者」的决定
enum room_forward_decision
{
    room_forward_send = 0,   //照常转发
    room_forward_drop = 1,   //丢弃：该接收者正处于这路视频的「等关键帧」状态，且本帧不是关键帧
};

//一个房间的慢观看者丢帧策略；不加锁，调用方负责串行化
class RoomForwardPolicy
{
public:
    //每个观看者输出缓冲上限的默认值，单位字节（imgserver 配置项 screenshareviewerbufferbytes）。2026-10-02 与用户确认为 1 MiB；
    //2026-10-15 调到 4 MiB（docs/远程桌面H264与共享画质提升方案.md 4.8，用户定的 Q3）：编码长边提到 2560 / 3840 后单个关键帧更大，
    //余量要不低于「2～3 倍最大关键帧 + 约 1 秒码流」
    static const size_t kDefaultViewerBufferCapBytes = 4 * 1024 * 1024;
    //同一路视频两次关键帧请求之间的最小间隔，单位毫秒
    static const int64_t kKeyFrameRequestIntervalMs = 2000;

    /**
     *  @param viewerBufferCapBytes 每个接收者的积压上限，单位字节；为 0 时按默认值处理
     */
    explicit RoomForwardPolicy(size_t viewerBufferCapBytes);

    /**
     *  决定一帧发给某个接收者时是转发还是丢弃。
     *  @param receiverSeat          接收者的座位号
     *  @param header                本帧的帧头（发送者座位号须已按发送连接改写）
     *  @param receiverPendingBytes  发给该接收者、还积压在服务端没有发出的字节数
     *  @param nowMs                 当前时间，单位毫秒（单调递增即可）
     *  @param outRequestKeyFrame    [出参] 本次决定让这路视频对该接收者进入「等关键帧」状态、且没有被限频挡住时为 true，
     *                               调用方须向 header.m_senderSeat 发一帧关键帧请求（视频轨为 header.m_track）；否则为 false
     *  @return 取值见 enum room_forward_decision
     */
    int decide(int receiverSeat, const RoomRelayFrameHeader& header, size_t receiverPendingBytes, int64_t nowMs,
               bool& outRequestKeyFrame);

    /**
     *  记下收到了一帧（每帧调用一次，与有没有接收者无关）。视频帧据此登记为已知视频流：发起人独自在房间里时
     *  发的画面也要算，否则第一个观看者加入时不会向他请求关键帧（2026-10-02 联调用例 SSRC1 查出）。
     *  @param header 本帧的帧头（发送者座位号须已按发送连接改写）
     */
    void noteFrame(const RoomRelayFrameHeader& header);

    /**
     *  有新成员加入房间：给出房间里已知的全部视频流（转发过至少一帧且发送者还在），调用方向每路的发送者
     *  各请求一次关键帧。不受限频约束，但会刷新这些视频流的限频计时。
     *  @param nowMs       当前时间，单位毫秒
     *  @param outRequests [出参] 要请求关键帧的视频流，按发送者座位号、视频轨升序
     */
    void onMemberJoined(int64_t nowMs, std::vector<RoomVideoStream>& outRequests);

    /**
     *  成员离开房间：清掉以他为接收者或发送者的全部状态，以及他的订阅与别人订阅里的他。
     *  @param seat 离开者的座位号
     */
    void onMemberLeft(int seat);

    /**
     *  接收者发来订阅帧（群通话第 5 期，docs/协议设计-v2.md 21.8）：此后只把这些座位的摄像头画面转发给他，声音等其余
     *  轨道不受影响。没发过订阅帧的接收者收全部画面。新订阅的座位（原来没在收的）对他进入「等关键帧」状态，这路画面
     *  已经在发时向发送者请求一次关键帧，不受 2 秒限频约束（会刷新限频计时）。
     *  @param receiverSeat 接收者的座位号
     *  @param seats        要看的摄像头座位号；为空表示一路画面都不要
     *  @param nowMs        当前时间，单位毫秒
     *  @param outRequests  [出参] 要请求关键帧的视频流（发送者座位 + 摄像头轨）
     */
    void setSubscription(int receiverSeat, const std::vector<int>& seats, int64_t nowMs,
                         std::vector<RoomVideoStream>& outRequests);

    /**
     *  某个接收者是否要收某个座位的摄像头画面：没发过订阅帧时一律要，发过时看订阅里有没有这个座位。
     */
    bool isSubscribed(int receiverSeat, int senderSeat) const;

private:
    //某个接收者对某路视频的状态
    struct ReceiverState
    {
        bool m_waitingKeyFrame;   //是否处于「等关键帧」状态
        bool m_droppingFrame;     //当前这一帧（首分片已决定丢弃）的后续分片是否也要丢

        ReceiverState();
    };

    typedef std::pair<int, RoomVideoStream> ReceiverStreamKey;   //(接收者座位号, 视频流)

    size_t                                     m_bufferCapBytes;   //每个接收者的积压上限，单位字节
    std::map<ReceiverStreamKey, ReceiverState> m_receiverStates;   //接收者 × 视频流的状态
    std::set<RoomVideoStream>                  m_knownStreams;     //转发过至少一帧的视频流（新成员加入时据此请求关键帧）
    std::map<RoomVideoStream, int64_t>         m_lastRequestMs;    //每路视频最近一次请求关键帧的时刻，单位毫秒
    std::map<int, std::set<int> >              m_subscriptions;    //接收者座位 → 他订阅的摄像头座位（群通话第 5 期）；
                                                                   //没有条目的接收者收全部画面，成员离开时清除
};
