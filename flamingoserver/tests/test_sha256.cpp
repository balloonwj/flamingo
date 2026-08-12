/**
 *  SHA-256 摘要单元测试（Suite SHA），test_sha256.cpp
 *  覆盖：FIPS 180-4 标准测试向量、填充分组的边界长度、二进制安全（含 \0 与高位字节）、
 *        与 PasswordHasher 串起来的口令存储格式。
 *
 *  其中"填充边界"是这类实现最容易出错的地方：消息末尾要补 0x80 再补 0，最后 8 字节
 *  放消息 bit 长度；当剩余字节数达到 56 时长度字段就放不进当前分组，必须多用一个分组。
 *  55 / 56 / 63 / 64 这几个长度正好卡在这个判断的两侧。
 *
 *  balloonwj@qq.com   2026-08-11
 */
#include "doctest.h"
#include "../utils/Sha256.h"
#include "../utils/PasswordHasher.h"

#include <string>

//构造一个由重复字符组成的字符串，用于生成指定长度的测试输入
static std::string Repeat(char c, size_t n)
{
    return std::string(n, c);
}

TEST_CASE("SHA1 空输入的摘要（FIPS 180-4 标准向量）")
{
    CHECK(Sha256::hexDigest(std::string(""))
          == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("SHA2 \"abc\" 的摘要（FIPS 180-4 附录 B.1）")
{
    CHECK(Sha256::hexDigest(std::string("abc"))
          == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("SHA3 56 字节输入的摘要（FIPS 180-4 附录 B.2，跨两个填充分组）")
{
    //这条向量长 56 字节，正好触发"长度字段放不下、需再补一个分组"的分支
    std::string msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK(msg.size() == 56);
    CHECK(Sha256::hexDigest(msg)
          == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("SHA4 112 字节输入的摘要（FIPS 180-4 附录 B.3 的双分组向量）")
{
    std::string msg = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
                      "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
    CHECK(msg.size() == 112);
    CHECK(Sha256::hexDigest(msg)
          == "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
}

TEST_CASE("SHA5 一百万个 'a' 的摘要（FIPS 180-4 长消息向量）")
{
    //这条向量能验证多分组循环与 64 bit 长度字段（8000000 bit 已超出 32 bit 的常见笔误范围）
    std::string msg = Repeat('a', 1000000);
    CHECK(Sha256::hexDigest(msg)
          == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("SHA6 填充分组的边界长度 55 / 56 / 63 / 64 各自算得出确定结果")
{
    //55：填充后剩余 56 字节，长度字段刚好放得下，单分组收尾
    //56：填充后需要第二个分组
    //63/64：另一侧的边界，64 时前面已有一个完整分组
    std::string h55 = Sha256::hexDigest(Repeat('a', 55));
    std::string h56 = Sha256::hexDigest(Repeat('a', 56));
    std::string h63 = Sha256::hexDigest(Repeat('a', 63));
    std::string h64 = Sha256::hexDigest(Repeat('a', 64));

    //长度恒为 64 个十六进制字符
    CHECK(h55.size() == 64);
    CHECK(h56.size() == 64);
    CHECK(h63.size() == 64);
    CHECK(h64.size() == 64);

    //已知向量（与 openssl / python hashlib 对齐）
    CHECK(h55 == "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    CHECK(h56 == "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
    CHECK(h63 == "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34");
    CHECK(h64 == "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");

    //相邻长度的摘要必须互不相同，防止填充写坏导致长度被"吃掉"
    CHECK(h55 != h56);
    CHECK(h63 != h64);
}

TEST_CASE("SHA7 二进制安全：内嵌 \\0 与高位字节都参与运算")
{
    //std::string 允许内嵌 \0，实现里不得按 C 字符串处理而提前截断
    std::string withNul("a\0b", 3);
    CHECK(withNul.size() == 3);
    CHECK(Sha256::hexDigest(withNul) != Sha256::hexDigest(std::string("a")));
    CHECK(Sha256::hexDigest(withNul) != Sha256::hexDigest(std::string("ab")));

    //高位字节（>= 0x80）不能被当作有符号数做符号扩展
    std::string highByte;
    highByte.push_back(static_cast<char>(0xff));
    CHECK(Sha256::hexDigest(highByte)
          == "a8100ae6aa1940d0b663bb31cd466142ebbdbd5187131b92d93818987832eb89");
}

TEST_CASE("SHA8 指针与长度版本和字符串版本结果一致")
{
    std::string msg = "flamingo";
    CHECK(Sha256::hexDigest(msg.data(), msg.size()) == Sha256::hexDigest(msg));

    //长度为 0 时允许传空指针，结果同空串
    CHECK(Sha256::hexDigest(NULL, 0)
          == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("SHA9 与 PasswordHasher 串起来即为 v2 的口令存储格式")
{
    //这正是管理端新建员工 / 重置口令要走的路径：服务端自己生成明文，
    //自己算完两层哈希后入库，登录时客户端上送 SHA256(明文) 即可校验通过。
    std::string plain = "a33a3b3903";
    std::string salt = PasswordHasher::generateSalt();
    std::string stored = PasswordHasher::hashPassword(Sha256::hexDigest(plain), salt);

    //存储哈希是 32 位 MD5 十六进制串，绝不能等于明文本身
    CHECK(stored.size() == 32);
    CHECK(stored != plain);

    //客户端上送 SHA256(明文) 时校验通过
    CHECK(PasswordHasher::verifyPassword(Sha256::hexDigest(plain), stored, salt));

    //上送明文本身（即历史缺陷下库里存的东西）必须校验不通过
    CHECK_FALSE(PasswordHasher::verifyPassword(plain, stored, salt));

    //换个口令校验不通过
    CHECK_FALSE(PasswordHasher::verifyPassword(Sha256::hexDigest("a33a3b3904"), stored, salt));
}
