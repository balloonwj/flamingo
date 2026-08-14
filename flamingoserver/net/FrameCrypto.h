/**
 *  v2 协议的帧体加密：服务端侧的身份密钥、三帧握手与帧体加解密。
 *
 *  设计与线上格式见 docs/协议加密方案.md，本文件只做实现说明。三件事：
 *
 *    1. <u>身份密钥</u>（Ed25519）：服务端长期密钥，只有它能通过客户端的验签，
 *       中间人因此插不进来。种子存文件、公钥分发给客户端。
 *    2. <u>握手</u>（走 cmd_handshake，明文）：客户端送来一次性公钥，服务端也生成
 *       一次性密钥对，双方各自导出会话密钥；服务端用身份私钥对握手记录签名，
 *       客户端用预置公钥验签。
 *    3. <u>帧体加解密</u>（XChaCha20-Poly1305 流加密）：握手完成后所有帧体都过它。
 *
 *  与压缩的顺序<u>不可颠倒</u>：发送时先 zlib 压缩、后加密；接收时先解密、后解压。
 *  密文是高熵数据压不动，顺序反了压缩等于白做。
 *
 *  客户端有一份镜像实现（flamingoclient/Source/net/FrameCrypto.h），线上格式必须
 *  逐字节一致；改动本文件的格式时<u>必须同步改那一份</u>，并更新两侧的测试向量。
 *
 *  balloonwj@qq.com   2026-08-12
 */
#pragma once

#include <stdint.h>
#include <string>
#include <mutex>

namespace net {

//==================== 尺寸常量 ====================
//与 libsodium 的对应常量一致；单独列出是为了让调用方不必包含 sodium.h。
//数值经实测核对（libsodium 1.0.22）。

//Ed25519 身份公钥字节数
const size_t IDENTITY_PUBLICKEY_BYTES = 32;
//Ed25519 身份私钥字节数
const size_t IDENTITY_SECRETKEY_BYTES = 64;
//Ed25519 身份种子字节数；只需持久化它，公私钥可由它确定性推出
const size_t IDENTITY_SEED_BYTES = 32;
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
//握手随机数字节数；两侧各一个，纳入握手记录使每次握手互不相同
const size_t HANDSHAKE_NONCE_BYTES = 16;
//握手协议版本号，写进握手记录并参与签名
const uint8_t HANDSHAKE_VERSION = 1;

//附加认证数据的字节数，见 buildFrameAd
const size_t FRAME_AD_BYTES = 16;

/**
 *  初始化底层密码学库。进程内调用一次即可，多次调用安全。
 *  必须在使用本文件其它任何功能之前、且在创建工作线程之前调用。
 *  @return 成功返回 true；返回 false 时所有加密功能不可用，调用方应终止相关流程。
 */
bool cryptoInit();

/**
 *  由帧头字段拼出「附加认证数据」（AD），供 FrameCipher 的加解密使用。
 *
 *  AD 参与认证但不被加密：把 cmd / seq / flags / 两个长度都纳进来之后，攻击者改动
 *  帧头任何一个字段，接收方解密都会失败 —— 也就无法把一帧改投成另一个 cmd、
 *  无法替换序列号、无法伪造长度。
 *
 *  这里<u>不直接取帧头的原始字节</u>，而是按固定布局重新拼一遍，原因有二：
 *  一是发送侧的帧头由 FrameCodec 内部生成、外部拿不到；二是这样两端都不依赖
 *  帧头的内存布局，日后帧头若增删保留字段也不会牵动加密。
 *
 *  布局（共 FRAME_AD_BYTES 字节，全部大端序）：
 *      cmd(2) | flags(1) | 固定 0(1) | seq(4) | bodylen(4) | originlen(4)
 *
 *  <u>两端必须逐字节一致</u>，改动这里必须同步改另一端。
 *
 *    flags：    应传<u>最终写进帧头的</u>标志位（已含 frame_flag_encrypted）
 *    bodylen：  线上包体长度，即密文长度
 *    originlen：最原始的明文长度（压缩前、加密前）
 *    out：      输出缓冲，须有 FRAME_AD_BYTES 字节
 */
void buildFrameAd(uint16_t cmd, uint32_t seq, uint8_t flags,
                  uint32_t bodylen, uint32_t originlen, unsigned char* out);

/**
 *  进程级加密配置。由各服务的 main 在启动时装载一次，之后各会话只读，无需加锁。
 *
 *    keyFilePath：身份密钥种子文件路径；<u>三个服务必须指向同一个文件</u>，
 *                 否则客户端要为每个服务配一把公钥。
 *    enabled：    配置项 encryption 的值。false 时不加载密钥、全程明文。
 *    errOut：     失败原因（英文）。
 *  @return 成功返回 true。enabled 为 true 但密钥加载失败时返回 false ——
 *          此时<u>不可退回明文继续跑</u>，调用方应终止启动，否则会在管理员
 *          以为已加密的情况下裸奔。
 */
bool cryptoConfigInit(const std::string& keyFilePath, bool enabled, std::string& errOut);

//本进程是否启用加密。未调用 cryptoConfigInit 时返回 false。
bool cryptoEncryptionEnabled();

//本进程的服务端身份密钥；未启用加密时其 valid() 为 false。
const class ServerIdentity& cryptoServerIdentity();

/**
 *  服务端长期身份密钥（Ed25519）。私钥只存在服务端，绝不外传、绝不进日志。
 */
class ServerIdentity
{
public:
    ServerIdentity();
    ~ServerIdentity();

    /**
     *  从种子文件载入身份密钥；文件不存在时生成一份新的并写入。
     *    keyFilePath：种子文件路径。内容为 64 个十六进制字符（32 字节种子）。
     *                 新建时权限设为仅属主可读写。
     *    errOut：     失败原因（英文，可直接进日志）。
     *  @return 成功返回 true。失败时本对象不可用。
     */
    bool loadOrCreate(const std::string& keyFilePath, std::string& errOut);

    //公钥字节（IDENTITY_PUBLICKEY_BYTES 字节）；未成功载入时返回 NULL
    const unsigned char* publicKey() const;

    //公钥的 base64 文本，用于写 .pub 文件与分发给客户端
    std::string publicKeyBase64() const;

    //公钥指纹：前 8 字节的十六进制。<u>日志里只允许打它，不允许打完整密钥</u>
    std::string fingerprint() const;

    //对任意消息签名，供握手使用。失败返回空串
    std::string sign(const std::string& message) const;

    bool valid() const { return m_valid; }

private:
    ServerIdentity(const ServerIdentity&);
    ServerIdentity& operator=(const ServerIdentity&);

    unsigned char m_pk[IDENTITY_PUBLICKEY_BYTES];   //公钥
    unsigned char m_sk[IDENTITY_SECRETKEY_BYTES];   //私钥，析构时擦除
    bool          m_valid;                          //是否已成功载入
};

/**
 *  一条连接的帧体加解密器。握手完成后由 ServerHandshake 交出。
 *
 *  <u>有状态且非线程安全</u>：内部 nonce 由库自增管理，同一方向的调用必须串行。
 *  本类内部各方向一把锁，故跨线程调用是安全的，但仍要求<u>同一方向的帧按序处理</u>
 *  —— 乱序会导致认证失败。
 */
class FrameCipher
{
public:
    FrameCipher();
    ~FrameCipher();

    /**
     *  加密一帧包体。
     *    plain：  待加密的包体（可能已被 zlib 压缩过）
     *    ad/adlen：附加认证数据，取<u>帧头前 20 字节</u>（除 headercrc 外的全部字段）。
     *             它参与认证但不被加密，使 cmd / seq / flags / 长度都无法被篡改。
     *    cipherOut：输出密文，长度为 plain.size() + STREAM_OVERHEAD_BYTES
     *  @return 成功返回 true。
     */
    bool encrypt(const std::string& plain, const unsigned char* ad, size_t adlen,
                 std::string& cipherOut);

    /**
     *  解密一帧包体。参数含义同 encrypt。
     *  @return 成功返回 true；<u>返回 false 时该方向的流已不可恢复</u>，调用方必须
     *          断开连接，不得跳过本帧继续解下一帧。
     */
    bool decrypt(const std::string& cipher, const unsigned char* ad, size_t adlen,
                 std::string& plainOut);

    bool ready() const { return m_ready; }

private:
    friend class ServerHandshake;

    //由握手方在导出会话密钥后调用；txKey 用于发送、rxKey 用于接收
    //  streamHeaderOut：输出本端发送流的流头，须发给对端
    bool initSender(const unsigned char* txKey, std::string& streamHeaderOut);
    //  peerStreamHeader：对端发来的流头
    bool initReceiver(const unsigned char* rxKey, const std::string& peerStreamHeader);

    FrameCipher(const FrameCipher&);
    FrameCipher& operator=(const FrameCipher&);

    struct Impl;
    Impl*      m_impl;        //持有两个方向的 secretstream 状态
    std::mutex m_sendMutex;   //发送方向串行化
    std::mutex m_recvMutex;   //接收方向串行化
    bool       m_ready;       //收发两个方向都初始化完毕
};

//握手状态机，定义在本文件靠后处；processInboundFrame 按引用取用
class ServerHandshake;

//收包时加密前置处理的结果，见 processInboundFrame
enum frame_security_result
{
    //本帧可继续走解压与业务分发，body 已是明文
    frame_security_ok = 0,
    //本帧是握手帧且已处理完毕，调用方不应再往下走业务分发；
    //若 responseOut 非空，须把它作为 cmd_handshake 的包体明文发回客户端
    frame_security_handled = 1,
    //协议错误，调用方必须记录 errOut 并<u>关闭连接</u>，不得跳过本帧继续
    frame_security_error = 2,
};

/**
 *  服务端会话层收到一帧后、业务分发之前的加密前置处理：分派握手帧、校验加密
 *  状态是否合规、把密文包体还原成明文。
 *
 *  抽在这里由两套会话共用（chatserver 的 ChatSession 与 fileserver / imgserver 的
 *  FileSession 各有一套互相独立的 TcpSession 基类），免得改了一处漏掉另一处。
 *
 *  <u>必须在 zlib 解压之前调用</u>：线上顺序是先压缩后加密，收包自然要先解密再解压。
 *
 *    handshake：  本连接的握手状态，由会话对象持有
 *    cmd/seq/flags/originlen：本帧帧头字段，原样传入，用于构造附加认证数据
 *    body：       入参为线上包体；本帧为密文且解密成功时，被替换为明文
 *    responseOut：需要回给客户端的握手应答包体，无应答时为空串
 *    errOut：     出错原因（英文，可直接进日志）
 */
frame_security_result processInboundFrame(ServerHandshake& handshake,
                                          uint16_t cmd, uint32_t seq, uint8_t flags,
                                          uint32_t originlen,
                                          std::string& body,
                                          std::string& responseOut,
                                          std::string& errOut);

/**
 *  收包时的握手分派与加密状态校验，<u>不含包体还原</u>。
 *
 *  这是 processInboundFrame 的前半段，单独导出是给<u>中转通道</u>用的。imgserver
 *  的 RelaySession 需要完全相同的这套判断（握手帧要分派、业务帧要求握手已完成且
 *  必须是密文），但它的数据帧只解密、不解压，没法照用 processInboundFrame 里的
 *  还原步骤。把判断单独暴露出来，两条路径就仍然共用同一份策略，日后改判定条件
 *  不会漏改一处。
 *
 *  <u>返回 frame_security_ok 时包体尚未还原</u>，调用方必须自己调用
 *  decodeFramePayload（普通场景）或 decryptFramePayload（中转场景）。
 *
 *    handshake：  本连接的握手状态，由会话对象持有
 *    cmd：        本帧的 cmd，用于识别握手帧
 *    flags：      本帧的 flags，用于判断线上包体是不是密文
 *    body：       线上包体。本帧为握手帧时按明文 JSON 解析，其余情况不读取；
 *                 <u>本函数不修改它</u>
 *    responseOut：需要回给客户端的握手应答包体，无应答时为空串
 *    errOut：     出错原因（英文，可直接进日志）
 *  @return 见 enum frame_security_result。
 */
frame_security_result checkInboundFrameSecurity(ServerHandshake& handshake,
                                                uint16_t cmd, uint8_t flags,
                                                const std::string& body,
                                                std::string& responseOut,
                                                std::string& errOut);

/**
 *  服务端侧的握手状态机。一条连接一个实例。
 */
class ServerHandshake
{
public:
    ServerHandshake();

    /**
     *  处理握手第 1 帧（ClientHello），产出第 2 帧（ServerHello）的包体。
     *    requestBody： 客户端送来的 JSON
     *    identity：    服务端身份密钥
     *    responseOut： 输出应答 JSON
     *    errOut：      失败原因（英文）
     *  @return 成功返回 true；失败时调用方应断开连接。
     */
    bool onClientHello(const std::string& requestBody, const ServerIdentity& identity,
                       std::string& responseOut, std::string& errOut);

    /**
     *  处理握手第 3 帧（客户端发来的流头）。成功后 ready() 变为 true。
     *    requestBody：客户端送来的 JSON
     *    errOut：     失败原因（英文）
     */
    bool onClientStreamHeader(const std::string& requestBody, std::string& errOut);

    /**
     *  握手帧（cmd_handshake）的统一入口，按内部状态自动分派到上面两步。
     *  会话层把收到的握手帧原样交进来即可，不必关心当前是第几帧。
     *    requestBody：客户端送来的 JSON
     *    identity：   服务端身份密钥
     *    responseOut：需要回给客户端的应答包体；<u>第 3 帧无应答</u>，此时为空串
     *    errOut：     失败原因（英文）
     *  @return 成功返回 true；失败时调用方必须断开连接 ——
     *          握手失败不重试、不降级为明文。
     */
    bool onHandshakeFrame(const std::string& requestBody, const ServerIdentity& identity,
                          std::string& responseOut, std::string& errOut);

    //握手是否已完成；完成后方可使用 cipher()
    bool ready() const { return m_state == state_done; }

    //本连接的帧体加解密器；ready() 为 false 时内容无意义
    FrameCipher& cipher() { return m_cipher; }

private:
    enum State
    {
        state_init = 0,        //尚未收到 ClientHello
        state_wait_header = 1, //已回 ServerHello，等客户端流头
        state_done = 2,        //握手完成
    };

    ServerHandshake(const ServerHandshake&);
    ServerHandshake& operator=(const ServerHandshake&);

    State         m_state;
    FrameCipher   m_cipher;
    unsigned char m_rxKey[KX_SESSIONKEY_BYTES];   //接收方向会话密钥，等流头到达后才用
};

} // namespace net
