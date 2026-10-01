/**
 *  ChatClipboardContentTests.cpp
 *
 *  聊天内容剪贴板模型（ChatClipboardContent）的测试：整条消息 / 选中一段 / 图片消息拆成
 *  "文字 / 表情 / 图片"片段，片段与剪贴板数据的序列化与解析（含各种非法数据），以及片段
 *  转纯文字时与以往复制结果保持一致。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不碰剪贴板与窗口。全局 _Module 由
 *  BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonChatWnd/ChatClipboardContent.h"
#include "../UI/BalloonChatWnd/ChatInlineText.h"      // BuildFlatText：与气泡选中文字同一套扁平串
#include "../UI/BalloonChatWnd/ChatTextSelection.h"   // ExtractSelectedText：以往复制出的纯文字

#include <string>
#include <vector>

using namespace balloonchatwnd;
using balloonchat::ChatSegment;

namespace {

// 测试里用到的表情编号。
const int kFaceSmile = 5;
const int kFaceFirst = 0;
const int kFaceLast  = 142;

// 取第 i 个片段，越界时让用例失败而不是崩溃。
const ChatClipSegment& At(const std::vector<ChatClipSegment>& segs, size_t i)
{
    REQUIRE(i < segs.size());
    return segs[i];
}

// "ab" + 表情 5 + "cd"：扁平串为 a b <占位> c d，共 5 个字符，表情位于下标 2。
std::vector<ChatSegment> MakeAbFaceCd()
{
    std::vector<ChatSegment> segs;
    segs.push_back(ChatSegment::Text(_T("ab")));
    segs.push_back(ChatSegment::Face(kFaceSmile));
    segs.push_back(ChatSegment::Text(_T("cd")));
    return segs;
}

ChatClipSegment MakeText(const wchar_t* text)
{
    ChatClipSegment s;
    s.m_kind = kChatClipText;
    s.m_text = text;
    return s;
}

ChatClipSegment MakeFace(int faceId)
{
    ChatClipSegment s;
    s.m_kind   = kChatClipFace;
    s.m_faceId = faceId;
    return s;
}

ChatClipSegment MakeImage(const wchar_t* path)
{
    ChatClipSegment s;
    s.m_kind      = kChatClipImage;
    s.m_imagePath = path;
    return s;
}

// 两个片段序列是否完全相同（类型、文字、表情编号、图片路径逐项比较）。
bool SameSegments(const std::vector<ChatClipSegment>& a, const std::vector<ChatClipSegment>& b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].m_kind != b[i].m_kind)
        {
            return false;
        }
        if (a[i].m_kind == kChatClipText && a[i].m_text != b[i].m_text)
        {
            return false;
        }
        if (a[i].m_kind == kChatClipFace && a[i].m_faceId != b[i].m_faceId)
        {
            return false;
        }
        if (a[i].m_kind == kChatClipImage && a[i].m_imagePath != b[i].m_imagePath)
        {
            return false;
        }
    }
    return true;
}

} // anonymous namespace

TEST_CASE("CCB1 整条文字消息：按原文顺序拆出文字、表情、文字，转义的表情标记还原成字面文字", "[chatclip]")
{
    //正文里只有紧跟表情标记的 "//" 才是转义（//f["3"] 是用户手打的字面文字 /f["3"]），
    //网址里的 "//" 原样保留。规则见 BalloonChatMsgFormat.cpp 的 ParseSendBody。
    const std::vector<ChatClipSegment> segs =
        BuildClipSegmentsFromBody(_T("你好/f[\"5\"]见 //f[\"3\"] 与 http://x"));
    REQUIRE(segs.size() == 3);
    CHECK(At(segs, 0).m_kind == kChatClipText);
    CHECK(At(segs, 0).m_text == L"你好");
    CHECK(At(segs, 1).m_kind == kChatClipFace);
    CHECK(At(segs, 1).m_faceId == kFaceSmile);
    CHECK(At(segs, 2).m_kind == kChatClipText);
    CHECK(At(segs, 2).m_text == L"见 /f[\"3\"] 与 http://x");

    //只有表情的消息、空消息。
    const std::vector<ChatClipSegment> onlyFace = BuildClipSegmentsFromBody(_T("/f[\"0\"]"));
    REQUIRE(onlyFace.size() == 1);
    CHECK(At(onlyFace, 0).m_kind == kChatClipFace);
    CHECK(At(onlyFace, 0).m_faceId == kFaceFirst);
    CHECK(BuildClipSegmentsFromBody(_T("")).empty());
}

TEST_CASE("CCB2 选中一段：只含选区里的文字与表情", "[chatclip]")
{
    const std::vector<ChatSegment> body = MakeAbFaceCd();

    //从 "b" 选到 "c"：跨过中间的表情。
    const std::vector<ChatClipSegment> mid = BuildClipSegmentsFromSelection(body, 1, 4);
    REQUIRE(mid.size() == 3);
    CHECK(At(mid, 0).m_text == L"b");
    CHECK(At(mid, 1).m_kind == kChatClipFace);
    CHECK(At(mid, 1).m_faceId == kFaceSmile);
    CHECK(At(mid, 2).m_text == L"c");

    //只选了表情之前的文字：没有表情。
    const std::vector<ChatClipSegment> head = BuildClipSegmentsFromSelection(body, 0, 2);
    REQUIRE(head.size() == 1);
    CHECK(At(head, 0).m_kind == kChatClipText);
    CHECK(At(head, 0).m_text == L"ab");
}

TEST_CASE("CCB3 选区只包含一个表情；越界截断；起点不小于终点视为没有选区", "[chatclip]")
{
    const std::vector<ChatSegment> body = MakeAbFaceCd();

    const std::vector<ChatClipSegment> face = BuildClipSegmentsFromSelection(body, 2, 3);
    REQUIRE(face.size() == 1);
    CHECK(At(face, 0).m_kind == kChatClipFace);
    CHECK(At(face, 0).m_faceId == kFaceSmile);

    //终点超出扁平串长度：按实际长度截断，拿到 "cd"。
    const std::vector<ChatClipSegment> tail = BuildClipSegmentsFromSelection(body, 3, 99);
    REQUIRE(tail.size() == 1);
    CHECK(At(tail, 0).m_text == L"cd");

    //起点等于或大于终点：没有选区。
    CHECK(BuildClipSegmentsFromSelection(body, 4, 4).empty());
    CHECK(BuildClipSegmentsFromSelection(body, 4, 2).empty());
}

TEST_CASE("CCB4 图片消息：有本地路径时是一个图片片段，路径为空时不生成片段", "[chatclip]")
{
    const std::vector<ChatClipSegment> img = BuildClipSegmentsForImage(_T("C:\\cache\\a.png"));
    REQUIRE(img.size() == 1);
    CHECK(At(img, 0).m_kind == kChatClipImage);
    CHECK(At(img, 0).m_imagePath == L"C:\\cache\\a.png");

    CHECK(BuildClipSegmentsForImage(_T("")).empty());
}

TEST_CASE("CCB5 序列化后再解析，得到完全相同的片段序列（含特殊字符）", "[chatclip]")
{
    std::vector<ChatClipSegment> segs;
    segs.push_back(MakeText(L"中文\r\n换行 \"引号\" \\反斜杠\\ /斜杠/ \t制表"));
    segs.push_back(MakeFace(kFaceFirst));
    segs.push_back(MakeFace(kFaceLast));
    segs.push_back(MakeImage(L"C:\\用户\\缓存 目录\\图片.png"));
    segs.push_back(MakeText(L"结尾"));

    const std::string data = SerializeClipSegments(segs);
    std::vector<ChatClipSegment> back;
    REQUIRE(ParseClipSegments(data, back));
    CHECK(SameSegments(segs, back));

    //空序列同样能往返。
    std::vector<ChatClipSegment> empty;
    std::vector<ChatClipSegment> emptyBack;
    REQUIRE(ParseClipSegments(SerializeClipSegments(empty), emptyBack));
    CHECK(emptyBack.empty());
}

TEST_CASE("CCB6 非法数据：结构不对整体失败，个别片段不对只丢弃该片段", "[chatclip]")
{
    std::vector<ChatClipSegment> out;
    out.push_back(MakeText(L"旧内容"));

    //结构不对：整体失败，出参被清空。
    CHECK_FALSE(ParseClipSegments("", out));
    CHECK(out.empty());
    CHECK_FALSE(ParseClipSegments("not json", out));
    CHECK_FALSE(ParseClipSegments("[1,2,3]", out));
    CHECK_FALSE(ParseClipSegments("{\"v\":2,\"segs\":[]}", out));   //版本号不认识
    CHECK_FALSE(ParseClipSegments("{\"v\":1}", out));                //没有片段列表

    //结构合法，但夹着几个非法片段：只保留合法的两个。
    const std::string mixed =
        "{\"v\":1,\"segs\":["
        "{\"t\":\"text\",\"s\":\"ok\"},"
        "{\"t\":\"face\",\"id\":-1},"
        "{\"t\":\"face\",\"id\":100000},"
        "{\"t\":\"image\",\"path\":\"\"},"
        "{\"t\":\"unknown\"},"
        "{\"t\":\"face\",\"id\":7}"
        "]}";
    REQUIRE(ParseClipSegments(mixed, out));
    REQUIRE(out.size() == 2);
    CHECK(At(out, 0).m_text == L"ok");
    CHECK(At(out, 1).m_faceId == 7);

    //片段数超过上限：只取前 kChatClipMaxSegments 个。
    std::vector<ChatClipSegment> many;
    for (size_t i = 0; i < kChatClipMaxSegments + 1; ++i)
    {
        many.push_back(MakeFace(kFaceSmile));
    }
    REQUIRE(ParseClipSegments(SerializeClipSegments(many), out));
    CHECK(out.size() == kChatClipMaxSegments);
}

TEST_CASE("CCB7 转纯文字：表情写成[表情]、图片写成[图片]，与以往复制结果一致", "[chatclip]")
{
    std::vector<ChatClipSegment> segs;
    segs.push_back(MakeText(L"ab"));
    segs.push_back(MakeFace(kFaceSmile));
    segs.push_back(MakeText(L"cd"));
    segs.push_back(MakeImage(L"C:\\a.png"));
    CHECK(ClipSegmentsToPlainText(segs) == L"ab[表情]cd[图片]");

    //选中一段后转出的纯文字，与气泡以往复制选区得到的文字完全相同。
    const std::vector<ChatSegment> body = MakeAbFaceCd();
    const std::wstring flat = BuildFlatText(body);
    for (int s = 0; s <= (int)flat.size(); ++s)
    {
        for (int e = s; e <= (int)flat.size(); ++e)
        {
            INFO("选区 [" << s << ", " << e << ")");
            CHECK(ClipSegmentsToPlainText(BuildClipSegmentsFromSelection(body, s, e))
                  == ExtractSelectedText(flat, s, e, L"[表情]"));
        }
    }
    CHECK(ClipSegmentsToPlainText(std::vector<ChatClipSegment>()).empty());
}
