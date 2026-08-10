/**
 *  ContactTests.cpp
 *
 *  联系人资料对话框的契约测试：
 *    C1-C8  BuildContactBody 产出的控件树符合"控件点名契约" —— 头像 /
 *           头部资料块 / 标签流式容器 / 备注名当前值 / 备注名文本框 /
 *           分组下拉 / 三个开关 / 两个底部按钮都能按既定数字 id 找到、
 *           类型正确、id 互不重复。
 *    C9-C15 ContactModel 数据模型：默认值、标签增删、空标签被拒、分组
 *           选择、三个开关读写、Switch / Group 越界被忽略。
 *
 *  用 Catch2 v3（amalgamated 形态）作测试框架。本工程不创建任何窗口，
 *  纯粹在内存里 build 控件树 / 操作模型 + 断言。
 *
 *  本文件不注册进 FlamingoUnit.vcxproj —— 由主 agent 集成时一并加入。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../BalloonContact/ContactLayout.h"
#include "../BalloonContact/ContactModel.h"
#include "../../balloonui/balloonui/Controls/Input/DuiEditHost.h"
#include "../../balloonui/balloonui/Controls/Input/DuiSwitch.h"
#include "../../balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiAvatar.h"

using namespace balloonwjui;
using namespace ballooncontact;

namespace {

// 递归收集子树里所有非 0 的 ctrlId。
void CollectContactIds(const DuiControl* node, std::vector<UINT>& out)
{
    if (node == nullptr)
    {
        return;
    }
    UINT id = node->GetCtrlId();
    if (id != 0)
    {
        out.push_back(id);
    }
    for (const auto& child : node->Children())
    {
        CollectContactIds(child.get(), out);
    }
}

// 判断一组 id 里是否存在重复值。
bool HasDuplicateContactId(std::vector<UINT> ids)
{
    std::sort(ids.begin(), ids.end());
    return std::adjacent_find(ids.begin(), ids.end()) != ids.end();
}

} // anonymous namespace

// =====================================================================
// C1-C8 联系人资料对话框布局契约
// =====================================================================

TEST_CASE("C1 BuildContactBody 返回非空控件树", "[contact]")
{
    auto root = BuildContactBody();
    REQUIRE(root != nullptr);
}

TEST_CASE("C2 联系人资料对话框含头像且类型正确", "[contact]")
{
    auto root = BuildContactBody();
    REQUIRE(root != nullptr);

    CHECK(dynamic_cast<DuiAvatar*>(root->FindCtrlById(kIdAvatar)) != nullptr);
}

TEST_CASE("C3 联系人资料对话框含头部资料块与标签流式容器", "[contact]")
{
    auto root = BuildContactBody();
    REQUIRE(root != nullptr);

    // 头部资料块与标签流式容器是文件局部类型，只能验"按 id 找得到"。
    CHECK(root->FindCtrlById(kIdHeaderInfo) != nullptr);
    CHECK(root->FindCtrlById(kIdTagFlow)    != nullptr);
}

TEST_CASE("C4 联系人资料对话框含备注名当前值与行内文本框", "[contact]")
{
    auto root = BuildContactBody();
    REQUIRE(root != nullptr);

    CHECK(root->FindCtrlById(kIdRemarkValue) != nullptr);
    CHECK(dynamic_cast<DuiEditHost*>(
              root->FindCtrlById(kIdRemarkEdit)) != nullptr);
}

TEST_CASE("C5 联系人资料对话框含分组下拉框", "[contact]")
{
    auto root = BuildContactBody();
    REQUIRE(root != nullptr);

    CHECK(dynamic_cast<DuiComboBox*>(
              root->FindCtrlById(kIdGroupCombo)) != nullptr);
}

TEST_CASE("C6 联系人资料对话框含两个 DuiSwitch 开关", "[contact]")
{
    auto root = BuildContactBody();
    REQUIRE(root != nullptr);

    CHECK(dynamic_cast<DuiSwitch*>(
              root->FindCtrlById(kIdSwitchMute)) != nullptr);
    CHECK(dynamic_cast<DuiSwitch*>(
              root->FindCtrlById(kIdSwitchPinned)) != nullptr);

    // 企业版取消好友关系后，「加入黑名单」那一行整行移除。用字面量 1032 断言
    // 而不是枚举 —— 枚举项已经删了，这里要防的是"日后有人复用这个编号"。
    CHECK(root->FindCtrlById(1032) == nullptr);
}

TEST_CASE("C7 联系人资料对话框含关闭 / 发消息底部按钮", "[contact]")
{
    auto root = BuildContactBody();
    REQUIRE(root != nullptr);

    CHECK(root->FindCtrlById(kIdBtnClose) != nullptr);
    CHECK(root->FindCtrlById(kIdBtnSend)  != nullptr);
}

TEST_CASE("C8 联系人资料对话框控件 id 互不重复", "[contact]")
{
    auto root = BuildContactBody();
    REQUIRE(root != nullptr);

    std::vector<UINT> ids;
    CollectContactIds(root.get(), ids);
    CHECK_FALSE(ids.empty());
    CHECK_FALSE(HasDuplicateContactId(ids));
}

// =====================================================================
// C9-C15 ContactModel 数据模型契约
// =====================================================================

TEST_CASE("C9 ContactModel 默认 mock 数据正确", "[contact][model]")
{
    ContactModel model;
    CHECK(model.Name()    == CString(_T("江屿白")));
    CHECK(model.Remark()  == CString(_T("设计组组长")));
    CHECK(model.Account() == CString(_T("10923847")));
    CHECK(model.Region()  == CString(_T("北京")));
    CHECK(model.Status()  == kContactStatusAway);
    // 头像占位字母 = 真名首字。
    CHECK(model.AvatarLetter() == CString(_T("江")));
}

TEST_CASE("C10 ContactModel 默认有两个标签「朋友」「设计」", "[contact][model]")
{
    ContactModel model;
    REQUIRE(model.TagCount() == 2);
    CHECK(model.Tag(0) == CString(_T("朋友")));
    CHECK(model.Tag(1) == CString(_T("设计")));
}

TEST_CASE("C11 ContactModel 标签可追加", "[contact][model]")
{
    ContactModel model;
    const int before = model.TagCount();

    CHECK(model.AddTag(_T("同学")) == true);
    REQUIRE(model.TagCount() == before + 1);
    CHECK(model.Tag(before) == CString(_T("同学")));
}

TEST_CASE("C12 ContactModel 空白标签被拒绝", "[contact][model]")
{
    ContactModel model;
    const int before = model.TagCount();

    CHECK(model.AddTag(_T("")) == false);       // 空字符串
    CHECK(model.AddTag(_T("   ")) == false);    // 纯空白
    CHECK(model.AddTag(NULL) == false);         // 空指针
    CHECK(model.TagCount() == before);          // 数量不变
}

TEST_CASE("C13 ContactModel 标签可按下标删除、越界忽略", "[contact][model]")
{
    ContactModel model;
    REQUIRE(model.TagCount() == 2);

    // 删除第 0 个「朋友」—— 后续标签整体前移。
    model.RemoveTag(0);
    REQUIRE(model.TagCount() == 1);
    CHECK(model.Tag(0) == CString(_T("设计")));

    // 越界下标删除应被忽略。
    model.RemoveTag(99);
    model.RemoveTag(-1);
    CHECK(model.TagCount() == 1);
}

TEST_CASE("C14 ContactModel 分组默认选中「产品部 · 设计组」、可切换", "[contact][model]")
{
    ContactModel model;
    REQUIRE(model.GroupCount() >= 2);

    // 默认选中下标 1，对应「产品部 · 设计组」。
    CHECK(model.SelectedGroup() == 1);
    CHECK(model.Group(1) == CString(_T("产品部 · 设计组")));

    // 切换到下标 0。
    model.SetSelectedGroup(0);
    CHECK(model.SelectedGroup() == 0);

    // 越界忽略。
    model.SetSelectedGroup(999);
    model.SetSelectedGroup(-1);
    CHECK(model.SelectedGroup() == 0);
}

TEST_CASE("C15 ContactModel 两个开关默认全关、可读写、越界忽略", "[contact][model]")
{
    ContactModel model;

    // 默认全关。
    CHECK(model.Switch(kContactSwitchMute)    == false);
    CHECK(model.Switch(kContactSwitchPinned)  == false);

    // 分别打开，互不串扰。
    model.SetSwitch(kContactSwitchPinned, true);
    CHECK(model.Switch(kContactSwitchMute)    == false);
    CHECK(model.Switch(kContactSwitchPinned)  == true);

    // 越界 kind：写忽略、读返回 false。开关只剩两个，下标 2 已经越界 ——
    // 这一条同时锁住"删掉黑名单开关后 kContactSwitchCount 确实降到了 2"。
    model.SetSwitch(2, true);
    CHECK(model.Switch(2) == false);
    model.SetSwitch(99, true);
    CHECK(model.Switch(99) == false);
}
