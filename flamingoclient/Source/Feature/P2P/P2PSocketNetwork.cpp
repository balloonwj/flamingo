/**
 *  P2P 宿主在 exe 里使用的网络层的实现，见 P2PSocketNetwork.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "P2PSocketNetwork.h"

#include "../../net/IUSocket.h"

namespace balloonp2p {

CP2PSocketNetwork& CP2PSocketNetwork::GetInstance()
{
    static CP2PSocketNetwork s_instance;
    return s_instance;
}

uint32_t CP2PSocketNetwork::SendP2PCommand(uint16_t cmd, const std::string& body)
{
    return CIUSocket::GetInstance().SendP2PCommand(cmd, body);
}

const unsigned char* CP2PSocketNetwork::GetServerPublicKey()
{
    return CIUSocket::GetInstance().GetServerPublicKey();
}

} // namespace balloonp2p
