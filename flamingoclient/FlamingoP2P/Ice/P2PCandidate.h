/**
 *  P2P 候选地址（docs/P2P通信方案.md 6.1 节，第 3 期 3b）：本机网卡地址的筛选与优先级、2303 里候选地址数组的编解码。
 *
 *  - 本机地址（host）：处于连接状态的网卡上的 IPv4 地址，排除回环（127/8）、链路本地（169.254/16）与 0.0.0.0；
 *    明显的虚拟网卡（Hyper-V、VMware、VirtualBox、WSL、Docker 等）降低优先级而不是排除，因为有的企业 VPN 也表现为
 *    虚拟网卡。网卡列表由 IO 层用 GetAdaptersAddresses 填写（第 3d 步），这里只做纯逻辑，单测用假数据。
 *  - 公网映射地址（srflx）：地址探测服务器看到的来源地址，见 P2PAddressProber。
 *  - 优先级沿用 ICE（RFC 8445 5.1.2.1）的形式：类型偏好 × 2^24 + 本地偏好 × 2^8 + (256 - 组件号)，组件号固定为 1。
 *    本机地址的类型偏好高于公网映射地址；同网段优先由连通性检查在配对时再加权（第 3c 步）。
 *  - 2303 的 data.candidates 是 [{ "type": 1, "ip": "192.168.10.2", "port": 54429, "priority": … }]
 *    （docs/协议设计-v2.md 18.3）；解码时跳过不合法的条目。
 *
 *  只依赖标准库与 jsoncpp，不碰 socket。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace Json { class Value; }

namespace p2p {

//候选地址的类型，与 2303 data.candidates[].type 一致
enum p2p_candidate_type
{
    p2p_candidate_host  = 1,   //本机网卡地址
    p2p_candidate_srflx = 2,   //地址探测服务器看到的公网映射地址
};

//一个候选地址
struct P2PCandidate
{
    int      m_type;        //类型，取值见 p2p_candidate_type
    uint32_t m_ip;          //IPv4 地址（主机字节序）
    uint16_t m_port;        //UDP 端口（主机字节序）
    uint32_t m_priority;    //优先级，越大越优先
    uint8_t  m_prefixLen;   //本机地址所在网段的前缀长度（只对本方的 host 候选有意义，不发给对方）；未知时为 0

    P2PCandidate()
        : m_type(0)
        , m_ip(0)
        , m_port(0)
        , m_priority(0)
        , m_prefixLen(0)
    {
    }
};

//一块网卡上的一个 IPv4 地址
struct P2PAdapterAddress
{
    uint32_t m_ip;          //IPv4 地址（主机字节序）
    uint8_t  m_prefixLen;   //网段前缀长度（0～32）

    P2PAdapterAddress()
        : m_ip(0)
        , m_prefixLen(0)
    {
    }
};

//一块网卡（IO 层用 GetAdaptersAddresses 填写，单测用假数据）
struct P2PAdapterInfo
{
    std::string                    m_name;          //友好名称（UTF-8），只用于日志
    std::string                    m_description;   //描述（UTF-8），用于识别虚拟网卡
    bool                           m_up;            //是否处于连接状态
    bool                           m_loopback;      //是否为回环网卡
    std::vector<P2PAdapterAddress> m_addresses;     //IPv4 地址

    P2PAdapterInfo()
        : m_up(false)
        , m_loopback(false)
    {
    }
};

//候选地址的类型偏好（ICE 优先级的最高 8 位）
const uint32_t kP2PTypePrefHost = 126;
const uint32_t kP2PTypePrefSrflx = 100;

//本地偏好：普通网卡与虚拟网卡
const uint16_t kP2PLocalPrefPhysical = 65535;
const uint16_t kP2PLocalPrefVirtual = 30000;

//一次最多发给对方的候选地址个数：网卡很多时只取优先级最高的这些，避免 2303 超长、探测包太多
const size_t kP2PMaxCandidates = 8;

/**
 *  地址是否为回环地址（127.0.0.0/8）。
 *  @param ip IPv4 地址（主机字节序）
 */
bool IsLoopbackIp(uint32_t ip);

/**
 *  地址是否为链路本地地址（169.254.0.0/16，网卡没有拿到 DHCP 地址时的自动地址）。
 *  @param ip IPv4 地址（主机字节序）
 */
bool IsLinkLocalIp(uint32_t ip);

/**
 *  按描述判断是否为明显的虚拟网卡（大小写不敏感地匹配 Hyper-V、VMware、VirtualBox、vEthernet、WSL、Docker 等关键词）。
 *  @param adapter 网卡
 *  @return 是虚拟网卡返回 true
 */
bool IsVirtualAdapter(const P2PAdapterInfo& adapter);

/**
 *  计算候选地址的优先级。
 *  @param typePref  类型偏好（kP2PTypePrefHost / kP2PTypePrefSrflx）
 *  @param localPref 本地偏好（0～65535）
 *  @return 优先级
 */
uint32_t CandidatePriority(uint32_t typePref, uint16_t localPref);

/**
 *  由网卡列表得到本机候选地址：排除未连接的网卡、回环网卡与回环 / 链路本地 / 全零地址，虚拟网卡降低优先级；
 *  同一个 IP 只保留一份（取优先级高的）；按优先级从高到低排列，最多 kP2PMaxCandidates 个。
 *  @param adapters 网卡列表
 *  @param port     本链路 UDP socket 绑定的端口（主机字节序）
 *  @return 本机候选地址
 */
std::vector<P2PCandidate> BuildHostCandidates(const std::vector<P2PAdapterInfo>& adapters, uint16_t port);

/**
 *  IPv4 地址与点分十进制文本互转（不依赖 winsock）。
 *  @param ip   IPv4 地址（主机字节序）
 *  @param text 点分十进制文本；FromString 只接受恰好四段、每段 0～255 的文本
 */
std::string IpToString(uint32_t ip);
bool IpFromString(const std::string& text, uint32_t& ip);

/**
 *  把候选地址列表写成 2303 data.candidates 的 JSON 数组（不写 m_prefixLen）。
 *  @param candidates 候选地址
 *  @param out        [出参] JSON 数组（先清空）
 */
void CandidatesToJson(const std::vector<P2PCandidate>& candidates, Json::Value& out);

/**
 *  从 2303 data.candidates 读出候选地址：跳过类型不认识、地址非法、端口为 0 的条目，最多读 kP2PMaxCandidates 个。
 *  @param arr JSON 数组；不是数组时什么都不读
 *  @param out [出参] 候选地址（先清空）
 *  @return 读出的个数
 */
size_t CandidatesFromJson(const Json::Value& arr, std::vector<P2PCandidate>& out);

} // namespace p2p
