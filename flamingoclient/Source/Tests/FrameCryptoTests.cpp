/**
 *  FrameCryptoTests.cpp
 *
 *  v2 协议帧体加密的客户端侧测试。设计见 docs/协议加密方案.md。
 *
 *  这里测的是<u>客户端这一份实现</u>：握手状态机的时序约束、验签能否挡住中间人、
 *  帧体加解密的往返一致性、以及篡改后必须失败。服务端那一份是镜像实现，两侧能否
 *  真正互通<u>必须靠联调验证</u>——单测只能保证各自这一半自洽。
 *
 *  之所以仍要写这些用例：加密代码出错时最危险的表现是「看着在工作、实际不安全」，
 *  比如验签没生效、篡改的包照样解开。这类问题不会自己暴露，只能靠断言钉住。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不建连接、不碰窗口。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-08-13
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include <sodium.h>

#include "../third-party/jsoncpp-1.9.0/json.h"   // 测试里就地拼装 ServerHello，需要它
#include "../net/FrameCrypto.h"

using namespace net;

namespace {

//测试里模拟服务端所需的最小材料：一把身份密钥 + 一次握手应答。
//客户端实现里没有服务端侧的类，故这里就地用 libsodium 拼一个「假服务端」，
//它必须与 flamingoserver/net/FrameCrypto.cpp 的格式完全一致 —— 这份拼装本身
//就是对「两侧格式约定」的一次复述，格式一改这里就会失败，起到提醒作用。
struct FakeServer
{
    unsigned char idPk[IDENTITY_PUBLICKEY_BYTES];
    unsigned char idSk[64];                       //crypto_sign_SECRETKEYBYTES
    unsigned char ePk[KX_PUBLICKEY_BYTES];
    unsigned char eSk[KX_SECRETKEY_BYTES];
    unsigned char rxKey[KX_SESSIONKEY_BYTES];
    unsigned char txKey[KX_SESSIONKEY_BYTES];
    unsigned char nonce[HANDSHAKE_NONCE_BYTES];
    std::string   streamHeader;
    crypto_secretstream_xchacha20poly1305_state pushState;
    crypto_secretstream_xchacha20poly1305_state pullState;

    FakeServer()
    {
        crypto_sign_keypair(idPk, idSk);
    }
};

//把 Json::Value 序列化为紧凑（无缩进、无换行）的 JSON 文本。
//写法与产品代码 net/FrameCrypto.cpp、以及服务端那一份镜像实现保持一致 ——
//旧的 Json::FastWriter 已被 jsoncpp 标记为废弃，编译时会产生 C4996 告警。
std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//解析 JSON 文本。同样只能用新版 CharReader 接口，其默认容错与旧 Json::Reader 等价。
bool parseJson(const std::string& text, Json::Value& out)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &out, &errs);
    delete reader;
    return ok && errs.empty();
}

std::string toB64(const unsigned char* p, size_t n)
{
    const size_t len = sodium_base64_ENCODED_LEN(n, sodium_base64_VARIANT_ORIGINAL);
    std::vector<char> buf(len, 0);
    sodium_bin2base64(&buf[0], len, p, n, sodium_base64_VARIANT_ORIGINAL);
    return std::string(&buf[0]);
}

bool fromB64(const std::string& s, size_t expect, unsigned char* out)
{
    size_t real = 0;
    if (sodium_base642bin(out, expect, s.c_str(), s.size(), NULL, &real, NULL,
                          sodium_base64_VARIANT_ORIGINAL) != 0)
    {
        return false;
    }
    return real == expect;
}

//握手记录的拼接，必须与两侧实现逐字节一致。
std::string transcript(const unsigned char* cpk, const unsigned char* cnonce,
                       const unsigned char* spk, const unsigned char* snonce,
                       const unsigned char* shdr)
{
    std::string t;
    t.append(1, (char)HANDSHAKE_VERSION);
    t.append((const char*)cpk, KX_PUBLICKEY_BYTES);
    t.append((const char*)cnonce, HANDSHAKE_NONCE_BYTES);
    t.append((const char*)spk, KX_PUBLICKEY_BYTES);
    t.append((const char*)snonce, HANDSHAKE_NONCE_BYTES);
    t.append((const char*)shdr, STREAM_HEADER_BYTES);
    return t;
}

//按客户端发来的 hello 造一份 ServerHello。signWith 为 NULL 时用自己的身份私钥签，
//否则用传入的私钥签 —— 后者用来模拟中间人（拿着别的密钥冒充服务端）。
std::string makeServerHello(FakeServer& s, const std::string& helloJson,
                            const unsigned char* signWith = NULL)
{
    Json::Value hello;
    REQUIRE(parseJson(helloJson, hello));

    unsigned char cpk[KX_PUBLICKEY_BYTES] = { 0 };
    unsigned char cnonce[HANDSHAKE_NONCE_BYTES] = { 0 };
    REQUIRE(fromB64(hello["cpk"].asString(), KX_PUBLICKEY_BYTES, cpk));
    REQUIRE(fromB64(hello["cnonce"].asString(), HANDSHAKE_NONCE_BYTES, cnonce));

    crypto_kx_keypair(s.ePk, s.eSk);
    REQUIRE(crypto_kx_server_session_keys(s.rxKey, s.txKey, s.ePk, s.eSk, cpk) == 0);

    unsigned char hdr[STREAM_HEADER_BYTES] = { 0 };
    REQUIRE(crypto_secretstream_xchacha20poly1305_init_push(&s.pushState, hdr, s.txKey) == 0);
    s.streamHeader.assign((const char*)hdr, STREAM_HEADER_BYTES);

    randombytes_buf(s.nonce, sizeof(s.nonce));

    const std::string t = transcript(cpk, cnonce, s.ePk, s.nonce, hdr);
    unsigned char sig[IDENTITY_SIGNATURE_BYTES] = { 0 };
    unsigned long long siglen = 0;
    crypto_sign_detached(sig, &siglen, (const unsigned char*)t.data(), t.size(),
                         signWith != NULL ? signWith : s.idSk);

    Json::Value resp;
    resp["code"]   = 0;
    resp["spk"]    = toB64(s.ePk, KX_PUBLICKEY_BYTES);
    resp["snonce"] = toB64(s.nonce, HANDSHAKE_NONCE_BYTES);
    resp["sig"]    = toB64(sig, (size_t)siglen);
    resp["shdr"]   = toB64(hdr, STREAM_HEADER_BYTES);

    return writeCompact(resp);
}

//让假服务端吃下客户端的流头，从而能解客户端发来的帧。
void acceptClientHeader(FakeServer& s, const std::string& chdrJson)
{
    Json::Value root;
    REQUIRE(parseJson(chdrJson, root));

    unsigned char hdr[STREAM_HEADER_BYTES] = { 0 };
    REQUIRE(fromB64(root["chdr"].asString(), STREAM_HEADER_BYTES, hdr));
    REQUIRE(crypto_secretstream_xchacha20poly1305_init_pull(&s.pullState, hdr, s.rxKey) == 0);
}

//典型的附加认证数据：由帧头字段拼出的固定 16 字节。
std::vector<unsigned char> makeAd(unsigned char fill = 0xAB)
{
    std::vector<unsigned char> ad(FRAME_AD_BYTES, fill);
    return ad;
}

} // anonymous namespace

// =====================================================================
// FC1-FC3. 初始化与配置解析
// =====================================================================

TEST_CASE("FC1 密码学库可初始化", "[framecrypto]")
{
    CHECK(cryptoInit());
    CHECK(cryptoInit());   // 重复调用必须安全
}

TEST_CASE("FC2 服务端公钥解析：合法值通过", "[framecrypto][config]")
{
    REQUIRE(cryptoInit());

    unsigned char pk[IDENTITY_PUBLICKEY_BYTES] = { 0 };
    unsigned char sk[64] = { 0 };
    crypto_sign_keypair(pk, sk);

    unsigned char parsed[IDENTITY_PUBLICKEY_BYTES] = { 0 };
    CHECK(parseServerPublicKey(toB64(pk, sizeof(pk)), parsed));
    CHECK(memcmp(pk, parsed, sizeof(pk)) == 0);
}

TEST_CASE("FC3 服务端公钥解析：非法值一律拒绝", "[framecrypto][config]")
{
    REQUIRE(cryptoInit());
    unsigned char parsed[IDENTITY_PUBLICKEY_BYTES] = { 0 };

    // 未配置：必须失败，调用方据此拒绝连接，绝不能当成"不加密"放行
    CHECK_FALSE(parseServerPublicKey("", parsed));
    // 非 base64
    CHECK_FALSE(parseServerPublicKey("这不是base64!!!", parsed));
    // 长度不对（16 字节而非 32）
    unsigned char shortKey[16] = { 1, 2, 3 };
    CHECK_FALSE(parseServerPublicKey(toB64(shortKey, sizeof(shortKey)), parsed));
}

// =====================================================================
// FC3b. 附加认证数据的字节布局
// =====================================================================

TEST_CASE("FC3b 附加认证数据按固定大端布局拼出", "[framecrypto][ad]")
{
    // 这个布局是<u>两端约定</u>的一部分，服务端 buildFrameAd 必须产出完全相同的字节。
    // 这里把期望值逐字节写死，是为了让「有人顺手改了字段顺序或字节序」立刻暴露 ——
    // 否则症状是联调时每一帧都解不开，排查起来要绕很大一圈。
    unsigned char ad[FRAME_AD_BYTES] = { 0 };
    buildFrameAd(/*cmd*/ 0x1003, /*seq*/ 0x01020304, /*flags*/ 0x03,
                 /*bodylen*/ 0x000000FF, /*originlen*/ 0x0000ABCD, ad);

    const unsigned char expect[FRAME_AD_BYTES] = {
        0x10, 0x03,               // cmd 大端
        0x03,                     // flags
        0x00,                     // 占位
        0x01, 0x02, 0x03, 0x04,   // seq 大端
        0x00, 0x00, 0x00, 0xFF,   // bodylen 大端
        0x00, 0x00, 0xAB, 0xCD,   // originlen 大端
    };
    CHECK(memcmp(ad, expect, FRAME_AD_BYTES) == 0);
}

TEST_CASE("FC3c 帧头任一字段变化都会改变附加认证数据", "[framecrypto][ad]")
{
    // 只要某个字段没被纳进 AD，攻击者就能改它而不被发现。逐个字段验一遍。
    unsigned char base[FRAME_AD_BYTES] = { 0 };
    buildFrameAd(1003, 7, 0x02, 100, 80, base);

    unsigned char other[FRAME_AD_BYTES] = { 0 };

    buildFrameAd(1004, 7, 0x02, 100, 80, other);          // 换 cmd
    CHECK(memcmp(base, other, FRAME_AD_BYTES) != 0);

    buildFrameAd(1003, 8, 0x02, 100, 80, other);          // 换 seq
    CHECK(memcmp(base, other, FRAME_AD_BYTES) != 0);

    buildFrameAd(1003, 7, 0x03, 100, 80, other);          // 换 flags
    CHECK(memcmp(base, other, FRAME_AD_BYTES) != 0);

    buildFrameAd(1003, 7, 0x02, 101, 80, other);          // 换 bodylen
    CHECK(memcmp(base, other, FRAME_AD_BYTES) != 0);

    buildFrameAd(1003, 7, 0x02, 100, 81, other);          // 换 originlen
    CHECK(memcmp(base, other, FRAME_AD_BYTES) != 0);
}

// =====================================================================
// FC4-FC7. 握手
// =====================================================================

TEST_CASE("FC4 完整握手成功后可加解密", "[framecrypto][handshake]")
{
    REQUIRE(cryptoInit());

    FakeServer server;
    ClientHandshake hs;

    const std::string hello = hs.buildClientHello();
    REQUIRE_FALSE(hello.empty());
    CHECK_FALSE(hs.ready());

    std::string err;
    REQUIRE(hs.onServerHello(makeServerHello(server, hello), server.idPk, err));
    CHECK(err.empty());
    CHECK_FALSE(hs.ready());          // 还差第 3 帧

    const std::string chdr = hs.buildClientStreamHeader();
    REQUIRE_FALSE(chdr.empty());
    CHECK(hs.ready());
    CHECK(hs.cipher().ready());
}

TEST_CASE("FC5 验签失败：换一把身份密钥签名必须被拒", "[framecrypto][handshake]")
{
    REQUIRE(cryptoInit());

    // 这条是整个方案抗中间人的核心断言。中间人能截包、能自己跑一遍密钥交换，
    // 唯独拿不到服务端身份私钥；只要这里放行，加密就形同虚设。
    FakeServer server;
    unsigned char evilPk[IDENTITY_PUBLICKEY_BYTES] = { 0 };
    unsigned char evilSk[64] = { 0 };
    crypto_sign_keypair(evilPk, evilSk);

    ClientHandshake hs;
    const std::string hello = hs.buildClientHello();

    std::string err;
    // 用攻击者的私钥签，客户端拿真服务端公钥来验
    CHECK_FALSE(hs.onServerHello(makeServerHello(server, hello, evilSk), server.idPk, err));
    CHECK_FALSE(err.empty());
    CHECK_FALSE(hs.ready());
}

TEST_CASE("FC6 应答字段缺失或损坏一律失败", "[framecrypto][handshake]")
{
    REQUIRE(cryptoInit());

    std::string err;

    {
        FakeServer server;
        ClientHandshake hs;
        const std::string hello = hs.buildClientHello();
        CHECK_FALSE(hs.onServerHello("not a json at all", server.idPk, err));
    }
    {
        FakeServer server;
        ClientHandshake hs;
        const std::string hello = hs.buildClientHello();
        CHECK_FALSE(hs.onServerHello("{\"code\":0}", server.idPk, err));   // 缺 spk/sig 等
    }
    {
        FakeServer server;
        ClientHandshake hs;
        const std::string hello = hs.buildClientHello();
        CHECK_FALSE(hs.onServerHello("{\"code\":7}", server.idPk, err));   // 服务端明确拒绝
    }
    {
        // 公钥指针为空（未配置 serverpubkey）
        FakeServer server;
        ClientHandshake hs;
        const std::string hello = hs.buildClientHello();
        CHECK_FALSE(hs.onServerHello(makeServerHello(server, hello), NULL, err));
    }
}

TEST_CASE("FC7 握手时序：不许跳步或重复", "[framecrypto][handshake]")
{
    REQUIRE(cryptoInit());

    FakeServer server;
    ClientHandshake hs;

    // 还没发 hello 就想要流头
    CHECK(hs.buildClientStreamHeader().empty());

    const std::string hello = hs.buildClientHello();
    // 重复发 hello
    CHECK(hs.buildClientHello().empty());

    std::string err;
    REQUIRE(hs.onServerHello(makeServerHello(server, hello), server.idPk, err));
    // 重复处理 ServerHello
    CHECK_FALSE(hs.onServerHello(makeServerHello(server, hello), server.idPk, err));
}

// =====================================================================
// FC8-FC11. 帧体加解密
// =====================================================================

TEST_CASE("FC8 客户端加密的帧，服务端能解开", "[framecrypto][cipher]")
{
    REQUIRE(cryptoInit());

    FakeServer server;
    ClientHandshake hs;
    const std::string hello = hs.buildClientHello();
    std::string err;
    REQUIRE(hs.onServerHello(makeServerHello(server, hello), server.idPk, err));
    acceptClientHeader(server, hs.buildClientStreamHeader());

    const std::string plain = "{\"cmd\":1003,\"account\":\"MY0309\"}";
    const std::vector<unsigned char> ad = makeAd();

    std::string cipher;
    REQUIRE(hs.cipher().encrypt(plain, &ad[0], ad.size(), cipher));
    CHECK(cipher.size() == plain.size() + STREAM_OVERHEAD_BYTES);

    // 假服务端解
    std::vector<unsigned char> out(cipher.size());
    unsigned long long outLen = 0;
    unsigned char tag = 0;
    REQUIRE(crypto_secretstream_xchacha20poly1305_pull(
                &server.pullState, &out[0], &outLen, &tag,
                (const unsigned char*)cipher.data(), cipher.size(),
                &ad[0], ad.size()) == 0);
    CHECK(std::string((const char*)&out[0], (size_t)outLen) == plain);
}

TEST_CASE("FC9 空包体与大包体都能往返", "[framecrypto][cipher]")
{
    REQUIRE(cryptoInit());

    FakeServer server;
    ClientHandshake hs;
    const std::string hello = hs.buildClientHello();
    std::string err;
    REQUIRE(hs.onServerHello(makeServerHello(server, hello), server.idPk, err));
    acceptClientHeader(server, hs.buildClientStreamHeader());

    const std::vector<unsigned char> ad = makeAd();

    // 心跳这类空包体
    std::string c1;
    REQUIRE(hs.cipher().encrypt(std::string(), &ad[0], ad.size(), c1));
    CHECK(c1.size() == STREAM_OVERHEAD_BYTES);

    // 一个已压缩的大包体（1 MiB 上限内）
    const std::string big(300 * 1024, 'x');
    std::string c2;
    REQUIRE(hs.cipher().encrypt(big, &ad[0], ad.size(), c2));
    CHECK(c2.size() == big.size() + STREAM_OVERHEAD_BYTES);
}

TEST_CASE("FC10 篡改密文或帧头必须解不开", "[framecrypto][cipher]")
{
    REQUIRE(cryptoInit());

    FakeServer server;
    ClientHandshake hs;
    const std::string hello = hs.buildClientHello();
    std::string err;
    REQUIRE(hs.onServerHello(makeServerHello(server, hello), server.idPk, err));
    acceptClientHeader(server, hs.buildClientStreamHeader());

    const std::string plain = "hello flamingo";
    const std::vector<unsigned char> ad = makeAd();

    std::string cipher;
    REQUIRE(hs.cipher().encrypt(plain, &ad[0], ad.size(), cipher));

    std::vector<unsigned char> out(cipher.size() + 1);
    unsigned long long outLen = 0;
    unsigned char tag = 0;

    SECTION("改一个密文字节")
    {
        std::string bad = cipher;
        bad[0] = (char)(bad[0] ^ 0x01);
        CHECK(crypto_secretstream_xchacha20poly1305_pull(
                  &server.pullState, &out[0], &outLen, &tag,
                  (const unsigned char*)bad.data(), bad.size(),
                  &ad[0], ad.size()) != 0);
    }

    SECTION("改帧头一个比特")
    {
        // 帧头进了附加认证数据，改动它同样解不开 —— 这保证 cmd / seq / 长度不可被替换
        std::vector<unsigned char> badAd = ad;
        badAd[3] ^= 0x01;
        CHECK(crypto_secretstream_xchacha20poly1305_pull(
                  &server.pullState, &out[0], &outLen, &tag,
                  (const unsigned char*)cipher.data(), cipher.size(),
                  &badAd[0], badAd.size()) != 0);
    }
}

TEST_CASE("FC11 握手未完成时不得加解密", "[framecrypto][cipher]")
{
    REQUIRE(cryptoInit());

    ClientHandshake hs;
    const std::vector<unsigned char> ad = makeAd();

    std::string out;
    CHECK_FALSE(hs.cipher().encrypt("x", &ad[0], ad.size(), out));
    CHECK_FALSE(hs.cipher().decrypt("xxxxxxxxxxxxxxxxxxxxx", &ad[0], ad.size(), out));

    // 密文短于固定开销，不可能是本流产出的
    CHECK_FALSE(hs.cipher().decrypt("short", &ad[0], ad.size(), out));
}

TEST_CASE("FC12 服务端公钥文本的解析与指纹", "[framecrypto][pubkey]")
{
    REQUIRE(cryptoInit());

    // 一把真实生成过的服务端公钥（base64，32 字节）。这里把它连同指纹一起钉死，
    // 是为了守住两件事：客户端的 base64 解析与服务端 sodium_bin2base64 的变体一致；
    // 客户端 publicKeyFingerprint 与服务端 ServerIdentity::fingerprint 取同一个值
    // ——两边指纹对不上，排查配置问题时就没有可比对的凭据了。
    const std::string kPubKeyText = "QLcjYOULvHX+dgwbRRvlwg0dwNRnO2H6D9a/u+7KZEg=";
    const std::string kFingerprint = "40b72360e50bbc75";

    unsigned char key[IDENTITY_PUBLICKEY_BYTES] = { 0 };
    REQUIRE(parseServerPublicKey(kPubKeyText, key));
    CHECK(publicKeyFingerprint(key) == kFingerprint);

    // 指纹只取前 8 字节，不能把整把公钥写进日志
    CHECK(publicKeyFingerprint(key).size() == 16);

    SECTION("空串视为未配置，解析失败")
    {
        unsigned char k2[IDENTITY_PUBLICKEY_BYTES] = { 0 };
        CHECK_FALSE(parseServerPublicKey("", k2));
    }

    SECTION("非法 base64 必须失败，不能悄悄截断")
    {
        unsigned char k2[IDENTITY_PUBLICKEY_BYTES] = { 0 };
        CHECK_FALSE(parseServerPublicKey("not-a-valid-base64!!!", k2));
    }

    SECTION("长度不足 32 字节必须失败")
    {
        unsigned char k2[IDENTITY_PUBLICKEY_BYTES] = { 0 };
        // 16 字节的合法 base64，长度不对，须判失败而不是补零
        CHECK_FALSE(parseServerPublicKey("AAAAAAAAAAAAAAAAAAAAAA==", k2));
    }

    SECTION("多出的尾随空白不应被当成合法公钥")
    {
        unsigned char k2[IDENTITY_PUBLICKEY_BYTES] = { 0 };
        CHECK_FALSE(parseServerPublicKey(kPubKeyText + "  ", k2));
    }

    SECTION("空指针不崩溃")
    {
        CHECK_FALSE(parseServerPublicKey(kPubKeyText, NULL));
        CHECK(publicKeyFingerprint(NULL).empty());
    }
}
