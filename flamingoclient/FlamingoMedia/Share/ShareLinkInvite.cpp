/**
 *  屏幕共享分享链接的文字，说明见 ShareLinkInvite.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "ShareLinkInvite.h"

#include <string.h>
#include <wchar.h>
#include "../../Source/Common/I18n/I18n.h"   // TR / TRF：界面文字的多语言查找

namespace screenshare {

namespace {

//分享链接的前缀（与 Source/UI/BalloonChatWnd/ChatLinkDetect.cpp 的识别前缀一致，由单元测试核对）
const wchar_t kShareLinkPrefix[] = L"flamingo://screenshare/";
//前缀的长度（字符，不含结尾的 '\0'）
const size_t kShareLinkPrefixLen = sizeof(kShareLinkPrefix) / sizeof(kShareLinkPrefix[0]) - 1;
//链接码的字符表：与服务端 ScreenShareLinkTable 相同，数字 2~9、去掉 I O 的大写字母、去掉 l o 的小写字母
const wchar_t kCodeAlphabet[] = L"23456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz";

//强制模式的说明（接在凭链接加入的确认正文之后）。只做标记，使用处再用 TR 取译文
const wchar_t* const kLinkForcedTail =
    TR_NOOP(L"这是强制观看的屏幕共享：加入后要等发起人结束共享才能离开，观看窗会一直全屏显示在最前面。");

//不区分大小写地比较 url 的开头是否为前缀
bool startsWithNoCase(const std::wstring& url, const wchar_t* prefix, size_t prefixLen)
{
    if (url.size() < prefixLen)
        return false;
    return _wcsnicmp(url.c_str(), prefix, prefixLen) == 0;
}

} // namespace

bool IsValidShareLinkCode(const std::wstring& code)
{
    if (code.size() != kShareLinkCodeLength)
        return false;
    for (size_t i = 0; i < code.size(); ++i)
    {
        //'\0' 也在字符表的末尾，须先排除
        if (code[i] == L'\0' || wcschr(kCodeAlphabet, code[i]) == NULL)
            return false;
    }
    return true;
}

std::wstring MakeShareLinkUrl(const std::string& code)
{
    //链接码只含 ASCII 字母数字，逐字节转宽字符即可
    const std::wstring wide(code.begin(), code.end());
    if (!IsValidShareLinkCode(wide))
        return std::wstring();
    return std::wstring(kShareLinkPrefix) + wide;
}

bool ParseShareLinkUrl(const std::wstring& url, std::string& outCode)
{
    if (!startsWithNoCase(url, kShareLinkPrefix, kShareLinkPrefixLen))
        return false;
    const std::wstring code = url.substr(kShareLinkPrefixLen);
    if (!IsValidShareLinkCode(code))
        return false;
    outCode.assign(code.begin(), code.end());
    return true;
}

std::wstring MakeShareInviteText(const std::wstring& title, const std::wstring& url, const std::wstring& password)
{
    //{0} 为共享标题，{1} 为分享链接
    std::wstring text = title.empty() ? TRF(L"邀请你观看屏幕共享：{0}", url)
                                      : TRF(L"邀请你观看屏幕共享「{0}」：{1}", title, url);
    if (!password.empty())
    {
        //{0} 为 6 位数字的加入密码
        text += L"\n";
        text += TRF(L"密码：{0}", password);
    }
    return text;
}

int EstimateShareTextLines(const std::wstring& text, int charsPerLine, int minLines, int maxLines)
{
    if (charsPerLine <= 0)
        charsPerLine = 1;
    int lines = 0;
    size_t start = 0;
    for (;;)
    {
        const size_t nl = text.find(L'\n', start);
        const size_t len = ((nl == std::wstring::npos) ? text.size() : nl) - start;
        //一段占的行数：向上取整，空段也算一行
        lines += (len == 0) ? 1 : static_cast<int>((len + static_cast<size_t>(charsPerLine) - 1) / charsPerLine);
        if (nl == std::wstring::npos)
            break;
        start = nl + 1;
    }
    if (lines < minLines)
        lines = minLines;
    if (lines > maxLines)
        lines = maxLines;
    return lines;
}

std::wstring MakeShareLinkJoinConfirmText(const std::wstring& hostName, const std::wstring& title,
                                          const std::wstring& desc, bool locked, bool forced)
{
    //{0} 为发起人显示名
    std::wstring text = TRF(L"加入 {0} 的屏幕共享？", hostName.empty() ? std::wstring(TR(L"对方")) : hostName);
    text += L"\n";
    if (!title.empty())
    {
        //标题加的引号随语言翻译
        text += L"\n";
        text += TRF(L"「{0}」", title);
        if (!desc.empty())
        {
            text += L"\n";
            text += desc;
        }
    }
    if (locked)
    {
        text += L"\n\n";
        text += TR(L"这场共享设了密码，加入时需要输入密码。");
    }
    if (forced)
    {
        text += L"\n\n";
        text += TR(kLinkForcedTail);
    }
    return text;
}

} // namespace screenshare
