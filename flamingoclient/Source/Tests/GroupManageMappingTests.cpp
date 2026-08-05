/**
 *  GroupManageMappingTests.cpp
 *
 *  群管理窗口纯逻辑层（BalloonGroupManage/GroupManageMapping）的单元测试。
 *
 *  这一层是整个群管理接入里最值得测的部分：角色映射两套枚举顺序相反、右键菜单
 *  的可用项有"登录者三种角色 × 目标三种角色 × 是否已禁言 × 是否是自己"这么多格、
 *  禁言到期与相对时间又各有一堆边界 —— 全都是靠手工点界面几乎不可能覆盖全的。
 *
 *  balloonwj@qq.com   2026-07-31
 */

#include "catch_amalgamated.hpp"

#include "../BalloonGroupManage/GroupManageMapping.h"
#include "../BalloonMain/AvatarPalette.h"   //群头像用例要按同一张调色板核对底色
#include "../GroupRoleRules.h"   //设 / 撤管理员的共用门控，群管理窗与群聊窗同一份规则

#include <time.h>     //mktime：公告时间文本的用例要按本地日历反算时间戳
#include <string.h>   //memset

using namespace balloongroupmanage;

// =====================================================================
// 测试用的时间基准
// =====================================================================
//
// 取一个固定时刻当"现在"，所有用例都相对它构造数据 —— 不取系统时间，
// 测试才可重复。

static const int64_t kNow = 1700000000000LL;   // 2023-11-15 前后的某个整点，取值本身无所谓
static const int64_t kOneMinute = 60LL * 1000;
static const int64_t kOneHour   = 60 * kOneMinute;
static const int64_t kOneDay    = 24 * kOneHour;

// 在菜单模型里找某个 id 的项。
// 返回值是<u>拷贝</u>而不是指针：调用方常写成
// FindMenuEntry(BuildMemberMenuModel(state), id) 这种形式，那个 vector 是临时量，
// 返回指针会在整条表达式结束时悬空。
static MemberMenuEntry FindMenuEntry(const std::vector<MemberMenuEntry>& entries, UINT id)
{
    for (size_t i = 0; i < entries.size(); ++i)
    {
        if (!entries[i].m_isSeparator && entries[i].m_id == id)
            return entries[i];
    }
    return MemberMenuEntry();   // m_id 为 0，表示没找到
}

static bool HasMenuEntry(const std::vector<MemberMenuEntry>& entries, UINT id)
{
    return FindMenuEntry(entries, id).m_id == id;
}

// 造一个菜单状态
static MemberMenuState MakeState(int myRole, int targetRole, bool targetMuted, bool isSelf)
{
    MemberMenuState s;
    s.m_myRole = myRole;
    s.m_targetRole = targetRole;
    s.m_targetMuted = targetMuted;
    s.m_isSelf = isSelf;
    return s;
}

// =====================================================================
// 角色映射
// =====================================================================

TEST_CASE("GM1 协议角色与界面角色的顺序相反，映射必须逐档对上", "[groupmanage][role]")
{
    //协议侧 0 成员 / 1 管理员 / 2 群主；界面侧 0 群主 / 1 管理员 / 2 成员。
    //两套枚举顺序正好相反，这条用例就是防止有人直接强转。
    CHECK(NetRoleToUiRole(0) == kRoleMember);
    CHECK(NetRoleToUiRole(1) == kRoleAdmin);
    CHECK(NetRoleToUiRole(2) == kRoleOwner);

    CHECK(UiRoleToNetRole(kRoleMember) == 0);
    CHECK(UiRoleToNetRole(kRoleAdmin) == 1);
    CHECK(UiRoleToNetRole(kRoleOwner) == 2);
}

TEST_CASE("GM2 越界的角色值一律按普通成员处理", "[groupmanage][role]")
{
    //服务端将来若加了新角色，老客户端读到不认识的值时不能把人当成群主
    CHECK(NetRoleToUiRole(3) == kRoleMember);
    CHECK(NetRoleToUiRole(-1) == kRoleMember);
    CHECK(UiRoleToNetRole(99) == 0);
    CHECK(UiRoleToNetRole(-5) == 0);
}

TEST_CASE("GM3 两个方向的映射互为逆运算", "[groupmanage][role]")
{
    CHECK(NetRoleToUiRole(UiRoleToNetRole(kRoleOwner)) == kRoleOwner);
    CHECK(NetRoleToUiRole(UiRoleToNetRole(kRoleAdmin)) == kRoleAdmin);
    CHECK(NetRoleToUiRole(UiRoleToNetRole(kRoleMember)) == kRoleMember);
}

// =====================================================================
// 页面门控
// =====================================================================

TEST_CASE("GM4 群主能看到全部六个页面", "[groupmanage][pagegate]")
{
    CHECK(IsPageVisibleForRole(1001, kRoleOwner));   // 成员管理
    CHECK(IsPageVisibleForRole(1002, kRoleOwner));   // 加群审批
    CHECK(IsPageVisibleForRole(1003, kRoleOwner));   // 禁言管理
    CHECK(IsPageVisibleForRole(1004, kRoleOwner));   // 基本信息
    CHECK(IsPageVisibleForRole(1005, kRoleOwner));   // 群权限
    CHECK(IsPageVisibleForRole(1006, kRoleOwner));   // 危险区域
}

TEST_CASE("GM5 管理员看不到危险区域", "[groupmanage][pagegate]")
{
    //解散群与转让群主只有群主能做，整页对管理员都不该出现
    CHECK(IsPageVisibleForRole(1001, kRoleAdmin));
    CHECK(IsPageVisibleForRole(1002, kRoleAdmin));
    CHECK(IsPageVisibleForRole(1003, kRoleAdmin));
    CHECK(IsPageVisibleForRole(1004, kRoleAdmin));
    CHECK(IsPageVisibleForRole(1005, kRoleAdmin));
    CHECK_FALSE(IsPageVisibleForRole(1006, kRoleAdmin));
}

TEST_CASE("GM6 普通成员只看得到成员管理与基本信息", "[groupmanage][pagegate]")
{
    CHECK(IsPageVisibleForRole(1001, kRoleMember));       // 成员管理（只读）
    CHECK(IsPageVisibleForRole(1004, kRoleMember));       // 基本信息（只读）
    CHECK_FALSE(IsPageVisibleForRole(1002, kRoleMember)); // 加群审批
    CHECK_FALSE(IsPageVisibleForRole(1003, kRoleMember)); // 禁言管理
    CHECK_FALSE(IsPageVisibleForRole(1005, kRoleMember)); // 群权限
    CHECK_FALSE(IsPageVisibleForRole(1006, kRoleMember)); // 危险区域
}

TEST_CASE("GM7 不认识的页面 id 一律不可见", "[groupmanage][pagegate]")
{
    //宁可少显示一页，也不要因为拼错 id 就把危险操作暴露给所有人
    CHECK_FALSE(IsPageVisibleForRole(9999, kRoleOwner));
    CHECK_FALSE(IsPageVisibleForRole(0, kRoleOwner));
}

// =====================================================================
// 成员右键菜单：可用项矩阵
// =====================================================================

TEST_CASE("GM8 对自己右键只有查看资料", "[groupmanage][membermenu]")
{
    //自己给自己发消息、自己禁言自己都没有意义。三种角色下表现一致。
    const int roles[] = { kRoleOwner, kRoleAdmin, kRoleMember };
    for (int i = 0; i < 3; ++i)
    {
        std::vector<MemberMenuEntry> m = BuildMemberMenuModel(
            MakeState(roles[i], roles[i], false, true));
        REQUIRE(m.size() == 1);
        CHECK(m[0].m_id == kMemberMenuProfile);
        CHECK_FALSE(m[0].m_isSeparator);
    }
}

TEST_CASE("GM9 发消息与查看资料对任何人都可用", "[groupmanage][membermenu]")
{
    const int myRoles[] = { kRoleOwner, kRoleAdmin, kRoleMember };
    const int targets[] = { kRoleOwner, kRoleAdmin, kRoleMember };
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            std::vector<MemberMenuEntry> m = BuildMemberMenuModel(
                MakeState(myRoles[i], targets[j], false, false));
            CHECK(HasMenuEntry(m, kMemberMenuSendMsg));
            CHECK(HasMenuEntry(m, kMemberMenuProfile));
        }
    }
}

TEST_CASE("GM10 只有群主能设置和撤销管理员", "[groupmanage][membermenu]")
{
    //群主对普通成员 → 出现"设为管理员"
    {
        std::vector<MemberMenuEntry> m = BuildMemberMenuModel(
            MakeState(kRoleOwner, kRoleMember, false, false));
        CHECK(HasMenuEntry(m, kMemberMenuSetAdmin));
        CHECK_FALSE(HasMenuEntry(m, kMemberMenuCancelAdmin));
    }

    //群主对管理员 → 出现"撤销管理员"
    {
        std::vector<MemberMenuEntry> m = BuildMemberMenuModel(
            MakeState(kRoleOwner, kRoleAdmin, false, false));
        CHECK(HasMenuEntry(m, kMemberMenuCancelAdmin));
        CHECK_FALSE(HasMenuEntry(m, kMemberMenuSetAdmin));
    }

    //管理员对任何人都不能改角色
    {
        std::vector<MemberMenuEntry> m = BuildMemberMenuModel(
            MakeState(kRoleAdmin, kRoleMember, false, false));
        CHECK_FALSE(HasMenuEntry(m, kMemberMenuSetAdmin));
        CHECK_FALSE(HasMenuEntry(m, kMemberMenuCancelAdmin));
    }

    //普通成员更不能
    {
        std::vector<MemberMenuEntry> m = BuildMemberMenuModel(
            MakeState(kRoleMember, kRoleMember, false, false));
        CHECK_FALSE(HasMenuEntry(m, kMemberMenuSetAdmin));
        CHECK_FALSE(HasMenuEntry(m, kMemberMenuCancelAdmin));
    }
}

TEST_CASE("GM11 任何人都不能对群主禁言或移出", "[groupmanage][membermenu]")
{
    //群主是本群权限的顶点。连另一个群主都不可能存在（每群唯一），
    //故这里覆盖的是"管理员 / 普通成员对群主右键"。
    const int myRoles[] = { kRoleOwner, kRoleAdmin, kRoleMember };
    for (int i = 0; i < 3; ++i)
    {
        std::vector<MemberMenuEntry> m = BuildMemberMenuModel(
            MakeState(myRoles[i], kRoleOwner, false, false));
        CHECK_FALSE(HasMenuEntry(m, kMemberMenuMute));
        CHECK_FALSE(HasMenuEntry(m, kMemberMenuUnmute));
        CHECK_FALSE(HasMenuEntry(m, kMemberMenuKick));
        CHECK_FALSE(HasMenuEntry(m, kMemberMenuSetAdmin));
        CHECK_FALSE(HasMenuEntry(m, kMemberMenuCancelAdmin));

        //但发消息和看资料仍然可以
        CHECK(HasMenuEntry(m, kMemberMenuSendMsg));
        CHECK(HasMenuEntry(m, kMemberMenuProfile));
    }
}

TEST_CASE("GM12 管理员不能禁言或移出其他管理员", "[groupmanage][membermenu]")
{
    std::vector<MemberMenuEntry> m = BuildMemberMenuModel(
        MakeState(kRoleAdmin, kRoleAdmin, false, false));
    CHECK_FALSE(HasMenuEntry(m, kMemberMenuMute));
    CHECK_FALSE(HasMenuEntry(m, kMemberMenuKick));

    //但群主可以
    std::vector<MemberMenuEntry> owner = BuildMemberMenuModel(
        MakeState(kRoleOwner, kRoleAdmin, false, false));
    CHECK(HasMenuEntry(owner, kMemberMenuMute));
    CHECK(HasMenuEntry(owner, kMemberMenuKick));
}

TEST_CASE("GM13 管理员可以禁言和移出普通成员", "[groupmanage][membermenu]")
{
    std::vector<MemberMenuEntry> m = BuildMemberMenuModel(
        MakeState(kRoleAdmin, kRoleMember, false, false));
    CHECK(HasMenuEntry(m, kMemberMenuMute));
    CHECK(HasMenuEntry(m, kMemberMenuKick));
}

TEST_CASE("GM14 普通成员的菜单里没有任何管理动作", "[groupmanage][membermenu]")
{
    const int targets[] = { kRoleOwner, kRoleAdmin, kRoleMember };
    for (int j = 0; j < 3; ++j)
    {
        std::vector<MemberMenuEntry> m = BuildMemberMenuModel(
            MakeState(kRoleMember, targets[j], false, false));
        CHECK_FALSE(HasMenuEntry(m, kMemberMenuMute));
        CHECK_FALSE(HasMenuEntry(m, kMemberMenuUnmute));
        CHECK_FALSE(HasMenuEntry(m, kMemberMenuKick));
        CHECK_FALSE(HasMenuEntry(m, kMemberMenuSetAdmin));
        CHECK_FALSE(HasMenuEntry(m, kMemberMenuCancelAdmin));

        //普通成员的菜单就只剩发消息与查看资料两项，连分隔线都不该有
        REQUIRE(m.size() == 2);
        CHECK_FALSE(m[0].m_isSeparator);
        CHECK_FALSE(m[1].m_isSeparator);
    }
}

TEST_CASE("GM15 禁言与解除禁言按目标当前状态二选一", "[groupmanage][membermenu]")
{
    //目标未禁言 → 显示"禁言"
    {
        std::vector<MemberMenuEntry> m = BuildMemberMenuModel(
            MakeState(kRoleOwner, kRoleMember, false, false));
        CHECK(HasMenuEntry(m, kMemberMenuMute));
        CHECK_FALSE(HasMenuEntry(m, kMemberMenuUnmute));
    }

    //目标已禁言 → 显示"解除禁言"
    {
        std::vector<MemberMenuEntry> m = BuildMemberMenuModel(
            MakeState(kRoleOwner, kRoleMember, true, false));
        CHECK(HasMenuEntry(m, kMemberMenuUnmute));
        CHECK_FALSE(HasMenuEntry(m, kMemberMenuMute));
    }
}

TEST_CASE("GM16 移出本群与上面的项之间有分隔线", "[groupmanage][membermenu]")
{
    //移出是这个菜单里唯一不可撤销的操作，不该紧挨着禁言，避免手滑
    std::vector<MemberMenuEntry> m = BuildMemberMenuModel(
        MakeState(kRoleOwner, kRoleMember, false, false));

    size_t kickIndex = m.size();
    for (size_t i = 0; i < m.size(); ++i)
    {
        if (!m[i].m_isSeparator && m[i].m_id == kMemberMenuKick)
        {
            kickIndex = i;
            break;
        }
    }
    REQUIRE(kickIndex < m.size());
    REQUIRE(kickIndex > 0);
    CHECK(m[kickIndex - 1].m_isSeparator);
}

TEST_CASE("GM17 菜单不以分隔线开头或结尾", "[groupmanage][membermenu]")
{
    //开头或结尾挂一条分隔线在真实菜单里会画出一条孤零零的横线
    const int myRoles[] = { kRoleOwner, kRoleAdmin, kRoleMember };
    const int targets[] = { kRoleOwner, kRoleAdmin, kRoleMember };
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            for (int muted = 0; muted < 2; ++muted)
            {
                std::vector<MemberMenuEntry> m = BuildMemberMenuModel(
                    MakeState(myRoles[i], targets[j], muted != 0, false));
                REQUIRE_FALSE(m.empty());
                CHECK_FALSE(m.front().m_isSeparator);
                CHECK_FALSE(m.back().m_isSeparator);
            }
        }
    }
}

TEST_CASE("GM18 菜单项都有非空文案", "[groupmanage][membermenu]")
{
    //空文案会在菜单里画出一个点得动但看不见字的条目
    const int myRoles[] = { kRoleOwner, kRoleAdmin, kRoleMember };
    for (int i = 0; i < 3; ++i)
    {
        std::vector<MemberMenuEntry> m = BuildMemberMenuModel(
            MakeState(myRoles[i], kRoleMember, false, false));
        for (size_t k = 0; k < m.size(); ++k)
        {
            if (m[k].m_isSeparator)
                continue;
            REQUIRE(m[k].m_label != NULL);
            CHECK(wcslen(m[k].m_label) > 0);
            CHECK(m[k].m_id != 0);
        }
    }
}

// =====================================================================
// 禁言剩余时长
// =====================================================================

TEST_CASE("GM19 到期时间为 0 表示永久禁言", "[groupmanage][mutetime]")
{
    //这是与服务端约定的取值，不是"没设置"
    CHECK(FormatMuteRemaining(0, kNow) == CString(_T("永久禁言")));

    //负值是脏数据，同样按永久处理，不能显示成负的剩余时间
    CHECK(FormatMuteRemaining(-1, kNow) == CString(_T("永久禁言")));
}

TEST_CASE("GM20 已到期返回空串，让调用方按未禁言处理", "[groupmanage][mutetime]")
{
    CHECK(FormatMuteRemaining(kNow, kNow).IsEmpty());
    CHECK(FormatMuteRemaining(kNow - 1, kNow).IsEmpty());
    CHECK(FormatMuteRemaining(kNow - kOneDay, kNow).IsEmpty());
}

TEST_CASE("GM21 剩余时长取最大的合适单位", "[groupmanage][mutetime]")
{
    CHECK(FormatMuteRemaining(kNow + 10 * kOneMinute, kNow) == CString(_T("剩余 10 分钟")));
    CHECK(FormatMuteRemaining(kNow + 3 * kOneHour, kNow) == CString(_T("剩余 3 小时")));
    CHECK(FormatMuteRemaining(kNow + 2 * kOneDay, kNow) == CString(_T("剩余 2 天")));

    //边界：正好一小时按小时显示，而不是"60 分钟"
    CHECK(FormatMuteRemaining(kNow + kOneHour, kNow) == CString(_T("剩余 1 小时")));
    //正好一天按天显示
    CHECK(FormatMuteRemaining(kNow + kOneDay, kNow) == CString(_T("剩余 1 天")));
}

TEST_CASE("GM22 不足一分钟不显示剩余 0 分钟", "[groupmanage][mutetime]")
{
    //"剩余 0 分钟"读起来像已经解禁了
    CHECK(FormatMuteRemaining(kNow + 1, kNow) == CString(_T("剩余不到 1 分钟")));
    CHECK(FormatMuteRemaining(kNow + 30 * 1000, kNow) == CString(_T("剩余不到 1 分钟")));
}

// =====================================================================
// 相对时间
// =====================================================================

TEST_CASE("GM23 一分钟内显示刚刚", "[groupmanage][reltime]")
{
    CHECK(FormatRelativeTime(kNow, kNow) == CString(_T("刚刚")));
    CHECK(FormatRelativeTime(kNow - 30 * 1000, kNow) == CString(_T("刚刚")));
}

TEST_CASE("GM24 时间戳晚于当前时刻也按刚刚处理", "[groupmanage][reltime]")
{
    //两端时钟不同步时会出现这种数据，不能显示成"-3 分钟前"
    CHECK(FormatRelativeTime(kNow + kOneHour, kNow) == CString(_T("刚刚")));
}

TEST_CASE("GM25 相对时间取最大的合适单位", "[groupmanage][reltime]")
{
    CHECK(FormatRelativeTime(kNow - 10 * kOneMinute, kNow) == CString(_T("10 分钟前")));
    CHECK(FormatRelativeTime(kNow - 5 * kOneHour, kNow) == CString(_T("5 小时前")));
    CHECK(FormatRelativeTime(kNow - 3 * kOneDay, kNow) == CString(_T("3 天前")));

    //边界
    CHECK(FormatRelativeTime(kNow - kOneMinute, kNow) == CString(_T("1 分钟前")));
    CHECK(FormatRelativeTime(kNow - kOneHour, kNow) == CString(_T("1 小时前")));
    CHECK(FormatRelativeTime(kNow - kOneDay, kNow) == CString(_T("1 天前")));
}

// =====================================================================
// 协议结构 → 界面模型
// =====================================================================

TEST_CASE("GM26 群资料映射：七项群设置逐个对上", "[groupmanage][mapping]")
{
    net::GroupInfoResult info;
    info.m_code = 0;
    info.m_groupid = 10000000001LL;
    info.m_name = "\xe8\xae\xbe\xe8\xae\xa1\xe7\xbb\x84";   // UTF-8 的"设计组"
    info.m_membercount = 5;
    info.m_myrole = 2;              // 协议侧群主
    info.m_muteall = 1;
    info.m_joinmode = 2;
    info.m_atallscope = 1;
    info.m_newseehistory = 0;
    info.m_memberinvite = 1;
    info.m_welcomeon = 1;
    info.m_sensitiveon = 1;

    GroupManageModel model;
    ApplyGroupInfo(info, model);

    CHECK(model.m_myRole == kRoleOwner);          // 协议 2 → 界面 0
    CHECK(model.m_groupNumber == CString(_T("10000000001")));
    CHECK(model.m_muteAll);
    CHECK(model.m_perms.joinMethodIndex == 2);
    CHECK(model.m_perms.atAllScopeIndex == 1);
    CHECK_FALSE(model.m_perms.newMemberSeeHistory);
    CHECK(model.m_perms.memberInviteFriend);
    CHECK(model.m_perms.welcomeMessage);
    CHECK(model.m_perms.sensitiveWordFilter);
}

TEST_CASE("GM27 成员映射：群名片优先于账号昵称", "[groupmanage][mapping]")
{
    net::GroupMembersResult members;
    members.m_code = 0;

    net::GroupMemberItem withCard;
    withCard.m_userid = 100001;
    withCard.m_card = "card-name";
    withCard.m_nickname = "nick-name";
    members.m_members.push_back(withCard);

    net::GroupMemberItem noCard;
    noCard.m_userid = 100002;
    noCard.m_nickname = "nick-only";
    members.m_members.push_back(noCard);

    net::GroupMemberItem noName;
    noName.m_userid = 100003;
    members.m_members.push_back(noName);

    GroupManageModel model;
    ApplyGroupMembers(members, kNow, model);

    REQUIRE(model.m_members.size() == 3);
    CHECK(model.m_members[0].name == CString(_T("card-name")));
    CHECK(model.m_members[1].name == CString(_T("nick-only")));
    //两者都空时用 userid 兜底，至少让这一行能被认出来、能右键
    CHECK(model.m_members[2].name == CString(_T("100003")));
}

TEST_CASE("GM28 成员映射：禁言列表与成员列表同源", "[groupmanage][mapping]")
{
    net::GroupMembersResult members;
    members.m_code = 0;

    net::GroupMemberItem normal;
    normal.m_userid = 100001;
    normal.m_nickname = "normal";
    members.m_members.push_back(normal);

    net::GroupMemberItem permanent;
    permanent.m_userid = 100002;
    permanent.m_nickname = "permanent";
    permanent.m_muted = 1;
    permanent.m_muteuntil = 0;
    permanent.m_mutereason = "spam";
    members.m_members.push_back(permanent);

    net::GroupMemberItem timed;
    timed.m_userid = 100003;
    timed.m_nickname = "timed";
    timed.m_muted = 1;
    timed.m_muteuntil = kNow + 18 * kOneMinute;
    members.m_members.push_back(timed);

    GroupManageModel model;
    ApplyGroupMembers(members, kNow, model);

    CHECK(model.m_members.size() == 3);
    REQUIRE(model.m_muted.size() == 2);

    CHECK(model.m_muted[0].userid == 100002);
    CHECK(model.m_muted[0].permanent);
    CHECK(model.m_muted[0].reason == CString(_T("spam")));
    CHECK(model.m_muted[0].remainingText == CString(_T("永久禁言")));

    CHECK(model.m_muted[1].userid == 100003);
    CHECK_FALSE(model.m_muted[1].permanent);
    CHECK(model.m_muted[1].remainingText == CString(_T("剩余 18 分钟")));
}

TEST_CASE("GM29 成员映射：已过期的定时禁言按未禁言处理", "[groupmanage][mapping]")
{
    //服务端组装应答时已折算过一次，这里是本地时钟的双保险
    net::GroupMembersResult members;
    members.m_code = 0;

    net::GroupMemberItem expired;
    expired.m_userid = 100001;
    expired.m_nickname = "expired";
    expired.m_muted = 1;
    expired.m_muteuntil = kNow - kOneMinute;
    expired.m_mutereason = "old";
    members.m_members.push_back(expired);

    GroupManageModel model;
    ApplyGroupMembers(members, kNow, model);

    REQUIRE(model.m_members.size() == 1);
    CHECK_FALSE(model.m_members[0].muted);
    CHECK(model.m_members[0].muteUntil == 0);
    CHECK(model.m_members[0].muteReason.IsEmpty());
    //禁言列表里也不该出现他
    CHECK(model.m_muted.empty());
}

TEST_CASE("GM30 成员映射：重复调用会整体替换而不是累加", "[groupmanage][mapping]")
{
    net::GroupMembersResult first;
    first.m_code = 0;
    net::GroupMemberItem a;
    a.m_userid = 100001;
    a.m_nickname = "a";
    first.m_members.push_back(a);

    GroupManageModel model;
    ApplyGroupMembers(first, kNow, model);
    REQUIRE(model.m_members.size() == 1);

    //推送到达后会重拉一次成员列表，第二次的结果必须整体替换第一次的
    net::GroupMembersResult second;
    second.m_code = 0;
    net::GroupMemberItem b;
    b.m_userid = 100002;
    b.m_nickname = "b";
    second.m_members.push_back(b);

    ApplyGroupMembers(second, kNow, model);
    REQUIRE(model.m_members.size() == 1);
    CHECK(model.m_members[0].userid == 100002);
}

TEST_CASE("GM31 加群申请映射：来源与推荐人", "[groupmanage][mapping]")
{
    net::GroupJoinRequestsResult requests;
    requests.m_code = 0;
    requests.m_groupid = 10000000001LL;

    net::GroupJoinRequestItem fromFriend;
    fromFriend.m_requestid = 55;
    fromFriend.m_userid = 100003;
    fromFriend.m_nickname = "applicant";
    fromFriend.m_verifymsg = "let me in";
    fromFriend.m_timestamp = kNow - 10 * kOneMinute;
    fromFriend.m_source = kJoinFromFriend;
    fromFriend.m_recommender = 100001;
    requests.m_requests.push_back(fromFriend);

    net::GroupJoinRequestItem fromSearch;
    fromSearch.m_requestid = 56;
    fromSearch.m_userid = 100004;
    fromSearch.m_nickname = "searcher";
    fromSearch.m_timestamp = kNow - 2 * kOneHour;
    fromSearch.m_source = kJoinFromSearch;
    fromSearch.m_recommender = 0;
    requests.m_requests.push_back(fromSearch);

    GroupManageModel model;
    ApplyJoinRequests(requests, kNow, model);

    REQUIRE(model.m_requests.size() == 2);

    CHECK(model.m_requests[0].requestid == 55);
    CHECK(model.m_requests[0].source == kJoinFromFriend);
    CHECK(model.m_requests[0].recommender == CString(_T("100001")));
    CHECK(model.m_requests[0].timeText == CString(_T("10 分钟前")));

    //非好友邀请来源不显示推荐人，即便协议里带了值
    CHECK(model.m_requests[1].source == kJoinFromSearch);
    CHECK(model.m_requests[1].recommender.IsEmpty());
    CHECK(model.m_requests[1].timeText == CString(_T("2 小时前")));
}

TEST_CASE("GM32 派生统计随映射结果变化", "[groupmanage][mapping]")
{
    net::GroupMembersResult members;
    members.m_code = 0;
    for (int i = 0; i < 4; ++i)
    {
        net::GroupMemberItem m;
        m.m_userid = 100001 + i;
        m.m_nickname = "m";
        //让第 0 个是群主、第 1 个是管理员，其余是普通成员
        m.m_role = (i == 0) ? 2 : ((i == 1) ? 1 : 0);
        //让最后一个被禁言
        if (i == 3)
        {
            m.m_muted = 1;
        }
        members.m_members.push_back(m);
    }

    GroupManageModel model;
    ApplyGroupMembers(members, kNow, model);

    CHECK(model.MemberCount() == 4);
    CHECK(model.MutedCount() == 1);
    CHECK(model.CountByRole(kRoleOwner) == 1);
    CHECK(model.CountByRole(kRoleAdmin) == 1);
    CHECK(model.CountByRole(kRoleMember) == 2);
}

// =====================================================================
// 失败提示
// =====================================================================

TEST_CASE("GM33 常见错误码有专门的中文说法", "[groupmanage][failuretip]")
{
    const int kCmdKick = 1310;
    const int kErrNotGroupAdmin = 303;
    const int kErrCannotOperateOwner = 307;

    CString notAdmin = DescribeGroupOpFailure(kCmdKick, kErrNotGroupAdmin, "only owner or admin");
    CHECK(notAdmin.Find(_T("群主")) >= 0);
    //不该把服务端的英文原文直接甩给用户
    CHECK(notAdmin.Find(_T("only owner")) < 0);

    CString ownerTip = DescribeGroupOpFailure(kCmdKick, kErrCannotOperateOwner, "");
    CHECK(ownerTip.Find(_T("群主")) >= 0);
}

TEST_CASE("GM34 无权限的说法按操作区分", "[groupmanage][failuretip]")
{
    //此值必须与 net/Protocol.h 的 error_code_permission_denied 一致。本用例
    //先前与产品代码一起误写成 12，两边错得一样，于是"无权限"这一整类提示
    //其实一句都走不到（服务端回的是 6），测试却是绿的。
    const int kErrPermissionDenied = 6;
    const int kCmdSetRole = 1312;
    const int kCmdDismiss = 1302;

    CString roleTip = DescribeGroupOpFailure(kCmdSetRole, kErrPermissionDenied, "");
    CString dismissTip = DescribeGroupOpFailure(kCmdDismiss, kErrPermissionDenied, "");

    //同一个错误码在不同操作下要说不同的话，否则用户不知道自己缺什么
    CHECK(roleTip != dismissTip);
    CHECK(roleTip.Find(_T("管理员")) >= 0);
    CHECK(dismissTip.Find(_T("解散")) >= 0);
}

TEST_CASE("GM34b 非群主设管理员被拒时给中文提示而不是英文原文", "[groupmanage][failuretip]")
{
    //服务端拒绝时回的是 error_code_permission_denied(6) + "only group owner can set role"。
    //一旦本端的错误码对不上，就会落到兜底分支、把这句英文原文抛给用户 ——
    //这条用例专门盯住这个回归。
    const int kErrPermissionDenied = 6;
    const int kCmdSetRole = 1312;

    CString tip = DescribeGroupOpFailure(kCmdSetRole, kErrPermissionDenied,
                                         "only group owner can set role");
    CHECK(tip.Find(_T("只有群主")) >= 0);
    CHECK(tip.Find(_T("only group owner")) < 0);
}

TEST_CASE("GM35 没有对应条目时附上服务端原文", "[groupmanage][failuretip]")
{
    //不认识的错误码不能把线索吞掉
    const int kUnknownCode = 9999;
    CString tip = DescribeGroupOpFailure(1310, kUnknownCode, "something went wrong");
    CHECK(tip.Find(_T("操作失败")) >= 0);
    CHECK(tip.Find(_T("something went wrong")) >= 0);

    //服务端没给原文时也要能给出一句完整的话
    CString bare = DescribeGroupOpFailure(1310, kUnknownCode, "");
    CHECK(bare.Find(_T("操作失败")) >= 0);
}

// =====================================================================
// 敏感词库映射
// =====================================================================

TEST_CASE("GM36 敏感词映射：正常词条与相对时间", "[groupmanage][sensitive]")
{
    net::SensitiveWordsResult words;
    words.m_code = 0;
    words.m_groupid = 10000000001LL;

    net::SensitiveWordItem w1;
    w1.m_id = 1;
    w1.m_word = "badword";
    w1.m_timestamp = kNow - 3 * kOneDay;
    words.m_words.push_back(w1);

    net::SensitiveWordItem w2;
    w2.m_id = 2;
    w2.m_word = "another";
    w2.m_timestamp = kNow - 20 * kOneMinute;
    words.m_words.push_back(w2);

    GroupManageModel model;
    ApplySensitiveWords(words, kNow, model);

    REQUIRE(model.m_sensitiveWords.size() == 2);
    CHECK(model.m_sensitiveWords[0].id == 1);
    CHECK(model.m_sensitiveWords[0].word == CString(_T("badword")));
    CHECK(model.m_sensitiveWords[0].timeText == CString(_T("3 天前")));
    CHECK(model.m_sensitiveWords[1].timeText == CString(_T("20 分钟前")));
}

TEST_CASE("GM37 敏感词映射：拉过但为空要与还没拉区分开", "[groupmanage][sensitive]")
{
    // 界面靠这个标志决定显示"加载中"还是"还没有敏感词"，两者不能混
    GroupManageModel model;
    CHECK_FALSE(model.m_sensitiveWordsLoaded);   // 初始：还没拉

    net::SensitiveWordsResult empty;
    empty.m_code = 0;
    ApplySensitiveWords(empty, kNow, model);

    CHECK(model.m_sensitiveWords.empty());
    CHECK(model.m_sensitiveWordsLoaded);          // 拉过了，只是没有词
}

TEST_CASE("GM38 敏感词映射：没有词条ID或空词的脏数据跳过", "[groupmanage][sensitive]")
{
    net::SensitiveWordsResult words;
    words.m_code = 0;

    net::SensitiveWordItem noId;       // 没有 id → 删不掉，留着只会让人点了没反应
    noId.m_id = 0;
    noId.m_word = "orphan";
    words.m_words.push_back(noId);

    net::SensitiveWordItem emptyWord;  // 空词 → 界面上就是一行空白
    emptyWord.m_id = 5;
    emptyWord.m_word = "";
    words.m_words.push_back(emptyWord);

    net::SensitiveWordItem good;
    good.m_id = 6;
    good.m_word = "ok";
    words.m_words.push_back(good);

    GroupManageModel model;
    ApplySensitiveWords(words, kNow, model);

    REQUIRE(model.m_sensitiveWords.size() == 1);
    CHECK(model.m_sensitiveWords[0].id == 6);
}

TEST_CASE("GM39 敏感词映射：重复调用整体替换而不是累加", "[groupmanage][sensitive]")
{
    // 增删之后会重拉一次，第二次的结果必须整体替换第一次的
    net::SensitiveWordsResult first;
    first.m_code = 0;
    net::SensitiveWordItem a;
    a.m_id = 1;
    a.m_word = "a";
    first.m_words.push_back(a);

    GroupManageModel model;
    ApplySensitiveWords(first, kNow, model);
    REQUIRE(model.m_sensitiveWords.size() == 1);

    net::SensitiveWordsResult second;
    second.m_code = 0;
    net::SensitiveWordItem b;
    b.m_id = 2;
    b.m_word = "b";
    second.m_words.push_back(b);

    ApplySensitiveWords(second, kNow, model);
    REQUIRE(model.m_sensitiveWords.size() == 1);
    CHECK(model.m_sensitiveWords[0].id == 2);
}

TEST_CASE("GM40 管理词库页的可见性与群权限页一致", "[groupmanage][pagegate]")
{
    // 它是群权限页的二级界面，能进群权限就该能进词库，反之亦然
    const int kPagePerms = 1005;
    const int kPageSensitive = 1007;

    const int roles[] = { kRoleOwner, kRoleAdmin, kRoleMember };
    for (int i = 0; i < 3; ++i)
    {
        CHECK(IsPageVisibleForRole(kPageSensitive, roles[i])
              == IsPageVisibleForRole(kPagePerms, roles[i]));
    }

    // 具体到取值：群主与管理员可见，普通成员不可见
    CHECK(IsPageVisibleForRole(kPageSensitive, kRoleOwner));
    CHECK(IsPageVisibleForRole(kPageSensitive, kRoleAdmin));
    CHECK_FALSE(IsPageVisibleForRole(kPageSensitive, kRoleMember));
}

// =====================================================================
// 群资料变更推送（1318）里的角色字段
// =====================================================================
//
// 这一组用例复现并锁死一个曾经很难查的现象：在群管理窗「基本信息」页点保存后，
// 左侧导航只剩「成员管理」与「基本信息」两项，加群审批 / 禁言管理 / 群权限 /
// 危险区域全部消失。根因是保存成功后服务端向<u>全群</u>推 1318，而这条推送体
// 里没有 myrole —— 一条推送发给所有人，角色因人而异，服务端不可能在里面带；
// 客户端却把它当"完整群资料"整份覆盖进模型，把本人角色一路降成了普通成员，
// 导航于是按普通成员重建。

// 按服务端 GroupProtocol::buildGroupInfoChangedNotify 的字段拼一份推送体。
// 与真实推送逐字段对齐（尤其是<u>不含</u> myrole 这一点）。
static std::string MakeInfoChangedNotifyBody()
{
    return std::string(
        "{\"groupid\":\"10000000001\",\"name\":\"design\",\"facetype\":0,"
        "\"customface\":\"\",\"announcement\":\"hello\",\"ownerid\":\"100001\","
        "\"joinmode\":1,\"muteall\":0,\"membercount\":5,\"intro\":\"\","
        "\"atallscope\":0,\"newseehistory\":1,\"memberinvite\":0,\"welcomeon\":0,"
        "\"welcometext\":\"\",\"sensitiveon\":0,\"changetype\":1}");
}

TEST_CASE("GM41 群资料变更推送不带 myrole，映射后本人角色须原样保留",
          "[groupmanage][mapping][notify]")
{
    net::GroupInfoChangedNotify notify;
    REQUIRE(net::GroupProtocolClient::parseGroupInfoChangedNotify(
                MakeInfoChangedNotifyBody(), notify));

    GroupManageModel model;
    model.m_myRole = kRoleOwner;      //开窗时由 1303 应答确定的真实角色

    ApplyGroupInfo(notify.m_info, model);

    CHECK(model.m_myRole == kRoleOwner);
}

TEST_CASE("GM42 推送刷新后左侧导航各页对群主仍然可见",
          "[groupmanage][mapping][notify][pagegate]")
{
    //现象层面的断言：角色没被降，六个页面就都还在
    const int kPageApproval  = 1002;
    const int kPageMute      = 1003;
    const int kPagePerms     = 1005;
    const int kPageDanger    = 1006;

    net::GroupInfoChangedNotify notify;
    REQUIRE(net::GroupProtocolClient::parseGroupInfoChangedNotify(
                MakeInfoChangedNotifyBody(), notify));

    GroupManageModel model;
    model.m_myRole = kRoleOwner;
    ApplyGroupInfo(notify.m_info, model);

    CHECK(IsPageVisibleForRole(kPageApproval, model.m_myRole));
    CHECK(IsPageVisibleForRole(kPageMute, model.m_myRole));
    CHECK(IsPageVisibleForRole(kPagePerms, model.m_myRole));
    CHECK(IsPageVisibleForRole(kPageDanger, model.m_myRole));
}

TEST_CASE("GM43 管理员收到推送后同样保留管理员身份",
          "[groupmanage][mapping][notify]")
{
    net::GroupInfoChangedNotify notify;
    REQUIRE(net::GroupProtocolClient::parseGroupInfoChangedNotify(
                MakeInfoChangedNotifyBody(), notify));

    GroupManageModel model;
    model.m_myRole = kRoleAdmin;
    ApplyGroupInfo(notify.m_info, model);

    CHECK(model.m_myRole == kRoleAdmin);
}

TEST_CASE("GM44 群资料应答带 myrole 时仍以应答为准（含降级）",
          "[groupmanage][mapping]")
{
    //上面三条锁的是"推送不许改角色"，这条锁的是另一半：1303 应答带了 myrole，
    //就必须照着改，包括从群主降成普通成员（被撤销管理员后重拉即走这条路）。
    net::GroupInfoResult info;
    info.m_code = 0;
    info.m_groupid = 10000000001LL;
    info.m_myrole = 0;               //协议侧普通成员

    GroupManageModel model;
    model.m_myRole = kRoleOwner;
    ApplyGroupInfo(info, model);

    CHECK(model.m_myRole == kRoleMember);
}

// =====================================================================
// 群公告
// =====================================================================

// 造一个"某个本地日期的 12:00"的 Unix 毫秒时刻。
//
// 直接写死一个时间戳会让 FormatAnnouncementTime 的用例随测试机时区飘 —— 同一个
// 时间戳在东八区是 20 日、在 UTC 就成了 19 日。这里改用 mktime 由本地日历反算
// 时间戳，于是"这一刻在本地是哪天几点"是确定的，与时区无关。
static int64_t LocalTimeMs(int year, int month, int day, int hour, int minute)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year  = year - 1900;
    t.tm_mon   = month - 1;
    t.tm_mday  = day;
    t.tm_hour  = hour;
    t.tm_min   = minute;
    t.tm_isdst = -1;      //由 mktime 自行判断夏令时
    const time_t seconds = mktime(&t);
    return static_cast<int64_t>(seconds) * 1000;
}

TEST_CASE("GM45 公告发布时刻：当天用「今天」，跨天用日期", "[groupmanage][announcement]")
{
    const int64_t now = LocalTimeMs(2026, 8, 3, 15, 0);

    //当天：用"今天 HH:MM"，比"3 小时前"好读，而且公告会长期挂着，
    //相对说法过两天再看就对不上了
    CHECK(FormatAnnouncementTime(LocalTimeMs(2026, 8, 3, 14, 32), now)
          == CString(_T("今天 14:32")));

    //同年不同天：M/D HH:MM
    CHECK(FormatAnnouncementTime(LocalTimeMs(2026, 5, 28, 9, 0), now)
          == CString(_T("5/28 09:00")));

    //跨年：精确到分没有意义，给到日即可
    CHECK(FormatAnnouncementTime(LocalTimeMs(2025, 12, 31, 23, 59), now)
          == CString(_T("2025/12/31")));

    //非法时间戳返回空串，由界面自行决定怎么显示
    CHECK(FormatAnnouncementTime(0, now) == CString());
    CHECK(FormatAnnouncementTime(-1, now) == CString());
}

TEST_CASE("GM46 公告映射：逐字段还原并置上已加载标记", "[groupmanage][announcement]")
{
    const int64_t now = LocalTimeMs(2026, 8, 3, 15, 0);

    net::GroupAnnouncementsResult result;
    result.m_code = 0;
    result.m_groupid = 10000000001LL;
    result.m_total = 2;

    net::GroupAnnouncementItem a;
    a.m_id = 12;
    a.m_title = "release";
    a.m_content = "body";
    a.m_severity = 1;
    a.m_publisher = 100023;
    a.m_publishername = "lin";
    a.m_publishtime = LocalTimeMs(2026, 8, 3, 14, 32);
    result.m_announcements.push_back(a);

    GroupManageModel model;
    //拉之前是"还没拉"，界面据此显示加载中
    CHECK_FALSE(model.m_announcementsLoaded);

    ApplyGroupAnnouncements(result, now, model);

    CHECK(model.m_announcementsLoaded);
    REQUIRE(model.m_announcements.size() == 1);
    CHECK(model.m_announcements[0].id == 12);
    CHECK(model.m_announcements[0].title == CString(_T("release")));
    CHECK(model.m_announcements[0].content == CString(_T("body")));
    CHECK(model.m_announcements[0].severity == kSeverityImportant);
    CHECK(model.m_announcements[0].publisher == 100023);
    CHECK(model.m_announcements[0].publisherName == CString(_T("lin")));
    CHECK(model.m_announcements[0].timeText == CString(_T("今天 14:32")));

    //总条数取服务端给的 total，不是本页条数 —— 本页只回了 1 条，实际有 2 条
    CHECK(model.m_announcementTotal == 2);
}

TEST_CASE("GM47 公告映射：空列表也算已加载，且整体替换旧列表", "[groupmanage][announcement]")
{
    const int64_t now = LocalTimeMs(2026, 8, 3, 15, 0);

    net::GroupAnnouncementsResult first;
    first.m_code = 0;
    net::GroupAnnouncementItem a;
    a.m_id = 12;
    a.m_title = "one";
    first.m_announcements.push_back(a);

    GroupManageModel model;
    ApplyGroupAnnouncements(first, now, model);
    REQUIRE(model.m_announcements.size() == 1);

    //第二次应答整体替换而不是追加 —— 否则撤回一条之后旧的还留在列表里
    net::GroupAnnouncementsResult second;
    second.m_code = 0;
    ApplyGroupAnnouncements(second, now, model);
    CHECK(model.m_announcements.empty());
    //拉回来是空的也要算"已加载"，界面才能显示空状态而不是一直转圈
    CHECK(model.m_announcementsLoaded);
}

TEST_CASE("GM48 公告映射：跳过空标题，发布人昵称缺失时退回 userid",
          "[groupmanage][announcement]")
{
    const int64_t now = LocalTimeMs(2026, 8, 3, 15, 0);

    net::GroupAnnouncementsResult result;
    result.m_code = 0;

    //空标题的公告在界面上是一张读不懂的空卡片。服务端把标题定为必填，出现空标题
    //说明数据有问题，不如不显示。
    net::GroupAnnouncementItem blank;
    blank.m_id = 11;
    blank.m_title = "";
    blank.m_content = "body only";
    result.m_announcements.push_back(blank);

    //发布人昵称取不到（已注销 / 老服务端不下发）时退回显示 userid，
    //至少让人知道是谁发的
    net::GroupAnnouncementItem noName;
    noName.m_id = 12;
    noName.m_title = "has title";
    noName.m_publisher = 100023;
    result.m_announcements.push_back(noName);

    GroupManageModel model;
    ApplyGroupAnnouncements(result, now, model);

    REQUIRE(model.m_announcements.size() == 1);
    CHECK(model.m_announcements[0].id == 12);
    CHECK(model.m_announcements[0].publisherName == CString(_T("100023")));
}

TEST_CASE("GM49 群公告页仅群主与管理员可见", "[groupmanage][announcement][pagegate]")
{
    const int kPageAnnouncement = 1008;

    //这一页是"发布与管理"公告的地方，只有能发的人需要进来；
    //普通成员看公告走群聊窗右侧边栏的「公告」Tab，那里是只读的
    CHECK(IsPageVisibleForRole(kPageAnnouncement, kRoleOwner));
    CHECK(IsPageVisibleForRole(kPageAnnouncement, kRoleAdmin));
    CHECK_FALSE(IsPageVisibleForRole(kPageAnnouncement, kRoleMember));
}

TEST_CASE("GM50 公告页数按总条数算，一条都没有也算一页", "[groupmanage][announcement]")
{
    GroupManageModel model;

    //一条都没有时也算一页 —— "第 1/0 页"读不通
    model.m_announcementTotal = 0;
    CHECK(model.AnnouncementPageCount() == 1);

    //不足一页
    model.m_announcementTotal = 1;
    CHECK(model.AnnouncementPageCount() == 1);

    //正好一页（边界）
    model.m_announcementTotal = kAnnouncePageSize;
    CHECK(model.AnnouncementPageCount() == 1);

    //多一条就翻到第二页（向上取整）
    model.m_announcementTotal = kAnnouncePageSize + 1;
    CHECK(model.AnnouncementPageCount() == 2);

    //公告攒到几百条时页数照样算得对 —— 这正是分页要解决的场景
    model.m_announcementTotal = kAnnouncePageSize * 40;
    CHECK(model.AnnouncementPageCount() == 40);
    model.m_announcementTotal = kAnnouncePageSize * 40 + 1;
    CHECK(model.AnnouncementPageCount() == 41);
}

TEST_CASE("GM51 公告映射：当前页越界时夹回最后一页", "[groupmanage][announcement]")
{
    //典型场景：停在最后一页、把那一页唯一的一条删掉了。重拉回来 total 变小，
    //原来那个页号已经不存在，必须夹回来，否则界面上会显示"第 5/4 页"加一片空白。
    const int64_t now = LocalTimeMs(2026, 8, 3, 15, 0);

    GroupManageModel model;
    model.m_announcementPage = 4;   //停在第 5 页

    net::GroupAnnouncementsResult shrunk;
    shrunk.m_code = 0;
    shrunk.m_total = 3;             //删到只剩 3 条，总共就 1 页了
    ApplyGroupAnnouncements(shrunk, now, model);

    CHECK(model.AnnouncementPageCount() == 1);
    CHECK(model.m_announcementPage == 0);

    //没越界时不动它 —— 删一条之后还留在原页，免得被弹回第一页还得再翻回来
    GroupManageModel stay;
    stay.m_announcementPage = 1;
    net::GroupAnnouncementsResult plenty;
    plenty.m_code = 0;
    plenty.m_total = kAnnouncePageSize * 3;
    ApplyGroupAnnouncements(plenty, now, stay);
    CHECK(stay.m_announcementPage == 1);
}

// =====================================================================
// GM20 —— 共用门控函数 grouprole::CanSetAdmin 的完整真值表
// =====================================================================
//
// 群管理窗口的成员卡菜单与群聊窗口的成员侧栏 / 气泡 @ 菜单共用这一个函数，
// 上面 GM10 验证的是"群管理窗经它得到的结论没变"，这里则把函数本身的
// 三种登录者角色 × 三种目标角色 × 是否本人共 18 格逐格钉死。
//
// 注意本函数用的是<u>网络侧</u>角色取值（群主为 2），与本文件其余用例里的
// 界面侧 GroupRole（群主为 0）顺序相反，故这里一律写全 grouprole:: 前缀。

TEST_CASE("GM20 CanSetAdmin 的 18 格真值表", "[groupmanage][membermenu][admin]")
{
    const int roles[] = { grouprole::kNetRoleMember,
                          grouprole::kNetRoleAdmin,
                          grouprole::kNetRoleOwner };

    //只有"群主 + 不是自己 + 目标不是群主"这一类放行，其余 15 格全部拒绝
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            const bool expectWhenOther =
                (roles[i] == grouprole::kNetRoleOwner &&
                 roles[j] != grouprole::kNetRoleOwner);

            INFO("我的角色 = " << roles[i] << "，目标角色 = " << roles[j]);
            CHECK(grouprole::CanSetAdmin(roles[i], roles[j], false) == expectWhenOther);

            //目标是自己时一律不放行 —— 群主卸任须走"转让群主"
            CHECK_FALSE(grouprole::CanSetAdmin(roles[i], roles[j], true));
        }
    }
}

TEST_CASE("GM21 CanSetAdmin 对不在群内的角色一律拒绝", "[groupmanage][membermenu][admin]")
{
    const int kNotInGroup = -1;   //与 GroupManager::getMemberRole 的约定一致

    //目标已不在本群：菜单里给了也点不动，服务端会回"该成员已不在本群"
    CHECK_FALSE(grouprole::CanSetAdmin(grouprole::kNetRoleOwner, kNotInGroup, false));
    //本人不在本群（异常态）：更不可能有权限
    CHECK_FALSE(grouprole::CanSetAdmin(kNotInGroup, grouprole::kNetRoleMember, false));
    CHECK_FALSE(grouprole::CanSetAdmin(kNotInGroup, kNotInGroup, false));
}

TEST_CASE("GM22 ShouldOfferCancelAdmin 只对管理员为真", "[groupmanage][membermenu][admin]")
{
    CHECK(grouprole::ShouldOfferCancelAdmin(grouprole::kNetRoleAdmin));
    CHECK_FALSE(grouprole::ShouldOfferCancelAdmin(grouprole::kNetRoleMember));
    CHECK_FALSE(grouprole::ShouldOfferCancelAdmin(grouprole::kNetRoleOwner));
}

// =====================================================================
// GM23-GM25 —— 移出成员的共用门控 grouprole::CanKickMember
// =====================================================================
//
// 与 CanSetAdmin 的关键差别：移出<u>管理员也能做</u>（只是够不到其他管理员和
// 群主），而设 / 撤管理员仅限群主。两者门控不同，容易写混，故分别钉死。
// 同样使用网络侧角色取值（群主为 2），与本文件其余用例的界面侧枚举相反。

TEST_CASE("GM23 CanKickMember 的 18 格真值表", "[groupmanage][membermenu][kick]")
{
    const int roles[] = { grouprole::kNetRoleMember,
                          grouprole::kNetRoleAdmin,
                          grouprole::kNetRoleOwner };

    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            const int my = roles[i];
            const int target = roles[j];

            //放行仅三格：群主移管理员、群主移普通成员、管理员移普通成员
            bool expect = false;
            if (my == grouprole::kNetRoleOwner)
            {
                expect = (target != grouprole::kNetRoleOwner);
            }
            else if (my == grouprole::kNetRoleAdmin)
            {
                expect = (target == grouprole::kNetRoleMember);
            }

            INFO("我的角色 = " << my << "，目标角色 = " << target);
            CHECK(grouprole::CanKickMember(my, target, false) == expect);

            //目标是自己时一律不放行 —— 主动退群走的是另一条路
            CHECK_FALSE(grouprole::CanKickMember(my, target, true));
        }
    }
}

TEST_CASE("GM24 管理员移不动群主和其他管理员", "[groupmanage][membermenu][kick]")
{
    //这两条是本次需求里特别强调的，单列出来免得被上面的循环淹没
    CHECK_FALSE(grouprole::CanKickMember(grouprole::kNetRoleAdmin,
                                         grouprole::kNetRoleOwner, false));
    CHECK_FALSE(grouprole::CanKickMember(grouprole::kNetRoleAdmin,
                                         grouprole::kNetRoleAdmin, false));
    //但普通成员是移得动的
    CHECK(grouprole::CanKickMember(grouprole::kNetRoleAdmin,
                                   grouprole::kNetRoleMember, false));
}

TEST_CASE("GM25 CanKickMember 对不在群内的角色一律拒绝", "[groupmanage][membermenu][kick]")
{
    const int kNotInGroup = -1;

    //目标已不在本群
    CHECK_FALSE(grouprole::CanKickMember(grouprole::kNetRoleOwner, kNotInGroup, false));
    CHECK_FALSE(grouprole::CanKickMember(grouprole::kNetRoleAdmin, kNotInGroup, false));
    //本人不在本群
    CHECK_FALSE(grouprole::CanKickMember(kNotInGroup, grouprole::kNetRoleMember, false));
    CHECK_FALSE(grouprole::CanKickMember(kNotInGroup, kNotInGroup, false));
}

// =====================================================================
// GM26 —— 成员卡 ctrl id 与成员下标的换算
// =====================================================================
//
// 成员卡建卡时把下标编进 ctrl id，右键弹菜单时反算回来。右键完全可能点在别的
// 控件上，那时必须认出"这不是成员卡"而不是误算成某个成员 —— 这正是本次修好的
// 那条链路的关键一环，故把边界钉死。

TEST_CASE("GM26 成员卡 id 与下标互算，越界一律返回 -1", "[groupmanage][membermenu]")
{
    const UINT kCardBase = 2400;   // kIdMemberCardBase
    const UINT kCardMax  = 2899;   // kIdMemberCardMax

    //区间内：首、次、末三个位置
    CHECK(MemberIndexFromCardId(kCardBase) == 0);
    CHECK(MemberIndexFromCardId(kCardBase + 1) == 1);
    CHECK(MemberIndexFromCardId(kCardMax) == (int)(kCardMax - kCardBase));

    //区间外：下界之下、上界之上，以及 0（没设 id 的子控件就是这个值 ——
    //本次的缺陷正是右键打在这类子控件上，绝不能把它当成第 0 位成员）
    CHECK(MemberIndexFromCardId(0) == -1);
    CHECK(MemberIndexFromCardId(kCardBase - 1) == -1);
    CHECK(MemberIndexFromCardId(kCardMax + 1) == -1);
    CHECK(MemberIndexFromCardId(9999) == -1);
}

// =====================================================================
// GM60~GM64 —— 群头像映射
// =====================================================================
//
// 群资料里的 (facetype, customface) 与联系人头像同一套编码（见 AvatarSpec.h）。
// 这里钉住三种取值各自映射成什么，以及脏数据必须安全落回自动渲染 —— 头像是
// 每个群都会画的东西，一条脏记录让绘制侧拿到非法字形就是满屏乱码。

// 造一份只填了群头像相关字段的群资料应答。
static net::GroupInfoResult MakeAvatarInfo(int facetype, const std::string& customface,
                                           const std::string& name = u8"技术部")
{
    net::GroupInfoResult info;
    info.m_code       = 0;
    info.m_groupid    = 10000000001LL;
    info.m_name       = name;       //协议层的字符串一律 UTF-8
    info.m_myrole     = 2;
    info.m_facetype   = facetype;
    info.m_customface = customface;
    return info;
}

TEST_CASE("GM60 自定义图头像带出 fileid，占位字仍是群名首字", "[groupmanage][groupavatar]")
{
    GroupManageModel model;
    ApplyGroupInfo(MakeAvatarInfo(2, "abc123fileid"), model);

    CHECK(model.m_groupAvatarKind == 2);
    CHECK(model.m_groupAvatarFileId == _T("abc123fileid"));
    //真图还没下到本地时先画首字占位，故这一项不能是空的。
    CHECK(model.m_groupAvatarText == _T("技"));
}

TEST_CASE("GM61 字形头像用编码里的字与底色，不再取群名首字", "[groupmanage][groupavatar]")
{
    GroupManageModel model;
    ApplyGroupInfo(MakeAvatarInfo(1, "A|3"), model);

    CHECK(model.m_groupAvatarKind == 1);
    CHECK(model.m_groupAvatarText == _T("A"));
    CHECK(model.m_groupAvatarColor == balloonmain::AvatarColorByIndex(3));
    CHECK(model.m_groupAvatarFileId.IsEmpty());
}

TEST_CASE("GM62 自动头像回落群名首字 + 按群 id 取色", "[groupmanage][groupavatar]")
{
    GroupManageModel model;
    ApplyGroupInfo(MakeAvatarInfo(0, ""), model);

    CHECK(model.m_groupAvatarKind == 0);
    CHECK(model.m_groupAvatarText == _T("技"));
    CHECK(model.m_groupAvatarColor == balloonmain::AvatarColorForUser(10000000001LL));
    CHECK(model.m_groupAvatarFileId.IsEmpty());

    //群名为空是异常数据：占位字留空，DuiAvatar 拿到空名字只画纯色底，
    //不能画成 "?"。
    GroupManageModel noName;
    ApplyGroupInfo(MakeAvatarInfo(0, "", ""), noName);
    CHECK(noName.m_groupAvatarText.IsEmpty());
}

TEST_CASE("GM63 customface 脏数据一律安全落回自动渲染", "[groupmanage][groupavatar]")
{
    //字形头像声明了 facetype=1 却给不出合法的 "glyph|idx"：缺分隔符、只有分隔符、
    //色号不是数字、整个空串。四种都必须落回自动渲染而不是把脏串当字形画上去。
    const char* kDirty[] = { "abc", "|", "A|xy", "" };
    for (size_t i = 0; i < sizeof(kDirty) / sizeof(kDirty[0]); ++i)
    {
        GroupManageModel model;
        ApplyGroupInfo(MakeAvatarInfo(1, kDirty[i]), model);

        CHECK(model.m_groupAvatarKind == 0);
        CHECK(model.m_groupAvatarText == _T("技"));
        CHECK(model.m_groupAvatarColor == balloonmain::AvatarColorForUser(10000000001LL));
        CHECK(model.m_groupAvatarFileId.IsEmpty());
    }
}

TEST_CASE("GM64 CanChangeGroupAvatar 只放行群主与管理员", "[groupmanage][groupavatar]")
{
    //与服务端 onUpdateGroupInfo 的 myrole >= group_role_admin 对齐。
    CHECK(grouprole::CanChangeGroupAvatar(grouprole::kNetRoleOwner));
    CHECK(grouprole::CanChangeGroupAvatar(grouprole::kNetRoleAdmin));
    CHECK_FALSE(grouprole::CanChangeGroupAvatar(grouprole::kNetRoleMember));

    //角色未知（群资料还没到，解析后为 -1）：宁可先不给点，等资料到了界面重建
    //再放开，也好过点了才发现没权限。
    CHECK_FALSE(grouprole::CanChangeGroupAvatar(-1));
}
