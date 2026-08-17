/**
 *  ProfileTests.cpp
 *
 *  个人信息对话框的契约测试：
 *    P1-P8   BuildProfileBody 产出的控件树符合"控件点名契约" —— 头像 /
 *            四个状态胶囊 / 查看态六个值文本 / 编辑态五个文本框 + 一个
 *            下拉框 / 底部按钮都能按既定数字 id 找到、类型正确、id 互不
 *            重复；查看态与编辑态各建一棵树。
 *    P9-P14  ProfileModel 数据模型：默认值、各字段读写、状态越界忽略、
 *            "编辑提交"语义（setter 改值后 getter 可见）。
 *
 *  用 Catch2 v3（amalgamated 形态）作测试框架。本工程不创建任何窗口，
 *  纯粹在内存里 build 控件树 / 操作模型 + 断言。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../BalloonProfile/ProfileLayout.h"
#include "../BalloonProfile/ProfileModel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"

using namespace balloonwjui;
using namespace balloonprofile;

namespace {

// 递归收集子树里所有非 0 的 ctrlId。
void CollectProfileIds(const DuiControl* node, std::vector<UINT>& out)
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
        CollectProfileIds(child.get(), out);
    }
}

// 判断一组 id 里是否存在重复值。
bool HasDuplicateProfileId(std::vector<UINT> ids)
{
    std::sort(ids.begin(), ids.end());
    return std::adjacent_find(ids.begin(), ids.end()) != ids.end();
}

} // anonymous namespace

// =====================================================================
// P1-P8 个人信息对话框布局契约
// =====================================================================

TEST_CASE("P1 BuildProfileBody 两态都返回非空控件树", "[profile]")
{
    auto viewRoot = BuildProfileBody(false);
    auto editRoot = BuildProfileBody(true);
    REQUIRE(viewRoot != nullptr);
    REQUIRE(editRoot != nullptr);
}

TEST_CASE("P2 查看态 / 编辑态都含头像控件", "[profile]")
{
    auto viewRoot = BuildProfileBody(false);
    auto editRoot = BuildProfileBody(true);
    REQUIRE(viewRoot != nullptr);
    REQUIRE(editRoot != nullptr);

    CHECK(viewRoot->FindCtrlById(kIdAvatar) != nullptr);
    CHECK(editRoot->FindCtrlById(kIdAvatar) != nullptr);
}

TEST_CASE("P3 查看态 / 编辑态都含四个状态胶囊", "[profile]")
{
    auto viewRoot = BuildProfileBody(false);
    REQUIRE(viewRoot != nullptr);

    CHECK(viewRoot->FindCtrlById(kIdPillOnline)    != nullptr);
    CHECK(viewRoot->FindCtrlById(kIdPillAway)      != nullptr);
    CHECK(viewRoot->FindCtrlById(kIdPillBusy)      != nullptr);
    // 隐身状态已去掉，编号 913 的胶囊不应再存在（防止有人把它加回来）
    CHECK(viewRoot->FindCtrlById(913)              == nullptr);

    // 胶囊代表的状态值应与 id 顺序一致。
    CHECK(GetProfilePillStatus(viewRoot.get(), kIdPillOnline)
          == kProfileStatusOnline);
    CHECK(GetProfilePillStatus(viewRoot.get(), kIdPillAway)
          == kProfileStatusAway);
    CHECK(GetProfilePillStatus(viewRoot.get(), kIdPillBusy)
          == kProfileStatusBusy);
}

TEST_CASE("P4 查看态含六个只读值文本控件", "[profile]")
{
    auto viewRoot = BuildProfileBody(false);
    REQUIRE(viewRoot != nullptr);

    CHECK(viewRoot->FindCtrlById(kIdViewNickname)  != nullptr);
    CHECK(viewRoot->FindCtrlById(kIdViewSignature) != nullptr);
    CHECK(viewRoot->FindCtrlById(kIdViewGender)    != nullptr);
    CHECK(viewRoot->FindCtrlById(kIdViewBirthday)  != nullptr);
    CHECK(viewRoot->FindCtrlById(kIdViewRegion)    != nullptr);
    CHECK(viewRoot->FindCtrlById(kIdViewEmail)     != nullptr);
}

TEST_CASE("P5 编辑态含五个文本框且类型为 DuiEdit", "[profile]")
{
    auto editRoot = BuildProfileBody(true);
    REQUIRE(editRoot != nullptr);

    CHECK(dynamic_cast<DuiEdit*>(
        editRoot->FindCtrlById(kIdEditNickname))  != nullptr);
    CHECK(dynamic_cast<DuiEdit*>(
        editRoot->FindCtrlById(kIdEditSignature)) != nullptr);
    CHECK(dynamic_cast<DuiEdit*>(
        editRoot->FindCtrlById(kIdEditBirthday))  != nullptr);
    CHECK(dynamic_cast<DuiEdit*>(
        editRoot->FindCtrlById(kIdEditRegion))    != nullptr);
    CHECK(dynamic_cast<DuiEdit*>(
        editRoot->FindCtrlById(kIdEditEmail))     != nullptr);
}

TEST_CASE("P6 编辑态性别为下拉框且含女 / 男 / 保密三项", "[profile]")
{
    auto editRoot = BuildProfileBody(true);
    REQUIRE(editRoot != nullptr);

    DuiComboBox* gender = dynamic_cast<DuiComboBox*>(
        editRoot->FindCtrlById(kIdEditGender));
    REQUIRE(gender != nullptr);
    CHECK(gender->GetCount() == 3);
    CHECK(gender->GetItemText(0) == CString(_T("女")));
    CHECK(gender->GetItemText(1) == CString(_T("男")));
    CHECK(gender->GetItemText(2) == CString(_T("保密")));
}

TEST_CASE("P7 两态底部按钮按模式各自存在", "[profile]")
{
    auto viewRoot = BuildProfileBody(false);
    auto editRoot = BuildProfileBody(true);
    REQUIRE(viewRoot != nullptr);
    REQUIRE(editRoot != nullptr);

    // 查看态：关闭 + 编辑资料。
    CHECK(viewRoot->FindCtrlById(kIdBtnClose) != nullptr);
    CHECK(viewRoot->FindCtrlById(kIdBtnEdit)  != nullptr);
    // 编辑态：取消 + 保存。
    CHECK(editRoot->FindCtrlById(kIdBtnCancel) != nullptr);
    CHECK(editRoot->FindCtrlById(kIdBtnSave)   != nullptr);
}

TEST_CASE("P8 两态控件 id 各自互不重复", "[profile]")
{
    auto viewRoot = BuildProfileBody(false);
    auto editRoot = BuildProfileBody(true);
    REQUIRE(viewRoot != nullptr);
    REQUIRE(editRoot != nullptr);

    std::vector<UINT> viewIds;
    CollectProfileIds(viewRoot.get(), viewIds);
    CHECK_FALSE(viewIds.empty());
    CHECK_FALSE(HasDuplicateProfileId(viewIds));

    std::vector<UINT> editIds;
    CollectProfileIds(editRoot.get(), editIds);
    CHECK_FALSE(editIds.empty());
    CHECK_FALSE(HasDuplicateProfileId(editIds));
}

// =====================================================================
// P9-P14 ProfileModel 数据模型契约
// =====================================================================

TEST_CASE("P9 ProfileModel 默认值为林知夏那组 mock 数据", "[profile][model]")
{
    ProfileModel model;
    CHECK(model.Name()      == CString(_T("林知夏")));
    CHECK(model.Account()   == CString(_T("88234561")));
    CHECK(model.Nickname()  == CString(_T("林知夏")));
    CHECK(model.Signature() == CString(_T("想做一件让自己骄傲的事")));
    CHECK(model.Gender()    == CString(_T("女")));
    CHECK(model.Birthday()  == CString(_T("1996-08-12")));
    CHECK(model.Region()    == CString(_T("上海")));
    CHECK(model.Email()     == CString(_T("lin.zhixia@flamingo.im")));
}

TEST_CASE("P10 ProfileModel 默认状态为在线", "[profile][model]")
{
    ProfileModel model;
    CHECK(model.Status() == kProfileStatusOnline);
}

TEST_CASE("P11 ProfileModel 可编辑字段写入后读回一致", "[profile][model]")
{
    ProfileModel model;

    model.SetNickname(_T("阿夏"));
    model.SetSignature(_T("继续努力"));
    model.SetGender(_T("保密"));
    model.SetBirthday(_T("2000-01-01"));
    model.SetRegion(_T("杭州"));
    model.SetEmail(_T("xia@flamingo.im"));

    CHECK(model.Nickname()  == CString(_T("阿夏")));
    CHECK(model.Signature() == CString(_T("继续努力")));
    CHECK(model.Gender()    == CString(_T("保密")));
    CHECK(model.Birthday()  == CString(_T("2000-01-01")));
    CHECK(model.Region()    == CString(_T("杭州")));
    CHECK(model.Email()     == CString(_T("xia@flamingo.im")));

    // 只读字段不应被上述写入影响。
    CHECK(model.Name()    == CString(_T("林知夏")));
    CHECK(model.Account() == CString(_T("88234561")));
}

TEST_CASE("P12 ProfileModel.SetStatus 合法值生效", "[profile][model]")
{
    ProfileModel model;

    model.SetStatus(kProfileStatusBusy);
    CHECK(model.Status() == kProfileStatusBusy);

    model.SetStatus(kProfileStatusAway);
    CHECK(model.Status() == kProfileStatusAway);
}

TEST_CASE("P13 ProfileModel.SetStatus 越界被忽略", "[profile][model]")
{
    ProfileModel model;

    model.SetStatus(kProfileStatusAway);
    CHECK(model.Status() == kProfileStatusAway);

    model.SetStatus(99);    // 越界 —— 应被忽略
    CHECK(model.Status() == kProfileStatusAway);

    model.SetStatus(-1);    // 越界 —— 应被忽略
    CHECK(model.Status() == kProfileStatusAway);
}

TEST_CASE("P14 ProfileStatusText / ProfileStatusColor 边界正确", "[profile][model]")
{
    // 四个合法状态的文字。
    CHECK(CString(ProfileStatusText(kProfileStatusOnline))
          == CString(_T("在线")));
    CHECK(CString(ProfileStatusText(kProfileStatusAway))
          == CString(_T("离开")));
    CHECK(CString(ProfileStatusText(kProfileStatusBusy))
          == CString(_T("忙碌")));

    // 越界回退到"在线"。
    CHECK(CString(ProfileStatusText(999)) == CString(_T("在线")));
    CHECK(ProfileStatusColor(999) == ProfileStatusColor(kProfileStatusOnline));

    // 在线状态点为绿色（设计稿 #22C55E）。
    CHECK(ProfileStatusColor(kProfileStatusOnline) == RGB(0x22, 0xC5, 0x5E));
}
