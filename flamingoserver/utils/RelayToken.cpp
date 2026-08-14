/**
 *  远程桌面中转令牌实现，RelayToken.cpp
 *  HMAC-MD5 计算与 FileToken.cpp 同款（各自自带，互不依赖）。
 *  balloonwj@qq.com   2026-06-08
 */
#include "RelayToken.h"
#include <sstream>
#include <vector>
#include <stdlib.h>
#include "MD5.h"

//以下六个角色常量在 RelayToken.h 的类内已给出初始化值，这里补上类外定义。
//C++11 规定：类内初始化的静态常量一旦被 ODR-use（取地址、绑定到引用等），
//就必须在某个编译单元里有唯一的类外定义，否则链接期报未定义符号。
//单元测试中 doctest 的 CHECK 宏按引用捕获操作数，正好构成 ODR-use。
const int RelayToken::role_controller;
const int RelayToken::role_controlled;
const int RelayToken::role_caller;
const int RelayToken::role_callee;
const int RelayToken::role_folder_sender;
const int RelayToken::role_folder_receiver;

//HMAC 分组字节数：MD5 的分组长度为 64 字节
static const size_t HMAC_BLOCK_SIZE = 64;
//HMAC 内层填充常量
static const unsigned char HMAC_IPAD = 0x36;
//HMAC 外层填充常量
static const unsigned char HMAC_OPAD = 0x5c;
//MD5 原始摘要长度，单位字节
static const size_t MD5_DIGEST_LEN = 16;
//令牌 payload 与签名之间的分隔符
static const char TOKEN_SEP = '.';
//令牌 payload 内各字段之间的分隔符
static const char FIELD_SEP = '|';
//令牌 payload 的字段个数：sessionid|role|uid|peeruid|expire
static const size_t TOKEN_FIELD_COUNT = 5;

//计算一段数据的 MD5 原始摘要（16 字节，非十六进制）
static std::string rawMd5(const std::string& data)
{
    MD5 md5;
    md5.update(data.data(), data.size());
    const unsigned char* digest = md5.digest();
    return std::string(reinterpret_cast<const char*>(digest), MD5_DIGEST_LEN);
}

//计算 HMAC-MD5(key, msg)，返回十六进制字符串
static std::string hmacMd5Hex(const std::string& key, const std::string& msg)
{
    //密钥长度超过分组长度时，先取其 MD5 摘要作为有效密钥
    std::string k = key;
    if (k.size() > HMAC_BLOCK_SIZE)
        k = rawMd5(k);
    //不足分组长度则以 0 右补齐
    k.resize(HMAC_BLOCK_SIZE, '\0');

    std::string ipad(HMAC_BLOCK_SIZE, '\0');
    std::string opad(HMAC_BLOCK_SIZE, '\0');
    for (size_t i = 0; i < HMAC_BLOCK_SIZE; ++i)
    {
        unsigned char kb = static_cast<unsigned char>(k[i]);
        ipad[i] = static_cast<char>(kb ^ HMAC_IPAD);
        opad[i] = static_cast<char>(kb ^ HMAC_OPAD);
    }

    //内层：MD5(ipad || msg)
    MD5 innerMd5;
    innerMd5.update(ipad.data(), ipad.size());
    innerMd5.update(msg.data(), msg.size());
    const unsigned char* innerDigest = innerMd5.digest();
    std::string innerRaw(reinterpret_cast<const char*>(innerDigest), MD5_DIGEST_LEN);

    //外层：MD5(opad || 内层摘要)，输出十六进制
    MD5 outerMd5;
    outerMd5.update(opad.data(), opad.size());
    outerMd5.update(innerRaw.data(), innerRaw.size());
    return outerMd5.toString();
}

//把 payload 文本按 FIELD_SEP 切分为字段列表
static void splitFields(const std::string& payload, std::vector<std::string>& out)
{
    out.clear();
    std::string cur;
    for (size_t i = 0; i < payload.size(); ++i)
    {
        if (payload[i] == FIELD_SEP)
        {
            out.push_back(cur);
            cur.clear();
        }
        else
        {
            cur.push_back(payload[i]);
        }
    }
    out.push_back(cur);
}

std::string RelayToken::sign(const RelayTokenPayload& payload, const std::string& key)
{
    //拼装明文 payload：sessionid|role|uid|peeruid|expire
    std::ostringstream osPayload;
    osPayload << payload.sessionid << FIELD_SEP
              << payload.role << FIELD_SEP
              << payload.uid << FIELD_SEP
              << payload.peeruid << FIELD_SEP
              << payload.expire;
    std::string payloadText = osPayload.str();

    //令牌 = payload + "." + HMAC 签名
    std::string sig = hmacMd5Hex(key, payloadText);
    return payloadText + TOKEN_SEP + sig;
}

bool RelayToken::verify(const std::string& token, const std::string& key,
                        int64_t nowMs, RelayTokenPayload& out)
{
    //以首个 "." 切分 payload 与签名；payload 各字段不含 "."，故无歧义
    std::string::size_type dotPos = token.find(TOKEN_SEP);
    if (dotPos == std::string::npos)
        return false;

    std::string payloadText = token.substr(0, dotPos);
    std::string sig = token.substr(dotPos + 1);

    //重算签名并比对，比对失败说明令牌被篡改或密钥不符
    std::string expectSig = hmacMd5Hex(key, payloadText);
    if (sig != expectSig)
        return false;

    //拆解 payload 字段
    std::vector<std::string> fields;
    splitFields(payloadText, fields);
    if (fields.size() != TOKEN_FIELD_COUNT)
        return false;

    RelayTokenPayload parsed;
    parsed.sessionid = fields[0];
    parsed.role      = atoi(fields[1].c_str());
    parsed.uid       = atoll(fields[2].c_str());
    parsed.peeruid   = atoll(fields[3].c_str());
    parsed.expire    = atoll(fields[4].c_str());

    //会话 id 不得为空
    if (parsed.sessionid.empty())
        return false;

    //过期判定：当前时间到达或越过过期时间即失效
    if (parsed.expire > 0 && nowMs >= parsed.expire)
        return false;

    out = parsed;
    return true;
}
