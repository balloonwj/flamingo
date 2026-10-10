/**
 *  转发请求与合并转发标题生成规则的实现，见 ForwardRequest.h。
 *  balloonwj@qq.com   2026-10-01
 */
#include "ForwardRequest.h"

#include "../../net/Protocol.h"   // conversation_group
#include "../../Common/I18n/I18n.h"   // TR / TRF：界面文字的多语言查找

namespace balloonmain {

namespace {

//标题的整句模板与缺省值，只做标记（TR_NOOP），生成标题时再取译文。
//标题随消息发给对方，用的是发送方的界面语言
const wchar_t* const kTitleOneName    = TR_NOOP(L"{0}的聊天记录");        // {0} 为群名或单聊一方的名字
const wchar_t* const kTitleTwoNames   = TR_NOOP(L"{0}和{1}的聊天记录");   // {0} 为自己的名字，{1} 为对方的名字
const wchar_t* const kTitleFallback   = TR_NOOP(L"聊天记录");
const wchar_t* const kGroupNameFallback = TR_NOOP(L"群聊");

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
        title = TRF(kTitleOneName, peerOrGroupName.empty() ? std::wstring(TR(kGroupNameFallback)) : peerOrGroupName);
    }
    else if (!selfName.empty() && !peerOrGroupName.empty())
    {
        title = TRF(kTitleTwoNames, selfName, peerOrGroupName);
    }
    else if (!selfName.empty() || !peerOrGroupName.empty())
    {
        title = TRF(kTitleOneName, selfName.empty() ? peerOrGroupName : selfName);
    }
    else
    {
        title = TR(kTitleFallback);
    }
    return TruncateTitle(title);
}

int DecideDirectFileForward(bool merge, bool hasLocalFile)
{
    //合并转发：记录只是一张卡片，重新上传的文件放不进去；这条照常进入记录，接收方在记录里看到说明文字
    if (merge)
        return kDirectForwardPlaceholder;
    //逐条转发：本机还有文件就以离线文件重新发出，转发出去的文件留在服务器上；本机没有就无法转发
    return hasLocalFile ? kDirectForwardReupload : kDirectForwardSkip;
}

} // namespace balloonmain
