/**
 *  转发请求与合并转发标题生成规则的实现，见 ForwardRequest.h。
 *  balloonwj@qq.com   2026-10-01
 */
#include "ForwardRequest.h"

#include "../../net/Protocol.h"   // conversation_group

namespace balloonmain {

namespace {

//标题的后缀与缺省值
const wchar_t* const kTitleSuffix     = L"的聊天记录";
const wchar_t* const kTitleFallback   = L"聊天记录";
const wchar_t* const kGroupNameFallback = L"群聊";

//UTF-16 高位代理的取值范围：落在截断点前一位时，说明截断点切在了一个代理对中间
const wchar_t kHighSurrogateFirst = 0xD800;
const wchar_t kHighSurrogateLast  = 0xDBFF;

//把标题截到 kForwardTitleMaxChars 个字符以内，不留下半个代理对
std::wstring TruncateTitle(const std::wstring& title)
{
    if (title.size() <= kForwardTitleMaxChars)
    {
        return title;
    }
    size_t cut = kForwardTitleMaxChars;
    const wchar_t last = title[cut - 1];
    if (last >= kHighSurrogateFirst && last <= kHighSurrogateLast)
    {
        --cut;
    }
    return title.substr(0, cut);
}

} // anonymous namespace

std::wstring BuildMergedForwardTitle(int srcCtype, const std::wstring& selfName,
                                     const std::wstring& peerOrGroupName)
{
    std::wstring title;
    if (srcCtype == conversation_group)
    {
        title = (peerOrGroupName.empty() ? std::wstring(kGroupNameFallback) : peerOrGroupName)
                + kTitleSuffix;
    }
    else if (!selfName.empty() && !peerOrGroupName.empty())
    {
        title = selfName + L"和" + peerOrGroupName + kTitleSuffix;
    }
    else if (!selfName.empty() || !peerOrGroupName.empty())
    {
        title = (selfName.empty() ? peerOrGroupName : selfName) + kTitleSuffix;
    }
    else
    {
        title = kTitleFallback;
    }
    return TruncateTitle(title);
}

} // namespace balloonmain
