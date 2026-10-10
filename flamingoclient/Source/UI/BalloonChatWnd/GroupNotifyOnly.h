/**
 *  GroupNotifyOnly.h
 *
 *  群「仅通知模式」（原称全员禁言，docs/群管理接入方案.md §14）在客户端的纯逻辑与界面文字：开启后只有群主和
 *  管理员可以发言。协议字段仍是 muteall（群资料、1320 群列表、1318 资料变更推送都带），表列仍是 f_muteall。
 *
 *    · CanSpeakInGroup：按开关与本人角色判断能不能发言，群聊窗据此在输入框与提示行之间切换；
 *    · 几条界面文字：开关标题、说明、普通成员输入区的提示、主面板徽章，都集中在这里，几处界面用同一套说法。
 *
 *  典型用法（群聊窗收到群资料或 1318 推送后）：
 *      const bool canSpeak = CanSpeakInGroup(info.m_muteall, m_myRole);
 *      ShowNotifyOnlyBar(!canSpeak);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <windows.h>

namespace balloonchatwnd {

// 本人在群内角色未知（群资料尚未取到）时的取值，与群聊窗 m_myRole 的初值相同
const int kGroupRoleUnknown = -1;

/**
 *  判断本人在群里能不能发言（只看仅通知模式，单人禁言另由服务端判断）。
 *    muteAll：仅通知模式开关，0 关、非 0 开。
 *    myRole： 本人角色，取值见 group_member_role（0 成员、1 管理员、2 群主）；kGroupRoleUnknown 表示未知。
 *  @return 能发言返回 true。开关关闭、本人是群主或管理员、角色未知（以服务端判断为准，不在客户端拦截）时都为 true。
 */
bool CanSpeakInGroup(int muteAll, int myRole);

/**
 *  群管理窗开关一行的标题：「仅通知模式」。
 */
LPCWSTR NotifyOnlyTitleText();

/**
 *  群管理窗开关一行的说明与 tooltip：「开启后，仅群主和管理员可以发言，其他成员只能查看消息」。
 */
LPCWSTR NotifyOnlyDescriptionText();

/**
 *  普通成员群聊窗输入区换成的提示，也用作发送被拒（306）时的提示：「本群为仅通知模式，仅群主和管理员可以发言」。
 */
LPCWSTR NotifyOnlyInputHintText();

/**
 *  主面板群名后面的灰色小标签：「仅通知」。
 */
LPCWSTR NotifyOnlyBadgeText();

} // namespace balloonchatwnd
