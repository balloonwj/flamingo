/**
 *  远程桌面中转客户端，RdRelayClient.h
 *
 *  管理一条到 imgserver 中转端口（一期 20003）的独立 TCP 连接：连上后用
 *  cmd_rd_relay_hello(1853) 携 relay 令牌握手，imgserver 校验令牌并按会话
 *  配对；握手成功后在该连接上双向收发 cmd_rd_relay_data(1854) 帧——其包体
 *  是引擎帧（屏幕帧 / 控制命令），imgserver 不解析、只按会话原样转发给配对端。
 *
 *  一次远程桌面会话的「本端」持有一个本类实例：被控端用它把抓到的屏幕帧发出、
 *  收回控制命令；控制端用它收回屏幕帧、把鼠标键盘命令发出。收发是双向实时流，
 *  故内部起一条接收线程阻塞收字节、拆帧后经回调把引擎帧交给上层（上层通常拷出
 *  后转投 UI 线程）。本连接独立于 chatserver 主连接与 imgserver 图片连接，互不
 *  影响。
 *
 *  封帧 / 拆帧逻辑在 RdRelayFraming（纯函数、可单测）；本类只负责 socket 连接、
 *  握手与收发线程，调用 RdRelayFraming 完成字节与帧的转换。socket 收发本身不在
 *  单测覆盖范围，靠真机联调验证。
 *
 *  balloonwj@qq.com   2026-06-09
 */
#pragma once

#include <winsock2.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include "RdRelayFraming.h"   // RdRelayInboundFrame / relayframing::*
#include "../../Source/net/FrameCrypto.h"   // net::ClientHandshake（本连接自己的会话密钥）

namespace remotedesktop {

//中转通道事件回调接口。
//注意：回调在 RdRelayClient 的接收线程上触发，实现方须自行保证线程安全（典型做法
//是把数据拷出后 PostMessage 转投 UI 线程），不可在回调里做耗时阻塞操作。
class RdRelayClientHandler
{
public:
    virtual ~RdRelayClientHandler() {}

    /**
     *  收到一个引擎帧（rd_relay_data 帧包体，首字节为 remotedesktop::rd_frame_type）。
     *  @param data 引擎帧字节起始地址；仅在本次回调内有效，需留存须自行拷贝
     *  @param len  引擎帧字节数
     */
    virtual void OnRelayEngineFrame(const unsigned char* data, size_t len) = 0;

    /**
     *  连接断开（对端关闭 / 网络错误 / 解帧失败）。
     *  本端主动调用 Close() 关闭时不回调本方法，仅异常断开时回调一次。
     */
    virtual void OnRelayClosed() = 0;
};

//远程桌面中转客户端：一条到 imgserver 中转端口的独立连接，握手后在其上双向收发引擎帧。
class RdRelayClient
{
public:
    RdRelayClient();
    ~RdRelayClient();

    RdRelayClient(const RdRelayClient&) = delete;
    RdRelayClient& operator=(const RdRelayClient&) = delete;

    /**
     *  连接中转端口并完成握手。阻塞直到握手成功 / 失败 / 超时。
     *  @param ip         中转通道地址（cmd_rd_relay_info 下发，点分十进制或主机名）
     *  @param port       中转通道端口（cmd_rd_relay_info 下发，一期为 20003）
     *  @param token      relay 令牌（cmd_rd_relay_info 下发，非空），作握手帧包体提交校验
     *  @param serverPubKey 服务端公钥（net::IDENTITY_PUBLICKEY_BYTES 字节），由调用方取自主连接或宿主；
     *                   为 NULL 表示服务端未配置公钥，本连接全程明文。内部复制保存，返回后调用方即可释放
     *  @param handler    事件回调（非空；生命周期须长于本连接，至少持续到 Close 返回）
     *  @param timeoutSec 连接 + 握手总超时（秒）
     *  @return 握手成功（imgserver 回 code==0）返回 true；连接失败 / 令牌非法 / 超时返回 false
     */
    bool Connect(const std::string& ip, int port, const std::string& token, const unsigned char* serverPubKey,
                 RdRelayClientHandler* handler, int timeoutSec = 5);

    /**
     *  发送一个引擎帧（封成 rd_relay_data 帧发出）。线程安全（内部串行化发送）。
     *  @param data 引擎帧字节起始地址（首字节为 rd_frame_type）
     *  @param len  引擎帧字节数
     *  @return 发送成功返回 true；未连接 / 超出中转包体上限 / socket 错误返回 false
     */
    bool SendEngineFrame(const unsigned char* data, size_t len);

    //关闭连接并停止接收线程。幂等；Close 后不再回调 handler。
    void Close();

    //是否处于已连接（握手成功且未断开）状态。
    bool IsConnected() const;

private:
    //接收线程主体：循环阻塞收字节、拆帧、按 cmd 把引擎帧分发给 handler。
    void RecvProc();

    //非阻塞 connect + select 等待（仿 CIUSocket::ConnectToImgServer），成功置 m_sock 并切回阻塞模式。
    bool DoConnect(const std::string& ip, int port, int timeoutSec);

    //在已连接 socket 上把整段字节全部发出（处理部分发送）；失败 / 断开返回 false。
    bool SendAll(const char* data, size_t len);

    /**
     *  在刚建好的中转连接上同步走完三帧协议加密握手。
     *
     *  <u>必须排在 relay 报到帧（cmd_rd_relay_hello）之前</u>：报到帧里带的是中转令牌，
     *  明文发出去等于把凭据白送，抓到它的人可以抢先冒充一端接进本次会话。也<u>必须
     *  排在接收线程启动之前</u>，否则会与它抢同一份数据。
     *
     *  未配置服务端公钥时直接返回成功且不做任何收发，此时本连接走明文。
     *
     *  @param timeoutSec 每次收发的超时时间，单位秒
     *  @return 成功返回 true；失败时调用方须放弃这条连接
     *  副作用：在中转连接上收发三帧；改写 m_handshake 的状态。
     */
    bool PerformHandshake(int timeoutSec);

private:
    //握手期间的字节收发适配器，定义在 .cpp 里。做成嵌套类是因为它要用本类的
    //私有 SendAll 与 socket。
    class HandshakeChannel;

    SOCKET                       m_sock;        //中转连接套接字；INVALID_SOCKET 表示未连接
    std::atomic<bool>            m_connected;   //是否已握手成功且未断开（收发与 IsConnected 的判据）
    std::atomic<bool>            m_stopping;    //是否正在主动关闭（接收线程据此区分主动关 vs 异常断；主动关不回调 OnRelayClosed）
    RdRelayClientHandler*        m_handler;     //事件回调（不拥有；生命周期由上层保证）
    std::unique_ptr<std::thread> m_recvThread;  //接收线程
    std::mutex                   m_sendMtx;     //串行化发送，避免多线程交错写同一 socket
    std::string                  m_recvBuf;     //接收累积缓冲（接收线程私有，处理粘包 / 半包）
    RdRelayReassembler           m_reassembler; //接收侧分片重组器（接收线程私有，把分片还原成完整引擎帧）

    //服务端公钥的副本（Connect 时从调用方传入的指针复制而来），供 PerformHandshake 使用
    unsigned char                m_serverPubKey[net::IDENTITY_PUBLICKEY_BYTES];
    //是否有服务端公钥；false 表示服务端未配置公钥，本连接全程明文
    bool                         m_hasServerPubKey;

    //本连接的协议加密状态。每个实例各一个——自测场景下同进程会同时存在被控端与
    //控制端两条连接，共用会让两条流互相踩踏。
    //<u>发送方向的加密必须在 m_sendMtx 之内做</u>：流加密按调用顺序推进内部计数器，
    //若在锁外加密、锁内发送，两条业务线程产出的密文上线顺序会与加密顺序不一致，
    //对端一定解不开。详见 SendEngineFrame 的注释。
    net::ClientHandshake         m_handshake;
};

} // namespace remotedesktop
