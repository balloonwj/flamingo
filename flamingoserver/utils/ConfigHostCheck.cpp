/**
 *  配置里的主机地址检查的实现，ConfigHostCheck.cpp
 *  判定规则见 ConfigHostCheck.h。
 *
 *  balloonwj@qq.com   2026-10-12
 */
#include "ConfigHostCheck.h"

#include <ctype.h>

namespace {

//点分十进制 IPv4 地址的段数
const size_t kIpv4PartCount = 4;

//IPv4 每段最多的数字位数
const size_t kIpv4MaxDigits = 3;

//IPv4 每段的最大取值
const int kIpv4MaxPartValue = 255;

//回环网段 127.0.0.0/8 的第一段
const int kLoopbackFirstOctet = 127;

//十进制的基数，逐位累加数字时用
const int kDecimalBase = 10;

//主机名 localhost，比较时不区分大小写
const char* const kLocalhostName = "localhost";

//IPv6 回环地址与未指定地址的简写
const char* const kIpv6Loopback = "::1";
const char* const kIpv6Unspecified = "::";

//一对方括号占的字符数：IPv6 文本写成 [::1] 时首尾各一个
const size_t kBracketPairChars = 2;

//告警里多个用途之间的分隔
const char* const kUsageSeparator = ", ";

/**
 *  严格按点分十进制解析 IPv4 文本：恰好四段，每段 1～3 位数字且不超过 255，不接受 127.1 这类简写。
 *  @param text  待解析的文本
 *  @param parts 解析成功时依次写入四段的值
 *  @return 是合法的点分十进制 IPv4 文本时返回 true
 */
bool ParseDottedIpv4(const std::string& text, int parts[kIpv4PartCount])
{
    size_t partIndex = 0;
    size_t digits = 0;
    int value = 0;
    for (size_t i = 0; i <= text.size(); ++i)
    {
        //末尾按一个分隔点处理，收尾最后一段
        const char ch = (i < text.size()) ? text[i] : '.';
        if (ch == '.')
        {
            if (digits == 0 || partIndex >= kIpv4PartCount)
                return false;
            parts[partIndex] = value;
            ++partIndex;
            digits = 0;
            value = 0;
            continue;
        }
        if (!isdigit(static_cast<unsigned char>(ch)))
            return false;
        ++digits;
        if (digits > kIpv4MaxDigits)
            return false;
        value = value * kDecimalBase + (ch - '0');
        if (value > kIpv4MaxPartValue)
            return false;
    }
    return partIndex == kIpv4PartCount;
}

//不区分大小写比较两个 ASCII 字符串
bool EqualsIgnoreCase(const std::string& a, const char* b)
{
    size_t i = 0;
    for (; i < a.size() && b[i] != '\0'; ++i)
    {
        if (tolower(static_cast<unsigned char>(a[i])) != tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return i == a.size() && b[i] == '\0';
}

/**
 *  判断 usages（以 kUsageSeparator 连接的用途列表）里是否已有 usage。
 *  @param usages 已合并的用途列表
 *  @param usage  要查的用途
 *  @return 已有时返回 true
 */
bool ContainsUsage(const std::string& usages, const std::string& usage)
{
    const std::string separator = kUsageSeparator;
    size_t begin = 0;
    while (begin <= usages.size())
    {
        size_t end = usages.find(separator, begin);
        if (end == std::string::npos)
            end = usages.size();
        if (usages.compare(begin, end - begin, usage) == 0)
            return true;
        begin = end + separator.size();
    }
    return false;
}

} // namespace

PublishedHost::PublishedHost()
    : m_port(0)
{
}

PublishedHost::PublishedHost(const std::string& key, const std::string& host, int port, const std::string& usage)
    : m_key(key)
    , m_host(host)
    , m_port(port)
    , m_usage(usage)
{
}

std::vector<LocalOnlyHostWarning> ConfigHostCheck::findLocalOnly(const std::vector<PublishedHost>& published)
{
    std::vector<LocalOnlyHostWarning> warnings;
    for (size_t i = 0; i < published.size(); ++i)
    {
        const PublishedHost& item = published[i];
        //地址为空或端口没有配置：这项功能不下发地址，客户端也不会去连，不必告警
        if (item.m_host.empty() || item.m_port <= 0)
            continue;
        if (!isLocalOnlyHost(item.m_host))
            continue;

        //同一配置项的同一个地址合并成一条，只追加新出现的用途
        size_t index = 0;
        while (index < warnings.size()
               && (warnings[index].m_key != item.m_key || warnings[index].m_host != item.m_host))
            ++index;
        if (index == warnings.size())
        {
            LocalOnlyHostWarning warning;
            warning.m_key = item.m_key;
            warning.m_host = item.m_host;
            warning.m_usages = item.m_usage;
            warnings.push_back(warning);
            continue;
        }
        if (!ContainsUsage(warnings[index].m_usages, item.m_usage))
        {
            warnings[index].m_usages += kUsageSeparator;
            warnings[index].m_usages += item.m_usage;
        }
    }
    return warnings;
}

bool ConfigHostCheck::isLocalOnlyHost(const std::string& host)
{
    if (host.empty())
        return false;

    int parts[kIpv4PartCount] = { 0 };
    if (ParseDottedIpv4(host, parts))
    {
        //127.0.0.0/8 回环网段
        if (parts[0] == kLoopbackFirstOctet)
            return true;
        //未指定地址 0.0.0.0
        return parts[0] == 0 && parts[1] == 0 && parts[2] == 0 && parts[3] == 0;
    }

    if (EqualsIgnoreCase(host, kLocalhostName))
        return true;

    //IPv6 文本可能带方括号（URL 里的写法），去掉后再比较
    std::string bare = host;
    if (bare.size() >= kBracketPairChars && bare[0] == '[' && bare[bare.size() - 1] == ']')
        bare = bare.substr(1, bare.size() - kBracketPairChars);
    return bare == kIpv6Loopback || bare == kIpv6Unspecified;
}
