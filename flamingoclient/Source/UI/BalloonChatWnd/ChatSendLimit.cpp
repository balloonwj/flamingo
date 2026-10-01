/**
 *  ChatSendLimit.cpp —— 聊天窗发送文字前的体积检查与发送失败提示文案。说明见 ChatSendLimit.h。
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"
#include "ChatSendLimit.h"

#include "../../Common/Util/EncodeUtil.h"
#include "../../net/Protocol.h"   // MESSAGE_CONTENT_MAX_BYTES / error_code_msg_too_large

namespace balloonchatwnd {

const wchar_t* const kSendTextTooLargeTip = L"消息过长，请删减后分条发送。";

namespace {

// CString → UTF-8。与单聊窗、群聊窗发送文字时的转换完全相同（同一个 EncodeUtil 函数、
// 同样按 4 倍预留缓冲），保证这里量出的体积与真正发出去的一致。
std::string ToUtf8(const CString& text)
{
    const int wlen = text.GetLength();
    if (wlen <= 0)
    {
        return std::string();
    }
    // UTF-8 单个字符最多 4 字节，按 4 倍 + 1 预留缓冲。
    std::vector<char> buf((size_t)wlen * 4 + 1, 0);
    EncodeUtil::UnicodeToUtf8(text.GetString(), buf.data(), (int)buf.size());
    return std::string(buf.data());
}

} // namespace

int FindOversizedTextPart(const std::vector<RichOutgoingPart>& parts,
                          const net::TextFont& font,
                          const net::ReplyQuote& quote,
                          const std::vector<MentionCandidate>& mentions)
{
    const net::ReplyQuote noQuote;
    bool firstText = true;   // 引用只随第一段文字发出：聊天窗发完第一段就清掉引用
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (parts[i].kind != kRichPartText)
        {
            continue;   // 图片段走图片消息，content 只有文件信息，不会超限
        }

        const std::string utf8 = ToUtf8(parts[i].text);
        //群聊按本段最终文本重扫 @，与 SendGroupTextPart 的做法相同；单聊不带候选，扫不出片段
        const std::vector<MentionSpan> ats = RescanMentions(utf8, mentions);
        const size_t bytes = net::MessageProtocolClient::textContentBytes(
            utf8, font, firstText ? quote : noQuote, ats, HasMentionAll(ats));
        firstText = false;

        if (bytes > MESSAGE_CONTENT_MAX_BYTES)
        {
            return (int)i;
        }
    }
    return -1;
}

std::wstring SendFailureText(int code)
{
    switch (code)
    {
    //单条消息内容超过服务端的体积上限。正常情况下发送前已经拦住，这里是服务端兜底拦截的结果
    case error_code_msg_too_large:
        return L"消息内容过长，未能发出。请删减后分条发送。";

    //其余错误：对端不存在、被拉黑、不能发给自己等，沿用通用说法并带上错误码便于排查
    default:
        return L"消息发送失败（错误码 " + std::to_wstring(code) + L"）。";
    }
}

} // namespace balloonchatwnd
