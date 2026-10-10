/**
 *  FlamingoP2P 链路报文与加密的单元测试（Suite P2PK，标签 [p2pcore]），P2PCoreTests.cpp
 *
 *  覆盖 docs/P2P通信方案.md 第 3 期 3a（7.1、7.2 节）：
 *    - P2PK1 密钥协商：双方交换公钥后，一方的发送标签等于另一方的接收标签；角色相同则对不上；公钥文本非法或
 *            长度不对时解码失败；全零的弱公钥 Init 失败；
 *    - P2PK2 加解密往返：各种报文类型封装再拆封得到原类型与原明文；计数器从 1 递增；空明文与最大明文都能往返，
 *            超长明文封装失败；
 *    - P2PK3 认证：报文头（类型、保留、会话标签、计数器）、密文、认证标签任意一字节被改动都拆封失败；用别的会话
 *            的密钥拆封得到「会话标签不对」；
 *    - P2PK4 重放窗口：同一报文第二次被拒；窗口内乱序接受；落后 1024 及以上被拒；认证失败的报文不推动窗口；
 *    - P2PK5 报文分类：链路报文类型都不小于 0x80，与 STUN（首字节最高两位为 0）可以区分；太短的报文为 unknown；
 *            报文头写入与解析一致；
 *    - P2PK6 （第 4 期 4a）中转路径的明文上限：默认上限下 64 KiB 明文封装失败，传入中转上限后成功并能拆封；超过
 *            中转上限时传入更大的上限也失败。
 *
 *  典型用法：FlamingoUnit.exe "[p2pcore]"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <sodium.h>
#include <string.h>
#include <string>
#include <vector>

#include "../Core/P2PCrypto.h"
#include "../Core/P2PPacket.h"
#include "../Core/P2PReplayWindow.h"

using namespace p2p;

namespace {

//一段测试明文
const char* const kHello = "hello p2p link";

//STUN Binding 成功应答的首字节（0x01）与一个 20 字节的最小 STUN 报文长度
const uint8_t kStunFirstByte = 0x01;
const size_t kStunHeaderBytes = 20;

//两个计数器之间相差窗口大小，用来造「刚好过旧」的报文
const uint64_t kWindow = kP2PReplayWindowSize;

//建好一对互相对应的加解密对象：a 为发起方，b 为应答方
void MakePair(P2PKeyPair& ka, P2PKeyPair& kb, P2PLinkCipher& a, P2PLinkCipher& b)
{
    REQUIRE(sodium_init() >= 0);
    ka.Generate();
    kb.Generate();
    REQUIRE(a.Init(true, ka, kb.PublicKey()));
    REQUIRE(b.Init(false, kb, ka.PublicKey()));
}

std::string Seal(P2PLinkCipher& c, uint8_t type, const std::string& plain)
{
    std::string wire;
    REQUIRE(c.Seal(type, reinterpret_cast<const uint8_t*>(plain.data()), plain.size(), wire));
    return wire;
}

int Open(P2PLinkCipher& c, const std::string& wire, uint8_t& type, std::string& plain)
{
    return c.Open(reinterpret_cast<const uint8_t*>(wire.data()), wire.size(), type, plain);
}

} // anonymous namespace

TEST_CASE("P2PK1 密钥协商与公钥文本", "[p2pcore]")
{
    P2PKeyPair ka;
    P2PKeyPair kb;
    P2PLinkCipher a;
    P2PLinkCipher b;
    MakePair(ka, kb, a, b);
    CHECK(a.IsReady());
    CHECK(b.IsReady());
    //一方的发送方向就是另一方的接收方向
    CHECK(a.TxTag() == b.RxTag());
    CHECK(b.TxTag() == a.RxTag());
    CHECK(a.TxTag() != a.RxTag());

    //两边都按发起方推导：收发对不上
    P2PLinkCipher wrong;
    REQUIRE(wrong.Init(true, kb, ka.PublicKey()));
    CHECK(wrong.TxTag() != a.RxTag());

    //公钥文本：往返一致
    uint8_t decoded[kP2PPublicKeyBytes] = { 0 };
    REQUIRE(P2PKeyPair::DecodePublicKey(ka.PublicKeyBase64(), decoded));
    CHECK(memcmp(decoded, ka.PublicKey(), kP2PPublicKeyBytes) == 0);
    //非法文本、长度不对（16 字节的 Base64）、空串都失败
    CHECK_FALSE(P2PKeyPair::DecodePublicKey("not base64 !!", decoded));
    CHECK_FALSE(P2PKeyPair::DecodePublicKey("AAAAAAAAAAAAAAAAAAAAAA==", decoded));
    CHECK_FALSE(P2PKeyPair::DecodePublicKey("", decoded));

    //全零的弱公钥：推导失败，对象保持未就绪
    const uint8_t zeros[kP2PPublicKeyBytes] = { 0 };
    P2PLinkCipher weak;
    CHECK_FALSE(weak.Init(true, ka, zeros));
    CHECK_FALSE(weak.IsReady());
}

TEST_CASE("P2PK2 加解密往返与计数器", "[p2pcore]")
{
    P2PKeyPair ka;
    P2PKeyPair kb;
    P2PLinkCipher a;
    P2PLinkCipher b;
    MakePair(ka, kb, a, b);

    const uint8_t types[] = { p2p_packet_probe, p2p_packet_probe_ack, p2p_packet_nominate, p2p_packet_nominate_ack,
                              p2p_packet_data, p2p_packet_keepalive, p2p_packet_close };
    uint64_t expectedCounter = 1;
    for (size_t i = 0; i < sizeof(types); ++i)
    {
        CHECK(a.NextTxCounter() == expectedCounter);
        const std::string wire = Seal(a, types[i], kHello);
        CHECK(wire.size() == kP2PHeaderBytes + strlen(kHello) + kP2PAuthTagBytes);
        ++expectedCounter;
        uint8_t type = 0;
        std::string plain;
        REQUIRE(Open(b, wire, type, plain) == p2p_open_ok);
        CHECK(type == types[i]);
        CHECK(plain == kHello);
    }

    //反方向
    uint8_t type = 0;
    std::string plain;
    REQUIRE(Open(a, Seal(b, p2p_packet_data, "pong"), type, plain) == p2p_open_ok);
    CHECK(plain == "pong");

    //空明文与最大明文都能往返；超长明文封装失败，计数器不变
    REQUIRE(Open(b, Seal(a, p2p_packet_keepalive, std::string()), type, plain) == p2p_open_ok);
    CHECK(plain.empty());
    const std::string maxPlain(kP2PMaxPlainBytes, 'm');
    const std::string maxWire = Seal(a, p2p_packet_data, maxPlain);
    CHECK(maxWire.size() == kP2PMaxDatagramBytes);
    REQUIRE(Open(b, maxWire, type, plain) == p2p_open_ok);
    CHECK(plain == maxPlain);
    const uint64_t before = a.NextTxCounter();
    const std::string tooLong(kP2PMaxPlainBytes + 1, 'x');
    std::string wire;
    CHECK_FALSE(a.Seal(p2p_packet_data, reinterpret_cast<const uint8_t*>(tooLong.data()), tooLong.size(), wire));
    //类型不认识也封装失败
    CHECK_FALSE(a.Seal(0x10, NULL, 0, wire));
    CHECK(a.NextTxCounter() == before);

    //未就绪的对象封装与拆封都失败
    P2PLinkCipher idle;
    CHECK_FALSE(idle.Seal(p2p_packet_data, NULL, 0, wire));
    CHECK(Open(idle, maxWire, type, plain) == p2p_open_not_ready);
}

TEST_CASE("P2PK3 报文任意一字节被改动都认证失败", "[p2pcore]")
{
    P2PKeyPair ka;
    P2PKeyPair kb;
    P2PLinkCipher a;
    P2PLinkCipher b;
    MakePair(ka, kb, a, b);

    const std::string wire = Seal(a, p2p_packet_data, kHello);
    for (size_t i = 0; i < wire.size(); ++i)
    {
        //会话标签那 4 字节被改：在解密之前就按「标签不对」丢弃；类型被改成不认识的值：格式不对；其余：认证失败
        std::string bad = wire;
        bad[i] = (char)(bad[i] ^ 0x01);
        uint8_t type = 0;
        std::string plain;
        const int r = Open(b, bad, type, plain);
        INFO("byte " << i << " result " << r);
        CHECK(r != p2p_open_ok);
        if (i >= kP2POffsetSessionTag && i < kP2POffsetCounter)
            CHECK(r == p2p_open_wrong_tag);
    }
    //原样的报文仍能拆封（篡改过的报文都没有推动重放窗口）
    uint8_t type = 0;
    std::string plain;
    CHECK(Open(b, wire, type, plain) == p2p_open_ok);

    //别的会话的报文：会话标签对不上
    P2PKeyPair kc;
    P2PKeyPair kd;
    P2PLinkCipher c;
    P2PLinkCipher d;
    MakePair(kc, kd, c, d);
    CHECK(Open(b, Seal(c, p2p_packet_data, kHello), type, plain) == p2p_open_wrong_tag);
    CHECK_FALSE(b.MatchesRxTag(reinterpret_cast<const uint8_t*>(wire.data()), 1));
    const std::string fromA = Seal(a, p2p_packet_keepalive, std::string());
    CHECK(b.MatchesRxTag(reinterpret_cast<const uint8_t*>(fromA.data()), fromA.size()));
    const std::string fromC = Seal(c, p2p_packet_keepalive, std::string());
    CHECK_FALSE(b.MatchesRxTag(reinterpret_cast<const uint8_t*>(fromC.data()), fromC.size()));
}

TEST_CASE("P2PK4 重放窗口", "[p2pcore]")
{
    //窗口本身
    P2PReplayWindow w;
    CHECK_FALSE(w.IsAcceptable(0));
    CHECK(w.IsAcceptable(1));
    w.Accept(5);
    CHECK(w.Highest() == 5);
    CHECK_FALSE(w.IsAcceptable(5));
    //窗口内乱序：比最大值小但没收到过
    CHECK(w.IsAcceptable(3));
    w.Accept(3);
    CHECK_FALSE(w.IsAcceptable(3));
    CHECK(w.Highest() == 5);
    //大幅前移后，落后窗口大小及以上的被拒，窗口内的仍可接受
    w.Accept(5 + kWindow * 3);
    const uint64_t top = 5 + kWindow * 3;
    CHECK_FALSE(w.IsAcceptable(top - kWindow));
    CHECK(w.IsAcceptable(top - kWindow + 1));
    CHECK(w.IsAcceptable(top - 1));
    //前移后旧位被清空：top - 2 = 3075 与早先接受过的 3 落在位图的同一位，不应被误判为已收到
    const uint64_t kSameBitAsThree = 2;
    CHECK((top - kSameBitAsThree) % kWindow == 3);
    CHECK(w.IsAcceptable(top - kSameBitAsThree));

    //经过加解密：同一报文第二次被拒，乱序在窗口内接受
    P2PKeyPair ka;
    P2PKeyPair kb;
    P2PLinkCipher a;
    P2PLinkCipher b;
    MakePair(ka, kb, a, b);
    const std::string first = Seal(a, p2p_packet_data, "1");
    const std::string second = Seal(a, p2p_packet_data, "2");
    uint8_t type = 0;
    std::string plain;
    REQUIRE(Open(b, second, type, plain) == p2p_open_ok);
    REQUIRE(Open(b, first, type, plain) == p2p_open_ok);
    CHECK(plain == "1");
    CHECK(Open(b, first, type, plain) == p2p_open_replayed);
    CHECK(Open(b, second, type, plain) == p2p_open_replayed);
}

TEST_CASE("P2PK5 报文分类与报文头", "[p2pcore]")
{
    const uint8_t types[] = { p2p_packet_probe, p2p_packet_probe_ack, p2p_packet_nominate, p2p_packet_nominate_ack,
                              p2p_packet_data, p2p_packet_keepalive, p2p_packet_close };
    std::vector<uint8_t> buf(kP2PHeaderBytes + kP2PAuthTagBytes, 0);
    for (size_t i = 0; i < sizeof(types); ++i)
    {
        CHECK(types[i] >= 0x80);
        CHECK(IsP2PPacketType(types[i]));
        buf[0] = types[i];
        CHECK(ClassifyDatagram(&buf[0], buf.size()) == p2p_datagram_link);
        //太短：报文头加认证标签都不够
        CHECK(ClassifyDatagram(&buf[0], kP2PHeaderBytes + kP2PAuthTagBytes - 1) == p2p_datagram_unknown);
    }
    CHECK_FALSE(IsP2PPacketType(0x80));
    CHECK_FALSE(IsP2PPacketType(0x88));

    //STUN：首字节最高两位为 0、不短于 20 字节
    std::vector<uint8_t> stun(kStunHeaderBytes, 0);
    stun[0] = kStunFirstByte;
    CHECK(ClassifyDatagram(&stun[0], stun.size()) == p2p_datagram_stun);
    CHECK(ClassifyDatagram(&stun[0], kStunHeaderBytes - 1) == p2p_datagram_unknown);
    //首字节最高两位不为 0、也不是链路报文类型
    stun[0] = 0x40;
    CHECK(ClassifyDatagram(&stun[0], stun.size()) == p2p_datagram_unknown);
    CHECK(ClassifyDatagram(NULL, 0) == p2p_datagram_unknown);

    //报文头写入与解析一致，多字节字段按网络字节序存放
    const uint32_t tag = 0x11223344;
    const uint64_t counter = 0x0102030405060708ULL;
    std::vector<uint8_t> wire(kP2PHeaderBytes + kP2PAuthTagBytes, 0xEE);
    WritePacketHeader(p2p_packet_nominate, tag, counter, &wire[0]);
    CHECK(wire[kP2POffsetType] == p2p_packet_nominate);
    CHECK(wire[kP2POffsetReserved] == 0);
    CHECK(wire[kP2POffsetSessionTag] == 0x11);
    CHECK(wire[kP2POffsetCounter] == 0x01);
    CHECK(wire[kP2POffsetCounter + 7] == 0x08);
    P2PPacketHeader h;
    REQUIRE(ParsePacketHeader(&wire[0], wire.size(), h));
    CHECK(h.m_type == p2p_packet_nominate);
    CHECK(h.m_sessionTag == tag);
    CHECK(h.m_counter == counter);
    CHECK_FALSE(ParsePacketHeader(&wire[0], kP2PHeaderBytes, h));
}

TEST_CASE("P2PK6 中转路径的明文上限", "[p2pcore]")
{
    P2PKeyPair ka;
    P2PKeyPair kb;
    P2PLinkCipher a;
    P2PLinkCipher b;
    MakePair(ka, kb, a, b);

    const std::string big(kP2PRelayMaxPlainBytes, 'r');
    std::string wire;
    //默认上限（UDP 路径）下超长：失败
    CHECK_FALSE(a.Seal(p2p_packet_data, reinterpret_cast<const uint8_t*>(big.data()), big.size(), wire));
    //按中转路径的上限封装：成功并能原样拆封
    REQUIRE(a.Seal(p2p_packet_data, reinterpret_cast<const uint8_t*>(big.data()), big.size(), wire, kP2PRelayMaxPlainBytes));
    CHECK(wire.size() == kP2PHeaderBytes + kP2PRelayMaxPlainBytes + kP2PAuthTagBytes);
    uint8_t type = 0;
    std::string plain;
    REQUIRE(Open(b, wire, type, plain) == p2p_open_ok);
    CHECK(plain == big);
    //超过中转路径的上限：传入更大的上限也失败
    const std::string tooBig(kP2PRelayMaxPlainBytes + 1, 'x');
    CHECK_FALSE(a.Seal(p2p_packet_data, reinterpret_cast<const uint8_t*>(tooBig.data()), tooBig.size(), wire,
                       kP2PRelayMaxPlainBytes * 2));
}
