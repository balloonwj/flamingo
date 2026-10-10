/**
 *  v2 协议帧体加密的服务端实现。设计见 docs/协议加密方案.md。
 *
 *  本文件<u>不自己实现任何密码学算法</u>，全部调用 libsodium 的高层 API：
 *  Ed25519 做身份签名、X25519 做密钥交换、XChaCha20-Poly1305 流加密做帧体加解密。
 *  选高层 API 的理由是它不暴露模式 / 填充 / nonce 的选择，能用错的地方少得多；
 *  尤其是 nonce 由库内部自增管理，避开了 AEAD 最常见的那类致命误用。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#include "FrameCrypto.h"

#include <sodium.h>
#include <string.h>
#include <stdio.h>
#include <fstream>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#endif

#include "../jsoncpp1.9.0/json.h"
//cmd_handshake 与 frame_flag_encrypted。只在 .cpp 里包含：Protocol.h 与遗留的
//v1 协议头 AdminApiCodes.h 定义了同名枚举，若放进 FrameCrypto.h 会污染包含它的会话头文件。
#include "../chatserversrc/Protocol.h"
#include "FramePayload.h"

namespace net {

namespace {

//指纹取公钥前多少字节转十六进制。8 字节（16 个十六进制字符）足以人工核对，
//又不至于把整把公钥写进日志。
const size_t FINGERPRINT_BYTES = 8;

//公钥文件名 = 种子文件名 + 该后缀，与 ssh 的 id_xxx / id_xxx.pub 惯例一致
const char* const PUBLIC_KEY_FILE_SUFFIX = ".pub";

//把二进制转 base64（标准变体，带填充）。
std::string toBase64(const unsigned char* data, size_t len)
{
    if (data == NULL || len == 0)
    {
        return std::string();
    }

    const size_t bufLen = sodium_base64_ENCODED_LEN(len, sodium_base64_VARIANT_ORIGINAL);
    std::vector<char> buf(bufLen, 0);
    sodium_bin2base64(&buf[0], bufLen, data, len, sodium_base64_VARIANT_ORIGINAL);
    return std::string(&buf[0]);
}

//base64 解码，并要求解出来的长度<u>恰好</u>等于 expectLen。
//长度校验必须做：握手里的每个字段都是定长的，长度不对说明对端不是我们认识的实现，
//或者有人在改包，直接判失败比截断 / 补齐安全。
bool fromBase64Exact(const std::string& text, size_t expectLen, unsigned char* out)
{
    if (text.empty())
    {
        return false;
    }

    size_t realLen = 0;
    if (sodium_base642bin(out, expectLen, text.c_str(), text.size(),
                          NULL, &realLen, NULL, sodium_base64_VARIANT_ORIGINAL) != 0)
    {
        return false;
    }
    return realLen == expectLen;
}

//取 JSON 里的字符串字段；字段缺失或类型不对返回空串。
std::string jsonString(const Json::Value& root, const char* key)
{
    if (!root.isMember(key) || !root[key].isString())
    {
        return std::string();
    }
    return root[key].asString();
}

//把 Json::Value 序列化为紧凑文本。与 chatserversrc 里的同名做法一致 ——
//本工程把 jsoncpp 的弃用告警当错误处理，不能用旧的 Json::FastWriter。
std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//解析 JSON 文本。同样只能用新版 CharReader 接口。
bool parseJson(const std::string& text, Json::Value& out)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &out, &errs);
    delete reader;
    return ok && errs.empty() && out.isObject();
}

//拼出握手记录：ver || cpk || cnonce || spk || snonce || shdr
//签名覆盖它的全部字节。把两个随机数纳入，使每次握手的记录互不相同；把服务端流头
//纳入，防止有人替换流头。<u>两侧的拼接顺序必须完全一致</u>，改这里必须同步改客户端。
std::string buildTranscript(uint8_t version,
                            const unsigned char* cpk, const unsigned char* cnonce,
                            const unsigned char* spk, const unsigned char* snonce,
                            const unsigned char* shdr)
{
    std::string t;
    t.reserve(1 + KX_PUBLICKEY_BYTES + HANDSHAKE_NONCE_BYTES
                + KX_PUBLICKEY_BYTES + HANDSHAKE_NONCE_BYTES + STREAM_HEADER_BYTES);
    t.append(1, (char)version);
    t.append((const char*)cpk, KX_PUBLICKEY_BYTES);
    t.append((const char*)cnonce, HANDSHAKE_NONCE_BYTES);
    t.append((const char*)spk, KX_PUBLICKEY_BYTES);
    t.append((const char*)snonce, HANDSHAKE_NONCE_BYTES);
    t.append((const char*)shdr, STREAM_HEADER_BYTES);
    return t;
}

} // anonymous namespace

//==================== cryptoInit ====================

bool cryptoInit()
{
    //返回 0 为首次成功、1 表示之前已初始化过（也算成功）、-1 为失败。
    return sodium_init() >= 0;
}

void buildFrameAd(uint16_t cmd, uint32_t seq, uint8_t flags,
                  uint32_t bodylen, uint32_t originlen, unsigned char* out)
{
    if (out == NULL)
    {
        return;
    }

    //全部大端序，与协议里其余多字节字段的字节序一致。
    out[0]  = (unsigned char)((cmd >> 8) & 0xFF);
    out[1]  = (unsigned char)(cmd & 0xFF);
    out[2]  = flags;
    out[3]  = 0;                       //占位，使后续 4 字节字段自然对齐
    out[4]  = (unsigned char)((seq >> 24) & 0xFF);
    out[5]  = (unsigned char)((seq >> 16) & 0xFF);
    out[6]  = (unsigned char)((seq >> 8) & 0xFF);
    out[7]  = (unsigned char)(seq & 0xFF);
    out[8]  = (unsigned char)((bodylen >> 24) & 0xFF);
    out[9]  = (unsigned char)((bodylen >> 16) & 0xFF);
    out[10] = (unsigned char)((bodylen >> 8) & 0xFF);
    out[11] = (unsigned char)(bodylen & 0xFF);
    out[12] = (unsigned char)((originlen >> 24) & 0xFF);
    out[13] = (unsigned char)((originlen >> 16) & 0xFF);
    out[14] = (unsigned char)((originlen >> 8) & 0xFF);
    out[15] = (unsigned char)(originlen & 0xFF);
}

//==================== 进程级加密配置 ====================

namespace {

//进程级单例。装载发生在 main 里、早于任何连接建立，之后只读，故不加锁。
ServerIdentity& processIdentity()
{
    static ServerIdentity s_identity;
    return s_identity;
}

bool& processEncryptionEnabled()
{
    static bool s_enabled = false;
    return s_enabled;
}

} // anonymous namespace

bool cryptoConfigInit(const std::string& keyFilePath, bool enabled, std::string& errOut)
{
    processEncryptionEnabled() = false;

    if (!cryptoInit())
    {
        errOut = "sodium_init failed";
        return false;
    }

    if (!enabled)
    {
        //明文模式：连密钥都不加载，避免在不需要的时候在磁盘上生成身份文件。
        return true;
    }

    if (!processIdentity().loadOrCreate(keyFilePath, errOut))
    {
        //加密开着却拿不到密钥时必须让启动失败。若在此退回明文，管理员会以为
        //链路已加密、实则裸奔 —— 这种"看着安全其实不安全"的状态比直接起不来危险得多。
        return false;
    }

    processEncryptionEnabled() = true;
    return true;
}

bool cryptoEncryptionEnabled()
{
    return processEncryptionEnabled();
}

const ServerIdentity& cryptoServerIdentity()
{
    return processIdentity();
}

//==================== ServerIdentity ====================

ServerIdentity::ServerIdentity()
    : m_valid(false)
{
    memset(m_pk, 0, sizeof(m_pk));
    memset(m_sk, 0, sizeof(m_sk));
}

ServerIdentity::~ServerIdentity()
{
    //私钥用完擦掉。sodium_memzero 不会被编译器当作无用写入优化掉，memset 会。
    sodium_memzero(m_sk, sizeof(m_sk));
}

bool ServerIdentity::loadOrCreate(const std::string& keyFilePath, std::string& errOut)
{
    if (keyFilePath.empty())
    {
        errOut = "server key file path is empty";
        return false;
    }

    unsigned char seed[IDENTITY_SEED_BYTES] = { 0 };
    bool haveSeed = false;

    //先尝试读已有的种子文件。文件内容是 64 个十六进制字符，允许尾部有换行 / 空白。
    {
        std::ifstream in(keyFilePath.c_str(), std::ios::binary);
        if (in)
        {
            std::string hex((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
            while (!hex.empty()
                   && (hex[hex.size() - 1] == '\n' || hex[hex.size() - 1] == '\r'
                       || hex[hex.size() - 1] == ' ' || hex[hex.size() - 1] == '\t'))
            {
                hex.erase(hex.size() - 1);
            }

            size_t binLen = 0;
            if (sodium_hex2bin(seed, sizeof(seed), hex.c_str(), hex.size(),
                               NULL, &binLen, NULL) == 0
                && binLen == IDENTITY_SEED_BYTES)
            {
                haveSeed = true;
            }
            else
            {
                //文件在但内容不对：不敢自作主张覆盖，否则会静默换掉服务端身份、
                //让所有已分发公钥的客户端连不上。报错让人来处理。
                errOut = "server key file exists but is not a valid 32-byte hex seed: "
                         + keyFilePath;
                sodium_memzero(seed, sizeof(seed));
                return false;
            }
        }
    }

    //没有就新建一份。
    if (!haveSeed)
    {
        randombytes_buf(seed, sizeof(seed));

        char hex[IDENTITY_SEED_BYTES * 2 + 1] = { 0 };
        sodium_bin2hex(hex, sizeof(hex), seed, sizeof(seed));

        std::ofstream out(keyFilePath.c_str(), std::ios::binary | std::ios::trunc);
        if (!out)
        {
            errOut = "cannot create server key file: " + keyFilePath;
            sodium_memzero(seed, sizeof(seed));
            return false;
        }
        out.write(hex, (std::streamsize)(sizeof(hex) - 1));
        out.close();

#ifndef _WIN32
        //私钥文件只允许属主读写。Windows 下无对应的简单做法，靠目录权限保护。
        ::chmod(keyFilePath.c_str(), S_IRUSR | S_IWUSR);
#endif
    }

    //由种子确定性地推出公私钥。只持久化种子，不必存 64 字节私钥。
    crypto_sign_seed_keypair(m_pk, m_sk, seed);
    sodium_memzero(seed, sizeof(seed));

    m_valid = true;

    //把公钥以 base64 写到 <keyFilePath>.pub，管理员照抄进客户端的 serverpubkey 即可，
    //不必去日志里翻。每次启动都重写：公钥由种子唯一确定，重写不会改变服务端身份，
    //却能自愈「.pub 被误删」「换过种子而 .pub 还是旧的」这两种情况。
    //写失败不影响服务运行（只是少个便利文件），故不当作错误返回。
    const std::string pubFilePath = keyFilePath + PUBLIC_KEY_FILE_SUFFIX;
    std::ofstream pubOut(pubFilePath.c_str(), std::ios::binary | std::ios::trunc);
    if (pubOut)
    {
        const std::string pubText = publicKeyBase64();
        pubOut.write(pubText.data(), static_cast<std::streamsize>(pubText.size()));
        pubOut.put('\n');
    }

    return true;
}

const unsigned char* ServerIdentity::publicKey() const
{
    return m_valid ? m_pk : NULL;
}

std::string ServerIdentity::publicKeyBase64() const
{
    if (!m_valid)
    {
        return std::string();
    }
    return toBase64(m_pk, IDENTITY_PUBLICKEY_BYTES);
}

std::string ServerIdentity::fingerprint() const
{
    if (!m_valid)
    {
        return std::string();
    }

    char hex[FINGERPRINT_BYTES * 2 + 1] = { 0 };
    sodium_bin2hex(hex, sizeof(hex), m_pk, FINGERPRINT_BYTES);
    return std::string(hex);
}

std::string ServerIdentity::sign(const std::string& message) const
{
    if (!m_valid)
    {
        return std::string();
    }

    unsigned char sig[IDENTITY_SIGNATURE_BYTES] = { 0 };
    unsigned long long sigLen = 0;
    if (crypto_sign_detached(sig, &sigLen,
                             (const unsigned char*)message.data(), message.size(),
                             m_sk) != 0)
    {
        return std::string();
    }
    return std::string((const char*)sig, (size_t)sigLen);
}

//==================== FrameCipher ====================

struct FrameCipher::Impl
{
    crypto_secretstream_xchacha20poly1305_state pushState;   //发送方向
    crypto_secretstream_xchacha20poly1305_state pullState;   //接收方向
    bool pushReady;
    bool pullReady;

    Impl() : pushReady(false), pullReady(false)
    {
        memset(&pushState, 0, sizeof(pushState));
        memset(&pullState, 0, sizeof(pullState));
    }

    ~Impl()
    {
        sodium_memzero(&pushState, sizeof(pushState));
        sodium_memzero(&pullState, sizeof(pullState));
    }
};

FrameCipher::FrameCipher()
    : m_impl(new Impl()), m_ready(false)
{
}

FrameCipher::~FrameCipher()
{
    delete m_impl;
    m_impl = NULL;
}

bool FrameCipher::initSender(const unsigned char* txKey, std::string& streamHeaderOut)
{
    unsigned char header[STREAM_HEADER_BYTES] = { 0 };
    if (crypto_secretstream_xchacha20poly1305_init_push(&m_impl->pushState, header, txKey) != 0)
    {
        return false;
    }

    streamHeaderOut.assign((const char*)header, STREAM_HEADER_BYTES);
    m_impl->pushReady = true;
    m_ready = m_impl->pushReady && m_impl->pullReady;
    return true;
}

bool FrameCipher::initReceiver(const unsigned char* rxKey, const std::string& peerStreamHeader)
{
    if (peerStreamHeader.size() != STREAM_HEADER_BYTES)
    {
        return false;
    }

    if (crypto_secretstream_xchacha20poly1305_init_pull(
            &m_impl->pullState, (const unsigned char*)peerStreamHeader.data(), rxKey) != 0)
    {
        return false;
    }

    m_impl->pullReady = true;
    m_ready = m_impl->pushReady && m_impl->pullReady;
    return true;
}

bool FrameCipher::encrypt(const std::string& plain, const unsigned char* ad, size_t adlen,
                          std::string& cipherOut)
{
    if (!m_impl->pushReady)
    {
        return false;
    }

    std::lock_guard<std::mutex> guard(m_sendMutex);

    cipherOut.resize(plain.size() + STREAM_OVERHEAD_BYTES);
    unsigned long long cipherLen = 0;
    if (crypto_secretstream_xchacha20poly1305_push(
            &m_impl->pushState,
            (unsigned char*)&cipherOut[0], &cipherLen,
            (const unsigned char*)plain.data(), plain.size(),
            ad, adlen,
            crypto_secretstream_xchacha20poly1305_TAG_MESSAGE) != 0)
    {
        cipherOut.clear();
        return false;
    }

    cipherOut.resize((size_t)cipherLen);
    return true;
}

bool FrameCipher::decrypt(const std::string& cipher, const unsigned char* ad, size_t adlen,
                          std::string& plainOut)
{
    if (!m_impl->pullReady)
    {
        return false;
    }

    //密文至少要有固定开销那么长，否则不可能是本流产出的。
    if (cipher.size() < STREAM_OVERHEAD_BYTES)
    {
        return false;
    }

    std::lock_guard<std::mutex> guard(m_recvMutex);

    plainOut.resize(cipher.size() - STREAM_OVERHEAD_BYTES);
    unsigned long long plainLen = 0;
    unsigned char tag = 0;
    if (crypto_secretstream_xchacha20poly1305_pull(
            &m_impl->pullState,
            plainOut.empty() ? NULL : (unsigned char*)&plainOut[0], &plainLen, &tag,
            (const unsigned char*)cipher.data(), cipher.size(),
            ad, adlen) != 0)
    {
        //认证失败：被篡改、乱序、重放，或密钥不对。此时该方向的流已不可恢复，
        //调用方必须断开连接，不能跳过本帧继续解下一帧。
        plainOut.clear();
        return false;
    }

    plainOut.resize((size_t)plainLen);
    return true;
}

//==================== ServerHandshake ====================

ServerHandshake::ServerHandshake()
    : m_state(state_init)
{
    memset(m_rxKey, 0, sizeof(m_rxKey));
}

bool ServerHandshake::onClientHello(const std::string& requestBody,
                                    const ServerIdentity& identity,
                                    std::string& responseOut, std::string& errOut)
{
    if (m_state != state_init)
    {
        errOut = "unexpected ClientHello, handshake already in progress";
        return false;
    }

    if (!identity.valid())
    {
        errOut = "server identity key is not loaded";
        return false;
    }

    Json::Value root;
    if (!parseJson(requestBody, root))
    {
        errOut = "ClientHello is not valid JSON";
        return false;
    }

    //版本号必须认识。开发阶段不做兼容，遇到不认识的版本直接失败。
    const int version = root.isMember("ver") && root["ver"].isInt() ? root["ver"].asInt() : 0;
    if (version != (int)HANDSHAKE_VERSION)
    {
        errOut = "unsupported handshake version";
        return false;
    }

    unsigned char clientPk[KX_PUBLICKEY_BYTES] = { 0 };
    unsigned char clientNonce[HANDSHAKE_NONCE_BYTES] = { 0 };
    if (!fromBase64Exact(jsonString(root, "cpk"), KX_PUBLICKEY_BYTES, clientPk))
    {
        errOut = "ClientHello field 'cpk' is missing or malformed";
        return false;
    }
    if (!fromBase64Exact(jsonString(root, "cnonce"), HANDSHAKE_NONCE_BYTES, clientNonce))
    {
        errOut = "ClientHello field 'cnonce' is missing or malformed";
        return false;
    }

    //本次连接专用的一次性密钥对，用完即弃 —— 它保证会话密钥不随身份私钥泄露而泄露。
    unsigned char serverPk[KX_PUBLICKEY_BYTES] = { 0 };
    unsigned char serverSk[KX_SECRETKEY_BYTES] = { 0 };
    crypto_kx_keypair(serverPk, serverSk);

    //导出两把会话密钥。服务端用 server 版；客户端的 tx 与服务端的 rx 是同一把。
    unsigned char txKey[KX_SESSIONKEY_BYTES] = { 0 };
    const int kxRet = crypto_kx_server_session_keys(m_rxKey, txKey, serverPk, serverSk, clientPk);
    sodium_memzero(serverSk, sizeof(serverSk));
    if (kxRet != 0)
    {
        //客户端公钥非法（如低阶点）。
        sodium_memzero(txKey, sizeof(txKey));
        errOut = "client public key rejected by key exchange";
        return false;
    }

    //初始化本端发送流，得到要发给客户端的流头。
    std::string serverStreamHeader;
    const bool senderOk = m_cipher.initSender(txKey, serverStreamHeader);
    sodium_memzero(txKey, sizeof(txKey));
    if (!senderOk)
    {
        errOut = "failed to init sender stream";
        return false;
    }

    unsigned char serverNonce[HANDSHAKE_NONCE_BYTES] = { 0 };
    randombytes_buf(serverNonce, sizeof(serverNonce));

    //签名覆盖整份握手记录，客户端用预置公钥验签 —— 中间人在这一步被挡住。
    const std::string transcript = buildTranscript(
        HANDSHAKE_VERSION, clientPk, clientNonce, serverPk, serverNonce,
        (const unsigned char*)serverStreamHeader.data());
    const std::string signature = identity.sign(transcript);
    if (signature.size() != IDENTITY_SIGNATURE_BYTES)
    {
        errOut = "failed to sign handshake transcript";
        return false;
    }

    Json::Value resp;
    resp["code"]   = 0;
    resp["spk"]    = toBase64(serverPk, KX_PUBLICKEY_BYTES);
    resp["snonce"] = toBase64(serverNonce, HANDSHAKE_NONCE_BYTES);
    resp["sig"]    = toBase64((const unsigned char*)signature.data(), signature.size());
    resp["shdr"]   = toBase64((const unsigned char*)serverStreamHeader.data(),
                              serverStreamHeader.size());

    responseOut = writeCompact(resp);

    m_state = state_wait_header;
    return true;
}

bool ServerHandshake::onClientStreamHeader(const std::string& requestBody, std::string& errOut)
{
    if (m_state != state_wait_header)
    {
        errOut = "unexpected client stream header at this handshake stage";
        return false;
    }

    Json::Value root;
    if (!parseJson(requestBody, root))
    {
        errOut = "client stream header frame is not valid JSON";
        return false;
    }

    unsigned char headerBytes[STREAM_HEADER_BYTES] = { 0 };
    const std::string chdrText = jsonString(root, "chdr");
    if (!fromBase64Exact(chdrText, STREAM_HEADER_BYTES, headerBytes))
    {
        //把实际收到的文本带进错误信息：这一步失败几乎都是两端 base64 变体不一致、
        //或字段被截断，光说"格式不对"排查时还得再加一次日志才能看到内容。
        //流头不是机密（它随握手明文交换），写进日志是安全的。
        char detail[128] = { 0 };
        snprintf(detail, sizeof(detail), " (got %u chars, expect base64 of %u bytes), body: ",
                 (unsigned)chdrText.size(), (unsigned)STREAM_HEADER_BYTES);
        errOut = "field 'chdr' is missing or malformed" + std::string(detail) + requestBody;
        return false;
    }

    const std::string peerHeader((const char*)headerBytes, STREAM_HEADER_BYTES);
    const bool ok = m_cipher.initReceiver(m_rxKey, peerHeader);
    sodium_memzero(m_rxKey, sizeof(m_rxKey));
    if (!ok)
    {
        errOut = "failed to init receiver stream";
        return false;
    }

    m_state = state_done;
    return true;
}

bool ServerHandshake::onHandshakeFrame(const std::string& requestBody,
                                       const ServerIdentity& identity,
                                       std::string& responseOut, std::string& errOut)
{
    responseOut.clear();

    switch (m_state)
    {
    case state_init:
        return onClientHello(requestBody, identity, responseOut, errOut);
    case state_wait_header:
        return onClientStreamHeader(requestBody, errOut);
    default:
        //握手已完成之后又收到握手帧：正常客户端不会这么做，更像是有人想重新
        //协商一把新密钥、或试图把已加密的连接拉回明文。直接判失败。
        errOut = "handshake frame received after handshake already completed";
        return false;
    }
}

frame_security_result checkInboundFrameSecurity(ServerHandshake& handshake,
                                                uint16_t cmd, uint8_t flags,
                                                const std::string& body,
                                                std::string& responseOut,
                                                std::string& errOut)
{
    responseOut.clear();
    errOut.clear();

    const bool encrypted = (flags & frame_flag_encrypted) != 0;

    //本服务未启用加密时，只需挡住两类不该出现的帧，其余原样放行
    if (!cryptoEncryptionEnabled())
    {
        if (encrypted)
        {
            errOut = "recv encrypted frame while encryption is disabled on this server";
            return frame_security_error;
        }

        //客户端要加密、而本服务关了加密：断开。这里<u>不能静默放行</u>让它明文接着跑，
        //否则客户端会一直以为链路是加密的。
        if (cmd == cmd_handshake)
        {
            errOut = "recv handshake frame but encryption is disabled on this server";
            return frame_security_error;
        }

        //这里<u>不直接返回</u>，而是落到函数末尾统一放行 —— 未加密的帧仍然
        //可能是压缩过的，调用方那一步解压不能跳。
    }
    else
    {
        //以下都是启用了加密的情况
        if (cmd == cmd_handshake)
        {
            //握手帧必须是明文：此刻双方都还没有密钥
            if (encrypted)
            {
                errOut = "handshake frame must not be encrypted";
                return frame_security_error;
            }

            if (!handshake.onHandshakeFrame(body, cryptoServerIdentity(), responseOut, errOut))
                return frame_security_error;

            return frame_security_handled;
        }

        //握手没完成就发业务帧：可能是没升级的老客户端，也可能是有人想绕开加密
        if (!handshake.ready())
        {
            errOut = "recv business frame before handshake completed";
            return frame_security_error;
        }

        //握手完成之后，业务帧一律要求是密文，不接受与明文混发
        if (!encrypted)
        {
            errOut = "recv plaintext frame after handshake completed";
            return frame_security_error;
        }
    }

    //两条分支都汇到这里：校验通过，包体交给调用方还原
    return frame_security_ok;
}

frame_security_result processInboundFrame(ServerHandshake& handshake,
                                          uint16_t cmd, uint32_t seq, uint8_t flags,
                                          uint32_t originlen,
                                          std::string& body,
                                          std::string& responseOut,
                                          std::string& errOut)
{
    //先做握手分派与加密状态校验；出错、或本帧是已处理完的握手帧，都不需要还原包体
    frame_security_result r = checkInboundFrameSecurity(handshake, cmd, flags, body,
                                                       responseOut, errOut);
    if (r != frame_security_ok)
        return r;

    //还原包体：解密与解压都交给 FramePayload 那条管线，顺序在那里定死，此处不重复实现。
    //加密开着与关着两种情形都走到这里，于是"加密关着但包体压缩过"也能正确还原。
    //失败说明这个方向的流已不可恢复（数据被改动、乱序，或根本不是本流的密文），
    //调用方必须断开连接：<u>不能跳过本帧去解下一帧</u>，流加密的内部状态已经对不上了。
    std::string plainBody;
    if (!decodeFramePayload(cmd, seq, flags, originlen, body,
                            &handshake.cipher(), plainBody, errOut))
    {
        return frame_security_error;
    }

    body.swap(plainBody);
    return frame_security_ok;
}

//==================== PeerClientHandshake ====================
//移植自 flamingoclient/Source/net/FrameCrypto.cpp 的 ClientHandshake，线上格式两边必须一致

PeerClientHandshake::PeerClientHandshake()
    : m_state(state_init)
{
    sodium_memzero(m_pk, sizeof(m_pk));
    sodium_memzero(m_sk, sizeof(m_sk));
    sodium_memzero(m_nonce, sizeof(m_nonce));
}

PeerClientHandshake::~PeerClientHandshake()
{
    //一次性密钥用完即擦
    sodium_memzero(m_sk, sizeof(m_sk));
}

std::string PeerClientHandshake::buildClientHello()
{
    if (m_state != state_init)
        return std::string();

    //本次连接专用的一次性密钥对：即便身份私钥日后泄露，也解不开已录下的会话
    crypto_kx_keypair(m_pk, m_sk);
    randombytes_buf(m_nonce, sizeof(m_nonce));

    Json::Value root;
    root["ver"]    = (int)HANDSHAKE_VERSION;
    root["cpk"]    = toBase64(m_pk, KX_PUBLICKEY_BYTES);
    root["cnonce"] = toBase64(m_nonce, HANDSHAKE_NONCE_BYTES);

    m_state = state_wait_server;
    return writeCompact(root);
}

bool PeerClientHandshake::onServerHello(const std::string& responseBody, const unsigned char* serverPubKey,
                                        std::string& errOut)
{
    if (m_state != state_wait_server)
    {
        errOut = "unexpected ServerHello at this handshake stage";
        return false;
    }
    if (serverPubKey == NULL)
    {
        errOut = "peer server public key is not available";
        return false;
    }

    Json::Value root;
    if (!parseJson(responseBody, root))
    {
        errOut = "ServerHello is not valid JSON";
        return false;
    }
    const int code = root.isMember("code") && root["code"].isInt() ? root["code"].asInt() : -1;
    if (code != 0)
    {
        errOut = "peer server rejected the handshake";
        return false;
    }

    unsigned char serverPk[KX_PUBLICKEY_BYTES] = { 0 };
    unsigned char serverNonce[HANDSHAKE_NONCE_BYTES] = { 0 };
    unsigned char signature[IDENTITY_SIGNATURE_BYTES] = { 0 };
    unsigned char streamHeader[STREAM_HEADER_BYTES] = { 0 };
    if (!fromBase64Exact(jsonString(root, "spk"), KX_PUBLICKEY_BYTES, serverPk))
    {
        errOut = "ServerHello field 'spk' is missing or malformed";
        return false;
    }
    if (!fromBase64Exact(jsonString(root, "snonce"), HANDSHAKE_NONCE_BYTES, serverNonce))
    {
        errOut = "ServerHello field 'snonce' is missing or malformed";
        return false;
    }
    if (!fromBase64Exact(jsonString(root, "sig"), IDENTITY_SIGNATURE_BYTES, signature))
    {
        errOut = "ServerHello field 'sig' is missing or malformed";
        return false;
    }
    if (!fromBase64Exact(jsonString(root, "shdr"), STREAM_HEADER_BYTES, streamHeader))
    {
        errOut = "ServerHello field 'shdr' is missing or malformed";
        return false;
    }

    //验签：不通过说明对端不是预期的服务器，必须断开，不降级为明文
    const std::string transcript = buildTranscript(
        HANDSHAKE_VERSION, m_pk, m_nonce, serverPk, serverNonce, streamHeader);
    if (crypto_sign_verify_detached(signature, (const unsigned char*)transcript.data(), transcript.size(),
                                    serverPubKey) != 0)
    {
        errOut = "peer server signature verification failed (different server_key or MITM)";
        return false;
    }

    //导出会话密钥：主动连接的一方用 client 版，本端 tx 与对端 rx 是同一把
    unsigned char rxKey[KX_SESSIONKEY_BYTES] = { 0 };
    unsigned char txKey[KX_SESSIONKEY_BYTES] = { 0 };
    const int kxRet = crypto_kx_client_session_keys(rxKey, txKey, m_pk, m_sk, serverPk);
    sodium_memzero(m_sk, sizeof(m_sk));
    if (kxRet != 0)
    {
        sodium_memzero(rxKey, sizeof(rxKey));
        sodium_memzero(txKey, sizeof(txKey));
        errOut = "peer server public key rejected by key exchange";
        return false;
    }

    const std::string peerHeader((const char*)streamHeader, STREAM_HEADER_BYTES);
    const bool recvOk = m_cipher.initReceiver(rxKey, peerHeader);
    sodium_memzero(rxKey, sizeof(rxKey));
    if (!recvOk)
    {
        sodium_memzero(txKey, sizeof(txKey));
        errOut = "failed to init receiver stream";
        return false;
    }
    const bool sendOk = m_cipher.initSender(txKey, m_streamHeader);
    sodium_memzero(txKey, sizeof(txKey));
    if (!sendOk)
    {
        errOut = "failed to init sender stream";
        return false;
    }

    m_state = state_wait_send;
    return true;
}

std::string PeerClientHandshake::buildClientStreamHeader()
{
    if (m_state != state_wait_send || m_streamHeader.size() != STREAM_HEADER_BYTES)
        return std::string();

    Json::Value root;
    root["chdr"] = toBase64((const unsigned char*)m_streamHeader.data(), m_streamHeader.size());
    m_state = state_done;
    return writeCompact(root);
}

} // namespace net
