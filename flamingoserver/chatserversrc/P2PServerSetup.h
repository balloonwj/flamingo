/**
 *  P2P 信令在 chatserver 里的接线辅助，P2PServerSetup.h
 *
 *  docs/P2P通信方案.md 第 2 期 2c-2a。把两件与网络无关、可以单独测试的事从 main.cpp 与 ChatSession 里拿出来：
 *    - P2PServerSetup::loadConfig：从 chatserver.conf 生成 P2PSessionManager 的配置（第 9.3 节的配置项）；
 *    - P2PFeatureGate：按「用户 → 所属企业 → 企业的 p2p 开关」实现 IP2PEnterpriseGate，供 P2PSignalService 查询。
 *  另有 appendPublishedHosts（2026-10-14）：按 loadConfig 的结果列出要下发给客户端的中转与探测地址，
 *  供启动时的地址检查（utils/ConfigHostCheck.h）使用，探测地址的回落规则因此只在 loadConfig 里写一份。
 *
 *  配置项（chatserver.conf）：
 *    relaytokenkey / imgserverhost / relayport   回落中转的令牌密钥、地址与端口，与远程桌面、通话共用，不另配
 *    p2pprobehost                                下发给客户端的地址探测服务器地址，不配时取 imgserverhost
 *    p2pprobeport1 / p2pprobeport2               两个 UDP 地址探测端口，默认 20004 / 20005（须与 imgserver 一致）；
 *                                                配成 0 表示不下发这个端口
 *    p2pconnecttimeoutms / p2pkeepalivems / p2plinktimeoutms   经 2302 下发的三个时间（毫秒），默认 3000 / 5000 / 15000
 *    p2pmaxperuser                               每人同时进行的 P2P 会话上限，默认 16
 *    p2pdebugopen                                为 1 时受理联调用的 2306，默认不受理
 *  数值项写成非数字、小于等于 0（端口另要求不超过 65535）时按默认值处理。
 *
 *  典型用法（main.cpp 与 ChatSession）：
 *      Singleton<P2PSessionManager>::Instance().configure(P2PServerSetup::loadConfig(config));
 *      P2PFeatureGate gate(Singleton<FeatureSwitchManager>::Instance(), LookupUserEnterprise);
 *      P2PSignalService svc(Singleton<P2PSessionManager>::Instance(), gate);
 *
 *  单元测试见 flamingoserver/tests/test_p2pserversetup.cpp（Suite P2PCF）。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <vector>

#include "P2PSessionManager.h"
#include "P2PSignalService.h"
#include "../utils/ConfigHostCheck.h"   // PublishedHost：appendPublishedHosts 的输出

class CConfigFileReader;
class FeatureSwitchManager;

/**
 *  P2P 配置的读取。只有静态函数，不需要实例。
 */
class P2PServerSetup
{
public:
    //地址探测端口的默认值（方案第 9.3 节，暂定）；imgserver 的 p2pprobeport1 / p2pprobeport2 默认值须与之相同
    static const int kDefaultProbePort1 = 20004;
    static const int kDefaultProbePort2 = 20005;

    /**
     *  从配置文件生成会话管理器的配置。
     *  @param config 已加载的配置文件（借用；getConfigName 不是 const 函数，故传非 const 引用）
     *  @return 会话管理器配置；缺项与非法值按文件头的规则取默认值，探测主机为空时不下发探测地址
     */
    static P2PSessionConfig loadConfig(CConfigFileReader& config);

    /**
     *  列出这份配置要下发给客户端的地址，追加到 out 末尾：
     *    - 中转：m_relayHost + m_relayPort，配置项 imgserverhost。通话、远程桌面、屏幕共享、群通话与 P2P 回落
     *      共用这一组（各模块都从 imgserverhost / relayport 读取，见 main.cpp），所以一并写进用途；
     *    - 地址探测：m_probes 里的每个地址；探测主机来自 p2pprobehost，不配时来自 imgserverhost。
     *  端口为 0、主机为空的项照样追加，是否下发由 ConfigHostCheck::findLocalOnly 判断。
     *  @param cfg                 loadConfig 的结果（P2PSessionManager 实际使用的配置）
     *  @param probeHostConfigured 配置里是否写了非空的 p2pprobehost；决定探测地址在告警里记为哪个配置项
     *  @param out                 [出参] 追加的地址
     */
    static void appendPublishedHosts(const P2PSessionConfig& cfg, bool probeHostConfigured,
                                     std::vector<PublishedHost>& out);
};

/**
 *  按企业开关实现的 IP2PEnterpriseGate：查出用户所属企业，再查该企业的 p2p 开关。
 *  「用户 → 企业」的查询以函数指针注入：chatserver 里接 DepartmentManager，测试里接固定的表。
 */
class P2PFeatureGate : public IP2PEnterpriseGate
{
public:
    //查询某个用户所属的企业 id；不属于任何企业时返回 0
    typedef int64_t (*EnterpriseLookup)(int64_t userId);

    /**
     *  @param switches 功能开关（借用，不持有；须比本对象活得久）
     *  @param lookup   「用户 → 企业」的查询函数，不得为 NULL
     */
    P2PFeatureGate(FeatureSwitchManager& switches, EnterpriseLookup lookup);

    /**
     *  查询某个用户所在企业是否开启了 P2P。
     *  @param userId 用户 userid
     *  @return 所属企业开启时返回 true；不属于任何企业或企业关闭时返回 false
     */
    virtual bool isP2PEnabledForUser(int64_t userId) override;

private:
    FeatureSwitchManager& m_switches;   //功能开关（借用）
    EnterpriseLookup      m_lookup;     //「用户 → 企业」的查询函数
};
