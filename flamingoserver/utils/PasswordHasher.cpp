/**
 *  v2 登录口令哈希与校验实现，PasswordHasher.cpp
 *  balloonwj@qq.com   2026-05-21
 */
#include "PasswordHasher.h"
#include "MD5.h"
#include <ctime>
#include <random>

//盐值字符个数
constexpr int SALT_LENGTH = 24;
//十六进制单字符的最大下标（HEX[0..15]）
constexpr int HEX_DIGIT_MAX = 15;

std::string PasswordHasher::hashPassword(const std::string& clienthash,
                                         const std::string& salt)
{
    MD5 md5(clienthash + salt);
    return md5.toString();
}

bool PasswordHasher::verifyPassword(const std::string& clienthash,
                                    const std::string& storedHash,
                                    const std::string& salt)
{
    //库中未设置口令哈希，一律视为校验失败
    if (storedHash.empty())
        return false;

    std::string expect = hashPassword(clienthash, salt);
    if (expect.size() != storedHash.size())
        return false;

    //定长常量时间比较：逐字节异或累积，不因首个差异字节提前返回
    unsigned char diff = 0;
    for (size_t i = 0; i < expect.size(); ++i)
        diff |= static_cast<unsigned char>(expect[i] ^ storedHash[i]);

    return diff == 0;
}

std::string PasswordHasher::generateSalt()
{
    static const char HEX[] = "0123456789abcdef";

    //生成器按线程各持一份并跨调用保持推进：避免每次新建生成器在部分平台
    //（random_device 退化为确定性实现时）反复产出相同盐值。
    static thread_local std::mt19937 gen(static_cast<uint32_t>(
        std::random_device{}() ^ static_cast<uint32_t>(time(nullptr))));
    std::uniform_int_distribution<int> dist(0, HEX_DIGIT_MAX);

    std::string salt;
    salt.reserve(SALT_LENGTH);
    for (int i = 0; i < SALT_LENGTH; ++i)
        salt.push_back(HEX[dist(gen)]);

    return salt;
}
