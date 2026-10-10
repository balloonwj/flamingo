/**
 *  聊天记录管理器「链接」页行内容纯逻辑的实现，说明见 HistoryLibraryLinks.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "HistoryLibraryLinks.h"

#include <windows.h>
#include "../BalloonChatWnd/ChatLinkDetect.h"   // balloonchatwnd::DetectLinks

namespace balloonhistory {

namespace {

//URL 里协议与主机之间的分隔符
const wchar_t kSchemeSep[] = L"://";
const size_t kSchemeSepLen = 3;
//显示域名时去掉的前缀
const wchar_t kWwwPrefix[] = L"www.";
const size_t kWwwPrefixLen = 4;
//「复制链接」多个链接之间的分隔
const wchar_t kClipboardLineSep[] = L"\r\n";

//UTF-8 转 UTF-16；空串或转换失败返回空串
std::wstring Utf8ToWideText(const std::string& s)
{
    if (s.empty())
    {
        return std::wstring();
    }
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    if (len <= 0)
    {
        return std::wstring();
    }
    std::wstring w((size_t)len, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], len);
    return w;
}

} // namespace

std::vector<std::wstring> LibraryLinksOfText(const std::string& bodyUtf8)
{
    std::vector<std::wstring> links;
    //「链接」页只汇总网址；屏幕共享的分享链接（第 25 期）是一次性的入口，不收进来
    const std::vector<balloonchatwnd::LinkSpan> spans =
        balloonchatwnd::DetectLinks(Utf8ToWideText(bodyUtf8), true, false);
    for (size_t i = 0; i < spans.size(); ++i)
    {
        if (!spans[i].m_url.empty())
        {
            links.push_back(spans[i].m_url);
        }
    }
    return links;
}

std::wstring LinkDomainOf(const std::wstring& url)
{
    //去掉协议
    size_t begin = url.find(kSchemeSep);
    begin = (begin == std::wstring::npos) ? 0 : begin + kSchemeSepLen;
    //主机部分到第一个 / ? # 为止
    size_t end = url.find_first_of(L"/?#", begin);
    if (end == std::wstring::npos)
    {
        end = url.size();
    }
    std::wstring host = url.substr(begin, end - begin);
    //去掉用户信息（最后一个 '@' 之前）
    const size_t at = host.find_last_of(L'@');
    if (at != std::wstring::npos)
    {
        host = host.substr(at + 1);
    }
    //去掉端口（IPv6 字面量 [..] 里的冒号不算）
    const size_t colon = host.find_last_of(L':');
    const size_t bracket = host.find_last_of(L']');
    if (colon != std::wstring::npos && (bracket == std::wstring::npos || colon > bracket))
    {
        host = host.substr(0, colon);
    }
    for (size_t i = 0; i < host.size(); ++i)
    {
        if (host[i] >= L'A' && host[i] <= L'Z')
        {
            host[i] = (wchar_t)(host[i] - L'A' + L'a');
        }
    }
    //去掉开头的 www.，去掉后为空时保留原样
    if (host.size() > kWwwPrefixLen && host.compare(0, kWwwPrefixLen, kWwwPrefix) == 0)
    {
        host = host.substr(kWwwPrefixLen);
    }
    return host;
}

bool SummarizeLibraryLinks(const std::string& bodyUtf8, HistoryLinkSummary& out)
{
    const std::vector<std::wstring> links = LibraryLinksOfText(bodyUtf8);
    if (links.empty())
    {
        return false;
    }
    out.m_url = links[0];
    out.m_domain = LinkDomainOf(links[0]);
    out.m_linkCount = (int)links.size();
    return true;
}

std::wstring JoinLinksForClipboard(const std::vector<std::wstring>& links)
{
    std::wstring text;
    for (size_t i = 0; i < links.size(); ++i)
    {
        if (i > 0)
        {
            text += kClipboardLineSep;
        }
        text += links[i];
    }
    return text;
}

} // namespace balloonhistory
