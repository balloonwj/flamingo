/**
 *  P2P 信令在 chatserver 里的接线辅助实现，见 P2PServerSetup.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "P2PServerSetup.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string>

#include "../base/ConfigFileReader.h"
#include "FeatureSwitchManager.h"

//类内静态常量的类外定义：g++ 在 ODR 使用（如绑定到 const 引用）时需要
const int P2PServerSetup::kDefaultProbePort1;
const int P2PServerSetup::kDefaultProbePort2;

namespace {

//chatserver.conf 里的配置项名（含义见 P2PServerSetup.h 文件头）
const char* const kKeyRelayTokenKey = "relaytokenkey";
const char* const kKeyImgServerHost = "imgserverhost";
const char* const kKeyRelayPort = "relayport";
const char* const kKeyProbeHost = "p2pprobehost";
const char* const kKeyProbePort1 = "p2pprobeport1";
const char* const kKeyProbePort2 = "p2pprobeport2";
const char* const kKeyConnectTimeout = "p2pconnecttimeoutms";
const char* const kKeyKeepalive = "p2pkeepalivems";
const char* const kKeyLinkTimeout = "p2plinktimeoutms";
const char* const kKeyMaxPerUser = "p2pmaxperuser";
const char* const kKeyDebugOpen = "p2pdebugopen";

//p2pdebugopen 取这个值时受理 2306，其余取值一律不受理
const char* const kDebugOpenOn = "1";

//UDP / TCP 端口的上限
const long long kMaxPort = 65535;

//取字符串配置项；缺项返回空串
std::string ReadText(CConfigFileReader& config, const char* key)
{
    const char* value = config.getConfigName(key);
    return value != NULL ? std::string(value) : std::string();
}

//把整段文本解析成十进制整数；为空、带非数字字符或超出 long long 范围时返回 false
bool ParseInteger(const std::string& text, long long& value)
{
    if (text.empty())
        return false;
    errno = 0;
    char* end = NULL;
    value = strtoll(text.c_str(), &end, 10);
    return errno == 0 && end != NULL && *end == '\0';
}

//取正整数配置项：缺项、非数字、小于等于 0 或大于 maxValue 时返回 defaultValue
int ReadPositive(CConfigFileReader& config, const char* key, int defaultValue, long long maxValue)
{
    long long value = 0;
    if (!ParseInteger(ReadText(config, key), value) || value <= 0 || value > maxValue)
        return defaultValue;
    return static_cast<int>(value);
}

//取地址探测端口：配成 0 时返回 0（表示不下发这个端口）；缺项、非数字、为负或超过 65535 时返回 defaultValue
int ReadProbePort(CConfigFileReader& config, const char* key, int defaultValue)
{
    long long value = 0;
    if (!ParseInteger(ReadText(config, key), value) || value < 0 || value > kMaxPort)
        return defaultValue;
    return static_cast<int>(value);
}

} // namespace

P2PSessionConfig P2PServerSetup::loadConfig(CConfigFileReader& config)
{
    const long long maxInt = INT_MAX;

    //默认构造已经填好方案第 6.5 节的三个时间与会话上限，非法或缺项时保留这些值
    P2PSessionConfig cfg;
    cfg.m_relayTokenKey = ReadText(config, kKeyRelayTokenKey);
    cfg.m_relayHost = ReadText(config, kKeyImgServerHost);
    cfg.m_relayPort = ReadPositive(config, kKeyRelayPort, 0, kMaxPort);
    cfg.m_connectTimeoutMs = ReadPositive(config, kKeyConnectTimeout, cfg.m_connectTimeoutMs, maxInt);
    cfg.m_keepaliveMs = ReadPositive(config, kKeyKeepalive, cfg.m_keepaliveMs, maxInt);
    cfg.m_linkTimeoutMs = ReadPositive(config, kKeyLinkTimeout, cfg.m_linkTimeoutMs, maxInt);
    cfg.m_maxPerUser = ReadPositive(config, kKeyMaxPerUser, cfg.m_maxPerUser, maxInt);
    cfg.m_debugOpen = (ReadText(config, kKeyDebugOpen) == kDebugOpenOn);

    //地址探测服务器：主机不配时取 imgserverhost（探测端口在 imgserver 上）；主机为空时客户端无处可探测，不下发
    std::string probeHost = ReadText(config, kKeyProbeHost);
    if (probeHost.empty())
        probeHost = cfg.m_relayHost;
    if (probeHost.empty())
        return cfg;
    const int ports[] =
    {
        ReadProbePort(config, kKeyProbePort1, kDefaultProbePort1),
        ReadProbePort(config, kKeyProbePort2, kDefaultProbePort2),
    };
    const size_t portCount = sizeof(ports) / sizeof(ports[0]);
    for (size_t i = 0; i < portCount; ++i)
    {
        //端口配成 0：不下发这个端口
        if (ports[i] <= 0)
            continue;
        P2PProbeAddress probe;
        probe.m_ip = probeHost;
        probe.m_port = ports[i];
        cfg.m_probes.push_back(probe);
    }
    return cfg;
}

P2PFeatureGate::P2PFeatureGate(FeatureSwitchManager& switches, EnterpriseLookup lookup)
    : m_switches(switches)
    , m_lookup(lookup)
{
}

bool P2PFeatureGate::isP2PEnabledForUser(int64_t userId)
{
    //查不到企业（企业 id 为 0）时 isEnabled 恒为 false，见 FeatureSwitchManager::resolveEnabled
    const int64_t enterpriseId = (m_lookup != NULL) ? m_lookup(userId) : 0;
    return m_switches.isEnabled(enterpriseId, kFeatureP2P);
}
