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
 *         提醒模式下拉框/置顶开关/两按钮)
 *    GI3  初始可见性:名片 view 可见、edit 隐藏
 *    GI4  ApplyGroupInfoDataToView 写入群名 / "群号 N · M 人" / 群主 / 公告 / 名片;
 *         公告空→占位"暂无群公告";名片空→占位"未设置"
 *    GI5  提醒模式下拉框与置顶开关按 notifyMode / pinned 置位
 *    GI5b 提醒模式下拉框的五个选项与右键二级菜单一致
 *    GI6  SetGroupInfoCardEditMode 翻转名片 view/edit 可见性
 *    GI7  ResolveGroupOwnerName / ResolveMyGroupCard:群名片优先、空回退账号昵称、
 *         未命中返回空串
 *    GI8  全部 ctrlId 互不重复
 *    GIC1 群名、群号那一行可点，点击复制的文字
 *    GIC2 「查看消息记录」打开本群的消息记录
 *    GIC3 复制提示条落在头部资料区下方，不遮住群名、群号
 *    GIX1 群主的底部多出「解散群聊」按钮，三个按钮等分一行、互不重叠；其余角色只有两个按钮
 *    GIX2 先按普通成员排版、再换成群主数据（"隐藏后再显示"的路径），解散按钮的位置与尺寸仍然正确
 *    GI9  群简介：有内容时显示原文（换行换成空格），为空时显示占位「暂无群简介」（2026-10-10 补上群简介）
 *    GI10 加了群简介之后按设计尺寸与最小尺寸都排得下
 *
 *  用 Catch2 v3(amalgamated)。全局 _Module 由 BalloonChatLayoutTests.cpp
 *  统一定义,本文件只引用。
 *
 *  balloonwj@qq.com   2026-06-08
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonMain/BalloonGroupInfoLayout.h"
#include "LayoutFitCheck.h"                       // layoutfit::MeasureAt：按设计尺寸排得下（GI10）
#include "../UI/BalloonMain/NotifyModeMenu.h"   // GetNotifyModeChoices / GetNotifyModeLabel：下拉框选项
#include "../net/Protocol.h"                    // enum conversation_notify_mode
#include "../net/GroupProtocolClient.h"          // net::GroupMemberItem
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiToast.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSwitch.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/DuiResMgr.h"
#include "../../../third-party/balloonui/balloonui/DuiDpi.h"

#include <cstdlib>     // std::abs —— GIX 组比较按钮宽度

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
    d.notifyMode  = notify_mode_allow;
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
    CHECK(dynamic_cast<DuiComboBox*>(root->FindCtrlById(kIdGroupNotifyCombo)) != nullptr);
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

TEST_CASE("GI5 提醒模式下拉框与置顶开关按 notifyMode / pinned 置位", "[groupinfo][apply]")
{
    std::unique_ptr<DuiControl> root = BuildGroupInfoBody();
    REQUIRE(root != nullptr);

    GroupInfoViewData d = MakeProjectGroupData();
    d.notifyMode = notify_mode_silent;
    d.pinned     = false;
    REQUIRE(ApplyGroupInfoDataToView(root.get(), d) == true);

    DuiComboBox* combo = dynamic_cast<DuiComboBox*>(root->FindCtrlById(kIdGroupNotifyCombo));
    DuiSwitch*   pin   = dynamic_cast<DuiSwitch*>(root->FindCtrlById(kIdGroupPinSwitch));
    REQUIRE(combo != nullptr);
    REQUIRE(pin   != nullptr);
    CHECK(combo->GetText() == CString(L"接收消息但不提醒"));
    CHECK(pin->IsChecked() == false);

    // 再换成屏蔽：只有群聊才有这一项
    d.notifyMode = notify_mode_block;
    REQUIRE(ApplyGroupInfoDataToView(root.get(), d) == true);
    CHECK(combo->GetText() == CString(L"屏蔽消息"));
}

TEST_CASE("GI5b 提醒模式下拉框的选项与右键二级菜单一致（六项，含屏蔽与只提醒 @我和关注的人）", "[groupinfo][notifymode]")
{
    std::unique_ptr<DuiControl> root = BuildGroupInfoBody();
    REQUIRE(root != nullptr);

    DuiComboBox* combo = dynamic_cast<DuiComboBox*>(root->FindCtrlById(kIdGroupNotifyCombo));
    REQUIRE(combo != nullptr);
    const std::vector<int> modes = balloonmain::GetNotifyModeChoices(true);
    REQUIRE(combo->GetCount() == (int)modes.size());
    //2026-10-06 起群聊有六种模式（加了「只提醒 @我和关注的人」，docs/消息提醒模式与消息盒子方案.md §12）
    const int kGroupNotifyModeCount = 6;
    REQUIRE(combo->GetCount() == kGroupNotifyModeCount);
    for (int i = 0; i < combo->GetCount(); ++i)
    {
        CHECK(combo->GetItemText(i) == CString(balloonmain::GetNotifyModeLabel(modes[(size_t)i])));
    }
    // 不可手输：只能从六种模式里选
    CHECK(combo->IsEditable() == false);
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

// =====================================================================
// FD. 字号随所在窗口的 DPI 变化
// =====================================================================
//
// 群资料里的文字标签只记磅值、绘制时按所在窗口的 DPI 现取字体。控件树没有挂到窗口上，
// 按全局 DPI 取字体，用例通过切换全局 DPI 观察字高变化。

namespace {

// 用例里切换的两档 DPI：96 对应 100% 缩放，192 对应 200% 缩放。
const int kFdLowDpi  = 96;
const int kFdHighDpi = 192;
// 1 英寸 = 72 磅。字高按 lfHeight = -MulDiv(磅值, DPI, 72) 计算。
const int kFdPointsPerInch = 72;

// 在作用域结束时把 DuiResMgr 的全局 DPI 恢复为进入时的值（断言失败提前退出时也恢复）。
class FdGroupDpiRestorer
{
public:
    FdGroupDpiRestorer()
        : m_savedDpi(DuiResMgr::Inst().GetDpi())
    {
    }

    ~FdGroupDpiRestorer()
    {
        // GetDpi 返回 0 表示进入时尚未设置过 DPI，此时恢复为系统 DPI。
        DuiResMgr::Inst().SetDpi(m_savedDpi > 0 ? m_savedDpi : DuiDpi::GetSystemDpi());
    }

    FdGroupDpiRestorer(const FdGroupDpiRestorer&) = delete;
    FdGroupDpiRestorer& operator=(const FdGroupDpiRestorer&) = delete;

private:
    int m_savedDpi;   // 进入作用域时的全局 DPI，析构时据此恢复
};

// 取字体的 lfHeight；句柄为空或无效时返回 0。
int FdGroupFontHeight(HFONT hf)
{
    LOGFONT lf;
    ::memset(&lf, 0, sizeof(lf));
    if (hf == NULL || ::GetObject(hf, sizeof(lf), &lf) == 0)
    {
        return 0;
    }
    return lf.lfHeight;
}

} // anonymous namespace

TEST_CASE("FD3 群资料的群名与公告正文标签按磅值设字号，字高随 DPI 变化",
          "[fontdpi][groupinfo]")
{
    FdGroupDpiRestorer restore;
    std::unique_ptr<DuiControl> root = BuildGroupInfoBody();
    REQUIRE(root != nullptr);

    const UINT ids[] = { kIdGroupName, kIdGroupAnnouncement };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        DuiLabel* lbl = dynamic_cast<DuiLabel*>(root->FindCtrlById(ids[i]));
        REQUIRE(lbl != nullptr);
        const int pt = lbl->GetTextPointSize();
        CHECK(pt > 0);

        DuiResMgr::Inst().SetDpi(kFdLowDpi);
        CHECK(FdGroupFontHeight(lbl->GetFont()) == -::MulDiv(pt, kFdLowDpi, kFdPointsPerInch));
        DuiResMgr::Inst().SetDpi(kFdHighDpi);
        CHECK(FdGroupFontHeight(lbl->GetFont()) == -::MulDiv(pt, kFdHighDpi, kFdPointsPerInch));
    }
}

// =====================================================================
// GIC1 / GIC2：点击群名 / 群号复制、「查看消息记录」接入（2026-10-01）
// =====================================================================

TEST_CASE("GIC1 群名、群号那一行可点；点群名复制完整群名，点群号复制纯数字群号", "[groupinfo][copy]")
{
    //群资料窗口的客户区尺寸量级（像素），取值无特殊含义
    const int kBodyW = 420;
    const int kBodyH = 520;

    std::unique_ptr<DuiControl> root = BuildGroupInfoBody();
    REQUIRE(root != nullptr);
    const GroupInfoViewData d = MakeProjectGroupData();
    REQUIRE(ApplyGroupInfoDataToView(root.get(), d));
    root->ForceLayout(RECT{ 0, 0, kBodyW, kBodyH });

    //可点：悬停在标签上时给手形光标（OnSetCursor 返回 true），普通标签返回 false
    const UINT clickable[] = { kIdGroupName, kIdGroupMeta };
    for (size_t i = 0; i < sizeof(clickable) / sizeof(clickable[0]); ++i)
    {
        INFO("ctrlId " << clickable[i]);
        DuiControl* lbl = root->FindCtrlById(clickable[i]);
        REQUIRE(lbl != nullptr);
        const RECT rc = lbl->GetRect();
        REQUIRE(rc.right > rc.left);
        const POINT inside = { rc.left + 2, (rc.top + rc.bottom) / 2 };
        CHECK(lbl->OnSetCursor(inside));
    }
    DuiControl* owner = root->FindCtrlById(kIdGroupOwner);
    REQUIRE(owner != nullptr);
    const RECT rcOwner = owner->GetRect();
    const POINT inOwner = { rcOwner.left + 2, (rcOwner.top + rcOwner.bottom) / 2 };
    CHECK_FALSE(owner->OnSetCursor(inOwner));

    //复制的文字
    CString text;
    REQUIRE(GroupInfoCopyText(d, kIdGroupName, text));
    CHECK(text == CString(_T("FlamingoIM 项目组")));
    REQUIRE(GroupInfoCopyText(d, kIdGroupMeta, text));
    CHECK(text == CString(_T("700001")));
    //与标签上显示的群号一致
    DuiLabel* meta = dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdGroupMeta));
    REQUIRE(meta != nullptr);
    CHECK(meta->GetText().Find(text) >= 0);

    //其余控件不复制；群名为空、群 id 无效时没有可复制的内容
    CHECK_FALSE(GroupInfoCopyText(d, kIdGroupOwner, text));
    GroupInfoViewData empty;
    CHECK_FALSE(GroupInfoCopyText(empty, kIdGroupName, text));
    CHECK_FALSE(GroupInfoCopyText(empty, kIdGroupMeta, text));
}

TEST_CASE("GIC2 查看消息记录：打开群聊类型、本群 id 的消息记录；按钮可用", "[groupinfo][copy]")
{
    const GroupInfoViewData d = MakeProjectGroupData();
    int ctype = 0;
    __int64 cid = 0;
    REQUIRE(GroupInfoHistoryTarget(d, ctype, cid));
    //会话类型 2 即群聊（与协议、FMG_MSG_OPEN_HISTORY 的约定一致）
    CHECK(ctype == 2);
    CHECK(cid == d.groupid);

    //群 id 无效时不打开
    GroupInfoViewData empty;
    CHECK_FALSE(GroupInfoHistoryTarget(empty, ctype, cid));

    //按钮存在且可用（不再是占位）
    std::unique_ptr<DuiControl> root = BuildGroupInfoBody();
    REQUIRE(root != nullptr);
    DuiButton* btn = dynamic_cast<DuiButton*>(root->FindCtrlById(kIdGroupBtnHistory));
    REQUIRE(btn != nullptr);
    CHECK(btn->IsEnabled());
}

TEST_CASE("GIC3 复制提示条落在头部资料区下方，不遮住头像、群名、群号与群主", "[groupinfo][copy]")
{
    //群资料窗口的客户区尺寸量级（像素），取值无特殊含义
    const int kBodyW = 420;
    const int kBodyH = 520;

    std::unique_ptr<DuiControl> root = BuildGroupInfoBody();
    REQUIRE(root != nullptr);
    REQUIRE(ApplyGroupInfoDataToView(root.get(), MakeProjectGroupData()));

    //与群资料窗口相同的挂法：提示条挂在根容器末尾，布局高度为 0
    DuiVBox* rootBox = dynamic_cast<DuiVBox*>(root.get());
    REQUIRE(rootBox != nullptr);
    std::unique_ptr<DuiToast> toastOwner(new DuiToast());
    DuiToast* toast = toastOwner.get();
    rootBox->AddChild(std::move(toastOwner), DuiLayout::Hint().Fixed(0));
    root->ForceLayout(RECT{ 0, 0, kBodyW, kBodyH });

    //默认位置（距顶 40 像素）会压在群号那一行上，这正是要改的情形
    const int offset = GroupInfoToastTopOffset(root.get());
    REQUIRE(offset > 0);
    toast->SetTopOffset(offset);
    toast->Show(_T("已复制群号"));
    const RECT rcToast = toast->GetRect();
    toast->HideNow();
    REQUIRE(rcToast.bottom > rcToast.top);

    //提示条的文字框顶边不高于头部各控件的底边：头部任何一处都不被遮住
    const UINT header[] = { kIdGroupAvatar, kIdGroupName, kIdGroupMeta, kIdGroupOwner };
    for (size_t i = 0; i < sizeof(header) / sizeof(header[0]); ++i)
    {
        INFO("ctrlId " << header[i]);
        DuiControl* c = root->FindCtrlById(header[i]);
        REQUIRE(c != nullptr);
        const RECT rc = c->GetRect();
        CHECK(root->GetRect().top + offset >= rc.bottom);
    }

    //提示条仍在客户区之内，且挨着头部（不跑到窗口底部去）
    CHECK(rcToast.bottom <= kBodyH);
    DuiControl* owner = root->FindCtrlById(kIdGroupOwner);
    DuiControl* avatar = root->FindCtrlById(kIdGroupAvatar);
    REQUIRE(owner != nullptr);
    REQUIRE(avatar != nullptr);
    const LONG headerBottom = (owner->GetRect().bottom > avatar->GetRect().bottom)
                            ? owner->GetRect().bottom : avatar->GetRect().bottom;
    CHECK(root->GetRect().top + offset == headerBottom);

    //空根控件返回 -1，调用方保持提示条原有位置
    CHECK(GroupInfoToastTopOffset(nullptr) == -1);
}

// =====================================================================
// GIX1 / GIX2：底部按钮按角色区分（2026-10-01）
// =====================================================================

namespace {

// 群资料窗口客户区尺寸量级（像素），取值无特殊含义，与 GIC 组一致
const int kGixBodyW = 420;
const int kGixBodyH = 520;

// 取底部某个按钮当前的矩形；按钮不存在时中止用例。
RECT GixButtonRect(DuiControl* root, UINT id)
{
    DuiControl* c = root->FindCtrlById(id);
    REQUIRE(c != nullptr);
    return c->GetRect();
}

// 两个矩形在水平方向上是否不重叠（左边那个的右沿不超过右边那个的左沿）。
bool GixNoOverlapH(const RECT& left, const RECT& right)
{
    return left.right <= right.left;
}

// 宽度。
int GixWidth(const RECT& rc)
{
    return rc.right - rc.left;
}

} // anonymous namespace

TEST_CASE("GIX1 群主的底部多出解散群聊按钮，三个按钮等分一行；其余角色只有两个按钮", "[groupinfo][groupexit]")
{
    std::unique_ptr<DuiControl> root = BuildGroupInfoBody();
    REQUIRE(root != nullptr);

    //按钮存在、文字正确，初始（未知角色）时隐藏
    DuiButton* btnDismiss = dynamic_cast<DuiButton*>(root->FindCtrlById(kIdGroupBtnDismiss));
    REQUIRE(btnDismiss != nullptr);
    CHECK(btnDismiss->GetText() == CString(_T("解散群聊")));
    CHECK_FALSE(btnDismiss->IsVisible());

    //群主
    GroupInfoViewData owner = MakeProjectGroupData();
    owner.isOwner = true;
    REQUIRE(ApplyGroupInfoDataToView(root.get(), owner));
    root->ForceLayout(RECT{ 0, 0, kGixBodyW, kGixBodyH });
    CHECK(btnDismiss->IsVisible());
    const RECT rcHistory = GixButtonRect(root.get(), kIdGroupBtnHistory);
    const RECT rcQuit    = GixButtonRect(root.get(), kIdGroupBtnQuit);
    const RECT rcDismiss = GixButtonRect(root.get(), kIdGroupBtnDismiss);
    CHECK(GixWidth(rcDismiss) > 0);
    CHECK(GixNoOverlapH(rcHistory, rcQuit));
    CHECK(GixNoOverlapH(rcQuit, rcDismiss));
    //三个按钮等分：宽度相差不超过 1 像素（整除余数）
    CHECK(std::abs(GixWidth(rcHistory) - GixWidth(rcDismiss)) <= 1);
    CHECK(std::abs(GixWidth(rcQuit) - GixWidth(rcDismiss)) <= 1);
    //同一行
    CHECK(rcDismiss.top == rcQuit.top);
    CHECK(rcDismiss.bottom == rcQuit.bottom);

    //普通成员：解散按钮隐藏，另两个按钮各占一半，比三等分时更宽
    GroupInfoViewData member = MakeProjectGroupData();
    REQUIRE(ApplyGroupInfoDataToView(root.get(), member));
    root->ForceLayout(RECT{ 0, 0, kGixBodyW, kGixBodyH });
    CHECK_FALSE(btnDismiss->IsVisible());
    const RECT rcHistory2 = GixButtonRect(root.get(), kIdGroupBtnHistory);
    const RECT rcQuit2    = GixButtonRect(root.get(), kIdGroupBtnQuit);
    CHECK(GixWidth(rcQuit2) > GixWidth(rcQuit));
    CHECK(std::abs(GixWidth(rcHistory2) - GixWidth(rcQuit2)) <= 1);
}

TEST_CASE("GIX2 先按普通成员排版再换成群主数据，解散按钮的位置与尺寸仍然正确", "[groupinfo][groupexit]")
{
    std::unique_ptr<DuiControl> root = BuildGroupInfoBody();
    REQUIRE(root != nullptr);

    //先按普通成员排一次版：解散按钮隐藏期间这次排版会跳过它
    REQUIRE(ApplyGroupInfoDataToView(root.get(), MakeProjectGroupData()));
    root->ForceLayout(RECT{ 0, 0, kGixBodyW, kGixBodyH });

    //再换成群主数据，不由调用方手动重排：ApplyGroupInfoDataToView 自己负责让底部重新排版
    GroupInfoViewData owner = MakeProjectGroupData();
    owner.isOwner = true;
    REQUIRE(ApplyGroupInfoDataToView(root.get(), owner));

    const RECT rcQuit    = GixButtonRect(root.get(), kIdGroupBtnQuit);
    const RECT rcDismiss = GixButtonRect(root.get(), kIdGroupBtnDismiss);
    CHECK(GixWidth(rcDismiss) > 0);
    CHECK(GixNoOverlapH(rcQuit, rcDismiss));
    CHECK(std::abs(GixWidth(rcQuit) - GixWidth(rcDismiss)) <= 1);
    //解散按钮在客户区之内
    CHECK(rcDismiss.right <= kGixBodyW);
    CHECK(rcDismiss.top == rcQuit.top);
}

// =====================================================================
// GI9 / GI10 群简介（2026-10-10 补上：群管理页说群简介「显示在群资料页」，此前群资料窗口里没有）
// =====================================================================

TEST_CASE("GI9 群资料显示群简介：有内容时显示原文（换行换成空格），为空时显占位", "[groupinfo][apply]")
{
    std::unique_ptr<DuiControl> root = BuildGroupInfoBody();
    REQUIRE(root != nullptr);

    GroupInfoViewData d = MakeProjectGroupData();
    d.intro = _T("产品与研发的日常沟通群\r\n版本发布前请留意群公告");
    REQUIRE(ApplyGroupInfoDataToView(root.get(), d) == true);
    DuiLabel* intro = dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdGroupIntro));
    REQUIRE(intro != nullptr);
    CHECK(intro->GetText() == CString(_T("产品与研发的日常沟通群 版本发布前请留意群公告")));

    d.intro = _T("");
    REQUIRE(ApplyGroupInfoDataToView(root.get(), d) == true);
    CHECK(intro->GetText() == CString(_T("暂无群简介")));
}

TEST_CASE("GI10 加了群简介之后，群资料按设计尺寸排得下：底部按钮与置顶行不被压缩", "[groupinfo][layoutfit]")
{
    std::unique_ptr<DuiControl> root = BuildGroupInfoBody();
    REQUIRE(root != nullptr);
    GroupInfoViewData d = MakeProjectGroupData();
    d.intro = _T("产品与研发的日常沟通群");
    d.isOwner = true;   // 群主多一个「解散群聊」按钮，底部一行最满
    REQUIRE(ApplyGroupInfoDataToView(root.get(), d) == true);

    const int contentW = kGroupInfoWidth;
    const int contentH = kGroupInfoHeight - kGroupInfoTitleBarH;
    const UINT ids[] = { kIdGroupBtnHistory, kIdGroupBtnQuit, kIdGroupBtnDismiss, kIdGroupPinSwitch, kIdGroupIntro };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        INFO("control id = " << ids[i]);
        const layoutfit::FitResult r = layoutfit::MeasureAt(root.get(), ids[i], contentW, contentH);
        REQUIRE(r.m_found);
        CHECK(r.m_bottom <= contentH);
        CHECK(r.m_height == r.m_roomyHeight);
    }
    //最小尺寸下同样排得下
    const layoutfit::FitResult minFit = layoutfit::MeasureAt(root.get(), kIdGroupBtnHistory, kGroupInfoMinWidth,
                                                            kGroupInfoMinHeight - kGroupInfoTitleBarH);
    REQUIRE(minFit.m_found);
    CHECK(minFit.m_bottom <= kGroupInfoMinHeight - kGroupInfoTitleBarH);
    CHECK(minFit.m_height == minFit.m_roomyHeight);
}
