/**
 *  v2 协议帧体加密的客户端实现，是 flamingoserver/net/FrameCrypto.cpp 的镜像。
 *
 *  同样<u>不自己实现任何密码学算法</u>，全部调用 libsodium 的高层 API。改动握手记录
 *  的拼接顺序、JSON 字段名、base64 变体或附加认证数据的取法时，必须同步改服务端。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#include "FrameCrypto.h"

#include <sodium.h>
#include <string.h>
#include <vector>

#include "../third-party/jsoncpp-1.9.0/json.h"

namespace net {

namespace {

//指纹取公钥前多少字节转十六进制。8 字节（16 个十六进制字符）足以人工核对，
//与服务端 ServerIdentity::fingerprint() 取同一个值，两边打出来的应当相同。
const size_t FINGERPRINT_BYTES = 8;

//把二进制转 base64（标准变体，带填充），与服务端同一变体。
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

//base64 解码并要求长度恰好等于 expectLen。握手字段都是定长的，长度不对一律判失败。
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

std::string jsonString(const Json::Value& root, const char* key)
{
    if (!root.isMember(key) || !root[key].isString())
    {
        return std::string();
    }
    return root[key].asString();
}

//握手记录：ver || cpk || cnonce || spk || snonce || shdr
//<u>与服务端 buildTranscript 必须逐字节一致</u>，否则验签必然失败。
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

bool cryptoInit()
{
    return sodium_init() >= 0;
}

void buildFrameAd(uint16_t cmd, uint32_t seq, uint8_t flags,
                  uint32_t bodylen, uint32_t originlen, unsigned char* out)
{
    if (out == NULL)
    {
        return;
    }

    //全部大端序。<u>与服务端 buildFrameAd 必须逐字节一致</u>，否则每一帧都解不开。
    out[0]  = (unsigned char)((cmd >> 8) & 0xFF);
    out[1]  = (unsigned char)(cmd & 0xFF);
    out[2]  = flags;
    out[3]  = 0;
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

bool parseServerPublicKey(const std::string& text, unsigned char* keyOut)
{
    if (keyOut == NULL)
    {
        return false;
    }
    return fromBase64Exact(text, IDENTITY_PUBLICKEY_BYTES, keyOut);
}

std::string publicKeyFingerprint(const unsigned char* key)
{
    if (key == NULL)
    {
        return std::string();
    }

    char hex[FINGERPRINT_BYTES * 2 + 1] = { 0 };
    sodium_bin2hex(hex, sizeof(hex), key, FINGERPRINT_BYTES);
    return std::string(hex);
}

//==================== FrameCipher ====================

struct FrameCipher::Impl
{
    crypto_secretstream_xchacha20poly1305_state pushState;
    crypto_secretstream_xchacha20poly1305_state pullState;
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

void FrameCipher::reset()
{
    //同时持有收发两把锁：本函数是唯一会同时拿两把的地方，encrypt / decrypt 各只拿一把，
    //故不存在加锁顺序引起的死锁。
    std::lock_guard<std::mutex> sendGuard(m_sendMutex);
    std::lock_guard<std::mutex> recvGuard(m_recvMutex);

    m_ready = false;
    m_impl->pushReady = false;
    m_impl->pullReady = false;
    //会话密钥就藏在流状态里，用擦除而不是简单置零标志位——sodium_memzero 不会被
    //编译器当作无用写入优化掉，memset 会。
    sodium_memzero(&m_impl->pushState, sizeof(m_impl->pushState));
    sodium_memzero(&m_impl->pullState, sizeof(m_impl->pullState));
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
        plainOut.clear();
        return false;
    }

    plainOut.resize((size_t)plainLen);
    return true;
}

//==================== ClientHandshake ====================

ClientHandshake::ClientHandshake()
    : m_state(state_init)
{
    memset(m_pk, 0, sizeof(m_pk));
    memset(m_sk, 0, sizeof(m_sk));
    memset(m_nonce, 0, sizeof(m_nonce));
}

void ClientHandshake::reset()
{
    m_state = state_init;
    //一次性密钥用完即擦，不留在内存里等着被 dump 出来
    sodium_memzero(m_pk, sizeof(m_pk));
    sodium_memzero(m_sk, sizeof(m_sk));
    sodium_memzero(m_nonce, sizeof(m_nonce));
    m_streamHeader.clear();
    m_cipher.reset();
}

std::string ClientHandshake::buildClientHello()
{
    if (m_state != state_init)
    {
        return std::string();
    }

    //本次连接专用的一次性密钥对：即便服务端身份私钥日后泄露，也解不开已录下的会话。
    crypto_kx_keypair(m_pk, m_sk);
    randombytes_buf(m_nonce, sizeof(m_nonce));

    Json::Value root;
    root["ver"]    = (int)HANDSHAKE_VERSION;
    root["cpk"]    = toBase64(m_pk, KX_PUBLICKEY_BYTES);
    root["cnonce"] = toBase64(m_nonce, HANDSHAKE_NONCE_BYTES);

    Json::FastWriter writer;
    m_state = state_wait_server;
    return writer.write(root);
}

bool ClientHandshake::onServerHello(const std::string& responseBody,
                                    const unsigned char* serverPubKey,
                                    std::string& errOut)
{
    if (m_state != state_wait_server)
    {
        errOut = "unexpected ServerHello at this handshake stage";
        return false;
    }
    if (serverPubKey == NULL)
    {
        errOut = "server public key is not configured";
        return false;
    }

    Json::Reader reader;
    Json::Value  root;
    if (!reader.parse(responseBody, root) || !root.isObject())
    {
        errOut = "ServerHello is not valid JSON";
        return false;
    }

    const int code = root.isMember("code") && root["code"].isInt() ? root["code"].asInt() : -1;
    if (code != 0)
    {
        errOut = "server rejected the handshake";
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

    //验签：这是本方案抗中间人的唯一手段。不通过说明对端不是我们认识的服务器，
    //必须直接断开，不得降级为明文、也不得询问用户是否继续。
    const std::string transcript = buildTranscript(
        HANDSHAKE_VERSION, m_pk, m_nonce, serverPk, serverNonce, streamHeader);
    if (crypto_sign_verify_detached(signature,
                                    (const unsigned char*)transcript.data(), transcript.size(),
                                    serverPubKey) != 0)
    {
        errOut = "server signature verification failed (wrong server key or MITM)";
        return false;
    }

    //导出会话密钥。客户端用 client 版；本端 tx 与服务端 rx 是同一把。
    unsigned char rxKey[KX_SESSIONKEY_BYTES] = { 0 };
    unsigned char txKey[KX_SESSIONKEY_BYTES] = { 0 };
    const int kxRet = crypto_kx_client_session_keys(rxKey, txKey, m_pk, m_sk, serverPk);
    sodium_memzero(m_sk, sizeof(m_sk));
    if (kxRet != 0)
    {
        sodium_memzero(rxKey, sizeof(rxKey));
        sodium_memzero(txKey, sizeof(txKey));
        errOut = "server public key rejected by key exchange";
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

std::string ClientHandshake::buildClientStreamHeader()
{
    if (m_state != state_wait_send || m_streamHeader.size() != STREAM_HEADER_BYTES)
    {
        return std::string();
    }

    Json::Value root;
    root["chdr"] = toBase64((const unsigned char*)m_streamHeader.data(), m_streamHeader.size());

    Json::FastWriter writer;
    m_state = state_done;
    return writer.write(root);
}

} // namespace net
