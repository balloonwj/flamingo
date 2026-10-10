/**
 *  文件夹传输的通道传输抽象，FolderLaneTransport.h
 *
 *  一次文件夹传输由 1 条控制通道（lane 0）与 N 条数据通道（lane 1..N）组成，每条通道收发的是
 *  FolderRelayFraming 定义的应用帧。原先每条通道固定是一条到 fileserver 的中转连接（FolderRelayClient）；
 *  P2P 方案第 5 期起，通道也可以是一条 P2P 链路上的一个可靠通道。本文件把发送 / 接收会话用到的那组方法抽成
 *  接口，会话只依赖接口：
 *    - IFolderLaneTransport        一条通道：连接、发应用帧、关闭、是否连着；收帧与断开经 FolderRelayClientHandler 回调
 *    - IFolderLaneTransportFactory 一次传输的通道工厂：会话按 lane 逐条创建通道
 *    - FolderRelayLaneTransport    中转实现：包装 FolderRelayClient，行为与原来完全相同
 *  会话参数里不给工厂时，会话自己按中转参数创建 FolderRelayLaneTransport，现有调用方（单聊文件夹传输的老路径、
 *  文件共享的传输子会话）不受影响。
 *
 *  全部写成内联：主程序与文件共享 DLL 编译同一份会话源文件，放在头文件里文件共享 DLL 的工程就不必新增 .cpp。
 *  本文件只包含 FolderRelayClient.h，不得包含主程序专属的头文件（守卫用例 FDG1）。
 *
 *  典型用法（会话内部）：
 *      std::unique_ptr<IFolderLaneTransport> t(factory != NULL ? factory->CreateLane(lane)
 *                                                               : new FolderRelayLaneTransport(ip, port, token, key));
 *      t->Connect(lane, handler, timeoutSec);
 *      t->SendAppFrame(frame, len);
 *      t->Close();
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stddef.h>
#include <string>

#include "FolderRelayClient.h"   // FolderRelayClient / FolderRelayClientHandler

namespace foldertransfer {

/**
 *  一条通道的传输。方法的语义与 FolderRelayClient 的同名方法相同：
 *  收到的应用帧与异常断开经 Connect 时交来的 FolderRelayClientHandler 回调（在实现自己的接收线程上，回调里可以阻塞，
 *  借此对发送端施加背压）；本端主动 Close 之后不再回调。
 */
class IFolderLaneTransport
{
public:
    virtual ~IFolderLaneTransport() {}

    /**
     *  建立本通道。阻塞直到成功、失败或超时。
     *  @param lane       通道序号：0 为控制通道，1..N 为数据通道
     *  @param handler    收帧与断开的回调（借用，生命周期须长于本通道，至少到 Close 返回）
     *  @param timeoutSec 超时（秒）：中转实现为连接 + 握手 + 报到的总超时；P2P 实现另有等待链路的时限
     *  @return 成功返回 true
     */
    virtual bool Connect(int lane, FolderRelayClientHandler* handler, int timeoutSec) = 0;

    /**
     *  发一个应用帧。可在任意线程上调用，实现负责线程安全与发送顺序。
     *  @param data 应用帧字节
     *  @param len  长度（字节），不超过 kFolderRelayMaxAppFrameBytes
     *  @return 已交出返回 true；未连接、已断开或发送失败返回 false
     */
    virtual bool SendAppFrame(const unsigned char* data, size_t len) = 0;

    //关闭本通道（幂等）；之后不再回调 handler
    virtual void Close() = 0;

    //本通道当前是否连着
    virtual bool IsConnected() const = 0;
};

/**
 *  一次传输的通道工厂：由会话的调用方提供（不拥有，生命周期须长于会话），会话按 lane 逐条创建通道。
 */
class IFolderLaneTransportFactory
{
public:
    virtual ~IFolderLaneTransportFactory() {}

    /**
     *  创建一条通道（尚未连接）。
     *  @param lane 通道序号
     *  @return 新的通道对象，所有权交给调用方
     */
    virtual IFolderLaneTransport* CreateLane(int lane) = 0;
};

/**
 *  中转实现：一条到 fileserver 的中转连接，全部方法转调 FolderRelayClient。
 */
class FolderRelayLaneTransport : public IFolderLaneTransport
{
public:
    /**
     *  @param relayIp      中转地址（1816 的 relayip）
     *  @param relayPort    中转端口（1816 的 relayport）
     *  @param token        中转令牌（1816 的 token）
     *  @param serverPubKey 服务器长期公钥（不拥有，须在本对象存续期间有效）；NULL 表示全程明文
     */
    FolderRelayLaneTransport(const std::string& relayIp, int relayPort, const std::string& token,
                             const unsigned char* serverPubKey)
        : m_relayIp(relayIp)
        , m_relayPort(relayPort)
        , m_token(token)
        , m_serverPubKey(serverPubKey)
    {
    }

    //连接中转、加密握手并以 lane 报到
    virtual bool Connect(int lane, FolderRelayClientHandler* handler, int timeoutSec) override
    {
        return m_client.Connect(m_relayIp, m_relayPort, m_token, lane, m_serverPubKey, handler, timeoutSec);
    }

    //经中转连接发一个应用帧
    virtual bool SendAppFrame(const unsigned char* data, size_t len) override
    {
        return m_client.SendAppFrame(data, len);
    }

    //关闭中转连接
    virtual void Close() override
    {
        m_client.Close();
    }

    //中转连接是否连着
    virtual bool IsConnected() const override
    {
        return m_client.IsConnected();
    }

private:
    std::string          m_relayIp;        // 中转地址
    int                  m_relayPort;      // 中转端口
    std::string          m_token;          // 中转令牌
    const unsigned char* m_serverPubKey;   // 服务器长期公钥（不拥有）；NULL 表示明文
    FolderRelayClient    m_client;         // 中转连接
};

} // namespace foldertransfer
