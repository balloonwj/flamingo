/**
 *  SHA-256 哈希工具实现，Sha256.cpp
 *
 *  基于 Windows CNG（bcrypt）的 BCRYPT_SHA256_ALGORITHM 实现。
 *
 *  balloonwj@qq.com   2026-05-22
 */
#include "Sha256.h"
#include <windows.h>
#include <bcrypt.h>

//链接 Windows CNG 库；用 pragma 就近声明，免去工程文件单独配置
#pragma comment(lib, "bcrypt.lib")

namespace net {

//SHA-256 摘要的字节数：算法固定输出 256 位 = 32 字节
constexpr size_t SHA256_DIGEST_SIZE = 32;

//十六进制数字字符表（小写），摘要字节转 hex 时查表用
static const char kHexDigitsLower[] = "0123456789abcdef";

//把一个字节按两个小写十六进制字符追加进 out
static void appendHexByte(std::string& out, unsigned char b)
{
    out.push_back(kHexDigitsLower[(b >> 4) & 0x0F]);
    out.push_back(kHexDigitsLower[b & 0x0F]);
}

std::string Sha256::hexDigest(const void* data, size_t len)
{
    //len 非 0 时 data 必须有效；len 为 0（空输入）允许 data 为 nullptr
    if (data == nullptr && len != 0)
        return std::string();

    BCRYPT_ALG_HANDLE  hAlg  = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    unsigned char digest[SHA256_DIGEST_SIZE] = { 0 };
    bool ok = false;

    //打开 SHA-256 算法提供者
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (BCRYPT_SUCCESS(status))
    {
        //创建哈希对象
        status = BCryptCreateHash(hAlg, &hHash, NULL, 0, NULL, 0, 0);
        if (BCRYPT_SUCCESS(status))
        {
            //喂入待哈希数据（len 为 0 时 CNG 接受空输入）
            status = BCryptHashData(hHash,
                                    static_cast<PUCHAR>(const_cast<void*>(data)),
                                    static_cast<ULONG>(len), 0);
            if (BCRYPT_SUCCESS(status))
            {
                //取出最终 32 字节摘要
                status = BCryptFinishHash(hHash, digest, SHA256_DIGEST_SIZE, 0);
                if (BCRYPT_SUCCESS(status))
                    ok = true;
            }
            BCryptDestroyHash(hHash);
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }

    if (!ok)
        return std::string();

    //32 字节摘要转 64 字符小写 hex
    std::string result;
    result.reserve(SHA256_DIGEST_SIZE * 2);
    for (size_t i = 0; i < SHA256_DIGEST_SIZE; ++i)
        appendHexByte(result, digest[i]);
    return result;
}

std::string Sha256::hexDigest(const std::string& data)
{
    return hexDigest(data.data(), data.size());
}

} // namespace net
