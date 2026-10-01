/**
 *  ImagePlaceholderTests.cpp
 *
 *  图片占位符排版（ImagePlaceholder）的测试：验证纯函数 ComputeImagePlaceholderLayout()
 *  在各种占位块尺寸下给出的图标大小、位置，以及说明文字是否显示、显示在哪里；外加
 *  MeasureImagePlaceholderCaption() 的基本行为。设计稿见
 *  snapshots/broken_image_placeholder_design.png。
 *
 *  用 Catch2 v3（amalgamated）作框架。排版用例是纯内存断言；测量用例只借用一个内存
 *  DC，不创建窗口。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonChatWnd/ImagePlaceholder.h"
#include "../UI/BalloonChatWnd/ChatMessageMenu.h"   // kChatImageState*

#include <cstdlib>   // std::abs
#include <string>    // std::wstring：比较说明文字

using namespace balloonchatwnd;

namespace {

// 测试用的说明文字尺寸（像素）：约等于 12 像素字号下"图片加载失败"六个字的宽高。
// 排版用例不依赖真实字体，直接给定尺寸。
const int kTestCaptionW = 72;
const int kTestCaptionH = 16;

// 居中判定允许的误差（像素）：整数除法取整最多带来 1 像素的偏差。
const int kCenterTolerance = 1;

// 百分比换算的基数。
const int kPercentBase = 100;

RECT MakeRect(long left, long top, long right, long bottom)
{
    RECT r;
    r.left = left;
    r.top = top;
    r.right = right;
    r.bottom = bottom;
    return r;
}

// 按排版结果算出图标（连同文字）这一整块的上下边缘。
int BlockTop(const ImagePlaceholderLayout& lay)
{
    const int glyphH = lay.m_glyphSize * kImagePlaceholderGlyphHeightPercent / kPercentBase;
    return lay.m_glyphCenter.y - glyphH / 2;
}

int BlockBottom(const ImagePlaceholderLayout& lay)
{
    if (lay.m_showCaption)
    {
        return lay.m_captionRect.bottom;
    }
    const int glyphH = lay.m_glyphSize * kImagePlaceholderGlyphHeightPercent / kPercentBase;
    return BlockTop(lay) + glyphH;
}

} // anonymous namespace

TEST_CASE("IPL1 220x165 的大占位块：图标取上限，显示文字，整体居中", "[imgph]")
{
    //短边 165 的 30% 是 49，超过上限，取 kImagePlaceholderGlyphMax。
    const RECT rc = MakeRect(0, 0, 220, 165);
    const ImagePlaceholderLayout lay = ComputeImagePlaceholderLayout(rc, kTestCaptionW, kTestCaptionH);

    REQUIRE(lay.m_drawGlyph);
    CHECK(lay.m_glyphSize == kImagePlaceholderGlyphMax);
    REQUIRE(lay.m_showCaption);
    CHECK(lay.m_captionRect.right - lay.m_captionRect.left == kTestCaptionW);
    CHECK(lay.m_captionRect.bottom - lay.m_captionRect.top == kTestCaptionH);

    //文字在图标下方，隔开固定间距。
    const int glyphH = lay.m_glyphSize * kImagePlaceholderGlyphHeightPercent / kPercentBase;
    CHECK(lay.m_captionRect.top == BlockTop(lay) + glyphH + kImagePlaceholderCaptionGap);

    //整体在占位块内上下、左右都居中。
    INFO("块上=" << BlockTop(lay) << " 块下=" << BlockBottom(lay));
    CHECK(std::abs((BlockTop(lay) - rc.top) - (rc.bottom - BlockBottom(lay))) <= kCenterTolerance);
    CHECK(std::abs((lay.m_captionRect.left - rc.left) - (rc.right - lay.m_captionRect.right)) <= kCenterTolerance);
    CHECK(lay.m_glyphCenter.x == (rc.left + rc.right) / 2);
}

TEST_CASE("IPL2 160x120 的占位块：图标取短边的 30%，显示文字", "[imgph]")
{
    //短边 120 的 30% 是 36，落在上下限之间。
    const RECT rc = MakeRect(0, 0, 160, 120);
    const ImagePlaceholderLayout lay = ComputeImagePlaceholderLayout(rc, kTestCaptionW, kTestCaptionH);

    REQUIRE(lay.m_drawGlyph);
    CHECK(lay.m_glyphSize == 120 * kImagePlaceholderGlyphPercent / kPercentBase);
    CHECK(lay.m_showCaption);
}

TEST_CASE("IPL3 64x64 的小图：图标取下限，宽度放不下文字，只显示图标", "[imgph]")
{
    //短边 64 的 30% 是 19，低于下限，取 kImagePlaceholderGlyphMin。
    //文字宽 72 加两侧留白 16 需要 88，大于 64，所以不显示文字。
    const RECT rc = MakeRect(0, 0, 64, 64);
    const ImagePlaceholderLayout lay = ComputeImagePlaceholderLayout(rc, kTestCaptionW, kTestCaptionH);

    REQUIRE(lay.m_drawGlyph);
    CHECK(lay.m_glyphSize == kImagePlaceholderGlyphMin);
    CHECK_FALSE(lay.m_showCaption);
    CHECK(lay.m_glyphCenter.x == 32);
    CHECK(lay.m_glyphCenter.y == 32);
}

TEST_CASE("IPL4 220x50 又宽又矮：高度放不下文字，图标不超出占位块", "[imgph]")
{
    //图标高 16，加间距 8、文字 16、上下留白 16，共需 56，大于 50。
    const RECT rc = MakeRect(0, 0, 220, 50);
    const ImagePlaceholderLayout lay = ComputeImagePlaceholderLayout(rc, kTestCaptionW, kTestCaptionH);

    REQUIRE(lay.m_drawGlyph);
    CHECK_FALSE(lay.m_showCaption);
    CHECK(BlockTop(lay) >= rc.top);
    CHECK(BlockBottom(lay) <= rc.bottom);
}

TEST_CASE("IPL5 极小的图：图标缩到放得下为止，再小就不画", "[imgph]")
{
    //12x12：按下限 20 放不下，缩到宽度方向恰好放下 —— 12 减去两侧各 2 像素，得 8。
    const RECT smallRc = MakeRect(0, 0, 12, 12);
    const ImagePlaceholderLayout a = ComputeImagePlaceholderLayout(smallRc, 0, 0);
    REQUIRE(a.m_drawGlyph);
    CHECK(a.m_glyphSize == 12 - kImagePlaceholderTightMargin * 2);
    CHECK(a.m_glyphSize >= kImagePlaceholderGlyphAbsMin);
    CHECK(BlockTop(a) >= smallRc.top);
    CHECK(BlockBottom(a) <= smallRc.bottom);

    //8x8：缩到 4，低于绝对下限，什么都不画。
    const RECT tiny = MakeRect(0, 0, 8, 8);
    const ImagePlaceholderLayout b = ComputeImagePlaceholderLayout(tiny, kTestCaptionW, kTestCaptionH);
    CHECK_FALSE(b.m_drawGlyph);
    CHECK_FALSE(b.m_showCaption);

    //宽或高为 0 的矩形同样什么都不画。
    const ImagePlaceholderLayout c = ComputeImagePlaceholderLayout(MakeRect(0, 0, 100, 0), 0, 0);
    CHECK_FALSE(c.m_drawGlyph);
}

TEST_CASE("IPL6 文字比占位块还宽：不显示文字", "[imgph]")
{
    const RECT rc = MakeRect(0, 0, 100, 100);
    const int wideCaptionW = 120;
    const ImagePlaceholderLayout lay = ComputeImagePlaceholderLayout(rc, wideCaptionW, kTestCaptionH);

    REQUIRE(lay.m_drawGlyph);
    CHECK_FALSE(lay.m_showCaption);

    //没有文字（尺寸传 0）时同样只画图标。
    const ImagePlaceholderLayout none = ComputeImagePlaceholderLayout(rc, 0, 0);
    REQUIRE(none.m_drawGlyph);
    CHECK_FALSE(none.m_showCaption);
}

TEST_CASE("IPL7 占位块原点不在 0：结果与原点在 0 时相差同样的偏移", "[imgph]")
{
    const long dx = 100;
    const long dy = 200;
    const ImagePlaceholderLayout base =
        ComputeImagePlaceholderLayout(MakeRect(0, 0, 220, 165), kTestCaptionW, kTestCaptionH);
    const ImagePlaceholderLayout moved =
        ComputeImagePlaceholderLayout(MakeRect(dx, dy, dx + 220, dy + 165), kTestCaptionW, kTestCaptionH);

    CHECK(moved.m_glyphSize == base.m_glyphSize);
    CHECK(moved.m_glyphCenter.x == base.m_glyphCenter.x + dx);
    CHECK(moved.m_glyphCenter.y == base.m_glyphCenter.y + dy);
    CHECK(moved.m_captionRect.left == base.m_captionRect.left + dx);
    CHECK(moved.m_captionRect.top == base.m_captionRect.top + dy);
}

TEST_CASE("IPL8 量说明文字：有字时宽高为正、字越多越宽，空串为 0", "[imgph]")
{
    HDC dc = ::CreateCompatibleDC(NULL);
    REQUIRE(dc != NULL);

    const SIZE six  = MeasureImagePlaceholderCaption(dc, L"图片加载失败", kImagePlaceholderCaptionPx);
    const SIZE four = MeasureImagePlaceholderCaption(dc, L"加载失败", kImagePlaceholderCaptionPx);
    const SIZE none = MeasureImagePlaceholderCaption(dc, L"", kImagePlaceholderCaptionPx);
    ::DeleteDC(dc);

    INFO("六字=" << six.cx << "x" << six.cy << " 四字=" << four.cx << "x" << four.cy);
    CHECK(six.cx > 0);
    CHECK(six.cy > 0);
    CHECK(six.cx > four.cx);
    CHECK(none.cx == 0);
    CHECK(none.cy == 0);
}

// =====================================================================
// CIF1-CIF6  聊天气泡的失败判断：该显示"图片发送失败"、"图片加载失败"，还是不算失败
// =====================================================================

namespace {

// 判断时用到的两组布尔入参，起个名字让用例读起来更直白。
const bool kHasPath      = true;
const bool kNoPath       = false;
const bool kDecodeFailed = true;
const bool kDecodeOk     = false;

} // anonymous namespace

TEST_CASE("CIF1 上传失败：显示发送失败，与本地文件状况无关", "[imgph]")
{
    CHECK(ResolveChatImageFailure(kChatImageStateFailed, kHasPath, kDecodeOk) == kChatImageSendFailed);
    CHECK(ResolveChatImageFailure(kChatImageStateFailed, kNoPath, kDecodeFailed) == kChatImageSendFailed);
}

TEST_CASE("CIF2 下载失败：显示加载失败，包括自己在别的设备上发出的图片", "[imgph]")
{
    //判断只看状态，不看是谁发的：自己在手机上发的图到电脑上下载失败，也是"加载失败"。
    CHECK(ResolveChatImageFailure(kChatImageStateDownloadFailed, kNoPath, kDecodeOk) == kChatImageLoadFailed);
    CHECK(ResolveChatImageFailure(kChatImageStateDownloadFailed, kHasPath, kDecodeFailed) == kChatImageLoadFailed);
}

TEST_CASE("CIF3 正常态、有路径、但本地文件丢失或损坏：显示加载失败", "[imgph]")
{
    CHECK(ResolveChatImageFailure(kChatImageStateReady, kHasPath, kDecodeFailed) == kChatImageLoadFailed);
    //解码正常则不算失败。
    CHECK(ResolveChatImageFailure(kChatImageStateReady, kHasPath, kDecodeOk) == kChatImageNotFailed);
}

TEST_CASE("CIF4 正常态、没有路径（调试入口里的示例图）：不算失败", "[imgph]")
{
    CHECK(ResolveChatImageFailure(kChatImageStateReady, kNoPath, kDecodeOk) == kChatImageNotFailed);
    CHECK(ResolveChatImageFailure(kChatImageStateReady, kNoPath, kDecodeFailed) == kChatImageNotFailed);
}

TEST_CASE("CIF5 上传中、下载中：无论缓存怎么说都不算失败", "[imgph]")
{
    const int busy[] = { kChatImageStateUploading, kChatImageStateDownloading };
    for (std::size_t i = 0; i < sizeof(busy) / sizeof(busy[0]); ++i)
    {
        INFO("imageState=" << busy[i]);
        CHECK(ResolveChatImageFailure(busy[i], kHasPath, kDecodeFailed) == kChatImageNotFailed);
        CHECK(ResolveChatImageFailure(busy[i], kNoPath, kDecodeOk) == kChatImageNotFailed);
    }
}

TEST_CASE("CIF6 说明文字与颜色：发送失败用红字，加载失败用灰字，不失败没有文字", "[imgph]")
{
    CHECK(std::wstring(ChatImageFailureCaption(kChatImageSendFailed)) == L"图片发送失败");
    CHECK(std::wstring(ChatImageFailureCaption(kChatImageLoadFailed)) == L"图片加载失败");
    CHECK(std::wstring(ChatImageFailureCaption(kChatImageNotFailed)).empty());

    CHECK(ChatImageFailureCaptionColor(kChatImageSendFailed) == kImagePlaceholderCaptionRed);
    CHECK(ChatImageFailureCaptionColor(kChatImageLoadFailed) == kImagePlaceholderCaptionGray);
}

// =====================================================================
// HTS1-HTS6  消息记录窗图片缩略图的状态：真图 / 需要下载 / 下载中 / 破图，
//            以及能否单击下载、悬停提示
// =====================================================================

namespace {

// 判断时用到的几组布尔入参，起个名字让用例读起来更直白。
const bool kDownloadable    = true;
const bool kNotDownloadable = false;
const bool kFileExists      = true;
const bool kFileMissing     = false;
const bool kDownloading     = true;
const bool kNotDownloading  = false;
const bool kDownloadFailed  = true;
const bool kNeverFailed     = false;

std::wstring TipOf(const HistoryThumbDecision& d)
{
    return std::wstring(d.m_tooltip != NULL ? d.m_tooltip : L"");
}

} // anonymous namespace

TEST_CASE("HTS1 本地有原图、解码正常：显示缩略图，不可点击", "[imgph]")
{
    const HistoryThumbDecision d =
        ResolveHistoryThumb(kDownloadable, kFileExists, kNotDownloading, kNeverFailed, kDecodeOk);
    CHECK(d.m_state == kHistoryThumbImage);
    CHECK_FALSE(d.m_clickable);
    CHECK(TipOf(d).empty());
}

TEST_CASE("HTS2 本地没有原图、可以下载：需要下载，可单击，提示单击下载", "[imgph]")
{
    const HistoryThumbDecision d =
        ResolveHistoryThumb(kDownloadable, kFileMissing, kNotDownloading, kNeverFailed, kDecodeOk);
    CHECK(d.m_state == kHistoryThumbNeedDownload);
    CHECK(d.m_clickable);
    CHECK(TipOf(d) == L"单击下载图片");
}

TEST_CASE("HTS3 下载中：显示加载圆环，不可点击，没有提示", "[imgph]")
{
    //下载中优先于其余一切：哪怕本地已有写了一半的文件、之前还失败过。
    const HistoryThumbDecision a =
        ResolveHistoryThumb(kDownloadable, kFileMissing, kDownloading, kNeverFailed, kDecodeOk);
    const HistoryThumbDecision b =
        ResolveHistoryThumb(kDownloadable, kFileExists, kDownloading, kDownloadFailed, kDecodeFailed);
    CHECK(a.m_state == kHistoryThumbDownloading);
    CHECK(b.m_state == kHistoryThumbDownloading);
    CHECK_FALSE(a.m_clickable);
    CHECK(TipOf(a).empty());
}

TEST_CASE("HTS4 下载失败、仍可下载：破图，可单击重试，提示下载失败", "[imgph]")
{
    const HistoryThumbDecision d =
        ResolveHistoryThumb(kDownloadable, kFileMissing, kNotDownloading, kDownloadFailed, kDecodeOk);
    CHECK(d.m_state == kHistoryThumbBroken);
    CHECK(d.m_clickable);
    CHECK(TipOf(d) == L"下载失败，单击重试");
}

TEST_CASE("HTS5 本地没有原图、也缺少下载信息：破图，不可点击，没有提示", "[imgph]")
{
    const HistoryThumbDecision a =
        ResolveHistoryThumb(kNotDownloadable, kFileMissing, kNotDownloading, kNeverFailed, kDecodeOk);
    CHECK(a.m_state == kHistoryThumbBroken);
    CHECK_FALSE(a.m_clickable);
    CHECK(TipOf(a).empty());

    //缺少下载信息时即便记着失败过，也没有可重试的东西。
    const HistoryThumbDecision b =
        ResolveHistoryThumb(kNotDownloadable, kFileMissing, kNotDownloading, kDownloadFailed, kDecodeOk);
    CHECK(b.m_state == kHistoryThumbBroken);
    CHECK_FALSE(b.m_clickable);
    CHECK(TipOf(b).empty());
}

TEST_CASE("HTS6 本地有原图但解码失败（文件损坏）：破图，不可点击", "[imgph]")
{
    const HistoryThumbDecision d =
        ResolveHistoryThumb(kDownloadable, kFileExists, kNotDownloading, kNeverFailed, kDecodeFailed);
    CHECK(d.m_state == kHistoryThumbBroken);
    CHECK_FALSE(d.m_clickable);
    CHECK(TipOf(d).empty());
}
