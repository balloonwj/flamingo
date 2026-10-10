/**
 *  HandshakeLiveTests.cpp
 *
 *  协议加密的<u>跨实现联调</u>测试：用客户端这一份实现，去连一个真实运行的
 *  chatserver，走完三帧握手并收发一帧加密业务帧。
 *
 *  为什么必须有这一组用例：客户端与服务端的加密是两份互为镜像的实现
 *  （flamingoclient/Source/net/FrameCrypto.* 与 flamingoserver/net/FrameCrypto.*），
 *  两边的单元测试各自只能证明"自己这一半自洽"，**证明不了两边能互通**。而互通失败
 *  的典型原因——握手记录的拼接顺序不同、附加认证数据的字节布局差一位、base64 变体
 *  不一致——全都是各自测试测不出来的。这类问题只有让两份实现真的对话一次才会暴露。
 *
 *  两份实现在同一个进程里链接不到一起（同名 namespace 下有同名的 FrameCipher、
 *  buildFrameAd 等符号，会重复定义），所以只能走真实 socket。
 *
 *  <u>默认不参与常规测试</u>：本文件的用例都带 [.live] 标签，Catch2 会跳过它们，
 *  因为它们需要一个正在运行的服务端。要跑就显式指定标签：
 *
 *      FlamingoUnit.exe "[live]"
 *
 *  跑之前需要准备一个独立的服务端实例（不要用开发中正在联调的那个，端口会撞）：
 *    1. 把 flamingoserver/windowsproj/x64/Debug/chatserver.exe 复制到一个空目录；
 *    2. 在它的上一级建 etc/chatserver.conf，把 listenport 改成 kTestServerPort、
 *       encryption 设为 1、serverkeyfile 指向服务端真正在用的那份密钥；
 *    3. 起它，再用 kTestServerPubKey 填上该密钥对应的公钥（server_key.pub 的内容）。
 *  服务端换过密钥之后，下面那个公钥常量要跟着换，否则验签必然失败。
 *
 *  balloonwj@qq.com   2026-08-13
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

//本文件要自己造一把假的身份密钥来验证验签，用到 Ed25519 的原语与尺寸常量。
//客户端的 FrameCrypto.h 只导出用得着的那几个尺寸（它不持有身份私钥），
//故这里直接用 libsodium 自己的常量。
#include <sodium.h>

//连接、按帧收发与三帧握手的脚手架，以及各测试实例的端口与身份公钥常量，
//与其它联调用例文件（RelayForwardLiveTests.cpp）共用同一份
#include "LiveTestConn.h"

#include "../net/FrameCodec.h"
#include "../net/FrameCrypto.h"
#include "../net/FramePayload.h"
#include "../net/HandshakeRunner.h"

using namespace net;
using namespace livetest;

namespace {

//本测试里发出的帧统一用这个序列号。服务端会原样回填到应答里，便于核对
const uint32_t kTestSeq = 7;

} // anonymous namespace

TEST_CASE("HSLIVE1 客户端侧走完三帧握手", "[.live][handshake]")
{
    REQUIRE(cryptoInit());

    TestConn conn;
    REQUIRE(conn.Connect(kTestServerPort));

    ClientHandshake hs;
    std::string reason;
    INFO("handshake failure reason: " << reason);
    REQUIRE(DoHandshake(conn, hs, reason));

    // 握手走通之后两个方向的流都该就绪，否则后面每一帧都会失败
    CHECK(hs.ready());
    CHECK(hs.cipher().ready());

    // 注意这条用例<u>证明不了两端真的握上手了</u>：第 3 帧没有应答，客户端把它发出去
    // 就自认为完成，服务端认不认这一帧在这里根本看不出来。开发中就踩过——第 3 帧因为
    // 缓冲区复用被拼在了 ClientHello 后面，服务端一路报错，本用例却照样全绿。
    // 真正验证互通的是下面的 HSLIVE2：只有服务端解得开我们的密文、我们也解得开它的，
    // 那一条才会通过。
}

TEST_CASE("HSLIVE2 握手后的加密业务帧能被服务端正确解出", "[.live][handshake]")
{
    REQUIRE(cryptoInit());

    TestConn conn;
    REQUIRE(conn.Connect(kTestServerPort));

    ClientHandshake hs;
    std::string reason;
    REQUIRE(DoHandshake(conn, hs, reason));

    // 发一个加密的心跳帧。选心跳是因为它包体为空、服务端无条件回一帧，
    // 不需要登录态，最适合用来验证"加密通道确实通了"。
    std::string wire;
    uint8_t     flags = 0;
    uint32_t    originlen = 0;
    REQUIRE(encodeFramePayload(cmd_heartbeat, kTestSeq, std::string(), &hs.cipher(), 0,
                               payload_compress_never, wire, flags, originlen));

    // 空包体加密之后应当只剩固定开销那么长，并且加密位必须被置上
    CHECK(wire.size() == STREAM_OVERHEAD_BYTES);
    CHECK((flags & frame_flag_encrypted) != 0);
    CHECK(originlen == 0);

    std::string frame;
    REQUIRE(encodeFrame(cmd_heartbeat, kTestSeq, flags, wire.data(),
                        (uint32_t)wire.size(), originlen, frame));
    REQUIRE(conn.SendAll(frame));

    // 服务端解得开就会回一帧心跳应答；解不开则会直接关连接，这里会收不到东西。
    // 也就是说这一条断言同时验证了"服务端能解我们的密文"。
    frame_header header;
    std::string respWire;
    REQUIRE(conn.RecvFrame(header, respWire));
    CHECK(header.cmd == cmd_heartbeat);

    // 反过来验证"我们能解服务端的密文"
    CHECK((header.flags & frame_flag_encrypted) != 0);

    std::string plain;
    std::string err;
    INFO("decode error: " << err);
    REQUIRE(decodeFramePayload(header.cmd, header.seq, header.flags, header.originlen,
                               respWire, &hs.cipher(), plain, err));
}

TEST_CASE("HSLIVE3 未握手就发业务帧会被服务端拒绝", "[.live][handshake]")
{
    REQUIRE(cryptoInit());

    TestConn conn;
    REQUIRE(conn.Connect(kTestServerPort));

    // 跳过握手直接发明文心跳。服务端开着加密时必须拒绝并关连接——若它照常应答，
    // 说明"握手完成前只收握手帧"这道闸门没生效，加密等于可以被绕开。
    std::string frame;
    REQUIRE(encodeFrame(cmd_heartbeat, kTestSeq, frame));
    REQUIRE(conn.SendAll(frame));

    frame_header header;
    std::string body;
    CHECK_FALSE(conn.RecvFrame(header, body));
}

TEST_CASE("HSLIVE4 换一把公钥验签必须失败", "[.live][handshake]")
{
    REQUIRE(cryptoInit());

    TestConn conn;
    REQUIRE(conn.Connect(kTestServerPort));

    // 自己造一把与服务端无关的身份公钥，冒充"客户端配错了公钥"或"中间人换了服务器"。
    // 这一步失败才说明验签真的在起作用——这正是中间人插不进来的唯一依据。
    unsigned char fakePk[crypto_sign_PUBLICKEYBYTES] = { 0 };
    unsigned char fakeSk[crypto_sign_SECRETKEYBYTES] = { 0 };
    unsigned char seed[crypto_sign_SEEDBYTES] = { 0 };
    randombytes_buf(seed, sizeof(seed));
    const int genRet = crypto_sign_seed_keypair(fakePk, fakeSk, seed);
    REQUIRE(genRet == 0);

    ClientHandshake hs;
    const std::string hello = hs.buildClientHello();
    REQUIRE_FALSE(hello.empty());

    std::string frame;
    REQUIRE(encodeFrame(cmd_handshake, kTestSeq, hello, frame));
    REQUIRE(conn.SendAll(frame));

    frame_header header;
    std::string respBody;
    REQUIRE(conn.RecvFrame(header, respBody));
    REQUIRE(header.cmd == cmd_handshake);

    std::string err;
    CHECK_FALSE(hs.onServerHello(respBody, fakePk, err));
    CHECK_FALSE(err.empty());
    CHECK_FALSE(hs.ready());
}

TEST_CASE("HSLIVE5 与真实 fileserver 走通握手并收发加密帧", "[.live][handshake]")
{
    REQUIRE(cryptoInit());

    // fileserver 与 imgserver 共用同一套会话实现（服务端 fileserversrc/FileSession），
    // 所以这一条同时覆盖了图片链路的服务端侧。客户端那边文件与图片是两套收发代码，
    // 各自的接入仍要分别验。
    TestConn conn;
    REQUIRE(conn.Connect(kTestFileServerPort));

    ClientHandshake hs;
    std::string reason;
    INFO("handshake failure reason: " << reason);
    REQUIRE(DoHandshake(conn, hs, reason));
    REQUIRE(hs.cipher().ready());

    // 发一个加密的下载请求帧。故意用一个不存在的令牌：这里要验的是"服务端能不能
    // 解开我们的密文"，而不是下载本身能不能成功——只要它回了一帧（哪怕是错误应答），
    // 就说明包体解密成功并进了业务分发；解不开的话服务端会直接关连接、这里收不到东西。
    const std::string reqBody = "{\"downloadtoken\":\"live-test-invalid-token\",\"offset\":0}";
    std::string wire;
    uint8_t     flags = 0;
    uint32_t    originlen = 0;
    REQUIRE(encodeFramePayload(cmd_file_download, kTestSeq, reqBody, &hs.cipher(), 0,
                               payload_compress_never, wire, flags, originlen));
    CHECK((flags & frame_flag_encrypted) != 0);
    CHECK(wire.size() == reqBody.size() + STREAM_OVERHEAD_BYTES);
    CHECK(originlen == reqBody.size());

    std::string frame;
    REQUIRE(encodeFrame(cmd_file_download, kTestSeq, flags, wire.data(),
                        (uint32_t)wire.size(), originlen, frame));
    REQUIRE(conn.SendAll(frame));

    frame_header header;
    std::string respWire;
    REQUIRE(conn.RecvFrame(header, respWire));

    // 反过来验证"我们能解服务端的密文"
    CHECK((header.flags & frame_flag_encrypted) != 0);

    std::string plain;
    std::string err;
    INFO("decode error: " << err);
    REQUIRE(decodeFramePayload(header.cmd, header.seq, header.flags, header.originlen,
                               respWire, &hs.cipher(), plain, err));
    // 令牌是无效的，服务端应当回一个带错误码的应答，而不是一个空包体
    CHECK_FALSE(plain.empty());
}

TEST_CASE("HSLIVE6 fileserver 上未握手就发业务帧会被拒绝", "[.live][handshake]")
{
    REQUIRE(cryptoInit());

    TestConn conn;
    REQUIRE(conn.Connect(kTestFileServerPort));

    // 跳过握手直接发明文下载请求。服务端开着加密时必须拒绝并关连接，
    // 否则"握手完成前只收握手帧"这道闸门在文件链路上就是失效的。
    const std::string reqBody = "{\"downloadtoken\":\"x\",\"offset\":0}";
    std::string frame;
    REQUIRE(encodeFrame(cmd_file_download, kTestSeq, reqBody, frame));
    REQUIRE(conn.SendAll(frame));

    frame_header header;
    std::string body;
    CHECK_FALSE(conn.RecvFrame(header, body));
}

TEST_CASE("HSLIVE7 中转通道走通握手并被令牌校验拦下", "[.live][handshake]")
{
    REQUIRE(cryptoInit());

    // 中转通道的服务端实现（RelaySession）与文件 / 图片那条不同：数据帧要"用本端
    // 密钥解开、再用配对端密钥重新加密"，报到帧则要自己读包体。这一条验的是
    // 握手与报到帧这两步——真正的双端转发要两个客户端配对，留给整体联调。
    TestConn conn;
    REQUIRE(conn.Connect(kTestRelayPort));

    ClientHandshake hs;
    std::string reason;
    INFO("handshake failure reason: " << reason);
    REQUIRE(DoHandshake(conn, hs, reason));
    REQUIRE(hs.cipher().ready());

    // 发一个加密的报到帧。令牌是随便编的，服务端验签必然不过——这里要的正是
    // 这个结果：它说明服务端把包体解开了、认出这是报到帧、并走到了令牌校验那一步。
    // 若包体解不开，服务端会在更早的地方就断开，我们根本收不到应答。
    const std::string helloBody = "{\"token\":\"live-test-invalid-relay-token\"}";
    std::string wire;
    uint8_t     flags = 0;
    uint32_t    originlen = 0;
    REQUIRE(encodeFramePayload(cmd_rd_relay_hello, 1, helloBody, &hs.cipher(), 0,
                               payload_compress_never, wire, flags, originlen));
    CHECK((flags & frame_flag_encrypted) != 0);

    std::string frame;
    REQUIRE(encodeFrame(cmd_rd_relay_hello, 1, flags, wire.data(),
                        (uint32_t)wire.size(), originlen, frame));
    REQUIRE(conn.SendAll(frame));

    frame_header header;
    std::string respWire;
    REQUIRE(conn.RecvFrame(header, respWire));
    CHECK(header.cmd == cmd_rd_relay_hello);
    CHECK((header.flags & frame_flag_encrypted) != 0);

    // 反过来验证"我们能解服务端的密文"
    std::string plain;
    std::string err;
    INFO("decode error: " << err);
    REQUIRE(decodeFramePayload(header.cmd, header.seq, header.flags, header.originlen,
                               respWire, &hs.cipher(), plain, err));
    // 令牌无效，应答里应当带着非零错误码
    CHECK_FALSE(plain.empty());
    CHECK(plain.find("code") != std::string::npos);
}

TEST_CASE("HSLIVE8 中转通道上未握手就发报到帧会被拒绝", "[.live][handshake]")
{
    REQUIRE(cryptoInit());

    TestConn conn;
    REQUIRE(conn.Connect(kTestRelayPort));

    // 跳过握手直接发明文报到帧。中转通道最该防的就是这个——报到帧里带着中转令牌，
    // 若服务端在未握手时也肯收，令牌就会以明文出现在网络上。
    const std::string helloBody = "{\"token\":\"x\"}";
    std::string frame;
    REQUIRE(encodeFrame(cmd_rd_relay_hello, 1, helloBody, frame));
    REQUIRE(conn.SendAll(frame));

    frame_header header;
    std::string body;
    CHECK_FALSE(conn.RecvFrame(header, body));
}
