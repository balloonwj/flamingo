/**
 *  imgserver 一对一中转「报到与转发」判断的实现，见 RelayHelloPolicy.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "RelayHelloPolicy.h"

#include "../chatserversrc/Protocol.h"   //cmd_rd_relay_* / cmd_av_relay_* / cmd_p2p_relay_*
#include "../utils/RelayToken.h"

namespace {

//角色是否属于 P2P 回落中转（11 发起方、12 应答方）
bool IsP2PRole(int role)
{
    return role == RelayToken::role_p2p_controlling || role == RelayToken::role_p2p_controlled;
}

//角色是否只用于服务端之间（云端录制的控制连接，第 26 期）：客户端的一对一报到一律不接受
bool IsServerRole(int role)
{
    return role == RelayToken::role_server_control;
}

} // namespace

int RelayHelloPolicy::pairKindOfHello(uint16_t helloCmd)
{
    switch (helloCmd)
    {
    //远程桌面报到
    case cmd_rd_relay_hello:
        return relay_pair_rd;

    //音视频报到
    case cmd_av_relay_hello:
        return relay_pair_av;

    //P2P 回落中转报到
    case cmd_p2p_relay_hello:
        return relay_pair_p2p;

    //其余命令（屏幕共享房间报到、各种数据帧等）都不是一对一报到
    default:
        return relay_pair_none;
    }
}

bool RelayHelloPolicy::isAcceptedPairRole(int kind, int role)
{
    switch (kind)
    {
    //P2P 报到只接受 P2P 的两种角色
    case relay_pair_p2p:
        return IsP2PRole(role);

    //远程桌面报到：拒绝 P2P 的角色、群通话成员的角色（群通话只走房间中转）与服务端控制连接的角色，其余维持原来的行为（照旧接受）
    case relay_pair_rd:
        return !IsP2PRole(role) && role != RelayToken::role_groupcall_member && !IsServerRole(role);

    //音视频报到：同远程桌面
    case relay_pair_av:
        return !IsP2PRole(role) && role != RelayToken::role_groupcall_member && !IsServerRole(role);

    //不是一对一报到
    default:
        return false;
    }
}

bool RelayHelloPolicy::isForwardableData(int kind, uint16_t dataCmd)
{
    switch (kind)
    {
    //P2P 连接只转发 P2P 的数据帧
    case relay_pair_p2p:
        return dataCmd == cmd_p2p_relay_data;

    //远程桌面连接：维持原来的两种数据帧都转发，不转发 P2P 的数据帧
    case relay_pair_rd:
        return dataCmd == cmd_rd_relay_data || dataCmd == cmd_av_relay_data;

    //音视频连接：同远程桌面
    case relay_pair_av:
        return dataCmd == cmd_rd_relay_data || dataCmd == cmd_av_relay_data;

    //尚未报到：什么都不转发
    default:
        return false;
    }
}

bool RelayHelloPolicy::canAcceptPairHello(bool paired, bool inRoom)
{
    return !paired && !inRoom;
}
