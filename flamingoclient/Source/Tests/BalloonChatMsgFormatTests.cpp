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

#include "../UI/BalloonChat/BalloonChatMsgFormat.h"
#include "../UI/BalloonChatWnd/ChatSystemTip.h"   // RCT5~RCT8：历史里已撤回的消息构造成提示行、取撤回者
#include "../net/Protocol.h"                      // message_type_*

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

TEST_CASE("F2 BuildSendBody 只给会被误判成表情标记的斜杠加转义", "[face][msgfmt]")
{
    // 普通斜杠原样保留 —— 早先是逐个翻倍，那会让网址在网线上变形、跨端显示出错，
    // 而且发送侧翻倍、接收侧还原两边一旦不对称就会吃掉斜杠（用户报过的 bug）。
    std::vector<ChatSegment> segs1;
    segs1.push_back(ChatSegment::Text(_T("a/b")));
    CHECK(WS(BuildSendBody(segs1)) == L"a/b");

    // 网址原样上线：对端不认这套 PC 端约定时照原样显示也是对的。
    std::vector<ChatSegment> segs2;
    segs2.push_back(ChatSegment::Text(_T("http://x")));
    CHECK(WS(BuildSendBody(segs2)) == L"http://x");

    // 用户手打的表情标记要转义，否则对端会把它渲染成表情。
    std::vector<ChatSegment> segs3;
    segs3.push_back(ChatSegment::Text(_T("literal /f[\"3\"] here")));
    CHECK(WS(BuildSendBody(segs3)) == L"literal //f[\"3\"] here");
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

TEST_CASE("F6 用户手打的 /f 不成标记则无需转义，成标记才转义", "[face][msgfmt]")
{
    // "/f fake" 缺 ["N"] 那一截，接收侧本来就解析不成表情，故不必转义 ——
    // 转义只加在"真会被误判"的地方，别处一律原样，网址才不会变形。
    std::vector<ChatSegment> segs;
    segs.push_back(ChatSegment::Text(_T("see /f fake")));
    segs.push_back(ChatSegment::Face(0));
    CHECK(WS(BuildSendBody(segs)) == L"see /f fake/f[\"0\"]");

    // 打全了的 /f["9"] 才是"会被误判"的那一种，必须转义。
    std::vector<ChatSegment> segs2;
    segs2.push_back(ChatSegment::Text(_T("see /f[\"9\"] fake")));
    segs2.push_back(ChatSegment::Face(0));
    CHECK(WS(BuildSendBody(segs2)) == L"see //f[\"9\"] fake/f[\"0\"]");
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

TEST_CASE("G4 ParseSendBody 只把紧跟表情标记的双斜杠当转义", "[face][msgfmt]")
{
    // 后面不是表情标记：两个斜杠原样留着，<u>不</u>还原成一个。
    // 网址里的 // 属于这一类，早先一律还原的做法会把它吃掉一个。
    {
        std::vector<ChatSegment> segs = ParseSendBody(CString(_T("a//b")));
        REQUIRE(segs.size() == 1);
        CHECK(WS(segs[0].m_strText) == L"a//b");
    }

    // 后面紧跟表情标记：这才是转义，还原成字面的 /f["3"]、且不渲染成表情。
    {
        std::vector<ChatSegment> segs = ParseSendBody(CString(_T("//f[\"3\"]")));
        REQUIRE(segs.size() == 1);
        CHECK(segs[0].m_bFace == false);
        CHECK(WS(segs[0].m_strText) == L"/f[\"3\"]");
    }
}

TEST_CASE("G4b 网址往返不丢斜杠（用户报的 bug）", "[face][msgfmt]")
{
    // 在输入框里打 http://www.baidu.com 发出去，对端 / 本端回显都应当一字不差。
    const CString typed = _T("http://www.baidu.com");

    std::vector<ChatSegment> in;
    in.push_back(ChatSegment::Text(typed));
    const CString body = BuildSendBody(in);
    //网线上的正文也保持原样，跨端照原样显示同样是对的
    CHECK(WS(body) == L"http://www.baidu.com");

    std::vector<ChatSegment> out = ParseSendBody(body);
    REQUIRE(out.size() == 1);
    CHECK(out[0].m_bFace == false);
    CHECK(WS(out[0].m_strText) == WS(typed));

    //会话列表的预览摘要走的是另一条路（BodyToDigest），同样不能丢斜杠
    CHECK(WS(BodyToDigest(body)) == WS(typed));
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
    in.push_back(ChatSegment::Text(_T("path a/b see http://x ")));   // 含字面斜杠与网址
    in.push_back(ChatSegment::Face(7));
    in.push_back(ChatSegment::Text(_T(" end /f[\"9\"] literal")));   // 含手打的表情标记
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

TEST_CASE("G7 BodyToDigest 表情替换成占位、普通双斜杠原样", "[face][msgfmt]")
{
    //后面不跟表情标记的 // 不是转义，摘要里原样保留（网址的双斜杠就靠这一条）
    CHECK(WS(BodyToDigest(CString(_T("hi/f[\"3\"]a//b")))) == L"hi[\x8868\x60c5]a//b");
    //跟着表情标记的 // 才是转义，摘要里还原成字面的 /f["3"]
    CHECK(WS(BodyToDigest(CString(_T("hi //f[\"3\"]")))) == L"hi /f[\"3\"]");
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

// ---- 历史里已撤回的消息：构造成撤回提示行 ----
// 服务端拉历史时照样下发已撤回的消息（带 recalled: true），聊天窗渲染前用 MakeRecalledTipRow 替换。

namespace {

//历史消息的 msgid 与时间（Unix 毫秒），取值无特殊含义
const long long kRecalledMsgid = 880301;
const long long kRecalledTimeMs = 1790953090000LL;

//构造一条已撤回的历史消息
net::MessageNotify MakeRecalledNotify(int msgtype)
{
    net::MessageNotify m;
    m.m_msgid     = kRecalledMsgid;
    m.m_timestamp = kRecalledTimeMs;
    m.m_msgtype   = msgtype;
    m.m_recalled  = true;
    m.m_text      = "secret";
    return m;
}

} // anonymous namespace

TEST_CASE("RCT5 历史里本人撤回的消息：提示行写「你撤回了一条消息」，带原 msgid 与时间", "[recall][msgfmt]")
{
    const balloonchatwnd::ChatMessage tip =
        balloonchatwnd::MakeRecalledTipRow(MakeRecalledNotify(message_type_text), true, CString(_T("\x5f20\x4e09")));
    CHECK(tip.kind == (int)balloonchatwnd::kMsgTime);
    CHECK(WS(tip.text) == L"\x4f60\x64a4\x56de\x4e86\x4e00\x6761\x6d88\x606f");
    CHECK(tip.msgid == kRecalledMsgid);
    CHECK(tip.timestampMs == kRecalledTimeMs);
    CHECK(tip.relayCardMsgid == 0);   //撤回提示行不可点击
}

TEST_CASE("RCT6 历史里别人撤回的消息：写撤回者的名字，名字为空时写「对方」", "[recall][msgfmt]")
{
    const balloonchatwnd::ChatMessage named =
        balloonchatwnd::MakeRecalledTipRow(MakeRecalledNotify(message_type_text), false, CString(_T("\x5f20\x4e09")));
    CHECK(WS(named.text) == L"\x5f20\x4e09\x64a4\x56de\x4e86\x4e00\x6761\x6d88\x606f");

    const balloonchatwnd::ChatMessage unnamed =
        balloonchatwnd::MakeRecalledTipRow(MakeRecalledNotify(message_type_text), false, CString());
    CHECK(WS(unnamed.text) == L"\x5bf9\x65b9\x64a4\x56de\x4e86\x4e00\x6761\x6d88\x606f");
}

TEST_CASE("RCT7 撤回的接龙卡片同样只剩一行提示，不再是卡片", "[recall][msgfmt]")
{
    net::MessageNotify card = MakeRecalledNotify(message_type_relay);
    card.m_relayContent = "{\"title\":\"x\",\"note\":\"\",\"deadline\":\"0\"}";
    const balloonchatwnd::ChatMessage tip = balloonchatwnd::MakeRecalledTipRow(card, false, CString());
    CHECK(tip.kind == (int)balloonchatwnd::kMsgTime);
    CHECK(tip.relayContent.empty());
    CHECK(tip.relayButton == 0);
}

TEST_CASE("RCT8 历史里撤回者的取法：服务端记录了撤回者就用它，没有记录时按发送人", "[recall][msgfmt]")
{
    //群主 5003 撤回了 5001 发的消息：提示里要写群主，而不是发送人
    net::MessageNotify byAdmin = MakeRecalledNotify(message_type_text);
    byAdmin.m_from       = 5001;
    byAdmin.m_recalledBy = 5003;
    CHECK(balloonchatwnd::RecallOperatorOf(byAdmin) == 5003);

    //服务端开始记录撤回者之前撤回的消息：撤回者为 0，按发送人处理（绝大多数撤回是本人操作的）
    net::MessageNotify legacy = MakeRecalledNotify(message_type_text);
    legacy.m_from       = 5001;
    legacy.m_recalledBy = 0;
    CHECK(balloonchatwnd::RecallOperatorOf(legacy) == 5001);
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

//历史里的窗口抖动（没有正文）曾落进「非文本用占位文字」分支，画成内容为「[抖一抖]」的文字气泡。
//改为居中提示行，文字与实时收发时插入的提示相同，带原 msgid 与时间。
TEST_CASE("SHK1 历史里的窗口抖动画成居中提示行：本人发出与对方发来各用各的文字", "[msgfmt]")
{
    net::MessageNotify m;
    m.m_msgid     = 880401;
    m.m_timestamp = 1790953190000LL;
    m.m_msgtype   = message_type_shake;

    const balloonchatwnd::ChatMessage mine = balloonchatwnd::MakeShakeTipRow(m, true);
    CHECK(mine.kind == (int)balloonchatwnd::kMsgTime);
    CHECK(WS(mine.text) == L"你向对方发送了窗口抖动");
    CHECK(mine.msgid == 880401);
    CHECK(mine.timestampMs == 1790953190000LL);

    const balloonchatwnd::ChatMessage peer = balloonchatwnd::MakeShakeTipRow(m, false);
    CHECK(peer.kind == (int)balloonchatwnd::kMsgTime);
    CHECK(WS(peer.text) == L"对方向你发送了窗口抖动");

    //与实时收发时插入的提示文字一致，首屏补回早到提示时才能据此去掉重复的那条
    CHECK(WS(peer.text) == WS(balloonchatwnd::ShakeTipText(false)));
    CHECK(WS(mine.text) == WS(balloonchatwnd::ShakeTipText(true)));
}
