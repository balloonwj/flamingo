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
 *  balloonwj@qq.com   2026-06-08
 */
#pragma once

#include <stdint.h>
#include <string>
#include <memory>
#include <mutex>
#include "../net/ByteBuffer.h"
#include "../fileserversrc/TcpSession.h"
#include "RelayHub.h"

//继承 enable_shared_from_this 是为了在握手成功时把「自己」登记进 RelayHub：
//配对表保存的是会话的弱引用，而会话对象由 RelayServer 的 std::shared_ptr 持有，
//故这里必须能从 this 拿回那个 shared_ptr
class RelaySession : public TcpSession, public std::enable_shared_from_this<RelaySession>
{
public:
    /**
     *  @param conn     本会话所属连接
     *  @param tokenkey relay 令牌的 HMAC 校验密钥（imgserver.conf 的 relaytokenkey）
     *  @param hub      配对表指针（由 RelayServer 持有，生命周期长于本会话；非空）
     */
    RelaySession(const std::shared_ptr<TcpConnection>& conn, const std::string& tokenkey, RelayHub* hub);
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

    //回握手应答帧（包体 {code,msg}）；应答 cmd 与收到的握手 cmd 一致（1853 / 1855）
    void sendHelloAck(uint16_t helloCmd, int code, const std::string& msg);

    //本会话发送普通帧的漏斗：包体按明文传入，压缩与加密交给 FramePayload 管线
    void sendFrame(uint16_t cmd, uint32_t seq, uint8_t extraFlags, const std::string& body);

private:
    std::string m_tokenKey;    //relay 令牌 HMAC 密钥
    RelayHub*   m_hub;         //配对表（不拥有）
    bool        m_paired;      //是否已完成握手并登记到配对表
    std::string m_sessionId;   //本会话 id（握手后填）
    int         m_role;        //本端角色，见 RelayToken::role_*（握手后填）

    //本连接发送方向的串行锁。加密器内部虽然自带互斥量，但它只保证单次加密是原子的：
    //本连接的帧有两个来源（本会话自己回的应答帧、配对端 I/O 线程转发来的数据帧），
    //若两者各自加密完再各自投递，写到 socket 的先后可能与加密的先后相反，接收端
    //按流顺序解密必然失败。故把「加密 + 投递」整体锁起来。
    std::mutex  m_sendMutex;
};
