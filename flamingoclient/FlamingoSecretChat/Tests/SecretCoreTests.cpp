/**
 *  密聊 DLL 纯逻辑单元测试（标签 [secretdll]），覆盖 docs/密聊方案.md 第 8 节：
 *    - SCD1~SCD4 加密：两端派生的收发密钥互相对应、加解密往返；篡改密文、篡改附加数据（会话号、序号、方向）、
 *                换用别的会话的密钥都解不开；
 *    - SCD7 安全码：两端相同、与谁是发起方无关、换掉一把公钥即不同、格式为 4 组 5 位数字；
 *    - SCD11~SCD12 报文：各命令组包与解析；缺字段、类型不对的包体被拒绝；
 *    - SCD13 防截图级别判定：桌面合成与系统版本的组合、亲和性取值换算；
 *    - SCD14~SCD16 信封与图片分块：文字往返、按 48 KB 分块后乱序拼回与原图逐字节相同、摘要不符与元数据矛盾作废、
 *                重复块忽略、清空。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../Core/SecretCrypto.h"
#include "../Core/SecretEnvelope.h"
#include "../Core/SecretProtocol.h"
#include "../Core/CaptureProtect.h"
#include "../SecretChatApi.h"
#include "../../Source/net/Protocol.h"

namespace {

//测试用的会话号文本
const char* const kSid = "4242";

//一段测试图片的字节数：两块多一点，最后一块不满
const uint32_t kImageBytes = 48 * 1024 * 2 + 777;

//生成指定长度、内容可辨认的字节串
std::string MakeBytes(uint32_t len)
{
    std::string s(len, '\0');
    for (uint32_t i = 0; i < len; ++i)
        s[i] = static_cast<char>((i * 31 + 7) & 0xFF);
    return s;
}

//建一对已派生好密钥的会话：inviter 为发起方，invitee 为被邀请方
void MakePair(secretchat::CSecretCipher& inviter, secretchat::CSecretCipher& invitee)
{
    REQUIRE(secretchat::SecretCryptoInit());
    REQUIRE(inviter.Generate());
    REQUIRE(invitee.Generate());
    const std::string pkA = inviter.PublicKeyBase64();
    const std::string pkB = invitee.PublicKeyBase64();
    REQUIRE(pkA.size() == 44);
    REQUIRE(invitee.DeriveAsInvitee(pkA));
    REQUIRE(inviter.DeriveAsInviter(pkB));
}

} // namespace

TEST_CASE("SCD1 两端派生的密钥互相对应，双向加解密往返", "[secretdll]")
{
    secretchat::CSecretCipher a;
    secretchat::CSecretCipher b;
    MakePair(a, b);
    CHECK(a.IsReady());
    CHECK(a.IsInviter());
    CHECK_FALSE(b.IsInviter());

    std::string nonce;
    std::string data;
    REQUIRE(a.Encrypt(kSid, 1, u8"你好，密聊", nonce, data));
    CHECK(nonce.size() == 32);
    std::string plain;
    REQUIRE(b.Decrypt(kSid, 1, nonce, data, plain));
    CHECK(plain == u8"你好，密聊");

    REQUIRE(b.Encrypt(kSid, 1, "reply", nonce, data));
    REQUIRE(a.Decrypt(kSid, 1, nonce, data, plain));
    CHECK(plain == "reply");
}

TEST_CASE("SCD2 篡改密文解不开", "[secretdll]")
{
    secretchat::CSecretCipher a;
    secretchat::CSecretCipher b;
    MakePair(a, b);
    std::string nonce;
    std::string data;
    REQUIRE(a.Encrypt(kSid, 5, "secret text", nonce, data));
    std::vector<unsigned char> raw;
    REQUIRE(secretchat::FromBase64(data, raw));
    raw[0] ^= 0x01;
    const std::string tampered = secretchat::ToBase64(&raw[0], raw.size());
    std::string plain;
    CHECK_FALSE(b.Decrypt(kSid, 5, nonce, tampered, plain));
    CHECK(plain.empty());
    CHECK_FALSE(b.Decrypt(kSid, 5, "not base64!", data, plain));
}

TEST_CASE("SCD3 附加数据被改（会话号、序号、方向）解不开", "[secretdll]")
{
    secretchat::CSecretCipher a;
    secretchat::CSecretCipher b;
    MakePair(a, b);
    std::string nonce;
    std::string data;
    REQUIRE(a.Encrypt(kSid, 9, "payload", nonce, data));
    std::string plain;
    CHECK_FALSE(b.Decrypt("4243", 9, nonce, data, plain));
    CHECK_FALSE(b.Decrypt(kSid, 10, nonce, data, plain));
    //方向不对：发起方自己去解自己发的帧，附加数据里的方向与接收密钥都对不上
    CHECK_FALSE(a.Decrypt(kSid, 9, nonce, data, plain));
    CHECK(b.Decrypt(kSid, 9, nonce, data, plain));
}

TEST_CASE("SCD4 换用别的会话的密钥解不开；未派生时不能加解密；清零后失效", "[secretdll]")
{
    secretchat::CSecretCipher a;
    secretchat::CSecretCipher b;
    MakePair(a, b);
    secretchat::CSecretCipher c;
    secretchat::CSecretCipher d;
    MakePair(c, d);
    std::string nonce;
    std::string data;
    REQUIRE(a.Encrypt(kSid, 1, "x", nonce, data));
    std::string plain;
    CHECK_FALSE(d.Decrypt(kSid, 1, nonce, data, plain));

    secretchat::CSecretCipher fresh;
    CHECK_FALSE(fresh.Encrypt(kSid, 1, "x", nonce, data));
    CHECK_FALSE(fresh.DeriveAsInviter(a.PublicKeyBase64()));
    REQUIRE(fresh.Generate());
    CHECK_FALSE(fresh.DeriveAsInviter("short"));

    b.Wipe();
    CHECK_FALSE(b.IsReady());
    CHECK_FALSE(b.Decrypt(kSid, 1, nonce, data, plain));
    CHECK(b.PublicKeyBase64().empty());
}

TEST_CASE("SCD7 安全码：两端相同、格式固定、换掉公钥即不同", "[secretdll]")
{
    secretchat::CSecretCipher a;
    secretchat::CSecretCipher b;
    MakePair(a, b);
    std::string codeA;
    std::string codeB;
    REQUIRE(a.SafetyCode(codeA));
    REQUIRE(b.SafetyCode(codeB));
    CHECK(codeA == codeB);
    REQUIRE(codeA.size() == 23);
    CHECK(codeA[5] == ' ');
    CHECK(codeA[11] == ' ');
    CHECK(codeA[17] == ' ');

    secretchat::CSecretCipher c;
    secretchat::CSecretCipher d;
    MakePair(c, d);
    std::string codeC;
    REQUIRE(c.SafetyCode(codeC));
    CHECK(codeC != codeA);

    secretchat::CSecretCipher none;
    std::string empty;
    CHECK_FALSE(none.SafetyCode(empty));
}

TEST_CASE("SCD11 报文组包", "[secretdll]")
{
    using secretchat::SecretProtocol;
    CHECK(SecretProtocol::BuildRegister(1, 2) == "{\"protect\":2,\"version\":1}");
    CHECK(SecretProtocol::BuildInvite(10002, "PK") == "{\"peerid\":\"10002\",\"pubkey\":\"PK\"}");
    CHECK(SecretProtocol::BuildRespond(77, true, "PK") == "{\"accept\":1,\"pubkey\":\"PK\",\"sessionid\":\"77\"}");
    CHECK(SecretProtocol::BuildRespond(77, false, "PK") == "{\"accept\":0,\"sessionid\":\"77\"}");
    CHECK(SecretProtocol::BuildSessionOnly(18446744073709551615ULL) == "{\"sessionid\":\"18446744073709551615\"}");
    CHECK(SecretProtocol::BuildMessage(7, 3, "N", "D") == "{\"data\":\"D\",\"nonce\":\"N\",\"seq\":3,\"sessionid\":\"7\"}");
}

TEST_CASE("SCD12 报文解析：正常与非法包体", "[secretdll]")
{
    using namespace secretchat;
    int code = -1;
    bool enabled = false;
    REQUIRE(SecretProtocol::ParseRegisterResponse("{\"code\":0,\"msg\":\"ok\",\"enabled\":1}", code, enabled));
    CHECK(code == 0);
    CHECK(enabled);
    CHECK_FALSE(SecretProtocol::ParseRegisterResponse("{\"msg\":\"ok\"}", code, enabled));

    uint64_t sid = 0;
    uint32_t expire = 0;
    REQUIRE(SecretProtocol::ParseInviteResponse("{\"code\":0,\"sessionid\":\"123\",\"expirems\":60000}", code, sid, expire));
    CHECK(sid == 123);
    CHECK(expire == 60000);
    REQUIRE(SecretProtocol::ParseInviteResponse("{\"code\":1201,\"msg\":\"x\"}", code, sid, expire));
    CHECK(code == 1201);
    CHECK(sid == 0);

    InviteNotify inv;
    REQUIRE(SecretProtocol::ParseInviteNotify(
        "{\"sessionid\":\"9\",\"fromid\":\"10001\",\"pubkey\":\"PK\",\"expirems\":60000,\"peerprotect\":1}", inv));
    CHECK(inv.fromId == 10001);
    CHECK(inv.peerProtect == 1);
    CHECK_FALSE(SecretProtocol::ParseInviteNotify("{\"sessionid\":\"9\",\"fromid\":\"10001\"}", inv));

    StateNotify st;
    REQUIRE(SecretProtocol::ParseStateNotify(
        "{\"sessionid\":\"9\",\"peerid\":\"10002\",\"state\":4,\"reason\":2,\"peerprotect\":2}", st));
    CHECK(st.state == 4);
    CHECK(st.reason == 2);
    CHECK(st.peerId == 10002);
    CHECK_FALSE(SecretProtocol::ParseStateNotify("{\"sessionid\":\"9\"}", st));

    MessageNotify mn;
    REQUIRE(SecretProtocol::ParseMessageNotify(
        "{\"sessionid\":\"9\",\"seq\":3,\"nonce\":\"N\",\"data\":\"D\",\"servertime\":1791000000000}", mn));
    CHECK(mn.seq == 3);
    CHECK(mn.serverTime == 1791000000000LL);
    CHECK_FALSE(SecretProtocol::ParseMessageNotify("{\"sessionid\":\"9\",\"seq\":0,\"nonce\":\"N\",\"data\":\"D\"}", mn));
    CHECK_FALSE(SecretProtocol::ParseMessageNotify("not json", mn));

    uint32_t seq = 0;
    REQUIRE(SecretProtocol::ParseMessageResponse("{\"code\":1206,\"seq\":5}", code, seq));
    CHECK(code == 1206);
    CHECK(seq == 5);
}

TEST_CASE("SCD13 防截图级别判定与亲和性换算", "[secretdll]")
{
    using namespace secretchat;
    CHECK(DecideProtectLevel(false, 22631) == kProtectNone);
    CHECK(DecideProtectLevel(true, 7601) == kProtectMonitor);
    CHECK(DecideProtectLevel(true, kExcludeFromCaptureMinBuild - 1) == kProtectMonitor);
    CHECK(DecideProtectLevel(true, kExcludeFromCaptureMinBuild) == kProtectExclude);
    CHECK(AffinityToLevel(0) == kProtectNone);
    CHECK(AffinityToLevel(kWdaMonitor) == kProtectMonitor);
    CHECK(AffinityToLevel(kWdaExcludeFromCapture) == kProtectExclude);
    //级别取值与协议头的 secret_protect_level 一致
    CHECK(static_cast<int>(kProtectNone) == secret_protect_none);
    CHECK(static_cast<int>(kProtectMonitor) == secret_protect_monitor);
    CHECK(static_cast<int>(kProtectExclude) == secret_protect_exclude);
    CHECK(IsSecretCommand(cmd_secret_register));
    CHECK(IsSecretCommand(cmd_secret_message_notify));
    CHECK_FALSE(IsSecretCommand(cmd_thread_list_posts));
}

TEST_CASE("SCD14 文字信封往返", "[secretdll]")
{
    using namespace secretchat;
    SecretEnvelope env;
    env.type = envelope_text;
    env.id = 12;
    env.text = u8"表情 /f[\"3\"] 与 4 字节字符 \U0001F600";
    env.ts = 1791000000123LL;
    std::string bin;
    REQUIRE(EncodeEnvelope(env, bin));
    SecretEnvelope back;
    REQUIRE(DecodeEnvelope(bin, back));
    CHECK(back.type == envelope_text);
    CHECK(back.id == 12);
    CHECK(back.text == env.text);
    CHECK(back.ts == env.ts);
    CHECK_FALSE(DecodeEnvelope(bin.substr(0, 2), back));
    std::string badType = bin;
    badType[0] = 9;
    CHECK_FALSE(DecodeEnvelope(badType, back));
}

TEST_CASE("SCD15 图片分块乱序拼回与原图一致", "[secretdll]")
{
    using namespace secretchat;
    const std::string image = MakeBytes(kImageBytes);
    std::vector<SecretEnvelope> chunks;
    SplitImage(3, image, 640, 480, chunks);
    REQUIRE(chunks.size() == 3);
    CHECK(ImageChunkCount(kImageBytes) == 3);
    CHECK(chunks[2].payload.size() == 777);

    //每块都经过编码与解码，再倒序加入
    CImageAssembler asm1;
    uint32_t received = 0;
    std::vector<int> results;
    for (size_t i = chunks.size(); i > 0; --i)
    {
        std::string bin;
        REQUIRE(EncodeEnvelope(chunks[i - 1], bin));
        SecretEnvelope env;
        REQUIRE(DecodeEnvelope(bin, env));
        results.push_back(asm1.AddChunk(env, received));
    }
    CHECK(results[0] == CImageAssembler::add_first_chunk);
    CHECK(results[1] == CImageAssembler::add_progress);
    CHECK(results[2] == CImageAssembler::add_complete);
    CHECK(received == kImageBytes);
    std::string out;
    uint32_t w = 0;
    uint32_t h = 0;
    REQUIRE(asm1.Take(3, out, w, h));
    CHECK(out == image);
    CHECK(w == 640);
    CHECK(h == 480);
    CHECK_FALSE(asm1.Take(3, out, w, h));

    //重复的块被忽略
    CImageAssembler asm2;
    CHECK(asm2.AddChunk(chunks[0], received) == CImageAssembler::add_first_chunk);
    CHECK(asm2.AddChunk(chunks[0], received) == CImageAssembler::add_duplicate);
    CHECK(asm2.PendingIds().size() == 1);
    asm2.Clear();
    CHECK(asm2.PendingIds().empty());
}

TEST_CASE("SCD16 图片摘要不符或元数据矛盾时整张作废", "[secretdll]")
{
    using namespace secretchat;
    const std::string image = MakeBytes(kImageBytes);
    std::vector<SecretEnvelope> chunks;
    SplitImage(4, image, 1, 1, chunks);
    //改动最后一块的一个字节：收齐后摘要对不上
    CImageAssembler a;
    uint32_t received = 0;
    a.AddChunk(chunks[0], received);
    a.AddChunk(chunks[1], received);
    chunks[2].payload[0] ^= 0x55;
    CHECK(a.AddChunk(chunks[2], received) == CImageAssembler::add_failed);
    CHECK(a.PendingIds().empty());

    //块序号越界、块长度不对、总字节数与块数对不上
    SecretEnvelope bad = chunks[0];
    bad.chunkIndex = 3;
    CImageAssembler b;
    CHECK(b.AddChunk(bad, received) == CImageAssembler::add_failed);
    bad = chunks[0];
    bad.payload.resize(10);
    CHECK(b.AddChunk(bad, received) == CImageAssembler::add_failed);
    bad = chunks[0];
    bad.chunkCount = 9;
    CHECK(b.AddChunk(bad, received) == CImageAssembler::add_failed);
}
