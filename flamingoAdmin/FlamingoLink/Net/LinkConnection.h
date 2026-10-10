/**
 *  FlamingoLink.dll —— 到 chatserver 管理端口的一条 TCP 连接：建连（带超时）、三帧加密握手、v2 帧的收发。
 *
 *  一个对象只对应一次连接尝试，断开后丢弃、重连时新建，因此握手状态与收包缓冲不必复位。
 *  全部方法只在连接线程上调用，不加锁。帧编解码与加密沿用 PC 客户端 Source/net 下的实现
 *  （FrameCodec / FramePayload / FrameCrypto / HandshakeRunner），与 20000 端口完全相同。
 *
 *  典型用法（连接线程）：
 *      LinkConnection c;
 *      if (!c.Connect(host, port, kConnectTimeoutMs, stopFlag, err)) { ...重连... }
 *      if (encrypt && !c.Handshake(pubkey, err)) { ...重连... }
 *      c.SendFrame(cmd_adminlink_auth, 1, body, err);
 *      while (c.PollRecv(200, err) >= 0) { while (c.NextFrame(cmd, seq, body, err) == 1) { ... } }
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <winsock2.h>
#include <stdint.h>
#include <string>
#include <atomic>

#include "../../../flamingoclient/Source/net/HandshakeRunner.h"

namespace flamingolink {

/**
 *  到管理端口的一条连接。不可复制。
 */
class LinkConnection : public net::IHandshakeChannel
{
public:
    LinkConnection();

    //析构时关闭 socket
    ~LinkConnection();

    LinkConnection(const LinkConnection&) = delete;
    LinkConnection& operator=(const LinkConnection&) = delete;

    /**
     *  解析主机名并建立 TCP 连接。连接等待期间每 200 毫秒检查一次 stop，被要求停止时立即放弃。
     *  @param host       主机名或 IP（UTF-16）
     *  @param port       端口，1~65535
     *  @param timeoutMs  连接超时（毫秒）
     *  @param stop       停止标志，由连接线程的拥有者置位；本函数只读
     *  @param err        [出参] 失败原因（英文），成功时清空
     *  @return 成功返回 true
     */
    bool Connect(const std::wstring& host, int port, int timeoutMs, const std::atomic<bool>& stop, std::string& err);

    /**
     *  在刚建好的连接上同步走完三帧加密握手。成功之后收发的帧都加密。
     *  @param serverPubKey  服务端身份公钥，net::IDENTITY_PUBLICKEY_BYTES 字节
     *  @param err           [出参] 失败原因（英文）
     *  @return 成功返回 true；失败时调用方必须放弃这条连接，不得退回明文
     */
    bool Handshake(const unsigned char* serverPubKey, std::string& err);

    /**
     *  组帧（握手完成后加密）并发出。
     *  @param cmd   命令号
     *  @param seq   序列号
     *  @param body  明文包体，可为空串
     *  @param err   [出参] 失败原因
     *  @return 全部写出返回 true
     */
    bool SendFrame(uint16_t cmd, uint32_t seq, const std::string& body, std::string& err);

    /**
     *  等待可读并把收到的字节追加进收包缓冲。
     *  @param timeoutMs  最长等待毫秒数
     *  @param err        [出参] 失败原因
     *  @return 1 收到了数据；0 超时没有数据；-1 出错或对端关闭
     */
    int PollRecv(int timeoutMs, std::string& err);

    /**
     *  从收包缓冲里取出下一个完整帧并还原成明文（先解密、后解压）。
     *  @param cmd   [出参] 命令号
     *  @param seq   [出参] 序列号
     *  @param body  [出参] 明文包体
     *  @param err   [出参] 失败原因
     *  @return 1 取出一帧；0 缓冲里不足一帧；-1 协议错误或解密失败（调用方必须断开）
     */
    int NextFrame(uint16_t& cmd, uint32_t& seq, std::string& body, std::string& err);

    /**
     *  关闭连接。重复调用安全。
     */
    void Close();

    //以下为 net::IHandshakeChannel 的实现，只在 Handshake 期间使用

    /**
     *  把 len 个字节全部发出（socket 处于阻塞模式，超时由 SO_SNDTIMEO 控制）。
     *  @param data 字节起始地址；len 字节数
     *  @return 全部发出返回 true
     */
    bool SendAll(const char* data, size_t len) override;

    /**
     *  收满 len 个字节（超时由 SO_RCVTIMEO 控制）。
     *  @param buf 接收缓冲，容量不小于 len；len 要收的字节数
     *  @return 收满返回 true；出错、超时或对端关闭返回 false
     */
    bool RecvAll(char* buf, size_t len) override;

private:
    SOCKET               m_sock;       //连接的 socket；未连接或已关闭时为 INVALID_SOCKET
    std::string          m_inbuf;      //收包缓冲：已收到、尚未解出完整帧的字节
    net::ClientHandshake m_hs;         //本连接的握手器，握手完成后用它的 cipher() 加解密
    bool                 m_encrypted;  //是否已完成握手（之后收发的帧都加密）
};

} // namespace flamingolink
