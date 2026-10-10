/**
 *  音视频通话中转「测试帧」编解码纯逻辑单元测试（Suite CTF），CallRelayTestFrameTests.cpp
 *  覆盖：
 *    - 编解码往返：BuildTestFrame(seq) 经 ParseTestFrame 还原出同一 seq（含边界 seq 取值）；
 *    - 帧长度：BuildTestFrame 产出长度恒为 kTestFrameTotalLen；
 *    - 魔数不符：篡改头部魔数后 ParseTestFrame 返回 false；
 *    - 载荷被篡改：翻转载荷任一字节后 ParseTestFrame 返回 false（发现透传字节损坏）；
 *    - 缓冲过短 / 截断 / 超长：长度不等于约定值时一律安全失败、不越界。
 *  框架 Catch2 v3。本组为纯逻辑，不触及 socket。
 *  balloonwj@qq.com   2026-06-11
 */
#include "catch_amalgamated.hpp"
#include "../../FlamingoMedia/Call/CallRelayTestFrame.h"

#include <string>
#include <vector>

using namespace call;

TEST_CASE("CTF1 测试帧编解码往返：多种 seq 均能原样还原")
{
    //覆盖 0、1、典型值与 32 位上界，确保大端序号编解码无误
    const uint32_t seqs[] = { 0u, 1u, 12345u, 0x7FFFFFFFu, 0xFFFFFFFFu };
    for (size_t i = 0; i < sizeof(seqs) / sizeof(seqs[0]); ++i)
    {
        std::string frame = BuildTestFrame(seqs[i]);
        uint32_t seqOut = 0;
        REQUIRE(ParseTestFrame(reinterpret_cast<const unsigned char*>(frame.data()),
                               frame.size(), seqOut));
        CHECK(seqOut == seqs[i]);
    }
}

TEST_CASE("CTF2 帧长度恒为 kTestFrameTotalLen")
{
    CHECK(BuildTestFrame(0u).size() == kTestFrameTotalLen);
    CHECK(BuildTestFrame(0xABCDEF01u).size() == kTestFrameTotalLen);
    //总长 = 魔数 + 序号 + 载荷，三者拼接，无填充
    CHECK(kTestFrameTotalLen == kTestFrameMagicLen + kTestFrameSeqLen + kTestFramePayloadLen);
}

TEST_CASE("CTF3 不同 seq 产出不同载荷（序号确实驱动载荷模式）")
{
    std::string a = BuildTestFrame(100u);
    std::string b = BuildTestFrame(101u);
    REQUIRE(a.size() == b.size());
    //头部魔数相同，但整帧（含载荷）应不同
    CHECK(a != b);
}

TEST_CASE("CTF4 魔数不符则校验失败")
{
    std::string frame = BuildTestFrame(7u);
    //翻转魔数首字节
    frame[0] = static_cast<char>(frame[0] ^ 0xFF);
    uint32_t seqOut = 0xDEADBEEF;
    CHECK_FALSE(ParseTestFrame(reinterpret_cast<const unsigned char*>(frame.data()),
                               frame.size(), seqOut));
    //失败时不应改写出参
    CHECK(seqOut == 0xDEADBEEF);
}

TEST_CASE("CTF5 载荷被篡改则校验失败")
{
    std::string frame = BuildTestFrame(42u);
    //翻转载荷区中段一个字节（位于魔数 + 序号之后）
    size_t mid = kTestFrameMagicLen + kTestFrameSeqLen + kTestFramePayloadLen / 2;
    frame[mid] = static_cast<char>(frame[mid] ^ 0x01);
    uint32_t seqOut = 0;
    CHECK_FALSE(ParseTestFrame(reinterpret_cast<const unsigned char*>(frame.data()),
                               frame.size(), seqOut));
}

TEST_CASE("CTF6 缓冲过短 / 截断 / 超长一律安全失败")
{
    std::string frame = BuildTestFrame(9u);
    uint32_t seqOut = 0;

    //截断到比总长少 1 字节
    CHECK_FALSE(ParseTestFrame(reinterpret_cast<const unsigned char*>(frame.data()),
                               kTestFrameTotalLen - 1, seqOut));
    //空缓冲
    CHECK_FALSE(ParseTestFrame(reinterpret_cast<const unsigned char*>(frame.data()), 0, seqOut));
    //NULL 指针
    CHECK_FALSE(ParseTestFrame(NULL, kTestFrameTotalLen, seqOut));
    //超长 1 字节（定长帧不接受多余尾字节）
    std::string longer = frame + std::string(1, '\0');
    CHECK_FALSE(ParseTestFrame(reinterpret_cast<const unsigned char*>(longer.data()),
                               longer.size(), seqOut));
}
