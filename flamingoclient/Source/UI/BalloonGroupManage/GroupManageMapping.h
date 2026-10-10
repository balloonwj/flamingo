/**
 *  GroupManageMapping.h
 *
 *  群管理窗口的纯逻辑层：把服务端回来的协议结构（net::GroupInfoResult /
 *  net::GroupMembersResult / net::GroupJoinRequestsResult）映射成界面用的
 *  GroupManageModel，并给出角色门控与成员右键菜单的可用项计算。
 *
 *  之所以从窗口类里抽出来单独成模块：这几件事全是"给定输入算出输出"的纯计算，
 *  但恰恰又是最容易写错、且靠手工点界面最难覆盖全的部分 —— 光是成员右键菜单
 *  的可用项就有"登录者三种角色 × 目标三种角色 × 是否已禁言 × 是否是自己"这么
 *  多格。抽出来之后可以被 FlamingoUnit 直接调用、逐格断言。
 *
 *  本文件不 include stdafx.h，与 FlamingoUnit 测试工程逐字共用同一份源码
 *  （Flamingo.vcxproj 里对本文件把预编译头设为 NotUsing）。
 *
 *  balloonwj@qq.com   2026-07-31
 */

#pragma once

#include <windows.h>
#include <atlstr.h>
#include <stdint.h>
#include <string>
#include <vector>

#include "GroupManageModel.h"
#include "../../net/GroupProtocolClient.h"
#include "../../net/GroupLinkProtocolClient.h"   // net::GroupLinksResult（加群链接列表应答）

namespace balloongroupmanage {

// =====================================================================
// 成员右键菜单
// =====================================================================

// 成员卡右键菜单的命令 id。数值从 1 起，0 留给"用户没选任何项"。
enum MemberMenuId : UINT
{
    kMemberMenuSendMsg     = 1,   // 发消息：打开与该成员的单聊窗
    kMemberMenuProfile     = 2,   // 查看资料：打开该成员的资料窗
    kMemberMenuSetAdmin    = 3,   // 设为管理员（仅群主可见，且目标是普通成员时）
    kMemberMenuCancelAdmin = 4,   // 撤销管理员（仅群主可见，且目标是管理员时）
    kMemberMenuMute        = 5,   // 禁言（目标未被禁言时）
    kMemberMenuUnmute      = 6,   // 解除禁言（目标已被禁言时）
    kMemberMenuKick        = 7,   // 移出本群
    kMemberMenuReleaseMustRead = 8 // 代为确认入群必读（群主 / 管理员可见，且目标尚未确认，docs/群公告接入方案.md §14）
};

// 计算菜单可用项时的输入状态。
struct MemberMenuState
{
    int  m_myRole;      // 登录者在本群的角色，取值见 GroupRole
    int  m_targetRole;  // 目标成员的角色，取值见 GroupRole
    bool m_targetMuted; // 目标成员当前是否被禁言
    bool m_isSelf;      // 目标是不是登录者自己
    bool m_targetMustRead; // 目标是否尚未确认入群必读

    MemberMenuState()
        : m_myRole(kRoleMember)
        , m_targetRole(kRoleMember)
        , m_targetMuted(false)
        , m_isSelf(false)
        , m_targetMustRead(false)
    {
    }
};

// 菜单里的一行。
struct MemberMenuEntry
{
    UINT           m_id;          // 命令 id，取值见 MemberMenuId；分隔线为 0
    bool           m_isSeparator; // true 表示这一行是分隔线，其余字段无意义
    const wchar_t* m_label;       // 显示文案（编译期常量，不需释放）

    MemberMenuEntry()
        : m_id(0)
        , m_isSeparator(false)
        , m_label(NULL)
    {
    }
};

/**
 *  算出成员卡右键菜单该显示哪些项。
 *
 *  规则（与服务端 ChatSession 里的权限校验一一对应；服务端校验是权威，这里
 *  只是不让用户白点）：
 *    · 对自己右键 —— 只有"查看资料"。既不能给自己发消息，也不能自己禁言自己。
 *    · 发消息 / 查看资料 —— 任何人对任何人都可用。
 *    · 设 / 撤管理员 —— 仅群主可用，且目标不能是群主。
 *    · 禁言 / 解禁 —— 群主可对任何非群主成员用；管理员只能对普通成员用
 *      （不能禁言其他管理员）；普通成员不可用。
 *    · 移出本群 —— 同上，且任何人不能移出群主。
 *
 *  不可用的项<u>不出现</u>，而不是灰着显示。这类操作菜单里灰项一多就看不清能
 *  干什么，而权限在一次会话里通常是稳定的，用户不需要"知道自己缺什么权限"。
 *
 *    state：登录者与目标成员的角色和状态。
 *  返回：菜单行列表，含分隔线；调用方按序 AppendItem / AppendSeparator 即可。
 *        纯函数，无副作用，便于单元测试。
 */
std::vector<MemberMenuEntry> BuildMemberMenuModel(const MemberMenuState& state);

/**
 *  由成员卡的 ctrl id 反算它对应的成员在模型 m_members 里的下标。
 *
 *  成员卡建卡时把下标编进了 ctrl id（见 GroupManageLayout 的 MakeMemberCard：
 *  id = kIdMemberCardBase + 下标）。右键弹菜单时反过来用它认出点的是哪一位。
 *  做成纯函数是为了能单测边界 —— 右键完全可能点在别的控件上，那时必须认出
 *  "这不是成员卡"，而不是把它误算成某个成员。
 *
 *    ctrlId：被右键的控件 id。
 *
 *  @return 成员下标；ctrlId 不在成员卡 id 区间内时返回 -1。纯函数，无副作用。
 */
int MemberIndexFromCardId(UINT ctrlId);

// =====================================================================
// 协议结构 → 界面模型
// =====================================================================

/**
 *  把开窗时就已知的群上下文写进界面模型：群号、群名、本人角色，以及由群名和
 *  群ID算出的占位头像（群名首字 + 按群ID取的底色）。
 *
 *  窗口打开的那一刻，主面板手里已经有这几项，不必等群资料应答（1303）。其余
 *  内容（成员、申请、禁言、群设置）一律不碰，等各自的应答到达后再填，对应的
 *  加载状态保持原值（新建的模型为 kLoadPending）。占位头像与群资料应答到达后
 *  「自动头像」的算法相同，应答到达时头像不会再变一次颜色。
 *
 *    groupId：群ID；<= 0 时不写群号，保持模型原值。
 *    name：   群名；NULL 或空串时不写群名，保持模型原值。
 *    netRole：本人在群内的角色，取协议侧的值（0 成员 / 1 管理员 / 2 群主），
 *             越界值按普通成员处理。
 *    model：  [出参] 待写入的界面模型。
 */
void ApplyGroupContext(int64_t groupId, LPCTSTR name, int netRole, GroupManageModel& model);

/**
 *  把群资料应答映射进界面模型的群基本信息与权限部分。
 *  不触碰模型里的成员 / 申请 / 禁言列表 —— 那三份数据由各自的应答填充，
 *  三个应答是并发发出、各自到达的，谁都不该清掉别人的结果。
 *
 *  本人角色（model.m_myRole）只在 info.m_myrole >= 0 时才更新：群资料变更推送
 *  （1318）的报文里没有 myrole（一份推送发给全群，角色因人而异），解析后为 -1，
 *  此时须保留模型中已有的角色，否则群主 / 管理员会被降级成普通成员。
 *
 *    info：  群资料应答或推送的解析结果（UTF-8 字符串会被转成宽字符）；
 *    model：[出参] 待填充的界面模型。
 */
void ApplyGroupInfo(const net::GroupInfoResult& info, GroupManageModel& model);

/**
 *  把群成员应答映射成界面模型的成员列表与禁言列表。
 *
 *  两份列表同源：禁言列表就是成员列表里 muted 为真的那些人，故一次遍历同时产出，
 *  避免两处各遍历一遍、口径还可能不一致。
 *    members：群成员应答解析结果；
 *    nowMs：  当前时刻，Unix毫秒。用来把禁言到期时间换算成"剩余 N 分钟"文本。
 *             由调用方传入而不是内部取系统时间，是为了让这个函数保持纯粹、可测。
 *    model：  [出参] 待填充的界面模型，其成员与禁言列表会被整体替换，并把
 *             m_membersState 置为 kLoadDone（空列表也一样）。
 */
void ApplyGroupMembers(const net::GroupMembersResult& members, int64_t nowMs,
                       GroupManageModel& model);

/**
 *  把待审批加群申请应答映射成界面模型的申请列表。
 *    requests：加群申请应答解析结果；
 *    nowMs：   当前时刻，Unix毫秒，用来算"N 分钟前"这类相对时间文本；
 *    model：   [出参] 待填充的界面模型，其申请列表会被整体替换，并把
 *              m_requestsState 置为 kLoadDone（空列表也一样）。
 */
void ApplyJoinRequests(const net::GroupJoinRequestsResult& requests, int64_t nowMs,
                       GroupManageModel& model);

/**
 *  某项数据的应答失败时，算出该项新的加载状态。
 *
 *    · 此前从未成功过（kLoadPending）：变为 kLoadFailed，界面改为显示失败提示。
 *    · 此前已经成功过（kLoadDone）：保持 kLoadDone。这种情况是后续刷新失败（如
 *      成员变动推送触发的重新拉取），模型里仍是上一次的真实数据，继续显示它比
 *      换成失败提示更有用。
 *    · 已经是 kLoadFailed：保持不变。
 *
 *    current：该项当前的加载状态。
 *  返回：新的加载状态。纯函数，无副作用。
 */
LoadState LoadStateAfterFailure(LoadState current);

/**
 *  把加群链接列表应答（1341）写进模型：成功时整体替换 m_links 并置 m_linksState 为 kLoadDone；
 *  失败时按 LoadStateAfterFailure 处理加载状态，列表保持原样。
 *    result：1341 应答解析结果；
 *    nowMs： 当前时间（Unix 毫秒），用来写生成时间的相对说法；
 *    model： [出参] 待写入的界面模型。
 */
void ApplyGroupLinks(const net::GroupLinksResult& result, int64_t nowMs, GroupManageModel& model);

/**
 *  把群公告列表应答映射成界面模型的公告列表。
 *    result：公告列表应答解析结果；
 *    nowMs： 当前时刻，Unix毫秒，用来算"今天 14:32" / "5/28 09:00"这类展示文本；
 *    model： [出参] 待填充的界面模型，其公告列表会被整体替换，并把
 *            m_announcementsLoaded 置为 true（区分"还没拉"与"拉过但是空的"）。
 */
void ApplyGroupAnnouncements(const net::GroupAnnouncementsResult& result, int64_t nowMs,
                             GroupManageModel& model);

/**
 *  把公告发布时刻换算成展示文本。
 *
 *  与禁言用的 FormatRelativeTime 不同，公告要的是"什么时候发的"这个绝对时刻 ——
 *  公告会长期挂着，"3 天前"这种相对说法过两天再看就对不上了，而"5/28 09:00"
 *  什么时候看都一样。只有当天发的才用"今天 HH:MM"，因为那确实更好读。
 *    timestamp：发布时刻，Unix毫秒；<= 0 时返回空串；
 *    nowMs：    当前时刻，Unix毫秒，用来判断是不是"今天"。
 *  返回：当天返回"今天 HH:MM"；今年内返回"M/D HH:MM"；跨年返回"YYYY/M/D"。
 */
CString FormatAnnouncementTime(int64_t timestamp, int64_t nowMs);

/**
 *  把禁言剩余时长换算成展示文本。
 *    muteUntil：禁言到期时间，Unix毫秒；0 表示永久禁言；
 *    nowMs：    当前时刻，Unix毫秒。
 *  返回：永久禁言返回"永久禁言"；已到期返回空串（调用方据此当作未禁言处理）；
 *        否则返回"剩余 N 分钟" / "剩余 N 小时" / "剩余 N 天"。
 */
CString FormatMuteRemaining(int64_t muteUntil, int64_t nowMs);

/**
 *  把一个过去的时刻换算成相对时间文本，供加群申请卡显示"10 分钟前"。
 *    timestamp：事件发生时刻，Unix毫秒；
 *    nowMs：    当前时刻，Unix毫秒。
 *  返回：一分钟内返回"刚刚"；否则按分钟 / 小时 / 天取最大的合适单位。
 *        timestamp 晚于 nowMs（两端时钟不同步）时同样返回"刚刚"，不显示负数。
 */
CString FormatRelativeTime(int64_t timestamp, int64_t nowMs);

/**
 *  给群管理写操作的失败应答挑一句人话。
 *
 *  服务端回的 msg 是英文诊断信息（"only owner or admin can mute" 之类），直接
 *  弹给用户看不懂，故按 (协议号, 错误码) 组合翻成中文。没有对应条目时退回
 *  "操作失败" + 服务端原文，至少不会吞掉信息。
 *
 *    cmd：    协议号（cmd_kick_group_member 等，取值见 net/Protocol.h）；
 *    code：   服务端返回的错误码（取值见 net/Protocol.h 的 error_code）；
 *    msgUtf8：服务端返回的诊断信息（UTF-8），仅在没有对应条目时拼进去。
 *  返回：可直接弹给用户的中文提示。纯函数，无副作用，便于单元测试。
 */
CString DescribeGroupOpFailure(int cmd, int code, const std::string& msgUtf8);

/**
 *  判断某个页面在当前角色下是否可见。
 *
 *  规则（与方案文档 §7.4 的表格一致）：
 *    · 成员管理、基本信息 —— 所有成员可见（普通成员为只读）；
 *    · 加群审批、禁言管理、群权限 —— 群主与管理员可见；
 *    · 危险区域、群机器人 —— 仅群主可见（群机器人另须企业打开机器人且插件可用，见 IsBotsPageVisible）。
 *
 *    pageId： 页面 id，取值见 GroupManageLayout.h 的 GmPageId；
 *    myRole： 登录者在本群的角色，取值见 GroupRole。
 *  返回：可见返回 true。未知的 pageId 返回 false。
 */
bool IsPageVisibleForRole(int pageId, int myRole);

/**
 *  「群机器人」页（添加与管理本群的群机器人，docs/机器人方案.md §14.9）是否出现在导航里：只有群主，且企业打开了机器人、
 *  机器人插件 FlamingoBot.dll 已加载时才出现；管理员与普通成员一律看不到（2026-10-08 用户确认）。
 *    myRole：       登录者在本群的角色，取值见 GroupRole。
 *    botsAvailable：企业打开了机器人（features.bot）且插件已加载（botui::IsAvailable）。
 *  @return 出现返回 true
 */
bool IsBotsPageVisible(int myRole, bool botsAvailable);

/**
 *  「群公告」页是否出现在导航里（基本信息页的「群公告」一行同样按它决定）：帖子群没有群公告，这一页与这一行都不出现
 *  （docs/帖子群方案.md §15.1 第 3 条，2026-10-08 用户确认）；普通群仍按角色判定，规则同 IsPageVisibleForRole。
 *    myRole：       登录者在本群的角色，取值见 GroupRole。
 *    isThreadGroup：本群是帖子群。
 *  @return 出现返回 true
 */
bool IsAnnouncementPageVisible(int myRole, bool isThreadGroup);

/**
 *  把服务端的角色值（net 层 group_member_role：0成员 1管理员 2群主）映射成
 *  界面模型的 GroupRole（0群主 1管理员 2成员）。
 *
 *  两套枚举的顺序<u>正好相反</u>，这是个极容易写错的地方 —— 界面枚举按"权限从高到低"
 *  排（方便按序分组展示），协议枚举按"权限从低到高"排（方便用 >= 做权限比较）。
 *  统一走本函数换算，不要在各处手写。
 *    netRole：协议侧角色值。
 *  返回：界面侧角色值；取值超出范围时按普通成员处理。
 */
GroupRole NetRoleToUiRole(int netRole);

/**
 *  NetRoleToUiRole 的反向映射：界面角色值 → 协议角色值。
 *    uiRole：界面侧角色值，取值见 GroupRole。
 *  返回：协议侧角色值；取值超出范围时按普通成员处理。
 */
int UiRoleToNetRole(int uiRole);

} // namespace balloongroupmanage
