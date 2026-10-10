/**
 *  MarkdownEditorModel.cpp
 *
 *  Markdown 编辑器纯逻辑部分的实现，说明见 MarkdownEditorModel.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "MarkdownEditorModel.h"

#include <vector>

#include "../BalloonChat/BalloonChatMsgFormat.h"   // BuildSendBody：正文里字面「/」的转义规则
#include "../BalloonChatWnd/GroupMentionModel.h"   // MentionSpan：textContentBytes 的参数
#include "../../net/MessageProtocolClient.h"       // textContentBytes / text_format_markdown
#include "../../net/Protocol.h"                    // MESSAGE_CONTENT_MAX_BYTES
#include "../../Common/I18n/I18n.h"

namespace balloonmd {

namespace {

//UTF-8 BOM 的三个字节
const unsigned char kUtf8Bom0 = 0xEF;
const unsigned char kUtf8Bom1 = 0xBB;
const unsigned char kUtf8Bom2 = 0xBF;
const size_t kUtf8BomBytes = 3;

//UTF-16LE BOM 的两个字节
const unsigned char kUtf16LeBom0 = 0xFF;
const unsigned char kUtf16LeBom1 = 0xFE;
const size_t kUtf16LeBomBytes = 2;

//UTF-8 首字节的掩码与取值：0xxxxxxx、110xxxxx、1110xxxx、11110xxx，后续字节 10xxxxxx
const unsigned char kUtf8Lead2Mask = 0xE0;
const unsigned char kUtf8Lead2Bits = 0xC0;
const unsigned char kUtf8Lead3Mask = 0xF0;
const unsigned char kUtf8Lead3Bits = 0xE0;
const unsigned char kUtf8Lead4Mask = 0xF8;
const unsigned char kUtf8Lead4Bits = 0xF0;
const unsigned char kUtf8ContMask = 0xC0;
const unsigned char kUtf8ContBits = 0x80;
const unsigned char kAsciiMax = 0x7F;
//两字节序列的首字节下限（0xC0、0xC1 只能产生过长编码，不合法）
const unsigned char kUtf8Lead2Min = 0xC2;
//四字节序列的首字节上限（0xF4 之后超出 U+10FFFF）
const unsigned char kUtf8Lead4Max = 0xF4;

//字节序列是否为合法的 UTF-8（只检查结构，足以区分 UTF-8 与 GBK 等双字节编码）
bool IsValidUtf8(const unsigned char* p, size_t n)
{
    size_t i = 0;
    while (i < n)
    {
        const unsigned char c = p[i];
        size_t follow = 0;
        if (c <= kAsciiMax)
            follow = 0;
        else if ((c & kUtf8Lead2Mask) == kUtf8Lead2Bits && c >= kUtf8Lead2Min)
            follow = 1;
        else if ((c & kUtf8Lead3Mask) == kUtf8Lead3Bits)
            follow = 2;
        else if ((c & kUtf8Lead4Mask) == kUtf8Lead4Bits && c <= kUtf8Lead4Max)
            follow = 3;
        else
            return false;
        //后续字节必须都在范围内且形如 10xxxxxx
        for (size_t k = 1; k <= follow; ++k)
        {
            if (i + k >= n || (p[i + k] & kUtf8ContMask) != kUtf8ContBits)
                return false;
        }
        i += follow + 1;
    }
    return true;
}

//按代码页把字节解码为 UTF-16
std::wstring DecodeBytes(UINT codePage, const char* p, size_t n)
{
    if (n == 0)
        return std::wstring();
    const int len = ::MultiByteToWideChar(codePage, 0, p, static_cast<int>(n), NULL, 0);
    if (len <= 0)
        return std::wstring();
    std::wstring out(static_cast<size_t>(len), L'\0');
    ::MultiByteToWideChar(codePage, 0, p, static_cast<int>(n), &out[0], len);
    return out;
}

//UTF-16 转 UTF-8
std::string ToUtf8(const std::wstring& text)
{
    if (text.empty())
        return std::string();
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), NULL, 0, NULL, NULL);
    if (len <= 0)
        return std::string();
    std::string out(static_cast<size_t>(len), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &out[0], len, NULL, NULL);
    return out;
}

//把 \r\n、\r、\n 一律换成 newline
std::wstring NormalizeBreaks(const std::wstring& text, const std::wstring& newline)
{
    std::wstring out;
    out.reserve(text.size() + text.size() / 8);
    for (size_t i = 0; i < text.size(); ++i)
    {
        const wchar_t c = text[i];
        if (c == L'\r')
        {
            out += newline;
            if (i + 1 < text.size() && text[i + 1] == L'\n')
                ++i;
        }
        else if (c == L'\n')
        {
            out += newline;
        }
        else
        {
            out += c;
        }
    }
    return out;
}

//路径里的文件名部分（最后一个 \ 或 / 之后）
std::wstring FileNameOf(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? path : path.substr(slash + 1);
}

//是否为空白字符（空格、制表符、全角空格）
bool IsBlankChar(wchar_t c)
{
    const wchar_t kIdeographicSpace = 0x3000;
    return c == L' ' || c == L'\t' || c == kIdeographicSpace;
}

} // namespace

std::wstring DecodeMarkdownFileBytes(const std::string& bytes, int* encoding)
{
    const unsigned char* p = reinterpret_cast<const unsigned char*>(bytes.data());
    const size_t n = bytes.size();
    int detected = md_file_utf8;
    std::wstring text;
    if (n >= kUtf8BomBytes && p[0] == kUtf8Bom0 && p[1] == kUtf8Bom1 && p[2] == kUtf8Bom2)
    {
        detected = md_file_utf8_bom;
        text = DecodeBytes(CP_UTF8, bytes.data() + kUtf8BomBytes, n - kUtf8BomBytes);
    }
    else if (n >= kUtf16LeBomBytes && p[0] == kUtf16LeBom0 && p[1] == kUtf16LeBom1)
    {
        detected = md_file_utf16le;
        const size_t units = (n - kUtf16LeBomBytes) / sizeof(wchar_t);
        text.assign(reinterpret_cast<const wchar_t*>(bytes.data() + kUtf16LeBomBytes), units);
    }
    else if (IsValidUtf8(p, n))
    {
        detected = md_file_utf8;
        text = DecodeBytes(CP_UTF8, bytes.data(), n);
    }
    else
    {
        detected = md_file_ansi;
        text = DecodeBytes(CP_ACP, bytes.data(), n);
    }
    if (encoding != NULL)
        *encoding = detected;
    return text;
}

std::string EncodeMarkdownForSave(const std::wstring& editorText)
{
    return ToUtf8(NormalizeBreaks(editorText, L"\r\n"));
}

std::string EditorTextToMarkdownUtf8(const std::wstring& editorText)
{
    return ToUtf8(NormalizeBreaks(editorText, L"\n"));
}

std::wstring MarkdownUtf8ToEditorText(const std::string& sourceUtf8)
{
    return NormalizeBreaks(DecodeBytes(CP_UTF8, sourceUtf8.data(), sourceUtf8.size()), L"\r\n");
}

std::string MarkdownBodyFromSourceUtf8(const std::string& sourceUtf8)
{
    if (sourceUtf8.empty())
        return std::string();
    const std::wstring source = DecodeBytes(CP_UTF8, sourceUtf8.data(), sourceUtf8.size());
    std::vector<balloonchat::ChatSegment> segs;
    segs.push_back(balloonchat::ChatSegment::Text(CString(source.c_str())));
    const CString body = balloonchat::BuildSendBody(segs);
    return ToUtf8(std::wstring((LPCWSTR)body));
}

std::wstring MarkdownTitleFromSource(const std::string& sourceUtf8)
{
    const std::wstring text = NormalizeBreaks(DecodeBytes(CP_UTF8, sourceUtf8.data(), sourceUtf8.size()), L"\n");
    size_t pos = 0;
    while (pos <= text.size())
    {
        size_t end = text.find(L'\n', pos);
        if (end == std::wstring::npos)
            end = text.size();
        std::wstring line = text.substr(pos, end - pos);
        //去掉行首的空白与标题标记 #，以及其后的空白
        size_t b = 0;
        while (b < line.size() && IsBlankChar(line[b]))
            ++b;
        while (b < line.size() && line[b] == L'#')
            ++b;
        while (b < line.size() && IsBlankChar(line[b]))
            ++b;
        size_t e = line.size();
        while (e > b && IsBlankChar(line[e - 1]))
            --e;
        if (e > b)
        {
            line = line.substr(b, e - b);
            if (line.size() > kMarkdownTitleMaxChars)
                line = line.substr(0, kMarkdownTitleMaxChars) + L"\x2026";
            return line;
        }
        if (end >= text.size())
            break;
        pos = end + 1;
    }
    return std::wstring();
}

std::wstring MarkdownEditorCaption(const std::wstring& filePath, bool dirty)
{
    const std::wstring name = filePath.empty() ? std::wstring(TR(L"未命名")) : FileNameOf(filePath);
    const std::wstring shown = dirty ? (L"*" + name) : name;
    return TRF(L"Markdown 编辑器 - {0}", shown.c_str());
}

std::wstring MarkdownSendTargetsText(bool loggedIn, const std::vector<MarkdownSendTarget>& targets)
{
    if (!loggedIn)
        return TR(L"登录后可发送");
    if (targets.empty())
        return TR(L"未选择发送对象");
    std::wstring first = targets[0].m_name;
    if (first.empty())
        first = std::to_wstring(static_cast<long long>(targets[0].m_id));
    if (targets.size() == 1)
        return TRF(L"发送给：{0}", first.c_str());
    return TRF(L"发送给：{0} 等 {1} 个", first.c_str(), static_cast<int>(targets.size()));
}

int CheckMarkdownSend(bool loggedIn, const std::string& sourceUtf8, size_t targetCount)
{
    if (!loggedIn)
        return md_send_offline;
    bool hasText = false;
    for (size_t i = 0; i < sourceUtf8.size(); ++i)
    {
        const char c = sourceUtf8[i];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
        {
            hasText = true;
            break;
        }
    }
    if (!hasText)
        return md_send_empty;
    const std::string body = MarkdownBodyFromSourceUtf8(sourceUtf8);
    const size_t bytes = net::MessageProtocolClient::textContentBytes(
        body, net::TextFont(), net::ReplyQuote(), std::vector<balloonchatwnd::MentionSpan>(), false,
        net::text_format_markdown);
    if (bytes > MESSAGE_CONTENT_MAX_BYTES)
        return md_send_too_large;
    if (targetCount == 0)
        return md_send_no_target;
    return md_send_ok;
}

std::wstring MarkdownPreviewMeta(const std::wstring& sender, long long timestampMs)
{
    const long long kMsPerSecond = 1000;
    std::wstring when;
    if (timestampMs > 0)
    {
        const time_t seconds = static_cast<time_t>(timestampMs / kMsPerSecond);
        struct tm local;
        if (localtime_s(&local, &seconds) == 0)
        {
            const int kTmYearBase = 1900;
            wchar_t buf[32] = { 0 };
            swprintf_s(buf, L"%04d-%02d-%02d %02d:%02d", local.tm_year + kTmYearBase, local.tm_mon + 1,
                       local.tm_mday, local.tm_hour, local.tm_min);
            when = buf;
        }
    }
    if (sender.empty())
        return when;
    if (when.empty())
        return sender;
    return TRF(L"{0} · {1}", sender.c_str(), when.c_str());
}

std::wstring MarkdownSendCheckText(int check)
{
    switch (check)
    {
    //未登录或连接不可用
    case md_send_offline:
        return TR(L"登录后才能发送。");
    //文档为空
    case md_send_empty:
        return TR(L"文档是空的，写点内容再发送吧。");
    //超过单条消息的体积上限
    case md_send_too_large:
        return TR(L"文档太长，超过了单条消息的上限，请删减后再发送，或导出为文件发送。");
    //还没有选择发送对象
    case md_send_no_target:
        return TR(L"请先选择发送对象。");
    //可以发送，或未知的取值
    default:
        return std::wstring();
    }
}

} // namespace balloonmd
