/**
 *  P2P 宿主在 exe 里使用的网络层：把 IP2PNetwork 的两个方法转给主连接 CIUSocket。
 *
 *  单独成文件、只编进 exe，是为了让 CP2PHost 本身不包含 IUSocket.h、能直接编进 FlamingoUnit 用真实的 DLL 测试
 *  （见 P2PHost.h）。本类没有状态，只做转发。
 *
 *  典型用法（CAppShell::LoadP2PModule）：
 *      CP2PHost::GetInstance().Load(dllPath, appDir, &CP2PSocketNetwork::GetInstance());
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include "P2PHost.h"

namespace balloonp2p {

/**
 *  CP2PSocketNetwork —— IP2PNetwork 的 exe 实现，转给 CIUSocket；进程内唯一。
 */
class CP2PSocketNetwork : public IP2PNetwork
{
public:
    /**
     *  取进程内唯一的对象。
     *  @return 网络层对象（静态存储）
     */
    static CP2PSocketNetwork& GetInstance();

    //以下为 IP2PNetwork 的实现

    /**
     *  经主连接发送一条 P2P 信令，转给 CIUSocket::SendP2PCommand（按 P2PChannelPolicy 校验）。线程安全。
     *  @param cmd  命令号
     *  @param body 包体（UTF-8 JSON）
     *  @return 已放入发送队列时返回帧 seq（大于 0）；不放行时返回 0
     */
    virtual uint32_t SendP2PCommand(uint16_t cmd, const std::string& body) override;

    /**
     *  取 chatserver 的公钥，转给 CIUSocket::GetServerPublicKey。线程安全。
     *  @return 指向 32 字节公钥的指针；尚未取得时返回 NULL
     */
    virtual const unsigned char* GetServerPublicKey() override;

private:
    CP2PSocketNetwork()
    {
    }
};

} // namespace balloonp2p
