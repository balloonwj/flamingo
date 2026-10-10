/**
 *  P2P 回落中转（imgserver 1861 / 1862）的端到端联调用例（Suite P2PR，标签 [.live][p2prelay]），P2PRelayLiveTests.cpp
 *
 *  docs/P2P通信方案.md 第 2 期 2f-3：直接连 imgserver 的中转端口，中转令牌在测试里自己签（与 chatserver 用同一把
 *  密钥，imgserver 只认令牌），会话号用 chatserver 实际使用的 "p2p-…" 格式。
 *    - P2PR1 两端分别用角色 11 / 12 的令牌发 1861 报到，应答 code 都是 0；
 *    - P2PR2 互发 1862：载荷覆盖 0～255 全部字节值，对端原样收到，命令号与序号不变；反方向同样；
 *    - P2PR3 角色不符被拒：角色 1 的令牌发 1861、角色 11 的令牌发 1853（远程桌面报到），应答都非 0，连接都被关闭；
 *    - P2PR4 一端断开后，另一端的中转连接被服务端关闭；
 *    - P2PR5 已进屏幕共享房间的连接再发 1861 被拒并断开；随后新连接用同一会话、同一角色报到成功
 *            （修复 RelaySession::onHello 只判断已配对之前，第一次报到会被接受，房间连接断开后在配对表留下残留
 *            表项，第二次报到因「角色重复」被拒）。
 *
 *  默认不参与常规测试：用例带 [.live] 标签。跑之前用 tmp/p2ptest/setup_p2ptest.py 建环境、start_server.ps1 起测试服务端
 *  （imgserver 中转端口 29903）。
 *
 *      FlamingoUnit.exe "[p2prelay]"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <stdio.h>
#include <time.h>
#include <string>
#include <thread>

#include "LiveTestConn.h"
#include "RoomRelayTestKit.h"
#include "../net/FrameCodec.h"
#include "../net/FrameCrypto.h"
#include "../net/FramePayload.h"
#include "../net/Protocol.h"
#include "../../../flamingoserver/utils/RelayToken.h"
#include "../third-party/jsoncpp-1.9.0/json.h"

using namespace net;
using namespace livetest;

namespace {

//tmp/p2ptest 测试服务端的 imgserver 中转端口
const unsigned short kP2PRelayPort = 29903;

//中转令牌的 HMAC 密钥，须与测试服务端 imgserver.conf 的 relaytokenkey 一致
const char* const kRelayTokenKey = "flamingo-relay-token-secret";

//令牌有效期（毫秒）：够跑完一条用例即可
const int64_t kTokenTtlMs = 5 * 60 * 1000;

//令牌里的两个用户（imgserver 只按会话号与角色配对，不校验用户）
const int64_t kUidA = 5127;
const int64_t kUidB = 5308;

//报到帧与两个方向数据帧的序号：两个方向取不同值，断言失败时能看出是哪个方向
const uint32_t kHelloSeq = 1;
const uint32_t kSeqAtoB = 0x1001;
const uint32_t kSeqBtoA = 0x2002;

//数据帧载荷长度（字节）：覆盖 0～255 全部字节值若干轮
const size_t kPayloadBytes = 4096;

//判定「被服务端关闭」的时限（毫秒）：被关闭时 recv 立即失败；连接还开着时要等满 kIoTimeoutMs（3 秒）才超时
const ULONGLONG kClosedWithinMs = 2000;

//P2PR5：房间连接断开后等服务端处理完断开事件的时间（毫秒）
const DWORD kDisconnectSettleMs = 300;

//P2PR5：屏幕共享房间里的座位号
const int kRoomSeat = 0;

//一条中转连接
struct RelayEnd
{
    TestConn             m_conn;   //与 imgserver 中转端口的连接
    net::ClientHandshake m_hs;     //握手器，握手成功后其 cipher() 用于加解密
};

//生成一个 chatserver 格式的 P2P 会话号：p2p-<uidA>-<uidB>-<毫秒>-<序号>
std::string MakeP2PSessionId()
{
    const size_t kBufBytes = 96;
    static unsigned int s_seq = 0;
    ++s_seq;
    char buf[kBufBytes] = { 0 };
    snprintf(buf, sizeof(buf), "p2p-%lld-%lld-%llu-%u", (long long)kUidA, (long long)kUidB,
             (unsigned long long)::GetTickCount64(), s_seq);
    return buf;
}

//签一枚中转令牌
std::string MakeToken(const std::string& sessionId, int role, int64_t uid, int64_t peerUid)
{
    const int64_t kMillisPerSecond = 1000;
    RelayTokenPayload payload;
    payload.sessionid = sessionId;
    payload.role = role;
    payload.uid = uid;
    payload.peeruid = peerUid;
    payload.expire = (int64_t)time(NULL) * kMillisPerSecond + kTokenTtlMs;
    return RelayToken::sign(payload, kRelayTokenKey);
}

//按普通业务帧发一条（加密、不压缩）
bool SendBody(RelayEnd& e, uint16_t cmd, uint32_t seq, const std::string& body)
{
    std::string wire;
    uint8_t flags = 0;
    uint32_t originlen = 0;
    if (!encodeFramePayload(cmd, seq, body, &e.m_hs.cipher(), 0, payload_compress_never, wire, flags, originlen))
        return false;
    std::string frame;
    if (!encodeFrame(cmd, seq, flags, wire.data(), (uint32_t)wire.size(), originlen, frame))
        return false;
    return e.m_conn.SendAll(frame);
}

//收一帧并还原包体
bool RecvBody(RelayEnd& e, frame_header& header, std::string& body)
{
    std::string wire;
    if (!e.m_conn.RecvFrame(header, wire, MAX_BODY_FILESERVER))
        return false;
    std::string err;
    return decodeFramePayload(header.cmd, header.seq, header.flags, header.originlen, wire, &e.m_hs.cipher(), body, err);
}

//在已握手的连接上发报到帧并等应答；收到应答返回 true，code 为应答里的错误码
bool HelloOnConnected(RelayEnd& e, uint16_t helloCmd, const std::string& token, int& code)
{
    code = -1;
    if (!SendBody(e, helloCmd, kHelloSeq, "{\"token\":\"" + token + "\"}"))
        return false;
    frame_header header;
    std::string body;
    if (!RecvBody(e, header, body) || header.cmd != helloCmd)
        return false;
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::Value root;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(body.c_str(), body.c_str() + body.size(), &root, &errs);
    delete reader;
    if (!ok || !root.isObject() || !root["code"].isInt())
        return false;
    code = root["code"].asInt();
    return true;
}

//连接中转端口、握手，再报到
bool Hello(RelayEnd& e, uint16_t helloCmd, const std::string& token, int& code, std::string& reason)
{
    if (!e.m_conn.Connect(kP2PRelayPort))
    {
        reason = "connect relay port failed";
        return false;
    }
    if (!DoHandshake(e.m_conn, e.m_hs, reason))
        return false;
    if (!HelloOnConnected(e, helloCmd, token, code))
    {
        reason = "no hello ack";
        return false;
    }
    return true;
}

//连接是否已被服务端关闭：recv 在 kClosedWithinMs 内失败即认为被关闭（收到任何帧都算没关闭）
bool ClosedByServer(RelayEnd& e)
{
    const ULONGLONG start = ::GetTickCount64();
    frame_header header;
    std::string wire;
    const bool got = e.m_conn.RecvFrame(header, wire, MAX_BODY_FILESERVER);
    return !got && (::GetTickCount64() - start) < kClosedWithinMs;
}

//覆盖 0～255 全部字节值的载荷，salt 让两个方向的内容不同
std::string MakePayload(int salt)
{
    std::string s(kPayloadBytes, '\0');
    for (size_t i = 0; i < kPayloadBytes; ++i)
        s[i] = (char)((i + (size_t)salt) & 0xFF);
    return s;
}

//两端用同一会话报到成一对
void PairUp(RelayEnd& a, RelayEnd& b, const std::string& sessionId)
{
    int codeA = -1;
    int codeB = -1;
    std::string reasonA;
    std::string reasonB;
    const bool okA = Hello(a, cmd_p2p_relay_hello, MakeToken(sessionId, RelayToken::role_p2p_controlling, kUidA, kUidB),
                           codeA, reasonA);
    const bool okB = Hello(b, cmd_p2p_relay_hello, MakeToken(sessionId, RelayToken::role_p2p_controlled, kUidB, kUidA),
                           codeB, reasonB);
    INFO("hello A: " << reasonA << " code " << codeA << "; hello B: " << reasonB << " code " << codeB);
    REQUIRE(okA);
    REQUIRE(codeA == 0);
    REQUIRE(okB);
    REQUIRE(codeB == 0);
}

} // anonymous namespace

TEST_CASE("P2PR1 两端用角色 11 / 12 报到", "[.live][p2prelay]")
{
    RelayEnd a;
    RelayEnd b;
    PairUp(a, b, MakeP2PSessionId());
}

TEST_CASE("P2PR2 互发 1862，载荷原样到达", "[.live][p2prelay]")
{
    RelayEnd a;
    RelayEnd b;
    PairUp(a, b, MakeP2PSessionId());

    //A → B
    const std::string toB = MakePayload(0);
    REQUIRE(SendBody(a, cmd_p2p_relay_data, kSeqAtoB, toB));
    frame_header header;
    std::string body;
    REQUIRE(RecvBody(b, header, body));
    CHECK(header.cmd == cmd_p2p_relay_data);
    CHECK(header.seq == kSeqAtoB);
    CHECK(body == toB);

    //B → A
    const std::string toA = MakePayload(7);
    REQUIRE(SendBody(b, cmd_p2p_relay_data, kSeqBtoA, toA));
    REQUIRE(RecvBody(a, header, body));
    CHECK(header.cmd == cmd_p2p_relay_data);
    CHECK(header.seq == kSeqBtoA);
    CHECK(body == toA);
}

TEST_CASE("P2PR3 报到命令与角色不符时被拒", "[.live][p2prelay]")
{
    const std::string sessionId = MakeP2PSessionId();
    int code = -1;
    std::string reason;

    //角色 1（远程桌面控制端）的令牌发 P2P 报到
    RelayEnd x;
    REQUIRE(Hello(x, cmd_p2p_relay_hello, MakeToken(sessionId, RelayToken::role_controller, kUidA, kUidB), code, reason));
    CHECK(code != 0);
    CHECK(ClosedByServer(x));

    //角色 11（P2P 发起方）的令牌发远程桌面报到
    RelayEnd y;
    REQUIRE(Hello(y, cmd_rd_relay_hello, MakeToken(sessionId, RelayToken::role_p2p_controlling, kUidA, kUidB), code, reason));
    CHECK(code != 0);
    CHECK(ClosedByServer(y));
}

TEST_CASE("P2PR4 一端断开后另一端被关闭", "[.live][p2prelay]")
{
    RelayEnd a;
    RelayEnd b;
    PairUp(a, b, MakeP2PSessionId());
    a.m_conn.Close();
    CHECK(ClosedByServer(b));
}

TEST_CASE("P2PR5 房间连接不能再做一对一报到，也不留残留表项", "[.live][p2prelay]")
{
    const std::string sessionId = MakeP2PSessionId();
    const std::string token11 = MakeToken(sessionId, RelayToken::role_p2p_controlling, kUidA, kUidB);

    //X 先进屏幕共享房间
    RelayEnd x;
    int code = -1;
    std::string reason;
    const std::string roomToken = roomrelaytest::makeToken(roomrelaytest::makeRoomId(), RelayToken::role_screenshare_host,
                                                           kRoomSeat, kRelayTokenKey);
    REQUIRE(Hello(x, cmd_screenshare_relay_hello, roomToken, code, reason));
    REQUIRE(code == 0);

    //同一连接再发 P2P 报到：应被拒并断开（不回成功应答）
    REQUIRE(SendBody(x, cmd_p2p_relay_hello, kHelloSeq, "{\"token\":\"" + token11 + "\"}"));
    CHECK(ClosedByServer(x));
    x.m_conn.Close();
    ::Sleep(kDisconnectSettleMs);

    //新连接用同一会话、同一角色报到：配对表里没有残留表项，应成功
    RelayEnd y;
    REQUIRE(Hello(y, cmd_p2p_relay_hello, token11, code, reason));
    CHECK(code == 0);
}
