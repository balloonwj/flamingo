/**
 *  P2P 链路端到端加密的实现，见 P2PCrypto.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "P2PCrypto.h"

#include <string.h>
#include <sodium.h>

#include "P2PPacket.h"

namespace p2p {

//头文件里的长度常量须与 libsodium 一致
static_assert(kP2PPublicKeyBytes == crypto_kx_PUBLICKEYBYTES, "public key size mismatch");
static_assert(kP2PSecretKeyBytes == crypto_kx_SECRETKEYBYTES, "secret key size mismatch");
static_assert(kP2PSessionKeyBytes == crypto_kx_SESSIONKEYBYTES, "session key size mismatch");
static_assert(kP2PAuthTagBytes == crypto_aead_chacha20poly1305_ietf_ABYTES, "auth tag size mismatch");

namespace {

//nonce 的长度（字节）与其中计数器的起始偏移：前 4 字节为 0，后 8 字节为计数器（网络字节序）
const size_t kNonceBytes = crypto_aead_chacha20poly1305_ietf_NPUBBYTES;
const size_t kNonceCounterOffset = 4;
const size_t kNonceCounterBytes = 8;

//派生会话标签时混入的上下文，使标签与同一密钥的其它派生值不同
const char* const kTagContext = "flamingo-p2p-session-tag";

//会话标签取派生值的前几个字节
const size_t kTagBytes = 4;

//一个字节的位数
const int kBitsPerByte = 8;

//公钥 Base64 文本的缓冲（字节）：32 字节编码后 44 个字符加结尾 0，留足余量
const size_t kBase64BufBytes = 64;

//解码公钥时的缓冲（字节）：比公钥长，用来识别「解出来的长度不对」
const size_t kDecodeBufBytes = 64;

//发送计数器的起始值：0 留作非法值（重放窗口拒绝 0）
const uint64_t kFirstCounter = 1;

//空明文时交给 libsodium 的占位缓冲（不读不写，只为避免传 NULL）
uint8_t g_emptyPlain[1] = { 0 };

} // namespace

P2PKeyPair::P2PKeyPair()
{
    memset(m_public, 0, sizeof(m_public));
    memset(m_secret, 0, sizeof(m_secret));
}

P2PKeyPair::~P2PKeyPair()
{
    sodium_memzero(m_secret, sizeof(m_secret));
}

void P2PKeyPair::Generate()
{
    crypto_kx_keypair(m_public, m_secret);
}

std::string P2PKeyPair::PublicKeyBase64() const
{
    char buf[kBase64BufBytes] = { 0 };
    sodium_bin2base64(buf, sizeof(buf), m_public, sizeof(m_public), sodium_base64_VARIANT_ORIGINAL);
    return buf;
}

bool P2PKeyPair::DecodePublicKey(const std::string& text, uint8_t* out)
{
    if (text.empty() || out == NULL)
        return false;
    uint8_t buf[kDecodeBufBytes] = { 0 };
    size_t binLen = 0;
    if (sodium_base642bin(buf, sizeof(buf), text.c_str(), text.size(), NULL, &binLen, NULL,
                          sodium_base64_VARIANT_ORIGINAL) != 0)
        return false;
    if (binLen != kP2PPublicKeyBytes)
        return false;
    memcpy(out, buf, kP2PPublicKeyBytes);
    return true;
}

P2PLinkCipher::P2PLinkCipher()
    : m_ready(false)
    , m_txTag(0)
    , m_rxTag(0)
    , m_txCounter(kFirstCounter)
{
    memset(m_txKey, 0, sizeof(m_txKey));
    memset(m_rxKey, 0, sizeof(m_rxKey));
}

P2PLinkCipher::~P2PLinkCipher()
{
    sodium_memzero(m_txKey, sizeof(m_txKey));
    sodium_memzero(m_rxKey, sizeof(m_rxKey));
}

bool P2PLinkCipher::Init(bool controlling, const P2PKeyPair& self, const uint8_t* peerPublicKey)
{
    m_ready = false;
    if (peerPublicKey == NULL)
        return false;
    //发起方按 client、应答方按 server 推导，于是一方的发送密钥正是另一方的接收密钥。
    //对方公钥为全零等弱公钥时，X25519 算出的共享秘密全为零，libsodium 返回 -1
    const int ret = controlling
        ? crypto_kx_client_session_keys(m_rxKey, m_txKey, self.PublicKey(), self.SecretKey(), peerPublicKey)
        : crypto_kx_server_session_keys(m_rxKey, m_txKey, self.PublicKey(), self.SecretKey(), peerPublicKey);
    if (ret != 0)
    {
        sodium_memzero(m_txKey, sizeof(m_txKey));
        sodium_memzero(m_rxKey, sizeof(m_rxKey));
        return false;
    }
    m_txTag = DeriveTag(m_txKey);
    m_rxTag = DeriveTag(m_rxKey);
    m_txCounter = kFirstCounter;
    m_replay = P2PReplayWindow();
    m_ready = true;
    return true;
}

bool P2PLinkCipher::Seal(uint8_t type, const uint8_t* plain, size_t plainLen, std::string& out, size_t maxPlain)
{
    const size_t limit = (maxPlain < kP2PRelayMaxPlainBytes) ? maxPlain : kP2PRelayMaxPlainBytes;
    if (!m_ready || !IsP2PPacketType(type) || plainLen > limit || (plain == NULL && plainLen > 0))
        return false;
    out.assign(kP2PHeaderBytes + plainLen + kP2PAuthTagBytes, '\0');
    uint8_t* wire = reinterpret_cast<uint8_t*>(&out[0]);
    WritePacketHeader(type, m_txTag, m_txCounter, wire);
    uint8_t nonce[kNonceBytes];
    MakeNonce(m_txCounter, nonce);
    unsigned long long cipherLen = 0;
    //报文头作为附加数据参与认证
    crypto_aead_chacha20poly1305_ietf_encrypt(wire + kP2PHeaderBytes, &cipherLen, (plain != NULL) ? plain : g_emptyPlain,
                                              plainLen, wire, kP2PHeaderBytes, NULL, nonce, m_txKey);
    ++m_txCounter;
    return true;
}

int P2PLinkCipher::Open(const uint8_t* data, size_t len, uint8_t& type, std::string& plain)
{
    if (!m_ready)
        return p2p_open_not_ready;
    P2PPacketHeader header;
    if (!ParsePacketHeader(data, len, header))
        return p2p_open_bad_format;
    //会话标签与重放检查都在解密之前：无关报文与重放报文不消耗解密的开销，也不推动重放窗口
    if (header.m_sessionTag != m_rxTag)
        return p2p_open_wrong_tag;
    if (!m_replay.IsAcceptable(header.m_counter))
        return p2p_open_replayed;

    const size_t cipherLen = len - kP2PHeaderBytes;
    std::string out(cipherLen - kP2PAuthTagBytes, '\0');
    uint8_t* outBuf = out.empty() ? g_emptyPlain : reinterpret_cast<uint8_t*>(&out[0]);
    uint8_t nonce[kNonceBytes];
    MakeNonce(header.m_counter, nonce);
    unsigned long long plainLen = 0;
    if (crypto_aead_chacha20poly1305_ietf_decrypt(outBuf, &plainLen, NULL, data + kP2PHeaderBytes, cipherLen, data,
                                                  kP2PHeaderBytes, nonce, m_rxKey) != 0)
        return p2p_open_auth_failed;
    m_replay.Accept(header.m_counter);
    out.resize((size_t)plainLen);
    type = header.m_type;
    plain.swap(out);
    return p2p_open_ok;
}

bool P2PLinkCipher::MatchesRxTag(const uint8_t* data, size_t len) const
{
    P2PPacketHeader header;
    return m_ready && ParsePacketHeader(data, len, header) && header.m_sessionTag == m_rxTag;
}

uint32_t P2PLinkCipher::DeriveTag(const uint8_t* key)
{
    uint8_t hash[crypto_generichash_BYTES];
    crypto_generichash(hash, sizeof(hash), reinterpret_cast<const unsigned char*>(kTagContext), strlen(kTagContext),
                       key, kP2PSessionKeyBytes);
    uint32_t tag = 0;
    for (size_t i = 0; i < kTagBytes; ++i)
        tag = (tag << kBitsPerByte) | hash[i];
    return tag;
}

void P2PLinkCipher::MakeNonce(uint64_t counter, uint8_t* nonce)
{
    memset(nonce, 0, kNonceBytes);
    for (size_t i = 0; i < kNonceCounterBytes; ++i)
        nonce[kNonceCounterOffset + i] = (uint8_t)(counter >> (kBitsPerByte * (kNonceCounterBytes - 1 - i)));
}

} // namespace p2p
