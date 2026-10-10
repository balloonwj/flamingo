/**
 *  ChatMemoPrefill.cpp
 *
 *  聊天消息「添加到备忘」的预填内容，说明见 ChatMemoPrefill.h 与 docs/备忘与提醒方案.md §8。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "ChatMemoPrefill.h"

#include <stdio.h>

#include "ChatClipboardContent.h"               // BuildClipSegmentsFromBody / ClipSegmentsToPlainText
#include "ChatMarkdown.h"                       // MarkdownSourceFromBody / MarkdownImageLinkPrefix
#include "../../Feature/Markdown/MarkdownDocument.h"   // markdown::MarkdownToPlainTextUtf8
#include "../../Common/Util/EncodeUtil.h"       // EncodeUtil::UnicodeToUtf8 / Utf8ToUnicode
#include "../../Common/I18n/I18n.h"             // TR / TRF：界面文字的多语言查找

namespace balloonchatwnd {

namespace {

// 截断后补在末尾的省略号
const wchar_t kEllipsis = L'\x2026';

// 1601-01-01 到 1970-01-01 之间的 100 纳秒数，Unix 毫秒换算 FILETIME 用
const unsigned long long kFileTimeUnixEpochDiff = 116444736000000000ULL;

// FILETIME 的单位是 100 纳秒，毫秒换算成 FILETIME 要乘的倍数
const unsigned long long kFileTimeTicksPerMs = 10000ULL;

// 「时:分」文字的缓冲区长度（字符），"23:59" 加结尾 0 足够
const int kClockTextChars = 8;

// UTF-16 高位代理
bool IsHighSurrogate(wchar_t ch)
{
    return ch >= 0xD800 && ch <= 0xDBFF;
}

// UTF-16 低位代理
bool IsLowSurrogate(wchar_t ch)
{
    return ch >= 0xDC00 && ch <= 0xDFFF;
}

// 取文字开头的 maxChars 个字符（代理对算 1 个），不会从代理对中间切开
std::wstring TakeChars(const std::wstring& text, int maxChars)
{
    if (maxChars <= 0)
    {
        return std::wstring();
    }
    int count = 0;
    size_t i = 0;
    while (i < text.size() && count < maxChars)
    {
        if (IsHighSurrogate(text[i]) && i + 1 < text.size() && IsLowSurrogate(text[i + 1]))
        {
            i += 2;
        }
        else
        {
            ++i;
        }
        ++count;
    }
    return text.substr(0, i);
}

// 超过 maxChars 个字时截成 maxChars - 1 个字加「…」，总数恰好等于 maxChars
std::wstring Ellipsize(const std::wstring& text, int maxChars)
{
    if (CountMemoChars(text) <= maxChars)
    {
        return text;
    }
    return TakeChars(text, maxChars - 1) + kEllipsis;
}

// 去掉首尾的空格、制表符与回车
std::wstring TrimSpaces(const std::wstring& text)
{
    const wchar_t* const kSpaces = L" \t\r";
    const size_t begin = text.find_first_not_of(kSpaces);
    if (begin == std::wstring::npos)
    {
        return std::wstring();
    }
    const size_t end = text.find_last_not_of(kSpaces);
    return text.substr(begin, end - begin + 1);
}

// 第一个非空行（去掉首尾空白）；没有非空行时返回空串
std::wstring FirstNonEmptyLine(const std::wstring& text)
{
    size_t start = 0;
    while (start <= text.size())
    {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos)
        {
            end = text.size();
        }
        const std::wstring line = TrimSpaces(text.substr(start, end - start));
        if (!line.empty())
        {
            return line;
        }
        start = end + 1;
    }
    return std::wstring();
}

// 把 Unix 毫秒换算成本地时间的「10月6日 14:30」
std::wstring LocalTimeText(long long timestampMs)
{
    ULARGE_INTEGER ticks;
    ticks.QuadPart = (unsigned long long)timestampMs * kFileTimeTicksPerMs + kFileTimeUnixEpochDiff;
    FILETIME utc;
    utc.dwLowDateTime = ticks.LowPart;
    utc.dwHighDateTime = ticks.HighPart;
    FILETIME local;
    SYSTEMTIME st;
    if (!::FileTimeToLocalFileTime(&utc, &local) || !::FileTimeToSystemTime(&local, &st))
    {
        return std::wstring();
    }
    wchar_t clock[kClockTextChars] = { 0 };
    _snwprintf_s(clock, kClockTextChars, _TRUNCATE, L"%02u:%02u", (unsigned)st.wHour, (unsigned)st.wMinute);
    return TRF(L"{0}月{1}日 {2}", (int)st.wMonth, (int)st.wDay, clock);
}

// 来源行：「来自 张三，10月6日 14:30」或「来自 项目群 / 张三，10月6日 14:30」
std::wstring SourceLine(const MemoPrefillSource& src)
{
    const std::wstring sender = src.m_fromMe ? std::wstring(TR(L"我")) : std::wstring((LPCWSTR)src.m_senderName);
    const std::wstring when = (src.m_timestampMs > 0) ? LocalTimeText(src.m_timestampMs) : std::wstring();
    const bool group = !src.m_groupName.IsEmpty();
    if (when.empty())
    {
        return group ? TRF(L"来自 {0} / {1}", (LPCWSTR)src.m_groupName, sender)
                     : TRF(L"来自 {0}", sender);
    }
    return group ? TRF(L"来自 {0} / {1}，{2}", (LPCWSTR)src.m_groupName, sender, when)
                 : TRF(L"来自 {0}，{1}", sender, when);
}

} // namespace

int CountMemoChars(const std::wstring& text)
{
    int count = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        //代理对的后半个不另计
        if (IsLowSurrogate(text[i]) && i > 0 && IsHighSurrogate(text[i - 1]))
        {
            continue;
        }
        ++count;
    }
    return count;
}

bool CanAddMessageToMemo(const ChatMessage& msg)
{
    return msg.kind == kMsgText && !msg.sendFailed;
}

std::wstring MessagePlainTextForMemo(const ChatMessage& msg)
{
    //代码片段：正文就是原文
    if (msg.isCode)
    {
        return std::wstring((LPCWSTR)msg.text);
    }
    //Markdown：取渲染后的纯文字；Markdown 模块不可用时为源文本
    if (msg.isMarkdown)
    {
        const std::string plain = markdown::MarkdownToPlainTextUtf8(MarkdownSourceFromBody(msg.text),
                                                                     MarkdownImageLinkPrefix());
        return EncodeUtil::Utf8ToUnicode(plain);
    }
    //普通文字：与「复制」相同，内联表情写成「[表情]」，@ 本来就以显示名写在正文里
    std::wstring plain = ClipSegmentsToPlainText(BuildClipSegmentsFromBody(msg.text));
    //统一成 \n 换行，方便按行取标题
    std::wstring out;
    out.reserve(plain.size());
    for (size_t i = 0; i < plain.size(); ++i)
    {
        if (plain[i] == L'\r')
        {
            if (i + 1 < plain.size() && plain[i + 1] == L'\n')
            {
                continue;
            }
            out += L'\n';
            continue;
        }
        out += plain[i];
    }
    return out;
}

MemoPrefill BuildMemoPrefill(const ChatMessage& msg, const MemoPrefillSource& src)
{
    const std::wstring body = MessagePlainTextForMemo(msg);

    //标题：第一个非空行，超长截断；没有文字时用「聊天消息」
    std::wstring title = FirstNonEmptyLine(body);
    if (title.empty())
    {
        title = TR(L"聊天消息");
    }
    title = Ellipsize(title, kMemoPrefillTitleMaxChars);

    //备注：来源行在前，正文在后；超长时只截正文，来源行保留（来源行本身超长时也截断）
    const std::wstring source = Ellipsize(SourceLine(src), kMemoPrefillNoteMaxChars);
    std::wstring note = source;
    const std::wstring trimmedBody = TrimSpaces(body);
    if (!trimmedBody.empty())
    {
        //来源行之后的换行占 1 个字
        const int room = kMemoPrefillNoteMaxChars - CountMemoChars(source) - 1;
        if (room > 0)
        {
            note += L'\n';
            note += Ellipsize(trimmedBody, room);
        }
    }

    MemoPrefill out;
    out.m_title = EncodeUtil::UnicodeToUtf8(title);
    out.m_note = EncodeUtil::UnicodeToUtf8(note);
    return out;
}

} // namespace balloonchatwnd
