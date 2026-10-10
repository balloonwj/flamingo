/**
 *  GroupMustRead.cpp
 *
 *  群「入群必读」的纯逻辑与界面文字，说明见 GroupMustRead.h 与 docs/群公告接入方案.md §14。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "GroupMustRead.h"

#include "GroupNotifyOnly.h"               // CanSpeakInGroup
#include "../../net/Protocol.h"            // group_role_admin
#include "../../Common/I18n/I18n.h"        // TR：界面文字的多语言查找

namespace balloonchatwnd {

int DecideGroupInputBlock(int muteAll, int myRole, bool mustReadPending)
{
    //入群必读优先：本人可以自己解除，先让他看到能做什么
    if (mustReadPending)
    {
        return kGroupInputMustRead;
    }
    if (!CanSpeakInGroup(muteAll, myRole))
    {
        return kGroupInputNotifyOnly;
    }
    return kGroupInputOpen;
}

bool ShouldShowMustReadMark(int myRole, int memberMustRead)
{
    return myRole >= group_role_admin && memberMustRead != 0;
}

bool ShouldAutoPopupMustRead(bool pending, bool hasAnnouncement, bool shownBefore)
{
    return pending && hasAnnouncement && !shownBefore;
}

LPCWSTR MustReadInputHintText()
{
    return TR(L"请先阅读入群必读公告并点「我已阅读」后再发言，点此阅读");
}

std::wstring MustReadSendFailureText()
{
    return TR(L"请先阅读入群必读公告并点「我已阅读」，再在本群发言");
}

LPCWSTR MustReadSenderName()
{
    return TR(L"入群必读");
}

LPCWSTR MustReadCardSubtitle()
{
    return TR(L"入群必读，阅读后才能在本群发言");
}

LPCWSTR MustReadMemberMarkText()
{
    return TR(L"未确认必读");
}

LPCWSTR MustReadPopupCaption()
{
    return TR(L"入群必读");
}

} // namespace balloonchatwnd
