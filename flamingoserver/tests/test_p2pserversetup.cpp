/**
 *  P2P 服务端接线辅助的单元测试（Suite P2PCF），test_p2pserversetup.cpp
 *
 *  覆盖 docs/P2P通信方案.md 第 2 期 2c-2a：
 *    - P2PCF1 没有任何 P2P 配置项：令牌密钥、中转地址与端口沿用 relaytokenkey / imgserverhost / relayport，
 *             三个时间与会话上限取默认值，联调建会话关闭，探测地址为 imgserverhost 的 20004 与 20005 端口；
 *    - P2PCF2 各项都配置时按配置取值；p2pprobehost 不配时取 imgserverhost；探测端口配 0 时不下发这个端口；
 *    - P2PCF3 非法值（非数字、小于等于 0、端口超出范围）回落默认值；空配置文件时中转参数为空、不下发探测地址；
 *    - P2PCF4 开关查询适配器 P2PFeatureGate：用户所在企业开启为真、关闭为假、不属于任何企业为假，
 *             开关在运行中改变时立即生效。
 *  配置文件真的写到当前目录再由 CConfigFileReader 读取，文件名带 flamingo_p2pcf 前缀，读完即删除。
 *
 *  典型用法：Tests.exe --test-case="P2PCF*"
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include <stdint.h>
#include <stdio.h>
#include <fstream>
#include <string>

#include "doctest.h"
#include "../base/ConfigFileReader.h"
#include "../chatserversrc/FeatureSwitchManager.h"
#include "../chatserversrc/P2PServerSetup.h"

namespace {

//本套用例专用的临时配置文件，写在当前目录，读完即删除
const char* const kConfPath = "flamingo_p2pcf_test.conf";

//各用例共用的中转配置：令牌密钥、imgserver 地址与中转端口
const char* const kBaseConf =
    "relaytokenkey=p2pcf-secret\n"
    "imgserverhost=10.0.0.5\n"
    "relayport=20003\n";
const char* const kRelayHost = "10.0.0.5";
const int kRelayPort = 20003;

//方案第 6.5 节与第 9.3 节给出的默认值
const int kDefaultConnectMs = 3000;     //打洞超时（毫秒）
const int kDefaultKeepaliveMs = 5000;   //保活间隔（毫秒）
const int kDefaultLinkMs = 15000;       //链路断开判定时间（毫秒）
const int kDefaultMaxPerUser = 16;      //每人同时进行的会话上限
const int kDefaultProbePort1 = 20004;   //第一个地址探测端口
const int kDefaultProbePort2 = 20005;   //第二个地址探测端口

//P2PCF2 里配置的各项取值
const char* const kProbeHost = "203.0.113.7";
const int kProbePort1 = 30004;
const int kProbePort2 = 30005;
const int kConnectMs = 2500;
const int kKeepaliveMs = 4000;
const int kLinkMs = 20000;
const int kMaxPerUser = 8;

//P2PCF4 的企业与用户：A 企业开启、B 企业关闭，另有一个不属于任何企业的用户
const int64_t kEnterpriseA = 7;
const int64_t kEnterpriseB = 8;
const int64_t kUserInA = 5001;
const int64_t kUserInB = 5002;
const int64_t kUserNoEnterprise = 5003;

//把 content 写成配置文件、用 CConfigFileReader 读出，再交给 P2PServerSetup::loadConfig
P2PSessionConfig LoadFrom(const std::string& content)
{
    {
        std::ofstream out(kConfPath, std::ios::binary | std::ios::trunc);
        REQUIRE(out.is_open());
        out << content;
    }
    CConfigFileReader reader(kConfPath);
    const bool loaded = reader.isLoadOk();
    P2PSessionConfig cfg;
    if (loaded)
        cfg = P2PServerSetup::loadConfig(reader);
    remove(kConfPath);
    REQUIRE(loaded);
    return cfg;
}

//测试用的「用户 → 企业」查询
int64_t FakeEnterpriseOf(int64_t userId)
{
    if (userId == kUserInA)
        return kEnterpriseA;
    if (userId == kUserInB)
        return kEnterpriseB;
    return 0;
}

} // anonymous namespace

TEST_CASE("P2PCF1 没有任何 P2P 配置项时的取值")
{
    const P2PSessionConfig cfg = LoadFrom(kBaseConf);
    //中转参数沿用远程桌面与通话已有的三项配置
    CHECK(cfg.m_relayTokenKey == "p2pcf-secret");
    CHECK(cfg.m_relayHost == kRelayHost);
    CHECK(cfg.m_relayPort == kRelayPort);
    //时间与上限取默认值，联调建会话默认关闭
    CHECK(cfg.m_connectTimeoutMs == kDefaultConnectMs);
    CHECK(cfg.m_keepaliveMs == kDefaultKeepaliveMs);
    CHECK(cfg.m_linkTimeoutMs == kDefaultLinkMs);
    CHECK(cfg.m_maxPerUser == kDefaultMaxPerUser);
    CHECK_FALSE(cfg.m_debugOpen);
    //探测地址：主机取 imgserverhost，两个默认端口
    REQUIRE(cfg.m_probes.size() == 2);
    CHECK(cfg.m_probes[0].m_ip == kRelayHost);
    CHECK(cfg.m_probes[0].m_port == kDefaultProbePort1);
    CHECK(cfg.m_probes[1].m_ip == kRelayHost);
    CHECK(cfg.m_probes[1].m_port == kDefaultProbePort2);
}

TEST_CASE("P2PCF2 各项都配置时按配置取值")
{
    const std::string full = std::string(kBaseConf) +
        "p2pprobehost=203.0.113.7\n"
        "p2pprobeport1=30004\n"
        "p2pprobeport2=30005\n"
        "p2pconnecttimeoutms=2500\n"
        "p2pkeepalivems=4000\n"
        "p2plinktimeoutms=20000\n"
        "p2pmaxperuser=8\n"
        "p2pdebugopen=1\n";
    const P2PSessionConfig cfg = LoadFrom(full);
    CHECK(cfg.m_connectTimeoutMs == kConnectMs);
    CHECK(cfg.m_keepaliveMs == kKeepaliveMs);
    CHECK(cfg.m_linkTimeoutMs == kLinkMs);
    CHECK(cfg.m_maxPerUser == kMaxPerUser);
    CHECK(cfg.m_debugOpen);
    REQUIRE(cfg.m_probes.size() == 2);
    CHECK(cfg.m_probes[0].m_ip == kProbeHost);
    CHECK(cfg.m_probes[0].m_port == kProbePort1);
    CHECK(cfg.m_probes[1].m_ip == kProbeHost);
    CHECK(cfg.m_probes[1].m_port == kProbePort2);

    //探测端口配 0：这个端口不下发；两个都配 0 时不下发探测地址
    const P2PSessionConfig one = LoadFrom(std::string(kBaseConf) + "p2pprobeport1=0\np2pprobeport2=30005\n");
    REQUIRE(one.m_probes.size() == 1);
    CHECK(one.m_probes[0].m_ip == kRelayHost);
    CHECK(one.m_probes[0].m_port == kProbePort2);
    const P2PSessionConfig none = LoadFrom(std::string(kBaseConf) + "p2pprobeport1=0\np2pprobeport2=0\n");
    CHECK(none.m_probes.empty());
}

TEST_CASE("P2PCF3 非法值回落默认值")
{
    const std::string bad = std::string(kBaseConf) +
        "p2pconnecttimeoutms=abc\n"
        "p2pkeepalivems=0\n"
        "p2plinktimeoutms=-5\n"
        "p2pmaxperuser=12x\n"
        "p2pprobeport1=70000\n"
        "p2pprobeport2=-1\n"
        "p2pdebugopen=yes\n";
    const P2PSessionConfig cfg = LoadFrom(bad);
    CHECK(cfg.m_connectTimeoutMs == kDefaultConnectMs);
    CHECK(cfg.m_keepaliveMs == kDefaultKeepaliveMs);
    CHECK(cfg.m_linkTimeoutMs == kDefaultLinkMs);
    CHECK(cfg.m_maxPerUser == kDefaultMaxPerUser);
    CHECK_FALSE(cfg.m_debugOpen);
    REQUIRE(cfg.m_probes.size() == 2);
    CHECK(cfg.m_probes[0].m_port == kDefaultProbePort1);
    CHECK(cfg.m_probes[1].m_port == kDefaultProbePort2);

    //空配置文件：中转参数为空，探测主机为空时不下发探测地址
    const P2PSessionConfig empty = LoadFrom("#empty\n");
    CHECK(empty.m_relayTokenKey.empty());
    CHECK(empty.m_relayHost.empty());
    CHECK(empty.m_relayPort == 0);
    CHECK(empty.m_probes.empty());
}

TEST_CASE("P2PCF4 开关查询适配器")
{
    FeatureSwitchManager switches;
    switches.setMemoryValue(kEnterpriseB, kSettingP2PEnabled, "0");
    P2PFeatureGate gate(switches, FakeEnterpriseOf);
    //A 企业未设置（默认开启）、B 企业关闭、不属于任何企业恒为关闭
    CHECK(gate.isP2PEnabledForUser(kUserInA));
    CHECK_FALSE(gate.isP2PEnabledForUser(kUserInB));
    CHECK_FALSE(gate.isP2PEnabledForUser(kUserNoEnterprise));

    //开关在运行中改变：同一个适配器立即按新值判断
    switches.setMemoryValue(kEnterpriseB, kSettingP2PEnabled, "1");
    CHECK(gate.isP2PEnabledForUser(kUserInB));
    switches.setMemoryValue(kEnterpriseA, kSettingP2PEnabled, "0");
    CHECK_FALSE(gate.isP2PEnabledForUser(kUserInA));
}
