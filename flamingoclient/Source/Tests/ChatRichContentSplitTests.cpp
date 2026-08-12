/**
 *  ChatRichContentSplitTests.cpp
 *
 *  第一期 b 测试：覆盖富文本输入框内容的有序拆分纯逻辑 SplitRichInputToParts
 *  —— 把图文混排（内嵌图在纯文本里是 WCH_EMBEDDING 0xFFFC）按文档顺序拆成
 *  文字部分 / 图片部分序列。对应方案用例 P6。从 OLE 抽位图、逐段发送等运行期
 *  逻辑不在本文件覆盖范围内。
 *
 *  用 Catch2 v3（amalgamated）。全局 _Module 由 BalloonChatLayoutTests.cpp
 *  统一定义，本文件只引用。内嵌占位符用 ￼（UCN，不依赖文件字节编码）。
 *
 *  balloonwj@qq.com   2026-06-04
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../BalloonChatWnd/ChatRichContentSplit.h"

using namespace balloonchatwnd;

// =====================================================================
// P6. 富文本输入框内容有序拆分
// =====================================================================

TEST_CASE("P6 纯文字 → 一个文字部分", "[richsplit]")
{
    std::vector<RichOutgoingPart> parts = SplitRichInputToParts(L"你好");
    REQUIRE(parts.size() == 1u);
    CHECK(parts[0].kind == kRichPartText);
    CHECK(parts[0].text == CString(_T("你好")));
}

TEST_CASE("P6 单个内嵌图 → 一个图片部分 index0", "[richsplit]")
{
    std::vector<RichOutgoingPart> parts = SplitRichInputToParts(L"￼");
    REQUIRE(parts.size() == 1u);
    CHECK(parts[0].kind == kRichPartImage);
    CHECK(parts[0].imageIndex == 0);
}

TEST_CASE("P6 文字+图+文字 → 按序三部分", "[richsplit]")
{
    std::vector<RichOutgoingPart> parts = SplitRichInputToParts(L"看图￼好的");
    REQUIRE(parts.size() == 3u);
    CHECK(parts[0].kind == kRichPartText);
    CHECK(parts[0].text == CString(_T("看图")));
    CHECK(parts[1].kind == kRichPartImage);
    CHECK(parts[1].imageIndex == 0);
    CHECK(parts[2].kind == kRichPartText);
    CHECK(parts[2].text == CString(_T("好的")));
}

TEST_CASE("P6 两连图 → 图0 图1", "[richsplit]")
{
    std::vector<RichOutgoingPart> parts = SplitRichInputToParts(L"￼￼");
    REQUIRE(parts.size() == 2u);
    CHECK(parts[0].kind == kRichPartImage);
    CHECK(parts[0].imageIndex == 0);
    CHECK(parts[1].kind == kRichPartImage);
    CHECK(parts[1].imageIndex == 1);
}

TEST_CASE("P6 图周围只有空白 → 丢弃空白文字、仅留图", "[richsplit]")
{
    std::vector<RichOutgoingPart> parts = SplitRichInputToParts(L"  ￼  ");
    REQUIRE(parts.size() == 1u);
    CHECK(parts[0].kind == kRichPartImage);
    CHECK(parts[0].imageIndex == 0);
}

TEST_CASE("P6 空串 / 全空白 / NULL → 不发", "[richsplit]")
{
    CHECK(SplitRichInputToParts(L"").empty());
    CHECK(SplitRichInputToParts(L"   \r\n  ").empty());
    CHECK(SplitRichInputToParts(NULL).empty());
}

TEST_CASE("P6 多图文混排 imageIndex 连续递增、文字段保真", "[richsplit]")
{
    std::vector<RichOutgoingPart> parts = SplitRichInputToParts(L"图1￼图2￼图3");
    REQUIRE(parts.size() == 5u);
    CHECK(parts[0].kind == kRichPartText);
    CHECK(parts[0].text == CString(_T("图1")));
    CHECK(parts[1].kind == kRichPartImage);
    CHECK(parts[1].imageIndex == 0);
    CHECK(parts[2].kind == kRichPartText);
    CHECK(parts[2].text == CString(_T("图2")));
    CHECK(parts[3].kind == kRichPartImage);
    CHECK(parts[3].imageIndex == 1);
    CHECK(parts[4].kind == kRichPartText);
    CHECK(parts[4].text == CString(_T("图3")));
}

// =====================================================================
// P7. EnumContent 原始段 → 待发送部分聚合（表情随文字、截图独立成段）
// =====================================================================

#include "../BalloonChatWnd/ChatFaceTag.h"

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
