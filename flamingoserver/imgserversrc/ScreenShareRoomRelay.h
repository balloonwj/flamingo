/**
 *  屏幕共享房间中转的调度，ScreenShareRoomRelay.h
 *
 *  把成员表（RelayRoomHubT）与慢观看者丢帧策略（每个房间一个 RoomForwardPolicy）串起来，供 RelaySession 在收到
 *  1859 握手、1860 数据帧以及连接断开时调用：
 *    - 握手：登记成员；有新成员加入时向房间里所有已知的视频发送者请求关键帧；
 *    - 数据帧：取房间里的其他成员，逐个按丢帧策略决定转发还是丢弃；需要时向这路视频的发送者请求关键帧；
 *    - 断开：摘除成员、清掉策略里与他有关的状态，房间摘空后连同策略一起删除；
 *    - 云端录制（第 26 期，docs/屏幕共享与录制方案.md 12.6.2）：设置了录制器时，转发的每一帧另交一份给录制器，
 *      有人进房间时写一条座位记录；开始录制时向房间里所有已知的视频发送者请求关键帧，录像从关键帧开始才能解码。
 *  房间成员断开时只摘除他自己，不连带关闭其他人 —— 这一点与 RelayHub 的两端配对不同。设计见
 *  docs/屏幕共享与录制方案.md 5.4。
 *
 *  线程安全：可被多个 I/O 工作线程并发调用。策略表由 m_mutex 保护，成员表有自己的锁；加锁顺序固定为
 *  先 m_mutex 后成员表，且向成员发帧一律在锁外进行。
 *
 *  典型用法（RelaySession 里）：
 *      if (!m_roomRelay->join(roomId, seat, role, shared_from_this()))
 *          拒绝握手;
 *      m_roomRelay->forward(m_roomId, conn, header, cmd, seq, flags, originlen, plainBody);
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "../net/TcpConnection.h"
#include "RelayRoomHub.h"
#include "RoomForwardPolicy.h"
#include "RoomRelayFrame.h"

//中转会话，定义在 RelaySession.h；它反过来要用到本类，故这里只做前向声明
class RelaySession;
//云端录制的录制器，定义在 CloudRecorder.h
class CloudRecorder;

//屏幕共享房间中转的调度；由 RelayServer 持有一份，生命周期长于全部中转会话
class ScreenShareRoomRelay
{
public:
    //代入实际类型后的成员表
    typedef imgserver::RelayRoomHubT<RelaySession, std::shared_ptr<net::TcpConnection>> RoomHub;

    ScreenShareRoomRelay();
    ~ScreenShareRoomRelay();

    ScreenShareRoomRelay(const ScreenShareRoomRelay& rhs) = delete;
    ScreenShareRoomRelay& operator=(const ScreenShareRoomRelay& rhs) = delete;

    /**
     *  设置每个观看者的积压上限（imgserver 配置项 screenshareviewerbufferbytes）。须在服务开始接受连接之前调用。
     *  @param bytes 上限，单位字节；为 0 时按 RoomForwardPolicy::kDefaultViewerBufferCapBytes 处理
     */
    void setViewerBufferCapBytes(size_t bytes);

    //当前生效的积压上限，单位字节
    size_t getViewerBufferCapBytes() const;

    /**
     *  设置云端录制的录制器（第 26 期）。须在服务开始接受连接之前调用；不设置时不录制。
     *  @param recorder 录制器，不拥有，生命周期须长于本对象；传 NULL 取消
     */
    void setCloudRecorder(CloudRecorder* recorder);

    /**
     *  开始录某个房间（imgserver 收到 chatserver 的开始录制命令时调用）：把房间里此刻的座位归属交给录制器，
     *  开始成功后向房间里所有已知的视频发送者请求一次关键帧。
     *  @param recordId 录像 id（chatserver 分配，>0）
     *  @param roomId   屏幕共享房间号
     *  @return 取值见 cloudrec_start_result；没有设置录制器时返回 cloudrec_start_bad_arg
     */
    int startCloudRecord(uint64_t recordId, const std::string& roomId);

    /**
     *  录制器用的当前时刻：Unix 毫秒，但由单调时钟推算，系统时间被调整时不会前后跳动（录像里的相对时刻据此计算）。
     */
    static int64_t recordNowMs();

    /**
     *  登记一名成员；成功后向房间里所有已知的视频发送者请求一次关键帧（新成员需要关键帧才能开始解码）。
     *  @param roomId  房间号（令牌的 sessionid）
     *  @param seat    座位号（令牌的 peeruid 字段）
     *  @param role    角色，取值见 RelayToken::role_screenshare_*
     *  @param session 本端会话；成员表只保存其弱引用
     *  @return 登记成功返回 true；参数非法、座位已被占用、角色与座位不符返回 false
     */
    bool join(const std::string& roomId, int seat, int role, const std::shared_ptr<RelaySession>& session,
              int64_t userId = 0);

    /**
     *  把一帧转发给房间里除发送者以外的成员，逐个按丢帧策略决定转发还是丢弃。
     *  @param roomId    发送者登记时的房间号
     *  @param self      发送者的连接
     *  @param header    帧头（发送者座位号须已改写为发送者登记的座位）
     *  @param cmd       原帧的命令号（cmd_screenshare_relay_data），原样沿用
     *  @param seq       原帧的 seq，原样沿用
     *  @param srcFlags  原帧的标志，加密位由接收方会话按自己的密钥重新置位
     *  @param originlen 原帧的 originlen，原样沿用
     *  @param plainBody 解密后的包体（帧头 + 载荷，发送者座位号已改写）
     */
    void forward(const std::string& roomId, const std::shared_ptr<net::TcpConnection>& self,
                 const RoomRelayFrameHeader& header, uint16_t cmd, uint32_t seq, uint8_t srcFlags, uint32_t originlen,
                 const std::string& plainBody);

    /**
     *  按连接摘除一名成员（连接断开时调用）；房间摘空后连同其丢帧策略一起删除。
     *  @param conn      已断开的连接
     *  @param outRoomId [出参] 可为 NULL；摘除成功时回填房间号
     *  @param outSeat   [出参] 可为 NULL；摘除成功时回填座位号
     *  @return 该连接是房间成员并已摘除时返回 true；不是房间成员（如远程桌面、通话的中转连接）返回 false
     */
    bool leave(const std::shared_ptr<net::TcpConnection>& conn, std::string* outRoomId, int* outSeat);

    /**
     *  接收者发来订阅帧（群通话第 5 期）：交给该房间的丢帧策略记下，新订阅的画面立即向发送者请求关键帧。
     *  @param roomId       接收者登记时的房间号
     *  @param receiverSeat 接收者的座位号
     *  @param seats        要看的摄像头座位号；为空表示一路画面都不要
     */
    void setSubscription(const std::string& roomId, int receiverSeat, const std::vector<int>& seats);

    /**
     *  把一帧只转发给房间里某个座位的成员（第 18 期键鼠输入控制帧：只发给画面主人，不广播）。不经丢帧策略：
     *  控制帧本来就一律不丢。目标座位不在房间里或就是发送者自己时什么都不做。
     *  @param roomId     发送者登记时的房间号
     *  @param senderSeat 发送者的座位号（用于排除发给自己）
     *  @param targetSeat 目标座位号
     *  @param cmd、seq、srcFlags、originlen、plainBody 同 forward
     *  @return 已转发返回 true；目标不在或是发送者自己返回 false
     */
    bool forwardTo(const std::string& roomId, int senderSeat, int targetSeat, uint16_t cmd, uint32_t seq,
                   uint8_t srcFlags, uint32_t originlen, const std::string& plainBody);

private:
    //单调时钟的当前时刻，单位毫秒（只用于限频计时，不受系统时间调整影响）
    static int64_t nowMs();

    //取某房间的丢帧策略，没有时新建；调用方须已持有 m_mutex
    std::shared_ptr<RoomForwardPolicy> policyLocked(const std::string& roomId);

    //向房间里某个座位的成员发一帧关键帧请求；该成员已不在时什么都不做。须在锁外调用
    void requestKeyFrame(const std::string& roomId, int senderSeat, int videoTrack);

    RoomHub                                                    m_hub;              //成员表（自带锁）
    CloudRecorder*                                             m_recorder;         //云端录制的录制器（不拥有）；NULL 表示不录制。只在开始接受连接前设置，之后只读
    mutable std::mutex                                         m_mutex;            //保护以下两项
    size_t                                                     m_bufferCapBytes;   //每个观看者的积压上限，单位字节
    std::map<std::string, std::shared_ptr<RoomForwardPolicy>> m_policies;         //房间号 → 该房间的丢帧策略
};
