/**
 *  RelayForwardLiveTests.cpp
 *
 *  中转通道（远程桌面 / 音视频）<u>双端转发</u>的跨实现联调测试：同时建两条连接，
 *  各自与真实运行的 imgserver 走完三帧加密握手、用配对的中转令牌报到，然后验证
 *  一端发出的数据帧能被另一端收到并解出<u>逐字节相同</u>的明文。
 *
 *  为什么必须有这一组用例：协议加密引入之后，服务端的 RelaySession 不再是字节级
 *  透传，而是「用发送端连接的密钥解密包体、再用配对端连接的密钥重新加密」。两条
 *  连接各有各的会话密钥，任何一侧的密钥取错、附加认证数据拼错、或是不小心退回成
 *  透传，都会让对端解不出内容。已有的 HSLIVE7 / HSLIVE8 只验到了单端的握手与报到，
 *  转发这一段在此之前没有任何测试覆盖。
 *
 *  <u>判据是往返而不是握手</u>：握手走完并不等于两端真的通了（见
 *  docs/协议加密方案.md 第 13.2 节的那条经验），所以本组用例的核心断言一律是
 *  「对端收到并解出了原样的明文」。密文在两端不相同这一条则从反面证明服务端确实
 *  做了解密与重新加密，而不是把字节原样转出去。
 *
 *  <u>默认不参与常规测试</u>：本文件的用例都带 [.live] 标签，Catch2 会跳过它们。
 *  要跑就显式指定标签：
 *
 *      FlamingoUnit.exe "[live]"
 *
 *  跑之前需要准备一个独立的 imgserver 实例（不要用开发中正在联调的那个，端口会撞）：
 *    1. 把 flamingoserver/windowsproj/imgserver.exe 复制到 tmp/cryptotest/；
 *    2. 在它的上一级建 etc/imgserver.conf（仓库里已有 tmp/etc/imgserver.conf），
 *       relaylistenport 设为 livetest::kTestRelayPort、encryption 设为 1、
 *       serverkeyfile 指向服务端真正在用的那份密钥、relaytokenkey 与本文件的
 *       kRelayTokenKey 一致；
 *    3. 起它。服务端换过密钥或改过 relaytokenkey 之后，本文件与 LiveTestConn.h
 *       里的对应常量要跟着换，否则握手验签或令牌校验必然失败。
 *
 *  中转令牌正常由 chatserver 签发，测试里没有 chatserver 可用，故直接链接服务端的
 *  flamingoserver/utils/RelayToken.cpp 自己签一枚（见 FlamingoUnit.vcxproj）。
 *  自己按同样算法在测试里重写一份 HMAC-MD5 也能签出来，但那样测的就是测试自己的
 *  实现，令牌格式一旦改动两边会各错各的，故取直接复用。
 *
 *  balloonwj@qq.com   2026-08-13
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>
#include <string.h>
#include <time.h>
#include <stdio.h>

//连接、按帧收发与三帧握手的脚手架，与 HandshakeLiveTests.cpp 共用同一份
#include "LiveTestConn.h"

#include "../net/FrameCodec.h"
#include "../net/FrameCrypto.h"
#include "../net/FramePayload.h"
#include "../net/Protocol.h"

//中转令牌的签发接口。这是服务端 utils/ 下的模块，纯逻辑、只依赖 utils/MD5，
//故可以直接编进测试工程；imgserver 校验令牌用的正是同一份代码。
#include "../../../flamingoserver/utils/RelayToken.h"

#include "../third-party/jsoncpp-1.9.0/json.h"

using namespace net;
using namespace livetest;

namespace {

//中转令牌的 HMAC 密钥，必须与被测 imgserver 实例配置里的 relaytokenkey 一致
//（见 tmp/etc/imgserver.conf）。不一致时服务端验签不过，表现为报到应答里带
//error_code_session_expired。
const char* const kRelayTokenKey = "flamingo-relay-token-secret";

//令牌有效期，单位毫秒：签发后 5 分钟内有效。取值只要够跑完一条用例即可
const int64_t kTokenTtlMs = 5 * 60 * 1000;

//报到帧统一用这个序列号。中转报到帧只发一次，序列号本身不参与配对
const uint32_t kHelloSeq = 1;

//A 端发往 B 端的数据帧序列号。两个方向取不同值，便于在断言失败时看出是哪个方向的帧
const uint32_t kSeqAtoB = 11;

//B 端发往 A 端的数据帧序列号
const uint32_t kSeqBtoA = 22;

//测试载荷长度，单位字节。取 1000 是为了跨过 COMPRESS_THRESHOLD（256 字节），
//让「服务端不压不解压」这条约定也一并受检
const size_t kPayloadBytes = 1000;

//令牌里的用户 id。imgserver 只按 sessionid 与 role 配对，不校验 uid，
//故取两个固定值即可，只要两端互为对方的 peeruid
const int64_t kUidA = 100001;
const int64_t kUidB = 100002;

//伪造的「压缩前原始长度」，单位字节。用于验证 originlen 经服务端转发后原样保留：
//取一个明显大于包体长度、又不会撞上任何自然值的数
const uint32_t kFakeOriginLen = 65537;

/**
 *  生成一个本次进程运行内唯一的中转会话 id。
 *
 *  RelayToken 约定 sessionid 只含十六进制字符与连字符（不得含 "." 与 "|"，
 *  否则会破坏令牌 payload 的切分），故这里用「毫秒计数 + 自增序号」的十六进制
 *  拼出来。每条用例各用一个新 id，避免上一条用例遗留的半开连接影响配对。
 *
 *  @return 形如 "0000018f2a3b4c5d-0001" 的会话 id
 */
std::string MakeSessionId()
{
    static unsigned int s_seq = 0;
    ++s_seq;

    char buf[64] = { 0 };
    sprintf_s(buf, sizeof(buf), "%016llx-%04x",
              (unsigned long long)::GetTickCount64(), s_seq);
    return std::string(buf);
}

/**
 *  按给定角色签发一枚中转令牌。
 *  @param sessionId 会话 id，同一会话两端必须相同
 *  @param role      本端角色，取值见 RelayToken::role_*
 *  @param uid       本端用户 id
 *  @param peerUid   对端用户 id
 *  @return 令牌字符串
 */
std::string MakeToken(const std::string& sessionId, int role, int64_t uid, int64_t peerUid)
{
    RelayTokenPayload payload;
    payload.sessionid = sessionId;
    payload.role      = role;
    payload.uid       = uid;
    payload.peeruid   = peerUid;
    payload.expire    = static_cast<int64_t>(time(NULL)) * 1000 + kTokenTtlMs;

    return RelayToken::sign(payload, kRelayTokenKey);
}

/**
 *  造一段测试载荷。
 *
 *  刻意让它覆盖全部 256 种字节取值（含 0x00 与高位字节）：中转通道跑的是屏幕帧与
 *  音视频帧这类二进制数据，若某一环节把包体当成 C 字符串处理，0x00 处就会被截断，
 *  用纯文本载荷是测不出来的。
 *
 *  @param len 载荷长度，单位字节
 *  @return 载荷内容
 */
std::string MakePayload(size_t len)
{
    std::string s;
    s.resize(len);
    for (size_t i = 0; i < len; ++i)
        s[i] = static_cast<char>(i & 0xFF);
    return s;
}

/**
 *  从中转应答的 JSON 包体里取出 code 字段。
 *  @param json    应答包体（UTF-8 JSON 文本）
 *  @param codeOut [出参] 解析出的错误码，取值见 enum error_code
 *  @return 解析成功且含 code 字段返回 true
 */
bool ParseAckCode(const std::string& json, int& codeOut)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::Value root;

    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;

    if (!ok || !root.isObject() || !root["code"].isInt())
        return false;

    codeOut = root["code"].asInt();
    return true;
}

/**
 *  建连、走完三帧握手、再用令牌报到，一步到位。
 *
 *  @param conn      [出参] 本端连接，调用前须是未连接状态
 *  @param hs        [出参] 本端握手器，成功后其 cipher() 即为本端会话密钥
 *  @param helloCmd  报到帧的 cmd：远程桌面取 cmd_rd_relay_hello，音视频取 cmd_av_relay_hello
 *  @param sessionId 会话 id，同一会话两端相同
 *  @param role      本端角色，取值见 RelayToken::role_*
 *  @param codeOut   [出参] 报到应答里的错误码；只有走到应答这一步才有意义
 *  @param reason    [出参] 失败原因，便于断言失败时定位是哪一步挂的
 *  @return 一路走到「收到并解开了报到应答」返回 true；中途任何一步失败返回 false
 *  副作用：在 conn 上收发握手三帧与报到帧。
 */
bool JoinRelay(TestConn& conn, ClientHandshake& hs, uint16_t helloCmd,
               const std::string& sessionId, int role, int& codeOut, std::string& reason)
{
    codeOut = -1;
    reason.clear();

    if (!conn.Connect(kTestRelayPort))
    {
        reason = "connect relay port failed";
        return false;
    }

    if (!DoHandshake(conn, hs, reason))
        return false;

    if (!hs.cipher().ready())
    {
        reason = "cipher not ready after handshake";
        return false;
    }

    //令牌里的 uid / peeruid 按角色互换，使同一会话两端的令牌互为对方的对端。
    //imgserver 目前只按 sessionid 与 role 配对、不校验这两个字段，但把它们填对
    //可以让用例继续有效，将来服务端若加上校验也不必改这里
    const bool  isInitiator = (role == RelayToken::role_controller || role == RelayToken::role_caller);
    const int64_t uid       = isInitiator ? kUidA : kUidB;
    const int64_t peerUid   = isInitiator ? kUidB : kUidA;

    //报到帧的包体是 JSON，只带一个 token 字段；服务端按普通业务帧还原（解密 + 解压），
    //故这里走完整的包体管线。中转帧一律不压缩，与产品代码里的中转客户端保持一致
    const std::string token = MakeToken(sessionId, role, uid, peerUid);
    const std::string body  = "{\"token\":\"" + token + "\"}";

    std::string wire;
    uint8_t     flags = 0;
    uint32_t    originlen = 0;
    if (!encodeFramePayload(helloCmd, kHelloSeq, body, &hs.cipher(), 0,
                            payload_compress_never, wire, flags, originlen))
    {
        reason = "encode hello payload failed";
        return false;
    }

    std::string frame;
    if (!encodeFrame(helloCmd, kHelloSeq, flags, wire.data(),
                     (uint32_t)wire.size(), originlen, frame))
    {
        reason = "encode hello frame failed";
        return false;
    }

    if (!conn.SendAll(frame))
    {
        reason = "send hello frame failed";
        return false;
    }

    frame_header header;
    std::string respWire;
    if (!conn.RecvFrame(header, respWire, MAX_BODY_FILESERVER))
    {
        reason = "no hello ack (server closed the connection?)";
        return false;
    }

    std::string plain;
    if (!decodeFramePayload(header.cmd, header.seq, header.flags, header.originlen,
                            respWire, &hs.cipher(), plain, reason))
    {
        return false;
    }

    if (!ParseAckCode(plain, codeOut))
    {
        reason = "hello ack is not a JSON object with a code field: " + plain;
        return false;
    }
    return true;
}

/**
 *  在一端上发一个中转数据帧。
 *
 *  只加密、不压缩，与产品代码里的中转客户端（RdRelayClient / CallRelayClient）
 *  走的是同一条路径：屏幕帧与音视频帧已由上层编码过，再走一遍 zlib 既费 CPU
 *  又压不动，originlen 也另有来源。
 *
 *  @param conn      本端连接
 *  @param hs        本端握手器，用其 cipher() 加密
 *  @param cmd       数据帧的 cmd：cmd_rd_relay_data 或 cmd_av_relay_data
 *  @param seq       本帧序列号，服务端会原样转给对端
 *  @param srcFlags  调用方要求带上的帧标志位（典型是 frame_flag_compressed）；
 *                   加密位由 encryptFramePayload 自行置上，不必传入
 *  @param originlen 写进帧头的「压缩前原始长度」，服务端原样透传
 *  @param body      待发送的包体（已是最终形态，不再压缩）
 *  @param wireOut   [出参] 本端产出的线上密文包体，供与对端收到的密文比对
 *  @return 帧已完整写进 socket 返回 true
 *  副作用：在 conn 上发出一帧。
 */
bool SendRelayData(TestConn& conn, ClientHandshake& hs, uint16_t cmd, uint32_t seq,
                   uint8_t srcFlags, uint32_t originlen, const std::string& body,
                   std::string& wireOut)
{
    wireOut.clear();

    uint8_t flags = 0;
    if (!encryptFramePayload(cmd, seq, srcFlags, originlen, body, &hs.cipher(), wireOut, flags))
        return false;

    std::string frame;
    if (!encodeFrame(cmd, seq, flags, wireOut.data(), (uint32_t)wireOut.size(), originlen, frame))
        return false;

    return conn.SendAll(frame);
}

/**
 *  建立一对已配对的中转连接：两端各自握手，再用同一 sessionid、相反角色的令牌报到。
 *
 *  断言直接写在本函数里，任何一步不成立就地失败，并把 JoinRelay 回带的失败原因一起
 *  报出来——「报到没成功」的原因有握手挂了、令牌签错了、密钥不一致等好几种，光看
 *  一个 false 分不出是哪一种。
 *
 *  @param connA     [出参] A 端连接，调用前须是未连接状态
 *  @param hsA       [出参] A 端握手器
 *  @param roleA     A 端角色，取值见 RelayToken::role_*
 *  @param connB     [出参] B 端连接，调用前须是未连接状态
 *  @param hsB       [出参] B 端握手器
 *  @param roleB     B 端角色，须与 roleA 不同，否则服务端按重复角色拒绝
 *  @param helloCmd  报到帧的 cmd：cmd_rd_relay_hello 或 cmd_av_relay_hello
 *  @param sessionId 会话 id，两端相同
 *  副作用：在两条连接上各收发握手三帧与报到帧一来一回。
 */
void RequirePairedEndpoints(TestConn& connA, ClientHandshake& hsA, int roleA,
                            TestConn& connB, ClientHandshake& hsB, int roleB,
                            uint16_t helloCmd, const std::string& sessionId)
{
    int         codeA = -1;
    std::string reasonA;
    const bool  joinedA = JoinRelay(connA, hsA, helloCmd, sessionId, roleA, codeA, reasonA);
    INFO("endpoint A (role " << roleA << ") join failed: " << reasonA);
    REQUIRE(joinedA);
    REQUIRE(codeA == error_code_ok);

    int         codeB = -1;
    std::string reasonB;
    const bool  joinedB = JoinRelay(connB, hsB, helloCmd, sessionId, roleB, codeB, reasonB);
    INFO("endpoint B (role " << roleB << ") join failed: " << reasonB);
    REQUIRE(joinedB);
    REQUIRE(codeB == error_code_ok);
}

} // anonymous namespace

TEST_CASE("RELAY1 一对角色相反的令牌能让两端在同一会话上报到成功", "[.live][relay]")
{
    REQUIRE(cryptoInit());

    // 这一条只验配对的前置条件：两端各自握手、各自用同一 sessionid、角色相反的
    // 令牌报到，服务端都应回 error_code_ok。它同时说明测试自己签出来的令牌是
    // 合法的——HSLIVE7 用的是随便编的令牌，验的是被拦下，两条互为对照。
    const std::string sessionId = MakeSessionId();

    TestConn        connA;
    ClientHandshake hsA;
    int             codeA = -1;
    std::string     reasonA;
    const bool      joinedA = JoinRelay(connA, hsA, cmd_rd_relay_hello, sessionId,
                                        RelayToken::role_controlled, codeA, reasonA);
    INFO("endpoint A join failed: " << reasonA);
    REQUIRE(joinedA);
    CHECK(codeA == error_code_ok);

    TestConn        connB;
    ClientHandshake hsB;
    int             codeB = -1;
    std::string     reasonB;
    const bool      joinedB = JoinRelay(connB, hsB, cmd_rd_relay_hello, sessionId,
                                        RelayToken::role_controller, codeB, reasonB);
    INFO("endpoint B join failed: " << reasonB);
    REQUIRE(joinedB);
    CHECK(codeB == error_code_ok);
}

TEST_CASE("RELAY2 A 端发出的数据帧经服务端换密钥后 B 端能解出逐字节相同的明文", "[.live][relay]")
{
    REQUIRE(cryptoInit());

    // 这是本组用例的核心。它一次证明三件事：服务端能用 A 的密钥解开包体、能用 B 的
    // 密钥重新加密、且中途一个字节都没改。任何一步不成立，B 这边要么解不开、要么
    // 解出来的内容对不上。
    const std::string sessionId = MakeSessionId();

    TestConn        connA;
    ClientHandshake hsA;
    TestConn        connB;
    ClientHandshake hsB;
    RequirePairedEndpoints(connA, hsA, RelayToken::role_controlled,
                           connB, hsB, RelayToken::role_controller,
                           cmd_rd_relay_hello, sessionId);

    const std::string payload = MakePayload(kPayloadBytes);

    // 与产品代码里的中转客户端一致：originlen 取包体本身的长度、不置压缩位
    std::string wireA;
    REQUIRE(SendRelayData(connA, hsA, cmd_rd_relay_data, kSeqAtoB, 0,
                          (uint32_t)payload.size(), payload, wireA));

    frame_header header;
    std::string  wireB;
    REQUIRE(connB.RecvFrame(header, wireB, MAX_BODY_FILESERVER));

    // 帧头三件套应当原样转过来：服务端只换密钥，不改 cmd / seq / originlen
    CHECK(header.cmd == cmd_rd_relay_data);
    CHECK(header.seq == kSeqAtoB);
    CHECK(header.originlen == (uint32_t)payload.size());
    CHECK((header.flags & frame_flag_encrypted) != 0);

    // 密文必须不同。两条连接的会话密钥各不相同，服务端若退回成字节透传，这里就会相等——
    // 这一条是「确实做了解密 + 重新加密」的反面证据
    CHECK(wireB != wireA);
    // 长度则应当相同：明文一样长，流加密的固定开销也一样
    CHECK(wireB.size() == wireA.size());

    // 中转数据帧走「只解密不解压」的路径，与产品代码里的中转客户端一致
    std::string plain;
    std::string err;
    INFO("decrypt error: " << err);
    REQUIRE(decryptFramePayload(header.cmd, header.seq, header.flags, header.originlen,
                                wireB, &hsB.cipher(), plain, err));

    // 逐字节比对。先比长度再比内容，断言失败时输出的是长度而不是一千字节的二进制
    REQUIRE(plain.size() == payload.size());
    CHECK(memcmp(plain.data(), payload.data(), payload.size()) == 0);
}

TEST_CASE("RELAY3 反方向 B 端发出的数据帧 A 端同样能解出原样明文", "[.live][relay]")
{
    REQUIRE(cryptoInit());

    // 转发是双向的，两个方向各用一套流密钥状态，必须分别验。只验一个方向的话，
    // 反方向取错密钥（比如两个方向都用了发送流）这类错误照样漏得过去。
    const std::string sessionId = MakeSessionId();

    TestConn        connA;
    ClientHandshake hsA;
    TestConn        connB;
    ClientHandshake hsB;
    RequirePairedEndpoints(connA, hsA, RelayToken::role_controlled,
                           connB, hsB, RelayToken::role_controller,
                           cmd_rd_relay_hello, sessionId);

    const std::string payload = MakePayload(kPayloadBytes);

    std::string wireB;
    REQUIRE(SendRelayData(connB, hsB, cmd_rd_relay_data, kSeqBtoA, 0,
                          (uint32_t)payload.size(), payload, wireB));

    frame_header header;
    std::string  wireA;
    REQUIRE(connA.RecvFrame(header, wireA, MAX_BODY_FILESERVER));

    CHECK(header.cmd == cmd_rd_relay_data);
    CHECK(header.seq == kSeqBtoA);
    CHECK(header.originlen == (uint32_t)payload.size());
    CHECK((header.flags & frame_flag_encrypted) != 0);
    CHECK(wireA != wireB);

    std::string plain;
    std::string err;
    INFO("decrypt error: " << err);
    REQUIRE(decryptFramePayload(header.cmd, header.seq, header.flags, header.originlen,
                                wireA, &hsA.cipher(), plain, err));

    REQUIRE(plain.size() == payload.size());
    CHECK(memcmp(plain.data(), payload.data(), payload.size()) == 0);
}

TEST_CASE("RELAY4 压缩标志与 originlen 经服务端转发后原样保留", "[.live][relay]")
{
    REQUIRE(cryptoInit());

    // 压缩是两端客户端之间的语义：服务端既不解压也不重压，压缩标志与 originlen
    // 都必须原样透传，否则对端客户端拿到的 originlen 对不上、解压必然失败。
    // 这里刻意置上压缩位、并把 originlen 填成一个与包体长度无关的值——服务端若
    // 顺手"纠正"了它们，这条就会失败。
    const std::string sessionId = MakeSessionId();

    TestConn        connA;
    ClientHandshake hsA;
    TestConn        connB;
    ClientHandshake hsB;
    RequirePairedEndpoints(connA, hsA, RelayToken::role_controlled,
                           connB, hsB, RelayToken::role_controller,
                           cmd_rd_relay_hello, sessionId);

    const std::string payload = MakePayload(kPayloadBytes);

    std::string wireA;
    REQUIRE(SendRelayData(connA, hsA, cmd_rd_relay_data, kSeqAtoB,
                          (uint8_t)frame_flag_compressed, kFakeOriginLen, payload, wireA));

    frame_header header;
    std::string  wireB;
    REQUIRE(connB.RecvFrame(header, wireB, MAX_BODY_FILESERVER));

    CHECK((header.flags & frame_flag_compressed) != 0);
    CHECK((header.flags & frame_flag_encrypted) != 0);
    CHECK(header.originlen == kFakeOriginLen);

    // 包体本身当然也要能解开并对得上。这里走「只解密不解压」——服务端没解压过，
    // 对端拿到的仍是发送端压缩后的那一层，是否解压由对端客户端自己决定
    std::string plain;
    std::string err;
    INFO("decrypt error: " << err);
    REQUIRE(decryptFramePayload(header.cmd, header.seq, header.flags, header.originlen,
                                wireB, &hsB.cipher(), plain, err));

    REQUIRE(plain.size() == payload.size());
    CHECK(memcmp(plain.data(), payload.data(), payload.size()) == 0);
}

TEST_CASE("RELAY5 音视频中转的两端同样能配对并双向转发", "[.live][relay]")
{
    REQUIRE(cryptoInit());

    // 音视频与远程桌面共用同一份服务端实现，只是 cmd 与角色编号不同
    // （cmd_av_relay_* / role_caller、role_callee）。分派那里若漏了 av 这一档，
    // 帧会被当成"不支持的 cmd"直接断连，只测远程桌面是发现不了的。
    const std::string sessionId = MakeSessionId();

    TestConn        connCaller;
    ClientHandshake hsCaller;
    TestConn        connCallee;
    ClientHandshake hsCallee;
    RequirePairedEndpoints(connCaller, hsCaller, RelayToken::role_caller,
                           connCallee, hsCallee, RelayToken::role_callee,
                           cmd_av_relay_hello, sessionId);

    const std::string payload = MakePayload(kPayloadBytes);

    //主叫 → 被叫
    std::string wireOut;
    REQUIRE(SendRelayData(connCaller, hsCaller, cmd_av_relay_data, kSeqAtoB, 0,
                          (uint32_t)payload.size(), payload, wireOut));

    frame_header header;
    std::string  wireIn;
    REQUIRE(connCallee.RecvFrame(header, wireIn, MAX_BODY_FILESERVER));
    CHECK(header.cmd == cmd_av_relay_data);
    CHECK(header.seq == kSeqAtoB);
    CHECK(wireIn != wireOut);

    std::string plain;
    std::string err;
    INFO("decrypt error: " << err);
    REQUIRE(decryptFramePayload(header.cmd, header.seq, header.flags, header.originlen,
                                wireIn, &hsCallee.cipher(), plain, err));
    REQUIRE(plain.size() == payload.size());
    CHECK(memcmp(plain.data(), payload.data(), payload.size()) == 0);

    //被叫 → 主叫
    REQUIRE(SendRelayData(connCallee, hsCallee, cmd_av_relay_data, kSeqBtoA, 0,
                          (uint32_t)payload.size(), payload, wireOut));

    REQUIRE(connCaller.RecvFrame(header, wireIn, MAX_BODY_FILESERVER));
    CHECK(header.cmd == cmd_av_relay_data);
    CHECK(header.seq == kSeqBtoA);

    plain.clear();
    REQUIRE(decryptFramePayload(header.cmd, header.seq, header.flags, header.originlen,
                                wireIn, &hsCaller.cipher(), plain, err));
    REQUIRE(plain.size() == payload.size());
    CHECK(memcmp(plain.data(), payload.data(), payload.size()) == 0);
}
