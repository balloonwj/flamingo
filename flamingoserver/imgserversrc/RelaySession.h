/**
 *  中转会话（远程桌面 + 音视频通话共用），RelaySession.h
 *
 *  imgserver relay 通道上的一条连接：先与服务端做三帧协议加密握手（cmd_handshake），
 *  再由首个业务帧（远程桌面 cmd_rd_relay_hello / 音视频 cmd_av_relay_hello）携带
 *  relay 令牌报到，校验通过后按令牌里的 sessionid / role 登记到 RelayHub 与对端配对；
 *  之后每个数据帧（远程桌面 cmd_rd_relay_data 的屏幕帧 / 控制命令、音视频
 *  cmd_av_relay_data 的音视频帧）都转发给配对端。两类中转完全同构、共用同一套配对
 *  与转发逻辑，仅 cmd 取值不同（音视频会话与远程桌面会话各用独立 sessionid，
 *  天然不串话）。
 *
 *  转发不是字节级透传，而是<u>解密后重新加密</u>：两条连接各有各的会话密钥，
 *  收到的包体先用本会话的密钥解密，再用配对端会话的密钥加密后发出，理由见
 *  docs/协议加密方案.md 第 10 节。中途<u>不解压、不解析</u>包体——压缩标志是两端
 *  客户端之间的语义，服务端只把它随帧透传。
 *
 *  业务鉴权仍完全依赖 relay 令牌（RelayToken），不登录、不查库；加密握手解决的是
 *  链路机密性与服务端身份，与令牌各管一段。
 *
 *  屏幕共享（2026-10-02 起）也走这里，但不是两端配对而是一个房间挂多名成员：首帧
 *  cmd_screenshare_relay_hello 带的令牌角色为 7 / 8，座位号取自令牌的 peeruid，登记到
 *  ScreenShareRoomRelay；之后每个 cmd_screenshare_relay_data 读帧头、改写发送者座位号，再按
 *  慢观看者丢帧策略转发给房间里的其他成员。为此本类额外记录「已交给本连接、还没写进 socket
 *  的字节数」，供丢帧策略判断这名观看者是否积压。设计见 docs/屏幕共享与录制方案.md 5.4。
 *
 *  云端录制（第 26 期，屏幕共享与录制方案 12.6.2）的控制连接也走这里：chatserver 用角色为 role_server_control 的令牌
 *  以 cmd_cloudrec_ctl_hello 报到后，本会话把自己登记为 CloudRecordControl 的发送出口，之后收到的 cmd_cloudrec_ctl_data
 *  交给 CloudRecordControl 处理，它的事件也经本会话发回 chatserver。控制连接不能再做一对一报到或进房间，反之亦然。
 *
 *  balloonwj@qq.com   2026-06-08
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <memory>
#include <atomic>
#include "../net/ByteBuffer.h"
#include "../fileserversrc/TcpSession.h"
#include "RelayHub.h"
#include "CloudRecordControl.h"

//屏幕共享房间中转的调度，定义在 ScreenShareRoomRelay.h；它反过来要用到本类，故这里只做前向声明
class ScreenShareRoomRelay;

//继承 enable_shared_from_this 是为了在握手成功时把「自己」登记进 RelayHub：
//配对表保存的是会话的弱引用，而会话对象由 RelayServer 的 std::shared_ptr 持有，
//故这里必须能从 this 拿回那个 shared_ptr
//实现 ICloudRecordCtlSink 是为了在本会话是云端录制控制连接时，充当 CloudRecordControl 的发送出口
class RelaySession : public TcpSession, public std::enable_shared_from_this<RelaySession>, public ICloudRecordCtlSink
{
public:
    /**
     *  @param conn     本会话所属连接
     *  @param tokenkey relay 令牌的 HMAC 校验密钥（imgserver.conf 的 relaytokenkey）
     *  @param hub      配对表指针（由 RelayServer 持有，生命周期长于本会话；非空）
     *  @param roomRelay 屏幕共享房间中转的调度（由 RelayServer 持有，生命周期长于本会话；非空）
     *  @param cloudControl 云端录制控制（由 RelayServer 持有，生命周期长于本会话）；为 NULL 时拒绝控制连接报到
     */
    RelaySession(const std::shared_ptr<TcpConnection>& conn, const std::string& tokenkey, RelayHub* hub,
                 ScreenShareRoomRelay* roomRelay, CloudRecordControl* cloudControl);
    ~RelaySession();

    RelaySession(const RelaySession& rhs) = delete;
    RelaySession& operator=(const RelaySession& rhs) = delete;

    //有数据可读，会被工作 loop 调用
    void onRead(const std::shared_ptr<TcpConnection>& conn, ByteBuffer* pBuffer, Timestamp receivTime);

    /**
     *  把一帧中转数据发到本会话所属连接上。<u>由配对端会话在它自己的 I/O 线程里
     *  调用</u>，包体用的是本会话的密钥，所以这一步必须由本对象来做。
     *
     *  @param cmd       原帧的 cmd（cmd_rd_relay_data / cmd_av_relay_data），原样沿用
     *  @param seq       原帧的 seq，原样沿用
     *  @param srcFlags  原帧的 flags。压缩位、附件位等原样透传给对端客户端；
     *                   其中的加密位会被丢弃，按本连接是否加密重新置位
     *  @param originlen 原帧的 originlen，即两端客户端之间那一层的明文长度。
     *                   <u>不能按 plainBody 的长度重算</u>，否则对端解压的目标长度就错了
     *  @param plainBody 已用发送端会话密钥解密、但<u>仍可能是 zlib 压缩状态</u>的包体
     *  副作用：向本连接写出一个完整 v2 帧；加密或建帧失败时只记日志并丢弃本帧。
     */
    void forwardFrame(uint16_t cmd, uint32_t seq, uint8_t srcFlags, uint32_t originlen,
                      const std::string& plainBody);

    /**
     *  已交给本连接、还没写进 socket 的字节数（屏幕共享丢帧策略的输入）。包括两部分：经 forwardFrame
     *  投递、还在本连接 EventLoop 任务队列里的包体字节，以及最近一次发送后连接输出缓冲里的字节。
     *  可从任意线程调用；是近似值，输出缓冲部分只在每次发送后与写完成时刷新。
     *  @return 字节数
     */
    size_t pendingBytes() const;

    /**
     *  连接的输出缓冲已全部写进 socket（TcpConnection 的写完成回调，在本连接的 EventLoop 线程里调用）。
     *  把输出缓冲部分的积压计数清零。
     */
    void onWriteComplete(const std::shared_ptr<TcpConnection>& conn);

    //本会话是否是已报到的云端录制控制连接（RelayServer 在连接断开时据此通知 CloudRecordControl）
    bool isCloudRecordControl() const;

    //把一条云端录制控制消息发给 chatserver（CloudRecordControl 调用，任意线程）：以 cmd_cloudrec_ctl_data 发出
    virtual void sendCloudRecordCtl(const std::string& body) override;

private:
    //处理首帧握手（cmd_rd_relay_hello / cmd_av_relay_hello）：验令牌 + 配对；返回 false 表示应关连接
    //@param helloCmd 收到的握手 cmd（远程桌面 1853 或音视频 1855），用于回对应 cmd 的应答帧
    //@param body     已还原成明文的包体
    bool onHello(const std::shared_ptr<TcpConnection>& conn, uint16_t helloCmd, const std::string& body);

    //处理数据帧（cmd_rd_relay_data / cmd_av_relay_data）：解密后交给配对端重新加密发出；
    //返回 false 表示流已不可恢复，调用方须关连接
    //@param wireBody 线上包体（可能是密文），尚未解密
    bool onData(const std::shared_ptr<TcpConnection>& conn, uint16_t cmd, uint32_t seq,
                uint8_t flags, uint32_t originlen, const std::string& wireBody);

    //处理屏幕共享的首帧握手（cmd_screenshare_relay_hello）：验令牌、检查角色、按令牌里的座位号登记到房间；
    //返回 false 表示应关连接
    //@param body 已还原成明文的包体
    bool onRoomHello(const std::shared_ptr<TcpConnection>& conn, const std::string& body);

    //处理屏幕共享数据帧（cmd_screenshare_relay_data）：解密、读帧头、改写发送者座位号，再交给房间中转按丢帧
    //策略转发；返回 false 表示流已不可恢复，调用方须关连接
    //@param wireBody 线上包体（可能是密文），尚未解密
    bool onRoomData(const std::shared_ptr<TcpConnection>& conn, uint16_t cmd, uint32_t seq,
                    uint8_t flags, uint32_t originlen, const std::string& wireBody);

    //处理云端录制控制连接的报到（cmd_cloudrec_ctl_hello）：验令牌、要求角色为 role_server_control，登记为控制出口；
    //返回 false 表示应关连接
    //@param body 已还原成明文的包体
    bool onControlHello(const std::shared_ptr<TcpConnection>& conn, const std::string& body);

    //处理云端录制控制命令（cmd_cloudrec_ctl_data）：还原包体后交给 CloudRecordControl，有回复时发回；
    //未报到为控制连接就发来时返回 false（关连接）
    //@param wireBody 线上包体（可能是密文），尚未解密
    bool onControlData(const std::shared_ptr<TcpConnection>& conn, uint16_t cmd, uint32_t seq,
                       uint8_t flags, uint32_t originlen, const std::string& wireBody);

    //回握手应答帧（包体 {code,msg}）；应答 cmd 与收到的握手 cmd 一致（1853 / 1855）
    void sendHelloAck(uint16_t helloCmd, int code, const std::string& msg);

    //本会话发送普通帧的漏斗：包体按明文传入，压缩与加密交给 FramePayload 管线。
    //可从任意线程调用，见实现里的说明
    void sendFrame(uint16_t cmd, uint32_t seq, uint8_t extraFlags, const std::string& body);
    //在本连接所属的 EventLoop 线程里完成「组帧 + 加密 + 写 socket」。
    //只允许由 sendFrame 直接调用（已在该线程时）或经 runInLoop 投递后执行
    void sendFrameInLoop(uint16_t cmd, uint32_t seq, uint8_t extraFlags, const std::string& body);
    //forwardFrame 的实际执行体，同样只在本连接所属的 EventLoop 线程里执行
    void forwardFrameInLoop(uint16_t cmd, uint32_t seq, uint8_t srcFlags, uint32_t originlen,
                            const std::string& plainBody);

private:
    std::string m_tokenKey;    //relay 令牌 HMAC 密钥
    RelayHub*   m_hub;         //配对表（不拥有）
    bool        m_paired;      //是否已完成握手并登记到配对表
    std::string m_sessionId;   //本会话 id（握手后填）
    int         m_role;        //本端角色，见 RelayToken::role_*（握手后填）
    int         m_pairKind;    //一对一报到的种类（relay_pair_kind，见 RelayHelloPolicy.h）：决定能转发哪些数据命令；报到前为 relay_pair_none

    ScreenShareRoomRelay* m_roomRelay;         //屏幕共享房间中转的调度（不拥有）
    bool                  m_inRoom;            //是否已完成屏幕共享握手并登记到房间（只在本连接的 I/O 线程读写）
    std::string           m_roomId;            //屏幕共享房间号（握手后填）
    int                   m_seat;              //屏幕共享座位号（握手后填）；未登记时为 -1
    bool                  m_trackDropLogged;   //是否已记过「轨道不允许、丢弃」的日志（每条连接只记一次，只在本连接的 I/O 线程读写）
    bool                  m_inputDropLogged;   //是否已记过「键鼠输入的目标座位不在房间里、丢弃」的日志（屏幕共享第 18 期；每条连接
                                               //只记一次，只在本连接的 I/O 线程读写）
    std::atomic<size_t>   m_queuedBytes;       //经 forwardFrame 投递、还在 EventLoop 任务队列里的包体字节数
    std::atomic<size_t>   m_outputBytes;       //最近一次发送后连接输出缓冲里的字节数；写完成时清零

    CloudRecordControl*   m_cloudControl;      //云端录制控制（不拥有）；为 NULL 时拒绝控制连接报到
    std::atomic<bool>     m_control;           //是否已作为云端录制控制连接报到（I/O 线程写，断开回调读）
};
