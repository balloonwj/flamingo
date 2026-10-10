/**
 *  拼行整理与图像尺寸策略的单元测试，用例 OCRL1 ~ OCRL6、OCRP1 ~ OCRP4（docs/图片文字识别方案.md 第 10.1 节）。
 *
 *    - OCRL1 两个汉字之间不加空格，间距再大也不加
 *    - OCRL2 拉丁词之间间距超过行高的 0.35 倍时保留一个空格，不超过时合并
 *    - OCRL3 汉字与拉丁词之间间距超过行高的 0.25 倍才加空格
 *    - OCRL4 被拆开的数字（间距很小）合并，「1 0 月」拼成「10月」
 *    - OCRL5 全角标点前后不加空格
 *    - OCRL6 行框等于词框的并集；乱序输入先按横坐标排序；空词被跳过；行高与行高中位数的取法
 *    - OCRP1 放大倍数：行高 ≥ 20 不放大，12 ~ 20 放大 2 倍，< 12 放大 3 倍；放大后最长边超过上限时降一档；
 *            第一遍一行都没识别出来时放大 2 倍再试
 *    - OCRP2 最长边超过上限的图等比缩小，缩放后的尺寸向下取整且不超过上限
 *    - OCRP3 缩放后识别出的坐标换算回原图坐标
 *    - OCRP4 透明背景铺白：全透明变白、不透明不变、半透明按比例混合
 *
 *  框架 Catch2 v3，标签 [ocrdll]。
 *  balloonwj@qq.com   2026-10-05
 */
#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../WinOcr/OcrLineAssembler.h"
#include "../WinOcr/OcrScalePolicy.h"

using namespace flamingoocr;

namespace {

//测试用的引擎上限（像素），与本机 OcrEngine::MaxImageDimension 实测值相同
const uint32_t kMaxDim = 10000;

//构造一个词
OcrWordBox Word(const wchar_t* text, float x, float y, float w, float h)
{
    OcrWordBox word;
    word.m_text = text;
    word.m_rect.m_x = x;
    word.m_rect.m_y = y;
    word.m_rect.m_w = w;
    word.m_rect.m_h = h;
    return word;
}

//构造一个矩形
OcrRect Rect(float x, float y, float w, float h)
{
    OcrRect r;
    r.m_x = x;
    r.m_y = y;
    r.m_w = w;
    r.m_h = h;
    return r;
}

} // namespace

TEST_CASE("OCRL1 两个汉字之间不加空格", "[ocrdll]")
{
    std::vector<OcrWordBox> words;
    words.push_back(Word(L"会", 0, 0, 16, 16));
    words.push_back(Word(L"议", 18, 0, 16, 16));
    words.push_back(Word(L"纪", 60, 0, 16, 16));   //与上一个字相距 26 像素，超过行高，仍不加空格
    words.push_back(Word(L"要", 78, 0, 16, 16));
    CHECK(AssembleLineText(words) == L"会议纪要");

    CHECK(IsCjkChar(L'会'));
    CHECK(IsCjkChar(L'。'));
    CHECK(IsCjkChar(L'あ'));
    CHECK(IsCjkChar(L'한'));
    CHECK_FALSE(IsCjkChar(L'A'));
    CHECK_FALSE(IsCjkChar(L'1'));
    CHECK_FALSE(IsCjkChar(L'.'));
}

TEST_CASE("OCRL2 拉丁词之间按间距决定是否保留空格", "[ocrdll]")
{
    //行高 14：阈值为 0.35 × 14 = 4.9 像素
    std::vector<OcrWordBox> spaced;
    spaced.push_back(Word(L"Please", 0, 0, 60, 14));
    spaced.push_back(Word(L"reply", 66, 0, 50, 14));   //间距 6 > 4.9
    CHECK(AssembleLineText(spaced) == L"Please reply");

    //引擎把一个单词拆成几段时（re p ly），段间距很小，拼回一个词
    std::vector<OcrWordBox> split;
    split.push_back(Word(L"re", 0, 0, 20, 14));
    split.push_back(Word(L"p", 21, 0, 9, 14));    //间距 1
    split.push_back(Word(L"ly", 32, 0, 16, 14));  //间距 2
    CHECK(AssembleLineText(split) == L"reply");
}

TEST_CASE("OCRL3 汉字与拉丁词之间间距超过行高的 0.25 倍才加空格", "[ocrdll]")
{
    //行高取汉字的高度 16：阈值为 4 像素
    std::vector<OcrWordBox> spaced;
    spaced.push_back(Word(L"是", 0, 0, 16, 16));
    spaced.push_back(Word(L"banner", 22, 2, 50, 12));   //间距 6 > 4
    spaced.push_back(Word(L"那", 78, 0, 16, 16));       //间距 6 > 4
    CHECK(AssembleLineText(spaced) == L"是 banner 那");

    std::vector<OcrWordBox> tight;
    tight.push_back(Word(L"版", 0, 0, 16, 16));
    tight.push_back(Word(L"v2", 18, 2, 14, 12));        //间距 2 < 4
    CHECK(AssembleLineText(tight) == L"版v2");
}

TEST_CASE("OCRL4 被拆开的数字合并", "[ocrdll]")
{
    //第 0 期实测里系统把「10月」读成「1 0 月」三个词
    std::vector<OcrWordBox> words;
    words.push_back(Word(L"1", 0, 2, 8, 14));
    words.push_back(Word(L"0", 10, 2, 8, 14));    //间距 2，行高 16（取汉字），阈值 5.6
    words.push_back(Word(L"月", 20, 0, 16, 16));  //间距 2 < 4
    CHECK(AssembleLineText(words) == L"10月");
}

TEST_CASE("OCRL5 全角标点前后不加空格", "[ocrdll]")
{
    std::vector<OcrWordBox> words;
    words.push_back(Word(L"登录失败", 0, 0, 64, 16));
    words.push_back(Word(L"：", 65, 0, 16, 16));
    words.push_back(Word(L"错误码", 82, 0, 48, 16));
    CHECK(AssembleLineText(words) == L"登录失败：错误码");

    std::vector<OcrWordBox> paren;
    paren.push_back(Word(L"（", 0, 0, 16, 16));
    paren.push_back(Word(L"session", 17, 2, 56, 12));   //间距 1 < 4
    paren.push_back(Word(L"）", 74, 0, 16, 16));        //间距 1 < 4
    CHECK(AssembleLineText(paren) == L"（session）");
}

TEST_CASE("OCRL6 行框、排序、空词与行高的取法", "[ocrdll]")
{
    //乱序输入按横坐标排序后再拼
    std::vector<OcrWordBox> words;
    words.push_back(Word(L"间", 54, 1, 16, 16));
    words.push_back(Word(L"会", 0, 0, 16, 16));
    words.push_back(Word(L"", 30, 0, 0, 0));         //空词被跳过
    words.push_back(Word(L"议", 18, 0, 16, 17));
    words.push_back(Word(L"时", 36, 0, 16, 16));
    CHECK(AssembleLineText(words) == L"会议时间");

    //并集：左 0、上 0、右 70、下 17
    OcrRect u = UnionRect(words);
    CHECK(u.m_x == Catch::Approx(0));
    CHECK(u.m_y == Catch::Approx(0));
    CHECK(u.m_w == Catch::Approx(70));
    CHECK(u.m_h == Catch::Approx(17));

    std::vector<OcrWordBox> none;
    OcrRect empty = UnionRect(none);
    CHECK(empty.m_w == Catch::Approx(0));
    CHECK(AssembleLineText(none).empty());

    //行高：只看含汉字的词（拉丁词 12 被忽略），偶数个时取中间两个的平均
    std::vector<OcrWordBox> mixed;
    mixed.push_back(Word(L"是", 0, 0, 16, 16));
    mixed.push_back(Word(L"banner", 20, 0, 50, 12));
    mixed.push_back(Word(L"那", 80, 0, 16, 18));
    CHECK(LineHeightOf(mixed) == Catch::Approx(17));

    //没有汉字时取全部词
    std::vector<OcrWordBox> latin;
    latin.push_back(Word(L"Please", 0, 0, 60, 14));
    latin.push_back(Word(L"reply", 66, 0, 50, 10));
    latin.push_back(Word(L"to", 120, 0, 20, 12));
    CHECK(LineHeightOf(latin) == Catch::Approx(12));

    //行高中位数：三行的行高 17、12、30 → 17；没有词的行不计
    std::vector<std::vector<OcrWordBox> > lines;
    lines.push_back(mixed);
    lines.push_back(latin);
    std::vector<OcrWordBox> big;
    big.push_back(Word(L"标", 0, 0, 30, 30));
    lines.push_back(big);
    lines.push_back(none);
    CHECK(MedianLineHeight(lines) == Catch::Approx(17));
    std::vector<std::vector<OcrWordBox> > noLines;
    CHECK(MedianLineHeight(noLines) == Catch::Approx(0));
}

TEST_CASE("OCRP1 按行高中位数选放大倍数，放大后不超过引擎上限", "[ocrdll]")
{
    CHECK(ChooseUpscaleFactor(25.0f, 1440, 900, kMaxDim) == 1);
    CHECK(ChooseUpscaleFactor(20.0f, 1440, 900, kMaxDim) == 1);
    CHECK(ChooseUpscaleFactor(19.9f, 1440, 900, kMaxDim) == 2);
    CHECK(ChooseUpscaleFactor(12.0f, 1440, 900, kMaxDim) == 2);
    CHECK(ChooseUpscaleFactor(11.9f, 1440, 900, kMaxDim) == 3);
    CHECK(ChooseUpscaleFactor(6.0f, 1440, 900, kMaxDim) == 3);

    //放大 3 倍后 12000 超过上限，降到 2 倍（8000）
    CHECK(ChooseUpscaleFactor(10.0f, 4000, 3000, kMaxDim) == 2);
    //放大 2 倍后 12000 也超过上限，不放大
    CHECK(ChooseUpscaleFactor(10.0f, 6000, 3000, kMaxDim) == 1);
    //恰好等于上限时允许
    CHECK(ChooseUpscaleFactor(10.0f, 5000, 3000, kMaxDim) == 2);

    //第一遍一行都没识别出来：放大 2 倍再试；放不下时不再试
    CHECK(ChooseUpscaleFactor(0.0f, 800, 600, kMaxDim) == kRetryUpscaleFactorWhenEmpty);
    CHECK(ChooseUpscaleFactor(0.0f, 6000, 3000, kMaxDim) == 1);
}

TEST_CASE("OCRP2 最长边超过上限的图等比缩小", "[ocrdll]")
{
    CHECK(ComputeDownscale(8000, 6000, kMaxDim) == Catch::Approx(1.0f));
    CHECK(ComputeDownscale(10000, 6000, kMaxDim) == Catch::Approx(1.0f));
    const float s = ComputeDownscale(12000, 3000, kMaxDim);
    CHECK(s == Catch::Approx(10000.0f / 12000.0f));

    uint32_t w = 0;
    uint32_t h = 0;
    ScaledSize(12000, 3000, s, w, h);
    //比例按 float 存，乘回去可能比精确值小一点，向下取整后差 1 像素
    CHECK(w <= kMaxDim);
    CHECK(w >= kMaxDim - 1);
    CHECK(h >= 2499);
    CHECK(h <= 2500);

    //竖长图按高度缩小
    CHECK(ComputeDownscale(3000, 20000, kMaxDim) == Catch::Approx(0.5f));

    //缩放后至少 1 像素
    ScaledSize(1, 1, 0.01f, w, h);
    CHECK(w == 1);
    CHECK(h == 1);

    //放大
    ScaledSize(1440, 900, 3.0f, w, h);
    CHECK(w == 4320);
    CHECK(h == 2700);
}

TEST_CASE("OCRP3 缩放后的坐标换算回原图", "[ocrdll]")
{
    OcrRect up = MapRectToSource(Rect(20, 40, 60, 80), 2.0f);
    CHECK(up.m_x == Catch::Approx(10));
    CHECK(up.m_y == Catch::Approx(20));
    CHECK(up.m_w == Catch::Approx(30));
    CHECK(up.m_h == Catch::Approx(40));

    OcrRect down = MapRectToSource(Rect(20, 40, 60, 80), 0.5f);
    CHECK(down.m_x == Catch::Approx(40));
    CHECK(down.m_h == Catch::Approx(160));

    //比例非法时原样返回
    OcrRect same = MapRectToSource(Rect(1, 2, 3, 4), 0.0f);
    CHECK(same.m_x == Catch::Approx(1));
    CHECK(same.m_h == Catch::Approx(4));
}

TEST_CASE("OCRP4 透明背景铺白", "[ocrdll]")
{
    //一行三个像素，再留 4 字节行尾填充（检查行跨度生效、填充不被改写）
    const uint32_t width = 3;
    const uint32_t stride = width * 4 + 4;
    std::vector<uint8_t> px(stride * 2, 0xAB);
    //第一行：全透明黑、不透明黑、半透明红（BGRA）
    uint8_t row0[12] = { 0, 0, 0, 0,   0, 0, 0, 255,   0, 0, 255, 128 };
    //第二行：全透明但颜色非零（如 PNG 里残留的颜色）、不透明白、四分之一透明蓝
    uint8_t row1[12] = { 10, 20, 30, 0,   255, 255, 255, 255,   255, 0, 0, 64 };
    for (int i = 0; i < 12; ++i)
    {
        px[i] = row0[i];
        px[stride + i] = row1[i];
    }

    FlattenOnWhite(&px[0], width, 2, stride);

    //全透明 → 白
    CHECK(px[0] == 255);
    CHECK(px[1] == 255);
    CHECK(px[2] == 255);
    CHECK(px[3] == 255);
    //不透明黑不变
    CHECK(px[4] == 0);
    CHECK(px[6] == 0);
    CHECK(px[7] == 255);
    //半透明红：B、G = (0×128 + 255×127 + 127) / 255 = 127，R = (255×128 + 255×127 + 127) / 255 = 255
    CHECK(px[8] == 127);
    CHECK(px[9] == 127);
    CHECK(px[10] == 255);
    CHECK(px[11] == 255);
    //行尾填充不被改写
    CHECK(px[12] == 0xAB);
    CHECK(px[15] == 0xAB);
    //第二行：全透明但颜色非零 → 白
    CHECK(px[stride + 0] == 255);
    CHECK(px[stride + 2] == 255);
    //四分之一透明蓝：B = (255×64 + 255×191 + 127) / 255 = 255，G、R = (0 + 255×191 + 127) / 255 = 191
    CHECK(px[stride + 8] == 255);
    CHECK(px[stride + 9] == 191);
    CHECK(px[stride + 10] == 191);
}
