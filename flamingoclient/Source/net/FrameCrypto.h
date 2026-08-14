/**
 *  v2 协议的帧体加密：客户端侧的握手与帧体加解密。
 *
 *  设计与线上格式见 docs/协议加密方案.md。本文件是服务端
 *  flamingoserver/net/FrameCrypto.h 的<u>镜像实现</u>，线上格式必须逐字节一致：
 *  握手记录的拼接顺序、base64 变体、JSON 字段名、附加认证数据的取法，任何一处
 *  与服务端不同，握手都会失败或帧解不开。改动格式时<u>必须同步改服务端那一份</u>。
 *
 *  客户端与服务端的职责分工：
 *    · 客户端生成一次性密钥对、发起握手，用<u>预置的服务端公钥</u>验签。
 *      验签是本方案抗中间人的唯一手段 —— 不校验就只防被动窃听、防不住中间人。
 *    · 服务端持有长期身份密钥并签名，客户端不需要也不应持有任何身份私钥。
 *
 *  与压缩的顺序<u>不可颠倒</u>：发送时先 zlib 压缩、后加密；接收时先解密、后解压。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <mutex>

namespace net {

//==================== 尺寸常量 ====================
//与服务端 FrameCrypto.h 逐个对应，数值经实测核对（libsodium 1.0.22）。

//Ed25519 身份公钥字节数（客户端只持有公钥，没有私钥）
const size_t IDENTITY_PUBLICKEY_BYTES = 32;
//Ed25519 签名字节数
const size_t IDENTITY_SIGNATURE_BYTES = 64;
//X25519 一次性公钥 / 私钥 / 导出的会话密钥字节数
const size_t KX_PUBLICKEY_BYTES = 32;
const size_t KX_SECRETKEY_BYTES = 32;
const size_t KX_SESSIONKEY_BYTES = 32;
//secretstream 流头字节数；每个方向一份，握手时交换
const size_t STREAM_HEADER_BYTES = 24;
//secretstream 每帧的固定膨胀量：密文长度 = 明文长度 + 该值
const size_t STREAM_OVERHEAD_BYTES = 17;
//握手随机数字节数
const size_t HANDSHAKE_NONCE_BYTES = 16;
//握手协议版本号，必须与服务端一致
const uint8_t HANDSHAKE_VERSION = 1;

//附加认证数据的字节数，见 buildFrameAd
const size_t FRAME_AD_BYTES = 16;

/**
 *  初始化底层密码学库。进程内调用一次即可，多次调用安全。
 *  必须在使用本文件其它任何功能之前、且在创建工作线程之前调用。
 *  @return 成功返回 true；返回 false 时所有加密功能不可用。
 */
bool cryptoInit();

/**
 *  由帧头字段拼出「附加认证数据」（AD），供 FrameCipher 的加解密使用。
 *  与服务端 net/FrameCrypto.h 的同名函数<u>必须逐字节一致</u>。
 *
 *  AD 参与认证但不被加密：cmd / seq / flags / 两个长度都纳进来之后，攻击者改动
 *  帧头任何一个字段，接收方解密都会失败 —— 无法把一帧改投成另一个 cmd、
 *  无法替换序列号、无法伪造长度。
 *
 *  布局（共 FRAME_AD_BYTES 字节，全部大端序）：
 *      cmd(2) | flags(1) | 固定 0(1) | seq(4) | bodylen(4) | originlen(4)
 *
 *    flags：    应传<u>最终写进帧头的</u>标志位（已含 frame_flag_encrypted）
 *    bodylen：  线上包体长度，即密文长度
 *    originlen：最原始的明文长度（压缩前、加密前）
 *    out：      输出缓冲，须有 FRAME_AD_BYTES 字节
 */
void buildFrameAd(uint16_t cmd, uint32_t seq, uint8_t flags,
                  uint32_t bodylen, uint32_t originlen, unsigned char* out);

/**
 *  把配置文件里的服务端公钥文本（base64）解析成字节。
 *    text：  flamingo.ini 的 serverpubkey 值
 *    keyOut：输出 IDENTITY_PUBLICKEY_BYTES 字节
 *  @return 文本为空、非法 base64、或长度不等于 32 字节时返回 false。
 *          <u>解析失败必须视为不能连接</u>，不可退回明文。
 */
bool parseServerPublicKey(const std::string& text, unsigned char* keyOut);

/**
 *  取公钥指纹：公钥前 8 字节的十六进制文本（16 个字符），用于日志与人工核对
 *  ——排查「客户端配的公钥和服务端的对不上」这类问题时，两边比指纹即可。
 *  与服务端 ServerIdentity::fingerprint() 取值口径一致，两边打出来的应当相同。
 *    key：公钥字节，须有 IDENTITY_PUBLICKEY_BYTES 字节；传 NULL 返回空串
 *  @return 十六进制小写文本；参数为空时返回空串。
 */
std::string publicKeyFingerprint(const unsigned char* key);

/**
 *  一条连接的帧体加解密器。握手完成后由 ClientHandshake 交出。
 *
 *  <u>有状态且要求同方向按序处理</u>：内部 nonce 由库自增管理，帧乱序会导致认证失败。
 *  本类内部各方向一把锁，跨线程调用安全。
 */
class FrameCipher
{
public:
    FrameCipher();
    ~FrameCipher();

    /**
     *  加密一帧包体。
     *    plain：   待加密的包体（可能已被 zlib 压缩过）
     *    ad/adlen：附加认证数据，取<u>帧头前 20 字节</u>（除 headercrc 外的全部字段），
     *              使 cmd / seq / flags / 长度均无法被篡改
     *    cipherOut：输出密文，长度为 plain.size() + STREAM_OVERHEAD_BYTES
     */
    bool encrypt(const std::string& plain, const unsigned char* ad, size_t adlen,
                 std::string& cipherOut);

    /**
     *  解密一帧包体。参数含义同 encrypt。
     *  @return 失败时该方向的流已不可恢复，调用方必须断开重连，
     *          不得跳过本帧继续解下一帧。
     */
    bool decrypt(const std::string& cipher, const unsigned char* ad, size_t adlen,
                 std::string& plainOut);

    bool ready() const { return m_ready; }

private:
    friend class ClientHandshake;

    //把两个方向的流状态清空、擦除密钥，回到"未就绪"。供 ClientHandshake::reset 调用。
    //内部同时持有收发两把锁，故与正在进行的 encrypt / decrypt 不会互相踩踏。
    void reset();

    bool initSender(const unsigned char* txKey, std::string& streamHeaderOut);
    bool initReceiver(const unsigned char* rxKey, const std::string& peerStreamHeader);

    FrameCipher(const FrameCipher&);
    FrameCipher& operator=(const FrameCipher&);

    struct Impl;
    Impl*      m_impl;
    std::mutex m_sendMutex;
    std::mutex m_recvMutex;
    bool       m_ready;
};

/**
 *  客户端侧的握手状态机。一条连接一个实例。
 *
 *  用法：
 *      ClientHandshake hs;
 *      发送 cmd_handshake，包体 = hs.buildClientHello();
 *      收到应答后 hs.onServerHello(body, serverPubKey, err);
 *      再发一帧 cmd_handshake，包体 = hs.buildClientStreamHeader();
 *      此后 hs.ready() 为真，用 hs.cipher() 加解密所有业务帧。
 */
class ClientHandshake
{
public:
    ClientHandshake();

    /**
     *  产出握手第 1 帧（ClientHello）的包体，并在内部生成本次连接的一次性密钥对。
     *  @return 请求 JSON；生成失败返回空串（调用方应放弃连接）。
     */
    std::string buildClientHello();

    /**
     *  处理握手第 2 帧（ServerHello）：验签并导出会话密钥。
     *    responseBody： 服务端应答 JSON
     *    serverPubKey： <u>预置的</u>服务端身份公钥（IDENTITY_PUBLICKEY_BYTES 字节）
     *    errOut：       失败原因（英文）
     *  @return 验签不过意味着对端不是我们认识的服务器（可能是中间人），
     *          必须断开连接，<u>不得</u>提示用户"是否继续"。
     */
    bool onServerHello(const std::string& responseBody, const unsigned char* serverPubKey,
                       std::string& errOut);

    /**
     *  产出握手第 3 帧的包体（本端发送流的流头）。成功后 ready() 变为 true。
     *  必须在 onServerHello 成功之后调用 —— 会话密钥要等服务端公钥到齐才能导出。
     */
    std::string buildClientStreamHeader();

    bool ready() const { return m_state == state_done; }

    /**
     *  把握手状态清回初始，供<u>同一个连接对象重连时复用</u>。
     *
     *  客户端的连接对象（CIUSocket 单例、FileServerConn 实例等）会在断线后重建
     *  socket 再连一次，那是一条<u>全新的连接</u>，必须重新握手、换一套会话密钥；
     *  沿用上一条连接的密钥会让流状态与对端完全对不上，表现为每一帧都解密失败。
     *
     *  这里做成"重置状态"而不是"销毁再新建一个对象"，是刻意的：连接对象上的发送
     *  线程与收取线程会通过 cipher() 读这个握手器，销毁对象会留下野指针、直接崩溃，
     *  而重置最坏只是让在途的那一帧解密失败、断开重连，代价小得多。
     *
     *  副作用：擦除本次连接的一次性密钥与两个方向的流状态。
     */
    void reset();

    FrameCipher& cipher() { return m_cipher; }

private:
    enum State
    {
        state_init = 0,        //尚未发出 ClientHello
        state_wait_server = 1, //已发出 ClientHello，等 ServerHello
        state_wait_send = 2,   //已验签并导出密钥，待发出本端流头
        state_done = 3,        //握手完成
    };

    ClientHandshake(const ClientHandshake&);
    ClientHandshake& operator=(const ClientHandshake&);

    State         m_state;
    FrameCipher   m_cipher;
    unsigned char m_pk[KX_PUBLICKEY_BYTES];        //本次连接的一次性公钥
    unsigned char m_sk[KX_SECRETKEY_BYTES];        //本次连接的一次性私钥，用完擦除
    unsigned char m_nonce[HANDSHAKE_NONCE_BYTES];  //本端随机数，验签时要用
    std::string   m_streamHeader;                  //本端发送流的流头，第 3 帧发出
};

} // namespace net
