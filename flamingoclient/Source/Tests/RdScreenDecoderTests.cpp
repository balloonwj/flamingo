/**
 *  远程桌面解码器单元测试（Suite RDDEC），RdScreenDecoderTests.cpp
 *  覆盖：BITMAPINFO 帧建立整屏缓冲（含 biSizeImage 为 0 时按行对齐重算）、
 *       首屏帧填充、增量帧（DIFF）按 [偏移+长度+数据] 贴回缓冲、光标位置 /
 *       类型解析、就绪状态、畸形输入安全失败；被控端重发相同的 BITMAPINFO 时保持原画面（RDDEC5），
 *       尺寸变化时按新尺寸重建（RDDEC6）。
 *  解码为纯内存操作，不触发 GDI 绘制。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-06-08
 */
#include "catch_amalgamated.hpp"
#include "../../FlamingoMedia/RemoteDesktop/RdScreenDecoder.h"
#include "../../FlamingoMedia/RemoteDesktop/RdEngineDef.h"

using namespace remotedesktop;

//小端追加一个 32 位整数
static void appendU32(std::vector<BYTE>& v, uint32_t x)
{
    v.push_back((BYTE)(x & 0xFF));
    v.push_back((BYTE)((x >> 8) & 0xFF));
    v.push_back((BYTE)((x >> 16) & 0xFF));
    v.push_back((BYTE)((x >> 24) & 0xFF));
}

//构造一个 BITMAPINFO 引擎帧（w*h*bits，biSizeImage 由 setSize 决定是否填）
static std::vector<BYTE> makeBmiPayload(int w, int h, int bits, bool setSizeImage)
{
    BITMAPINFOHEADER hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.biSize        = sizeof(BITMAPINFOHEADER);
    hdr.biWidth       = w;
    hdr.biHeight      = h;
    hdr.biPlanes      = 1;
    hdr.biBitCount    = (WORD)bits;
    hdr.biCompression = BI_RGB;
    long stride = (((long)w * bits + 31) & ~31) >> 3;
    hdr.biSizeImage = setSizeImage ? (DWORD)(stride * h) : 0;

    std::vector<BYTE> out;
    out.push_back((BYTE)rd_frame_bitmapinfo);
    const BYTE* p = (const BYTE*)&hdr;
    out.insert(out.end(), p, p + sizeof(hdr));
    return out;
}

//4 位宽 4 高 16 位色：行字节 = ((4*16+31)&~31)>>3 = 8，整屏 = 8*4 = 32
static const int kStride16x4 = 8;
static const int kSize16x4   = 32;

TEST_CASE("RDDEC1 BITMAPINFO + 首屏建立整屏缓冲")
{
    RdScreenDecoder d;
    std::vector<BYTE> bmi = makeBmiPayload(4, 4, 16, true);

    //只收到 BITMAPINFO 还不算就绪（缺首屏）
    CHECK_FALSE(d.apply(bmi.data(), bmi.size()));
    CHECK_FALSE(d.ready());
    CHECK(d.width() == 4);
    CHECK(d.height() == 4);

    //首屏：32 字节全 0
    std::vector<BYTE> first;
    first.push_back((BYTE)rd_frame_first);
    first.insert(first.end(), kSize16x4, 0);
    CHECK(d.apply(first.data(), first.size()));
    CHECK(d.ready());
    REQUIRE(d.screen().size() == (size_t)kSize16x4);
    CHECK(d.screen()[0] == 0);
}

TEST_CASE("RDDEC2 增量帧(DIFF) 把变化块贴回缓冲并解析光标")
{
    RdScreenDecoder d;
    std::vector<BYTE> bmi = makeBmiPayload(4, 4, 16, true);
    d.apply(bmi.data(), bmi.size());
    std::vector<BYTE> first;
    first.push_back((BYTE)rd_frame_first);
    first.insert(first.end(), kSize16x4, 0);
    d.apply(first.data(), first.size());

    //增量帧：算法 DIFF + 光标(3,7)类型5 + 差异块[偏移4, 长度4, DEADBEEF]
    std::vector<BYTE> next;
    next.push_back((BYTE)rd_frame_next);
    next.push_back(rd_algo_diff);
    POINT pt;
    pt.x = 3;
    pt.y = 7;
    const BYTE* pp = (const BYTE*)&pt;
    next.insert(next.end(), pp, pp + sizeof(POINT));
    next.push_back(5);              //光标类型
    appendU32(next, 4);            //偏移
    appendU32(next, 4);            //长度
    next.push_back(0xDE);
    next.push_back(0xAD);
    next.push_back(0xBE);
    next.push_back(0xEF);

    CHECK(d.apply(next.data(), next.size()));

    //偏移 0..3 仍为 0，偏移 4..7 被改写为 DEADBEEF
    CHECK(d.screen()[0] == 0);
    CHECK(d.screen()[4] == 0xDE);
    CHECK(d.screen()[5] == 0xAD);
    CHECK(d.screen()[6] == 0xBE);
    CHECK(d.screen()[7] == 0xEF);
    //光标位置 / 类型解析正确
    CHECK(d.cursorPos().x == 3);
    CHECK(d.cursorPos().y == 7);
    CHECK(d.cursorType() == 5);
}

TEST_CASE("RDDEC3 biSizeImage 为 0 时按行对齐重算缓冲大小")
{
    RdScreenDecoder d;
    std::vector<BYTE> bmi = makeBmiPayload(4, 4, 16, false);   //biSizeImage 置 0
    d.apply(bmi.data(), bmi.size());

    std::vector<BYTE> first;
    first.push_back((BYTE)rd_frame_first);
    first.insert(first.end(), kSize16x4, 0);
    d.apply(first.data(), first.size());

    REQUIRE(d.screen().size() == (size_t)(kStride16x4 * 4));
}

TEST_CASE("RDDEC4 畸形输入一律安全")
{
    RdScreenDecoder d;
    //空 / 空指针
    CHECK_FALSE(d.apply(NULL, 0));
    //未知帧类型
    BYTE bad = 99;
    CHECK_FALSE(d.apply(&bad, 1));
    //未建 BITMAPINFO 直接喂首屏：不就绪、不崩
    std::vector<BYTE> first;
    first.push_back((BYTE)rd_frame_first);
    first.insert(first.end(), 16, 0);
    d.apply(first.data(), first.size());
    CHECK_FALSE(d.ready());
    //未建首屏直接喂增量：忽略、不崩
    std::vector<BYTE> next;
    next.push_back((BYTE)rd_frame_next);
    next.push_back(rd_algo_diff);
    next.insert(next.end(), sizeof(POINT) + 1, 0);
    CHECK(d.apply(next.data(), next.size()));   //返回 true（是 next 帧），但内部忽略
    CHECK_FALSE(d.ready());
    //BITMAPINFO 帧过短（不足头）：忽略
    BYTE shortBmi[3] = { (BYTE)rd_frame_bitmapinfo, 1, 2 };
    CHECK_FALSE(d.apply(shortBmi, 3));
    CHECK_FALSE(d.ready());
}

//构造一个首屏引擎帧：size 字节全部填 fill
static std::vector<BYTE> makeFirstPayload(size_t size, BYTE fill)
{
    std::vector<BYTE> out;
    out.push_back((BYTE)rd_frame_first);
    out.insert(out.end(), size, fill);
    return out;
}

//首屏填充值：区分新旧两份首屏
static const BYTE kOldFill = 0x11;
static const BYTE kNewFill = 0x22;

TEST_CASE("RDDEC5 就绪后再收到相同的 BITMAPINFO：保持就绪与原画面，等新首屏整屏覆盖")
{
    //被控端每隔一段时间重发「BITMAPINFO + 首屏」。P2P 上 4 MB 的首屏从发出到拼齐要 1.2～1.5 秒（2026-10-04 实测），
    //若 BITMAPINFO 一到就清掉画面，在约 3.3 秒的重发周期里观看端约四成时间没有画面、画面闪烁（见 docs/P2P通信方案.md 第 14.26 节）
    RdScreenDecoder d;
    std::vector<BYTE> bmi = makeBmiPayload(4, 4, 16, true);
    d.apply(bmi.data(), bmi.size());
    std::vector<BYTE> oldFirst = makeFirstPayload(kSize16x4, kOldFill);
    d.apply(oldFirst.data(), oldFirst.size());
    REQUIRE(d.ready());

    //重发的 BITMAPINFO 与当前完全相同：仍就绪，画面不变
    CHECK_FALSE(d.apply(bmi.data(), bmi.size()));
    CHECK(d.ready());
    REQUIRE(d.screen().size() == (size_t)kSize16x4);
    CHECK(d.screen()[0] == kOldFill);
    CHECK(d.screen()[kSize16x4 - 1] == kOldFill);

    //随后到达的新首屏整屏覆盖
    std::vector<BYTE> newFirst = makeFirstPayload(kSize16x4, kNewFill);
    CHECK(d.apply(newFirst.data(), newFirst.size()));
    CHECK(d.ready());
    CHECK(d.screen()[0] == kNewFill);
    CHECK(d.screen()[kSize16x4 - 1] == kNewFill);
}

TEST_CASE("RDDEC6 收到尺寸不同的 BITMAPINFO：不再就绪，按新尺寸重建缓冲，新首屏到达后恢复")
{
    RdScreenDecoder d;
    std::vector<BYTE> bmi = makeBmiPayload(4, 4, 16, true);
    d.apply(bmi.data(), bmi.size());
    std::vector<BYTE> first = makeFirstPayload(kSize16x4, kOldFill);
    d.apply(first.data(), first.size());
    REQUIRE(d.ready());

    //被控端换了分辨率：8×4，16 位色，行字节 16，整屏 64
    const int newW = 8;
    const size_t newSize = 64;
    std::vector<BYTE> bigger = makeBmiPayload(newW, 4, 16, true);
    CHECK_FALSE(d.apply(bigger.data(), bigger.size()));
    CHECK_FALSE(d.ready());
    CHECK(d.width() == newW);
    CHECK(d.screen().size() == newSize);

    std::vector<BYTE> newFirst = makeFirstPayload(newSize, kNewFill);
    CHECK(d.apply(newFirst.data(), newFirst.size()));
    CHECK(d.ready());
    CHECK(d.screen()[newSize - 1] == kNewFill);
}
