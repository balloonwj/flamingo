/**
 *  GroupNotifyOnly.cpp
 *
 *  群「仅通知模式」的纯逻辑与界面文字，说明见 GroupNotifyOnly.h 与 docs/群管理接入方案.md §14。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "GroupNotifyOnly.h"

#include "../../net/Protocol.h"            // group_role_admin
#include "../../Common/I18n/I18n.h"        // TR：界面文字的多语言查找

namespace balloonchatwnd {

bool CanSpeakInGroup(int muteAll, int myRole)
{
    //开关关闭：谁都能发言
    if (muteAll == 0)
    {
        return true;
    }
    //角色未知时不在客户端拦截，以服务端判断为准（被拒时气泡标为发送失败）
    if (myRole == kGroupRoleUnknown)
    {
        return true;
    }
    //群主与管理员不受仅通知模式限制
    return myRole >= group_role_admin;
}

LPCWSTR NotifyOnlyTitleText()
{
    return TR(L"仅通知模式");
}

LPCWSTR NotifyOnlyDescriptionText()
{
    return TR(L"开启后，仅群主和管理员可以发言，其他成员只能查看消息");
}

LPCWSTR NotifyOnlyInputHintText()
{
    return TR(L"本群为仅通知模式，仅群主和管理员可以发言");
}

LPCWSTR NotifyOnlyBadgeText()
{
    return TR(L"仅通知");
}

} // namespace balloonchatwnd
