/**
 *  Windows.Media.Ocr 后端的单元测试，用例 OCRE1 ~ OCRE5、OCRS1、OCRP5（docs/图片文字识别方案.md 第 10.1 节）。
 *
 *    - OCRE1 [ocrengine] 本机报告可用，语言列表里有 zh-Hans，界面语言 zh-CN 时默认识别语言是简体中文
 *    - OCRE2 [ocrengine] 24px 的「会议时间」能识别出来，行框落在图内，词框落在行框内
 *    - OCRE3 [ocrengine] 12px 小字：自动放大后认对的字数多于只识别原图，且结果报告做了放大
 *    - OCRE4 [ocrengine] 取消标志已置位时返回 ocr_cancelled，不给结果
 *    - OCRE5 [ocrengine] 空白图返回成功、0 行
 *    - OCRS1 [ocrdll]    默认识别语言的选择：界面语言换成识别语言前缀、按前缀在列表里找（不区分大小写、整段匹配）
 *    - OCRP5 [ocrdll]    GDI+ 缩放：尺寸正确、完全不透明、颜色基本保持
 *
 *  OCRE 一组调用真实的系统识别引擎，测试图用 GDI+ 以微软雅黑绘制。本机没有中文识别组件（或不是 Win10 及以上）时
 *  整组跳过（SKIP），不算失败。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-05
 */
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <gdiplus.h>
#include <string.h>
#include <string>
#include <vector>

#include "../FlamingoOcrApi.h"
#include "../OcrApiTable.h"
#include "../Core/OcrResultBuilder.h"
#include "../WinOcr/OcrBitmapScaler.h"
#include "../WinOcr/OcrLanguagePolicy.h"
#include "../WinOcr/WinOcrEngine.h"

using namespace flamingoocr;

namespace {

//每像素字节数（32 位 BGRA）
const uint32_t kBytesPerPixel = 4;

//测试图所用字体
const wchar_t* const kFontName = L"Microsoft YaHei";

//OCRE2 的字号（像素）与图像尺寸
const float kLargeFontPx = 24.0f;
const uint32_t kLargeWidth = 700;
const uint32_t kLargeHeight = 80;

//OCRE3 的字号（像素）与图像尺寸：12px 是 100% 缩放下界面截图里常见的字号，第 0 期实测原图几乎认不出
const float kSmallFontPx = 12.0f;
const uint32_t kSmallWidth = 420;
const uint32_t kSmallHeight = 60;

//OCRE5 空白图的尺寸
const uint32_t kBlankWidth = 200;
const uint32_t kBlankHeight = 100;

//文字左上角距图像边缘的距离（像素）
const float kTextMargin = 12.0f;

//行框允许超出图像边缘的误差（像素）：引擎给出的浮点坐标可能比边缘多出零点几像素
const float kEdgeTolerance = 1.0f;

//一张 BGRA 测试图
struct TestBitmap
{
    std::vector<uint8_t> m_pixels;   //像素，行跨度为 m_width × 4
    uint32_t             m_width;    //宽（像素）
    uint32_t             m_height;   //高（像素）

    OcrImage AsImage() const
    {
        OcrImage image;
        image.m_cbSize = sizeof(OcrImage);
        image.m_width = m_width;
        image.m_height = m_height;
        image.m_stride = m_width * kBytesPerPixel;
        image.m_pixels = &m_pixels[0];
        return image;
    }
};

//在白底上用指定字号画几行黑字，行距为字号的 2 倍
TestBitmap RenderText(const std::vector<std::wstring>& lines, float fontPx, uint32_t width, uint32_t height)
{
    TestBitmap out;
    out.m_width = width;
    out.m_height = height;
    out.m_pixels.assign(static_cast<size_t>(width) * height * kBytesPerPixel, 0xFF);
    REQUIRE(EnsureOcrGdiplus());

    Gdiplus::Bitmap bmp(static_cast<INT>(width), static_cast<INT>(height), PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&bmp);
        g.Clear(Gdiplus::Color(255, 255, 255, 255));
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
        Gdiplus::Font font(kFontName, fontPx, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush brush(Gdiplus::Color(255, 0, 0, 0));
        float y = kTextMargin;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            g.DrawString(lines[i].c_str(), -1, &font, Gdiplus::PointF(kTextMargin, y), &brush);
            y += fontPx * 2.0f;
        }
    }

    Gdiplus::BitmapData data;
    Gdiplus::Rect rect(0, 0, static_cast<INT>(width), static_cast<INT>(height));
    REQUIRE(bmp.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) == Gdiplus::Ok);
    const size_t rowBytes = static_cast<size_t>(width) * kBytesPerPixel;
    for (uint32_t row = 0; row < height; ++row)
    {
        memcpy(&out.m_pixels[row * rowBytes], static_cast<const uint8_t*>(data.Scan0) + static_cast<ptrdiff_t>(row) * data.Stride,
               rowBytes);
    }
    bmp.UnlockBits(&data);
    return out;
}

//把文字转成以空格分隔的 Unicode 码点（如 "4F1A 8BAE"），断言失败时在控制台上也能看清认成了哪个字
std::string ToCodePoints(const std::wstring& text)
{
    std::string out;
    char buf[8];
    for (size_t i = 0; i < text.size(); ++i)
    {
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%04X ", static_cast<unsigned>(text[i]));
        out += buf;
    }
    return out;
}

//去掉空白字符
std::wstring StripSpaces(const std::wstring& text)
{
    std::wstring out;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] != L' ' && text[i] != L'\t' && text[i] != L'\r' && text[i] != L'\n')
            out += text[i];
    }
    return out;
}

//编辑距离
size_t EditDistance(const std::wstring& a, const std::wstring& b)
{
    std::vector<size_t> prev(b.size() + 1);
    std::vector<size_t> cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j)
        prev[j] = j;
    for (size_t i = 1; i <= a.size(); ++i)
    {
        cur[0] = i;
        for (size_t j = 1; j <= b.size(); ++j)
        {
            size_t best = prev[j - 1] + ((a[i - 1] == b[j - 1]) ? 0 : 1);
            if (prev[j] + 1 < best)
                best = prev[j] + 1;
            if (cur[j - 1] + 1 < best)
                best = cur[j - 1] + 1;
            cur[j] = best;
        }
        prev.swap(cur);
    }
    return prev[b.size()];
}

//认对的字数：标准答案字数减去编辑距离（比较前去掉空白），不低于 0
size_t CorrectChars(const std::wstring& expected, const std::wstring& actual)
{
    const std::wstring e = StripSpaces(expected);
    const std::wstring a = StripSpaces(actual);
    const size_t d = EditDistance(e, a);
    return (d >= e.size()) ? 0 : e.size() - d;
}

//把结果的各行连成一段文字（行间以换行分隔）
std::wstring JoinLines(const OcrResult* result)
{
    std::wstring out;
    for (uint32_t i = 0; i < result->m_lineCount; ++i)
    {
        if (i > 0)
            out += L'\n';
        out += result->m_lines[i].m_text;
    }
    return out;
}

//换上真实的 Windows 后端并初始化；本机不能识别简体中文时返回 false
bool PrepareRealEngine()
{
    SetOcrBackendForTest(GetWinOcrBackend());
    const OcrApi* api = GetOcrApiTable();
    OcrHostInfo host;
    host.m_cbSize = sizeof(host);
    host.m_log = NULL;
    host.m_uiLanguage = L"zh-CN";
    if (api->m_init(&host) != ocr_ok)
        return false;
    OcrAvailabilityInfo info;
    info.m_cbSize = sizeof(info);
    if (api->m_queryAvailability(&info) != ocr_ok || info.m_availability != ocr_available)
        return false;
    std::vector<std::wstring> tags;
    for (uint32_t i = 0; i < api->m_getLanguageCount(); ++i)
        tags.push_back(api->m_getLanguageTag(i));
    return FindOcrLanguageByPrefix(tags, L"zh-Hans") >= 0;
}

//离开作用域时卸掉后端并恢复第二遍
class RealEngineScope
{
public:
    RealEngineScope()
        : m_ready(PrepareRealEngine())
    {
    }
    ~RealEngineScope()
    {
        SetWinOcrSecondPassEnabledForTest(true);
        SetOcrBackendForTest(NULL);
    }
    bool Ready() const
    {
        return m_ready;
    }

private:
    bool m_ready;   //本机能否识别简体中文
};

} // namespace

TEST_CASE("OCRE1 本机报告可用，有简体中文识别语言并被选为默认语言", "[ocrengine]")
{
    RealEngineScope scope;
    if (!scope.Ready())
        SKIP("this machine cannot recognize zh-Hans (needs Windows 10+ with the Chinese OCR component)");

    const OcrApi* api = GetOcrApiTable();
    CHECK(std::wstring(api->m_getEngineName()) == L"Windows.Media.Ocr");
    REQUIRE(api->m_getLanguageCount() > 0);
    for (uint32_t i = 0; i < api->m_getLanguageCount(); ++i)
    {
        CHECK(api->m_getLanguageTag(i) != NULL);
        CHECK(api->m_getLanguageName(i) != NULL);
    }
    REQUIRE(api->m_getDefaultLanguage() != NULL);
    std::vector<std::wstring> one(1, api->m_getDefaultLanguage());
    CHECK(FindOcrLanguageByPrefix(one, L"zh-Hans") == 0);
}

TEST_CASE("OCRE2 24px 的会议时间能识别出来，行框与词框位置合理", "[ocrengine]")
{
    RealEngineScope scope;
    if (!scope.Ready())
        SKIP("this machine cannot recognize zh-Hans");

    //用一整行而不是孤立的四个字：2026-10-05 实测孤立的「会议时间」被读成「刽义时间」（缺少上下文），
    //同样字号的整句在第 0 期里是全对的
    const std::wstring sentence = L"会议时间：10月8日 14:30，地点三楼A会议室";
    std::vector<std::wstring> lines(1, sentence);
    TestBitmap bmp = RenderText(lines, kLargeFontPx, kLargeWidth, kLargeHeight);
    OcrImage image = bmp.AsImage();
    const OcrApi* api = GetOcrApiTable();
    OcrResult* result = NULL;
    REQUIRE(api->m_recognize(&image, NULL, NULL, &result) == ocr_ok);
    REQUIRE(result != NULL);

    const std::wstring text = JoinLines(result);
    INFO("recognized code points: " << ToCodePoints(text));
    CHECK(StripSpaces(text).find(L"会议时间") != std::wstring::npos);
    //整行认对的字不少于 80%（引擎偶有个别错字，这里只确认整体可用）
    const size_t expectedChars = StripSpaces(sentence).size();
    CHECK(CorrectChars(sentence, text) * 5 >= expectedChars * 4);
    CHECK(text.find(L"会 议") == std::wstring::npos);   //拼行整理去掉了汉字之间的空格
    REQUIRE(result->m_lineCount >= 1);
    for (uint32_t i = 0; i < result->m_lineCount; ++i)
    {
        const OcrLine& line = result->m_lines[i];
        CHECK(line.m_rect.m_x >= -kEdgeTolerance);
        CHECK(line.m_rect.m_y >= -kEdgeTolerance);
        CHECK(line.m_rect.m_x + line.m_rect.m_w <= kLargeWidth + kEdgeTolerance);
        CHECK(line.m_rect.m_y + line.m_rect.m_h <= kLargeHeight + kEdgeTolerance);
        for (uint32_t j = 0; j < line.m_wordCount; ++j)
        {
            const OcrRect& w = line.m_words[j].m_rect;
            CHECK(w.m_x >= line.m_rect.m_x - kEdgeTolerance);
            CHECK(w.m_x + w.m_w <= line.m_rect.m_x + line.m_rect.m_w + kEdgeTolerance);
        }
    }
    api->m_freeResult(result);
}

TEST_CASE("OCRE3 12px 小字自动放大后认对的字数多于只识别原图", "[ocrengine]")
{
    RealEngineScope scope;
    if (!scope.Ready())
        SKIP("this machine cannot recognize zh-Hans");

    const std::wstring line1 = L"会议时间：10月8日 14:30，地点三楼A会议室";
    const std::wstring line2 = L"请各位同事提前做好工作交接";
    std::vector<std::wstring> lines;
    lines.push_back(line1);
    lines.push_back(line2);
    TestBitmap bmp = RenderText(lines, kSmallFontPx, kSmallWidth, kSmallHeight);
    OcrImage image = bmp.AsImage();
    const OcrApi* api = GetOcrApiTable();
    const std::wstring expected = line1 + line2;

    SetWinOcrSecondPassEnabledForTest(false);
    OcrResult* single = NULL;
    REQUIRE(api->m_recognize(&image, NULL, NULL, &single) == ocr_ok);
    REQUIRE(single != NULL);
    const size_t singleCorrect = CorrectChars(expected, JoinLines(single));
    CHECK(single->m_scalePercent == 100);
    api->m_freeResult(single);

    SetWinOcrSecondPassEnabledForTest(true);
    OcrResult* automatic = NULL;
    REQUIRE(api->m_recognize(&image, NULL, NULL, &automatic) == ocr_ok);
    REQUIRE(automatic != NULL);
    const size_t autoCorrect = CorrectChars(expected, JoinLines(automatic));
    INFO("single pass correct " << singleCorrect << ", auto correct " << autoCorrect << " of " << StripSpaces(expected).size()
         << ", auto text: " << ToCodePoints(JoinLines(automatic)));
    CHECK(automatic->m_scalePercent > 100);
    CHECK(autoCorrect > singleCorrect);
    api->m_freeResult(automatic);
}

TEST_CASE("OCRE4 取消标志已置位时返回已取消", "[ocrengine]")
{
    RealEngineScope scope;
    if (!scope.Ready())
        SKIP("this machine cannot recognize zh-Hans");

    std::vector<std::wstring> lines(1, L"会议时间");
    TestBitmap bmp = RenderText(lines, kLargeFontPx, kLargeWidth, kLargeHeight);
    OcrImage image = bmp.AsImage();
    const OcrApi* api = GetOcrApiTable();
    volatile long cancel = 1;
    OcrResult* result = reinterpret_cast<OcrResult*>(static_cast<uintptr_t>(0x1));
    CHECK(api->m_recognize(&image, NULL, &cancel, &result) == ocr_cancelled);
    CHECK(result == NULL);

    //指定一个本机没有的识别语言
    CHECK(api->m_recognize(&image, L"xx-Fake", NULL, &result) == ocr_language_unsupported);
    CHECK(result == NULL);
}

TEST_CASE("OCRE5 空白图返回成功且没有任何行", "[ocrengine]")
{
    RealEngineScope scope;
    if (!scope.Ready())
        SKIP("this machine cannot recognize zh-Hans");

    std::vector<std::wstring> none;
    TestBitmap bmp = RenderText(none, kLargeFontPx, kBlankWidth, kBlankHeight);
    OcrImage image = bmp.AsImage();
    const OcrApi* api = GetOcrApiTable();
    const long liveBefore = OcrResultLiveCount();
    OcrResult* result = NULL;
    REQUIRE(api->m_recognize(&image, NULL, NULL, &result) == ocr_ok);
    REQUIRE(result != NULL);
    CHECK(result->m_lineCount == 0);
    api->m_freeResult(result);
    CHECK(OcrResultLiveCount() == liveBefore);
}

TEST_CASE("OCRS1 默认识别语言的选择", "[ocrdll]")
{
    CHECK(OcrLanguagePrefixForUi(L"zh-CN") == L"zh-Hans");
    CHECK(OcrLanguagePrefixForUi(L"zh-SG") == L"zh-Hans");
    CHECK(OcrLanguagePrefixForUi(L"zh") == L"zh-Hans");
    CHECK(OcrLanguagePrefixForUi(L"zh-Hans-CN") == L"zh-Hans");
    CHECK(OcrLanguagePrefixForUi(L"zh-TW") == L"zh-Hant");
    CHECK(OcrLanguagePrefixForUi(L"zh-HK") == L"zh-Hant");
    CHECK(OcrLanguagePrefixForUi(L"zh_MO") == L"zh-Hant");
    CHECK(OcrLanguagePrefixForUi(L"zh-Hant") == L"zh-Hant");
    CHECK(OcrLanguagePrefixForUi(L"en-US") == L"en");
    CHECK(OcrLanguagePrefixForUi(L"ja-JP") == L"ja");
    CHECK(OcrLanguagePrefixForUi(L"KO-kr") == L"ko");
    CHECK(OcrLanguagePrefixForUi(NULL).empty());
    CHECK(OcrLanguagePrefixForUi(L"").empty());

    std::vector<std::wstring> tags;
    tags.push_back(L"en-US");
    tags.push_back(L"zh-Hans-CN");
    tags.push_back(L"ja");
    CHECK(FindOcrLanguageByPrefix(tags, L"zh-Hans") == 1);
    CHECK(FindOcrLanguageByPrefix(tags, L"ZH-HANS") == 1);
    CHECK(FindOcrLanguageByPrefix(tags, L"en") == 0);
    CHECK(FindOcrLanguageByPrefix(tags, L"ja") == 2);
    CHECK(FindOcrLanguageByPrefix(tags, L"zh-Hant") == -1);
    CHECK(FindOcrLanguageByPrefix(tags, L"e") == -1);       //必须整段匹配，e 不能匹配 en-US
    CHECK(FindOcrLanguageByPrefix(tags, L"zh-H") == -1);
    CHECK(FindOcrLanguageByPrefix(tags, L"") == -1);

    CHECK(SameLanguageTag(L"zh-Hans-CN", L"ZH-hans-cn"));
    CHECK_FALSE(SameLanguageTag(L"zh-Hans-CN", L"zh-Hans"));
}

TEST_CASE("OCRP5 GDI+ 缩放：尺寸正确、不透明、颜色基本保持", "[ocrdll]")
{
    REQUIRE(EnsureOcrGdiplus());

    //2×2：左上红、右上绿、左下蓝、右下白（BGRA）
    const uint8_t src[16] = { 0, 0, 255, 255,   0, 255, 0, 255,   255, 0, 0, 255,   255, 255, 255, 255 };
    std::vector<uint8_t> up;
    REQUIRE(ScaleBgra(src, 2, 2, 8, 6, 6, up));
    REQUIRE(up.size() == 6u * 6u * kBytesPerPixel);
    for (size_t i = 3; i < up.size(); i += kBytesPerPixel)
        CHECK(up[i] == 255);
    //左上角仍以红色为主
    CHECK(up[2] > 180);
    CHECK(up[1] < 90);
    CHECK(up[0] < 90);
    //右下角仍接近白色
    const size_t last = up.size() - kBytesPerPixel;
    CHECK(up[last] > 180);
    CHECK(up[last + 1] > 180);
    CHECK(up[last + 2] > 180);

    //4×4 全黑缩到 2×2，仍是黑色
    std::vector<uint8_t> black(4 * 4 * kBytesPerPixel, 0);
    for (size_t i = 3; i < black.size(); i += kBytesPerPixel)
        black[i] = 255;
    std::vector<uint8_t> down;
    REQUIRE(ScaleBgra(&black[0], 4, 4, 16, 2, 2, down));
    REQUIRE(down.size() == 2u * 2u * kBytesPerPixel);
    CHECK(down[0] < 10);
    CHECK(down[1] < 10);
    CHECK(down[2] < 10);

    //非法参数
    std::vector<uint8_t> none;
    CHECK_FALSE(ScaleBgra(NULL, 2, 2, 8, 4, 4, none));
    CHECK_FALSE(ScaleBgra(src, 2, 2, 8, 0, 4, none));
}
