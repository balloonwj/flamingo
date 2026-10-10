/**
 *  imgserver 地址探测服务的纯逻辑单元测试（Suite STUN、RL），test_p2paddressprobe.cpp
 *
 *  覆盖 docs/P2P通信方案.md 第 2 期 2e-2（6.2 节：兼容 RFC 5389 的 Binding，严格不放大，按来源 IP 限频）：
 *    - STUN1 合法请求：Binding 类型、魔数正确、长度字段与实际一致、总长不小于 32 字节，带属性也认，解析出事务号；
 *    - STUN2 不应答的报文：不足 20 字节、20～31 字节的合法请求（应答 32 字节，会放大）、类型不是 Binding 请求、
 *            魔数不对、长度字段与实际不符或不是 4 的倍数、首字节最高两位不为 0；
 *    - STUN3 应答按 RFC 5769 第 2.2 节示例里的 XOR-MAPPED-ADDRESS（192.0.2.1:32853）逐字节核对，带回请求的事务号，
 *            长度固定 32 字节；
 *    - RL1 同一来源 IP 一秒内超过上限后被拒，下一秒恢复；
 *    - RL2 不同来源 IP 互不影响；
 *    - RL3 表项数量有上限：表满且都未过期时新来源被拒；过期表项被清理后新来源可以进来，表项数不超过上限。
 *
 *  典型用法：Tests.exe --test-case="STUN*"、Tests.exe --test-case="RL*"
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include <stdint.h>
#include <string>

#include "doctest.h"
#include "../imgserversrc/P2PProbeCore.h"

namespace {

//RFC 5769 第 2.2 节示例的事务号与映射地址（192.0.2.1:32853）
const uint8_t kRfcTransactionId[] = { 0xb7, 0xe7, 0xa7, 0x01, 0xbc, 0x34, 0xd6, 0x86, 0xfa, 0x87, 0xdf, 0xae };
const uint32_t kRfcMappedIp = 0xC0000201;
const uint16_t kRfcMappedPort = 32853;

//期望的应答：20 字节头（类型 0x0101、长度 12、魔数、事务号）加 XOR-MAPPED-ADDRESS（RFC 5769 2.2 节中的同一属性）
const uint8_t kExpectedResponse[] =
{
    0x01, 0x01, 0x00, 0x0c, 0x21, 0x12, 0xa4, 0x42,
    0xb7, 0xe7, 0xa7, 0x01, 0xbc, 0x34, 0xd6, 0x86, 0xfa, 0x87, 0xdf, 0xae,
    0x00, 0x20, 0x00, 0x08, 0x00, 0x01, 0xa1, 0x47, 0xe1, 0x12, 0xa6, 0x43,
};

//请求里用来补足长度的 SOFTWARE 属性（类型 0x8022，可忽略的属性），值为 8 字节
const uint8_t kSoftwareAttr[] = { 0x80, 0x22, 0x00, 0x08, 'f', 'l', 'a', 'm', 'i', 'n', 'g', 'o' };

//限频用例的参数：每个来源 IP 每秒上限、表项上限、一秒（毫秒）、测试起始时刻（毫秒）
const int kMaxPerSecond = 3;
const size_t kMaxEntries = 4;
const int64_t kOneSecondMs = 1000;
const int64_t kStartMs = 100000;

//几个来源 IP（主机字节序）
const uint32_t kIpA = 0x0A000001;
const uint32_t kIpB = 0x0A000002;

//构造一个 Binding 请求：20 字节头加 attrs；type、cookie、lengthField 可改，用来造各种非法报文
std::string MakeRequest(const std::string& attrs, uint16_t type = 0x0001, uint32_t cookie = 0x2112A442,
                        int lengthField = -1)
{
    const uint16_t length = (lengthField < 0) ? (uint16_t)attrs.size() : (uint16_t)lengthField;
    std::string msg;
    msg.push_back((char)(type >> 8));
    msg.push_back((char)(type & 0xFF));
    msg.push_back((char)(length >> 8));
    msg.push_back((char)(length & 0xFF));
    msg.push_back((char)(cookie >> 24));
    msg.push_back((char)((cookie >> 16) & 0xFF));
    msg.push_back((char)((cookie >> 8) & 0xFF));
    msg.push_back((char)(cookie & 0xFF));
    msg.append((const char*)kRfcTransactionId, sizeof(kRfcTransactionId));
    msg += attrs;
    return msg;
}

std::string SoftwareAttr()
{
    return std::string((const char*)kSoftwareAttr, sizeof(kSoftwareAttr));
}

bool Parse(const std::string& msg, StunTransactionId& txid)
{
    return P2PStunCodec::parseBindingRequest((const uint8_t*)msg.data(), msg.size(), txid);
}

} // anonymous namespace

TEST_CASE("STUN1 合法的 Binding 请求")
{
    //32 字节：头加一个 SOFTWARE 属性
    StunTransactionId txid;
    REQUIRE(Parse(MakeRequest(SoftwareAttr()), txid));
    CHECK(std::string((const char*)txid.m_bytes, sizeof(txid.m_bytes)) ==
          std::string((const char*)kRfcTransactionId, sizeof(kRfcTransactionId)));

    //更长的请求（两个属性）同样接受，属性内容不解析
    StunTransactionId txid2;
    CHECK(Parse(MakeRequest(SoftwareAttr() + SoftwareAttr()), txid2));
}

TEST_CASE("STUN2 不应答的报文")
{
    StunTransactionId txid;
    const std::string valid = MakeRequest(SoftwareAttr());
    //不足 20 字节
    CHECK_FALSE(Parse(valid.substr(0, 19), txid));
    //20 字节的最小请求：合法但应答会放大，不应答
    CHECK_FALSE(Parse(MakeRequest(std::string()), txid));
    //28 字节（头加 8 字节属性）：同样小于应答长度
    CHECK_FALSE(Parse(MakeRequest(std::string((const char*)kSoftwareAttr, 4) + std::string(4, 'x')), txid));
    //类型不是 Binding 请求（0x0101 是成功应答，0x0011 是 Binding 指示）
    CHECK_FALSE(Parse(MakeRequest(SoftwareAttr(), 0x0101), txid));
    CHECK_FALSE(Parse(MakeRequest(SoftwareAttr(), 0x0011), txid));
    //魔数不对（RFC 3489 的老客户端没有魔数）
    CHECK_FALSE(Parse(MakeRequest(SoftwareAttr(), 0x0001, 0x12345678), txid));
    //长度字段与实际不符、不是 4 的倍数
    CHECK_FALSE(Parse(MakeRequest(SoftwareAttr(), 0x0001, 0x2112A442, 16), txid));
    CHECK_FALSE(Parse(MakeRequest(SoftwareAttr() + "ab", 0x0001, 0x2112A442), txid));
    //首字节最高两位不为 0（不是 STUN 报文）
    std::string highBits = valid;
    highBits[0] = (char)(highBits[0] | 0x40);
    CHECK_FALSE(Parse(highBits, txid));
    //对照：原样的合法请求仍然接受
    CHECK(Parse(valid, txid));
}

TEST_CASE("STUN3 应答与 RFC 5769 示例一致")
{
    StunTransactionId txid;
    for (size_t i = 0; i < sizeof(kRfcTransactionId); ++i)
        txid.m_bytes[i] = kRfcTransactionId[i];
    const std::string resp = P2PStunCodec::buildBindingResponse(txid, kRfcMappedIp, kRfcMappedPort);
    CHECK(resp.size() == P2PStunCodec::kBindingResponseBytes);
    CHECK(resp == std::string((const char*)kExpectedResponse, sizeof(kExpectedResponse)));
}

TEST_CASE("RL1 同一来源超过上限后被拒，下一秒恢复")
{
    P2PProbeRateLimiter limiter(kMaxPerSecond, kMaxEntries);
    for (int i = 0; i < kMaxPerSecond; ++i)
        CHECK(limiter.allow(kIpA, kStartMs + i));
    CHECK_FALSE(limiter.allow(kIpA, kStartMs + kMaxPerSecond));
    //一秒之后恢复
    CHECK(limiter.allow(kIpA, kStartMs + kOneSecondMs));
}

TEST_CASE("RL2 不同来源互不影响")
{
    P2PProbeRateLimiter limiter(kMaxPerSecond, kMaxEntries);
    for (int i = 0; i < kMaxPerSecond; ++i)
        CHECK(limiter.allow(kIpA, kStartMs));
    CHECK_FALSE(limiter.allow(kIpA, kStartMs));
    //A 已被拒，B 照样放行
    CHECK(limiter.allow(kIpB, kStartMs));
}

TEST_CASE("RL3 表项数量有上限，过期表项被清理")
{
    P2PProbeRateLimiter limiter(kMaxPerSecond, kMaxEntries);
    //填满：kMaxEntries 个不同来源都在同一秒内
    for (size_t i = 0; i < kMaxEntries; ++i)
        CHECK(limiter.allow(kIpA + (uint32_t)i, kStartMs));
    CHECK(limiter.entryCount() == kMaxEntries);
    //表满且都未过期：新来源被拒，表项数不增长
    CHECK_FALSE(limiter.allow(kIpA + (uint32_t)kMaxEntries, kStartMs));
    CHECK(limiter.entryCount() == kMaxEntries);
    //一秒之后旧表项都已过期：新来源可以进来，过期表项被清理
    CHECK(limiter.allow(kIpA + (uint32_t)kMaxEntries, kStartMs + kOneSecondMs));
    CHECK(limiter.entryCount() <= kMaxEntries);
    CHECK(limiter.entryCount() == 1);
}
