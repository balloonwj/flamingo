/**
 *  聊天窗发送前体积检查的单元测试（Suite CSL），ChatSendLimitTests.cpp
 *
 *  背景见 bugs.md 的 BUG-32 与 UI/BalloonChatWnd/ChatSendLimit.h：单条消息 content 超过
 *  MESSAGE_CONTENT_MAX_BYTES（256 KiB，按紧凑 JSON 计）时服务端拒收，超大消息被放行则会让
 *  接收方断线。客户端在按下发送时按与服务端相同的口径检查。覆盖：
 *    CSL1 英文正文正好到上限放行，多一个字符拒绝；
 *    CSL2 中文按 JSON 转义后的字节数计（一个汉字 6 字节），与服务端口径一致；
 *    CSL3 字体与引用也计入体积；
 *    CSL4 客户端的上限取值与服务端头文件里的取值相同（运行时读取服务端源码核对）；
 *    CSL5 发送失败回执的提示：内容过长有专门说法，其余错误码带上错误码；
 *    CSL6 逐段检查：图片段不计，引用只计入第一段文字，群聊 @ 按每段重扫后计入。
 *
 *  期望值一律按固定的序列化规则独立写出（空正文的 content 是 {"text":""}，11 字节），
 *  不借被测函数自己算。框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonChatWnd/ChatSendLimit.h"
#include "../net/Protocol.h"
#include "../Common/Util/EncodeUtil.h"

#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using balloonchatwnd::FindOversizedTextPart;
using balloonchatwnd::MentionCandidate;
using balloonchatwnd::RichOutgoingPart;
using balloonchatwnd::SendFailureText;

namespace {

// 空正文的 content 序列化后的字节数：{"text":""}
const size_t kEmptyTextContentBytes = 11;

// 一个汉字（BMP 内）经紧凑 JSON 写出后的字节数：\uXXXX
const size_t kBytesPerEscapedHanzi = 6;

// 测试用 @ 对象的 userid
const long long kMentionUid = 10001;

// 造一个文字段。
RichOutgoingPart TextPart(const CString& text)
{
    RichOutgoingPart p;
    p.kind = balloonchatwnd::kRichPartText;
    p.text = text;
    p.imageIndex = -1;
    return p;
}

// 造一个图片段。
RichOutgoingPart ImagePart(int index)
{
    RichOutgoingPart p;
    p.kind = balloonchatwnd::kRichPartImage;
    p.imageIndex = index;
    return p;
}

// count 个英文字母 a。
CString RepeatA(size_t count)
{
    return CString(_T('a'), (int)count);
}

// count 个"张"（U+5F20）。
CString RepeatZhang(size_t count)
{
    return CString((wchar_t)0x5F20, (int)count);
}

// 只有一段文字的发送序列。
std::vector<RichOutgoingPart> OneText(const CString& text)
{
    std::vector<RichOutgoingPart> parts;
    parts.push_back(TextPart(text));
    return parts;
}

// 一份有效的引用快照。
net::ReplyQuote MakeQuote()
{
    net::ReplyQuote q;
    q.m_valid  = true;
    q.m_msgid  = 123456;
    q.m_from   = 10002;
    q.m_name   = "quoted";
    q.m_digest = "quoted digest";
    return q;
}

// 从测试程序所在目录逐级往上找仓库根（含 flamingoserver 目录的那一级），返回服务端
// MessageProtocol.h 的完整路径；找不到返回空串。
std::wstring FindServerMessageProtocolHeader()
{
    wchar_t buf[MAX_PATH] = { 0 };
    ::GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::wstring dir(buf);
    for (;;)
    {
        const size_t slash = dir.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
        {
            return std::wstring();
        }
        dir.erase(slash);
        const std::wstring candidate = dir + L"\\flamingoserver\\chatserversrc\\MessageProtocol.h";
        if (::GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            return candidate;
        }
    }
}

} // namespace

TEST_CASE("CSL1 英文正文正好到上限放行，多一个字符拒绝", "[sendlimit]")
{
    const net::TextFont noFont;
    const net::ReplyQuote noQuote;
    const std::vector<MentionCandidate> noMentions;

    //空正文的 content 就是 {"text":""}
    CHECK(net::MessageProtocolClient::textContentBytes("", noFont, noQuote,
                                                       std::vector<balloonchatwnd::MentionSpan>(),
                                                       false) == kEmptyTextContentBytes);

    const size_t fit = MESSAGE_CONTENT_MAX_BYTES - kEmptyTextContentBytes;
    CHECK(FindOversizedTextPart(OneText(RepeatA(fit)), noFont, noQuote, noMentions) == -1);
    CHECK(FindOversizedTextPart(OneText(RepeatA(fit + 1)), noFont, noQuote, noMentions) == 0);
}

TEST_CASE("CSL2 中文按 JSON 转义后的字节数计", "[sendlimit]")
{
    const net::TextFont noFont;
    const net::ReplyQuote noQuote;
    const std::vector<MentionCandidate> noMentions;

    //UTF-8 里每字 3 字节，序列化后写成 \uXXXX 每字 6 字节；以序列化后的为准
    const size_t sample = 100;
    const std::string utf8 = EncodeUtil::UnicodeToUtf8(std::wstring((LPCWSTR)RepeatZhang(sample)));
    CHECK(net::MessageProtocolClient::textContentBytes(utf8, noFont, noQuote,
                                                       std::vector<balloonchatwnd::MentionSpan>(),
                                                       false)
          == kEmptyTextContentBytes + sample * kBytesPerEscapedHanzi);

    const size_t fit = (MESSAGE_CONTENT_MAX_BYTES - kEmptyTextContentBytes) / kBytesPerEscapedHanzi;
    CHECK(FindOversizedTextPart(OneText(RepeatZhang(fit)), noFont, noQuote, noMentions) == -1);
    CHECK(FindOversizedTextPart(OneText(RepeatZhang(fit + 1)), noFont, noQuote, noMentions) == 0);
    //按 UTF-8 计远没到上限，按序列化后的计已经超了
    CHECK((fit + 1) * 3 < MESSAGE_CONTENT_MAX_BYTES);
}

TEST_CASE("CSL3 字体与引用也计入体积", "[sendlimit]")
{
    const net::TextFont noFont;
    const net::ReplyQuote noQuote;
    const std::vector<MentionCandidate> noMentions;

    //不带字体与引用时正好到上限
    const CString text = RepeatA(MESSAGE_CONTENT_MAX_BYTES - kEmptyTextContentBytes);
    REQUIRE(FindOversizedTextPart(OneText(text), noFont, noQuote, noMentions) == -1);

    //带上字体就超了
    net::TextFont font;
    font.m_valid = true;
    font.m_bold  = true;
    CHECK(FindOversizedTextPart(OneText(text), font, noQuote, noMentions) == 0);

    //带上引用也超了
    CHECK(FindOversizedTextPart(OneText(text), noFont, MakeQuote(), noMentions) == 0);
}

TEST_CASE("CSL4 客户端上限与服务端取值相同", "[sendlimit]")
{
    const std::wstring path = FindServerMessageProtocolHeader();
    INFO("server MessageProtocol.h not found from the test program directory upwards");
    REQUIRE_FALSE(path.empty());

    std::ifstream fin(path.c_str(), std::ios::binary);
    REQUIRE(fin.good());
    std::stringstream ss;
    ss << fin.rdbuf();
    const std::string header = ss.str();

    //服务端写法：MSG_BYTES_PER_KIB = 1024; MESSAGE_CONTENT_MAX_BYTES = 256 * MSG_BYTES_PER_KIB;
    std::smatch kib;
    std::smatch max;
    REQUIRE(std::regex_search(header, kib, std::regex("MSG_BYTES_PER_KIB\\s*=\\s*(\\d+)")));
    REQUIRE(std::regex_search(header, max,
                              std::regex("MESSAGE_CONTENT_MAX_BYTES\\s*=\\s*(\\d+)\\s*\\*\\s*MSG_BYTES_PER_KIB")));
    const size_t serverMax = (size_t)std::stoul(max[1].str()) * (size_t)std::stoul(kib[1].str());
    CHECK(serverMax == (size_t)MESSAGE_CONTENT_MAX_BYTES);
}

TEST_CASE("CSL5 发送失败的提示文案", "[sendlimit]")
{
    //内容过长：专门的说法
    const std::wstring tooLarge = SendFailureText(error_code_msg_too_large);
    CHECK(tooLarge.find(L"过长") != std::wstring::npos);

    //其余错误码：通用说法并带上错误码
    const int kOtherCode = 9999;
    const std::wstring other = SendFailureText(kOtherCode);
    CHECK(other.find(L"9999") != std::wstring::npos);
    CHECK(other.find(L"过长") == std::wstring::npos);
}

TEST_CASE("CSL6 逐段检查：图片段不计、引用只计第一段、@ 按段重扫", "[sendlimit]")
{
    const net::TextFont noFont;
    const std::vector<MentionCandidate> noMentions;
    const CString full = RepeatA(MESSAGE_CONTENT_MAX_BYTES - kEmptyTextContentBytes);

    //引用只随第一段文字发出：第二段正好到上限，第一段带着引用也很小，整体放行
    std::vector<RichOutgoingPart> parts;
    parts.push_back(TextPart(_T("hi")));
    parts.push_back(ImagePart(0));
    parts.push_back(TextPart(full));
    CHECK(FindOversizedTextPart(parts, noFont, MakeQuote(), noMentions) == -1);

    //同样的满载文字放在第一段，引用就落在它身上，超限，报告它的下标
    std::vector<RichOutgoingPart> parts2;
    parts2.push_back(ImagePart(0));
    parts2.push_back(TextPart(full));
    CHECK(FindOversizedTextPart(parts2, noFont, MakeQuote(), noMentions) == 1);

    //群聊 @：不带候选时正好到上限；带上候选，重扫出 @ 片段写进 content，超限
    const CString atText = _T("@") + CString((wchar_t)0x5F20) + CString((wchar_t)0x4E09);   // @张三
    const size_t atTextBytes = 1 + 2 * kBytesPerEscapedHanzi;
    const CString withAt = atText + RepeatA(MESSAGE_CONTENT_MAX_BYTES - kEmptyTextContentBytes - atTextBytes);
    REQUIRE(FindOversizedTextPart(OneText(withAt), noFont, net::ReplyQuote(), noMentions) == -1);

    std::vector<MentionCandidate> mentions;
    MentionCandidate c;
    c.m_uid = kMentionUid;
    c.m_displayName = EncodeUtil::UnicodeToUtf8(std::wstring(L"\x5F20\x4E09"));   // 张三
    mentions.push_back(c);
    CHECK(FindOversizedTextPart(OneText(withAt), noFont, net::ReplyQuote(), mentions) == 0);
}
