/**
 *  v2 登录口令哈希单元测试（Suite P），test_passwordhasher.cpp
 *  覆盖：哈希已知向量、口令校验通过/不通过、加盐区分、空哈希、盐值生成。
 *  balloonwj@qq.com   2026-05-21
 */
#include "doctest.h"
#include "../utils/PasswordHasher.h"

//判断字符是否为十六进制小写数字
static bool isLowerHex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

TEST_CASE("P1 hashPassword 计算 MD5(clienthash+salt) 的已知向量")
{
    //MD5("abc" + "xyz") == MD5("abcxyz") == 70fb874a43097a25234382390c0baeb3
    CHECK(PasswordHasher::hashPassword("abc", "xyz")
          == "70fb874a43097a25234382390c0baeb3");

    //结果应为长度 32 的十六进制小写串
    std::string h = PasswordHasher::hashPassword("clienthash", "saltvalue");
    CHECK(h.size() == 32);
    bool allHex = true;
    for (size_t i = 0; i < h.size(); ++i)
    {
        if (!isLowerHex(h[i]))
            allHex = false;
    }
    CHECK(allHex);
}

TEST_CASE("P2 verifyPassword 对正确口令通过校验")
{
    std::string salt = "a1b2c3d4";
    std::string stored = PasswordHasher::hashPassword("clienthashAAA", salt);
    CHECK(PasswordHasher::verifyPassword("clienthashAAA", stored, salt));
}

TEST_CASE("P3 verifyPassword 对错误口令不通过校验")
{
    std::string salt = "a1b2c3d4";
    std::string stored = PasswordHasher::hashPassword("clienthashAAA", salt);
    CHECK_FALSE(PasswordHasher::verifyPassword("clienthashBBB", stored, salt));
}

TEST_CASE("P4 相同口令哈希配不同盐值产出不同存储哈希")
{
    std::string h1 = PasswordHasher::hashPassword("sameclienthash", "saltone");
    std::string h2 = PasswordHasher::hashPassword("sameclienthash", "salttwo");
    CHECK(h1 != h2);
}

TEST_CASE("P5 verifyPassword 在存储哈希为空时返回 false")
{
    CHECK_FALSE(PasswordHasher::verifyPassword("anyclienthash", "", "somesalt"));
}

TEST_CASE("P6 generateSalt 产出 24 位十六进制随机串且每次不同")
{
    std::string s1 = PasswordHasher::generateSalt();
    std::string s2 = PasswordHasher::generateSalt();
    CHECK(s1.size() == 24);
    CHECK(s2.size() == 24);
    CHECK(s1 != s2);

    bool allHex = true;
    for (size_t i = 0; i < s1.size(); ++i)
    {
        if (!isLowerHex(s1[i]))
            allHex = false;
    }
    CHECK(allHex);
}
