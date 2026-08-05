/**
 *  BalloonChatMsgFormatTests.cpp
 *
 *  P1b 测试：验证 BalloonChatMsgFormat 的三个纯函数 —— 发送校验
 *  ValidateOutgoing、协议标签剥离 StripControlTags、显示行格式化
 *  FormatDisplayLine。
 *
 *  覆盖用例（与需求确认一致）：
 *    D1-D3  ValidateOutgoing
 *    D4-D6  StripControlTags
 *    D7-D8  FormatDisplayLine
 *
 *  balloonwj@qq.com   2026.05.21
 */

#include "stdafx.h"
#include <tchar.h>

#include "catch_amalgamated.hpp"

#include "../BalloonChat/BalloonChatMsgFormat.h"

using namespace balloonchat;

namespace {

// CString → std::wstring，便于 Catch2 在断言失败时打印可读字符串。
std::wstring WS(const CString& s)
{
    return std::wstring(s.GetString());
}

// 判断 s 是否形如 "YYYY-MM-DD HH:MM:SS"（19 字符，分隔符就位、其余是数字）。
// 与时区无关 —— 只校验"形状"，不校验具体年月日。
bool LooksLikeTimestamp(const CString& s)
{
    if (s.GetLength() != 19)
    {
        return false;
    }
    for (int i = 0; i < 19; ++i)
    {
        const TCHAR c = s[i];
        if (i == 4 || i == 7)
        {
            if (c != _T('-')) { return false; }
        }
        else if (i == 10)
        {
            if (c != _T(' ')) { return false; }
        }
        else if (i == 13 || i == 16)
        {
            if (c != _T(':')) { return false; }
        }
        else
        {
            if (!_istdigit((_TUCHAR)c)) { return false; }
        }
    }
    return true;
}

} // anonymous namespace

// =====================================================================
// D. BalloonChatMsgFormat
// =====================================================================

TEST_CASE("D1 ValidateOutgoing 空串判为 Empty", "[p1b][msgfmt]")
{
    CHECK(ValidateOutgoing(CString(_T(""))) == kSendEmpty);
    // 纯空白也应判空。
    CHECK(ValidateOutgoing(CString(_T("   \r\n\t"))) == kSendEmpty);
}

TEST_CASE("D2 ValidateOutgoing 正常文本判为 Ok", "[p1b][msgfmt]")
{
    CHECK(ValidateOutgoing(CString(_T("hi"))) == kSendOk);
    CHECK(ValidateOutgoing(CString(_T("你好，世界"))) == kSendOk);
}

TEST_CASE("D3 ValidateOutgoing 超长文本判为 TooLong", "[p1b][msgfmt]")
{
    // 恰好上限 → Ok；上限 +1 → TooLong。
    CString atLimit(_T('a'), kMaxOutgoingChars);
    CString overLimit(_T('a'), kMaxOutgoingChars + 1);
    CHECK(ValidateOutgoing(atLimit)   == kSendOk);
    CHECK(ValidateOutgoing(overLimit) == kSendTooLong);
}

TEST_CASE("D4 StripControlTags 去掉尾部字体标签", "[p1b][msgfmt]")
{
    CString raw = _T("hello/o[\"\x5b8b\x4f53,9,FF0000,0,0,0\"]");
    CHECK(WS(StripControlTags(raw)) == L"hello");
}

TEST_CASE("D5 StripControlTags 无标签纯文本原样返回", "[p1b][msgfmt]")
{
    CHECK(WS(StripControlTags(CString(_T("plain text")))) == L"plain text");
    // 残缺标签（缺右括号）按普通字符保留，不吞正文。
    CHECK(WS(StripControlTags(CString(_T("a/o[broken")))) == L"a/o[broken");
}

TEST_CASE("D6 StripControlTags 去掉多个标签", "[p1b][msgfmt]")
{
    CString raw = _T("a/o[\"x\"]b/r[\"1\"]c/s[\"1\"]");
    CHECK(WS(StripControlTags(raw)) == L"abc");
}

TEST_CASE("D7 FormatDisplayLine 含发送者与正文", "[p1b][msgfmt]")
{
    CString line = FormatDisplayLine(CString(_T("alice")),
                                     time(NULL),
                                     CString(_T("hello world")));
    CHECK(line.Find(_T("alice"))       >= 0);
    CHECK(line.Find(_T("hello world")) >= 0);
}

TEST_CASE("D8 FormatDisplayLine 时间戳形如 YYYY-MM-DD HH:MM:SS", "[p1b][msgfmt]")
{
    // 发送者长度已知（"u"=1 字符）+ 2 个空格 → 时间戳从第 3 个字符起、占 19 字符。
    CString line = FormatDisplayLine(CString(_T("u")),
                                     time(NULL),
                                     CString(_T("m")));
    CString ts = line.Mid(3, 19);
    // 时间戳是纯 ASCII，转窄串喂给 Catch2 的 INFO 流（其 << 是 narrow 流，
    // 不接受 std::wstring）。
    INFO("timestamp substring = " << CStringA(ts).GetString());
    CHECK(LooksLikeTimestamp(ts));
}

// =====================================================================
// F. BuildSendBody —— 表情/文本段序列编码成发送正文（接 Bin/Face 表情）
// =====================================================================

TEST_CASE("F1 BuildSendBody 纯文本无表情原样输出", "[face][msgfmt]")
{
    std::vector<ChatSegment> segs;
    segs.push_back(ChatSegment::Text(_T("hello")));
    CHECK(WS(BuildSendBody(segs)) == L"hello");
}

TEST_CASE("F2 BuildSendBody 文本里的斜杠转义成双斜杠", "[face][msgfmt]")
{
    std::vector<ChatSegment> segs1;
    segs1.push_back(ChatSegment::Text(_T("a/b")));
    CHECK(WS(BuildSendBody(segs1)) == L"a//b");

    // URL 里的双斜杠也逐个转义，发送管线 // → / 可无损还原。
    std::vector<ChatSegment> segs2;
    segs2.push_back(ChatSegment::Text(_T("http://x")));
    CHECK(WS(BuildSendBody(segs2)) == L"http:////x");
}

TEST_CASE("F3 BuildSendBody 单个表情编码成 /f[\"id\"]", "[face][msgfmt]")
{
    std::vector<ChatSegment> segs;
    segs.push_back(ChatSegment::Face(12));
    CHECK(WS(BuildSendBody(segs)) == L"/f[\"12\"]");
}

TEST_CASE("F4 BuildSendBody 图文穿插按序拼接", "[face][msgfmt]")
{
    std::vector<ChatSegment> segs;
    segs.push_back(ChatSegment::Text(_T("\x4f60\x597d")));   // 你好
    segs.push_back(ChatSegment::Face(3));
    segs.push_back(ChatSegment::Text(_T("\x4e16\x754c")));   // 世界
    segs.push_back(ChatSegment::Face(5));
    CHECK(WS(BuildSendBody(segs)) == L"\x4f60\x597d/f[\"3\"]\x4e16\x754c/f[\"5\"]");
}

TEST_CASE("F5 BuildSendBody 空段序列返回空串", "[face][msgfmt]")
{
    std::vector<ChatSegment> segs;
    CHECK(WS(BuildSendBody(segs)) == L"");
}

TEST_CASE("F6 BuildSendBody 用户手打的 /f 被转义不会冒充表情", "[face][msgfmt]")
{
    // 用户在文本里真的打了 "/f"，转义成 "//f" 后发送管线不会把它解析成表情；
    // 紧随其后的真正表情段才输出 /f["0"]。
    std::vector<ChatSegment> segs;
    segs.push_back(ChatSegment::Text(_T("see /f fake")));
    segs.push_back(ChatSegment::Face(0));
    CHECK(WS(BuildSendBody(segs)) == L"see //f fake/f[\"0\"]");
}

// ---- ParseSendBody —— BuildSendBody 的逆（接收 / 回显解析内联表情）----

TEST_CASE("G1 ParseSendBody 纯文本一段", "[face][msgfmt]")
{
    std::vector<ChatSegment> segs = ParseSendBody(CString(_T("hello")));
    REQUIRE(segs.size() == 1);
    CHECK(segs[0].m_bFace == false);
    CHECK(WS(segs[0].m_strText) == L"hello");
}

TEST_CASE("G2 ParseSendBody 单个表情", "[face][msgfmt]")
{
    std::vector<ChatSegment> segs = ParseSendBody(CString(_T("/f[\"12\"]")));
    REQUIRE(segs.size() == 1);
    CHECK(segs[0].m_bFace == true);
    CHECK(segs[0].m_nFaceId == 12);
}

TEST_CASE("G3 ParseSendBody 图文穿插", "[face][msgfmt]")
{
    std::vector<ChatSegment> segs =
        ParseSendBody(CString(_T("\x4f60\x597d/f[\"3\"]\x4e16\x754c/f[\"5\"]")));
    REQUIRE(segs.size() == 4);
    CHECK(segs[0].m_bFace == false);
    CHECK(WS(segs[0].m_strText) == L"\x4f60\x597d");
    CHECK(segs[1].m_bFace == true);
    CHECK(segs[1].m_nFaceId == 3);
    CHECK(segs[2].m_bFace == false);
    CHECK(WS(segs[2].m_strText) == L"\x4e16\x754c");
    CHECK(segs[3].m_bFace == true);
    CHECK(segs[3].m_nFaceId == 5);
}

TEST_CASE("G4 ParseSendBody 双斜杠还原成单斜杠", "[face][msgfmt]")
{
    std::vector<ChatSegment> segs = ParseSendBody(CString(_T("a//b")));
    REQUIRE(segs.size() == 1);
    CHECK(WS(segs[0].m_strText) == L"a/b");
}

TEST_CASE("G5 ParseSendBody 残缺表情标签当普通文本", "[face][msgfmt]")
{
    // 缺右半的 /f[" 不应被当成表情，也不应吞掉后文。
    std::vector<ChatSegment> segs = ParseSendBody(CString(_T("/f[\"oops")));
    REQUIRE(segs.size() == 1);
    CHECK(segs[0].m_bFace == false);
    CHECK(WS(segs[0].m_strText) == L"/f[\"oops");
}

TEST_CASE("G6 ParseSendBody 与 BuildSendBody 往返一致", "[face][msgfmt]")
{
    std::vector<ChatSegment> in;
    in.push_back(ChatSegment::Text(_T("path a/b ")));   // 含字面斜杠
    in.push_back(ChatSegment::Face(7));
    in.push_back(ChatSegment::Text(_T(" end")));
    std::vector<ChatSegment> out = ParseSendBody(BuildSendBody(in));
    REQUIRE(out.size() == in.size());
    for (size_t i = 0; i < in.size(); ++i)
    {
        CHECK(out[i].m_bFace == in[i].m_bFace);
        if (in[i].m_bFace)
        {
            CHECK(out[i].m_nFaceId == in[i].m_nFaceId);
        }
        else
        {
            CHECK(WS(out[i].m_strText) == WS(in[i].m_strText));
        }
    }
}

TEST_CASE("G7 BodyToDigest 表情替换成占位、双斜杠还原", "[face][msgfmt]")
{
    CHECK(WS(BodyToDigest(CString(_T("hi/f[\"3\"]a//b")))) == L"hi[\x8868\x60c5]a/b");
}

// =====================================================================
// RCT. 撤回提示文案 BuildRecallTipText —— 聊天窗提示行与会话列表预览共用
// =====================================================================
// 中文断言值一律用 \x 转义写出，与本文件既有写法一致，避免测试源码的编码
// 影响断言结果。对照表：
//   你撤回了一条消息 = \x4f60\x64a4\x56de\x4e86\x4e00\x6761\x6d88\x606f
//   对方撤回了一条消息 = \x5bf9\x65b9 + 撤回了一条消息
//   张三 = \x5f20\x4e09

TEST_CASE("RCT1 本人撤回显示\"你撤回了一条消息\"", "[recall][msgfmt]")
{
    //本人撤回时忽略 operatorName，传什么都应是同一句。
    CHECK(WS(BuildRecallTipText(true, CString())) ==
          L"\x4f60\x64a4\x56de\x4e86\x4e00\x6761\x6d88\x606f");
    CHECK(WS(BuildRecallTipText(true, CString(_T("\x5f20\x4e09")))) ==
          L"\x4f60\x64a4\x56de\x4e86\x4e00\x6761\x6d88\x606f");
}

TEST_CASE("RCT2 他人撤回且有昵称时显示\"<昵称>撤回了一条消息\"", "[recall][msgfmt]")
{
    CHECK(WS(BuildRecallTipText(false, CString(_T("\x5f20\x4e09")))) ==
          L"\x5f20\x4e09\x64a4\x56de\x4e86\x4e00\x6761\x6d88\x606f");
}

TEST_CASE("RCT3 他人撤回但昵称为空时退化成\"对方撤回了一条消息\"", "[recall][msgfmt]")
{
    //联系人缓存还没拉到昵称时不能拼出缺主语的句子。
    CHECK(WS(BuildRecallTipText(false, CString())) ==
          L"\x5bf9\x65b9\x64a4\x56de\x4e86\x4e00\x6761\x6d88\x606f");
    CHECK(WS(BuildRecallTipText(false, CString(_T("")))) ==
          L"\x5bf9\x65b9\x64a4\x56de\x4e86\x4e00\x6761\x6d88\x606f");
}

TEST_CASE("RCT4 三种文案两两不同，UI 上可区分是谁撤的", "[recall][msgfmt]")
{
    CString self  = BuildRecallTipText(true,  CString());
    CString named = BuildRecallTipText(false, CString(_T("\x5f20\x4e09")));
    CString anon  = BuildRecallTipText(false, CString());

    CHECK(WS(self)  != WS(named));
    CHECK(WS(self)  != WS(anon));
    CHECK(WS(named) != WS(anon));

    //三句都非空，不会在界面上留下空白提示行。
    CHECK_FALSE(self.IsEmpty());
    CHECK_FALSE(named.IsEmpty());
    CHECK_FALSE(anon.IsEmpty());
}
