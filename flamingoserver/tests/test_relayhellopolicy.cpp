/**
 *  imgserver 一对一中转「报到与转发」判断的单元测试（Suite IRR），test_relayhellopolicy.cpp
 *
 *  覆盖 docs/P2P通信方案.md 第 2 期 2f-1：
 *    - IRR1 报到命令与角色：1861（P2P）只接受角色 11 / 12；1853（远程桌面）与 1855（音视频）不接受 11 / 12，
 *           其余角色维持现状（照旧接受）；不是一对一报到的命令一律不接受；
 *    - IRR2 数据命令与报到种类：用 1861 报到的连接只转发 1862；用 1853 / 1855 报到的连接转发 1854 / 1856、
 *           不转发 1862；尚未报到时什么都不转发；
 *    - IRR3 报到时的状态：已配对或已在屏幕共享房间里的连接，不能再做一对一报到（修正 onHello 只判断已配对的问题）。
 *
 *  典型用法：Tests.exe --test-case="IRR*"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "doctest.h"
#include "../chatserversrc/Protocol.h"
#include "../imgserversrc/RelayHelloPolicy.h"
#include "../utils/RelayToken.h"

TEST_CASE("IRR1 报到命令与角色")
{
    const int p2p = RelayHelloPolicy::pairKindOfHello(cmd_p2p_relay_hello);
    const int rd = RelayHelloPolicy::pairKindOfHello(cmd_rd_relay_hello);
    const int av = RelayHelloPolicy::pairKindOfHello(cmd_av_relay_hello);
    CHECK(p2p == relay_pair_p2p);
    CHECK(rd == relay_pair_rd);
    CHECK(av == relay_pair_av);
    //屏幕共享房间报到与数据命令都不是一对一报到
    CHECK(RelayHelloPolicy::pairKindOfHello(cmd_screenshare_relay_hello) == relay_pair_none);
    CHECK(RelayHelloPolicy::pairKindOfHello(cmd_p2p_relay_data) == relay_pair_none);

    //P2P 报到：只接受 11 / 12
    CHECK(RelayHelloPolicy::isAcceptedPairRole(p2p, RelayToken::role_p2p_controlling));
    CHECK(RelayHelloPolicy::isAcceptedPairRole(p2p, RelayToken::role_p2p_controlled));
    CHECK_FALSE(RelayHelloPolicy::isAcceptedPairRole(p2p, RelayToken::role_controller));
    CHECK_FALSE(RelayHelloPolicy::isAcceptedPairRole(p2p, RelayToken::role_caller));
    CHECK_FALSE(RelayHelloPolicy::isAcceptedPairRole(p2p, RelayToken::role_screenshare_member));

    //远程桌面与音视频报到：不接受 P2P 的角色，其余照旧
    CHECK_FALSE(RelayHelloPolicy::isAcceptedPairRole(rd, RelayToken::role_p2p_controlling));
    CHECK_FALSE(RelayHelloPolicy::isAcceptedPairRole(rd, RelayToken::role_p2p_controlled));
    CHECK_FALSE(RelayHelloPolicy::isAcceptedPairRole(av, RelayToken::role_p2p_controlling));
    CHECK_FALSE(RelayHelloPolicy::isAcceptedPairRole(av, RelayToken::role_p2p_controlled));
    CHECK(RelayHelloPolicy::isAcceptedPairRole(rd, RelayToken::role_controller));
    CHECK(RelayHelloPolicy::isAcceptedPairRole(rd, RelayToken::role_controlled));
    CHECK(RelayHelloPolicy::isAcceptedPairRole(av, RelayToken::role_caller));
    CHECK(RelayHelloPolicy::isAcceptedPairRole(av, RelayToken::role_callee));
    //云端录制控制连接的角色（第 26 期）：三种一对一报到都不接受，它也不是一对一报到命令
    CHECK_FALSE(RelayHelloPolicy::isAcceptedPairRole(rd, RelayToken::role_server_control));
    CHECK_FALSE(RelayHelloPolicy::isAcceptedPairRole(av, RelayToken::role_server_control));
    CHECK_FALSE(RelayHelloPolicy::isAcceptedPairRole(p2p, RelayToken::role_server_control));
    CHECK(RelayHelloPolicy::pairKindOfHello(cmd_cloudrec_ctl_hello) == relay_pair_none);

    //不是一对一报到
    CHECK_FALSE(RelayHelloPolicy::isAcceptedPairRole(relay_pair_none, RelayToken::role_p2p_controlling));
    CHECK_FALSE(RelayHelloPolicy::isAcceptedPairRole(relay_pair_none, RelayToken::role_controller));
}

TEST_CASE("IRR2 数据命令与报到种类")
{
    //P2P：只转发 1862
    CHECK(RelayHelloPolicy::isForwardableData(relay_pair_p2p, cmd_p2p_relay_data));
    CHECK_FALSE(RelayHelloPolicy::isForwardableData(relay_pair_p2p, cmd_rd_relay_data));
    CHECK_FALSE(RelayHelloPolicy::isForwardableData(relay_pair_p2p, cmd_av_relay_data));
    //远程桌面与音视频：现状是两种数据命令都转发，维持；不转发 1862
    CHECK(RelayHelloPolicy::isForwardableData(relay_pair_rd, cmd_rd_relay_data));
    CHECK(RelayHelloPolicy::isForwardableData(relay_pair_rd, cmd_av_relay_data));
    CHECK(RelayHelloPolicy::isForwardableData(relay_pair_av, cmd_av_relay_data));
    CHECK(RelayHelloPolicy::isForwardableData(relay_pair_av, cmd_rd_relay_data));
    CHECK_FALSE(RelayHelloPolicy::isForwardableData(relay_pair_rd, cmd_p2p_relay_data));
    CHECK_FALSE(RelayHelloPolicy::isForwardableData(relay_pair_av, cmd_p2p_relay_data));
    //尚未报到
    CHECK_FALSE(RelayHelloPolicy::isForwardableData(relay_pair_none, cmd_p2p_relay_data));
    CHECK_FALSE(RelayHelloPolicy::isForwardableData(relay_pair_none, cmd_rd_relay_data));
}

TEST_CASE("IRR3 已配对或已在房间里的连接不能再做一对一报到")
{
    CHECK(RelayHelloPolicy::canAcceptPairHello(false, false));
    CHECK_FALSE(RelayHelloPolicy::canAcceptPairHello(true, false));
    CHECK_FALSE(RelayHelloPolicy::canAcceptPairHello(false, true));
    CHECK_FALSE(RelayHelloPolicy::canAcceptPairHello(true, true));
}
