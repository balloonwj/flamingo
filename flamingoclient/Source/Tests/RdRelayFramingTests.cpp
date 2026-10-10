/**
 *  远程桌面中转帧封装 / 拆解 / 分片重组纯逻辑单元测试（Suite RDRELAY），
 *  RdRelayFramingTests.cpp
 *  覆盖：
 *    - 封帧：BuildHelloFrame / BuildChunkFrame 产出的帧能被 decodeFrame 还原出
 *      正确 cmd 与包体（hello 包体为 {"token":...}；data 包体为 [分片标志]+分片数据）；
 *    - 拆帧：DrainFrames 对「单帧 / 两帧粘连 / 半帧 / 一帧半 / 混合」的消费字节数
 *      与输出帧序列正确；坏魔数、非中转 cmd 一律置 outError；
 *    - 分片：BuildEngineFrames 小帧产单个 whole、大帧产 more...last，分片大小正确；
 *    - 重组：RdRelayReassembler 对 whole / more+last 正确还原；
 *    - 端到端：大引擎帧经 BuildEngineFrames → DrainFrames → Reassembler 逐帧重组
 *      还原为原始字节（round-trip）；
 *    - 与 parseRelayHelloAck 衔接：拆出的 hello 帧包体可解析出握手应答 code。
 *  框架 Catch2 v3。本组为纯逻辑，不触及 socket。
 *  balloonwj@qq.com   2026-06-09
 */
#include "catch_amalgamated.hpp"
#include "../../FlamingoMedia/RemoteDesktop/RdRelayFraming.h"
#include "../net/FrameCodec.h"
#include "../net/Protocol.h"
#include "../../FlamingoMedia/RemoteDesktop/RemoteDesktopProtocolClient.h"
#include "../third-party/jsoncpp-1.9.0/json.h"

#include <string>
#include <vector>

using namespace remotedesktop;

//把 JSON 文本解析成 Json::Value，供断言检查 hello 包体
static bool parseJsonText(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

//把 Json::Value 序列化为紧凑 JSON 文本，供构造握手应答报文
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//用 v2 FrameCodec 直接造一个完整帧（供构造测试输入：握手应答、非中转 cmd 帧等）
static std::string makeFrame(uint16_t cmd, const std::string& body)
{
    std::string out;
    net::encodeFrame(cmd, 0, body, out);
    return out;
}

TEST_CASE("RDRELAY1 BuildHelloFrame 封出可解的握手帧、包体为 {token}")
{
    std::string frame;
    REQUIRE(relayframing::BuildHelloFrame("tok-abc-123", frame));

    frame_header header;
    std::string body;
    size_t consumed = 0;
    REQUIRE(net::decodeFrame(frame.data(), frame.size(), header, body, consumed)
            == net::frame_decode_ok);
    CHECK(header.cmd == cmd_rd_relay_hello);
    CHECK(consumed == frame.size());

    Json::Value root;
    REQUIRE(parseJsonText(body, root));
    CHECK(root["token"].isString());
    CHECK(root["token"].asString() == "tok-abc-123");
}

TEST_CASE("RDRELAY2 BuildChunkFrame 封出可解的数据帧、包体为 [标志]+数据")
{
    //构造含 0 字节、非 ASCII 的分片数据，验证二进制透传不被截断 / 改写
    unsigned char chunk[] = { 0x00, 0xAB, 0xFF, 0x01, 0x00, 0x7F };
    std::string frame;
    REQUIRE(relayframing::BuildChunkFrame(rd_relay_chunk_whole, chunk, sizeof(chunk), frame));

    frame_header header;
    std::string body;
    size_t consumed = 0;
    REQUIRE(net::decodeFrame(frame.data(), frame.size(), header, body, consumed)
            == net::frame_decode_ok);
    CHECK(header.cmd == cmd_rd_relay_data);
    REQUIRE(body.size() == sizeof(chunk) + 1);
    CHECK(static_cast<unsigned char>(body[0]) == rd_relay_chunk_whole);
    CHECK(memcmp(body.data() + 1, chunk, sizeof(chunk)) == 0);
}

TEST_CASE("RDRELAY3 DrainFrames 单帧：拆出一帧、消费全部字节")
{
    unsigned char chunk[] = { 0x11, 0x22, 0x33 };
    std::string frame;
    REQUIRE(relayframing::BuildChunkFrame(rd_relay_chunk_whole, chunk, sizeof(chunk), frame));

    std::vector<RdRelayInboundFrame> frames;
    bool err = true;
    size_t used = relayframing::DrainFrames(
        reinterpret_cast<const unsigned char*>(frame.data()), frame.size(), frames, err);

    CHECK_FALSE(err);
    CHECK(used == frame.size());
    REQUIRE(frames.size() == 1);
    CHECK(frames[0].m_cmd == cmd_rd_relay_data);
    //包体 = [whole 标志] + 数据
    REQUIRE(frames[0].m_body.size() == sizeof(chunk) + 1);
    CHECK(frames[0].m_body[0] == rd_relay_chunk_whole);
    CHECK(memcmp(frames[0].m_body.data() + 1, chunk, sizeof(chunk)) == 0);
}

TEST_CASE("RDRELAY4 DrainFrames 两帧粘连：按序拆出两帧、cmd 各自正确")
{
    unsigned char chunk[] = { 0xDE, 0xAD };
    std::string f1;
    REQUIRE(relayframing::BuildChunkFrame(rd_relay_chunk_whole, chunk, sizeof(chunk), f1));
    std::string f2;
    REQUIRE(relayframing::BuildHelloFrame("tok-2", f2));

    std::string buf = f1 + f2;
    std::vector<RdRelayInboundFrame> frames;
    bool err = true;
    size_t used = relayframing::DrainFrames(
        reinterpret_cast<const unsigned char*>(buf.data()), buf.size(), frames, err);

    CHECK_FALSE(err);
    CHECK(used == buf.size());
    REQUIRE(frames.size() == 2);
    CHECK(frames[0].m_cmd == cmd_rd_relay_data);
    CHECK(frames[1].m_cmd == cmd_rd_relay_hello);
}

TEST_CASE("RDRELAY5 DrainFrames 半帧：不消费、不输出、不报错")
{
    unsigned char chunk[] = { 0x55, 0x66, 0x77, 0x88 };
    std::string frame;
    REQUIRE(relayframing::BuildChunkFrame(rd_relay_chunk_whole, chunk, sizeof(chunk), frame));

    //只喂前一半字节（不足一个完整帧）
    std::string half = frame.substr(0, frame.size() / 2);
    std::vector<RdRelayInboundFrame> frames;
    bool err = true;
    size_t used = relayframing::DrainFrames(
        reinterpret_cast<const unsigned char*>(half.data()), half.size(), frames, err);

    CHECK_FALSE(err);
    CHECK(used == 0);
    CHECK(frames.empty());
}

TEST_CASE("RDRELAY6 DrainFrames 一帧半：拆出整帧、剩余半帧不消费")
{
    unsigned char c1[] = { 0xA1, 0xA2 };
    unsigned char c2[] = { 0xB1, 0xB2, 0xB3, 0xB4 };
    std::string f1;
    REQUIRE(relayframing::BuildChunkFrame(rd_relay_chunk_whole, c1, sizeof(c1), f1));
    std::string f2;
    REQUIRE(relayframing::BuildChunkFrame(rd_relay_chunk_whole, c2, sizeof(c2), f2));

    //完整 f1 + f2 的前一半
    std::string buf = f1 + f2.substr(0, f2.size() / 2);
    std::vector<RdRelayInboundFrame> frames;
    bool err = true;
    size_t used = relayframing::DrainFrames(
        reinterpret_cast<const unsigned char*>(buf.data()), buf.size(), frames, err);

    CHECK_FALSE(err);
    CHECK(used == f1.size());
    REQUIRE(frames.size() == 1);
    CHECK(frames[0].m_cmd == cmd_rd_relay_data);
}

TEST_CASE("RDRELAY7 DrainFrames 坏魔数：置 outError")
{
    //30 字节垃圾（魔数非法），长度足够触发魔数判定
    std::string garbage(30, '\xEE');
    std::vector<RdRelayInboundFrame> frames;
    bool err = false;
    relayframing::DrainFrames(
        reinterpret_cast<const unsigned char*>(garbage.data()), garbage.size(), frames, err);

    CHECK(err);
    CHECK(frames.empty());
}

TEST_CASE("RDRELAY8 DrainFrames 非中转 cmd：置 outError")
{
    //用一个非 relay 的 cmd 造一帧，应被判为异常
    std::string frame = makeFrame(cmd_rd_invite, "{\"x\":1}");
    std::vector<RdRelayInboundFrame> frames;
    bool err = false;
    relayframing::DrainFrames(
        reinterpret_cast<const unsigned char*>(frame.data()), frame.size(), frames, err);

    CHECK(err);
    CHECK(frames.empty());
}

TEST_CASE("RDRELAY9 拆出的握手应答帧可经 parseRelayHelloAck 取 code")
{
    //模拟 imgserver 回的成功握手应答 {code:0,msg:ok}
    Json::Value ok;
    ok["code"] = 0;
    ok["msg"] = "ok";
    std::string okFrame = makeFrame(cmd_rd_relay_hello, writeCompact(ok));

    std::vector<RdRelayInboundFrame> frames;
    bool err = true;
    relayframing::DrainFrames(
        reinterpret_cast<const unsigned char*>(okFrame.data()), okFrame.size(), frames, err);
    CHECK_FALSE(err);
    REQUIRE(frames.size() == 1);
    REQUIRE(frames[0].m_cmd == cmd_rd_relay_hello);

    std::string body(reinterpret_cast<const char*>(frames[0].m_body.data()),
                     frames[0].m_body.size());
    net::RDRelayHelloAck ack;
    REQUIRE(net::RemoteDesktopProtocolClient::parseRelayHelloAck(body, ack));
    CHECK(ack.m_code == 0);

    //再模拟失败握手应答 {code:非0,msg:...}
    Json::Value bad;
    bad["code"] = error_code_session_expired;
    bad["msg"] = "invalid or expired token";
    std::string badFrame = makeFrame(cmd_rd_relay_hello, writeCompact(bad));

    std::vector<RdRelayInboundFrame> frames2;
    bool err2 = true;
    relayframing::DrainFrames(
        reinterpret_cast<const unsigned char*>(badFrame.data()), badFrame.size(), frames2, err2);
    CHECK_FALSE(err2);
    REQUIRE(frames2.size() == 1);
    std::string body2(reinterpret_cast<const char*>(frames2[0].m_body.data()),
                      frames2[0].m_body.size());
    net::RDRelayHelloAck ack2;
    REQUIRE(net::RemoteDesktopProtocolClient::parseRelayHelloAck(body2, ack2));
    CHECK(ack2.m_code == error_code_session_expired);
}

TEST_CASE("RDRELAY10 BuildEngineFrames 小帧(<=chunkSize)：产出单个 whole 帧")
{
    unsigned char engine[] = { 0x02, 0x10, 0x20, 0x30 };
    std::vector<std::string> frames;
    //chunkSize 取 16，数据 4 字节 <= 16，应只产一个 whole 帧
    REQUIRE(relayframing::BuildEngineFrames(engine, sizeof(engine), 16, frames));
    REQUIRE(frames.size() == 1);

    frame_header header;
    std::string body;
    size_t consumed = 0;
    REQUIRE(net::decodeFrame(frames[0].data(), frames[0].size(), header, body, consumed)
            == net::frame_decode_ok);
    CHECK(header.cmd == cmd_rd_relay_data);
    REQUIRE(body.size() == sizeof(engine) + 1);
    CHECK(static_cast<unsigned char>(body[0]) == rd_relay_chunk_whole);
    CHECK(memcmp(body.data() + 1, engine, sizeof(engine)) == 0);
}

TEST_CASE("RDRELAY11 BuildEngineFrames 大帧(>chunkSize)：产出 more...last、分片大小正确")
{
    //13 字节，chunkSize=4 → 4 帧：more(4)+more(4)+more(4)+last(1)
    unsigned char engine[13];
    for (int i = 0; i < 13; ++i)
        engine[i] = static_cast<unsigned char>(i + 1);

    std::vector<std::string> frames;
    REQUIRE(relayframing::BuildEngineFrames(engine, sizeof(engine), 4, frames));
    REQUIRE(frames.size() == 4);

    const unsigned char expectFlags[4] = {
        rd_relay_chunk_more, rd_relay_chunk_more, rd_relay_chunk_more, rd_relay_chunk_last
    };
    const size_t expectChunkLen[4] = { 4, 4, 4, 1 };
    for (size_t i = 0; i < frames.size(); ++i)
    {
        frame_header header;
        std::string body;
        size_t consumed = 0;
        REQUIRE(net::decodeFrame(frames[i].data(), frames[i].size(), header, body, consumed)
                == net::frame_decode_ok);
        CHECK(header.cmd == cmd_rd_relay_data);
        REQUIRE(body.size() == expectChunkLen[i] + 1);
        CHECK(static_cast<unsigned char>(body[0]) == expectFlags[i]);
    }
}

TEST_CASE("RDRELAY12 Reassembler 完整帧：直接还原")
{
    unsigned char engine[] = { 0x01, 0x02, 0x03 };
    //包体 = [whole] + 数据
    std::vector<unsigned char> body;
    body.push_back(rd_relay_chunk_whole);
    body.insert(body.end(), engine, engine + sizeof(engine));

    RdRelayReassembler reasm;
    std::vector<unsigned char> out;
    REQUIRE(reasm.Feed(body.data(), body.size(), out));
    REQUIRE(out.size() == sizeof(engine));
    CHECK(memcmp(out.data(), engine, sizeof(engine)) == 0);
}

TEST_CASE("RDRELAY13 Reassembler 分片帧：more+more+last 还原")
{
    RdRelayReassembler reasm;
    std::vector<unsigned char> out;

    //more: [more]+{A1,A2}
    unsigned char b1[] = { rd_relay_chunk_more, 0xA1, 0xA2 };
    CHECK_FALSE(reasm.Feed(b1, sizeof(b1), out));
    //more: [more]+{A3}
    unsigned char b2[] = { rd_relay_chunk_more, 0xA3 };
    CHECK_FALSE(reasm.Feed(b2, sizeof(b2), out));
    //last: [last]+{A4,A5}
    unsigned char b3[] = { rd_relay_chunk_last, 0xA4, 0xA5 };
    REQUIRE(reasm.Feed(b3, sizeof(b3), out));

    unsigned char expect[] = { 0xA1, 0xA2, 0xA3, 0xA4, 0xA5 };
    REQUIRE(out.size() == sizeof(expect));
    CHECK(memcmp(out.data(), expect, sizeof(expect)) == 0);
}

TEST_CASE("RDRELAY14 端到端：大引擎帧经分片→封帧→拆帧→重组还原(round-trip)")
{
    //构造一段 1000 字节、内容有规律的引擎帧
    std::vector<unsigned char> engine(1000);
    for (size_t i = 0; i < engine.size(); ++i)
        engine[i] = static_cast<unsigned char>((i * 31 + 7) & 0xFF);

    //chunkSize=256 → 4 帧(256*3 + 232)
    std::vector<std::string> frames;
    REQUIRE(relayframing::BuildEngineFrames(engine.data(), engine.size(), 256, frames));
    REQUIRE(frames.size() == 4);

    //把所有帧粘成一个字节流（模拟 TCP 收到的连续字节）
    std::string wire;
    for (size_t i = 0; i < frames.size(); ++i)
        wire += frames[i];

    //拆帧
    std::vector<RdRelayInboundFrame> inbound;
    bool err = true;
    size_t used = relayframing::DrainFrames(
        reinterpret_cast<const unsigned char*>(wire.data()), wire.size(), inbound, err);
    CHECK_FALSE(err);
    CHECK(used == wire.size());
    REQUIRE(inbound.size() == 4);

    //逐帧喂重组器，最后一帧应还原出完整引擎帧
    RdRelayReassembler reasm;
    std::vector<unsigned char> restored;
    bool got = false;
    for (size_t i = 0; i < inbound.size(); ++i)
    {
        std::vector<unsigned char> out;
        if (reasm.Feed(inbound[i].m_body.data(), inbound[i].m_body.size(), out))
        {
            got = true;
            restored.swap(out);
        }
    }
    REQUIRE(got);
    REQUIRE(restored.size() == engine.size());
    CHECK(memcmp(restored.data(), engine.data(), engine.size()) == 0);
}
