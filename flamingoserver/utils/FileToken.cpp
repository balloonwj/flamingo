/**
 *  文件上传 / 下载令牌实现，FileToken.cpp
 *  balloonwj@qq.com   2026-05-22
 */
#include "FileToken.h"
#include <sstream>
#include <vector>
#include <stdlib.h>
#include "MD5.h"
#include "../chatserversrc/Protocol.h"   //file_scene 枚举值

//以下两个令牌类型常量在 FileToken.h 的类内已给出初始化值，这里补上类外定义。
//原因同 RelayToken.cpp：C++11 下被 ODR-use 的类内初始化静态常量必须有类外定义，
//单元测试中 doctest 的 CHECK 宏按引用捕获操作数即构成 ODR-use。
const int FileToken::token_type_upload;
const int FileToken::token_type_download;

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
//令牌 payload 的字段个数：type|scene|md5|size|uid|expire
static const size_t TOKEN_FIELD_COUNT = 6;

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

std::string FileToken::sign(const FileTokenPayload& payload, const std::string& key)
{
    //拼装明文 payload：type|scene|md5|size|uid|expire
    std::ostringstream osPayload;
    osPayload << payload.type << FIELD_SEP
              << payload.scene << FIELD_SEP
              << payload.md5 << FIELD_SEP
              << payload.size << FIELD_SEP
              << payload.uid << FIELD_SEP
              << payload.expire;
    std::string payloadText = osPayload.str();

    //令牌 = payload + "." + HMAC 签名
    std::string sig = hmacMd5Hex(key, payloadText);
    return payloadText + TOKEN_SEP + sig;
}

bool FileToken::verify(const std::string& token, const std::string& key,
                       int64_t nowMs, FileTokenPayload& out)
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

    FileTokenPayload parsed;
    parsed.type   = atoi(fields[0].c_str());
    parsed.scene  = atoi(fields[1].c_str());
    parsed.md5    = fields[2];
    parsed.size   = atoll(fields[3].c_str());
    parsed.uid    = atoll(fields[4].c_str());
    parsed.expire = atoll(fields[5].c_str());

    //md5 字段不得为空
    if (parsed.md5.empty())
        return false;

    //过期判定：当前时间到达或越过过期时间即失效
    if (parsed.expire > 0 && nowMs >= parsed.expire)
        return false;

    out = parsed;
    return true;
}

bool FileToken::isImageScene(int scene)
{
    //头像与图片消息由 imgserver 处理
    return scene == file_scene_avatar || scene == file_scene_image_msg;
}

bool FileToken::isFileScene(int scene)
{
    //聊天文件、群文件与客户端升级包由 fileserver 处理。
    //fileserver 那道场景门禁（FileSession.cpp 的 isSceneAllowedOnServer）只委托给本函数，
    //故此处放行即等于放行整条下载链路，无需再改 fileserver。
    return scene == file_scene_chat_file || scene == file_scene_group_file
        || scene == file_scene_update;
}
