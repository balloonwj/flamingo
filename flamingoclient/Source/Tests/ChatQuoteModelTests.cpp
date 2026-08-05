/**
 *  ChatQuoteModelTests.cpp
 *
 *  引用回复的「协议结构 ↔ 展示结构」换算与摘要生成（BalloonChatWnd/ChatQuoteModel.h）测试。
 *
 *  这里最要紧的是<u>两套消息类型枚举的换算</u>：气泡侧 ChatMsgKind 的 kMsgImage 是 3、
 *  kMsgFile 是 4，而协议侧 message_type_image 是 2、message_type_file 是 3。数值交错，
 *  直接赋值会把「引用了一张图片」写成「引用了一个文件」—— 而且这种错在界面上几乎看不出来
 *  （引用块本来就只是一行占位文字），只有靠用例钉死。
 *
 *  用 Catch2 v3（amalgamated），纯内存断言，不建窗口。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../BalloonChatWnd/ChatQuoteModel.h"
#include "../BalloonChatWnd/ChatMockData.h"
#include "../net/Protocol.h"

using namespace balloonchatwnd;

namespace {

const long long kSrcMsgid = 184;
const long long kSrcFrom  = 5308;

// 造一条已落库（有 msgid）的文本消息，作为"被引用的那一条"。
ChatMessage MakeSrcText(const CString& text)
{
    ChatMessage m = MakeTextMessage(false, text);
    m.msgid        = kSrcMsgid;
    m.senderUserId = kSrcFrom;
    return m;
}

} // anonymous namespace

// =====================================================================
// K 组：两套消息类型枚举的换算
// =====================================================================

TEST_CASE("K1 气泡类型换算到协议类型（数值交错，必须逐个映射）", "[quote][kind]")
{
    CHECK(MsgKindToProtocolType(kMsgText)  == message_type_text);
    CHECK(MsgKindToProtocolType(kMsgImage) == message_type_image);
    CHECK(MsgKindToProtocolType(kMsgFile)  == message_type_file);
    // 大 emoji 在协议里就是普通文本，没有独立类型。
    CHECK(MsgKindToProtocolType(kMsgEmoji) == message_type_text);

    // 关键回归：kMsgImage 的数值(3) 恰好等于 message_type_file(3)。
    // 若哪天有人图省事写成直接赋值，这一条立刻会红。
    CHECK(MsgKindToProtocolType(kMsgImage) != message_type_file);
}

TEST_CASE("K2 协议类型换算回气泡类型", "[quote][kind]")
{
    CHECK(ProtocolTypeToMsgKind(message_type_text)  == kMsgText);
    CHECK(ProtocolTypeToMsgKind(message_type_image) == kMsgImage);
    CHECK(ProtocolTypeToMsgKind(message_type_file)  == kMsgFile);
}

TEST_CASE("K3 三种可引用类型换算一圈回到原值", "[quote][kind]")
{
    const int kinds[] = { kMsgText, kMsgImage, kMsgFile };
    for (int i = 0; i < 3; ++i)
    {
        INFO("ChatMsgKind = " << kinds[i]);
        CHECK(ProtocolTypeToMsgKind(MsgKindToProtocolType(kinds[i])) == kinds[i]);
    }
}

TEST_CASE("K4 未知类型两个方向都安全退化成文本", "[quote][kind]")
{
    // 时间分隔行、撤回提示行本来就不该被引用；真走到这里也不能写出个乱七八糟的类型。
    CHECK(MsgKindToProtocolType(kMsgTime) == message_type_text);
    CHECK(MsgKindToProtocolType(999)      == message_type_text);
    CHECK(ProtocolTypeToMsgKind(999)      == kMsgText);
}

// =====================================================================
// D 组：摘要生成
// =====================================================================

TEST_CASE("D1 文本消息的摘要就是正文", "[quote][digest]")
{
    CHECK(BuildQuoteDigest(MakeSrcText(_T("你好"))) == CString(_T("你好")));
}

TEST_CASE("D2 内联表情不暴露协议转义", "[quote][digest]")
{
    // 走 BodyToDigest：/f["5"] 换成 [表情]，与会话列表预览同口径。
    // 用户在引用块里绝不该看到 /f["5"] 这种东西。
    const CString digest = BuildQuoteDigest(MakeSrcText(_T("hi/f[\"5\"]")));

    CHECK(digest.Find(_T("/f[")) < 0);
    CHECK(digest.Find(_T("[表情]")) >= 0);
}

TEST_CASE("D3 超长正文按上限截断并加省略号", "[quote][digest]")
{
    CString longText(_T('a'), kQuoteDigestMaxChars + 50);
    const CString digest = BuildQuoteDigest(MakeSrcText(longText));

    // 截断后长度 = 上限 + 1（省略号占一个字符）。
    CHECK(digest.GetLength() == kQuoteDigestMaxChars + 1);
    CHECK(digest.Right(1) == CString(_T("…")));
}

TEST_CASE("D4 恰好等于上限时不截断", "[quote][digest]")
{
    CString exact(_T('a'), kQuoteDigestMaxChars);
    const CString digest = BuildQuoteDigest(MakeSrcText(exact));

    CHECK(digest.GetLength() == kQuoteDigestMaxChars);
    CHECK(digest.Find(_T("…")) < 0);
}

TEST_CASE("D5 图片给占位、文件给文件名", "[quote][digest]")
{
    ChatMessage img = MakeSrcText(_T(""));
    img.kind = kMsgImage;
    CHECK(BuildQuoteDigest(img) == CString(_T("[图片]")));

    ChatMessage file = MakeSrcText(_T(""));
    file.kind     = kMsgFile;
    file.fileName = _T("design.fig");
    // 文件名比 [文件] 有信息量得多 —— 一个会话里往往有好几个文件。
    CHECK(BuildQuoteDigest(file) == CString(_T("design.fig")));

    // 文件名为空时才退回占位。
    ChatMessage noname = MakeSrcText(_T(""));
    noname.kind = kMsgFile;
    CHECK(BuildQuoteDigest(noname) == CString(_T("[文件]")));
}

// =====================================================================
// B 组：从消息生成协议快照
// =====================================================================

TEST_CASE("B1 文本消息生成的快照字段齐全", "[quote][build]")
{
    net::ReplyQuote q = BuildQuoteFromMessage(MakeSrcText(_T("你好")), _T("张三"));

    REQUIRE(q.m_valid);
    CHECK(q.m_msgid   == kSrcMsgid);
    CHECK(q.m_from    == kSrcFrom);
    CHECK(q.m_msgtype == message_type_text);
    CHECK_FALSE(q.m_name.empty());
    CHECK_FALSE(q.m_digest.empty());
    // 文本引用不该带图片那几项。
    CHECK(q.m_fileid.empty());
    CHECK(q.m_width == 0);
}

TEST_CASE("B2 没有服务器 msgid 的消息不能被引用", "[quote][build]")
{
    // 本端刚发出、回执还没到的消息 msgid 是 0 —— 引用它，对端拿到的 msgid 也是 0，
    // 永远定位不到原消息，不如直接不让引用。
    ChatMessage pending = MakeTextMessage(true, _T("刚发出"));
    pending.msgid = 0;

    net::ReplyQuote q = BuildQuoteFromMessage(pending, _T("我"));
    CHECK_FALSE(q.m_valid);
}

TEST_CASE("B3 图片消息的快照带上 fileid / md5 / 宽高", "[quote][build]")
{
    ChatMessage img = MakeSrcText(_T(""));
    img.kind   = kMsgImage;
    img.imageW = 800;
    img.imageH = 600;

    net::ReplyQuote q = BuildQuoteFromMessage(img, _T("张三"), _T("9021"), _T("abc123"));

    REQUIRE(q.m_valid);
    CHECK(q.m_msgtype == message_type_image);   // 不是 message_type_file
    CHECK(q.m_fileid  == "9021");
    CHECK(q.m_md5     == "abc123");
    CHECK(q.m_width   == 800);
    CHECK(q.m_height  == 600);
}

// =====================================================================
// F 组：协议快照填回展示结构
// =====================================================================

TEST_CASE("F1 快照填进消息结构后各字段就位", "[quote][fill]")
{
    net::ReplyQuote q;
    q.m_valid   = true;
    q.m_msgid   = kSrcMsgid;
    q.m_from    = kSrcFrom;
    q.m_msgtype = message_type_image;
    q.m_width   = 800;
    q.m_height  = 600;

    ChatMessage out = MakeTextMessage(true, _T("回复"));
    FillQuoteForRender(q, out);

    CHECK(out.hasQuote);
    CHECK(out.quoteMsgid == kSrcMsgid);
    CHECK(out.quoteFrom  == kSrcFrom);
    // 存成气泡口径，渲染侧直接按 ChatMsgKind 分派。
    CHECK(out.quoteMsgType == kMsgImage);
    CHECK(out.quoteImageW  == 800);
    CHECK(out.quoteImageH  == 600);
}

TEST_CASE("F2 无引用的快照把消息上的引用字段清干净", "[quote][fill]")
{
    // 复用同一个 ChatMessage 对象重复填充时（如翻页复用），残留的旧引用
    // 会让一条本来没有引用的消息凭空长出一个引用块。
    ChatMessage out = MakeTextMessage(true, _T("普通消息"));
    out.hasQuote   = true;
    out.quoteMsgid = 999;
    out.quoteName  = _T("残留");

    net::ReplyQuote empty;   // m_valid 默认 false
    FillQuoteForRender(empty, out);

    CHECK_FALSE(out.hasQuote);
    CHECK(out.quoteMsgid == 0);
    CHECK(out.quoteName.IsEmpty());
}

TEST_CASE("F3 快照有 valid 但 msgid 为 0 时按无引用处理", "[quote][fill]")
{
    net::ReplyQuote q;
    q.m_valid = true;
    q.m_msgid = 0;   // 没有可定位的原消息

    ChatMessage out = MakeTextMessage(true, _T("回复"));
    FillQuoteForRender(q, out);

    CHECK_FALSE(out.hasQuote);
}

TEST_CASE("F4 生成快照再填回来，类型能原样还原", "[quote][fill]")
{
    // 端到端把两次换算串起来：气泡类型 → 协议类型 → 气泡类型，中间不能走样。
    const int kinds[] = { kMsgText, kMsgImage, kMsgFile };
    for (int i = 0; i < 3; ++i)
    {
        ChatMessage src = MakeSrcText(_T("x"));
        src.kind = kinds[i];
        if (kinds[i] == kMsgFile)
        {
            src.fileName = _T("a.txt");
        }

        net::ReplyQuote q = BuildQuoteFromMessage(src, _T("张三"));
        REQUIRE(q.m_valid);

        ChatMessage out = MakeTextMessage(true, _T("回复"));
        FillQuoteForRender(q, out);

        INFO("原始 ChatMsgKind = " << kinds[i]);
        CHECK(out.quoteMsgType == kinds[i]);
    }
}
