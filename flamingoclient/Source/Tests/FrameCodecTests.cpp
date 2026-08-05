/**
 *  v2 帧编解码单元测试（Suite F），FrameCodecTests.cpp
 *  覆盖：编解码往返、空包体、魔数/版本校验、粘包、半包、长度越界、
 *       CRC 校验、压缩元信息透传、附件包体（描述符+二进制）编解码。
 *  框架 Catch2 v3；由服务端 flamingoserver/tests/test_framecodec.cpp 移植。
 *  balloonwj@qq.com   2026-05-22
 */
#include "catch_amalgamated.hpp"
#include "../net/FrameCodec.h"

using namespace net;

TEST_CASE("F1 正常帧编码后解码往返一致")
{
    std::string out;
    REQUIRE(encodeFrame(cmd_login, 42, std::string("{\"a\":1}"), out));
    CHECK(out.size() == FRAME_HEADER_SIZE + 7);

    frame_header h;
    std::string body;
    size_t consumed = 0;
    CHECK(decodeFrame(out.data(), out.size(), h, body, consumed) == frame_decode_ok);
    CHECK(h.magic == FRAME_MAGIC);
    CHECK(h.version == PROTOCOL_VERSION_V2);
    CHECK(h.cmd == cmd_login);
    CHECK(h.seq == 42u);
    CHECK(h.bodylen == 7u);
    CHECK(body == "{\"a\":1}");
    CHECK(consumed == out.size());
}

TEST_CASE("F2 空包体帧（心跳）编解码往返")
{
    std::string out;
    REQUIRE(encodeFrame(cmd_heartbeat, 1, out));
    CHECK(out.size() == FRAME_HEADER_SIZE);

    frame_header h;
    std::string body;
    size_t consumed = 0;
    CHECK(decodeFrame(out.data(), out.size(), h, body, consumed) == frame_decode_ok);
    CHECK(h.cmd == cmd_heartbeat);
    CHECK(h.bodylen == 0u);
    CHECK(body.empty());
    CHECK(consumed == FRAME_HEADER_SIZE);
}

TEST_CASE("F3 魔数错误判为协议错误")
{
    std::string out;
    encodeFrame(cmd_login, 1, std::string("x"), out);
    out[0] = 0x00;   //破坏魔数高字节

    frame_header h;
    std::string body;
    size_t consumed = 0;
    CHECK(decodeFrame(out.data(), out.size(), h, body, consumed) == frame_decode_error);
}

TEST_CASE("F4 协议版本不符判为协议错误")
{
    std::string out;
    encodeFrame(cmd_login, 1, std::string("x"), out);
    out[2] = 0x09;   //把 version 改成不支持的值

    frame_header h;
    std::string body;
    size_t consumed = 0;
    CHECK(decodeFrame(out.data(), out.size(), h, body, consumed) == frame_decode_error);
}

TEST_CASE("F5 粘包：一个缓冲含两个完整帧")
{
    std::string buf;
    encodeFrame(cmd_login, 1, std::string("first"), buf);
    encodeFrame(cmd_heartbeat, 2, buf);   //第二帧追加在后

    frame_header h;
    std::string body;
    size_t consumed = 0;

    CHECK(decodeFrame(buf.data(), buf.size(), h, body, consumed) == frame_decode_ok);
    CHECK(h.cmd == cmd_login);
    CHECK(body == "first");

    size_t off = consumed;
    CHECK(decodeFrame(buf.data() + off, buf.size() - off, h, body, consumed) == frame_decode_ok);
    CHECK(h.cmd == cmd_heartbeat);
    CHECK(off + consumed == buf.size());
}

TEST_CASE("F6 半包：数据不足一个完整帧返回 need_more")
{
    std::string out;
    encodeFrame(cmd_login, 1, std::string("hello world"), out);   //bodylen = 11

    frame_header h;
    std::string body;
    size_t consumed = 0;

    //只给前 10 字节，连帧头都不够
    CHECK(decodeFrame(out.data(), 10, h, body, consumed) == frame_decode_need_more);
    //帧头齐了但包体缺最后一字节
    CHECK(decodeFrame(out.data(), out.size() - 1, h, body, consumed) == frame_decode_need_more);
    //数据给全 → 成功
    CHECK(decodeFrame(out.data(), out.size(), h, body, consumed) == frame_decode_ok);
}

TEST_CASE("F7 包体长度超过上限判为协议错误")
{
    std::string out;
    encodeFrame(cmd_login, 1, std::string("0123456789"), out);   //bodylen = 10

    frame_header h;
    std::string body;
    size_t consumed = 0;
    //传入 maxbody=5，包体 10 字节 > 5 → 协议错误
    CHECK(decodeFrame(out.data(), out.size(), h, body, consumed, 5) == frame_decode_error);
}

TEST_CASE("F8 帧头被篡改时 CRC 校验失败")
{
    std::string out;
    encodeFrame(cmd_login, 7, std::string("payload"), out);
    //篡改 seq 字段（偏移 8~11）中的一个字节，魔数与版本仍合法
    out[9] = static_cast<char>(out[9] ^ 0xFF);

    frame_header h;
    std::string body;
    size_t consumed = 0;
    CHECK(decodeFrame(out.data(), out.size(), h, body, consumed) == frame_decode_error);
}

TEST_CASE("F9 压缩标志与 originlen 正确透传")
{
    //FrameCodec 自身不压缩，但需如实承载压缩元信息供会话层使用
    std::string fakeCompressed("\x01\x02\x03\x04", 4);
    std::string out;
    REQUIRE(encodeFrame(cmd_login, 5, frame_flag_compressed,
                        fakeCompressed.data(), 4, 999, out));

    frame_header h;
    std::string body;
    size_t consumed = 0;
    CHECK(decodeFrame(out.data(), out.size(), h, body, consumed) == frame_decode_ok);
    CHECK((h.flags & frame_flag_compressed) != 0);
    CHECK(h.bodylen == 4u);
    CHECK(h.originlen == 999u);
    CHECK(body == fakeCompressed);
}

TEST_CASE("F10 crc32 对空输入与已知输入的基本性质")
{
    //空输入的 CRC-32 标准值为 0
    CHECK(crc32(nullptr, 0) == 0u);
    //同一输入两次计算结果一致
    const char* s = "flamingo";
    CHECK(crc32(s, 8) == crc32(s, 8));
    //不同输入结果不同
    CHECK(crc32("abc", 3) != crc32("abd", 3));
}

TEST_CASE("F-att1 附件包体编码后解码往返一致")
{
    //描述符里含中文，附件为含 0x00 的二进制串
    std::string descriptor("{\"fileid\":\"123\",\"offset\":0}");
    std::string binary;
    binary.push_back('\x00');
    binary.push_back('\xFF');
    binary.append("chunk-bytes");
    binary.push_back('\x00');

    std::string body;
    REQUIRE(encodeAttachmentBody(descriptor, binary.data(),
                                 (uint32_t)binary.size(), body));
    //包体长度 = 4 字节长度域 + 描述符 + 附件
    CHECK(body.size() == 4 + descriptor.size() + binary.size());

    std::string descOut;
    size_t binoff = 0;
    size_t binlen = 0;
    REQUIRE(decodeAttachmentBody(body.data(), body.size(), descOut, binoff, binlen));
    CHECK(descOut == descriptor);
    CHECK(binlen == binary.size());
    CHECK(std::string(body.data() + binoff, binlen) == binary);
}

TEST_CASE("F-att2 空附件二进制段往返")
{
    //末片场景：仅描述符、无附件字节
    std::string descriptor("{\"done\":true}");
    std::string body;
    REQUIRE(encodeAttachmentBody(descriptor, nullptr, 0, body));
    CHECK(body.size() == 4 + descriptor.size());

    std::string descOut;
    size_t binoff = 0;
    size_t binlen = 0;
    REQUIRE(decodeAttachmentBody(body.data(), body.size(), descOut, binoff, binlen));
    CHECK(descOut == descriptor);
    CHECK(binlen == 0);
    CHECK(binoff == body.size());
}

TEST_CASE("F-att3 desclen 越出包体范围被拒")
{
    //构造一个 desclen 声称很大、实际包体很短的畸形附件包体
    std::string bad;
    bad.push_back('\x00');
    bad.push_back('\x00');
    bad.push_back('\x10');   //desclen 高位，使其远大于实际长度
    bad.push_back('\x00');
    bad.append("short");

    std::string descOut;
    size_t binoff = 0;
    size_t binlen = 0;
    CHECK_FALSE(decodeAttachmentBody(bad.data(), bad.size(), descOut, binoff, binlen));
}

TEST_CASE("F-att4 包体不足 4 字节被拒")
{
    std::string descOut;
    size_t binoff = 0;
    size_t binlen = 0;
    //连 desclen 字段都读不全
    std::string tooShort("\x00\x00\x00", 3);
    CHECK_FALSE(decodeAttachmentBody(tooShort.data(), tooShort.size(), descOut, binoff, binlen));
    //空包体
    CHECK_FALSE(decodeAttachmentBody(nullptr, 0, descOut, binoff, binlen));
}
