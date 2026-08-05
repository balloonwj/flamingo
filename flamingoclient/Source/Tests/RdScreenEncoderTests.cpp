/**
 *  远程桌面帧编码器单元测试（Suite RDENC），RdScreenEncoderTests.cpp
 *  覆盖：BGRA32 → 16 位 RGB555 DIB 首屏往返、DIFF 增量往返、无变化、stride
 *       padding 逐行、帧类型与 BITMAPINFO 头、光标字段往返。
 *  编码与解码均为纯内存操作，不触发 GDI、不依赖真实屏幕。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-06-11
 */
#include "catch_amalgamated.hpp"
#include "../RemoteDesktop/ScreenFrameEncoder.h"
#include "../RemoteDesktop/RdScreenDecoder.h"
#include "../RemoteDesktop/RdEngineDef.h"

using namespace remotedesktop;

//BGRA → RGB555 期望值（与编码器内部转换一致）。
static WORD expectRgb555(BYTE b, BYTE g, BYTE r)
{
    return (WORD)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
}

//一张构造出来的 BGRA32 测试图（含可选行尾 padding，模拟 stride > width*4）。
struct TestImage
{
    std::vector<BYTE> data;
    int w;
    int h;
    int stride;
};

//构造测试图：像素 (x,y) 的 B/G/R 由 fill 回调给出，A 固定 255；stridePad 为行尾填充字节数。
static TestImage makeBgra(int w, int h, int stridePad,
                          void (*fill)(int x, int y, BYTE& b, BYTE& g, BYTE& r))
{
    TestImage img;
    img.w = w;
    img.h = h;
    img.stride = w * 4 + stridePad;
    img.data.assign((size_t)img.stride * h, 0);
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            BYTE b = 0;
            BYTE g = 0;
            BYTE r = 0;
            fill(x, y, b, g, r);
            BYTE* p = &img.data[(size_t)y * img.stride + x * 4];
            p[0] = b;
            p[1] = g;
            p[2] = r;
            p[3] = 255;
        }
    }
    return img;
}

//把测试图包成 FrameView 喂给编码器。
static FrameView viewOf(const TestImage& img)
{
    FrameView v;
    v.m_data = img.data.data();
    v.m_width = img.w;
    v.m_height = img.h;
    v.m_stride = img.stride;
    return v;
}

//从解码端整屏 DIB（16 位、bottom-up、4 字节行对齐）读屏幕坐标 (x,y) 的 RGB555 值。
static WORD screenPixel(const RdScreenDecoder& d, int x, int y)
{
    int w = d.width();
    int h = d.height();
    int stride = (((w * 16) + 31) & ~31) >> 3;
    const std::vector<BYTE>& s = d.screen();
    size_t off = (size_t)(h - 1 - y) * stride + (size_t)x * 2;   //bottom-up
    return (WORD)(s[off] | (s[off + 1] << 8));
}

//渐变填充：颜色随坐标变化，且每通道低 3 位置 0，使 RGB555 转换无损、便于精确比对。
static void fillGradient(int x, int y, BYTE& b, BYTE& g, BYTE& r)
{
    b = (BYTE)((x * 8) & 0xF8);
    g = (BYTE)((y * 8) & 0xF8);
    r = (BYTE)(((x + y) * 8) & 0xF8);
}

TEST_CASE("RDENC1 首屏往返：BGRA → 16 位 → 解码逐像素一致")
{
    const int w = 4;
    const int h = 4;
    TestImage img = makeBgra(w, h, 0, fillGradient);

    CScreenFrameEncoder enc;
    enc.Feed(viewOf(img));

    std::vector<BYTE> bmi;
    std::vector<BYTE> first;
    enc.GetBitmapInfo(bmi);
    enc.GetFirstFrame(first);

    RdScreenDecoder d;
    CHECK_FALSE(d.apply(bmi.data(), bmi.size()));    //仅收 BITMAPINFO 未就绪
    CHECK(d.apply(first.data(), first.size()));
    REQUIRE(d.ready());
    CHECK(d.width() == w);
    CHECK(d.height() == h);

    //逐像素：解码端还原的 RGB555 == 输入 BGRA 的 RGB555
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            BYTE b = (BYTE)((x * 8) & 0xF8);
            BYTE g = (BYTE)((y * 8) & 0xF8);
            BYTE r = (BYTE)(((x + y) * 8) & 0xF8);
            CHECK(screenPixel(d, x, y) == expectRgb555(b, g, r));
        }
    }
}

TEST_CASE("RDENC2 增量往返：局部变化只发差异且还原正确")
{
    const int w = 8;
    const int h = 8;
    TestImage img = makeBgra(w, h, 0, fillGradient);

    CScreenFrameEncoder enc;
    enc.Feed(viewOf(img));
    std::vector<BYTE> bmi;
    std::vector<BYTE> first;
    enc.GetBitmapInfo(bmi);
    enc.GetFirstFrame(first);

    RdScreenDecoder d;
    d.apply(bmi.data(), bmi.size());
    d.apply(first.data(), first.size());

    //改第二帧：把 (2,3) 像素改成纯白（BGR 低 3 位为 0，RGB555 无损）
    TestImage img2 = img;
    BYTE* p = &img2.data[(size_t)3 * img2.stride + 2 * 4];
    p[0] = 248;
    p[1] = 248;
    p[2] = 248;

    enc.Feed(viewOf(img2));
    std::vector<BYTE> next;
    POINT cur;
    cur.x = 0;
    cur.y = 0;
    enc.GetNextFrame(next, cur, 0);

    //增量帧应远小于首屏帧（只发了变化区）
    CHECK(next.size() < first.size());

    CHECK(d.apply(next.data(), next.size()));
    //变化点还原为白，其它点保持原色
    CHECK(screenPixel(d, 2, 3) == expectRgb555(248, 248, 248));
    CHECK(screenPixel(d, 0, 0) == expectRgb555(0, 0, 0));
}

TEST_CASE("RDENC3 无变化：增量帧不含差异块，画面不变")
{
    const int w = 4;
    const int h = 4;
    TestImage img = makeBgra(w, h, 0, fillGradient);

    CScreenFrameEncoder enc;
    enc.Feed(viewOf(img));
    std::vector<BYTE> bmi;
    std::vector<BYTE> first;
    enc.GetBitmapInfo(bmi);
    enc.GetFirstFrame(first);

    RdScreenDecoder d;
    d.apply(bmi.data(), bmi.size());
    d.apply(first.data(), first.size());

    //再喂一张完全相同的图
    enc.Feed(viewOf(img));
    std::vector<BYTE> next;
    POINT cur;
    cur.x = 1;
    cur.y = 1;
    enc.GetNextFrame(next, cur, 0);

    //帧体 = 类型(1) + 算法(1) + POINT(8) + 光标类型(1) = 11 字节，无差异块
    CHECK(next.size() == (size_t)(1 + 1 + sizeof(POINT) + 1));

    WORD before = screenPixel(d, 2, 2);
    CHECK(d.apply(next.data(), next.size()));
    CHECK(screenPixel(d, 2, 2) == before);
}

TEST_CASE("RDENC4 stride padding：行尾填充不影响还原")
{
    const int w = 3;   //w=3 → DIB 行字节 8，含 2 字节对齐 padding
    const int h = 3;
    TestImage img = makeBgra(w, h, 4, fillGradient);   //输入再额外加 4 字节行尾 padding

    CScreenFrameEncoder enc;
    enc.Feed(viewOf(img));
    std::vector<BYTE> bmi;
    std::vector<BYTE> first;
    enc.GetBitmapInfo(bmi);
    enc.GetFirstFrame(first);

    RdScreenDecoder d;
    d.apply(bmi.data(), bmi.size());
    d.apply(first.data(), first.size());
    REQUIRE(d.ready());

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            BYTE b = (BYTE)((x * 8) & 0xF8);
            BYTE g = (BYTE)((y * 8) & 0xF8);
            BYTE r = (BYTE)(((x + y) * 8) & 0xF8);
            CHECK(screenPixel(d, x, y) == expectRgb555(b, g, r));
        }
    }
}

TEST_CASE("RDENC5 帧类型字节与 BITMAPINFO 头")
{
    const int w = 5;
    const int h = 4;
    TestImage img = makeBgra(w, h, 0, fillGradient);

    CScreenFrameEncoder enc;
    enc.Feed(viewOf(img));
    std::vector<BYTE> bmi;
    std::vector<BYTE> first;
    std::vector<BYTE> next;
    enc.GetBitmapInfo(bmi);
    enc.GetFirstFrame(first);
    POINT cur;
    cur.x = 0;
    cur.y = 0;
    enc.GetNextFrame(next, cur, 0);

    REQUIRE(bmi.size() >= 1 + sizeof(BITMAPINFOHEADER));
    CHECK(bmi[0] == (BYTE)rd_frame_bitmapinfo);
    CHECK(first[0] == (BYTE)rd_frame_first);
    CHECK(next[0] == (BYTE)rd_frame_next);
    CHECK(next[1] == (BYTE)rd_algo_diff);

    BITMAPINFOHEADER hdr;
    memcpy(&hdr, &bmi[1], sizeof(hdr));
    CHECK(hdr.biBitCount == 16);
    CHECK(hdr.biWidth == w);
    CHECK(hdr.biHeight == h);
    CHECK(hdr.biCompression == (DWORD)BI_RGB);
}

TEST_CASE("RDENC6 光标位置与类型透传")
{
    const int w = 4;
    const int h = 4;
    TestImage img = makeBgra(w, h, 0, fillGradient);

    CScreenFrameEncoder enc;
    enc.Feed(viewOf(img));
    std::vector<BYTE> bmi;
    std::vector<BYTE> first;
    enc.GetBitmapInfo(bmi);
    enc.GetFirstFrame(first);

    RdScreenDecoder d;
    d.apply(bmi.data(), bmi.size());
    d.apply(first.data(), first.size());

    enc.Feed(viewOf(img));
    std::vector<BYTE> next;
    POINT cur;
    cur.x = 37;
    cur.y = 91;
    enc.GetNextFrame(next, cur, 7);
    CHECK(d.apply(next.data(), next.size()));
    CHECK(d.cursorPos().x == 37);
    CHECK(d.cursorPos().y == 91);
    CHECK(d.cursorType() == 7);
}
