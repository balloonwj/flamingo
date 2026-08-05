/**
 *  SHA-256 哈希工具单元测试（Suite SHA），Sha256Tests.cpp
 *  覆盖：空串 / "abc" 已知标准向量、输出格式（64 字符小写 hex）、
 *       确定性与区分性（含中文 UTF-8 字节）。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-05-22
 */
#include "catch_amalgamated.hpp"
#include "../net/Sha256.h"
#include <string>

using namespace net;

TEST_CASE("SHA1 空串的 SHA-256 标准向量")
{
    //SHA-256("") 的标准值
    CHECK(Sha256::hexDigest(std::string())
          == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("SHA2 abc 的 SHA-256 标准向量")
{
    //SHA-256("abc") 的标准值
    CHECK(Sha256::hexDigest(std::string("abc"))
          == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("SHA3 输出恒为 64 个小写十六进制字符")
{
    std::string d = Sha256::hexDigest(std::string("flamingo"));
    REQUIRE(d.size() == 64);
    for (size_t i = 0; i < d.size(); ++i)
    {
        char c = d[i];
        bool isLowerHex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        CHECK(isLowerHex);
    }
}

TEST_CASE("SHA4 确定性与区分性（含中文 UTF-8 字节）")
{
    //同一输入两次结果一致
    std::string a1 = Sha256::hexDigest(std::string("password123"));
    std::string a2 = Sha256::hexDigest(std::string("password123"));
    CHECK(a1 == a2);

    //不同输入结果不同
    CHECK(Sha256::hexDigest(std::string("password123"))
          != Sha256::hexDigest(std::string("password124")));

    //中文 UTF-8 字节串（"你好" 的 UTF-8 编码）：可正常哈希且具确定性。
    //此处用 \x 转义而非中文字面量，避免依赖源文件编码。
    std::string utf8cn("\xE4\xBD\xA0\xE5\xA5\xBD");
    std::string h1 = Sha256::hexDigest(utf8cn);
    CHECK(h1.size() == 64);
    CHECK(h1 == Sha256::hexDigest(utf8cn));
}
