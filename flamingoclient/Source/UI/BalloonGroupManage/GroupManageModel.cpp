/**
 *  GroupManageModel.cpp
 *
 *  群管理数据模型的派生统计实现，以及 design 示例内容的假数据填充。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 与其它 BalloonGroupManage
 *  控件 / 布局文件一致，自带 ATL/WTL 前置依赖，工程里标 PrecompiledHeader
 *  = NotUsing。
 *
 *  balloonwj@qq.com   2026-06-03
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（等价 NewChatDemo/stdafx.h）----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限（窗口类仍为 RichEdit20W）
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>
#include <atlcrack.h>
#include <atlmisc.h>

#include "GroupManageModel.h"
#include "../BalloonSearch/ContactMatch.h"   // contactsearch::Match：成员页搜索按名字（含拼音）与职务匹配
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloongroupmanage {

GroupManageModel::GroupManageModel()
    : m_groupAvatarColor(RGB(0xFF, 0x7A, 0x45))
    , m_groupAvatarKind(0)          //默认按群名首字 + 群 id 取色自动渲染
    , m_groupFacetype(0)            //协议原值，与上面的解码结果配套
    , m_myRole(kRoleOwner)
    , m_isThreadGroup(false)        //主面板打开窗口时按群类型告知；之前当作普通群
    , m_membersState(kLoadPending)  //开窗时成员与申请都还没拉回来
    , m_requestsState(kLoadPending)
    , m_groupInfoState(kLoadPending)  //群资料同样要等应答
    , m_muteAll(false)
    , m_announcementTotal(0)
    , m_announcementPage(0)
    , m_announcementsLoaded(false)
    , m_draftSeverity(kSeverityNormal)
    , m_mustReadId(0)
    , m_linksState(kLoadPending)
    , m_botsAvailable(false)        //主面板打开窗口后才告知；之前一律当作不可用，导航里没有「群机器人」
    , m_botsState(kLoadPending)
{
}

int GroupManageModel::MemberCount() const
{
    return (int)m_members.size();
}

int GroupManageModel::PendingRequestCount() const
{
    return (int)m_requests.size();
}

int GroupManageModel::MutedCount() const
{
    return (int)m_muted.size();
}

int GroupManageModel::AnnouncementPageCount() const
{
    //一条都没有时也算一页 —— 显示"第 1/0 页"读不通，而空列表本来就是"第 1 页
    //上什么都没有"这个意思
    if (m_announcementTotal <= 0)
    {
        return 1;
    }
    //向上取整：9 条按每页 8 条算是 2 页
    return (m_announcementTotal + kAnnouncePageSize - 1) / kAnnouncePageSize;
}

int GroupManageModel::CountByRole(GroupRole role) const
{
    int count = 0;
    for (std::vector<GroupMember>::const_iterator it = m_members.begin();
         it != m_members.end(); ++it)
    {
        if (it->role == role)
        {
            ++count;
        }
    }
    return count;
}

int GroupManageModel::CountByRoleMatching(GroupRole role) const
{
    int count = 0;
    for (std::vector<GroupMember>::const_iterator it = m_members.begin();
         it != m_members.end(); ++it)
    {
        if (it->role == role && MemberMatchesKeyword(*it, m_memberKeyword))
        {
            ++count;
        }
    }
    return count;
}

bool MemberMatchesKeyword(const GroupMember& m, const CString& keyword)
{
    CString k = keyword;
    k.Trim();
    if (k.IsEmpty())
    {
        return true;
    }
    //职务借用匹配器的"地区"一档：该档就是不区分大小写的子串匹配，排在名字与拼音之前
    //还是之后对这里没有影响，只关心命中与否。账号一档不用，传空串
    const contactsearch::MatchResult r = contactsearch::Match(
        std::wstring((LPCTSTR)k), std::wstring((LPCTSTR)m.name), std::wstring(),
        std::wstring((LPCTSTR)m.title));
    return r.m_matched;
}

// =====================================================================
// 假数据填充
// =====================================================================

namespace {

// 往成员列表追加一名成员（收拢重复的字段赋值）。
void AddMember(std::vector<GroupMember>& list,
               LPCTSTR name, LPCTSTR title, LPCTSTR avatarText,
               COLORREF avatarColor, GroupRole role, int online)
{
    GroupMember m;
    m.name        = name;
    m.title       = title;
    m.avatarText  = avatarText;
    m.avatarColor = avatarColor;
    m.role        = role;
    m.online      = online;
    list.push_back(m);
}

// 往申请列表追加一条加群申请。
void AddRequest(std::vector<JoinRequest>& list,
                LPCTSTR name, LPCTSTR account, LPCTSTR avatarText,
                COLORREF avatarColor, LPCTSTR message,
                int source, LPCTSTR recommender, LPCTSTR timeText)
{
    JoinRequest r;
    r.name        = name;
    r.account     = account;
    r.avatarText  = avatarText;
    r.avatarColor = avatarColor;
    r.message     = message;
    r.source      = source;
    r.recommender = recommender;
    r.timeText    = timeText;
    list.push_back(r);
}

// 往禁言列表追加一名被禁言成员。
void AddMuted(std::vector<MutedMember>& list,
              LPCTSTR name, LPCTSTR avatarText, COLORREF avatarColor,
              LPCTSTR reason, LPCTSTR remainingText, bool permanent)
{
    MutedMember m;
    m.name          = name;
    m.avatarText    = avatarText;
    m.avatarColor   = avatarColor;
    m.reason        = reason;
    m.remainingText = remainingText;
    m.permanent     = permanent;
    list.push_back(m);
}

} // anonymous namespace

// i18n-ignore-begin: 调试入口的模拟群管理数据
void FillMockData(GroupManageModel& model)
{
    // ---- 群基本信息（对照 design/群管理1.png、群管理4.png）----
    model.m_groupName        = L"设计组·周会";
    model.m_groupAvatarText  = L"设";
    model.m_groupAvatarColor = RGB(0xFF, 0x7A, 0x45);
    model.m_groupNumber      = _T("881234567");
    model.m_announcement     = L"每周三 14:00 例会，提前提交设计稿。";
    model.m_intro.Empty();
    model.m_myRole           = kRoleOwner;

    // ---- 成员（群主 1 + 管理员 2 + 普通成员 4 = 7 人，对照群管理1.png）----
    model.m_members.clear();
    AddMember(model.m_members, L"林知夏", L"资深设计师", L"林",
              RGB(0xFF, 0x7A, 0x45), kRoleOwner, kOnlineOnline);
    AddMember(model.m_members, L"设计组组长", L"设计组组长", L"江",
              RGB(0x45, 0x6C, 0xDF), kRoleAdmin, kOnlineOnline);
    AddMember(model.m_members, L"谢知行", L"产品总监", L"谢",
              RGB(0x2E, 0xA1, 0x9C), kRoleAdmin, kOnlineOnline);
    AddMember(model.m_members, L"苏沐辰", L"资深设计师", L"苏",
              RGB(0xFF, 0x7A, 0x45), kRoleMember, kOnlineOnline);
    AddMember(model.m_members, L"宋微之", L"后端工程师", L"宋",
              RGB(0x2E, 0xA1, 0x9C), kRoleMember, kOnlineOnline);
    AddMember(model.m_members, L"方亦初", L"高级产品经理", L"方",
              RGB(0xFF, 0x7A, 0x45), kRoleMember, kOnlineOnline);
    AddMember(model.m_members, L"唐砚秋", L"前端工程师", L"唐",
              RGB(0x8A, 0x5C, 0xD6), kRoleMember, kOnlineOnline);

    // ---- 加群申请（3 条，对照群管理2.png）----
    model.m_requests.clear();
    AddRequest(model.m_requests, L"王语嫣", _T("23451234"), L"王",
               RGB(0x6C, 0x7A, 0xE0), L"同事推荐，希望加入产品讨论",
               kJoinFromFriend, L"方亦初", L"10 分钟前");
    AddRequest(model.m_requests, L"陈致远", _T("98761234"), L"陈",
               RGB(0x3F, 0x8E, 0xE0), L"@谢知行 推荐，正在做竞品分析",
               kJoinFromSearch, _T(""), L"2 小时前");
    AddRequest(model.m_requests, L"李清照", _T("56781234"), L"李",
               RGB(0x3F, 0xA8, 0x6B), L"希望参与 v3.3 设计评审",
               kJoinFromQrCode, _T(""), L"昨天 18:43");

    // ---- 禁言成员（2 人，对照群管理3.png）----
    model.m_muted.clear();
    AddMuted(model.m_muted, L"某热心群友", L"热", RGB(0x8A, 0x8A, 0x8A),
             L"频繁刷屏", L"剩余 18 分钟", false);
    AddMuted(model.m_muted, L"广告账号A", L"广", RGB(0xE0, 0x4F, 0x4F),
             L"发布广告链接", L"永久", true);

    // ---- 权限默认值（对照群管理5.png）----
    model.m_perms = GroupPermissions();
    model.m_muteAll = false;

    // ---- 加载状态：示例数据视为已全部到齐，界面按内容显示而不是加载提示 ----
    model.m_membersState   = kLoadDone;
    model.m_requestsState  = kLoadDone;
    model.m_groupInfoState = kLoadDone;
}
// i18n-ignore-end

} // namespace balloongroupmanage
