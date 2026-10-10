/**
 *  密聊端到端加密 CSecretCipher 的实现，见 SecretCrypto.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "SecretCrypto.h"

#include <stdio.h>
#include <string.h>
#include <sodium.h>

namespace secretchat {

namespace {

//安全码：4 组各取摘要里的 4 个字节
const size_t kSafetyBytesPerGroup = 4;

//每组 5 位数字的取模基数
const uint32_t kSafetyGroupModulo = 100000;

//32 位整数的十进制文本最多 10 个字符，留余量
const int kNumberTextChars = 16;

} // namespace

bool SecretCryptoInit()
{
    //sodium_init 返回 0 表示首次初始化成功，1 表示已经初始化过，-1 表示失败
    return sodium_init() >= 0;
}

std::string ToBase64(const unsigned char* data, size_t len)
{
    const size_t cap = sodium_base64_ENCODED_LEN(len, sodium_base64_VARIANT_ORIGINAL);
    std::vector<char> buf(cap, '\0');
    sodium_bin2base64(&buf[0], cap, data, len, sodium_base64_VARIANT_ORIGINAL);
    return std::string(&buf[0]);
}

bool FromBase64(const std::string& text, std::vector<unsigned char>& out)
{
    out.clear();
    if (text.empty())
        return false;
    //解码后的长度不会超过 3/4 的输入长度
    out.resize(text.size() / 4 * 3 + 3);
    size_t binLen = 0;
    if (sodium_base642bin(&out[0], out.size(), text.c_str(), text.size(), NULL, &binLen, NULL,
                          sodium_base64_VARIANT_ORIGINAL) != 0)
    {
        out.clear();
        return false;
    }
    out.resize(binLen);
    return true;
}

std::string Hash32(const unsigned char* data, size_t len)
{
    unsigned char out[crypto_generichash_BYTES];
    crypto_generichash(out, sizeof(out), data, len, NULL, 0);
    return std::string(reinterpret_cast<const char*>(out), sizeof(out));
}

CSecretCipher::CSecretCipher()
    : m_hasKeyPair(false)
    , m_ready(false)
    , m_inviter(false)
{
    memset(m_publicKey, 0, sizeof(m_publicKey));
    memset(m_secretKey, 0, sizeof(m_secretKey));
    memset(m_peerPublicKey, 0, sizeof(m_peerPublicKey));
    memset(m_rxKey, 0, sizeof(m_rxKey));
    memset(m_txKey, 0, sizeof(m_txKey));
}

CSecretCipher::~CSecretCipher()
{
    Wipe();
}

bool CSecretCipher::Generate()
{
    Wipe();
    if (crypto_kx_keypair(m_publicKey, m_secretKey) != 0)
        return false;
    m_hasKeyPair = true;
    return true;
}

std::string CSecretCipher::PublicKeyBase64() const
{
    if (!m_hasKeyPair)
        return std::string();
    return ToBase64(m_publicKey, sizeof(m_publicKey));
}

bool CSecretCipher::DeriveAsInviter(const std::string& peerPublicKeyBase64)
{
    return Derive(peerPublicKeyBase64, true);
}

bool CSecretCipher::DeriveAsInvitee(const std::string& peerPublicKeyBase64)
{
    return Derive(peerPublicKeyBase64, false);
}

bool CSecretCipher::Derive(const std::string& peerPublicKeyBase64, bool asInviter)
{
    if (!m_hasKeyPair)
        return false;
    std::vector<unsigned char> peer;
    if (!FromBase64(peerPublicKeyBase64, peer) || peer.size() != kSecretKeyBytes)
        return false;
    int rc = 0;
    if (asInviter)
        rc = crypto_kx_client_session_keys(m_rxKey, m_txKey, m_publicKey, m_secretKey, &peer[0]);
    else
        rc = crypto_kx_server_session_keys(m_rxKey, m_txKey, m_publicKey, m_secretKey, &peer[0]);
    if (rc != 0)
        return false;
    memcpy(m_peerPublicKey, &peer[0], kSecretKeyBytes);
    //私钥只用于这一次派生，派生后立即清零
    sodium_memzero(m_secretKey, sizeof(m_secretKey));
    m_inviter = asInviter;
    m_ready = true;
    return true;
}

bool CSecretCipher::IsReady() const
{
    return m_ready;
}

bool CSecretCipher::IsInviter() const
{
    return m_inviter;
}

std::string CSecretCipher::AssociatedData(const std::string& sessionId, bool senderIsInviter, uint32_t seq)
{
    char seqText[kNumberTextChars];
    snprintf(seqText, sizeof(seqText), "%u", seq);
    return "S" + sessionId + "|" + (senderIsInviter ? "i" : "r") + "|" + seqText;
}

bool CSecretCipher::Encrypt(const std::string& sessionId, uint32_t seq, const std::string& plain,
                            std::string& nonceB64, std::string& dataB64) const
{
    nonceB64.clear();
    dataB64.clear();
    if (!m_ready)
        return false;
    unsigned char nonce[kSecretNonceBytes];
    randombytes_buf(nonce, sizeof(nonce));
    const std::string ad = AssociatedData(sessionId, m_inviter, seq);
    std::vector<unsigned char> cipher(plain.size() + kSecretTagBytes);
    unsigned long long cipherLen = 0;
    crypto_aead_xchacha20poly1305_ietf_encrypt(&cipher[0], &cipherLen,
                                               reinterpret_cast<const unsigned char*>(plain.data()), plain.size(),
                                               reinterpret_cast<const unsigned char*>(ad.data()), ad.size(), NULL,
                                               nonce, m_txKey);
    nonceB64 = ToBase64(nonce, sizeof(nonce));
    dataB64 = ToBase64(&cipher[0], static_cast<size_t>(cipherLen));
    return true;
}

bool CSecretCipher::Decrypt(const std::string& sessionId, uint32_t seq, const std::string& nonceB64,
                            const std::string& dataB64, std::string& plain) const
{
    plain.clear();
    if (!m_ready)
        return false;
    std::vector<unsigned char> nonce;
    std::vector<unsigned char> cipher;
    if (!FromBase64(nonceB64, nonce) || nonce.size() != kSecretNonceBytes || !FromBase64(dataB64, cipher)
        || cipher.size() < kSecretTagBytes)
        return false;
    //对方的发送方向与本方相反
    const std::string ad = AssociatedData(sessionId, !m_inviter, seq);
    std::vector<unsigned char> out(cipher.size());
    unsigned long long outLen = 0;
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(&out[0], &outLen, NULL, &cipher[0], cipher.size(),
                                                   reinterpret_cast<const unsigned char*>(ad.data()), ad.size(),
                                                   &nonce[0], m_rxKey) != 0)
        return false;
    plain.assign(reinterpret_cast<const char*>(&out[0]), static_cast<size_t>(outLen));
    sodium_memzero(&out[0], out.size());
    return true;
}

bool CSecretCipher::SafetyCode(std::string& out) const
{
    out.clear();
    if (!m_ready)
        return false;
    //两把公钥按字节序排序后拼接：两端拼出的内容相同
    unsigned char joined[kSecretKeyBytes * 2];
    const bool mineFirst = memcmp(m_publicKey, m_peerPublicKey, kSecretKeyBytes) < 0;
    memcpy(joined, mineFirst ? m_publicKey : m_peerPublicKey, kSecretKeyBytes);
    memcpy(joined + kSecretKeyBytes, mineFirst ? m_peerPublicKey : m_publicKey, kSecretKeyBytes);
    unsigned char hash[kSafetyBytesPerGroup * kSafetyCodeGroups];
    crypto_generichash(hash, sizeof(hash), joined, sizeof(joined), NULL, 0);
    for (int g = 0; g < kSafetyCodeGroups; ++g)
    {
        const unsigned char* p = hash + g * kSafetyBytesPerGroup;
        const uint32_t value = (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16)
                               | (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
        char group[kNumberTextChars];
        snprintf(group, sizeof(group), "%05u", value % kSafetyGroupModulo);
        if (g > 0)
            out += " ";
        out += group;
    }
    return true;
}

void CSecretCipher::Wipe()
{
    sodium_memzero(m_publicKey, sizeof(m_publicKey));
    sodium_memzero(m_secretKey, sizeof(m_secretKey));
    sodium_memzero(m_peerPublicKey, sizeof(m_peerPublicKey));
    sodium_memzero(m_rxKey, sizeof(m_rxKey));
    sodium_memzero(m_txKey, sizeof(m_txKey));
    m_hasKeyPair = false;
    m_ready = false;
    m_inviter = false;
}

} // namespace secretchat
