/**
 *  Markdown 预览复制到剪贴板（纯文字加 CF_HTML）的实现，说明见 MarkdownClipboard.h。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "MarkdownClipboard.h"

#include <stdio.h>
#include <string.h>

#include "../BalloonChatWnd/ChatClipboardIo.h"         // AllocGlobalCopy
#include "../BalloonChatWnd/ChatMarkdown.h"            // MarkdownImageLinkPrefix
#include "../../Feature/Markdown/MarkdownDocument.h"   // CMarkdownDocument / MarkdownToPlainTextUtf8
#include "../../Common/Log/IULog.h"

namespace balloonmd {

const wchar_t* const kCfHtmlFormatName = L"HTML Format";

namespace {

//CF_HTML 的头部：版本与四个固定宽度（十位）的字节偏移。宽度固定，先按占位值算出头部长度，再填入实际值
const char* const kCfHtmlHeaderFormat =
    "Version:0.9\r\nStartHTML:%010u\r\nEndHTML:%010u\r\nStartFragment:%010u\r\nEndFragment:%010u\r\n";

//头部缓冲区的大小（字节）：头部实际约 105 字节
const size_t kCfHtmlHeaderBufferSize = 160;

//片段标记，与 DLL 输出里的写法相同（FlamingoMarkdown/Core/MdHtml.h 的 kMdHtmlFragmentStart / End）
const char* const kFragmentStart = "<!--StartFragment-->";
const char* const kFragmentEnd = "<!--EndFragment-->";

//DLL 输出开头的文档类型声明：CF_HTML 里去掉，StartHTML 直接指向 <html>
const char* const kDoctypeLine = "<!DOCTYPE html>\n";

//UTF-8 转 UTF-16
std::wstring WideFromUtf8(const std::string& text)
{
    if (text.empty())
        return std::wstring();
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), NULL, 0);
    if (len <= 0)
        return std::wstring();
    std::wstring out(static_cast<size_t>(len), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], len);
    return out;
}

//单独的 \n 换成 \r\n（记事本等程序只认 \r\n）；已经是 \r\n 的不重复转换
std::wstring ToCrLf(const std::wstring& text)
{
    std::wstring out;
    out.reserve(text.size() + text.size() / 16);
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == L'\n' && (i == 0 || text[i - 1] != L'\r'))
            out += L'\r';
        out += text[i];
    }
    return out;
}

//CF_HTML 的格式编号（进程内只注册一次）；注册失败时为 0
UINT CfHtmlFormat()
{
    static const UINT format = ::RegisterClipboardFormatW(kCfHtmlFormatName);
    return format;
}

//把一块数据以 format 格式放进已打开的剪贴板；失败时释放内存
bool SetClipboardBytes(UINT format, const void* data, size_t bytes)
{
    HGLOBAL mem = balloonchatwnd::AllocGlobalCopy(data, bytes);
    if (mem == NULL)
        return false;
    if (::SetClipboardData(format, mem) == NULL)
    {
        ::GlobalFree(mem);
        return false;
    }
    return true;
}

} // namespace

std::string WrapCfHtml(const std::string& html)
{
    std::string body = html;
    const size_t doctypeLen = strlen(kDoctypeLine);
    if (body.compare(0, doctypeLen, kDoctypeLine) == 0)
        body.erase(0, doctypeLen);
    const size_t fragStart = body.find(kFragmentStart);
    const size_t fragEnd = body.find(kFragmentEnd);
    if (fragStart == std::string::npos || fragEnd == std::string::npos || fragEnd < fragStart)
        return std::string();

    char header[kCfHtmlHeaderBufferSize] = { 0 };
    const int headerLen = _snprintf_s(header, sizeof(header), _TRUNCATE, kCfHtmlHeaderFormat, 0u, 0u, 0u, 0u);
    if (headerLen <= 0)
        return std::string();
    const unsigned base = static_cast<unsigned>(headerLen);
    _snprintf_s(header, sizeof(header), _TRUNCATE, kCfHtmlHeaderFormat, base,
                base + static_cast<unsigned>(body.size()),
                base + static_cast<unsigned>(fragStart + strlen(kFragmentStart)),
                base + static_cast<unsigned>(fragEnd));
    return std::string(header) + body;
}

bool BuildMarkdownSelectionClipboard(const markdown::CMarkdownDocument& doc, int begin, int end,
                                     MarkdownClipboardData& out)
{
    out.m_text.clear();
    out.m_cfHtml.clear();
    if (begin == end)
        return false;
    //纯文字与以前相同：渲染后文字的这一段，换行已是 \r\n
    out.m_text = doc.GetText(begin, end);
    if (out.m_text.empty())
        return false;
    //DLL 有第四版接口时再生成这一段的 HTML；旧版 DLL 只放纯文字
    std::string html;
    if (doc.ExportHtmlRange(begin, end, html) && !html.empty())
        out.m_cfHtml = WrapCfHtml(html);
    return true;
}

bool BuildMarkdownWholeClipboard(const std::string& sourceUtf8, MarkdownClipboardData& out)
{
    out.m_text.clear();
    out.m_cfHtml.clear();
    const std::wstring imagePrefix = balloonchatwnd::MarkdownImageLinkPrefix();
    //纯文字与「复制为纯文字」相同
    out.m_text = ToCrLf(WideFromUtf8(markdown::MarkdownToPlainTextUtf8(sourceUtf8, imagePrefix)));
    if (out.m_text.empty())
        return false;
    markdown::CMarkdownDocument doc;
    std::string html;
    if (doc.Parse(sourceUtf8, imagePrefix) && doc.ExportHtmlRange(0, doc.GetTextLength(), html) && !html.empty())
        out.m_cfHtml = WrapCfHtml(html);
    return true;
}

bool PutMarkdownClipboard(HWND owner, const MarkdownClipboardData& data)
{
    if (data.m_text.empty())
        return false;
    if (!::OpenClipboard(owner))
    {
        LOG_WARNING("markdown clipboard: OpenClipboard failed, error %u.", (unsigned)::GetLastError());
        return false;
    }
    ::EmptyClipboard();
    const bool textOk = SetClipboardBytes(CF_UNICODETEXT, data.m_text.c_str(), (data.m_text.size() + 1) * sizeof(wchar_t));
    bool htmlOk = false;
    if (!data.m_cfHtml.empty())
    {
        //CF_HTML 以 0 结尾
        const UINT format = CfHtmlFormat();
        if (format != 0)
            htmlOk = SetClipboardBytes(format, data.m_cfHtml.c_str(), data.m_cfHtml.size() + 1);
    }
    ::CloseClipboard();
    LOG_INFO("markdown clipboard: text %u chars (ok=%d), html %u bytes (ok=%d).", (unsigned)data.m_text.size(),
             textOk ? 1 : 0, (unsigned)data.m_cfHtml.size(), htmlOk ? 1 : 0);
    return textOk;
}

bool CopyMarkdownRichTextToClipboard(HWND owner, const std::string& sourceUtf8)
{
    MarkdownClipboardData data;
    if (!BuildMarkdownWholeClipboard(sourceUtf8, data))
        return false;
    return PutMarkdownClipboard(owner, data);
}

} // namespace balloonmd
