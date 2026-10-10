/**
 *  ChatLinkDetect.cpp
 *
 *  聊天正文超链接识别的实现。识别口径与取舍见 ChatLinkDetect.h 的文件头。
 *
 *  本文件被 FlamingoUnit 测试工程直接编译，故<u>不</u>走主工程的预编译头
 *  （两处工程文件里都登记为 NotUsing），不依赖 ATL / Windows 头。
 *
 *  balloonwj@qq.com   2026-08-11
 */

#include "ChatLinkDetect.h"

#include <string.h>
#include <wchar.h>   // wcschr / wcslen：分享链接的前缀与链接码（第 25 期）
#include "../../net/GroupLinkProtocolClient.h"   // parseJoinUrl：加群链接的判定（docs/邀请入群方案.md §12）

namespace balloonchatwnd {

namespace {

//内联表情在扁平文本里的占位字符（与 ChatInlineText / ChatTextSelection 同一口径）。
const wchar_t kFacePlaceholder = 0xFFFC;

//屏幕共享分享链接的前缀（第 25 期），全小写，MatchesAt 按小写比较；与 FlamingoMedia/Share/ShareLinkInvite.cpp 的前缀一致，
//由单元测试核对
const wchar_t kShareLinkPrefix[] = L"flamingo://screenshare/";
//加群链接的前缀（全小写，docs/邀请入群方案.md §12）；随机码之后的判定交给 net::GroupLinkProtocolClient::parseJoinUrl
const wchar_t kJoinLinkPrefix[] = L"flamingo://joingroup?code=";
//分享链接码的长度与字符表：数字 2~9、去掉 I O 的大写字母、去掉 l o 的小写字母（与服务端 ScreenShareLinkTable 一致）
const std::wstring::size_type kShareLinkCodeChars = 10;
const wchar_t kShareLinkCodeAlphabet[] = L"23456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz";

//ASCII 可打印区间的两端，用来判断"这个字符还算不算 URL 的一部分"。
const wchar_t kAsciiPrintableFirst = 0x21;   // '!'：空格及以下的控制字符都终止链接
const wchar_t kAsciiPrintableLast  = 0x7E;   // '~'：再往上是 DEL 与非 ASCII，一律终止

/**
 *  把一个字符转成小写（只处理 ASCII 大写字母）。
 *  URL 的协议名与 www 前缀都是纯 ASCII，不需要走区域相关的大小写映射。
 */
wchar_t LowerAscii(wchar_t ch)
{
    if (ch >= L'A' && ch <= L'Z')
    {
        return (wchar_t)(ch - L'A' + L'a');
    }
    return ch;
}

/**
 *  text 从 pos 起是否（忽略大小写地）以 token 开头。token 须为全小写 ASCII。
 */
bool MatchesAt(const std::wstring& text, std::wstring::size_type pos, const wchar_t* token)
{
    std::wstring::size_type i = 0;
    for (; token[i] != L'\0'; ++i)
    {
        if (pos + i >= text.size())
        {
            return false;
        }
        if (LowerAscii(text[pos + i]) != token[i])
        {
            return false;
        }
    }
    return true;
}

/**
 *  该字符是否可以作为链接正文的一部分。
 *
 *  只放行 ASCII 可打印字符：空白、控制字符、表情占位符、以及一切非 ASCII
 *  （中文、中文标点、全角符号）都终止链接。理由见头文件里的取舍说明。
 */
bool IsUrlChar(wchar_t ch)
{
    if (ch == kFacePlaceholder)
    {
        return false;
    }
    if (ch < kAsciiPrintableFirst || ch > kAsciiPrintableLast)
    {
        return false;
    }
    //反斜杠不算 URL 字符：正文里出现的多半是 Windows 路径（含 UNC），不该被
    //粘进链接。正经 URL 用的是正斜杠。
    if (ch == L'\\')
    {
        return false;
    }
    return true;
}

/**
 *  链接<u>起点</u>前一个字符是否构成合法边界。
 *
 *  用来挡住 "xhttp://a.com"、"awww.a.com"、"mail@www.a.com" 这类把链接头粘在
 *  别的词里的情形 —— 前一个字符是字母 / 数字 / '@' / '.' / '-' 时都不算边界。
 */
bool IsBoundaryBefore(const std::wstring& text, std::wstring::size_type pos)
{
    if (pos == 0)
    {
        return true;
    }
    const wchar_t prev = text[pos - 1];
    if ((prev >= L'a' && prev <= L'z') || (prev >= L'A' && prev <= L'Z')
        || (prev >= L'0' && prev <= L'9'))
    {
        return false;
    }
    if (prev == L'@' || prev == L'.' || prev == L'-' || prev == L'/' || prev == L'\\')
    {
        return false;
    }
    return true;
}

/**
 *  把链接尾部多余的标点剥掉，返回剥完之后的长度。
 *
 *    text：  扁平文本。
 *    begin： 链接起点下标。
 *    end：   链接终点下标（右开）。
 *  @return 剥完尾随标点后的终点下标（右开）；可能等于 begin（整段都是标点）。
 */
std::wstring::size_type TrimTrailingPunct(const std::wstring& text,
                                          std::wstring::size_type begin,
                                          std::wstring::size_type end)
{
    while (end > begin)
    {
        const wchar_t last = text[end - 1];

        //一定要剥的尾随标点：句读、引号之类，正常不会是 URL 的最后一个字符。
        if (last == L'.' || last == L',' || last == L';' || last == L':'
            || last == L'!' || last == L'?' || last == L'\'' || last == L'"'
            || last == L'<' || last == L'>')
        {
            --end;
            continue;
        }

        //右括号只在"链接里没有与之配对的左括号"时才剥：
        //  (https://a.com)      → 末尾的 ) 是正文的括号，要剥
        //  https://a.com/x(y)   → 末尾的 ) 与 ( 配对，是 URL 的一部分，不能剥
        if (last == L')' || last == L']' || last == L'}')
        {
            const wchar_t open = (last == L')') ? L'(' : ((last == L']') ? L'[' : L'{');
            int balance = 0;
            for (std::wstring::size_type i = begin; i + 1 < end; ++i)
            {
                if (text[i] == open)
                {
                    ++balance;
                }
                else if (text[i] == last)
                {
                    --balance;
                }
            }
            if (balance <= 0)
            {
                --end;      // 没有等着被闭合的左括号，说明这个右括号属于正文
                continue;
            }
        }
        break;
    }
    return end;
}

} // namespace

std::vector<LinkSpan> DetectLinks(const std::wstring& flatText)
{
    return DetectLinks(flatText, true, true);
}

bool IsScreenShareLinkUrl(const std::wstring& url)
{
    if (!MatchesAt(url, 0, kShareLinkPrefix))
    {
        return false;
    }
    const std::wstring::size_type prefixLen = wcslen(kShareLinkPrefix);
    if (url.size() != prefixLen + kShareLinkCodeChars)
    {
        return false;
    }
    for (std::wstring::size_type k = prefixLen; k < url.size(); ++k)
    {
        //'\0' 也在字符表的末尾，须先排除
        if (url[k] == L'\0' || wcschr(kShareLinkCodeAlphabet, url[k]) == NULL)
        {
            return false;
        }
    }
    return true;
}

bool IsJoinGroupLinkUrl(const std::wstring& url, std::string* code)
{
    if (!MatchesAt(url, 0, kJoinLinkPrefix))
    {
        return false;
    }
    std::string parsed;
    if (!net::GroupLinkProtocolClient::parseJoinUrl(url, parsed))
    {
        return false;
    }
    //整段必须正好是前缀加随机码：带了别的参数的不当作气泡里的链接
    if (url.size() != wcslen(kJoinLinkPrefix) + parsed.size())
    {
        return false;
    }
    if (code != NULL)
    {
        *code = parsed;
    }
    return true;
}

std::vector<LinkSpan> DetectLinks(const std::wstring& flatText, bool webLinks, bool shareLinks)
{
    std::vector<LinkSpan> out;
    std::wstring::size_type i = 0;
    while (i < flatText.size())
    {
        int  scheme     = kLinkSchemeHttps;
        int  prefixLen  = 0;
        bool matched    = false;

        //先看协议头。https 要排在 http 前面判断吗？不必 —— 两者前缀不同
        //（"https://" 与 "http://" 在第 5 个字符上就分岔），各判各的即可。
        if (webLinks && MatchesAt(flatText, i, L"https://"))
        {
            scheme    = kLinkSchemeHttps;
            prefixLen = 8;
            matched   = true;
        }
        else if (webLinks && MatchesAt(flatText, i, L"http://"))
        {
            scheme    = kLinkSchemeHttp;
            prefixLen = 7;
            matched   = true;
        }
        else if (webLinks && MatchesAt(flatText, i, L"www."))
        {
            scheme    = kLinkSchemeBare;
            prefixLen = 4;
            matched   = true;
        }
        //屏幕共享的分享链接（第 25 期）
        else if (shareLinks && MatchesAt(flatText, i, kShareLinkPrefix))
        {
            scheme    = kLinkSchemeScreenShare;
            prefixLen = (int)wcslen(kShareLinkPrefix);
            matched   = true;
        }
        //加群链接（docs/邀请入群方案.md §12）
        else if (shareLinks && MatchesAt(flatText, i, kJoinLinkPrefix))
        {
            scheme    = kLinkSchemeJoinGroup;
            prefixLen = (int)wcslen(kJoinLinkPrefix);
            matched   = true;
        }

        if (!matched || !IsBoundaryBefore(flatText, i))
        {
            ++i;
            continue;
        }

        //吃掉协议头之后的部分，直到遇到终止字符。
        std::wstring::size_type end = i + (std::wstring::size_type)prefixLen;
        while (end < flatText.size() && IsUrlChar(flatText[end]))
        {
            ++end;
        }

        //协议头后面一个字符都没有（如正文里只写了 "http://"）：不算链接。
        if (end == i + (std::wstring::size_type)prefixLen)
        {
            i = end;
            continue;
        }

        end = TrimTrailingPunct(flatText, i, end);
        if (end <= i + (std::wstring::size_type)prefixLen)
        {
            //剥完标点之后主机部分空了（如 "http://."）：同样不算链接。
            i = i + (std::wstring::size_type)prefixLen;
            continue;
        }

        LinkSpan span;
        span.m_off    = (int)i;
        span.m_len    = (int)(end - i);
        span.m_scheme = scheme;
        span.m_url    = flatText.substr(i, end - i);
        //分享链接只认合规的链接码：多一位、少一位或夹了别的字符都当普通文字
        if (scheme == kLinkSchemeScreenShare && !IsScreenShareLinkUrl(span.m_url))
        {
            i = end;
            continue;
        }
        //加群链接同样只认合规的随机码
        if (scheme == kLinkSchemeJoinGroup && !IsJoinGroupLinkUrl(span.m_url, NULL))
        {
            i = end;
            continue;
        }
        if (scheme == kLinkSchemeBare)
        {
            //正文里没写协议，补一个 https —— 现在的站点基本都上了 TLS，补 http
            //反而更容易撞上跳转。
            span.m_url = L"https://" + span.m_url;
        }
        out.push_back(span);

        i = end;
    }
    return out;
}

namespace {

//进程级识别开关的当前值。默认开 —— 未登录 / 未读到配置时也按"识别"处理，与
//设置项的默认值一致。只在 UI 线程读写。
bool g_linkDetectEnabled = true;

} // namespace

bool ChatLinkOptionEnabled()
{
    return g_linkDetectEnabled;
}

void SetChatLinkOptionEnabled(bool enabled)
{
    g_linkDetectEnabled = enabled;
}

bool IsSafeLinkUrl(const std::wstring& url)
{
    //只放行 http / https。file、javascript、data、UNC 路径一律拦下 —— 一条消息
    //不该能让对方点开本地程序或访问内网共享。
    if (MatchesAt(url, 0, L"http://"))
    {
        return url.size() > 7;
    }
    if (MatchesAt(url, 0, L"https://"))
    {
        return url.size() > 8;
    }
    return false;
}

} // namespace balloonchatwnd
