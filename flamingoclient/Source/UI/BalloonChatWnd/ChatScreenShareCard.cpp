/**
 *  ChatScreenShareCard.cpp —— 屏幕共享卡片内容的解析与状态文字，见 ChatScreenShareCard.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "ChatScreenShareCard.h"

#include "ChatCardModel.h"                     // ChatCardChipTone：卡片状态标签的语义
#include "../../net/MessageProtocolClient.h"   // parseScreenShareCardContent
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonchatwnd {

namespace {

//卡片在 text 字段里的摘要：引用回复、复制等按 text 取摘要的地方拿到它，与会话列表摘要一致
const wchar_t* const kCardDigest = TR_NOOP(L"[屏幕共享]");
//没有共享标题的卡片（第 15 期之前）在粗体标题行写的文字
const wchar_t* const kUntitledCardTitle = TR_NOOP(L"屏幕共享");

//UTF-8 → CString（宽字符）
CString Utf8ToWide(const std::string& s)
{
    if (s.empty())
        return CString();
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), NULL, 0);
    if (n <= 0)
        return CString();
    CString out;
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.GetBuffer(n), n);
    out.ReleaseBuffer(n);
    return out;
}

//状态文字
CString StatusText(int roomState)
{
    switch (roomState)
    {
    //这一场就是该会话进行中的共享
    case kScreenShareRoomActive:
        return CString(TR(L"正在共享"));
    //该会话已没有共享，或进行中的是另一场
    case kScreenShareRoomEnded:
        return CString(TR(L"共享已结束"));
    //本次登录还没有收到该会话的状态
    default:
        return CString(TR(L"正在获取状态"));
    }
}

} // namespace

std::string ScreenShareRoomIdOf(const std::string& contentJson)
{
    net::ScreenShareCardContent card;
    if (!net::MessageProtocolClient::parseScreenShareCardContent(contentJson, card))
        return std::string();
    return card.m_roomId;
}

void ApplyScreenShareCard(ChatMessage& msg, const std::string& contentJson, const CString& hostName, int roomState)
{
    msg.kind = kMsgScreenShare;
    msg.text = TR(kCardDigest);
    msg.screenShareContent = contentJson;

    net::ScreenShareCardContent card;
    const bool ok = net::MessageProtocolClient::parseScreenShareCardContent(contentJson, card);
    msg.screenShareRoomId = ok ? card.m_roomId : std::string();
    //发起人名：优先用调用方给的（通讯录、群名片里的名字），其次用发起时写进正文的快照
    msg.screenShareHostName = !hostName.IsEmpty() ? hostName : Utf8ToWide(card.m_hostName);

    if (msg.fromMe)
    {
        msg.screenShareSubtitle = TR(L"我发起的共享");
    }
    else if (!msg.screenShareHostName.IsEmpty())
    {
        msg.screenShareSubtitle = TRF(L"{0}发起的共享", msg.screenShareHostName).c_str();
    }
    else
    {
        msg.screenShareSubtitle = TR(L"对方发起的共享");
    }
    //设了加入密码的共享（第 12 期）：副标题后标出「需要密码」，不显示密码本身
    if (ok && card.m_locked)
        msg.screenShareSubtitle += TR(L" · 需要密码");
    //强制模式的共享（第 13 期）：加入前先确认；副标题下另起说明行写明效果（第 14 期，不再只写一个词）
    msg.screenShareForced = ok && card.m_forced;
    msg.screenShareNotice = msg.screenShareForced ? CString(TR(L"强制模式：加入后全屏观看、不能退出，直到共享结束")) : CString();
    //共享标题与描述（第 15 期）：标题显示在粗体标题行，第 15 期之前的卡片没有标题，照旧写「屏幕共享」
    msg.screenShareTitle = (ok && !card.m_title.empty()) ? Utf8ToWide(card.m_title) : CString(TR(kUntitledCardTitle));
    msg.screenShareDesc = ok ? Utf8ToWide(card.m_desc) : CString();
    //本人发起的卡片在控制条里结束共享，不需要「加入」；内容不可用的卡片没有房间号，加入不了
    msg.screenShareHasButton = !msg.fromMe && !msg.screenShareRoomId.empty();
    ApplyScreenShareState(msg, roomState);
}

void ApplyScreenShareState(ChatMessage& msg, int roomState)
{
    //群通话卡片、一对一通话卡片、群公告卡片、群备忘卡片与云端录像卡片复用这种气泡，但状态各有来源（群通话状态缓存、
    //卡片 content、备忘簿），不在这里重填
    if (msg.kind != kMsgScreenShare || msg.screenShareIsGroupCall || msg.screenShareIsCallCard
        || msg.screenShareIsAnnouncement || msg.screenShareIsMemo || msg.screenShareIsCloudRecord)
        return;
    msg.screenShareState = roomState;
    msg.screenShareStatus = StatusText(roomState);
    //状态标签就是这句状态文字：正在共享为绿，已结束与还不知道为灰（docs/聊天卡片美化方案.md 5.2）
    msg.cardChipText = msg.screenShareStatus;
    msg.cardChipTone = (roomState == kScreenShareRoomActive) ? kCardChipLive : kCardChipEnded;
    msg.cardMetaText.Empty();
    //还不知道状态时也允许点：共享若已结束，服务端拒绝加入后卡片会被重填为已结束
    msg.screenShareButtonEnabled = msg.screenShareHasButton && roomState != kScreenShareRoomEnded;
}

bool ApplyScreenShareInfo(ChatMessage& msg, const std::string& contentJson)
{
    if (msg.kind != kMsgScreenShare || msg.screenShareIsGroupCall || msg.screenShareIsCallCard
        || msg.screenShareIsAnnouncement || msg.screenShareIsMemo || msg.screenShareIsCloudRecord)
        return false;
    net::ScreenShareCardContent card;
    if (!net::MessageProtocolClient::parseScreenShareCardContent(contentJson, card))
        return false;
    //标题与描述的取法与 ApplyScreenShareCard 相同；副标题、状态、按钮与发起人名都不随标题变化
    const CString title = card.m_title.empty() ? CString(TR(kUntitledCardTitle)) : Utf8ToWide(card.m_title);
    const CString desc = Utf8ToWide(card.m_desc);
    if (title == msg.screenShareTitle && desc == msg.screenShareDesc && contentJson == msg.screenShareContent)
        return false;
    msg.screenShareTitle = title;
    msg.screenShareDesc = desc;
    msg.screenShareContent = contentJson;
    return true;
}

} // namespace balloonchatwnd
