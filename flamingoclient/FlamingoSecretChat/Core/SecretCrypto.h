/**
 *  密聊的端到端加密（docs/密聊方案.md 4.4），SecretCrypto.h
 *
 *  每次密聊双方各生成一对临时 X25519 密钥，公钥经服务端交换后各自派生收发两把会话密钥：发起方按 crypto_kx 的
 *  client 角色、被邀请方按 server 角色，于是发起方的发送密钥等于被邀请方的接收密钥，反之亦然。每帧用
 *  XChaCha20-Poly1305 加密，nonce 随机 24 字节，附加数据为「会话号 | 发送方向 | 序号」，任何一项被篡改都解不开。
 *
 *  纯逻辑（只依赖 libsodium），不触及窗口与网络，便于单元测试（Tests/SecretCryptoTests.cpp，[secretdll]）。
 *  一个 CSecretCipher 对应一个会话；析构或 Wipe 时用 sodium_memzero 清零全部密钥材料。
 *
 *  典型用法：
 *      CSecretCipher c;
 *      c.Generate();                        // 发起方：邀请前生成；被邀请方：同意时生成
 *      std::string pk = c.PublicKeyBase64();  // 随 2602 / 2604 发出
 *      c.DeriveAsInviter(peerPk);           // 发起方收到 2605（已建立）后；被邀请方用 DeriveAsInvitee
 *      c.Encrypt(sid, seq, plain, nonce, data);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace secretchat {

//X25519 公钥、私钥与会话密钥的字节数（crypto_kx_*BYTES）
const size_t kSecretKeyBytes = 32;

//XChaCha20-Poly1305 的 nonce 与认证标签字节数
const size_t kSecretNonceBytes = 24;
const size_t kSecretTagBytes = 16;

//安全码：4 组、每组 5 位十进制数字
const int kSafetyCodeGroups = 4;
const int kSafetyCodeDigits = 5;

/**
 *  初始化 libsodium（可重复调用，线程安全）。
 *  @return 初始化成功返回 true
 */
bool SecretCryptoInit();

/**
 *  标准 base64 编码（带填充）。
 */
std::string ToBase64(const unsigned char* data, size_t len);

/**
 *  标准 base64 解码；输入不是合法 base64 时返回 false。
 */
bool FromBase64(const std::string& text, std::vector<unsigned char>& out);

/**
 *  计算一段数据的 32 字节 BLAKE2b 摘要（图片完整性校验用）。
 */
std::string Hash32(const unsigned char* data, size_t len);

/**
 *  一个密聊会话的加密状态。
 */
class CSecretCipher
{
public:
    CSecretCipher();
    ~CSecretCipher();

    /**
     *  生成本次的临时密钥对；已有的密钥材料先清零。
     *  @return 成功返回 true
     */
    bool Generate();

    /**
     *  本方公钥的 base64（44 个字符）；没有生成过时为空串。
     */
    std::string PublicKeyBase64() const;

    /**
     *  以发起方身份派生会话密钥。
     *  @param peerPublicKeyBase64 被邀请方的公钥（base64，解码后 32 字节）
     *  @return 公钥非法或派生失败返回 false
     */
    bool DeriveAsInviter(const std::string& peerPublicKeyBase64);

    /**
     *  以被邀请方身份派生会话密钥。参数与返回值同 DeriveAsInviter。
     */
    bool DeriveAsInvitee(const std::string& peerPublicKeyBase64);

    /**
     *  会话密钥是否已派生。
     */
    bool IsReady() const;

    /**
     *  本方是否为发起方（派生之后才有意义）。
     */
    bool IsInviter() const;

    /**
     *  加密一帧。
     *  @param sessionId 会话号（十进制文本），参与附加数据
     *  @param seq       本方向的帧序号，参与附加数据
     *  @param plain     明文（信封，见 SecretEnvelope.h）
     *  @param nonceB64  [出参] nonce 的 base64（32 个字符）
     *  @param dataB64   [出参] 密文的 base64
     *  @return 会话密钥未派生返回 false
     */
    bool Encrypt(const std::string& sessionId, uint32_t seq, const std::string& plain, std::string& nonceB64,
                 std::string& dataB64) const;

    /**
     *  解密对方发来的一帧（附加数据按对方的发送方向计算）。
     *  @return 未派生、nonce 或密文不是合法 base64、认证失败都返回 false
     */
    bool Decrypt(const std::string& sessionId, uint32_t seq, const std::string& nonceB64, const std::string& dataB64,
                 std::string& plain) const;

    /**
     *  安全码：两把公钥按字节序排序后拼接，取 BLAKE2b 摘要，换算成 4 组 5 位数字，组间一个空格。
     *  两端算出的相同，与谁是发起方无关。
     *  @return 会话密钥已派生时返回 true
     */
    bool SafetyCode(std::string& out) const;

    /**
     *  清零全部密钥材料，回到初始状态。
     */
    void Wipe();

private:
    //派生会话密钥的公共部分；asInviter 为 true 时按 client 角色
    bool Derive(const std::string& peerPublicKeyBase64, bool asInviter);

    //附加数据：「S<会话号>|<发送方向 i 或 r>|<序号>」
    static std::string AssociatedData(const std::string& sessionId, bool senderIsInviter, uint32_t seq);

    //禁止拷贝：密钥材料不应出现第二份
    CSecretCipher(const CSecretCipher&);
    CSecretCipher& operator=(const CSecretCipher&);

private:
    unsigned char m_publicKey[kSecretKeyBytes];     //本方临时公钥
    unsigned char m_secretKey[kSecretKeyBytes];     //本方临时私钥，派生后即清零
    unsigned char m_peerPublicKey[kSecretKeyBytes]; //对方公钥（安全码用）
    unsigned char m_rxKey[kSecretKeyBytes];         //接收密钥：解密对方发来的帧
    unsigned char m_txKey[kSecretKeyBytes];         //发送密钥：加密本方发出的帧
    bool          m_hasKeyPair;                     //是否已生成密钥对
    bool          m_ready;                          //会话密钥是否已派生
    bool          m_inviter;                        //本方是否为发起方
};

} // namespace secretchat
