/**
 *  P2P 候选地址的实现，见 P2PCandidate.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "P2PCandidate.h"

#include <ctype.h>
#include <stdlib.h>
#include <algorithm>

#include "../../Source/third-party/jsoncpp-1.9.0/json.h"

namespace p2p {

namespace {

//回环地址 127.0.0.0/8 与链路本地地址 169.254.0.0/16 的网络号及掩码（主机字节序）
const uint32_t kLoopbackNet = 0x7F000000;
const uint32_t kLoopbackMask = 0xFF000000;
const uint32_t kLinkLocalNet = 0xA9FE0000;
const uint32_t kLinkLocalMask = 0xFFFF0000;

//ICE 优先级的组成：类型偏好左移 24 位、本地偏好左移 8 位，再加 (256 - 组件号)，组件号固定为 1
const int kTypePrefShift = 24;
const int kLocalPrefShift = 8;
const uint32_t kComponentTerm = 256 - 1;

//识别虚拟网卡的关键词（小写），在描述与友好名称里查找
const char* const kVirtualKeywords[] =
{
    "hyper-v", "vethernet", "vmware", "virtualbox", "wsl", "docker", "virtual adapter", "tap-windows",
};

//IPv4 点分十进制的段数、每段的最大值与一个字节的位数
const int kIpv4Parts = 4;
const int kMaxPartValue = 255;
const int kBitsPerByte = 8;
const int kMaxPartDigits = 3;
const int kDecimalBase = 10;

//候选地址 JSON 里端口的取值上限
const int kMaxPort = 65535;

//转成小写（只处理 ASCII）
std::string ToLower(const std::string& s)
{
    std::string out(s);
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = (char)tolower((unsigned char)out[i]);
    return out;
}

//按优先级从高到低排序的比较函数（stable_sort 保持优先级相同者的原有顺序）
bool HigherPriority(const P2PCandidate& a, const P2PCandidate& b)
{
    return a.m_priority > b.m_priority;
}

} // namespace

bool IsLoopbackIp(uint32_t ip)
{
    return (ip & kLoopbackMask) == kLoopbackNet;
}

bool IsLinkLocalIp(uint32_t ip)
{
    return (ip & kLinkLocalMask) == kLinkLocalNet;
}

bool IsVirtualAdapter(const P2PAdapterInfo& adapter)
{
    const std::string text = ToLower(adapter.m_description + " " + adapter.m_name);
    for (size_t i = 0; i < sizeof(kVirtualKeywords) / sizeof(kVirtualKeywords[0]); ++i)
    {
        if (text.find(kVirtualKeywords[i]) != std::string::npos)
            return true;
    }
    return false;
}

uint32_t CandidatePriority(uint32_t typePref, uint16_t localPref)
{
    return (typePref << kTypePrefShift) + ((uint32_t)localPref << kLocalPrefShift) + kComponentTerm;
}

std::vector<P2PCandidate> BuildHostCandidates(const std::vector<P2PAdapterInfo>& adapters, uint16_t port)
{
    std::vector<P2PCandidate> out;
    for (size_t i = 0; i < adapters.size(); ++i)
    {
        const P2PAdapterInfo& a = adapters[i];
        if (!a.m_up || a.m_loopback)
            continue;
        const uint16_t localPref = IsVirtualAdapter(a) ? kP2PLocalPrefVirtual : kP2PLocalPrefPhysical;
        for (size_t j = 0; j < a.m_addresses.size(); ++j)
        {
            const uint32_t ip = a.m_addresses[j].m_ip;
            if (ip == 0 || IsLoopbackIp(ip) || IsLinkLocalIp(ip))
                continue;
            P2PCandidate c;
            c.m_type = p2p_candidate_host;
            c.m_ip = ip;
            c.m_port = port;
            c.m_priority = CandidatePriority(kP2PTypePrefHost, localPref);
            c.m_prefixLen = a.m_addresses[j].m_prefixLen;

            //同一个 IP 只留一份：已有且优先级不低于新的就跳过，否则用新的替换
            bool merged = false;
            for (size_t k = 0; k < out.size(); ++k)
            {
                if (out[k].m_ip != ip)
                    continue;
                if (c.m_priority > out[k].m_priority)
                    out[k] = c;
                merged = true;
                break;
            }
            if (!merged)
                out.push_back(c);
        }
    }
    std::stable_sort(out.begin(), out.end(), HigherPriority);
    if (out.size() > kP2PMaxCandidates)
        out.resize(kP2PMaxCandidates);
    return out;
}

std::string IpToString(uint32_t ip)
{
    std::string s;
    for (int i = kIpv4Parts - 1; i >= 0; --i)
    {
        s += std::to_string((ip >> (kBitsPerByte * i)) & kMaxPartValue);
        if (i > 0)
            s += ".";
    }
    return s;
}

bool IpFromString(const std::string& text, uint32_t& ip)
{
    uint32_t value = 0;
    int parts = 0;
    size_t pos = 0;
    while (parts < kIpv4Parts)
    {
        //每段 1～3 位数字，取值不超过 255
        int digits = 0;
        int part = 0;
        while (pos < text.size() && isdigit((unsigned char)text[pos]) && digits < kMaxPartDigits)
        {
            part = part * kDecimalBase + (text[pos] - '0');
            ++pos;
            ++digits;
        }
        if (digits == 0 || part > kMaxPartValue)
            return false;
        value = (value << kBitsPerByte) | (uint32_t)part;
        ++parts;
        if (parts < kIpv4Parts)
        {
            if (pos >= text.size() || text[pos] != '.')
                return false;
            ++pos;
        }
    }
    if (pos != text.size())
        return false;
    ip = value;
    return true;
}

void CandidatesToJson(const std::vector<P2PCandidate>& candidates, Json::Value& out)
{
    out = Json::Value(Json::arrayValue);
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        Json::Value item;
        item["type"] = candidates[i].m_type;
        item["ip"] = IpToString(candidates[i].m_ip);
        item["port"] = candidates[i].m_port;
        item["priority"] = candidates[i].m_priority;
        out.append(item);
    }
}

size_t CandidatesFromJson(const Json::Value& arr, std::vector<P2PCandidate>& out)
{
    out.clear();
    if (!arr.isArray())
        return 0;
    for (Json::ArrayIndex i = 0; i < arr.size() && out.size() < kP2PMaxCandidates; ++i)
    {
        const Json::Value& item = arr[i];
        if (!item.isObject() || !item["type"].isInt() || !item["ip"].isString() || !item["port"].isInt()
            || !item["priority"].isUInt())
            continue;
        const int type = item["type"].asInt();
        const int port = item["port"].asInt();
        uint32_t ip = 0;
        if ((type != p2p_candidate_host && type != p2p_candidate_srflx) || port <= 0 || port > kMaxPort
            || !IpFromString(item["ip"].asString(), ip) || ip == 0)
            continue;
        P2PCandidate c;
        c.m_type = type;
        c.m_ip = ip;
        c.m_port = (uint16_t)port;
        c.m_priority = item["priority"].asUInt();
        out.push_back(c);
    }
    return out.size();
}

} // namespace p2p
