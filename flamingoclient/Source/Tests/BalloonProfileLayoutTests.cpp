/**
 *  BalloonProfileLayoutTests.cpp
 *
 *  P 系列测试:验证 BalloonProfileLayout 的种子函数 BuildProfileBody()
 *  产出的 balloonui 控件树符合"控件点名契约" —— 对话框所有需要被代码
 *  引用的控件都能按既定数字 id 找到、类型正确、初始可见性正确。
 *
 *  设计稿:design/个人信息.png(查看态) / design/个人信息02.png(编辑态)。
 *
 *  覆盖用例:
 *    P1   BuildProfileBody 返回非空
 *    P2   顶部 Banner 区(avatar / nickname / account)三个 ctrlId 存在
 *    P3   (已删:状态胶囊去除)
 *    P4   6 行信息查看态 ctrlId 存在,类型是 DuiLabel
 *    P5   6 行信息编辑态 ctrlId 存在;昵称/签名/生日/地址/邮箱是
 *         DuiEditHost,性别是 DuiComboBox
 *    P6   初始可见性:View 系列可见,Edit 系列隐藏(初始处于"查看态")
 *    P7   4 底部按钮 ctrlId 存在;初始 Close/Edit 可见,Cancel/Save 隐藏
 *    P8   全部 ctrlId 互不重复
 *
 *  用 Catch2 v3(amalgamated)。全局 _Module 由 BalloonChatLayoutTests.cpp
 *  统一定义,本文件只引用。
 *
 *  balloonwj@qq.com   2026-05-28
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../BalloonMain/BalloonProfileLayout.h"
#include "../BalloonMain/MainPanelUserData.h"   // GetUserAvatarColor
#include "../../balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../balloonui/balloonui/Controls/Input/DuiEditHost.h"

using namespace balloonwjui;
using namespace balloonmain;

namespace {

// 递归收集子树里所有<u>非 0</u> 的 ctrlId(同 BalloonMainLayoutTests 风格)。
void CollectIdsProfile(const DuiControl* node, std::vector<UINT>& out)
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
        CollectIdsProfile(child.get(), out);
    }
}

bool HasDuplicateProfileId(std::vector<UINT> ids)
{
    std::sort(ids.begin(), ids.end());
    return std::adjacent_find(ids.begin(), ids.end()) != ids.end();
}

} // anonymous namespace

// =====================================================================
// P. 对话框控件契约
// =====================================================================

TEST_CASE("P1 BuildProfileBody 返回非空控件树", "[p2][profile]")
{
    std::unique_ptr<DuiControl> root = BuildProfileBody();
    REQUIRE(root != nullptr);
}

TEST_CASE("P2 顶部 Banner 区 avatar / nickname / account 三 ctrlId 存在",
          "[p2][profile]")
{
    std::unique_ptr<DuiControl> root = BuildProfileBody();
    REQUIRE(root != nullptr);

    CHECK(root->FindCtrlById(kIdProfileAvatar)   != nullptr);
    CHECK(root->FindCtrlById(kIdProfileNickname) != nullptr);
    CHECK(root->FindCtrlById(kIdProfileAccount)  != nullptr);
}

// (原 P3「状态胶囊 4 个 ctrlId 存在」用例已随状态胶囊去除而删除。)

TEST_CASE("P4 6 行查看态 ctrlId 存在,类型是 DuiLabel",
          "[p2][profile]")
{
    std::unique_ptr<DuiControl> root = BuildProfileBody();
    REQUIRE(root != nullptr);

    const UINT viewIds[] = { kIdNicknameView, kIdSignatureView,
                             kIdGenderView,   kIdBirthdayView,
                             kIdLocationView, kIdEmailView };
    for (size_t i = 0; i < sizeof(viewIds) / sizeof(viewIds[0]); ++i)
    {
        INFO("view id = " << viewIds[i]);
        DuiControl* c = root->FindCtrlById(viewIds[i]);
        REQUIRE(c != nullptr);
        CHECK(dynamic_cast<DuiLabel*>(c) != nullptr);
    }
}

TEST_CASE("P5 6 行编辑态 ctrlId 存在;5 行是 DuiEditHost,性别是 DuiComboBox",
          "[p2][profile]")
{
    std::unique_ptr<DuiControl> root = BuildProfileBody();
    REQUIRE(root != nullptr);

    // 5 行 DuiEditHost
    const UINT editIds[] = { kIdNicknameEdit, kIdSignatureEdit,
                             kIdBirthdayEdit, kIdLocationEdit,
                             kIdEmailEdit };
    for (size_t i = 0; i < sizeof(editIds) / sizeof(editIds[0]); ++i)
    {
        INFO("edit id = " << editIds[i]);
        DuiControl* c = root->FindCtrlById(editIds[i]);
        REQUIRE(c != nullptr);
        CHECK(dynamic_cast<DuiEditHost*>(c) != nullptr);
    }

    // 性别下拉
    DuiControl* gender = root->FindCtrlById(kIdGenderEdit);
    REQUIRE(gender != nullptr);
    CHECK(dynamic_cast<DuiComboBox*>(gender) != nullptr);
}

TEST_CASE("P6 初始可见性:View 系列可见,Edit 系列隐藏",
          "[p2][profile]")
{
    std::unique_ptr<DuiControl> root = BuildProfileBody();
    REQUIRE(root != nullptr);

    const UINT viewIds[] = { kIdNicknameView, kIdSignatureView,
                             kIdGenderView,   kIdBirthdayView,
                             kIdLocationView, kIdEmailView };
    for (size_t i = 0; i < sizeof(viewIds) / sizeof(viewIds[0]); ++i)
    {
        INFO("view id = " << viewIds[i]);
        DuiControl* c = root->FindCtrlById(viewIds[i]);
        REQUIRE(c != nullptr);
        CHECK(c->IsVisible() == true);
    }

    const UINT editIds[] = { kIdNicknameEdit, kIdSignatureEdit,
                             kIdGenderEdit,   kIdBirthdayEdit,
                             kIdLocationEdit, kIdEmailEdit };
    for (size_t i = 0; i < sizeof(editIds) / sizeof(editIds[0]); ++i)
    {
        INFO("edit id = " << editIds[i]);
        DuiControl* c = root->FindCtrlById(editIds[i]);
        REQUIRE(c != nullptr);
        CHECK(c->IsVisible() == false);
    }
}

TEST_CASE("P7 4 底部按钮 ctrlId 存在;初始 Close/Edit 可见,Cancel/Save 隐藏",
          "[p2][profile]")
{
    std::unique_ptr<DuiControl> root = BuildProfileBody();
    REQUIRE(root != nullptr);

    DuiControl* btnClose  = root->FindCtrlById(kIdBtnClose);
    DuiControl* btnEdit   = root->FindCtrlById(kIdBtnEdit);
    DuiControl* btnCancel = root->FindCtrlById(kIdBtnCancel);
    DuiControl* btnSave   = root->FindCtrlById(kIdBtnSave);
    REQUIRE(btnClose  != nullptr);
    REQUIRE(btnEdit   != nullptr);
    REQUIRE(btnCancel != nullptr);
    REQUIRE(btnSave   != nullptr);
    CHECK(dynamic_cast<DuiButton*>(btnClose)  != nullptr);
    CHECK(dynamic_cast<DuiButton*>(btnEdit)   != nullptr);
    CHECK(dynamic_cast<DuiButton*>(btnCancel) != nullptr);
    CHECK(dynamic_cast<DuiButton*>(btnSave)   != nullptr);

    CHECK(btnClose->IsVisible()  == true);
    CHECK(btnEdit->IsVisible()   == true);
    CHECK(btnCancel->IsVisible() == false);
    CHECK(btnSave->IsVisible()   == false);
}

TEST_CASE("P8 全部 ctrlId 互不重复", "[p2][profile][ids]")
{
    std::unique_ptr<DuiControl> root = BuildProfileBody();
    REQUIRE(root != nullptr);

    std::vector<UINT> ids;
    CollectIdsProfile(root.get(), ids);
    CHECK_FALSE(ids.empty());
    CHECK_FALSE(HasDuplicateProfileId(ids));
}

// =====================================================================
// F. ApplyProfileDataToView:把 ProfileDataInput 写到控件树
// =====================================================================

namespace {

ProfileDataInput MakeWangYapingInput()
{
    ProfileDataInput info;
    info.userid    = 5312;
    info.account   = _T("MY0312");
    info.nickname  = _T("王雅平");
    info.signature = _T("用数据验证每个判断，知行合一。");
    info.gender    = 0;            // testdata.sql:391 王雅平 gender=0
    info.birthday  = 19950402;
    info.address   = _T("上海·张江 17F");
    info.email     = _T("wangyp@mingyuan.com");
    return info;
}

} // anonymous namespace

TEST_CASE("F1 ApplyProfileDataToView 把 6 行字段写到 View 系列 DuiLabel",
          "[p2][profile][apply]")
{
    std::unique_ptr<DuiControl> root = BuildProfileBody();
    REQUIRE(root != nullptr);

    ProfileDataInput info = MakeWangYapingInput();
    REQUIRE(ApplyProfileDataToView(root.get(), info) == true);

    DuiLabel* nick = dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdNicknameView));
    DuiLabel* sig  = dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdSignatureView));
    DuiLabel* loc  = dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdLocationView));
    DuiLabel* eml  = dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdEmailView));
    REQUIRE(nick != nullptr);
    REQUIRE(sig  != nullptr);
    REQUIRE(loc  != nullptr);
    REQUIRE(eml  != nullptr);
    CHECK(nick->GetText() == CString(_T("王雅平")));
    CHECK(sig->GetText()  == CString(_T("用数据验证每个判断，知行合一。")));
    CHECK(loc->GetText()  == CString(_T("上海·张江 17F")));
    CHECK(eml->GetText()  == CString(_T("wangyp@mingyuan.com")));
}

TEST_CASE("F2 性别 0/1 -> 男/女 文字写到 GenderView",
          "[p2][profile][apply]")
{
    SECTION("gender=0 -> 男")
    {
        std::unique_ptr<DuiControl> root = BuildProfileBody();
        ProfileDataInput info = MakeWangYapingInput();
        info.gender = 0;
        REQUIRE(ApplyProfileDataToView(root.get(), info) == true);
        DuiLabel* g = dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdGenderView));
        REQUIRE(g != nullptr);
        CHECK(g->GetText() == CString(_T("男")));
    }
    SECTION("gender=1 -> 女")
    {
        std::unique_ptr<DuiControl> root = BuildProfileBody();
        ProfileDataInput info = MakeWangYapingInput();
        info.gender = 1;
        REQUIRE(ApplyProfileDataToView(root.get(), info) == true);
        DuiLabel* g = dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdGenderView));
        REQUIRE(g != nullptr);
        CHECK(g->GetText() == CString(_T("女")));
    }
}

TEST_CASE("F3 生日 YYYYMMDD int -> YYYY-MM-DD 字符串;0 写空串",
          "[p2][profile][apply]")
{
    SECTION("19950402 -> 1995-04-02")
    {
        std::unique_ptr<DuiControl> root = BuildProfileBody();
        ProfileDataInput info = MakeWangYapingInput();
        info.birthday = 19950402;
        REQUIRE(ApplyProfileDataToView(root.get(), info) == true);
        DuiLabel* b = dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdBirthdayView));
        REQUIRE(b != nullptr);
        CHECK(b->GetText() == CString(_T("1995-04-02")));
    }
    SECTION("0 -> 空串")
    {
        std::unique_ptr<DuiControl> root = BuildProfileBody();
        ProfileDataInput info = MakeWangYapingInput();
        info.birthday = 0;
        REQUIRE(ApplyProfileDataToView(root.get(), info) == true);
        DuiLabel* b = dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdBirthdayView));
        REQUIRE(b != nullptr);
        CHECK(b->GetText().IsEmpty());
    }
}

TEST_CASE("F4 顶部 Banner avatar 取昵称首字、底色按 userid 哈希、nickname/account 行",
          "[p2][profile][apply]")
{
    std::unique_ptr<DuiControl> root = BuildProfileBody();
    ProfileDataInput info = MakeWangYapingInput();
    REQUIRE(ApplyProfileDataToView(root.get(), info) == true);

    DuiAvatar* av = dynamic_cast<DuiAvatar*>(root->FindCtrlById(kIdProfileAvatar));
    DuiLabel*  nm = dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdProfileNickname));
    DuiLabel*  ac = dynamic_cast<DuiLabel*>(root->FindCtrlById(kIdProfileAccount));
    REQUIRE(av != nullptr);
    REQUIRE(nm != nullptr);
    REQUIRE(ac != nullptr);
    // DuiAvatar 在 fallback 模式按 SetName 全名算缩写,完整名字喂进去即可。
    CHECK(av->GetName() == CString(_T("王雅平")));
    CHECK(av->GetFallbackBgColor() == balloonmain::GetUserAvatarColor(5312));
    CHECK(nm->GetText() == CString(_T("王雅平")));
    // 账号行显示登录账号名(MY0312),与登录一致;不再是 userid 数字。
    CHECK(ac->GetText() == CString(_T("账号 MY0312")));
}

TEST_CASE("F5 性别 ComboBox 初值 2 项(男/女),SetCurSel 与 gender 对齐",
          "[p2][profile][apply]")
{
    std::unique_ptr<DuiControl> root = BuildProfileBody();
    ProfileDataInput info = MakeWangYapingInput();
    info.gender = 1;
    REQUIRE(ApplyProfileDataToView(root.get(), info) == true);

    DuiComboBox* cb = dynamic_cast<DuiComboBox*>(root->FindCtrlById(kIdGenderEdit));
    REQUIRE(cb != nullptr);
    CHECK(cb->GetCount()      == 2);
    CHECK(cb->GetItemText(0)  == CString(_T("男")));
    CHECK(cb->GetItemText(1)  == CString(_T("女")));
    CHECK(cb->GetCurSel()     == 1);
}

TEST_CASE("F6 ApplyProfileDataToView 对 null root 返 false 不崩",
          "[p2][profile][apply]")
{
    ProfileDataInput info = MakeWangYapingInput();
    CHECK(ApplyProfileDataToView(nullptr, info) == false);
}

// =====================================================================
// S. ApplyProfileButtonStyles:按钮与状态胶囊视觉样式
// =====================================================================

TEST_CASE("S1 关闭/取消用 Variant::Default;编辑资料/保存用 Primary",
          "[p2][profile][styles]")
{
    std::unique_ptr<DuiControl> root = BuildProfileBody();
    REQUIRE(root != nullptr);
    REQUIRE(ApplyProfileButtonStyles(root.get(), ProfileVisualResources()) == true);

    DuiButton* btnClose  = dynamic_cast<DuiButton*>(root->FindCtrlById(kIdBtnClose));
    DuiButton* btnEdit   = dynamic_cast<DuiButton*>(root->FindCtrlById(kIdBtnEdit));
    DuiButton* btnCancel = dynamic_cast<DuiButton*>(root->FindCtrlById(kIdBtnCancel));
    DuiButton* btnSave   = dynamic_cast<DuiButton*>(root->FindCtrlById(kIdBtnSave));
    REQUIRE(btnClose  != nullptr);
    REQUIRE(btnEdit   != nullptr);
    REQUIRE(btnCancel != nullptr);
    REQUIRE(btnSave   != nullptr);
    CHECK(btnClose->GetVariant()  == DuiButton::Variant::Default);
    CHECK(btnCancel->GetVariant() == DuiButton::Variant::Default);
    CHECK(btnEdit->GetVariant()   == DuiButton::Variant::Primary);
    CHECK(btnSave->GetVariant()   == DuiButton::Variant::Primary);
}

// (原 S2「4 状态胶囊 Variant=Default」用例已随状态胶囊去除而删除。)

TEST_CASE("S3 ApplyProfileButtonStyles 对 null root 返 false 不崩",
          "[p2][profile][styles]")
{
    CHECK(ApplyProfileButtonStyles(nullptr, ProfileVisualResources()) == false);
}

// =====================================================================
// L. 布局几何:按钮高度 + 编辑态切换重排
//    这两类用例对应用户报的三个 bug 中可纯逻辑复现的两个:
//      · 按钮太高 —— footer 按钮交叉轴默认 AlignFill 被拉满,须配 alignCross=center
//        才取 fixedHeight;改前断言会是 46(hbox 整高),改后 30。
//      · 编辑态切回不了 —— 行 / footer 容器自身矩形不变,根 ForceLayout 经 SetRect
//        相等提前返回不重排其子,刚可见的编辑框 / 取消保存停留在 {0,0,0,0};
//        SetProfileEditMode 内逐容器 ForceLayout 修正之。
// =====================================================================

namespace {

// 控件矩形是否非空(宽、高都 > 0)。
bool RectNonEmpty(const DuiControl* c)
{
    if (c == nullptr)
    {
        return false;
    }
    const RECT& rc = c->GetRect();
    return (rc.right - rc.left) > 0 && (rc.bottom - rc.top) > 0;
}

} // anonymous namespace

TEST_CASE("L1 footer 按钮按 alignCross=center 取 30 高,不被拉满 hbox",
          "[p2][profile][layout]")
{
    std::unique_ptr<DuiControl> root = BuildProfileBody();
    REQUIRE(root != nullptr);

    // 模拟对话框 ResizeClient(420,392):按客户区尺寸排版。
    RECT client = { 0, 0, 420, 392 };
    root->ForceLayout(client);

    // 查看态下 Close / Edit 可见,断言它们高度 == 30(若退回默认 AlignFill 会等于
    // footer hbox 整高 46)。
    DuiControl* btnClose = root->FindCtrlById(kIdBtnClose);
    DuiControl* btnEdit  = root->FindCtrlById(kIdBtnEdit);
    REQUIRE(btnClose != nullptr);
    REQUIRE(btnEdit  != nullptr);
    RECT rcClose = btnClose->GetRect();
    RECT rcEdit  = btnEdit->GetRect();
    CHECK((rcClose.bottom - rcClose.top) == 30);
    CHECK((rcEdit.bottom  - rcEdit.top)  == 30);
}

TEST_CASE("L2 SetProfileEditMode(true) 让编辑框与取消/保存拿到非空矩形",
          "[p2][profile][layout][editmode]")
{
    std::unique_ptr<DuiControl> root = BuildProfileBody();
    REQUIRE(root != nullptr);

    RECT client = { 0, 0, 420, 392 };
    root->ForceLayout(client);

    REQUIRE(SetProfileEditMode(root.get(), true) == true);

    // 6 个编辑控件(含性别 combobox)+ 取消 / 保存,切到可见后必须拿到非空矩形
    // (改前停留在 {0,0,0,0},这正是"编辑框出不来 / 按钮点不动"的根因)。
    const UINT editIds[] = {
        kIdNicknameEdit, kIdSignatureEdit, kIdGenderEdit,
        kIdBirthdayEdit, kIdLocationEdit,  kIdEmailEdit
    };
    for (size_t i = 0; i < sizeof(editIds) / sizeof(editIds[0]); ++i)
    {
        INFO("edit id = " << editIds[i]);
        DuiControl* c = root->FindCtrlById(editIds[i]);
        REQUIRE(c != nullptr);
        CHECK(c->IsVisible());
        CHECK(RectNonEmpty(c));
    }
    CHECK(RectNonEmpty(root->FindCtrlById(kIdBtnCancel)));
    CHECK(RectNonEmpty(root->FindCtrlById(kIdBtnSave)));
}

TEST_CASE("L3 SetProfileEditMode(false) 切回查看态:view 系列与关闭/编辑非空",
          "[p2][profile][layout][editmode]")
{
    std::unique_ptr<DuiControl> root = BuildProfileBody();
    REQUIRE(root != nullptr);

    RECT client = { 0, 0, 420, 392 };
    root->ForceLayout(client);

    // 先进编辑态再切回,模拟用户"编辑资料 → 取消"的操作序列。
    REQUIRE(SetProfileEditMode(root.get(), true)  == true);
    REQUIRE(SetProfileEditMode(root.get(), false) == true);

    const UINT viewIds[] = {
        kIdNicknameView, kIdSignatureView, kIdGenderView,
        kIdBirthdayView, kIdLocationView,  kIdEmailView
    };
    for (size_t i = 0; i < sizeof(viewIds) / sizeof(viewIds[0]); ++i)
    {
        INFO("view id = " << viewIds[i]);
        DuiControl* c = root->FindCtrlById(viewIds[i]);
        REQUIRE(c != nullptr);
        CHECK(c->IsVisible());
        CHECK(RectNonEmpty(c));
    }
    CHECK(RectNonEmpty(root->FindCtrlById(kIdBtnClose)));
    CHECK(RectNonEmpty(root->FindCtrlById(kIdBtnEdit)));
}

TEST_CASE("L4 SetProfileEditMode 对 null root 返 false 不崩",
          "[p2][profile][layout][editmode]")
{
    CHECK(SetProfileEditMode(nullptr, true)  == false);
    CHECK(SetProfileEditMode(nullptr, false) == false);
}

// =====================================================================
// B. ParseProfileBirthday:"YYYY-MM-DD" 文本 → YYYYMMDD 整数（FormatBirthday 的逆）
//    保存资料时把生日编辑框文本转回整数;非法格式要能识别以提示用户、不提交。
// =====================================================================

TEST_CASE("B1 合法 YYYY-MM-DD → YYYYMMDD 整数", "[p2][profile][birthday]")
{
    CHECK(ParseProfileBirthday(_T("1995-04-02")) == 19950402);
    CHECK(ParseProfileBirthday(_T("2000-12-31")) == 20001231);
    CHECK(ParseProfileBirthday(_T("1900-01-01")) == 19000101);
}

TEST_CASE("B2 空串 / 纯空白 → 0（未设生日）", "[p2][profile][birthday]")
{
    CHECK(ParseProfileBirthday(_T(""))      == 0);
    CHECK(ParseProfileBirthday(_T("   "))   == 0);
}

TEST_CASE("B3 首尾空白被裁剪后再解析", "[p2][profile][birthday]")
{
    CHECK(ParseProfileBirthday(_T("  1995-04-02  ")) == 19950402);
}

TEST_CASE("B4 非法格式 → -1", "[p2][profile][birthday]")
{
    CHECK(ParseProfileBirthday(_T("bad"))         == -1);   // 不是日期
    CHECK(ParseProfileBirthday(_T("1995/04/02"))  == -1);   // 分隔符不对
    CHECK(ParseProfileBirthday(_T("1995-4-2"))    == -1);   // 位数不对(长度≠10)
    CHECK(ParseProfileBirthday(_T("19950402"))    == -1);   // 缺分隔符
    CHECK(ParseProfileBirthday(_T("1995-13-02"))  == -1);   // 月越界
    CHECK(ParseProfileBirthday(_T("1995-04-32"))  == -1);   // 日越界
    CHECK(ParseProfileBirthday(_T("1899-04-02"))  == -1);   // 年越界
    CHECK(ParseProfileBirthday(_T("19x5-04-02"))  == -1);   // 含非数字
}

TEST_CASE("B5 与 FormatBirthday 往返一致", "[p2][profile][birthday]")
{
    // ParseProfileBirthday(FormatBirthday(n)) == n（n 为合法生日）。
    // FormatBirthday 是文件私有,这里用其公开效果间接覆盖:对几个值手工写出格式串。
    CHECK(ParseProfileBirthday(_T("1988-08-08")) == 19880808);
    CHECK(ParseProfileBirthday(_T("2010-10-10")) == 20101010);
}

// =====================================================================
// PW —— 行左侧字段名列宽（对应"个性签名"标签被裁掉半个字的缺陷）
// =====================================================================

namespace {

// 产品侧列宽的上下限,与 BalloonProfileLayout.cpp 的 kFieldLabelWidthMin /
// kFieldLabelWidthMax 同值。测试自带一份而不去引产品的匿名命名空间常量 —— 这样
// 产品那两个值一旦被改动,本文件的断言会立刻炸出来,而不是跟着一起变形失去意义。
const int kColumnWidthMin = 60;
const int kColumnWidthMax = 88;

// 全部八个字段名,顺序与产品的 kFieldRows 一致。同样自带一份:将来产品加了字段
// 却忘了它更长,PW4 会失败提醒。
const wchar_t* const kFieldLabels[] = {
    L"昵称", L"个性签名", L"性别", L"生日",
    L"所在地", L"邮箱", L"部门", L"职位"
};
const size_t kFieldLabelCount = sizeof(kFieldLabels) / sizeof(kFieldLabels[0]);

// 按给定 DPI 造一个与产品同款的 10pt 字体。产品侧走 DuiResMgr::GetFontByPointSize,
// 其 lfHeight = -MulDiv(pt, dpi, 72),这里照抄同一公式,好把 DPI 作为自变量注入。
//   dpi  目标 DPI(96=100%,120=125%,144=150%)。
//   pt   字号(磅)。
// 调用方负责 ::DeleteObject。
HFONT MakeLabelFont(int dpi, int pt)
{
    return ::CreateFontW(-::MulDiv(pt, dpi, 72), 0, 0, 0,
                         FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, L"微软雅黑");
}

// 用给定字体量一段文字的像素宽度。
//   hFont  字体句柄,不可为 NULL；text 待测文字；len 字符数。
int MeasureTextPixels(HFONT hFont, const wchar_t* text, int len)
{
    HDC hdc = ::GetDC(NULL);
    if (hdc == NULL)
    {
        return 0;
    }
    HGDIOBJ oldFont = ::SelectObject(hdc, hFont);
    SIZE sz = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text, len, &sz);
    ::SelectObject(hdc, oldFont);
    ::ReleaseDC(NULL, hdc);
    return (int)sz.cx;
}

} // anonymous namespace

// PW1~PW3 —— 三档常见缩放下,列宽都要放得下最长的字段名"个性签名"。
//
// 修复前这一列写死 60 像素:实测 120 DPI 下"个性签名"要 68 像素,于是最后一个字被
// DrawText 从中间硬裁(截图 snapshots/171401.png 就是这个现象)。这三条把"列宽必须
// 跟着字体走"钉死。
TEST_CASE("PW1 96 DPI(100%)下列宽放得下最长字段名", "[p2][profile][labelwidth]")
{
    HFONT hFont = MakeLabelFont(96, 10);
    REQUIRE(hFont != NULL);

    const int needed = MeasureTextPixels(hFont, L"个性签名", 4);
    const int width  = ComputeFieldLabelColumnWidth(hFont);
    ::DeleteObject(hFont);

    CHECK(width >= needed);
    CHECK(width >= kColumnWidthMin);
    CHECK(width <= kColumnWidthMax);
}

TEST_CASE("PW2 120 DPI(125%)下列宽放得下最长字段名", "[p2][profile][labelwidth]")
{
    // 这就是缺陷复现的那一档:needed 实测 68 > 写死的 60。
    HFONT hFont = MakeLabelFont(120, 10);
    REQUIRE(hFont != NULL);

    const int needed = MeasureTextPixels(hFont, L"个性签名", 4);
    const int width  = ComputeFieldLabelColumnWidth(hFont);
    ::DeleteObject(hFont);

    CHECK(needed > kColumnWidthMin);   // 钉死"旧的 60 确实不够"这个前提
    CHECK(width >= needed);
    CHECK(width <= kColumnWidthMax);
}

TEST_CASE("PW3 144 DPI(150%)下列宽放得下最长字段名", "[p2][profile][labelwidth]")
{
    // 150% 是列宽上限 88 仍能容下四个汉字的最高一档;再高就会被上限夹住,
    // 转由标签的 DT_END_ELLIPSIS 退化成省略号(见 PW5)。
    HFONT hFont = MakeLabelFont(144, 10);
    REQUIRE(hFont != NULL);

    const int needed = MeasureTextPixels(hFont, L"个性签名", 4);
    const int width  = ComputeFieldLabelColumnWidth(hFont);
    ::DeleteObject(hFont);

    CHECK(width >= needed);
    CHECK(width <= kColumnWidthMax);
}

TEST_CASE("PW4 八个字段名逐个都不会被裁", "[p2][profile][labelwidth]")
{
    // 所有行共用一个列宽,故必须按最宽的那个字段名取值。逐个断言而不是只测
    // "个性签名",是为了将来加了更长的字段名时这条会失败提醒。
    HFONT hFont = MakeLabelFont(120, 10);
    REQUIRE(hFont != NULL);

    const int width = ComputeFieldLabelColumnWidth(hFont);
    for (size_t i = 0; i < kFieldLabelCount; ++i)
    {
        const int needed = MeasureTextPixels(hFont, kFieldLabels[i],
                                             (int)::wcslen(kFieldLabels[i]));
        CHECK(width >= needed);
    }
    ::DeleteObject(hFont);
}

TEST_CASE("PW5 字体过大时列宽被上限夹住,不挤压右侧值列", "[p2][profile][labelwidth]")
{
    // 上限 88 的由来:客户区 420 - 左右 padding 各 20 = 380 可用,右侧值列固定 260、
    // 两条 gap 共 32,380 - 260 - 32 = 88。给一个夸张字号,断言不会突破这个上限。
    HFONT hFont = MakeLabelFont(144, 40);
    REQUIRE(hFont != NULL);

    const int width = ComputeFieldLabelColumnWidth(hFont);
    ::DeleteObject(hFont);

    CHECK(width == kColumnWidthMax);
}

TEST_CASE("PW6 DPI 越大列宽不减", "[p2][profile][labelwidth]")
{
    // 防止 MulDiv 的换算方向被写反 —— 写反了字会越放越小,测量值反而变窄,
    // 单看某一档还可能"碰巧通过"。
    HFONT f96  = MakeLabelFont(96, 10);
    HFONT f120 = MakeLabelFont(120, 10);
    HFONT f144 = MakeLabelFont(144, 10);
    REQUIRE(f96 != NULL);
    REQUIRE(f120 != NULL);
    REQUIRE(f144 != NULL);

    const int w96  = ComputeFieldLabelColumnWidth(f96);
    const int w120 = ComputeFieldLabelColumnWidth(f120);
    const int w144 = ComputeFieldLabelColumnWidth(f144);

    ::DeleteObject(f96);
    ::DeleteObject(f120);
    ::DeleteObject(f144);

    CHECK(w120 >= w96);
    CHECK(w144 >= w120);
}

TEST_CASE("PW7 字体为空时返回下限而不是 0", "[p2][profile][labelwidth]")
{
    // 取不到字体时整列宽度若返回 0,标签会整条消失。必须退回原设计值兜底。
    CHECK(ComputeFieldLabelColumnWidth(NULL) == kColumnWidthMin);
}
