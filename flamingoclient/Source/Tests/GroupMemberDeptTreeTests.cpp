/**
 *  GroupMemberDeptTreeTests.cpp
 *
 *  群聊窗「成员」页签的部门树视图（标签 [memberdepttree]，docs/群成员部门树方案.md）：
 *
 *    MT1 纯函数：只出现含本群成员的部门，没有本群成员的部门被剪掉、上级部门保留；人数按下级合计且同一人
 *        只算一次；下级部门在前、成员在后，成员按群角色再按部门内排序值
 *    MT2 纯函数：兼属的人在两个部门下都出现，兼属那一行标记为兼属；不属于任何部门的成员在最后的「未分配部门」下；
 *        部门上级链成环时不死循环、每个部门只出现一次
 *    MT3 列表：部门树视图下行数与纯函数输出一致；折叠一个部门后它的下级行都不可见，其后各行上移，展开后恢复
 *    MT4 搜索：部门树视图下命中成员所在的部门及上级显示，其余部门隐藏；清空关键字后恢复折叠前的状态；平铺视图照旧
 *    MT5 在线状态：同一个人在两个部门下各一行，在线状态变化后两行都更新
 *    MT6 配置：[GroupDlg] MemberView 写入读回；没写过时为平铺
 *
 *  用 Catch2 v3（amalgamated 形态）。全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <memory>
#include <string>
#include <vector>

#include "../UI/BalloonChatWnd/GroupMemberDeptTree.h"
#include "../UI/BalloonChatWnd/GroupDeptHeaderItem.h"
#include "../UI/BalloonChatWnd/GroupMemberItem.h"
#include "../UI/BalloonChatWnd/GroupMemberList.h"
#include "../Session/UserConfig.h"

using namespace balloonchatwnd;

namespace {

// 群角色取值（与 grouprole::NetGroupRole 一致）：0 普通成员、1 管理员、2 群主
const int kRoleMember = 0;
const int kRoleAdmin  = 1;
const int kRoleOwner  = 2;

// 部门 id：公司（顶级）、研发部、前端组（研发部下）、后端组（研发部下）、市场部（顶级，没有本群成员）
const int64_t kDeptCompany  = 10;
const int64_t kDeptRd       = 20;
const int64_t kDeptFrontend = 21;
const int64_t kDeptBackend  = 22;
const int64_t kDeptMarket   = 30;

// 成员 userid：张三（群主，前端）、李四（前端，兼后端）、王五（管理员，后端）、赵六（研发部直属）、
// 钱七（不在任何部门）、孙八（市场部，不在群里）
const int64_t kZhang = 1001;
const int64_t kLi    = 1002;
const int64_t kWang  = 1003;
const int64_t kZhao  = 1004;
const int64_t kQian  = 1005;
const int64_t kSun   = 1006;

// 列表尺寸：宽 240，可视高度容纳 20 行
const int kListW = 240;
const int kListRows = 20;
const int kListH = kListRows * kGroupMemberRowHeight;

// 在线状态：1 在线、0 离线（同 presence_type）
const int kPresenceOnline  = 1;
const int kPresenceOffline = 0;

// 造一个部门
net::DepartmentItem MakeDept(int64_t id, const char* name, int64_t parent, int order)
{
    net::DepartmentItem d;
    d.m_deptid       = id;
    d.m_name         = name;
    d.m_parentdeptid = parent;
    d.m_deptorder    = order;
    return d;
}

// 往部门里加一位成员
void AddDeptMember(net::DepartmentItem& d, int64_t uid, bool primary, int order)
{
    net::DeptMemberItem m;
    m.m_userid      = uid;
    m.m_isprimary   = primary ? 1 : 0;
    m.m_memberorder = order;
    d.m_members.push_back(m);
}

// 样例部门：公司 → 研发部 → { 前端组, 后端组 }；市场部（顶级，只有不在群里的孙八）
std::vector<net::DepartmentItem> SampleDepts()
{
    std::vector<net::DepartmentItem> depts;
    net::DepartmentItem company  = MakeDept(kDeptCompany, "Company", 0, 1);
    net::DepartmentItem rd       = MakeDept(kDeptRd, "R&D", kDeptCompany, 1);
    net::DepartmentItem frontend = MakeDept(kDeptFrontend, "Frontend", kDeptRd, 2);
    net::DepartmentItem backend  = MakeDept(kDeptBackend, "Backend", kDeptRd, 1);
    net::DepartmentItem market   = MakeDept(kDeptMarket, "Market", 0, 2);
    AddDeptMember(rd, kZhao, true, 1);
    AddDeptMember(frontend, kLi, true, 1);
    AddDeptMember(frontend, kZhang, true, 2);
    AddDeptMember(backend, kWang, true, 2);
    AddDeptMember(backend, kLi, false, 1);
    AddDeptMember(market, kSun, true, 1);
    depts.push_back(frontend);   //故意打乱顺序，验证按上级关系还原
    depts.push_back(company);
    depts.push_back(market);
    depts.push_back(backend);
    depts.push_back(rd);
    return depts;
}

// 样例群成员（服务端返回的顺序）
std::vector<DeptTreeMemberInput> SampleMembers()
{
    const int64_t ids[]   = { kQian, kLi, kZhang, kWang, kZhao };
    const int     roles[] = { kRoleMember, kRoleMember, kRoleOwner, kRoleAdmin, kRoleMember };
    std::vector<DeptTreeMemberInput> v;
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        DeptTreeMemberInput in;
        in.m_userId = ids[i];
        in.m_role   = roles[i];
        v.push_back(in);
    }
    return v;
}

// 成员显示名（列表用例按 userid 取）
CString NameOf(int64_t uid)
{
    switch (uid)
    {
    //张三
    case kZhang:
        return CString(L"Zhang");
    //李四
    case kLi:
        return CString(L"Li");
    //王五
    case kWang:
        return CString(L"Wang");
    //赵六
    case kZhao:
        return CString(L"Zhao");
    //钱七及其它
    default:
        return CString(L"Qian");
    }
}

// 按纯函数结果建一个部门树视图的列表，与群聊窗 RebuildMemberRows 的做法相同
std::unique_ptr<GroupMemberList> BuildTreeList(const std::vector<DeptTreeRow>& rows)
{
    std::unique_ptr<GroupMemberList> list(new GroupMemberList());
    list->SetTreeMode(true);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].m_kind == kDeptTreeRowDept)
        {
            std::unique_ptr<GroupDeptHeaderItem> head(new GroupDeptHeaderItem());
            head->SetDept(rows[i].m_deptId, CString(rows[i].m_deptName.c_str()), rows[i].m_memberCount,
                          rows[i].m_depth);
            list->AddRow(std::unique_ptr<balloonwjui::DuiControl>(head.release()), rows[i].m_parent);
            continue;
        }
        std::unique_ptr<GroupMemberItem> item(new GroupMemberItem());
        item->SetUserId(rows[i].m_userId);
        const CString name = NameOf(rows[i].m_userId);
        item->SetMember(name, name.Left(1), RGB(0x40, 0x80, 0xC0), kRoleMember);
        item->SetTreeLayout(rows[i].m_depth, rows[i].m_secondary);
        list->AddRow(std::unique_ptr<balloonwjui::DuiControl>(item.release()), rows[i].m_parent);
    }
    RECT rc = { 0, 0, kListW, kListH };
    list->SetRect(rc);
    list->ReapplyFilter();
    return list;
}

// 列表里可见行的个数
int VisibleCount(const GroupMemberList& list)
{
    return list.VisibleRowCount();
}

// 找第一个某部门 id 的部门标题行下标；找不到返回 -1
int HeaderIndex(const GroupMemberList& list, int64_t deptId)
{
    for (size_t i = 0; i < list.Children().size(); ++i)
    {
        const GroupDeptHeaderItem* h = dynamic_cast<const GroupDeptHeaderItem*>(list.Children()[i].get());
        if (h != nullptr && h->DeptId() == deptId)
            return (int)i;
    }
    return -1;
}

// 生成本进程专用的临时 ini 路径，并删掉上一次残留的同名文件
std::wstring FreshIniPath(const wchar_t* tag)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    wchar_t name[MAX_PATH] = { 0 };
    swprintf_s(name, L"%sflamingo_memberview_%s_%lu.ini", dir, tag, ::GetCurrentProcessId());
    ::DeleteFileW(name);
    return name;
}

} // anonymous namespace

TEST_CASE("MT1 部门树：剪掉没有本群成员的部门、保留上级，人数按下级合计去重，下级部门在前、成员按角色排", "[memberdepttree]")
{
    const std::vector<DeptTreeRow> rows = BuildGroupMemberDeptTree(SampleMembers(), SampleDepts());
    //期望的行：公司(4) / 研发部(4) / 后端组(2) / 王五 / 李四(兼) / 前端组(2) / 张三 / 李四 / 赵六 / 未分配部门(1) / 钱七
    const size_t kExpectedRows = 11;
    REQUIRE(rows.size() == kExpectedRows);

    CHECK(rows[0].m_kind == kDeptTreeRowDept);
    CHECK(rows[0].m_deptId == kDeptCompany);
    CHECK(rows[0].m_depth == 0);
    CHECK(rows[0].m_parent == -1);
    CHECK(rows[0].m_memberCount == 4);   //张三、李四、王五、赵六；李四在两个组里只算一次

    CHECK(rows[1].m_deptId == kDeptRd);
    CHECK(rows[1].m_parent == 0);
    CHECK(rows[1].m_memberCount == 4);

    //下级部门在前：后端组排序值 1，前端组排序值 2
    CHECK(rows[2].m_deptId == kDeptBackend);
    CHECK(rows[2].m_depth == 2);
    CHECK(rows[2].m_memberCount == 2);
    //后端组：管理员王五在前，兼属的李四在后（角色优先于部门内排序值）
    CHECK(rows[3].m_userId == kWang);
    CHECK(rows[3].m_depth == 3);
    CHECK(rows[3].m_parent == 2);
    CHECK(rows[4].m_userId == kLi);

    CHECK(rows[5].m_deptId == kDeptFrontend);
    //前端组：群主张三在前
    CHECK(rows[6].m_userId == kZhang);
    CHECK(rows[7].m_userId == kLi);

    //研发部的直属成员排在它的下级部门之后
    CHECK(rows[8].m_kind == kDeptTreeRowMember);
    CHECK(rows[8].m_userId == kZhao);
    CHECK(rows[8].m_parent == 1);
    CHECK(rows[8].m_depth == 2);

    //市场部没有本群成员，不出现
    for (size_t i = 0; i < rows.size(); ++i)
    {
        CHECK(rows[i].m_deptId != kDeptMarket);
        CHECK(rows[i].m_userId != kSun);
    }
}

TEST_CASE("MT2 部门树：兼属标记、未分配部门、上级链成环", "[memberdepttree]")
{
    const std::vector<DeptTreeRow> rows = BuildGroupMemberDeptTree(SampleMembers(), SampleDepts());
    REQUIRE(rows.size() > 9);
    //李四在后端组是兼属、在前端组是主部门
    CHECK(rows[4].m_userId == kLi);
    CHECK(rows[4].m_secondary);
    CHECK(rows[7].m_userId == kLi);
    CHECK_FALSE(rows[7].m_secondary);
    CHECK_FALSE(rows[3].m_secondary);

    //钱七不在任何部门：最后的「未分配部门」
    CHECK(rows[9].m_kind == kDeptTreeRowDept);
    CHECK(rows[9].m_deptId == kDeptTreeUnassignedId);
    CHECK(rows[9].m_deptName.empty());
    CHECK(rows[9].m_memberCount == 1);
    CHECK(rows[10].m_userId == kQian);
    CHECK(rows[10].m_parent == 9);

    //一个主部门都没有的人：兼属记录也不标「兼」
    std::vector<net::DepartmentItem> onlySecondary;
    net::DepartmentItem d = MakeDept(kDeptCompany, "Company", 0, 1);
    AddDeptMember(d, kZhao, false, 1);
    onlySecondary.push_back(d);
    std::vector<DeptTreeMemberInput> one(1);
    one[0].m_userId = kZhao;
    const std::vector<DeptTreeRow> r2 = BuildGroupMemberDeptTree(one, onlySecondary);
    REQUIRE(r2.size() == 2);
    CHECK_FALSE(r2[1].m_secondary);

    //上级链成环：A 的上级是 B、B 的上级是 A；两个部门各出现一次，不死循环
    std::vector<net::DepartmentItem> cyc;
    net::DepartmentItem a = MakeDept(kDeptRd, "A", kDeptFrontend, 1);
    net::DepartmentItem b = MakeDept(kDeptFrontend, "B", kDeptRd, 1);
    AddDeptMember(a, kZhao, true, 1);
    AddDeptMember(b, kLi, true, 1);
    cyc.push_back(a);
    cyc.push_back(b);
    std::vector<DeptTreeMemberInput> two(2);
    two[0].m_userId = kZhao;
    two[1].m_userId = kLi;
    const std::vector<DeptTreeRow> r3 = BuildGroupMemberDeptTree(two, cyc);
    int deptRows = 0;
    int memberRows = 0;
    for (size_t i = 0; i < r3.size(); ++i)
    {
        if (r3[i].m_kind == kDeptTreeRowDept)
            ++deptRows;
        else
            ++memberRows;
    }
    CHECK(deptRows == 2);
    CHECK(memberRows == 2);
    //部门自己指向自己：按顶级部门处理
    std::vector<net::DepartmentItem> self;
    self.push_back(MakeDept(kDeptRd, "Self", kDeptRd, 1));
    AddDeptMember(self[0], kZhao, true, 1);
    const std::vector<DeptTreeRow> r4 = BuildGroupMemberDeptTree(one, self);
    REQUIRE(r4.size() == 2);
    CHECK(r4[0].m_parent == -1);
}

TEST_CASE("MT3 列表：部门树视图下行数与纯函数一致，折叠后下级行隐藏、其后各行上移，展开后恢复", "[memberdepttree]")
{
    const std::vector<DeptTreeRow> rows = BuildGroupMemberDeptTree(SampleMembers(), SampleDepts());
    std::unique_ptr<GroupMemberList> list = BuildTreeList(rows);
    REQUIRE(list->Children().size() == rows.size());
    CHECK(VisibleCount(*list) == (int)rows.size());
    CHECK(list->MemberRowCount() == 6);   //李四两行

    //默认全部展开；记下「未分配部门」标题行的位置
    const int unassigned = HeaderIndex(*list, kDeptTreeUnassignedId);
    REQUIRE(unassigned >= 0);
    const RECT before = list->Children()[(size_t)unassigned]->GetRect();

    //折叠研发部：后端组、前端组、它们的成员、赵六（共 7 行）都隐藏，其后的行上移 7 行
    list->ToggleDeptCollapsed(kDeptRd);
    CHECK(list->IsDeptCollapsed(kDeptRd));
    const int kHiddenUnderRd = 7;
    CHECK(VisibleCount(*list) == (int)rows.size() - kHiddenUnderRd);
    for (size_t i = 2; i <= 8; ++i)
    {
        CHECK_FALSE(list->Children()[i]->IsVisible());
    }
    const RECT collapsed = list->Children()[(size_t)unassigned]->GetRect();
    CHECK(collapsed.top == before.top - kHiddenUnderRd * kGroupMemberRowHeight);
    const GroupDeptHeaderItem* rdHead =
        dynamic_cast<const GroupDeptHeaderItem*>(list->Children()[1].get());
    REQUIRE(rdHead != nullptr);
    CHECK(rdHead->IsCollapsed());

    //再折叠它下面的后端组，然后展开研发部：后端组仍是折叠的，它的成员仍隐藏
    list->ToggleDeptCollapsed(kDeptBackend);
    list->ToggleDeptCollapsed(kDeptRd);
    CHECK(list->Children()[2]->IsVisible());
    CHECK_FALSE(list->Children()[3]->IsVisible());
    CHECK(list->Children()[5]->IsVisible());
    //全部展开：矩形恢复
    list->ToggleDeptCollapsed(kDeptBackend);
    CHECK(VisibleCount(*list) == (int)rows.size());
    const RECT after = list->Children()[(size_t)unassigned]->GetRect();
    CHECK(after.top == before.top);

    //重建行时折叠状态按部门 id 保留
    list->ToggleDeptCollapsed(kDeptFrontend);
    list->ClearRows();
    CHECK(list->Children().empty());
    CHECK(list->IsDeptCollapsed(kDeptFrontend));
}

TEST_CASE("MT4 搜索：命中成员所在部门及上级显示并展开，其余部门隐藏；清空后恢复折叠；平铺视图照旧", "[memberdepttree]")
{
    const std::vector<DeptTreeRow> rows = BuildGroupMemberDeptTree(SampleMembers(), SampleDepts());
    std::unique_ptr<GroupMemberList> list = BuildTreeList(rows);
    //先把研发部折叠起来
    list->ToggleDeptCollapsed(kDeptRd);

    //搜「Wang」：公司、研发部、后端组、王五四行可见；研发部虽然折叠着，搜索时照样展开
    list->SetFilterKeyword(CString(L"Wang"));
    CHECK(VisibleCount(*list) == 4);
    CHECK(list->Children()[0]->IsVisible());
    CHECK(list->Children()[1]->IsVisible());
    CHECK(list->Children()[2]->IsVisible());
    CHECK(list->Children()[3]->IsVisible());
    CHECK_FALSE(list->Children()[5]->IsVisible());   //前端组没有命中的人
    CHECK_FALSE(list->Children()[(size_t)HeaderIndex(*list, kDeptTreeUnassignedId)]->IsVisible());
    CHECK_FALSE(list->HasNoMatches());

    //搜「Li」：两个组里的李四都显示
    list->SetFilterKeyword(CString(L"Li"));
    CHECK(list->Children()[4]->IsVisible());
    CHECK(list->Children()[7]->IsVisible());

    //没有命中
    list->SetFilterKeyword(CString(L"nobody"));
    CHECK(VisibleCount(*list) == 0);
    CHECK(list->HasNoMatches());

    //清空关键字：研发部仍是折叠的
    list->SetFilterKeyword(CString());
    CHECK(list->IsDeptCollapsed(kDeptRd));
    CHECK(list->Children()[1]->IsVisible());
    CHECK_FALSE(list->Children()[2]->IsVisible());

    //平铺视图：搜索只按名字过滤成员行
    std::unique_ptr<GroupMemberList> flat(new GroupMemberList());
    const int64_t flatIds[] = { kZhang, kWang, kLi };
    for (size_t i = 0; i < sizeof(flatIds) / sizeof(flatIds[0]); ++i)
    {
        std::unique_ptr<GroupMemberItem> item(new GroupMemberItem());
        item->SetUserId(flatIds[i]);
        item->SetMember(NameOf(flatIds[i]), NameOf(flatIds[i]).Left(1), RGB(0x40, 0x80, 0xC0), kRoleMember);
        flat->AddRow(std::unique_ptr<balloonwjui::DuiControl>(item.release()), -1);
    }
    RECT rc = { 0, 0, kListW, kListH };
    flat->SetRect(rc);
    flat->SetFilterKeyword(CString(L"Wang"));
    CHECK(VisibleCount(*flat) == 1);
    CHECK(flat->Children()[1]->IsVisible());
    flat->SetFilterKeyword(CString());
    CHECK(VisibleCount(*flat) == 3);
}

TEST_CASE("MT5 同一个人在两个部门下各一行，在线状态变化后两行都更新", "[memberdepttree]")
{
    const std::vector<DeptTreeRow> rows = BuildGroupMemberDeptTree(SampleMembers(), SampleDepts());
    std::unique_ptr<GroupMemberList> list = BuildTreeList(rows);
    CHECK(list->SetMemberPresence(kLi, kPresenceOnline));
    int liRows = 0;
    for (size_t i = 0; i < list->Children().size(); ++i)
    {
        const GroupMemberItem* item = dynamic_cast<const GroupMemberItem*>(list->Children()[i].get());
        if (item != nullptr && item->UserId() == kLi)
        {
            ++liRows;
            CHECK(item->Presence() == kPresenceOnline);
        }
    }
    CHECK(liRows == 2);

    CHECK(list->SetMemberPresence(kLi, kPresenceOffline));
    for (size_t i = 0; i < list->Children().size(); ++i)
    {
        const GroupMemberItem* item = dynamic_cast<const GroupMemberItem*>(list->Children()[i].get());
        if (item != nullptr && item->UserId() == kLi)
        {
            CHECK(item->Presence() == kPresenceOffline);
        }
    }
    //侧栏里没有的人
    CHECK_FALSE(list->SetMemberPresence(kSun, kPresenceOnline));
}

TEST_CASE("MT6 [GroupDlg] MemberView 写入读回，没写过时为平铺", "[memberdepttree]")
{
    const std::wstring path = FreshIniPath(L"mt6");
    {
        CUserConfig fresh;
        REQUIRE(fresh.LoadConfig(path.c_str()));
        CHECK_FALSE(fresh.IsGroupMemberTreeView());
        fresh.SetGroupMemberTreeView(true);
        REQUIRE(fresh.SaveConfig(path.c_str()));
    }
    {
        CUserConfig loaded;
        REQUIRE(loaded.LoadConfig(path.c_str()));
        CHECK(loaded.IsGroupMemberTreeView());
        loaded.SetGroupMemberTreeView(false);
        REQUIRE(loaded.SaveConfig(path.c_str()));
    }
    {
        CUserConfig again;
        REQUIRE(again.LoadConfig(path.c_str()));
        CHECK_FALSE(again.IsGroupMemberTreeView());
    }
    //不认识的值按平铺
    ::WritePrivateProfileStringW(L"GroupDlg", L"MemberView", L"7", path.c_str());
    {
        CUserConfig odd;
        REQUIRE(odd.LoadConfig(path.c_str()));
        CHECK_FALSE(odd.IsGroupMemberTreeView());
    }
    ::DeleteFileW(path.c_str());
}
