/**
 *  音视频通话中转客户端，CallRelayClient.h
 *
 *  管理一条到 imgserver 中转端口（与远程桌面共用，一期 20003）的独立 TCP 连接：
 *  连上后用 cmd_av_relay_hello(1855) 携 relay 令牌握手，imgserver 校验令牌并按
 *  会话配对；握手成功后在该连接上双向收发 cmd_av_relay_data(1856) 帧——其包体
 *  是编码后的音视频帧（第一期为验证中转通道的测试帧），imgserver 不解析、只按
 *  会话原样转发给配对端。
 *
 *  一次通话的「本端」持有一个本类实例：主叫 / 被叫各用它把本端音视频帧发出、收回
 *  对端音视频帧。收发是双向实时流，故内部起一条接收线程阻塞收字节、拆帧后经回调
 *  把帧包体交给上层（上层通常拷出后转投 UI / 播放线程）。本连接独立于 chatserver
 *  主连接与图片 / 文件连接，互不影响。
 *
 *  与远程桌面中转客户端 RdRelayClient 同构，差异：握手 / 数据用音视频的 1855 /
 *  1856 cmd；第一期纯语音帧很小（远不到中转单帧上限），data 帧包体即整个音视频帧、
 *  不分片，故直接用 net::FrameCodec 编解码、不引入分片重组（第二期接入视频、帧可能
 *  超过单帧上限时，再参照 RdRelayFraming 补分片逻辑）。socket 收发不在单测覆盖范围，
 *  靠真机联调验证；帧编解码复用已有单测覆盖的 net::FrameCodec。
 *
 *  balloonwj@qq.com   2026-06-11
 */
#pragma once

#include <winsock2.h>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>

#include "../../net/FrameCrypto.h"   // net::ClientHandshake（本连接自己的会话密钥）

namespace call {

//中转通道事件回调接口。
//注意：回调在 CallRelayClient 的接收线程上触发，实现方须自行保证线程安全（典型做法
//是把数据拷出后 PostMessage 转投 UI 线程），不可在回调里做耗时阻塞操作。
class CallRelayClientHandler
{
public:
    virtual ~CallRelayClientHandler() {}

    /**
     *  收到一个中转数据帧的包体（cmd_av_relay_data 帧包体）。
     *  第一期为对端发来的测试帧；第二期为对端的音视频帧。
     *  @param data 包体字节起始地址；仅在本次回调内有效，需留存须自行拷贝
     *  @param len  包体字节数
     */
    virtual void OnRelayData(const unsigned char* data, size_t len) = 0;

    /**
     *  连接断开（对端关闭 / 网络错误 / 解帧失败）。
     *  本端主动调用 Close() 关闭时不回调本方法，仅异常断开时回调一次。
     */
    virtual void OnRelayClosed() = 0;
};

//音视频通话中转客户端：一条到 imgserver 中转端口的独立连接，握手后在其上双向收发数据帧。
class CallRelayClient
{
public:
    CallRelayClient();
    ~CallRelayClient();

    CallRelayClient(const CallRelayClient&) = delete;
    CallRelayClient& operator=(const CallRelayClient&) = delete;

    /**
     *  连接中转端口并完成握手。阻塞直到握手成功 / 失败 / 超时。
     *  @param ip         中转通道地址（cmd_call_signaling 下发，点分十进制或主机名）
     *  @param port       中转通道端口（cmd_call_signaling 下发，一期为 20003）
     *  @param token      relay 令牌（cmd_call_signaling 下发，非空），作握手帧包体提交校验
     *  @param handler    事件回调（非空；生命周期须长于本连接，至少持续到 Close 返回）
     *  @param timeoutSec 连接 + 握手总超时（秒）
     *  @return 握手成功（imgserver 回 code==0）返回 true；连接失败 / 令牌非法 / 超时返回 false
     */
    bool Connect(const std::string& ip, int port, const std::string& token,
                 CallRelayClientHandler* handler, int timeoutSec = 5);

    /**
     *  发送一个数据帧（封成 cmd_av_relay_data 帧发出）。线程安全（内部串行化发送）。
     *  @param data 数据字节起始地址
     *  @param len  数据字节数
     *  @return 发送成功返回 true；未连接 / 超出中转包体上限 / socket 错误返回 false
     */
    bool SendData(const unsigned char* data, size_t len);

    //关闭连接并停止接收线程。幂等；Close 后不再回调 handler。
    void Close();

    //是否处于已连接（握手成功且未断开）状态。
    bool IsConnected() const;

private:
    //接收线程主体：循环阻塞收字节、拆帧、把 data 帧包体分发给 handler。
    void RecvProc();

    //非阻塞 connect + select 等待（仿 CIUSocket::ConnectToImgServer），成功置 m_sock 并切回阻塞模式。
    bool DoConnect(const std::string& ip, int port, int timeoutSec);

    //在已连接 socket 上把整段字节全部发出（处理部分发送）；失败 / 断开返回 false。
    bool SendAll(const char* data, size_t len);

    /**
     *  在刚建好的中转连接上同步走完三帧协议加密握手。
     *
     *  <u>必须排在 relay 报到帧（cmd_av_relay_hello）之前</u>：报到帧里带的是中转令牌，
     *  明文发出去等于把凭据白送，抓到它的人可以抢先冒充一端接进本次通话。也<u>必须
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
    CallRelayClientHandler*      m_handler;     //事件回调（不拥有；生命周期由上层保证）
    std::unique_ptr<std::thread> m_recvThread;  //接收线程
    std::mutex                   m_sendMtx;     //串行化发送，避免多线程交错写同一 socket
    std::string                  m_recvBuf;     //接收累积缓冲（接收线程私有，处理粘包 / 半包）

    //本连接的协议加密状态。每个实例各一个——自测场景下同进程会同时存在两条连接，
    //共用会让两条流互相踩踏。
    //<u>发送方向的加密必须在 m_sendMtx 之内做</u>：本类的发送方是音频采集线程与
    //视频采集线程两条，若在锁外加密、锁内发送，密文上线顺序会与加密顺序错开，
    //服务端一定解不开。详见 SendData 的注释。
    net::ClientHandshake         m_handshake;
};

} // namespace call
