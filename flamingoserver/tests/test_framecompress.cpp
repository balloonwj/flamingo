/**
 *  v2 帧「压缩往返」单元测试（Suite FC），test_framecompress.cpp
 *
 *  覆盖：客户端对超过阈值的大包体做 zlib 压缩、置 frame_flag_compressed
 *  标志位发送；服务端会话层收到后必须先按 originlen 解压、再解析 JSON。
 *  本组用例锁定「压缩 → 编码 → 解码 → 解压」整条链路能无损还原原始包体，
 *  即 ChatSession::onRead 解压步骤所依赖的契约。
 *
 *  背景：曾出现「群发长消息必定失败、服务端返回 error_code_bad_request」的
 *  缺陷，根因是客户端压缩了 ≥256 字节的大包，而服务端收包后未解压、直接把
 *  压缩字节当 JSON 解析。本测试复现该现场（解码后的 body 仍是压缩字节、不等
 *  于原文），并固化正确的往返行为（按 originlen 解压后与原文逐字节一致）。
 *
 *  另有 ZLB1（2026-10-09）：守卫链接进来的 zlib 版本，见 docs/服务端依赖库源码编译方案.md 第 6 节。
 *
 *  balloonwj@qq.com   2026-06-02
 */
#include "doctest.h"
#include "../net/FrameCodec.h"
#include "../zlib1.3.2/ZlibUtil.h"
#include "../zlib1.3.2/zlib.h"
#include <string>

using namespace net;

namespace {

/**
 *  构造一个长度必定超过压缩阈值（COMPRESS_THRESHOLD = 256 字节）的真实风格
 *  「发送消息」JSON 包体：text 字段把「拉我的群列表」重复 21 次，模拟用户实际
 *  触发失败的那条长群消息。每个「拉我的群列表」UTF-8 占 18 字节，重复 21 次约
 *  378 字节，连同外层 JSON 远超 256 字节阈值，足以触发客户端压缩。
 */
std::string makeLongSendMessageBody()
{
    std::string text;
    for (int i = 0; i < 21; ++i)
        text += u8"拉我的群列表";

    std::string body;
    body += u8"{\"clientmsgid\":\"c-1001\",\"ctype\":2,\"cid\":\"700001\",";
    body += u8"\"msgtype\":1,\"content\":{\"text\":\"";
    body += text;
    body += u8"\"}}";
    return body;
}

} // namespace

TEST_CASE("FC1 压缩大包体经编码-解码-解压可无损还原")
{
    std::string original = makeLongSendMessageBody();
    //前提：包体确实超过压缩阈值，才会被客户端压缩、才会走到出问题的路径
    REQUIRE(original.size() >= COMPRESS_THRESHOLD);

    //客户端口径：先对包体做 zlib 压缩
    std::string compressed;
    REQUIRE(ZlibUtil::compressBuf(original, compressed));
    //重复内容压缩后应明显更小，确认确实压上了
    REQUIRE(compressed.size() < original.size());

    //置 frame_flag_compressed 标志、originlen 填原始长度，编码成完整帧
    std::string frame;
    REQUIRE(encodeFrame(cmd_send_message, 7, frame_flag_compressed,
                        compressed.data(), static_cast<uint32_t>(compressed.size()),
                        static_cast<uint32_t>(original.size()), frame));

    //服务端口径第一步：按帧解码
    frame_header header;
    std::string body;
    size_t consumed = 0;
    REQUIRE(decodeFrame(frame.data(), frame.size(), header, body, consumed)
            == frame_decode_ok);
    CHECK((header.flags & frame_flag_compressed) != 0);
    CHECK(header.originlen == original.size());

    //decodeFrame 不解压：此时 body 仍是压缩字节，直接当 JSON 解析必然失败
    //（这正是历史缺陷的现场——服务端缺了下面这一步解压）
    CHECK(body == compressed);
    CHECK(body != original);

    //服务端口径第二步（本次修复补上的契约）：按 originlen 解压还原原始 JSON 包体
    std::string restored;
    REQUIRE(ZlibUtil::uncompressBuf(body, restored, header.originlen));
    CHECK(restored == original);
    CHECK(restored.size() == original.size());
}

TEST_CASE("FC2 未压缩短包体的 body 即原文，无需解压")
{
    //短包体（未达阈值）客户端不会压缩，flags 不含压缩位，body 即原始 JSON。
    //本例确认修复不会影响占绝大多数的非压缩路径。
    std::string original =
        u8"{\"clientmsgid\":\"c-1\",\"ctype\":1,\"cid\":\"1\",\"msgtype\":1,\"content\":{\"text\":\"hi\"}}";
    REQUIRE(original.size() < COMPRESS_THRESHOLD);

    std::string frame;
    REQUIRE(encodeFrame(cmd_send_message, 8, original, frame));

    frame_header header;
    std::string body;
    size_t consumed = 0;
    REQUIRE(decodeFrame(frame.data(), frame.size(), header, body, consumed)
            == frame_decode_ok);
    //未压缩：标志位不含压缩、originlen 等于 bodylen、body 即原文
    CHECK((header.flags & frame_flag_compressed) == 0);
    CHECK(header.originlen == header.bodylen);
    CHECK(body == original);
}

//随仓库提交的 zlib 版本号。升级 zlib（整体替换 zlib1.x.y/ 下的官方文件）时同步修改此处
static const char* const kBundledZlibVersion = "1.3.2";

//ZLB1：链接进来的 zlib 必须是随仓库提交的那一份。zlibVersion() 返回库实现里编进去的版本串，
//ZLIB_VERSION 是头文件里的版本串：两者一致，说明头文件与实现出自同一份源码；再等于
//kBundledZlibVersion，说明链接的不是系统里的 libz 或 MariaDB 连接器自带的另一份 zlib
TEST_CASE("ZLB1 链接的 zlib 为随仓库提交的版本，且与头文件版本一致")
{
    CHECK(std::string(zlibVersion()) == std::string(ZLIB_VERSION));
    CHECK(std::string(zlibVersion()) == std::string(kBundledZlibVersion));
}
