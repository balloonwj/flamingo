/**
 *  主面板压测数据生成器的实现。整体说明见 MainPanelStressData.h。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#include "stdafx.h"
#include "MainPanelStressData.h"

#include <time.h>
#include <random>
#include <algorithm>
#include <string>
#include <vector>

#include "../../net/Protocol.h"   // presence_type / conversation_type / message_type

namespace balloonmain {

namespace {

// 姓与名的取样表。组合出来的名字有大量同姓，搜索"zhang"一类的拼音首字母时命中数与真实
// 企业通讯录接近，搜索的开销才有参考价值。
const char* const kSurnames[] =
{
    "张", "王", "李", "赵", "刘", "陈", "杨", "黄", "周", "吴",
    "徐", "孙", "马", "朱", "胡", "郭", "何", "高", "林", "罗",
};
const char* const kGivenNames[] =
{
    "伟", "芳", "娜", "敏", "静", "磊", "洋", "勇", "军", "杰",
    "涛", "明", "超", "秀英", "华", "平", "刚", "桂英", "志强", "思源",
    "晓东", "雪梅", "海燕", "建国", "子涵", "浩然", "欣怡", "宇航", "诗琪", "俊杰",
};

// 部门名的取样表，与序号拼成"研发中心 3"一类的名字。
const char* const kDeptNames[] =
{
    "研发中心", "产品部", "市场部", "销售部", "财务部", "人力资源部", "法务部",
    "客服中心", "运维部", "测试部", "设计部", "行政部", "采购部", "质量部",
};

// 群名的取样表，与序号拼成群名。
const char* const kGroupNames[] =
{
    "项目组", "技术交流群", "周报同步", "值班群", "新人答疑", "发布协调",
    "需求评审", "午餐搭子", "读书会", "跨部门协作",
};

// 会话最后一条消息的预览文字取样表。长短不一，覆盖需要截断显示的情形。
const char* const kDigests[] =
{
    "好的，收到",
    "今天下午三点开会，别忘了带上周的数据",
    "[图片]",
    "[文件] 2026年第三季度经营分析报告（终版）.xlsx",
    "这个问题我们明天再对一下吧，今天先把发布的事情处理完",
    "OK",
    "辛苦了！",
    "[语音通话] 通话时长 03:21",
    "麻烦把合同的扫描件发我一份，谢谢",
    "周五团建地点定在郊野公园，大家记得穿运动鞋",
};

// 服务端对特别关注与收藏的数量上限（StarredProtocol.h / FavoriteProtocol.h），
// 压测数据不超过它们，否则造出来的是正式环境里不可能出现的数据。
const int kStarredCount         = 50;    // 特别关注人数
const int kFavoriteContactCount = 150;   // 收藏的联系人数
const int kFavoriteGroupCount   = 50;    // 收藏的群数

// 各类 ID 的起始值，彼此错开，排查日志时一眼能看出是哪类对象。
const int64_t kFirstUserId  = 100000;   // 联系人 userid 起点
const int64_t kFirstGroupId = 900000;   // 群 id 起点
const int64_t kFirstDeptId  = 1;        // 部门 id 起点

// 部门层级的划分：顶级部门占总数的六分之一，二级部门数为顶级的三倍，其余为三级。
const int kTopDeptDivisor      = 6;   // 部门总数除以它得到顶级部门数
const int kSecondLevelMultiple = 3;   // 二级部门数是顶级部门数的几倍

// 在线状态分布（百分比累计阈值）：在线 40%、离开 10%、勿扰 5%，其余离线。
const int kPercent             = 100;
const int kOnlineUpperPercent  = 40;
const int kAwayUpperPercent    = 50;
const int kBusyUpperPercent    = 55;

// 约一成联系人另有一个兼属部门。
const int kSecondaryDeptPercent = 10;

// 会话属性分布。
const int kUnreadPercent       = 30;    // 有未读消息的会话占比（百分比）
const int kMaxUnread           = 120;   // 单个会话的最大未读数，覆盖 99+ 的显示
const int kPinnedCount         = 8;     // 置顶会话数
const int kMuteDivisor         = 20;    // 每 20 个会话中约 1 个免打扰
const int kGroupConvPercent    = 17;    // 会话中群聊的占比（百分比），约 500 / 3000

// 会话时间的分布：最近一条在"现在"，往前每条间隔 1 ~ 30 分钟，覆盖今天、昨天、更早三种显示。
const int64_t kMsPerMinute         = 60 * 1000;
const int     kMaxGapMinutes       = 30;
const int64_t kMsPerSecond         = 1000;

// 群成员数的取值范围。
const int kMinGroupMembers = 3;
const int kMaxGroupMembers = 2000;

// 在 [0, n) 内取一个均匀分布的随机整数。n <= 0 时返回 0。
int RandomBelow(std::mt19937& rng, int n)
{
    if (n <= 0)
    {
        return 0;
    }
    std::uniform_int_distribution<int> dist(0, n - 1);
    return dist(rng);
}

// 取一个随机的中文姓名（UTF-8）。
std::string RandomName(std::mt19937& rng)
{
    std::string name = kSurnames[RandomBelow(rng, (int)_countof(kSurnames))];
    name += kGivenNames[RandomBelow(rng, (int)_countof(kGivenNames))];
    return name;
}

// 按百分比分布取一个在线状态。
int RandomPresence(std::mt19937& rng)
{
    const int roll = RandomBelow(rng, kPercent);
    if (roll < kOnlineUpperPercent)
    {
        return presence_online;
    }
    if (roll < kAwayUpperPercent)
    {
        return presence_away;
    }
    if (roll < kBusyUpperPercent)
    {
        return presence_busy;
    }
    return presence_offline;
}

// 生成三级部门树，结果按 deptid 升序存放。
void BuildDepartments(std::mt19937& rng, int deptCount, std::vector<net::DepartmentItem>& depts)
{
    depts.clear();
    const int topCount    = (std::max)(1, deptCount / kTopDeptDivisor);
    const int secondCount = (std::min)(deptCount - topCount, topCount * kSecondLevelMultiple);

    for (int i = 0; i < deptCount; ++i)
    {
        net::DepartmentItem dept;
        dept.m_deptid    = kFirstDeptId + i;
        dept.m_deptorder = i;
        dept.m_name      = kDeptNames[i % _countof(kDeptNames)];
        dept.m_name     += " ";
        dept.m_name     += std::to_string(i + 1);

        if (i < topCount)
        {
            //顶级部门
            dept.m_parentdeptid = 0;
        }
        else if (i < topCount + secondCount)
        {
            //二级部门：挂在随机一个顶级部门下
            dept.m_parentdeptid = kFirstDeptId + RandomBelow(rng, topCount);
        }
        else
        {
            //三级部门：挂在随机一个二级部门下
            dept.m_parentdeptid = kFirstDeptId + topCount + RandomBelow(rng, (std::max)(1, secondCount));
        }
        depts.push_back(dept);
    }
}

} // anonymous namespace

int64_t StressDataSet::ContactUserIdAt(int index) const
{
    const std::vector<net::ContactItem>& pool = m_contacts.m_contacts;
    if (pool.empty() || index < 0)
    {
        return 0;
    }
    return pool[(size_t)index % pool.size()].m_userid;
}

void BuildStressData(const StressDataSpec& spec, StressDataSet& out)
{
    std::mt19937 rng(spec.m_seed);

    const int contactCount = (std::max)(1, spec.m_contactCount);
    const int deptCount    = (std::max)(1, spec.m_departmentCount);
    const int groupCount   = (std::max)(0, spec.m_groupCount);

    // ---- 通讯录 ----
    net::ContactsResult& contacts = out.m_contacts;
    contacts = net::ContactsResult();
    contacts.m_code           = 0;
    contacts.m_enterpriseid   = 1;
    contacts.m_enterprisename = "压测科技有限公司";
    BuildDepartments(rng, deptCount, contacts.m_departments);

    contacts.m_contacts.reserve((size_t)contactCount);
    for (int i = 0; i < contactCount; ++i)
    {
        net::ContactItem item;
        item.m_userid    = kFirstUserId + i;
        item.m_username  = "u" + std::to_string(kFirstUserId + i);
        item.m_nickname  = RandomName(rng);
        item.m_usertype  = 0;
        item.m_facetype  = 0;
        item.m_signature = kDigests[RandomBelow(rng, (int)_countof(kDigests))];
        item.m_presence  = RandomPresence(rng);
        contacts.m_contacts.push_back(item);

        //主部门
        net::DeptMemberItem primary;
        primary.m_userid      = item.m_userid;
        primary.m_isprimary   = 1;
        primary.m_memberorder = i;
        primary.m_position    = "工程师";
        contacts.m_departments[(size_t)RandomBelow(rng, deptCount)].m_members.push_back(primary);

        //约一成另有一个兼属部门
        if (RandomBelow(rng, kPercent) < kSecondaryDeptPercent)
        {
            net::DeptMemberItem secondary = primary;
            secondary.m_isprimary = 0;
            contacts.m_departments[(size_t)RandomBelow(rng, deptCount)].m_members.push_back(secondary);
        }
    }

    //特别关注与收藏：从打乱后的联系人序列里依次取，保证不重复
    std::vector<int> order((size_t)contactCount);
    for (int i = 0; i < contactCount; ++i)
    {
        order[(size_t)i] = i;
    }
    std::shuffle(order.begin(), order.end(), rng);

    //特别关注是会话的提醒模式，名单随会话列表应答下发，这里先记下，建会话列表时再写进去
    const int starred = (std::min)(kStarredCount, contactCount);
    std::vector<int64_t> starredIds;
    for (int i = 0; i < starred; ++i)
    {
        starredIds.push_back(kFirstUserId + order[(size_t)i]);
    }
    const int favContacts = (std::min)(kFavoriteContactCount, contactCount);
    for (int i = 0; i < favContacts; ++i)
    {
        net::FavoriteItem fav;
        fav.m_ctype = conversation_single;
        fav.m_cid   = kFirstUserId + order[(size_t)((i + starred) % contactCount)];
        contacts.m_favorites.push_back(fav);
    }
    const int favGroups = (std::min)(kFavoriteGroupCount, groupCount);
    for (int i = 0; i < favGroups; ++i)
    {
        net::FavoriteItem fav;
        fav.m_ctype = conversation_group;
        fav.m_cid   = kFirstGroupId + i;
        contacts.m_favorites.push_back(fav);
    }

    // ---- 群列表 ----
    net::UserGroupsResult& groups = out.m_groups;
    groups = net::UserGroupsResult();
    groups.m_code = 0;
    for (int i = 0; i < groupCount; ++i)
    {
        net::UserGroupItem group;
        group.m_groupid     = kFirstGroupId + i;
        group.m_name        = kGroupNames[i % _countof(kGroupNames)];
        group.m_name       += " ";
        group.m_name       += std::to_string(i + 1);
        group.m_ownerid     = kFirstUserId + RandomBelow(rng, contactCount);
        group.m_membercount = kMinGroupMembers + RandomBelow(rng, kMaxGroupMembers - kMinGroupMembers);
        group.m_facetype    = 0;
        group.m_pinned      = (i < kPinnedCount) ? 1 : 0;
        groups.m_groups.push_back(group);
    }

    // ---- 会话列表 ----
    net::ConversationsResult& convs = out.m_conversations;
    convs = net::ConversationsResult();
    convs.m_code = 0;
    for (size_t i = 0; i < starredIds.size(); ++i)
    {
        net::NotifyModeEntry entry;
        entry.m_ctype = conversation_single;
        entry.m_cid   = starredIds[i];
        entry.m_mode  = notify_mode_starred;
        convs.m_notifyModes.push_back(entry);
    }

    const int maxConvs  = contactCount + groupCount;
    const int convCount = (std::min)((std::max)(0, spec.m_conversationCount), maxConvs);
    int groupConvs      = (std::min)(groupCount, convCount * kGroupConvPercent / kPercent);
    int singleConvs     = convCount - groupConvs;
    if (singleConvs > contactCount)
    {
        singleConvs = contactCount;
        groupConvs  = convCount - singleConvs;
    }

    //单聊对象与群聊对象各自打乱后取前若干个，再把两类混在一起打乱，模拟真实的交错
    std::vector<net::ConversationItem> items;
    items.reserve((size_t)convCount);
    std::shuffle(order.begin(), order.end(), rng);
    for (int i = 0; i < singleConvs; ++i)
    {
        net::ConversationItem conv;
        conv.m_ctype    = conversation_single;
        conv.m_cid      = kFirstUserId + order[(size_t)i];
        conv.m_lastFrom = conv.m_cid;
        items.push_back(conv);
    }
    for (int i = 0; i < groupConvs; ++i)
    {
        net::ConversationItem conv;
        conv.m_ctype    = conversation_group;
        conv.m_cid      = kFirstGroupId + i;
        conv.m_lastFrom = kFirstUserId + RandomBelow(rng, contactCount);
        items.push_back(conv);
    }
    std::shuffle(items.begin(), items.end(), rng);

    int64_t timestamp = (int64_t)::time(NULL) * kMsPerSecond;
    for (size_t i = 0; i < items.size(); ++i)
    {
        net::ConversationItem& conv = items[i];
        conv.m_lastmsgid   = (int64_t)(i + 1);
        conv.m_lastMsgType = message_type_text;
        conv.m_digest      = kDigests[RandomBelow(rng, (int)_countof(kDigests))];
        conv.m_timestamp   = timestamp;
        conv.m_unread      = (RandomBelow(rng, kPercent) < kUnreadPercent) ? 1 + RandomBelow(rng, kMaxUnread) : 0;
        conv.m_pinned      = ((int)i < kPinnedCount) ? 1 : 0;
        conv.m_notifyMode  = (RandomBelow(rng, kMuteDivisor) == 0) ? notify_mode_silent : notify_mode_allow;
        timestamp         -= (1 + RandomBelow(rng, kMaxGapMinutes)) * kMsPerMinute;
    }
    convs.m_items.swap(items);
    convs.m_synctime = (int64_t)::time(NULL) * kMsPerSecond;
}

} // namespace balloonmain
