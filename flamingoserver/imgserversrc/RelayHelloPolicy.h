/**
 *  imgserver 一对一中转的「报到与转发」判断，RelayHelloPolicy.h
 *
 *  docs/P2P通信方案.md 第 2 期 2f-1。imgserver 的一对一配对表（RelayHub）同时承载远程桌面（1853 / 1854，角色 1 / 2）、
 *  音视频（1855 / 1856，角色 3 / 4）与 P2P 回落中转（1861 / 1862，角色 11 / 12），配对表本身只要求同一会话两端角色不同。
 *  这里给出三项判断，由 RelaySession 在报到与转发时调用：
 *    - 报到命令与令牌角色是否相符：P2P 报到只接受 11 / 12；远程桌面与音视频的报到不接受 11 / 12，其余角色维持
 *      原来的「照单全收」（收紧它们不在本期范围）；
 *    - 某种报到之后的连接能转发哪些数据命令：P2P 只转发 1862；远程桌面与音视频维持原来的 1854 / 1856 都转发，
 *      但不转发 1862；
 *    - 当前状态能否接受一对一报到：已配对、或已在屏幕共享房间里的连接都不能（原先 onHello 只判断了已配对，
 *      房间连接还能再加入配对表，断开时只退出房间，在配对表里留下残留表项）。
 *  纯函数，单元测试见 tests/test_relayhellopolicy.cpp（Suite IRR）。
 *
 *  典型用法（RelaySession::onHello）：
 *      const int kind = RelayHelloPolicy::pairKindOfHello(helloCmd);
 *      if (!RelayHelloPolicy::isAcceptedPairRole(kind, payload.role))
 *          ...回报文非法并断开
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>

//一对一中转报到的种类，由报到命令决定
enum relay_pair_kind
{
    relay_pair_none = 0,   //不是一对一报到命令（或连接尚未报到）
    relay_pair_rd   = 1,   //远程桌面：cmd_rd_relay_hello 报到
    relay_pair_av   = 2,   //音视频：cmd_av_relay_hello 报到
    relay_pair_p2p  = 3,   //P2P 回落中转：cmd_p2p_relay_hello 报到
};

/**
 *  一对一中转的报到与转发判断。只有静态函数，不需要实例。
 */
class RelayHelloPolicy
{
public:
    /**
     *  报到命令对应的种类。
     *  @param helloCmd 报到帧的命令号
     *  @return relay_pair_kind；不是一对一报到命令时为 relay_pair_none
     */
    static int pairKindOfHello(uint16_t helloCmd);

    /**
     *  令牌角色能否用于这种报到。
     *  @param kind 报到种类（relay_pair_kind）
     *  @param role 令牌里的角色，取值见 RelayToken::role_*
     *  @return P2P 报到时角色为 11 / 12 返回 true；远程桌面与音视频报到时角色既不是 11 / 12、也不是群通话成员 13 时返回 true；
     *          其余返回 false
     */
    static bool isAcceptedPairRole(int kind, int role);

    /**
     *  以某种方式报到的连接能否转发这条数据命令。
     *  @param kind    该连接报到时的种类（relay_pair_kind）
     *  @param dataCmd 数据帧的命令号
     *  @return P2P 连接只能转发 cmd_p2p_relay_data；远程桌面与音视频连接能转发 cmd_rd_relay_data 与 cmd_av_relay_data
     */
    static bool isForwardableData(int kind, uint16_t dataCmd);

    /**
     *  当前状态能否接受一对一报到。
     *  @param paired 是否已经完成过一对一报到
     *  @param inRoom 是否已经进了屏幕共享房间
     *  @return 两者都不是时返回 true
     */
    static bool canAcceptPairHello(bool paired, bool inRoom);
};
