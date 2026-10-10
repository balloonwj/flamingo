/**
 *  管理控制台长连接的会话类，AdminLinkSession.h
 *
 *  管理端口（adminlinklistenport）上的每条 TCP 连接对应一个实例，由 AdminLinkServer 的会话表持有。
 *  负责：解出 v2 帧并走加密握手（与 20000 端口相同的 net::processInboundFrame），回应心跳，
 *  用 HTTP 登录得到的 token 鉴权（2501），校验订阅请求（2502 / 2503）后交给 AdminLinkServer
 *  的推送线程处理。在线名单的计算与推送都不在本类里。
 *
 *  线程：onRead 及其调用的处理函数运行在本连接所属的 I/O 线程上；AdminLinkServer 的推送线程
 *  会读取 isAuthed / getToken / getLastRecvSec 等状态并调用 send，这些状态因此用原子量或锁保护。
 *
 *  典型用法（AdminLinkServer::onConnected 内）：
 *      std::shared_ptr<AdminLinkSession> s(new AdminLinkSession(conn));
 *      conn->setMessageCallback(std::bind(&AdminLinkSession::onRead, s.get(), _1, _2, _3));
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <string>
#include <mutex>
#include <atomic>
#include "../net/ByteBuffer.h"
#include "TcpSession.h"

using namespace net;

//v2 帧头，定义在 Protocol.h；此处仅按 const 引用传递，理由同 ChatSession.h：
//本头文件会被 HttpSession.cpp 间接包含，不能直接包含 Protocol.h
struct frame_header;

/**
 *  管理端口上的一条控制台连接。对象一律由 shared_ptr 持有（基类发送路径要用 shared_from_this）。
 */
class AdminLinkSession : public TcpSession
{
public:
    /**
     *  @param conn  本会话对应的连接；会话只持有它的弱引用
     */
    explicit AdminLinkSession(const std::shared_ptr<TcpConnection>& conn);
    virtual ~AdminLinkSession();

    AdminLinkSession(const AdminLinkSession& rhs) = delete;
    AdminLinkSession& operator =(const AdminLinkSession& rhs) = delete;

    /**
     *  有数据可读时由网络层回调，运行在本连接的 I/O 线程上。逐个解出完整帧并分发；
     *  解帧失败、加密校验失败、鉴权前收到不允许的命令时断开连接。
     */
    void onRead(const std::shared_ptr<TcpConnection>& conn, ByteBuffer* pBuffer, Timestamp receivTime);

    //是否已鉴权成功。任意线程可调
    bool isAuthed() const { return m_authed.load(); }

    //连接建立的时间，Unix 秒。构造后不再改变
    int64_t getConnectTimeSec() const { return m_connectTimeSec; }

    //最近一次收到完整帧的时间，Unix 秒。任意线程可调
    int64_t getLastRecvSec() const { return m_lastRecvSec.load(); }

    /**
     *  取鉴权时记下的身份。未鉴权时 token 为空、ID 均为 0。任意线程可调（内部加锁）。
     *  @param outToken             [出参] 鉴权用的 token
     *  @param outAdminId           [出参] 管理员ID
     *  @param outAdminEnterpriseId [出参] 管理员的管辖企业，0 表示平台超管
     */
    void getIdentity(std::string& outToken, int64_t& outAdminId, int64_t& outAdminEnterpriseId);

    //对端地址（ip:port），只用于日志。构造后不再改变
    const std::string& getPeer() const { return m_peer; }

    /**
     *  主动断开本连接。任意线程可调（TcpConnection::forceClose 会把动作投递回连接自己的线程）。
     *  @param reason  断开原因（英文），写进日志
     */
    void close(const char* reason);

private:
    //处理加密握手与帧体解密，语义同 ChatSession::processFrameSecurity。返回 false 表示已断开连接
    bool processFrameSecurity(const std::shared_ptr<TcpConnection>& conn, const frame_header& header,
                              std::string& body, bool& handled);

    //按 cmd 分发一帧。返回 false 表示应断开连接（鉴权前收到不允许的命令、或未知命令）
    bool process(uint16_t cmd, uint32_t seq, const std::string& body);

    //1002：原样回一个空包体心跳
    void onHeartbeat(uint32_t seq);

    //2501：用 token 鉴权，成功后记下身份
    void onAuth(uint32_t seq, const std::string& body);

    //2502：校验订阅请求与权限，通过后交给 AdminLinkServer 的推送线程登记并发完整名单
    void onSubscribe(uint32_t seq, const std::string& body);

    //2503：交给推送线程退订
    void onUnsubscribe(uint32_t seq);

private:
    const int64_t        m_connectTimeSec;      //连接建立的时间，Unix 秒；未鉴权超时据此判定
    std::atomic<int64_t> m_lastRecvSec;         //最近一次收到完整帧的时间，Unix 秒；空闲超时据此判定
    std::atomic<bool>    m_authed;              //是否已鉴权成功；推送线程据此决定是否复查 token
    std::mutex           m_identityMutex;       //保护下面三项身份字段（I/O 线程写、推送线程读）
    std::string          m_token;               //鉴权用的 token；推送线程定期用它复查有效性
    int64_t              m_adminId;             //管理员ID；未鉴权时为 0
    int64_t              m_adminEnterpriseId;   //管理员的管辖企业，0 表示平台超管；订阅权限据此判定
    std::string          m_peer;                //对端地址 ip:port，只用于日志
};
