/**
 *  枚举本机网卡（docs/P2P通信方案.md 6.1 节，第 3 期 3d）：用 GetAdaptersAddresses 取每块网卡的状态、名称、描述与
 *  IPv4 地址（带前缀长度），交给 BuildHostCandidates 筛选。头文件不包含 winsock 与 iphlpapi 的头文件。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <vector>

#include "../Ice/P2PCandidate.h"

namespace p2p {

/**
 *  枚举本机网卡。
 *  @return 每块网卡一项（含未连接与回环网卡，由 BuildHostCandidates 筛掉）；系统调用失败时为空
 */
std::vector<P2PAdapterInfo> EnumerateAdapters();

/**
 *  把服务器地址（IP 文本或域名）解析成 IPv4 地址。会阻塞（域名要查 DNS），只在登录时与建会话时调用。
 *  @param host 地址或域名
 *  @param ip   [出参] IPv4 地址（主机字节序）
 *  @return 解析成功返回 true
 */
bool ResolveIpv4(const std::string& host, uint32_t& ip);

} // namespace p2p
