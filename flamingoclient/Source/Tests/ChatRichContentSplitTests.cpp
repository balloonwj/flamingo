/**
 *  ChatRichContentSplitTests.cpp
 *
 *  富文本输入框内容有序拆分纯逻辑 BuildOutgoingParts 的测试：把 EnumContent 按文档顺序
 *  枚举出的原始段（文本 / 表情 / 截图等内嵌图）聚合成文字部分 / 图片部分序列。
 *    P7  表情随文字聚合与正文编码、截图打断文字组；
 *    FTG / ROP  图片标记的编码，以及带标记的图片按标记取源文件下标；
 *    RS1～RS6  基础拆分：纯文字、单图、图文交替、连图、图旁空白、多段混排。原先这几个
 *              场景测的是已经删除的 SplitRichInputToParts（用例 P6），生产代码早已不走它，
 *              现改为针对正式发送路径（bugs.md 的 BUG-6）。
 *  从 OLE 抽位图、逐段发送等运行期逻辑不在本文件覆盖范围内。
 *
 *  用 Catch2 v3（amalgamated）。全局 _Module 由 BalloonChatLayoutTests.cpp
 *  统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-06-04
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonChatWnd/ChatRichContentSplit.h"

using namespace balloonchatwnd;

// =====================================================================
// P7. EnumContent 原始段 → 待发送部分聚合（表情随文字、截图独立成段）
// =====================================================================

#include "../UI/BalloonChatWnd/ChatFaceTag.h"

namespace {

// 便捷构造：文本段。
RichInputSegment SegText(LPCTSTR s)
{
    RichInputSegment seg;
    seg.isImage = false;
    seg.text = s;
    seg.tag = 0;
    return seg;
}

// 便捷构造：表情段（faceId 经 ChatFaceTag 编码）。
RichInputSegment SegFace(int faceId)
{
    RichInputSegment seg;
    seg.isImage = true;
    seg.tag = FaceTagFromId(faceId);
    return seg;
}

// 便捷构造：截图等非表情内嵌图段（tag==0）。
RichInputSegment SegShot()
{
    RichInputSegment seg;
    seg.isImage = true;
    seg.tag = 0;
    return seg;
}

} // anonymous namespace

TEST_CASE("P7 文字+表情聚合成一个编码文本部分", "[richsplit][face]")
{
    std::vector<RichInputSegment> in;
    in.push_back(SegText(_T("你好")));
    in.push_back(SegFace(3));
    std::vector<RichOutgoingPart> parts = BuildOutgoingParts(in);
    REQUIRE(parts.size() == 1u);
    CHECK(parts[0].kind == kRichPartText);
    CHECK(parts[0].text == CString(_T("你好/f[\"3\"]")));
}

TEST_CASE("P7 纯文本组的斜杠原样上线（互通保真）", "[richsplit][face]")
{
    std::vector<RichInputSegment> in;
    in.push_back(SegText(_T("http://a.com")));
    std::vector<RichOutgoingPart> parts = BuildOutgoingParts(in);
    REQUIRE(parts.size() == 1u);
    CHECK(parts[0].text == CString(_T("http://a.com")));
}

TEST_CASE("P7 含表情的组里普通斜杠同样原样保留", "[richsplit][face]")
{
    //早先这里会把 a/b 转义成 a//b —— 只因为同一条消息里有表情。于是"消息里有没有
    //表情"决定了斜杠要不要翻倍，同一段文字两种写法，接收侧再一律还原，网址就被
    //吃掉一个斜杠。现在转义只认"会被误判成表情标记"的那个斜杠。
    std::vector<RichInputSegment> in;
    in.push_back(SegText(_T("a/b")));
    in.push_back(SegFace(0));
    std::vector<RichOutgoingPart> parts = BuildOutgoingParts(in);
    REQUIRE(parts.size() == 1u);
    CHECK(parts[0].text == CString(_T("a/b/f[\"0\"]")));
}

TEST_CASE("P7 手打 /f 标签的纯文本仍被转义防误判", "[richsplit][face]")
{
    std::vector<RichInputSegment> in;
    in.push_back(SegText(_T("literal /f[\"3\"] here")));
    std::vector<RichOutgoingPart> parts = BuildOutgoingParts(in);
    REQUIRE(parts.size() == 1u);
    CHECK(parts[0].text == CString(_T("literal //f[\"3\"] here")));
}

TEST_CASE("P7 截图打断富文本组、imageIndex 只数非表情图", "[richsplit][face]")
{
    std::vector<RichInputSegment> in;
    in.push_back(SegText(_T("前")));
    in.push_back(SegFace(1));
    in.push_back(SegShot());        // 截图0
    in.push_back(SegFace(2));
    in.push_back(SegText(_T("后")));
    in.push_back(SegShot());        // 截图1
    std::vector<RichOutgoingPart> parts = BuildOutgoingParts(in);
    REQUIRE(parts.size() == 4u);
    CHECK(parts[0].kind == kRichPartText);
    CHECK(parts[0].text == CString(_T("前/f[\"1\"]")));
    CHECK(parts[1].kind == kRichPartImage);
    CHECK(parts[1].imageIndex == 0);
    CHECK(parts[2].kind == kRichPartText);
    CHECK(parts[2].text == CString(_T("/f[\"2\"]后")));
    CHECK(parts[3].kind == kRichPartImage);
    CHECK(parts[3].imageIndex == 1);
}

TEST_CASE("P7 只有表情也成一条文本部分；全空白无表情则丢弃", "[richsplit][face]")
{
    std::vector<RichInputSegment> onlyFace;
    onlyFace.push_back(SegFace(7));
    std::vector<RichOutgoingPart> parts = BuildOutgoingParts(onlyFace);
    REQUIRE(parts.size() == 1u);
    CHECK(parts[0].text == CString(_T("/f[\"7\"]")));

    std::vector<RichInputSegment> blank;
    blank.push_back(SegText(_T("   \r\n")));
    CHECK(BuildOutgoingParts(blank).empty());

    CHECK(BuildOutgoingParts(std::vector<RichInputSegment>()).empty());
}

// =====================================================================
// FTG1 / ROP1 / ROP2  带标记的内联图片（2026-09-30）
//
// 截图、粘贴或拖进输入框的图片都带一个图片标记，标记里记着该图源文件在聊天窗
// m_pendingShotPaths 里的下标。发送时按标记取源文件，而不是按"第几张图"去对应 ——
// 粘贴时光标可以在任何位置，文档里的先后与插入的先后未必一致。
// =====================================================================

namespace {

// 便捷构造：带图片标记的内嵌图段，index 为源文件下标。
RichInputSegment SegTaggedImage(int index)
{
    RichInputSegment seg;
    seg.isImage = true;
    seg.tag = ImageTagFromIndex(index);
    return seg;
}

// 测试用的源文件下标。
const int kFirstInserted  = 0;
const int kSecondInserted = 1;

// 测试用的表情编号：取表情编号的两端。
const int kFaceIdLow  = 0;
const int kFaceIdHigh = 142;

} // anonymous namespace

TEST_CASE("FTG1 图片标记与表情标记互不重叠，序号编码后能解码还原", "[richsplit][imgtag]")
{
    const int samples[] = { 0, 1, 7, 999 };
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i)
    {
        const DWORD_PTR tag = ImageTagFromIndex(samples[i]);
        INFO("index=" << samples[i]);
        CHECK(IsImageTag(tag));
        CHECK_FALSE(IsFaceTag(tag));
        CHECK(ImageIndexFromTag(tag) == samples[i]);
    }
    //表情标记不会被当成图片标记；未标记的 0 两者都不是。
    CHECK_FALSE(IsImageTag(FaceTagFromId(kFaceIdLow)));
    CHECK_FALSE(IsImageTag(FaceTagFromId(kFaceIdHigh)));
    CHECK_FALSE(IsImageTag(0));
    CHECK_FALSE(IsFaceTag(0));
}

TEST_CASE("ROP1 带标记的图片按标记取源文件，与在文档里的先后无关", "[richsplit][imgtag]")
{
    //先插入的图（下标 0）在文档里排在后面：光标移到它前面又粘贴了一张（下标 1）。
    std::vector<RichInputSegment> segs;
    segs.push_back(SegText(_T("看这两张")));
    segs.push_back(SegTaggedImage(kSecondInserted));
    segs.push_back(SegTaggedImage(kFirstInserted));

    const std::vector<RichOutgoingPart> parts = BuildOutgoingParts(segs);
    REQUIRE(parts.size() == 3);
    CHECK(parts[0].kind == kRichPartText);
    CHECK(parts[1].kind == kRichPartImage);
    CHECK(parts[1].imageIndex == kSecondInserted);
    CHECK(parts[2].kind == kRichPartImage);
    CHECK(parts[2].imageIndex == kFirstInserted);
}

TEST_CASE("ROP2 表情与带标记图片混排：表情仍并入文字部分，图片单独成段", "[richsplit][imgtag]")
{
    std::vector<RichInputSegment> segs;
    segs.push_back(SegText(_T("a")));
    segs.push_back(SegFace(kFaceIdLow));
    segs.push_back(SegTaggedImage(kFirstInserted));
    segs.push_back(SegFace(kFaceIdHigh));
    segs.push_back(SegText(_T("b")));

    const std::vector<RichOutgoingPart> parts = BuildOutgoingParts(segs);
    REQUIRE(parts.size() == 3);
    CHECK(parts[0].kind == kRichPartText);
    CHECK(parts[0].text.Find(_T("/f[\"0\"]")) >= 0);
    CHECK(parts[1].kind == kRichPartImage);
    CHECK(parts[1].imageIndex == kFirstInserted);
    CHECK(parts[2].kind == kRichPartText);
    CHECK(parts[2].text.Find(_T("/f[\"142\"]")) >= 0);
}

// =====================================================================
// RS. BuildOutgoingParts 的基础拆分（原 P6 针对已删除的 SplitRichInputToParts，
//     这里把同样的场景改为针对正式发送路径，见 bugs.md 的 BUG-6）
// =====================================================================

TEST_CASE("RS1 纯文字 → 一个文字部分，原文不变", "[richsplit]")
{
    std::vector<RichInputSegment> segs;
    segs.push_back(SegText(_T("你好")));
    const std::vector<RichOutgoingPart> parts = BuildOutgoingParts(segs);
    REQUIRE(parts.size() == 1u);
    CHECK(parts[0].kind == kRichPartText);
    CHECK(parts[0].text == CString(_T("你好")));
}

TEST_CASE("RS2 一张未带标记的图 → 一个图片部分，下标 0", "[richsplit]")
{
    std::vector<RichInputSegment> segs;
    segs.push_back(SegShot());
    const std::vector<RichOutgoingPart> parts = BuildOutgoingParts(segs);
    REQUIRE(parts.size() == 1u);
    CHECK(parts[0].kind == kRichPartImage);
    CHECK(parts[0].imageIndex == 0);
}

TEST_CASE("RS3 文字、图、文字 → 按顺序三个部分", "[richsplit]")
{
    std::vector<RichInputSegment> segs;
    segs.push_back(SegText(_T("看图")));
    segs.push_back(SegShot());
    segs.push_back(SegText(_T("好的")));
    const std::vector<RichOutgoingPart> parts = BuildOutgoingParts(segs);
    REQUIRE(parts.size() == 3u);
    CHECK(parts[0].kind == kRichPartText);
    CHECK(parts[0].text == CString(_T("看图")));
    CHECK(parts[1].kind == kRichPartImage);
    CHECK(parts[1].imageIndex == 0);
    CHECK(parts[2].kind == kRichPartText);
    CHECK(parts[2].text == CString(_T("好的")));
}

TEST_CASE("RS4 两张图相连 → 两个图片部分，下标 0、1", "[richsplit]")
{
    std::vector<RichInputSegment> segs;
    segs.push_back(SegShot());
    segs.push_back(SegShot());
    const std::vector<RichOutgoingPart> parts = BuildOutgoingParts(segs);
    REQUIRE(parts.size() == 2u);
    CHECK(parts[0].kind == kRichPartImage);
    CHECK(parts[0].imageIndex == 0);
    CHECK(parts[1].kind == kRichPartImage);
    CHECK(parts[1].imageIndex == 1);
}

TEST_CASE("RS5 图两侧只有空白 → 丢弃空白，只剩图片部分", "[richsplit]")
{
    std::vector<RichInputSegment> segs;
    segs.push_back(SegText(_T("  ")));
    segs.push_back(SegShot());
    segs.push_back(SegText(_T(" \r\n ")));
    const std::vector<RichOutgoingPart> parts = BuildOutgoingParts(segs);
    REQUIRE(parts.size() == 1u);
    CHECK(parts[0].kind == kRichPartImage);
    CHECK(parts[0].imageIndex == 0);
}

TEST_CASE("RS6 多段图文混排：图片下标连续递增，文字（含换行）原样保留", "[richsplit]")
{
    std::vector<RichInputSegment> segs;
    segs.push_back(SegText(_T("图1")));
    segs.push_back(SegShot());
    segs.push_back(SegText(_T("图2\r\n第二行")));
    segs.push_back(SegShot());
    segs.push_back(SegText(_T("图3")));
    const std::vector<RichOutgoingPart> parts = BuildOutgoingParts(segs);
    REQUIRE(parts.size() == 5u);
    CHECK(parts[0].kind == kRichPartText);
    CHECK(parts[0].text == CString(_T("图1")));
    CHECK(parts[1].kind == kRichPartImage);
    CHECK(parts[1].imageIndex == 0);
    CHECK(parts[2].kind == kRichPartText);
    CHECK(parts[2].text == CString(_T("图2\r\n第二行")));
    CHECK(parts[3].kind == kRichPartImage);
    CHECK(parts[3].imageIndex == 1);
    CHECK(parts[4].kind == kRichPartText);
    CHECK(parts[4].text == CString(_T("图3")));
}
