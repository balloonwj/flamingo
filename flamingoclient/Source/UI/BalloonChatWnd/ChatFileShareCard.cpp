/**
 *  ChatFileShareCard.cpp —— 文件共享卡片内容的解析与状态文字，见 ChatFileShareCard.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-03
 */

// ---- ATL 头文件要求的前置依赖，与同目录 ChatMergedCard.cpp 相同 ----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>

#include "ChatFileShareCard.h"
#include "../../net/MessageProtocolClient.h"   // parseShareCardContent
#include "../../net/Protocol.h"                // share_kind / share_state / 原因
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonchatwnd {

namespace {

//以下文字只做标记（TR_NOOP），使用处再用 TR / TRF 取译文
//卡片的 text 字段前缀：引用回复、复制等按 text 取摘要的地方拿到「[共享] 名称」，与会话列表摘要一致
const wchar_t* const kShareTypeText = TR_NOOP(L"[共享]");
//content 不可用时的名称
const wchar_t* const kDefaultShareName = TR_NOOP(L"共享");
//本人发出的卡片的第三行前半
const wchar_t* const kMineOwnerLine = TR_NOOP(L"我的共享");
//别人发出的卡片的第三行前半，{0} 为分享者名字
const wchar_t* const kOwnerLineTemplate = TR_NOOP(L"{0}的共享");
//暂不可用、已失效两种状态带原因时的整句，{0} 为原因
const wchar_t* const kUnavailableTemplate = TR_NOOP(L"暂不可用：{0}");
const wchar_t* const kInvalidTemplate = TR_NOOP(L"已失效：{0}");

//UTF-8 转 CString（UTF-16）。空串安全
CString Utf8ToCStr(const std::string& utf8)
{
    if (utf8.empty())
        return CString();
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), NULL, 0);
    if (len <= 0)
        return CString();
    CString out;
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), out.GetBuffer(len), len);
    out.ReleaseBuffer(len);
    return out;
}

//暂不可用的原因
CString UnavailableReasonText(int reason)
{
    switch (reason)
    {
    //分享者不在线
    case share_unavailable_owner_offline:
        return TR(L"分享者不在线");
    //分享者在别的电脑上登录
    case share_unavailable_other_device:
        return TR(L"分享者不在共享所在的电脑上");
    //共享根不存在
    case share_unavailable_root_missing:
        return TR(L"共享的内容在分享者电脑上已不存在");
    //功能被关闭
    case share_unavailable_feature_disabled:
        return TR(L"文件共享功能已被管理员关闭");
    //其余原因：只写「暂不可用」
    default:
        return CString();
    }
}

//已失效的原因
CString InvalidReasonText(int reason)
{
    switch (reason)
    {
    //分享者取消
    case share_invalid_owner_canceled:
        return TR(L"分享者已取消共享");
    //群主或管理员取消
    case share_invalid_group_admin_canceled:
        return TR(L"群主或管理员已取消共享");
    //后台管理员取消
    case share_invalid_admin_canceled:
        return TR(L"管理员已取消共享");
    //群已解散
    case share_invalid_group_dismissed:
        return TR(L"群已解散");
    //分享者退群
    case share_invalid_owner_left_group:
        return TR(L"分享者已退出群聊");
    //本人被移出成员
    case share_invalid_member_removed:
        return TR(L"你已不是这个共享的成员");
    //本人已不在群里
    case share_invalid_member_left_group:
        return TR(L"你已不在这个群里");
    //其余原因：只写「已失效」
    default:
        return CString();
    }
}

} // namespace

CString ShareCardStatusText(bool moduleReady, bool hasState, int state, int reason)
{
    if (!moduleReady)
        return TR(L"当前客户端未启用共享功能");
    if (!hasState || state == 0)
        return TR(L"正在获取状态");
    if (state == share_state_available)
        return TR(L"可用");
    if (state == share_state_unavailable)
    {
        const CString why = UnavailableReasonText(reason);
        return why.IsEmpty() ? CString(TR(L"暂不可用")) : CString(TRF(kUnavailableTemplate, why).c_str());
    }
    const CString why = InvalidReasonText(reason);
    return why.IsEmpty() ? CString(TR(L"已失效")) : CString(TRF(kInvalidTemplate, why).c_str());
}

LPCWSTR ShareCardButtonLabel(int button)
{
    switch (button)
    {
    //磁盘与文件夹
    case kShareCardButtonOpen:
        return TR(L"打开");
    //单个文件
    case kShareCardButtonDownload:
        return TR(L"下载");
    //本人发起的共享
    case kShareCardButtonOpenLocal:
        return TR(L"在本机打开");
    //内容不可用
    default:
        return NULL;
    }
}

void ApplyFileShareCard(ChatMessage& msg, const std::string& contentJson, const CString& ownerName,
                        const ShareCardLiveState& live)
{
    msg.kind = kMsgFileShare;
    msg.shareContent = contentJson;
    msg.shareIsOwner = msg.fromMe;
    msg.shareOwnerLine = msg.fromMe ? CString(TR(kMineOwnerLine)) : CString(TRF(kOwnerLineTemplate, ownerName).c_str());

    net::ShareCardContent card;
    if (!net::MessageProtocolClient::parseShareCardContent(contentJson, card))
    {
        msg.shareId = 0;
        msg.shareKind = 0;
        msg.shareName = TR(kDefaultShareName);
        msg.sharePath.Empty();
        msg.shareButton = kShareCardButtonNone;
        msg.shareButtonEnabled = false;
        msg.shareState = 0;
        msg.shareStatus = ShareCardStatusText(live.m_moduleReady, false, 0, 0);
        msg.text = TR(kShareTypeText);
        return;
    }

    msg.shareId = card.m_shareId;
    msg.shareKind = card.m_kind;
    msg.shareName = Utf8ToCStr(card.m_name);
    //分享者的真实路径只给分享者本人看（2026-10-04 起）：服务端只在发给本人的卡片里带路径，这里再挡一道，
    //别人的卡片即使带着路径（如服务端升级前缓存在本地库里的旧卡片）也不显示
    if (msg.shareIsOwner)
        msg.sharePath = Utf8ToCStr(card.m_displayPath);
    else
        msg.sharePath.Empty();
    msg.text = CString(TR(kShareTypeText)) + L" " + msg.shareName;
    //本人发起的共享：在本机打开（文件就在本机上，不经中转）
    if (msg.shareIsOwner)
        msg.shareButton = kShareCardButtonOpenLocal;
    else
        msg.shareButton = (card.m_kind == share_kind_file) ? kShareCardButtonDownload : kShareCardButtonOpen;

    //DLL 手里的最新状态优先，其次是服务端合并进 content 的状态
    if (live.m_hasState)
        ApplyFileShareState(msg, live.m_moduleReady, live.m_state, live.m_reason);
    else if (card.m_hasState)
        ApplyFileShareState(msg, live.m_moduleReady, card.m_state, card.m_reason);
    else
        ApplyFileShareState(msg, live.m_moduleReady, 0, 0);
}

void ApplyFileShareState(ChatMessage& msg, bool moduleReady, int state, int reason)
{
    if (msg.kind != kMsgFileShare)
        return;
    msg.shareState = state;
    msg.shareStatus = ShareCardStatusText(moduleReady, state != 0, state, reason);
    //按钮：本客户端加载了模块、共享没有失效、本企业的功能没有关闭才可点；状态还不知道时也允许点，浏览窗口里会显示实际情况
    const bool featureOff = (state == share_state_unavailable && reason == share_unavailable_feature_disabled);
    msg.shareButtonEnabled = moduleReady && msg.shareButton != kShareCardButtonNone && msg.shareId > 0
        && state != share_state_invalid && !featureOff;
}

} // namespace balloonchatwnd
