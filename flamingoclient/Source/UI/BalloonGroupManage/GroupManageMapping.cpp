/**
 *  群管理窗口纯逻辑层的实现，见 GroupManageMapping.h。
 *  balloonwj@qq.com   2026-07-31
 */

#include "GroupManageMapping.h"
#include "../BalloonMain/AvatarPalette.h"   // AvatarColorForUser：头像底色按 userid 稳定取
#include "../BalloonMain/AvatarSpec.h"      // DecodeAvatar：把群的 (facetype,customface) 解成该画什么
#include "../../Session/GroupRoleRules.h"              // CanSetAdmin：设 / 撤管理员的门控，与群聊窗共用

#include <windows.h>
#include <time.h>          //localtime_s：公告发布时刻要按本地时区拆成年月日时分

namespace balloongroupmanage {

// =====================================================================
// 时间换算里用到的单位常量
// =====================================================================

static const int64_t kMillisPerSecond = 1000;
static const int64_t kSecondsPerMinute = 60;
static const int64_t kMinutesPerHour = 60;
static const int64_t kHoursPerDay = 24;
static const int64_t kMillisPerMinute = kMillisPerSecond * kSecondsPerMinute;
static const int64_t kMillisPerHour = kMillisPerMinute * kMinutesPerHour;
static const int64_t kMillisPerDay = kMillisPerHour * kHoursPerDay;

// =====================================================================
// UTF-8 → 宽字符
// =====================================================================

// 协议层的字符串一律是 UTF-8，界面层用的是宽字符，故每个文本字段都要过一道。
// 空串直接返回空，省掉一次 API 调用（成员列表里空的群名片、空的禁言原因很常见）。
static CString Utf8ToCString(const std::string& utf8)
{
    if (utf8.empty())
    {
        return CString();
    }

    const int needed = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                             static_cast<int>(utf8.size()), NULL, 0);
    if (needed <= 0)
    {
        return CString();
    }

    CString out;
    wchar_t* buf = out.GetBuffer(needed);
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), buf, needed);
    out.ReleaseBuffer(needed);
    return out;
}

// 取头像里显示的那个字：名字的第一个字符。名字为空时返回空串，由绘制侧
// 退回画通用人形占位。
static CString FirstCharOf(const CString& name)
{
    if (name.IsEmpty())
    {
        return CString();
    }
    return name.Left(1);
}

// =====================================================================
// 角色映射
// =====================================================================

GroupRole NetRoleToUiRole(int netRole)
{
    //协议侧：0 成员 1 管理员 2 群主；界面侧：0 群主 1 管理员 2 成员。顺序相反。
    if (netRole == 2)
    {
        return kRoleOwner;
    }
    if (netRole == 1)
    {
        return kRoleAdmin;
    }
    return kRoleMember;
}

int UiRoleToNetRole(int uiRole)
{
    if (uiRole == kRoleOwner)
    {
        return 2;
    }
    if (uiRole == kRoleAdmin)
    {
        return 1;
    }
    return 0;
}

// =====================================================================
// 时间文本
// =====================================================================

CString FormatMuteRemaining(int64_t muteUntil, int64_t nowMs)
{
    //到期时间为 0 是"永久禁言"这一约定的取值，不是"没设置"
    if (muteUntil <= 0)
    {
        return CString(_T("永久禁言"));
    }

    const int64_t left = muteUntil - nowMs;
    if (left <= 0)
    {
        //已到期。返回空串让调用方按未禁言处理 —— 服务端组装应答时也会把
        //过期的禁言折算掉，这里是双保险（本地时钟可能比服务端快一点）。
        return CString();
    }

    CString text;
    if (left >= kMillisPerDay)
    {
        text.Format(_T("剩余 %d 天"), static_cast<int>(left / kMillisPerDay));
    }
    else if (left >= kMillisPerHour)
    {
        text.Format(_T("剩余 %d 小时"), static_cast<int>(left / kMillisPerHour));
    }
    else if (left >= kMillisPerMinute)
    {
        text.Format(_T("剩余 %d 分钟"), static_cast<int>(left / kMillisPerMinute));
    }
    else
    {
        //不足一分钟不显示"剩余 0 分钟"，那读起来像已经解禁了
        text = _T("剩余不到 1 分钟");
    }
    return text;
}

CString FormatRelativeTime(int64_t timestamp, int64_t nowMs)
{
    const int64_t elapsed = nowMs - timestamp;

    //时间戳晚于当前时刻说明两端时钟不同步，按"刚刚"处理，不显示负数
    if (elapsed < kMillisPerMinute)
    {
        return CString(_T("刚刚"));
    }

    CString text;
    if (elapsed >= kMillisPerDay)
    {
        text.Format(_T("%d 天前"), static_cast<int>(elapsed / kMillisPerDay));
    }
    else if (elapsed >= kMillisPerHour)
    {
        text.Format(_T("%d 小时前"), static_cast<int>(elapsed / kMillisPerHour));
    }
    else
    {
        text.Format(_T("%d 分钟前"), static_cast<int>(elapsed / kMillisPerMinute));
    }
    return text;
}

// =====================================================================
// 协议结构 → 界面模型
// =====================================================================

void ApplyGroupContext(int64_t groupId, LPCTSTR name, int netRole, GroupManageModel& model)
{
    //群号在开窗这一刻就已经知道，先填上，不必等群资料应答（1303）。应答到达后
    //ApplyGroupInfo 会用同一个 groupid 再写一遍，值相同。这样即便应答迟到或失败，
    //「基本信息」页的群号也是准确的。
    if (groupId > 0)
    {
        model.m_groupNumber.Format(_T("%lld"), static_cast<long long>(groupId));
    }
    if (name != NULL && name[0] != _T('\0'))
    {
        model.m_groupName = name;
    }
    model.m_myRole = NetRoleToUiRole(netRole);

    //占位头像按"自动头像"算：群名首字 + 按群ID取的底色，与 ApplyGroupInfo 在
    //facetype 为 0 时的结果相同。群资料到达后若该群用的是字形或自定义图头像，
    //再由 ApplyGroupInfo 替换。
    balloonmain::AvatarSpec avatar = balloonmain::DecodeAvatar(
        balloonmain::avatar_kind_auto, std::string(), model.m_groupName, groupId);
    model.m_groupAvatarKind = avatar.m_kind;
    model.m_groupAvatarColor = avatar.m_color;
    model.m_groupAvatarText = FirstCharOf(model.m_groupName);
}

LoadState LoadStateAfterFailure(LoadState current)
{
    //已经成功过的数据保留：后续刷新失败时继续显示上一次的真实内容
    if (current == kLoadDone)
    {
        return kLoadDone;
    }
    return kLoadFailed;
}

void ApplyGroupInfo(const net::GroupInfoResult& info, GroupManageModel& model)
{
    model.m_groupName = Utf8ToCString(info.m_name);

    //群头像：与联系人头像同一套编码（见 BalloonMain/AvatarSpec.h）。DecodeAvatar
    //会把脏数据安全落回自动渲染，故这里拿到的字形 / 底色任何情况下都能直接画；
    //真图只带出 fileid，缓存路径与下载交给窗口层。
    balloonmain::AvatarSpec avatar = balloonmain::DecodeAvatar(
        info.m_facetype, info.m_customface, model.m_groupName, info.m_groupid);
    model.m_groupAvatarKind = avatar.m_kind;
    model.m_groupAvatarColor = avatar.m_color;
    model.m_groupAvatarFileId = avatar.m_fileId;
    //原值留一份，打开「修改群头像」对话框时要拿它预选（解码结果反推不回色号）。
    model.m_groupFacetype = info.m_facetype;
    model.m_groupCustomfaceUtf8 = info.m_customface;
    //占位字沿用本模块原有口径：字形头像用编码里的字，其余（自动、以及真图还没
    //下到本地时）取群名首字、群名为空则留空 —— DuiAvatar 拿到空名字只画纯色底，
    //这正是原来想要的效果，不能换成 AvatarSpec 的 "?" 回退。
    model.m_groupAvatarText = (avatar.m_kind == balloonmain::avatar_kind_glyph)
                              ? avatar.m_glyph
                              : FirstCharOf(model.m_groupName);

    model.m_groupNumber.Format(_T("%lld"), static_cast<long long>(info.m_groupid));
    model.m_announcement = Utf8ToCString(info.m_announcement);
    model.m_intro = Utf8ToCString(info.m_intro);

    //角色只在报文确实带了它时才更新。群资料变更推送（1318）是同一份报文发给全群，
    //里面没有 myrole，解析后为 -1；此时必须原样保留模型里已有的角色（那是开窗时
    //由 1303 应答确定的）。若照着 -1 覆盖，本人会被当成普通成员，左侧导航随即按
    //普通成员重建 —— 只剩「成员管理」与「基本信息」两项，这正是"点保存后其它
    //tab 全没了"的成因。
    if (info.m_myrole >= 0)
    {
        model.m_myRole = NetRoleToUiRole(info.m_myrole);
    }

    model.m_muteAll = (info.m_muteall != 0);

    model.m_perms.joinMethodIndex = info.m_joinmode;
    model.m_perms.atAllScopeIndex = info.m_atallscope;
    model.m_perms.newMemberSeeHistory = (info.m_newseehistory != 0);
    model.m_perms.memberInviteFriend = (info.m_memberinvite != 0);
    model.m_perms.welcomeMessage = (info.m_welcomeon != 0);
    model.m_perms.sensitiveWordFilter = (info.m_sensitiveon != 0);
    model.m_welcomeText = Utf8ToCString(info.m_welcometext);

    //群简介、欢迎语、群权限与全员禁言从此按真实值显示，不再显示加载提示
    model.m_groupInfoState = kLoadDone;
}

void ApplyGroupMembers(const net::GroupMembersResult& members, int64_t nowMs,
                       GroupManageModel& model)
{
    model.m_members.clear();
    model.m_muted.clear();

    for (size_t i = 0; i < members.m_members.size(); ++i)
    {
        const net::GroupMemberItem& src = members.m_members[i];

        GroupMember m;
        m.userid = src.m_userid;
        m.role = NetRoleToUiRole(src.m_role);

        //显示名优先取群名片，为空才退回账号昵称 —— 与群聊窗成员边栏同口径
        m.name = Utf8ToCString(src.m_card);
        if (m.name.IsEmpty())
        {
            m.name = Utf8ToCString(src.m_nickname);
        }
        //群名片与账号昵称都为空时用 userid 兜底，至少让这一行能被认出来、能右键
        if (m.name.IsEmpty())
        {
            m.name.Format(_T("%lld"), static_cast<long long>(src.m_userid));
        }

        m.avatarText = FirstCharOf(m.name);
        m.avatarColor = balloonmain::AvatarColorForUser(src.m_userid);

        //职务副标题：服务端没有这个概念，留空。成员卡在副标题为空时只画主标题。
        m.title.Empty();

        //在线状态同样不在群成员应答里，留"不显示状态点"。
        m.online = kOnlineNone;

        //服务端已按当时时刻折算过到期，这里再用本地时间兜一次底
        m.muteUntil = src.m_muteuntil;
        m.muteReason = Utf8ToCString(src.m_mutereason);
        m.muted = (src.m_muted != 0);
        if (m.muted && m.muteUntil > 0 && m.muteUntil <= nowMs)
        {
            m.muted = false;
            m.muteUntil = 0;
            m.muteReason.Empty();
        }

        model.m_members.push_back(m);

        //禁言列表与成员列表同源，一次遍历同时产出
        if (!m.muted)
        {
            continue;
        }

        MutedMember mm;
        mm.userid = m.userid;
        mm.name = m.name;
        mm.avatarText = m.avatarText;
        mm.avatarColor = m.avatarColor;
        mm.reason = m.muteReason;
        mm.permanent = (m.muteUntil <= 0);
        mm.remainingText = FormatMuteRemaining(m.muteUntil, nowMs);
        model.m_muted.push_back(mm);
    }

    //即便一个成员都没有也要置为"已加载" —— 界面要靠它区分"还没拉回来"（显示
    //加载提示）与"拉回来了"（按真实列表显示）
    model.m_membersState = kLoadDone;
}

CString FormatAnnouncementTime(int64_t timestamp, int64_t nowMs)
{
    if (timestamp <= 0)
    {
        return CString();
    }

    //Unix 毫秒 → 本地时间。localtime 的入参是秒。
    const time_t seconds = static_cast<time_t>(timestamp / kMillisPerSecond);
    struct tm when;
    if (localtime_s(&when, &seconds) != 0)
    {
        return CString();
    }

    const time_t nowSeconds = static_cast<time_t>(nowMs / kMillisPerSecond);
    struct tm now;
    if (localtime_s(&now, &nowSeconds) != 0)
    {
        return CString();
    }

    CString text;
    if (when.tm_year == now.tm_year && when.tm_yday == now.tm_yday)
    {
        //当天发的用"今天"更好读
        text.Format(_T("今天 %02d:%02d"), when.tm_hour, when.tm_min);
    }
    else if (when.tm_year == now.tm_year)
    {
        text.Format(_T("%d/%d %02d:%02d"),
                    when.tm_mon + 1, when.tm_mday, when.tm_hour, when.tm_min);
    }
    else
    {
        //跨年的公告精确到分没有意义，给到日即可
        text.Format(_T("%d/%d/%d"), when.tm_year + 1900, when.tm_mon + 1, when.tm_mday);
    }
    return text;
}

void ApplyGroupAnnouncements(const net::GroupAnnouncementsResult& result, int64_t nowMs,
                             GroupManageModel& model)
{
    model.m_announcements.clear();

    for (size_t i = 0; i < result.m_announcements.size(); ++i)
    {
        const net::GroupAnnouncementItem& src = result.m_announcements[i];

        GroupAnnouncement a;
        a.id       = src.m_id;
        a.title    = Utf8ToCString(src.m_title);
        a.content  = Utf8ToCString(src.m_content);
        a.severity = src.m_severity;
        a.publisher = src.m_publisher;

        //发布人昵称由服务端下发（发布人可能已退群，本地查不到）；取不到时退回
        //显示 userid，至少让人知道是谁发的
        a.publisherName = Utf8ToCString(src.m_publishername);
        if (a.publisherName.IsEmpty() && src.m_publisher > 0)
        {
            a.publisherName.Format(_T("%lld"), static_cast<long long>(src.m_publisher));
        }

        a.timeText = FormatAnnouncementTime(src.m_publishtime, nowMs);

        //标题为空的公告在界面上是一张点不开也读不懂的空卡片。解析层已经挡过没有ID
        //的项，这里再挡一次没有标题的 —— 服务端把标题定为必填，出现空标题说明数据
        //有问题，不如不显示。
        if (a.title.IsEmpty())
        {
            continue;
        }

        model.m_announcements.push_back(a);
    }

    //总条数取服务端给的 total（不受本次分页限制）——「共 N 条 · 第 x/y 页」的
    //N 与页数、以及左侧导航的计数徽章都用它。
    model.m_announcementTotal = result.m_total;

    //当前页可能已经越界（比如刚删掉最后一页的唯一一条），夹回最后一页
    const int pageCount = model.AnnouncementPageCount();
    if (model.m_announcementPage >= pageCount)
    {
        model.m_announcementPage = pageCount - 1;
    }
    if (model.m_announcementPage < 0)
    {
        model.m_announcementPage = 0;
    }

    //即便一条都没有也要置为"已加载" —— 界面要靠它区分"还没拉回来"（显示加载中）
    //与"拉过了但确实没有公告"（显示空状态提示）。
    model.m_announcementsLoaded = true;
}

void ApplyJoinRequests(const net::GroupJoinRequestsResult& requests, int64_t nowMs,
                       GroupManageModel& model)
{
    model.m_requests.clear();

    for (size_t i = 0; i < requests.m_requests.size(); ++i)
    {
        const net::GroupJoinRequestItem& src = requests.m_requests[i];

        JoinRequest r;
        r.requestid = src.m_requestid;
        r.userid = src.m_userid;
        r.name = Utf8ToCString(src.m_nickname);
        if (r.name.IsEmpty())
        {
            r.name.Format(_T("%lld"), static_cast<long long>(src.m_userid));
        }
        r.account.Format(_T("%lld"), static_cast<long long>(src.m_userid));
        r.avatarText = FirstCharOf(r.name);
        r.avatarColor = balloonmain::AvatarColorForUser(src.m_userid);
        r.message = Utf8ToCString(src.m_verifymsg);
        r.source = src.m_source;
        r.timeText = FormatRelativeTime(src.m_timestamp, nowMs);

        //推荐人只有"好友邀请"这一来源才有意义。这里只存 userid 的文本形式 ——
        //把它换成昵称需要查通讯录缓存，那是窗口层的事，纯逻辑层拿不到。
        if (src.m_source == kJoinFromFriend && src.m_recommender > 0)
        {
            r.recommender.Format(_T("%lld"), static_cast<long long>(src.m_recommender));
        }

        model.m_requests.push_back(r);
    }

    //空列表同样置为"已加载"：没有待处理申请是合法结果，不能一直显示加载提示
    model.m_requestsState = kLoadDone;
}

void ApplySensitiveWords(const net::SensitiveWordsResult& words, int64_t nowMs,
                         GroupManageModel& model)
{
    model.m_sensitiveWords.clear();

    for (size_t i = 0; i < words.m_words.size(); ++i)
    {
        const net::SensitiveWordItem& src = words.m_words[i];

        SensitiveWord w;
        w.id = src.m_id;
        w.word = Utf8ToCString(src.m_word);
        if (w.id <= 0 || w.word.IsEmpty())
        {
            //没有词条ID就删不掉、空词也没有展示价值 —— 解析层已经挡过一道，
            //这里再挡一次，两边都便宜。
            continue;
        }
        w.timeText = FormatRelativeTime(src.m_timestamp, nowMs);
        model.m_sensitiveWords.push_back(w);
    }

    //即便词库是空的也要置为"已加载" —— 界面要靠它区分"还没拉回来"（显示
    //加载中）与"拉过了但确实没有词"（显示空状态提示）。
    model.m_sensitiveWordsLoaded = true;
}

// =====================================================================
// 失败提示
// =====================================================================

CString DescribeGroupOpFailure(int cmd, int code, const std::string& msgUtf8)
{
    //协议命令字在此复制一份。本模块另抄而不 include net/Protocol.h：原先是因为
    //协议头与 v1 的 net/Msg.h 有同名枚举冲突（v1 协议头已于 2026-08-14 删除，该限制
    //不复存在），现在的理由是让本模块能被测试工程直接编译、不拖进整个协议头。
    const int kCmdKickMember      = 1310;   // 移出成员
    const int kCmdSetMemberRole   = 1312;   // 设 / 撤管理员
    const int kCmdMuteMember      = 1313;   // 禁言 / 解禁
    const int kCmdTransferOwner   = 1316;   // 转让群主
    const int kCmdDismissGroup    = 1302;   // 解散群
    const int kCmdHandleJoinGroup = 1308;   // 审批加群

    const int kErrNotGroupMember     = 302;  // 目标不是群成员
    const int kErrNotGroupAdmin      = 303;  // 本人不是群主 / 管理员
    //通用无权限。此值须与 net/Protocol.h 的 error_code_permission_denied 一致 ——
    //先前这里误抄成 12，导致服务端拒绝时（如非群主设管理员）匹配不上，
    //下面那些逐操作的中文提示一句都走不到，只能把服务端的英文原文抛给用户。
    const int kErrPermissionDenied   = 6;
    const int kErrCannotOperateOwner = 307;  // 不能对群主执行该操作

    //先按错误码给通用解释 —— 同一个错误码在各操作下的含义是一致的
    if (code == kErrNotGroupAdmin)
    {
        return CString(_T("你不是本群的群主或管理员，无法执行该操作。"));
    }
    if (code == kErrCannotOperateOwner)
    {
        return CString(_T("不能对群主执行该操作。"));
    }
    if (code == kErrNotGroupMember)
    {
        //目标已经不在群里了 —— 多半是别人刚把他移出去、本端列表还没刷新
        return CString(_T("该成员已不在本群，请刷新后重试。"));
    }

    if (code == kErrPermissionDenied)
    {
        //无权限在不同操作下的原因不同，逐条说清楚比笼统说"无权限"有用
        switch (cmd)
        {
        case kCmdSetMemberRole:
            return CString(_T("只有群主可以设置或撤销管理员。"));
        case kCmdMuteMember:
            return CString(_T("你没有权限禁言该成员（管理员不能禁言其他管理员）。"));
        case kCmdKickMember:
            return CString(_T("你没有权限移出该成员（管理员不能移出其他管理员）。"));
        case kCmdTransferOwner:
            return CString(_T("只有群主可以转让群主。"));
        case kCmdDismissGroup:
            return CString(_T("只有群主可以解散群聊。"));
        case kCmdHandleJoinGroup:
            return CString(_T("只有群主或管理员可以审批加群申请。"));
        default:
            return CString(_T("你没有权限执行该操作。"));
        }
    }

    //没有对应条目：给一句通用提示，并把服务端原文附上，免得线索被吞掉
    CString tip(_T("操作失败。"));
    if (!msgUtf8.empty())
    {
        CString detail = Utf8ToCString(msgUtf8);
        if (!detail.IsEmpty())
        {
            tip += _T("（");
            tip += detail;
            tip += _T("）");
        }
    }
    return tip;
}

// =====================================================================
// 角色门控
// =====================================================================

bool IsPageVisibleForRole(int pageId, int myRole)
{
    //页面 id 与 GroupManageLayout.h 的 GmPageId 一致。本文件不 include 那个头
    //（它依赖 balloonui 的控件类型，而本模块要保持只依赖 std 与 ATL 字符串），
    //故在此复述一份常量并注明出处。
    const int kPageMembers   = 1001;   // 成员管理
    const int kPageApproval  = 1002;   // 加群审批
    const int kPageMute      = 1003;   // 禁言管理
    const int kPageBasicInfo = 1004;   // 基本信息
    const int kPagePerms     = 1005;   // 群权限
    const int kPageDanger    = 1006;   // 危险区域
    const int kPageSensitive = 1007;   // 管理词库（群权限页的二级界面）
    const int kPageAnnouncement = 1008;   // 群公告

    switch (pageId)
    {
    case kPageMembers:
    case kPageBasicInfo:
        //所有成员都能看，普通成员为只读
        return true;

    case kPageApproval:
    case kPageMute:
    case kPagePerms:
    case kPageSensitive:
        //管理词库是群权限页的二级界面，可见性与它一致
        return (myRole == kRoleOwner || myRole == kRoleAdmin);

    case kPageAnnouncement:
        //这一页是"发布与管理"公告的地方，只有能发的人需要进来。普通成员看公告
        //走群聊窗右侧边栏的「公告」Tab，那里是只读的。
        return (myRole == kRoleOwner || myRole == kRoleAdmin);

    case kPageDanger:
        //解散群与转让群主只有群主能做，整页对管理员都不显示
        return (myRole == kRoleOwner);

    default:
        return false;
    }
}

// =====================================================================
// 成员右键菜单
// =====================================================================

// 往菜单模型里追加一个功能项
static void AppendEntry(std::vector<MemberMenuEntry>& out, UINT id, const wchar_t* label)
{
    MemberMenuEntry e;
    e.m_id = id;
    e.m_isSeparator = false;
    e.m_label = label;
    out.push_back(e);
}

// 往菜单模型里追加一条分隔线
static void AppendSeparator(std::vector<MemberMenuEntry>& out)
{
    MemberMenuEntry e;
    e.m_id = 0;
    e.m_isSeparator = true;
    e.m_label = NULL;
    out.push_back(e);
}

int MemberIndexFromCardId(UINT ctrlId)
{
    //区间与 GroupManageLayout.h 的 GmCtrlId 一致。本模块不 include 那个头
    //（它依赖 balloonui 的控件类型），故在此复述并注明出处。
    const UINT kCardBase = 2400;   // kIdMemberCardBase
    const UINT kCardMax  = 2899;   // kIdMemberCardMax

    if (ctrlId < kCardBase || ctrlId > kCardMax)
    {
        return -1;   //右键点在别的控件上，不是任何一张成员卡
    }
    return (int)(ctrlId - kCardBase);
}

std::vector<MemberMenuEntry> BuildMemberMenuModel(const MemberMenuState& state)
{
    std::vector<MemberMenuEntry> entries;

    //对自己右键：能看自己的资料，但没有任何可施加于自己的管理动作。
    //"给自己发消息"也没有意义，故一并去掉。
    if (state.m_isSelf)
    {
        AppendEntry(entries, kMemberMenuProfile, L"查看资料");
        return entries;
    }

    AppendEntry(entries, kMemberMenuSendMsg, L"发消息");
    AppendEntry(entries, kMemberMenuProfile, L"查看资料");

    //"群主是权限顶点、谁都不能对他设管理员 / 禁言 / 移出"这条已经收进下面两个
    //共用判定里，故此处不再单独算 targetIsOwner。

    //设 / 撤管理员：判定收在 GroupRoleRules 里，与群聊窗成员侧栏 / 气泡 @ 菜单
    //共用同一份规则。那个模块用的是<u>网络侧</u>角色取值（群主为 2），与本文件
    //界面侧的 GroupRole（群主为 0）顺序相反，故须先转换再问。
    const bool canChangeRole = grouprole::CanSetAdmin(
        UiRoleToNetRole(state.m_myRole),
        UiRoleToNetRole(state.m_targetRole),
        state.m_isSelf);

    //禁言与移出：群主可对任何非群主成员用；管理员只能对普通成员用。
    //判定同样收在 GroupRoleRules 里，与群聊窗成员侧栏 / 气泡 @ 菜单共用 ——
    //"移出本群"两处都有，规则各写一份必然漂移。禁言的门控与移出完全一致，
    //故共用同一个判定，不再单列。
    const bool canModerate = grouprole::CanKickMember(
        UiRoleToNetRole(state.m_myRole),
        UiRoleToNetRole(state.m_targetRole),
        state.m_isSelf);

    if (canChangeRole || canModerate)
    {
        AppendSeparator(entries);
    }

    if (canChangeRole)
    {
        if (grouprole::ShouldOfferCancelAdmin(UiRoleToNetRole(state.m_targetRole)))
        {
            AppendEntry(entries, kMemberMenuCancelAdmin, L"撤销管理员");
        }
        else
        {
            AppendEntry(entries, kMemberMenuSetAdmin, L"设为管理员");
        }
    }

    if (canModerate)
    {
        if (state.m_targetMuted)
        {
            AppendEntry(entries, kMemberMenuUnmute, L"解除禁言");
        }
        else
        {
            AppendEntry(entries, kMemberMenuMute, L"禁言");
        }

        //移出本群与上面几项之间再隔一道 —— 它是这个菜单里唯一不可撤销的操作，
        //不该紧挨着禁言，避免手滑点错。
        AppendSeparator(entries);
        AppendEntry(entries, kMemberMenuKick, L"移出本群");
    }

    return entries;
}

} // namespace balloongroupmanage
