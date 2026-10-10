/**
 *  ChatReedit.cpp
 *
 *  聊天气泡「重新编辑」的实现，说明见 ChatReedit.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "ChatReedit.h"
#include "ChatInputRichEdit.h"
#include "ChatMarkdown.h"   // MarkdownSourceFromBody

namespace balloonchatwnd {

namespace {

// 一个 @ 片段至少包含 '@' 与一个字的显示名
const int kMinMentionSpanLen = 2;

// 两个候选是不是同一个（同一个人、同一个显示名）
bool SameCandidate(const MentionCandidate& a, const MentionCandidate& b)
{
    return a.m_uid == b.m_uid && a.m_displayName == b.m_displayName;
}

// 宽字符串转 UTF-8
std::string WideToUtf8(const CString& text)
{
    if (text.IsEmpty())
    {
        return std::string();
    }
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)text, text.GetLength(), NULL, 0, NULL, NULL);
    if (n <= 0)
    {
        return std::string();
    }
    std::string out((size_t)n, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)text, text.GetLength(), &out[0], n, NULL, NULL);
    return out;
}

} // namespace

bool CanReeditMessage(const ChatMessage& msg)
{
    //只有自己发出、被服务端拒绝的文字消息（文字与大表情都是 msgtype=1 的文字消息）
    return msg.fromMe && msg.sendFailed && (msg.kind == kMsgText || msg.kind == kMsgEmoji);
}

std::vector<MentionCandidate> MentionsFromRenderSpans(const CString& text, const std::vector<RenderSpan>& ats)
{
    std::vector<MentionCandidate> out;
    for (size_t i = 0; i < ats.size(); ++i)
    {
        const RenderSpan& span = ats[i];
        //越界、过短（只有一个 '@'）、或片段不以 '@' 开头的都跳过
        if (span.m_off < 0 || span.m_len < kMinMentionSpanLen || span.m_off + span.m_len > text.GetLength())
        {
            continue;
        }
        if (text[span.m_off] != L'@')
        {
            continue;
        }
        MentionCandidate cand;
        cand.m_uid = span.m_uid;
        cand.m_displayName = WideToUtf8(text.Mid(span.m_off + 1, span.m_len - 1));
        bool seen = false;
        for (size_t j = 0; j < out.size(); ++j)
        {
            if (SameCandidate(out[j], cand))
            {
                seen = true;
                break;
            }
        }
        if (!seen)
        {
            out.push_back(cand);
        }
    }
    return out;
}

ChatReeditData BuildReeditData(const ChatMessage& msg)
{
    if (!CanReeditMessage(msg))
    {
        return ChatReeditData();
    }
    return BuildReeditPayload(msg);
}

ChatReeditData BuildReeditPayload(const ChatMessage& msg)
{
    ChatReeditData data;
    //与「复制」后粘贴回输入框同一种拆法：文字原样、表情还原成表情
    data.m_segments = BuildClipSegmentsFromBody(msg.text);
    data.m_mentions = MentionsFromRenderSpans(msg.text, msg.ats);
    data.m_isMarkdown = msg.isMarkdown;
    //Markdown：源文本交给宿主预填进 Markdown 编辑器
    if (msg.isMarkdown)
        data.m_markdownSource = MarkdownSourceFromBody(msg.text);
    //代码片段：原文、语言与批注交给宿主预填进发送代码窗口
    if (msg.isCode)
    {
        data.m_isCode = true;
        data.m_codeLang = msg.codeLang;
        data.m_code = std::wstring((LPCWSTR)msg.text);
        data.m_codeNotes = msg.codeNotes;
        //追加批注的消息引用的是原消息，重新编辑时据此回到批注模式（docs/代码片段方案.md 第 11.3 节）
        if (msg.hasQuote)
        {
            data.m_codeQuoteMsgid = msg.quoteMsgid;
            data.m_codeQuoteFrom = msg.quoteFrom;
        }
    }
    return data;
}

void ApplyReeditToInput(const ChatReeditData& data, ChatInputRichEdit* input,
                        std::vector<MentionCandidate>* pendingMentions)
{
    if (input == NULL)
    {
        return;
    }
    //插在当前光标处：输入框为空时等于直接填入，不为空时已有内容不动
    input->InsertClipSegments(data.m_segments);
    if (pendingMentions == NULL)
    {
        return;
    }
    //@ 重新登记为待发候选；已经登记过的同一个人同一个名字不重复登记，发送时照常按最终文本重扫
    for (size_t i = 0; i < data.m_mentions.size(); ++i)
    {
        bool seen = false;
        for (size_t j = 0; j < pendingMentions->size(); ++j)
        {
            if (SameCandidate((*pendingMentions)[j], data.m_mentions[i]))
            {
                seen = true;
                break;
            }
        }
        if (!seen)
        {
            pendingMentions->push_back(data.m_mentions[i]);
        }
    }
}

} // namespace balloonchatwnd
