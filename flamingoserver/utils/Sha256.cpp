/**
 *  SHA-256 摘要算法实现，Sha256.cpp
 *
 *  严格按 FIPS 180-4 实现，未做任何裁剪。常量表与轮函数的出处均在注释里标注了
 *  规范章节号，便于日后核对。正确性由 tests/test_sha256.cpp 用规范附录的标准
 *  测试向量把关。
 *
 *  balloonwj@qq.com   2026-08-11
 */
#include "Sha256.h"

#include <stdint.h>
#include <string.h>

namespace {

//分组长度（字节）。SHA-256 按 512 bit 分组处理消息
const size_t SHA256_BLOCK_BYTES = 64;
//摘要长度（字节）。8 个 32 位字，即 256 bit
const size_t SHA256_DIGEST_WORDS = 8;
//消息长度字段的字节数。分组末尾用 64 bit 大端存放原始消息的 bit 长度
const size_t SHA256_LENGTH_FIELD_BYTES = 8;
//一个分组内长度字段之前可容纳的字节数（64 - 8 = 56）。
//填充后若剩余数据超过它，就必须再多用一个分组来安放长度字段
const size_t SHA256_MAX_TAIL_BYTES = SHA256_BLOCK_BYTES - SHA256_LENGTH_FIELD_BYTES;
//压缩函数的轮数（FIPS 180-4 §6.2.2）
const int SHA256_ROUNDS = 64;
//消息扩展中直接来自分组的字数：前 16 个字由分组按大端解析而来
const int SHA256_WORDS_FROM_BLOCK = 16;
//一个 32 位字的位宽，供循环右移使用
const int WORD_BITS = 32;
//一字节的位宽，用于 bit 长度换算
const int BITS_PER_BYTE = 8;

//轮常量 K：前 64 个素数的立方根小数部分的前 32 bit（FIPS 180-4 §4.2.2）
const uint32_t K[SHA256_ROUNDS] =
{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

//初始哈希值 H(0)：前 8 个素数的平方根小数部分的前 32 bit（FIPS 180-4 §5.3.3）
const uint32_t H_INIT[SHA256_DIGEST_WORDS] =
{
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

//32 位循环右移。n 取值范围 [1, 31]，本文件内所有调用点均满足
uint32_t RotR(uint32_t x, int n)
{
    return (x >> n) | (x << (WORD_BITS - n));
}

//以下六个轮函数对应 FIPS 180-4 §4.1.2 的 Ch、Maj、Σ0、Σ1、σ0、σ1。
//保留规范中的命名以便逐条对照，函数名首字母大写以符合本工程的命名习惯。

uint32_t Ch(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) ^ (~x & z);
}

uint32_t Maj(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) ^ (x & z) ^ (y & z);
}

//大写 Sigma 0：用于压缩循环中的 T2
uint32_t BigSigma0(uint32_t x)
{
    return RotR(x, 2) ^ RotR(x, 13) ^ RotR(x, 22);
}

//大写 Sigma 1：用于压缩循环中的 T1
uint32_t BigSigma1(uint32_t x)
{
    return RotR(x, 6) ^ RotR(x, 11) ^ RotR(x, 25);
}

//小写 sigma 0：用于消息扩展
uint32_t SmallSigma0(uint32_t x)
{
    return RotR(x, 7) ^ RotR(x, 18) ^ (x >> 3);
}

//小写 sigma 1：用于消息扩展
uint32_t SmallSigma1(uint32_t x)
{
    return RotR(x, 17) ^ RotR(x, 19) ^ (x >> 10);
}

/**
 *  对单个 512 bit 分组做一次压缩，就地更新中间哈希值。
 *  @param state 长度为 8 的中间哈希值数组，入参为 H(i-1)，返回时已更新为 H(i)
 *  @param block 长度为 64 字节的消息分组，按大端解析
 */
void Transform(uint32_t state[SHA256_DIGEST_WORDS], const uint8_t block[SHA256_BLOCK_BYTES])
{
    uint32_t w[SHA256_ROUNDS];

    //前 16 个字直接由分组按大端拼出
    for (int i = 0; i < SHA256_WORDS_FROM_BLOCK; ++i)
    {
        w[i] = (static_cast<uint32_t>(block[i * 4]) << 24)
             | (static_cast<uint32_t>(block[i * 4 + 1]) << 16)
             | (static_cast<uint32_t>(block[i * 4 + 2]) << 8)
             | (static_cast<uint32_t>(block[i * 4 + 3]));
    }

    //其余 48 个字由消息扩展递推得到
    for (int i = SHA256_WORDS_FROM_BLOCK; i < SHA256_ROUNDS; ++i)
    {
        w[i] = SmallSigma1(w[i - 2]) + w[i - 7] + SmallSigma0(w[i - 15]) + w[i - 16];
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    uint32_t f = state[5];
    uint32_t g = state[6];
    uint32_t h = state[7];

    for (int i = 0; i < SHA256_ROUNDS; ++i)
    {
        uint32_t t1 = h + BigSigma1(e) + Ch(e, f, g) + K[i] + w[i];
        uint32_t t2 = BigSigma0(a) + Maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

} // namespace

std::string Sha256::hexDigest(const void* data, size_t length)
{
    static const char HEX[] = "0123456789abcdef";

    uint32_t state[SHA256_DIGEST_WORDS];
    memcpy(state, H_INIT, sizeof(state));

    const uint8_t* p = static_cast<const uint8_t*>(data);

    //先处理所有完整分组
    size_t full = length / SHA256_BLOCK_BYTES;
    for (size_t i = 0; i < full; ++i)
    {
        Transform(state, p + i * SHA256_BLOCK_BYTES);
    }

    //收尾：把剩余字节 + 0x80 + 若干 0x00 + 64 bit 大端消息 bit 长度凑成
    //一个或两个分组。剩余字节数超过 55 时长度字段放不下，需要两个分组。
    size_t rest = length % SHA256_BLOCK_BYTES;
    uint8_t tail[SHA256_BLOCK_BYTES * 2];
    memset(tail, 0, sizeof(tail));
    if (rest > 0)
    {
        memcpy(tail, p + full * SHA256_BLOCK_BYTES, rest);
    }
    //填充的起始位：规范要求先补一个 1 bit，字节对齐下即 0x80
    tail[rest] = 0x80;

    size_t tailBlocks = (rest < SHA256_MAX_TAIL_BYTES) ? 1 : 2;
    size_t tailBytes = tailBlocks * SHA256_BLOCK_BYTES;

    //末尾 8 字节大端存放原始消息的 bit 长度
    uint64_t bits = static_cast<uint64_t>(length) * BITS_PER_BYTE;
    for (size_t i = 0; i < SHA256_LENGTH_FIELD_BYTES; ++i)
    {
        tail[tailBytes - 1 - i] = static_cast<uint8_t>((bits >> (i * BITS_PER_BYTE)) & 0xff);
    }

    for (size_t i = 0; i < tailBlocks; ++i)
    {
        Transform(state, tail + i * SHA256_BLOCK_BYTES);
    }

    //按大端把 8 个字展开成 64 个十六进制小写字符
    std::string out;
    out.reserve(SHA256_DIGEST_WORDS * 8);
    for (size_t i = 0; i < SHA256_DIGEST_WORDS; ++i)
    {
        for (int shift = 28; shift >= 0; shift -= 4)
        {
            out.push_back(HEX[(state[i] >> shift) & 0x0f]);
        }
    }

    return out;
}

std::string Sha256::hexDigest(const std::string& input)
{
    return hexDigest(input.data(), input.size());
}
