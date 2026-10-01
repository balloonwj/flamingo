/**
 *  HistoryExport.cpp —— 消息记录导出网页与文件名处理的实现，说明见头文件。
 *  balloonwj@qq.com   2026-10-01
 */
#include "HistoryExport.h"

#include <wctype.h>   // towlower

namespace balloonhistory {

namespace {

//网页样式：单列、发送人加粗、时间灰色、图片限宽，不依赖外部文件
const wchar_t* const kHtmlStyle =
    L"body{font-family:'Microsoft YaHei',sans-serif;font-size:14px;color:#222;max-width:860px;margin:24px auto;padding:0 16px;}"
    L"h1{font-size:20px;border-bottom:1px solid #ddd;padding-bottom:8px;}"
    L".msg{padding:8px 0;border-bottom:1px solid #f0f0f0;}"
    L".sender{font-weight:bold;margin-right:8px;}"
    L".time{color:#999;font-size:12px;}"
    L".body{margin-top:4px;white-space:normal;word-break:break-all;}"
    L".body img{max-width:360px;max-height:360px;border:1px solid #eee;}";

//认得出的图片格式的文件头。按顺序比较，先匹配上的为准
struct ImageMagic
{
    size_t               m_offset;   //文件头在文件里的起始位置
    const unsigned char* m_bytes;    //文件头的字节
    size_t               m_size;     //文件头的字节数
    const wchar_t*       m_ext;      //对应的扩展名
};

const unsigned char kPngMagic[]  = { 0x89, 'P', 'N', 'G' };
const unsigned char kJpgMagic[]  = { 0xFF, 0xD8, 0xFF };
const unsigned char kGifMagic[]  = { 'G', 'I', 'F', '8' };
const unsigned char kBmpMagic[]  = { 'B', 'M' };
const unsigned char kRiffMagic[] = { 'R', 'I', 'F', 'F' };
const unsigned char kWebpMagic[] = { 'W', 'E', 'B', 'P' };
//WebP 是 RIFF 容器，"WEBP" 在第 8 个字节起
const size_t kWebpTagOffset = 8;

bool MatchAt(const unsigned char* head, size_t size, size_t offset, const unsigned char* bytes, size_t n)
{
    if (head == NULL || offset + n > size)
    {
        return false;
    }
    for (size_t i = 0; i < n; ++i)
    {
        if (head[offset + i] != bytes[i])
        {
            return false;
        }
    }
    return true;
}

//Windows 文件名里不允许出现的字符
bool IsIllegalFileNameChar(wchar_t c)
{
    const wchar_t kIllegal[] = L"\\/:*?\"<>|";
    if (c < L' ')
    {
        return true;
    }
    for (size_t i = 0; kIllegal[i] != L'\0'; ++i)
    {
        if (c == kIllegal[i])
        {
            return true;
        }
    }
    return false;
}

bool IsTrimChar(wchar_t c)
{
    return c == L'.' || c == L' ' || c == L'\t';
}

std::wstring ToLower(const std::wstring& s)
{
    std::wstring out(s);
    for (size_t i = 0; i < out.size(); ++i)
    {
        out[i] = (wchar_t)towlower(out[i]);
    }
    return out;
}

} // namespace

std::wstring HtmlEscape(const std::wstring& text)
{
    std::wstring out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        const wchar_t c = text[i];
        switch (c)
        {
        //字符实体：这几个字符在网页里有特殊含义，原样写出会被当成标签或属性的一部分
        case L'&':
            out += L"&amp;";
            break;
        case L'<':
            out += L"&lt;";
            break;
        case L'>':
            out += L"&gt;";
            break;
        case L'"':
            out += L"&quot;";
            break;
        case L'\'':
            out += L"&#39;";
            break;
        //换行：网页里普通换行不起作用，换成 <br>；回车随换行一起处理，单独出现时丢弃
        case L'\n':
            out += L"<br>";
            break;
        case L'\r':
            break;
        //其余字符原样输出
        default:
            out += c;
            break;
        }
    }
    return out;
}

std::wstring BuildHistoryHtml(const std::wstring& title, const std::vector<HistoryExportRow>& rows)
{
    const std::wstring safeTitle = HtmlEscape(title);
    std::wstring html;
    html += L"<!DOCTYPE html>\n<html lang=\"zh-CN\">\n<head>\n<meta charset=\"utf-8\">\n";
    html += L"<title>" + safeTitle + L"</title>\n";
    html += L"<style>";
    html += kHtmlStyle;
    html += L"</style>\n</head>\n<body>\n";
    html += L"<h1>" + safeTitle + L"</h1>\n";
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const HistoryExportRow& row = rows[i];
        html += L"<div class=\"msg\"><div class=\"meta\"><span class=\"sender\">";
        html += HtmlEscape(row.m_sender);
        html += L"</span><span class=\"time\">";
        html += HtmlEscape(row.m_time);
        html += L"</span></div><div class=\"body\">";
        if (row.m_kind == kHistoryExportImage)
        {
            if (!row.m_imageRelPath.empty())
            {
                html += L"<img src=\"" + HtmlEscape(row.m_imageRelPath) + L"\" alt=\"图片\">";
            }
            else
            {
                html += L"[图片未下载]";
            }
        }
        else if (row.m_kind == kHistoryExportFile)
        {
            html += L"[文件] " + HtmlEscape(row.m_text);
        }
        else
        {
            html += HtmlEscape(row.m_text);
        }
        html += L"</div></div>\n";
    }
    html += L"</body>\n</html>\n";
    return html;
}

std::wstring SanitizeExportFileName(const std::wstring& name)
{
    std::wstring cleaned(name);
    for (size_t i = 0; i < cleaned.size(); ++i)
    {
        if (IsIllegalFileNameChar(cleaned[i]))
        {
            cleaned[i] = L'_';
        }
    }
    //Windows 会把结尾的点和空格去掉，留着会让拼出的路径与磁盘上的文件名对不上；开头的点
    //会让文件变成"隐藏文件"的样子，一并去掉
    size_t begin = 0;
    while (begin < cleaned.size() && IsTrimChar(cleaned[begin]))
    {
        ++begin;
    }
    size_t end = cleaned.size();
    while (end > begin && IsTrimChar(cleaned[end - 1]))
    {
        --end;
    }
    cleaned = cleaned.substr(begin, end - begin);
    return cleaned.empty() ? std::wstring(L"image") : cleaned;
}

std::wstring UniqueExportFileName(const std::wstring& name, std::set<std::wstring>& used)
{
    if (used.insert(ToLower(name)).second)
    {
        return name;
    }
    //拆出扩展名：最后一个点之后的部分；点在开头或没有点时视为没有扩展名
    const size_t dot = name.rfind(L'.');
    const bool hasExt = (dot != std::wstring::npos && dot > 0);
    const std::wstring base = hasExt ? name.substr(0, dot) : name;
    const std::wstring ext = hasExt ? name.substr(dot) : std::wstring();
    //编号从 2 起，与资源管理器复制同名文件时的习惯一致
    const int kFirstSuffix = 2;
    for (int n = kFirstSuffix; ; ++n)
    {
        wchar_t suffix[32] = { 0 };
        swprintf_s(suffix, L" (%d)", n);
        const std::wstring candidate = base + suffix + ext;
        if (used.insert(ToLower(candidate)).second)
        {
            return candidate;
        }
    }
}

std::wstring ExportFilesDirName(const std::wstring& htmlFileName)
{
    const size_t dot = htmlFileName.rfind(L'.');
    const std::wstring base = (dot != std::wstring::npos && dot > 0) ? htmlFileName.substr(0, dot) : htmlFileName;
    return base + L"_files";
}

std::wstring ImageExtensionFromHeader(const unsigned char* head, size_t size)
{
    if (MatchAt(head, size, 0, kPngMagic, sizeof(kPngMagic)))
    {
        return L".png";
    }
    if (MatchAt(head, size, 0, kJpgMagic, sizeof(kJpgMagic)))
    {
        return L".jpg";
    }
    if (MatchAt(head, size, 0, kGifMagic, sizeof(kGifMagic)))
    {
        return L".gif";
    }
    if (MatchAt(head, size, 0, kBmpMagic, sizeof(kBmpMagic)))
    {
        return L".bmp";
    }
    if (MatchAt(head, size, 0, kRiffMagic, sizeof(kRiffMagic))
        && MatchAt(head, size, kWebpTagOffset, kWebpMagic, sizeof(kWebpMagic)))
    {
        return L".webp";
    }
    return L".img";
}

bool ShouldPromptUnloadedBeforeExport(bool hasUnloadedOlder)
{
    //没有未加载的消息时直接导出；有的话导出的只是一部分，先说清楚让用户决定
    return hasUnloadedOlder;
}

} // namespace balloonhistory
