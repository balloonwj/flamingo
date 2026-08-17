/**
 *  BalloonGroupInfoLayoutTests.cpp
 *
 *  GI 系列测试:验证 BalloonGroupInfoLayout 的种子函数 BuildGroupInfoBody()
 *  产出的 balloonui 控件树符合"控件点名契约",ApplyGroupInfoDataToView 正确
 *  注入数据,SetGroupInfoCardEditMode 正确翻转群名片双态,以及
 *  ResolveGroupOwnerName / ResolveMyGroupCard 的群名片回退选择逻辑。
 *
 *  设计稿:design/群资料.png。
 *
 *  覆盖用例:
 *    GI1  BuildGroupInfoBody 返回非空
 *    GI2  关键 ctrlId 存在且类型正确(头像/群名/群号人数/群主/公告/名片 view+edit/
 *         两开关/两按钮)
 *    GI3  初始可见性:名片 view 可见、edit 隐藏
 *    GI4  ApplyGroupInfoDataToView 写入群名 / "群号 N · M 人" / 群主 / 公告 / 名片;
 *         公告空→占位"暂无群公告";名片空→占位"未设置"
 *    GI5  两开关按 mute / pinned 置位
 *    GI6  SetGroupInfoCardEditMode 翻转名片 view/edit 可见性
 *    GI7  ResolveGroupOwnerName / ResolveMyGroupCard:群名片优先、空回退账号昵称、
 *         未命中返回空串
 *    GI8  全部 ctrlId 互不重复
 *
 *  用 Catch2 v3(amalgamated)。全局 _Module 由 BalloonChatLayoutTests.cpp
 *  统一定义,本文件只引用。
 *
 *  balloonwj@qq.com   2026-06-08
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../BalloonMain/BalloonGroupInfoLayout.h"
#include "../net/GroupProtocolClient.h"          // net::GroupMemberItem
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSwitch.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"

using namespace balloonwjui;
using namespace balloonmain;

namespace {

// 递归收集子树里所有非 0 的 ctrlId。
void CollectIdsGroupInfo(const DuiControl* node, std::vector<UINT>& out)
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
        CollectIdsGroupInfo(child.get(), out);
    }
}

bool HasDuplicateGroupInfoId(std::vector<UINT> ids)
{
    std::sort(ids.begin(), ids.end());
    return std::adjacent_find(ids.begin(), ids.end()) != ids.end();
}

// 一份典型展示数据(对照 design/群资料.png)。
GroupInfoViewData MakeProjectGroupData()
{
    GroupInfoViewData d;
    d.groupid     = 700001;
    d.groupName   = _T("FlamingoIM 项目组");
    d.memberCount = 28;
    d.ownerName   = _T("谢知行");
    d.announcement= _T("v3.2 版本即将发布，全员准备。");
    d.myCard      = _T("林知夏");
    d.mute        = false;
    d.pinned      = true;
    return d;
}

} // anonymous namespace

// =====================================================================
// GI. 控件契约
// =====================================================================

TEST_CASE("GI1 BuildGroupInfoBody 返回非空控件树", "[groupinfo]")
{
    std::unique_ptr<DuiControl> root = BuildGroupInfoBody();
    REQUIRE(root != nullptr);
}

TEST_CASE("GI2 关键 ctrlId 存在且类型正确", "[groupinfo]")
{
    std::unique_ptr<DuiControl> root = BuildGroupInfoBody();
    REQUIRE(root != nullptr);

    CHECK(dynamic_cast<DuiAvatar*>(root->FindCtrlById(kIdGroupAvatar)) != nullptr);
    CHECK(dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdGroupName)) != nullptr);
    CHECK(dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdGroupMeta)) != nullptr);
    CHECK(dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdGroupOwner)) != nullptr);
    CHECK(dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdGroupAnnouncement)) != nullptr);
    CHECK(dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdGroupCardLabel)) != nullptr);
    CHECK(dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdGroupCardView)) != nullptr);
    CHECK(dynamic_cast<DuiEdit*>(root->FindCtrlById(kIdGroupCardEdit)) != nullptr);
    CHECK(dynamic_cast<DuiSwitch*>(root->FindCtrlById(kIdGroupMuteSwitch)) != nullptr);
    CHECK(dynamic_cast<DuiSwitch*>(root->FindCtrlById(kIdGroupPinSwitch)) != nullptr);
    CHECK(dynamic_cast<DuiButton*>(root->FindCtrlById(kIdGroupBtnHistory)) != nullptr);
    CHECK(dynamic_cast<DuiButton*>(root->FindCtrlById(kIdGroupBtnQuit)) != nullptr);
}

TEST_CASE("GI3 初始可见性:名片 view 可见、edit 隐藏", "[groupinfo]")
{
    std::unique_ptr<DuiControl> root = BuildGroupInfoBody();
    REQUIRE(root != nullptr);

    DuiControl* view = root->FindCtrlById(kIdGroupCardView);
    DuiControl* edit = root->FindCtrlById(kIdGroupCardEdit);
    REQUIRE(view != nullptr);
    REQUIRE(edit != nullptr);
    CHECK(view->IsVisible() == true);
    CHECK(edit->IsVisible() == false);
}

// =====================================================================
// GI4 / GI5. ApplyGroupInfoDataToView
// =====================================================================

TEST_CASE("GI4 ApplyGroupInfoDataToView 写入群名 / 群号人数 / 群主 / 公告 / 名片",
          "[groupinfo][apply]")
{
    std::unique_ptr<DuiControl> root = BuildGroupInfoBody();
    REQUIRE(root != nullptr);

    GroupInfoViewData d = MakeProjectGroupData();
    REQUIRE(ApplyGroupInfoDataToView(root.get(), d) == true);

    DuiLabel* name  = dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdGroupName));
    DuiLabel* meta  = dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdGroupMeta));
    DuiLabel* owner = dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdGroupOwner));
    DuiLabel* notice= dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdGroupAnnouncement));
    DuiLabel* card  = dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdGroupCardView));
    REQUIRE(name  != nullptr);
    REQUIRE(meta  != nullptr);
    REQUIRE(owner != nullptr);
    REQUIRE(notice!= nullptr);
    REQUIRE(card  != nullptr);

    CHECK(name->GetText()   == CString(_T("FlamingoIM 项目组")));
    CHECK(meta->GetText()   == CString(_T("群号 700001 · 28 人")));
    CHECK(owner->GetText()  == CString(_T("群主：谢知行")));
    CHECK(notice->GetText() == CString(_T("v3.2 版本即将发布，全员准备。")));
    CHECK(card->GetText()   == CString(_T("林知夏")));

    // 名片 edit 初值同步为真实名片。
    DuiEdit* cardEdit = dynamic_cast<DuiEdit*>(root->FindCtrlById(kIdGroupCardEdit));
    REQUIRE(cardEdit != nullptr);
    CHECK(cardEdit->GetText() == CString(_T("林知夏")));
}

TEST_CASE("GI4b 公告 / 名片为空时显占位", "[groupinfo][apply]")
{
    std::unique_ptr<DuiControl> root = BuildGroupInfoBody();
    REQUIRE(root != nullptr);

    GroupInfoViewData d = MakeProjectGroupData();
    d.announcement = _T("");
    d.myCard       = _T("");
    REQUIRE(ApplyGroupInfoDataToView(root.get(), d) == true);

    DuiLabel* notice = dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdGroupAnnouncement));
    DuiLabel* card   = dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdGroupCardView));
    REQUIRE(notice != nullptr);
    REQUIRE(card   != nullptr);
    CHECK(notice->GetText() == CString(_T("暂无群公告")));
    CHECK(card->GetText()   == CString(_T("未设置")));
}

TEST_CASE("GI5 两开关按 mute / pinned 置位", "[groupinfo][apply]")
{
    std::unique_ptr<DuiControl> root = BuildGroupInfoBody();
    REQUIRE(root != nullptr);

    GroupInfoViewData d = MakeProjectGroupData();
    d.mute   = true;
    d.pinned = false;
    REQUIRE(ApplyGroupInfoDataToView(root.get(), d) == true);

    DuiSwitch* mute = dynamic_cast<DuiSwitch*>(root->FindCtrlById(kIdGroupMuteSwitch));
    DuiSwitch* pin  = dynamic_cast<DuiSwitch*>(root->FindCtrlById(kIdGroupPinSwitch));
    REQUIRE(mute != nullptr);
    REQUIRE(pin  != nullptr);
    CHECK(mute->IsChecked() == true);
    CHECK(pin->IsChecked()  == false);
}

// =====================================================================
// GI6. SetGroupInfoCardEditMode
// =====================================================================

TEST_CASE("GI6 SetGroupInfoCardEditMode 翻转名片 view/edit 可见性", "[groupinfo]")
{
    std::unique_ptr<DuiControl> root = BuildGroupInfoBody();
    REQUIRE(root != nullptr);

    DuiControl* view = root->FindCtrlById(kIdGroupCardView);
    DuiControl* edit = root->FindCtrlById(kIdGroupCardEdit);
    REQUIRE(view != nullptr);
    REQUIRE(edit != nullptr);

    // 切到编辑态:view 隐、edit 显。
    REQUIRE(SetGroupInfoCardEditMode(root.get(), true) == true);
    CHECK(view->IsVisible() == false);
    CHECK(edit->IsVisible() == true);

    // 切回查看态:view 显、edit 隐。
    REQUIRE(SetGroupInfoCardEditMode(root.get(), false) == true);
    CHECK(view->IsVisible() == true);
    CHECK(edit->IsVisible() == false);
}

// =====================================================================
// GI7. ResolveGroupOwnerName / ResolveMyGroupCard
// =====================================================================

TEST_CASE("GI7 群主显示名 / 我的群名片:名片优先、空回退昵称、未命中空串", "[groupinfo]")
{
    std::vector<net::GroupMemberItem> members;

    net::GroupMemberItem owner;       // 群主:有群名片
    owner.m_userid   = 12345;
    owner.m_role     = 2;
    owner.m_card     = "\xE8\xB0\xA2\xE7\x9F\xA5\xE8\xA1\x8C";   // "谢知行"
    owner.m_nickname = "xiezhixing";
    members.push_back(owner);

    net::GroupMemberItem me;          // 我:无群名片,回退账号昵称
    me.m_userid   = 5308;
    me.m_role     = 0;
    me.m_card     = "";
    me.m_nickname = "\xE6\x9E\x97\xE7\x9F\xA5\xE5\xA4\x8F";      // "林知夏"
    members.push_back(me);

    // 群主有名片 → 取名片。
    CHECK(ResolveGroupOwnerName(members, 12345) == "\xE8\xB0\xA2\xE7\x9F\xA5\xE8\xA1\x8C");
    // 我无名片 → 回退账号昵称。
    CHECK(ResolveMyGroupCard(members, 5308) == "\xE6\x9E\x97\xE7\x9F\xA5\xE5\xA4\x8F");
    // 未命中 → 空串。
    CHECK(ResolveGroupOwnerName(members, 99999).empty());
    CHECK(ResolveMyGroupCard(members, 99999).empty());
}

// =====================================================================
// GI8. ctrlId 唯一性
// =====================================================================

TEST_CASE("GI8 全部 ctrlId 互不重复", "[groupinfo][ids]")
{
    std::unique_ptr<DuiControl> root = BuildGroupInfoBody();
    REQUIRE(root != nullptr);

    std::vector<UINT> ids;
    CollectIdsGroupInfo(root.get(), ids);
    CHECK_FALSE(ids.empty());
    CHECK_FALSE(HasDuplicateGroupInfoId(ids));
}
