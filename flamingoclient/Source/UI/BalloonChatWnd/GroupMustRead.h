/**
 *  GroupMustRead.h
 *
 *  群「入群必读」（docs/群公告接入方案.md §14）在客户端的纯逻辑与界面文字：群主或管理员把一条公告设为入群必读后，
 *  之后加入的成员要先阅读并点「我已阅读」才能发言。
 *
 *    · DecideGroupInputBlock：结合仅通知模式与本人是否待确认，决定群聊窗输入区显示输入框还是哪一种提示行；
 *    · ShouldShowMustReadMark：成员边栏里「未确认必读」标记只给群主与管理员看；
 *    · 几条界面文字：输入区提示、发送被拒（315）的提示、必读卡片的发送人名与说明，几处界面用同一套说法。
 *
 *  典型用法（群聊窗收到 1335 应答或 1318 推送后）：
 *      const int block = DecideGroupInputBlock(m_muteAll, m_myRole, m_mustReadPending);
 *      ShowInputBlock(block);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <windows.h>
#include <string>

namespace balloonchatwnd {

// 群聊窗输入区的状态
enum GroupInputBlock
{
    // 可以发言：显示工具栏、输入框与发送按钮
    kGroupInputOpen = 0,
    // 本群开启了仅通知模式、本人是普通成员：输入区换成灰字提示「本群为仅通知模式……」
    kGroupInputNotifyOnly = 1,
    // 本人尚未确认入群必读：输入区换成可点的提示「请先阅读入群必读……点此阅读」
    kGroupInputMustRead = 2
};

/**
 *  决定群聊窗输入区的状态。两种限制同时存在时先显示入群必读：它是本人可以自己解除的，确认之后
 *  若仍是仅通知模式，再换成仅通知的提示。
 *    muteAll：        仅通知模式开关，0 关、非 0 开。
 *    myRole：         本人角色，取值见 group_member_role；kGroupRoleUnknown（-1）表示未知。
 *    mustReadPending：本人是否尚未确认入群必读（cmd_get_must_read 应答的 pending）。
 *  @return 取值见 GroupInputBlock
 */
int DecideGroupInputBlock(int muteAll, int myRole, bool mustReadPending);

/**
 *  成员边栏里某一行要不要画「未确认必读」标记：只有群主与管理员看得到，且只标尚未确认的成员。
 *    myRole：        本人角色，取值见 group_member_role，负值表示未知。
 *    memberMustRead：该成员的 mustread 字段（成员列表应答），1 表示尚未确认。
 */
bool ShouldShowMustReadMark(int myRole, int memberMustRead);

/**
 *  进入群聊窗时要不要自动弹出入群必读：本人待确认、必读公告的内容已取到、本窗口还没弹过。
 *  弹过一次之后，本人关掉弹窗不再自动弹，可点输入区的提示重新打开。
 */
bool ShouldAutoPopupMustRead(bool pending, bool hasAnnouncement, bool shownBefore);

/**
 *  输入区换成的提示（可点）：「请先阅读入群必读公告并点「我已阅读」后再发言，点此阅读」。
 */
LPCWSTR MustReadInputHintText();

/**
 *  发送被拒（315 error_code_must_read_pending）时的提示，不带句号：「请先阅读入群必读公告并点「我已阅读」，再在本群发言」。
 */
std::wstring MustReadSendFailureText();

/**
 *  必读卡片（服务端以系统名义、只发给新成员本人的公告卡片）在消息区里显示的发送人名：「入群必读」。
 */
LPCWSTR MustReadSenderName();

/**
 *  必读卡片的副标题：「入群必读，阅读后才能在本群发言」。
 */
LPCWSTR MustReadCardSubtitle();

/**
 *  成员边栏与群管理成员页上的标记文字：「未确认必读」。
 */
LPCWSTR MustReadMemberMarkText();

/**
 *  入群必读弹窗的标题：「入群必读」。
 */
LPCWSTR MustReadPopupCaption();

} // namespace balloonchatwnd
