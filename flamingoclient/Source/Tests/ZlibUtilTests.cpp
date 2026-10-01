/**
 *  压缩工具 ZlibUtil 与帧包体压缩路径的单元测试（ZL1~ZL5），ZlibUtilTests.cpp
 *
 *  覆盖：压缩后按原长解压往返一致；解压时给出的长度不足、压缩数据损坏都判失败且不改动输出；
 *  空输入与超过 10MB 上限的输入不压缩；帧级往返（encodeFramePayload 压缩、decodeFramePayload
 *  解压）与收帧时的真实调用路径一致。
 *
 *  背景：ZlibUtil 曾把 size_t 变量的地址强转成 uLongf* 交给 zlib（Windows 上 uLong 为 32 位），
 *  x64 下 zlib 只写入低 4 字节。该问题要在长度超过 4GB 时才会表现出来，用例无法构造，
 *  这组用例的作用是保证改用 uLong 中转变量之后压缩与解压的行为不变。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-01
 */
#include "catch_amalgamated.hpp"
#include <sstream>
#include <string>
#include "../Common/Util/ZlibUtil.h"
#include "../net/FramePayload.h"
#include "../net/Protocol.h"

namespace {

//ZL1 / ZL5 用的明文大约多少字节：远大于帧压缩阈值 COMPRESS_THRESHOLD，又足够小、跑得快
constexpr size_t kSampleTextBytes = 10 * 1024;
//ZlibUtil 允许压缩的上限，与 ZlibUtil.cpp 的 MAX_COMPRESS_BUF_SIZE 一致（10MB）
constexpr size_t kMaxCompressBytes = 10 * 1024 * 1024;
//ZL5 帧级往返用的 cmd 与 seq：取值本身不影响压缩，只需编码与解码两侧一致
constexpr uint16_t kTestCmd = 1501;
constexpr uint32_t kTestSeq = 42;

//生成一段近似真实消息列表的 JSON 文本：字段重复度高、可压缩，内容又随序号变化
std::string MakeSampleJson(size_t minBytes)
{
    std::ostringstream text;
    text << "{\"msgs\":[";
    for (int i = 0; text.tellp() < (std::streamoff)minBytes; ++i)
    {
        if (i > 0)
        {
            text << ",";
        }
        text << "{\"msgid\":" << (100000 + i)
             << ",\"fromid\":" << (5000 + i % 7)
             << ",\"msgtype\":1,\"content\":\"hello flamingo " << i << "\"}";
    }
    text << "]}";
    return text.str();
}

} // namespace

TEST_CASE("ZL1 压缩后按原长解压：内容与长度都与原文一致", "[zlib]")
{
    const std::string plain = MakeSampleJson(kSampleTextBytes);
    std::string compressed;
    REQUIRE(ZlibUtil::CompressBuf(plain, compressed));
    CHECK(compressed.size() < plain.size());

    std::string restored;
    REQUIRE(ZlibUtil::UncompressBuf(compressed, restored, plain.size()));
    CHECK(restored.size() == plain.size());
    CHECK(restored == plain);
}

TEST_CASE("ZL2 解压时给出的长度比原长小：判失败，输出不追加数据", "[zlib]")
{
    const std::string plain = MakeSampleJson(kSampleTextBytes);
    std::string compressed;
    REQUIRE(ZlibUtil::CompressBuf(plain, compressed));

    //输出里预先放一段内容，用来确认失败时没有追加任何字节
    const std::string prefix = "prefix";
    std::string out = prefix;
    CHECK_FALSE(ZlibUtil::UncompressBuf(compressed, out, plain.size() / 2));
    CHECK(out == prefix);
}

TEST_CASE("ZL3 压缩数据中间被改坏：判失败", "[zlib]")
{
    const std::string plain = MakeSampleJson(kSampleTextBytes);
    std::string compressed;
    REQUIRE(ZlibUtil::CompressBuf(plain, compressed));
    REQUIRE(compressed.size() > 2);

    //改坏中间一段字节。即便残余数据碰巧还能解码，末尾的 adler32 校验也对不上
    std::string broken = compressed;
    const size_t middle = broken.size() / 2;
    for (size_t i = middle; i < broken.size() && i < middle + 8; ++i)
    {
        broken[i] = (char)~broken[i];
    }
    std::string out;
    CHECK_FALSE(ZlibUtil::UncompressBuf(broken, out, plain.size()));
    CHECK(out.empty());
}

TEST_CASE("ZL4 空输入与超过 10MB 上限的输入不压缩", "[zlib]")
{
    std::string out;
    CHECK_FALSE(ZlibUtil::CompressBuf(std::string(), out));
    CHECK(out.empty());

    const std::string tooLarge(kMaxCompressBytes + 1, 'a');
    CHECK_FALSE(ZlibUtil::CompressBuf(tooLarge, out));
    CHECK(out.empty());

    //恰好等于上限的仍然压缩
    const std::string atLimit(kMaxCompressBytes, 'a');
    CHECK(ZlibUtil::CompressBuf(atLimit, out));
    CHECK_FALSE(out.empty());
}

TEST_CASE("ZL5 帧级往返：encodeFramePayload 压缩、decodeFramePayload 还原", "[zlib]")
{
    const std::string plain = MakeSampleJson(kSampleTextBytes);
    REQUIRE(plain.size() >= COMPRESS_THRESHOLD);

    //不加密（cipher 传 NULL），只走压缩这一步；这正是握手前与未配置公钥时的收发路径
    std::string wire;
    uint8_t flags = 0;
    uint32_t originLen = 0;
    REQUIRE(net::encodeFramePayload(kTestCmd, kTestSeq, plain, NULL, 0, net::payload_compress_auto,
                                    wire, flags, originLen));
    CHECK((flags & frame_flag_compressed) != 0);
    CHECK((flags & frame_flag_encrypted) == 0);
    CHECK(originLen == (uint32_t)plain.size());
    CHECK(wire.size() < plain.size());

    std::string restored;
    std::string err;
    REQUIRE(net::decodeFramePayload(kTestCmd, kTestSeq, flags, originLen, wire, NULL, restored, err));
    CHECK(err.empty());
    CHECK(restored == plain);
}
