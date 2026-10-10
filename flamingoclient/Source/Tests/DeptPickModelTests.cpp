/**
 *  DeptPickModelTests.cpp
 *
 *  「选择部门」小窗部门树的纯逻辑（UI/BalloonMain/DeptPickModel.h，标签 [deptpick]，docs/机器人方案.md §14.8）。覆盖：
 *    DPK1 建树：层级、缩进、父节点下标、同级按 m_deptorder 排序（权重相同时按列表顺序），父部门不在列表里的当作根部门；
 *    DPK2 搜索：大小写不敏感的子串匹配，保留命中部门及其全部上级，上级标为「未命中」，没命中的兄弟部门不出现；没有命中时为空；
 *    DPK3 部门数据成环（父子互指）不死循环，环上的部门各出现一次；
 *    DPK4 整理选择结果：去掉重复的、已删除的、上级也被选中的部门，保持先后顺序；环上的部门不因互为上级而全部丢失；
 *    DPK5 按部门ID取名称；
 *    DPL1 小窗（UI/BalloonMain/DeptPickLayout.h）按设计尺寸排得下：底部「确定」没有被压扁、下方留白与空间充裕时相同；
 *    DPL2 状态切换：空树时显示提示、「确定」禁用；有行与勾选时树可见、计数更新、「确定」可用；
 *         先隐藏再显示（搜索没有命中 → 清空搜索）之后，滚动视口重新排了版（开发约定文档 UI 约定第 5 条）。
 *
 *  用 Catch2 v3，纯内存断言，不创建窗口。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../UI/BalloonMain/DeptPickModel.h"
#include "../UI/BalloonMain/DeptPickLayout.h"
#include "LayoutFitCheck.h"   // layoutfit::MeasureAt：按设计尺寸排得下的检查
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"

using namespace balloonmain;

namespace {

//造一个部门；name 用 UTF-8 字面量（本工程按 /utf-8 编译）
net::DepartmentItem MakeDept(int64_t deptId, const char* name, int64_t parentId, int order = 0)
{
    net::DepartmentItem d;
    d.m_deptid = deptId;
    d.m_name = (name != NULL) ? name : "";
    d.m_parentdeptid = parentId;
    d.m_deptorder = order;
    return d;
}

//一家公司：公司（1）下有研发中心（10，排第 2）与市场部（20，排第 1）；研发中心下有前端组（11）与后端组（12，权重相同、按列表顺序）
std::vector<net::DepartmentItem> Company()
{
    std::vector<net::DepartmentItem> d;
    d.push_back(MakeDept(1, u8"明远科技", 0));
    d.push_back(MakeDept(10, u8"研发中心", 1, 2));
    d.push_back(MakeDept(11, u8"前端组", 10, 1));
    d.push_back(MakeDept(12, u8"后端组", 10, 1));
    d.push_back(MakeDept(20, u8"市场部", 1, 1));
    return d;
}

} // anonymous namespace

TEST_CASE("DPK1 建树：层级、父节点下标与同级排序", "[deptpick]")
{
    std::vector<net::DepartmentItem> d = Company();
    //父部门不在列表里的部门当作根部门，排在公司之后
    d.push_back(MakeDept(30, u8"外部顾问", 999, 5));
    const std::vector<DeptPickNode> nodes = BuildDeptPickTree(d, std::wstring());
    REQUIRE(nodes.size() == 6);
    CHECK(nodes[0].m_deptId == 1);
    CHECK(nodes[0].m_depth == 0);
    CHECK(nodes[0].m_parent == -1);
    CHECK(nodes[0].m_name == L"明远科技");
    //市场部权重 1 排在研发中心（权重 2）之前
    CHECK(nodes[1].m_deptId == 20);
    CHECK(nodes[1].m_depth == 1);
    CHECK(nodes[1].m_parent == 0);
    CHECK(nodes[2].m_deptId == 10);
    //前端组、后端组权重相同：按列表顺序
    CHECK(nodes[3].m_deptId == 11);
    CHECK(nodes[3].m_depth == 2);
    CHECK(nodes[3].m_parent == 2);
    CHECK(nodes[4].m_deptId == 12);
    CHECK(nodes[5].m_deptId == 30);
    CHECK(nodes[5].m_depth == 0);
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        CHECK(nodes[i].m_matched);
    }
    CHECK(BuildDeptPickTree(std::vector<net::DepartmentItem>(), std::wstring()).empty());
}

TEST_CASE("DPK2 搜索：保留命中部门与上级，大小写不敏感", "[deptpick]")
{
    std::vector<net::DepartmentItem> d = Company();
    d.push_back(MakeDept(13, u8"QA 测试组", 10, 3));
    const std::vector<DeptPickNode> nodes = BuildDeptPickTree(d, L"后端");
    REQUIRE(nodes.size() == 3);
    CHECK(nodes[0].m_deptId == 1);
    CHECK_FALSE(nodes[0].m_matched);
    CHECK(nodes[1].m_deptId == 10);
    CHECK_FALSE(nodes[1].m_matched);
    CHECK(nodes[2].m_deptId == 12);
    CHECK(nodes[2].m_matched);
    CHECK(nodes[2].m_parent == 1);

    //大小写不敏感
    const std::vector<DeptPickNode> qa = BuildDeptPickTree(d, L"qa");
    REQUIRE(qa.size() == 3);
    CHECK(qa[2].m_deptId == 13);
    //命中上级部门时只列上级本身及其上级，不展开下级
    const std::vector<DeptPickNode> rd = BuildDeptPickTree(d, L"研发");
    REQUIRE(rd.size() == 2);
    CHECK(rd[1].m_deptId == 10);
    CHECK(rd[1].m_matched);
    //没有命中
    CHECK(BuildDeptPickTree(d, L"财务").empty());
}

TEST_CASE("DPK3 部门数据成环不死循环", "[deptpick]")
{
    std::vector<net::DepartmentItem> d;
    d.push_back(MakeDept(1, u8"公司", 0));
    d.push_back(MakeDept(2, u8"甲", 3));
    d.push_back(MakeDept(3, u8"乙", 2));
    const std::vector<DeptPickNode> nodes = BuildDeptPickTree(d, std::wstring());
    REQUIRE(nodes.size() == 3);
    int seen2 = 0;
    int seen3 = 0;
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        seen2 += (nodes[i].m_deptId == 2) ? 1 : 0;
        seen3 += (nodes[i].m_deptId == 3) ? 1 : 0;
    }
    CHECK(seen2 == 1);
    CHECK(seen3 == 1);
    //搜索环上的部门同样不死循环
    CHECK(BuildDeptPickTree(d, L"乙").size() >= 1);
}

TEST_CASE("DPK4 整理选择结果：去掉重复、已删除与被上级包含的部门", "[deptpick]")
{
    const std::vector<net::DepartmentItem> d = Company();
    std::vector<int64_t> picked;
    picked.push_back(11);    //前端组：上级研发中心也选了，去掉
    picked.push_back(20);    //市场部
    picked.push_back(10);    //研发中心
    picked.push_back(20);    //重复
    picked.push_back(777);   //已删除的部门
    const std::vector<int64_t> out = NormalizeDeptSelection(picked, d);
    REQUIRE(out.size() == 2);
    CHECK(out[0] == 20);
    CHECK(out[1] == 10);

    //选了公司：其余全部被包含
    picked.push_back(1);
    const std::vector<int64_t> all = NormalizeDeptSelection(picked, d);
    REQUIRE(all.size() == 1);
    CHECK(all[0] == 1);

    //环上的部门互为上级：不因此全部丢失
    std::vector<net::DepartmentItem> cyc;
    cyc.push_back(MakeDept(2, u8"甲", 3));
    cyc.push_back(MakeDept(3, u8"乙", 2));
    std::vector<int64_t> both;
    both.push_back(2);
    both.push_back(3);
    CHECK(NormalizeDeptSelection(both, cyc).size() == 2);
}

TEST_CASE("DPL1 选择部门小窗按设计尺寸排得下", "[deptpick]")
{
    std::unique_ptr<balloonwjui::DuiControl> root = BuildDeptPickBody();
    REQUIRE(root);
    ApplyDeptPickState(root.get(), 5, 1);
    const int contentH = kDeptPickWindowHeight - kDeptPickTitleBarH;
    const UINT ids[] = { kIdDeptPickOk, kIdDeptPickCancel, kIdDeptPickBottom };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        const layoutfit::FitResult r = layoutfit::MeasureAt(root.get(), ids[i], kDeptPickWindowWidth, contentH);
        REQUIRE(r.m_found);
        CHECK(r.m_bottom <= contentH);
        CHECK(r.m_height == r.m_roomyHeight);
        CHECK(r.m_bottomGap == r.m_roomyBottomGap);
    }
    //中间的树区有足够高度（至少放得下 5 行）
    const RECT rcScroll = root->FindCtrlById(kIdDeptPickScroll)->GetRect();
    CHECK(rcScroll.bottom - rcScroll.top >= 5 * kDeptPickRowHeight);
}

TEST_CASE("DPL2 空树、有行、先隐藏再显示", "[deptpick]")
{
    std::unique_ptr<balloonwjui::DuiControl> root = BuildDeptPickBody();
    REQUIRE(root);
    const RECT rc = { 0, 0, kDeptPickWindowWidth, kDeptPickWindowHeight - kDeptPickTitleBarH };
    root->ForceLayout(rc);

    //建出来是空的：提示可见、树隐藏、「确定」禁用
    balloonwjui::DuiControl* scroll = root->FindCtrlById(kIdDeptPickScroll);
    balloonwjui::DuiControl* empty = root->FindCtrlById(kIdDeptPickEmpty);
    balloonwjui::DuiControl* ok = root->FindCtrlById(kIdDeptPickOk);
    REQUIRE(scroll != NULL);
    REQUIRE(empty != NULL);
    REQUIRE(ok != NULL);
    CHECK_FALSE(scroll->IsVisible());
    CHECK(empty->IsVisible());
    CHECK_FALSE(ok->IsEnabled());

    //有行、勾了两个：树可见且排了版，计数更新，「确定」可用
    ApplyDeptPickState(root.get(), 4, 2);
    CHECK(scroll->IsVisible());
    CHECK_FALSE(empty->IsVisible());
    CHECK(ok->IsEnabled());
    const RECT shown = scroll->GetRect();
    CHECK(shown.bottom - shown.top > 0);
    balloonwjui::DuiLabel* count = dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(kIdDeptPickCount));
    REQUIRE(count != NULL);
    CHECK(std::wstring(count->GetText()).find(L"2") != std::wstring::npos);

    //搜索没有命中：树隐藏、提示可见；勾选还在，「确定」仍可用
    ApplyDeptPickState(root.get(), 0, 2);
    CHECK_FALSE(scroll->IsVisible());
    CHECK(empty->IsVisible());
    CHECK(ok->IsEnabled());
    //清空搜索后再显示：滚动视口的矩形与第一次显示时相同
    ApplyDeptPickState(root.get(), 4, 0);
    CHECK(scroll->IsVisible());
    const RECT again = scroll->GetRect();
    CHECK(again.top == shown.top);
    CHECK(again.bottom == shown.bottom);
    CHECK_FALSE(ok->IsEnabled());
}

TEST_CASE("DPK5 按部门ID取名称", "[deptpick]")
{
    const std::vector<net::DepartmentItem> d = Company();
    CHECK(DeptNameOf(d, 20) == L"市场部");
    CHECK(DeptNameOf(d, 777).empty());
}
