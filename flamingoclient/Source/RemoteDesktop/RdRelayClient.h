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
     *  @param handler    事件回调（非空；生命周期须长于本连接，至少持续到 Close 返回）
     *  @param timeoutSec 连接 + 握手总超时（秒）
     *  @return 握手成功（imgserver 回 code==0）返回 true；连接失败 / 令牌非法 / 超时返回 false
     */
    bool Connect(const std::string& ip, int port, const std::string& token,
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

private:
    SOCKET                       m_sock;        //中转连接套接字；INVALID_SOCKET 表示未连接
    std::atomic<bool>            m_connected;   //是否已握手成功且未断开（收发与 IsConnected 的判据）
    std::atomic<bool>            m_stopping;    //是否正在主动关闭（接收线程据此区分主动关 vs 异常断；主动关不回调 OnRelayClosed）
    RdRelayClientHandler*        m_handler;     //事件回调（不拥有；生命周期由上层保证）
    std::unique_ptr<std::thread> m_recvThread;  //接收线程
    std::mutex                   m_sendMtx;     //串行化发送，避免多线程交错写同一 socket
    std::string                  m_recvBuf;     //接收累积缓冲（接收线程私有，处理粘包 / 半包）
    RdRelayReassembler           m_reassembler; //接收侧分片重组器（接收线程私有，把分片还原成完整引擎帧）
};

} // namespace remotedesktop
