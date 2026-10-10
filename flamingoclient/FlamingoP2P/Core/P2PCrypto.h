/**
 *  P2P 链路的端到端加密（docs/P2P通信方案.md 7.2 节，第 3 期 3a）。
 *
 *  - 每个 P2P 会话两端各生成一个临时 X25519 密钥对（P2PKeyPair），公钥经 2303 交换（Base64 文本）；
 *  - P2PLinkCipher 用 crypto_kx 按角色推导收、发两个方向的密钥：发起方（role 1）按 client、应答方（role 2）按
 *    server，于是一方的发送密钥正是另一方的接收密钥；
 *  - 每个报文用 ChaCha20-Poly1305（IETF）加密认证，12 字节 nonce 为 4 个零字节加 8 字节计数器（网络字节序）；
 *    收发各用一把密钥、计数器从 1 起单调递增，nonce 不会重复；
 *  - 报文头（14 字节，见 P2PPacket.h）作为附加数据参与认证；
 *  - 会话标签取发送密钥派生值的前 4 字节，接收方用接收密钥算出同一个值，在解密之前丢弃无关报文；
 *  - 接收方向带重放窗口（P2PReplayWindow）。
 *  信任模型：信任 chatserver 转交的对方公钥（chatserver 连接本身经服务器身份认证并加密），网络上的第三方与中转
 *  服务器都读不到内容。
 *
 *  调用前进程（或 DLL）须已调用过 sodium_init（P2PModule::Init 里调用）。单线程使用。
 *
 *  典型用法：
 *      P2PKeyPair self;
 *      self.Generate();
 *      ...经 2303 把 self.PublicKeyBase64() 发给对方，收到对方的公钥文本 peerText
 *      uint8_t peerPk[kP2PPublicKeyBytes];
 *      P2PKeyPair::DecodePublicKey(peerText, peerPk);
 *      P2PLinkCipher cipher;
 *      cipher.Init(controlling, self, peerPk);
 *      cipher.Seal(p2p_packet_probe, plain, plainLen, wire);          //发
 *      cipher.Open(data, len, type, plain);                            //收
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>

#include "P2PPacket.h"         //kP2PMaxPlainBytes / kP2PRelayMaxPlainBytes
#include "P2PReplayWindow.h"

namespace p2p {

//X25519 公钥与私钥的长度（字节），与 crypto_kx_PUBLICKEYBYTES / crypto_kx_SECRETKEYBYTES 相同
const size_t kP2PPublicKeyBytes = 32;
const size_t kP2PSecretKeyBytes = 32;

//会话密钥的长度（字节），与 crypto_kx_SESSIONKEYBYTES 相同
const size_t kP2PSessionKeyBytes = 32;

//Open 的结果
enum p2p_open_result
{
    p2p_open_ok          = 0,   //解密认证通过，类型与明文有效
    p2p_open_bad_format  = 1,   //不是链路报文：太短或类型不认识
    p2p_open_wrong_tag   = 2,   //会话标签对不上：别的会话或无关的报文，未解密即丢弃
    p2p_open_replayed    = 3,   //计数器重复或太旧，未解密即丢弃
    p2p_open_auth_failed = 4,   //认证失败：被篡改、密钥不对或报文头被改动
    p2p_open_not_ready   = 5,   //本对象尚未 Init 成功
};

/**
 *  一个临时 X25519 密钥对。每个 P2P 会话生成一个，会话结束即丢弃。
 */
class P2PKeyPair
{
public:
    P2PKeyPair();
    ~P2PKeyPair();

    //生成新的密钥对（覆盖原有的）
    void Generate();

    //公钥（kP2PPublicKeyBytes 字节）；Generate 之前全为 0
    const uint8_t* PublicKey() const { return m_public; }

    //私钥（kP2PSecretKeyBytes 字节），只供 P2PLinkCipher 推导会话密钥
    const uint8_t* SecretKey() const { return m_secret; }

    //公钥的 Base64 文本（标准字母表、带填充），放进 2303 的 data.pubkey
    std::string PublicKeyBase64() const;

    /**
     *  把对方的公钥文本解码成字节。
     *  @param text Base64 文本（标准字母表、带填充）
     *  @param out  [出参] kP2PPublicKeyBytes 字节的缓冲
     *  @return 文本合法且解出的长度正好是 kP2PPublicKeyBytes 时返回 true
     */
    static bool DecodePublicKey(const std::string& text, uint8_t* out);

private:
    P2PKeyPair(const P2PKeyPair&);
    P2PKeyPair& operator=(const P2PKeyPair&);

private:
    uint8_t m_public[kP2PPublicKeyBytes];   //公钥
    uint8_t m_secret[kP2PSecretKeyBytes];   //私钥；析构时清零
};

/**
 *  一条链路两个方向的加解密：发送方向的计数器与会话标签、接收方向的会话标签与重放窗口。
 */
class P2PLinkCipher
{
public:
    P2PLinkCipher();
    ~P2PLinkCipher();

    /**
     *  按角色推导收发密钥与两个方向的会话标签，并重置计数器与重放窗口。
     *  @param controlling   本方是否为发起方（2302 的 role 为 p2p_role_controlling）
     *  @param self          本方的密钥对（借用，只在调用期间读取）
     *  @param peerPublicKey 对方的公钥（kP2PPublicKeyBytes 字节）
     *  @return 推导成功返回 true；对方公钥不合法（如全零等弱公钥）时返回 false，本对象保持未就绪
     */
    bool Init(bool controlling, const P2PKeyPair& self, const uint8_t* peerPublicKey);

    //是否已 Init 成功
    bool IsReady() const { return m_ready; }

    /**
     *  封装一个报文：分配下一个发送计数器，写报文头，加密明文并附上认证标签。
     *  @param type     报文类型（p2p_packet_type）
     *  @param plain    明文，不接管所有权；plainLen 为 0 时可以为 NULL
     *  @param plainLen 明文长度（字节），不得超过 maxPlain
     *  @param out      [出参] 完整的链路报文（报文头 + 密文 + 认证标签）
     *  @param maxPlain 明文上限（字节）：UDP 路径为 kP2PMaxPlainBytes（默认），中转路径可放宽到 kP2PRelayMaxPlainBytes；
     *                  超过 kP2PRelayMaxPlainBytes 的值按 kP2PRelayMaxPlainBytes 处理
     *  @return 成功返回 true；未就绪、类型不认识或明文超长时返回 false，计数器不变
     */
    bool Seal(uint8_t type, const uint8_t* plain, size_t plainLen, std::string& out,
              size_t maxPlain = kP2PMaxPlainBytes);

    /**
     *  拆封一个收到的报文：依次核对格式、会话标签、重放窗口，再解密认证；通过后记入重放窗口。
     *  @param data  报文内容，不接管所有权
     *  @param len   报文长度（字节）
     *  @param type  [出参] 报文类型，仅在返回 p2p_open_ok 时有效
     *  @param plain [出参] 明文，仅在返回 p2p_open_ok 时有效
     *  @return p2p_open_result
     */
    int Open(const uint8_t* data, size_t len, uint8_t& type, std::string& plain);

    //发送方向的会话标签（写进本方发出的报文头）
    uint32_t TxTag() const { return m_txTag; }

    //接收方向的会话标签（对方发来的报文头里应是这个值）
    uint32_t RxTag() const { return m_rxTag; }

    //下一个要用的发送计数器
    uint64_t NextTxCounter() const { return m_txCounter; }

    /**
     *  只看报文头判断是不是发给本链路的报文（会话标签对得上），不解密。IO 层据此把报文分给对应的链路。
     *  @param data 报文内容
     *  @param len  报文长度（字节）
     *  @return 是链路报文且会话标签等于 RxTag() 时返回 true
     */
    bool MatchesRxTag(const uint8_t* data, size_t len) const;

private:
    P2PLinkCipher(const P2PLinkCipher&);
    P2PLinkCipher& operator=(const P2PLinkCipher&);

    //由一把会话密钥派生 4 字节的会话标签
    static uint32_t DeriveTag(const uint8_t* key);

    //由计数器生成 12 字节的 nonce
    static void MakeNonce(uint64_t counter, uint8_t* nonce);

private:
    bool            m_ready;                         //是否已 Init 成功
    uint8_t         m_txKey[kP2PSessionKeyBytes];    //发送方向的密钥；析构时清零
    uint8_t         m_rxKey[kP2PSessionKeyBytes];    //接收方向的密钥；析构时清零
    uint32_t        m_txTag;                         //发送方向的会话标签
    uint32_t        m_rxTag;                         //接收方向的会话标签
    uint64_t        m_txCounter;                     //下一个发送计数器，从 1 开始
    P2PReplayWindow m_replay;                        //接收方向的重放窗口
};

} // namespace p2p
