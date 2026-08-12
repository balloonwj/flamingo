/**
 *  BalloonLoginLayoutTests.cpp
 *
 *  P1a 测试：验证 BalloonLoginLayout 的种子函数 BuildLoginBody() 产出的
 *  balloonui 控件树符合"控件点名契约" —— 登录界面所有需要被对话框代码
 *  引用的控件都能按既定数字 id 找到、类型正确、关键属性正确。
 *
 *  覆盖用例（与需求确认一致）：
 *    L1-L7   登录布局契约
 *    C1      CtrlId enum 自洽
 *    V1-V5   登录输入校验
 *    A1-A13  Banner 头像底色 / 显示名 / 下拉框账号取法
 *    PR1-PR7 注册成功后回填登录表单
 *
 *  用 Catch2 v3（amalgamated 形态）作测试框架。本工程不创建任何窗口，
 *  纯粹在内存里 build 控件树 + 断言，无需消息循环 / HWND。
 *
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用
 *  （经 stdafx.h 的 extern 声明），不可重复定义。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../BalloonAuth/BalloonLoginLayout.h"
#include "../BalloonMain/AvatarPalette.h"
#include "../../balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../balloonui/balloonui/Controls/Input/DuiEditHost.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiButton.h"

#include <set>

using namespace balloonwjui;
using namespace balloonauth;

namespace {

// 递归收集子树里所有<u>非 0</u> 的 ctrlId。0 = 未命名（Banner 占位 /
// spacer），不属于"控件点名契约"，跳过。
void CollectIds(const DuiControl* node, std::vector<UINT>& out)
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
        CollectIds(child.get(), out);
    }
}

// 判断一组 id 里是否存在重复值。
bool HasDuplicateId(std::vector<UINT> ids)
{
    std::sort(ids.begin(), ids.end());
    return std::adjacent_find(ids.begin(), ids.end()) != ids.end();
}

} // anonymous namespace

// =====================================================================
// L. 登录布局契约
// =====================================================================

TEST_CASE("L1 BuildLoginBody 返回非空控件树", "[p1a][login]")
{
    auto root = BuildLoginBody();
    REQUIRE(root != nullptr);
}

TEST_CASE("L2 登录页含可编辑账号下拉框", "[p1a][login]")
{
    auto root = BuildLoginBody();
    REQUIRE(root != nullptr);

    DuiControl* account = root->FindCtrlById(kIdAccountCombo);
    REQUIRE(account != nullptr);

    auto* combo = dynamic_cast<DuiComboBox*>(account);
    REQUIRE(combo != nullptr);
    CHECK(combo->IsEditable());
}

TEST_CASE("L3 登录页含密码输入框且为密码模式", "[p1a][login]")
{
    auto root = BuildLoginBody();
    REQUIRE(root != nullptr);

    DuiControl* password = root->FindCtrlById(kIdPasswordEdit);
    REQUIRE(password != nullptr);

    auto* edit = dynamic_cast<DuiEditHost*>(password);
    REQUIRE(edit != nullptr);
    CHECK(edit->IsPassword());
    CHECK_FALSE(edit->GetPlaceholder().IsEmpty());
}

TEST_CASE("L4 登录页含记住密码 / 自动登录复选框", "[p1a][login]")
{
    auto root = BuildLoginBody();
    REQUIRE(root != nullptr);

    auto* remember = dynamic_cast<DuiButton*>(root->FindCtrlById(kIdRememberPwd));
    REQUIRE(remember != nullptr);
    CHECK(remember->GetButtonType() == DuiButton::StyleCheckbox);

    auto* autoLogin = dynamic_cast<DuiButton*>(root->FindCtrlById(kIdAutoLogin));
    REQUIRE(autoLogin != nullptr);
    CHECK(autoLogin->GetButtonType() == DuiButton::StyleCheckbox);
}

TEST_CASE("L5 登录页含带文字的登录按钮", "[p1a][login]")
{
    auto root = BuildLoginBody();
    REQUIRE(root != nullptr);

    auto* btn = dynamic_cast<DuiButton*>(root->FindCtrlById(kIdLoginButton));
    REQUIRE(btn != nullptr);
    CHECK_FALSE(btn->GetText().IsEmpty());
}

TEST_CASE("L6 登录页含注册账号 / 网络设置链接", "[p1a][login]")
{
    auto root = BuildLoginBody();
    REQUIRE(root != nullptr);

    CHECK(root->FindCtrlById(kIdLinkRegister) != nullptr);
    CHECK(root->FindCtrlById(kIdLinkNetwork)  != nullptr);
}

TEST_CASE("L7 登录布局所有控件 id 互不重复", "[p1a][login]")
{
    auto root = BuildLoginBody();
    REQUIRE(root != nullptr);

    std::vector<UINT> ids;
    CollectIds(root.get(), ids);
    CHECK_FALSE(ids.empty());
    CHECK_FALSE(HasDuplicateId(ids));
}

// =====================================================================
// C. CtrlId enum 自洽
// =====================================================================

TEST_CASE("C1 登录页 CtrlId 各枚举值互不重复", "[p1a][login][ids]")
{
    const std::vector<UINT> allIds =
    {
        kIdAccountCombo, kIdPasswordEdit,
        kIdRememberPwd, kIdAutoLogin,
        kIdLoginButton,
        kIdLinkRegister, kIdLinkNetwork, kIdLinkForgotPwd,
    };
    CHECK_FALSE(HasDuplicateId(allIds));
}

// =====================================================================
// V. 登录输入校验（P1b）
// =====================================================================

TEST_CASE("V1 账号密码均非空 → kLoginInputOk", "[p1b][login]")
{
    CHECK(ValidateLoginInput(_T("88234561"), _T("flamingo")) == kLoginInputOk);
}

TEST_CASE("V2 账号为空 → kLoginInputNoAccount", "[p1b][login]")
{
    CHECK(ValidateLoginInput(_T(""), _T("flamingo")) == kLoginInputNoAccount);
}

TEST_CASE("V3 密码为空 → kLoginInputNoPassword", "[p1b][login]")
{
    CHECK(ValidateLoginInput(_T("88234561"), _T("")) == kLoginInputNoPassword);
}

TEST_CASE("V4 账号纯空格 → kLoginInputNoAccount", "[p1b][login]")
{
    CHECK(ValidateLoginInput(_T("   "), _T("flamingo")) == kLoginInputNoAccount);
}

TEST_CASE("V5 账号非空、密码纯空白 → kLoginInputNoPassword", "[p1b][login]")
{
    CHECK(ValidateLoginInput(_T("88234561"), _T("  \t ")) == kLoginInputNoPassword);
}

// =====================================================================
// A. 登录窗 Banner 头像底色（切换账号头像跟着变）
//
// 纯函数 ChooseBannerAvatarColor / AvatarColorForText 的行为断言：
//   · 未登录 / 空账号 → 默认橙；
//   · 已登录账号 → 取自调色板、同账号恒定、不同账号有区分度。
// =====================================================================

namespace {

// 调色板第 0 个 = 默认橙 RGB(0xFF,0x7A,0x45)。
const COLORREF kDefaultOrange = RGB(0xFF, 0x7A, 0x45);

// 收集整张调色板，供"取色必落在调色板内"的断言用。
std::set<COLORREF> AllPaletteColors()
{
    std::set<COLORREF> colors;
    for (int i = 0; i < balloonmain::kAvatarPaletteSize; ++i)
    {
        colors.insert(balloonmain::AvatarColorByIndex(i));
    }
    return colors;
}

} // anonymous namespace

TEST_CASE("A1 未登录账号 → 默认橙色", "[login][avatar]")
{
    CHECK(ChooseBannerAvatarColor(_T("test99990"), false) == kDefaultOrange);
}

TEST_CASE("A2 账号为空（即便标记已登录）→ 默认橙色", "[login][avatar]")
{
    CHECK(ChooseBannerAvatarColor(_T(""), true) == kDefaultOrange);
}

TEST_CASE("A3 已登录账号 → 颜色取自调色板", "[login][avatar]")
{
    std::set<COLORREF> palette = AllPaletteColors();
    COLORREF c = ChooseBannerAvatarColor(_T("alice123"), true);
    CHECK(palette.find(c) != palette.end());
}

TEST_CASE("A4 同一已登录账号多次取色恒定相等", "[login][avatar]")
{
    COLORREF c1 = ChooseBannerAvatarColor(_T("balloonwj"), true);
    COLORREF c2 = ChooseBannerAvatarColor(_T("balloonwj"), true);
    COLORREF c3 = ChooseBannerAvatarColor(_T("balloonwj"), true);
    CHECK(c1 == c2);
    CHECK(c2 == c3);
}

TEST_CASE("A5 一组不同已登录账号至少取到两种颜色（有区分度）", "[login][avatar]")
{
    const TCHAR* accounts[] =
    {
        _T("alice"), _T("bob"),   _T("carol"), _T("dave"),
        _T("erin"),  _T("frank"), _T("grace"), _T("heidi"),
        _T("88234561"), _T("test99990"),
    };
    std::set<COLORREF> distinct;
    for (int i = 0; i < (int)(sizeof(accounts) / sizeof(accounts[0])); ++i)
    {
        distinct.insert(ChooseBannerAvatarColor(accounts[i], true));
    }
    CHECK(distinct.size() >= 2);
}

TEST_CASE("A6 AvatarColorForText / AvatarColorForUser 取自同一调色板", "[login][avatar]")
{
    std::set<COLORREF> palette = AllPaletteColors();

    // 空串 → 默认橙（调色板第 0 个）。
    CHECK(balloonmain::AvatarColorForText(_T("")) == kDefaultOrange);
    CHECK(balloonmain::AvatarColorForText(NULL)   == kDefaultOrange);

    // 文本取色落在调色板内。
    CHECK(palette.find(balloonmain::AvatarColorForText(_T("anyaccount"))) != palette.end());

    // userid 取色落在调色板内，且负数 userid 安全（不越界、仍在调色板内）。
    CHECK(palette.find(balloonmain::AvatarColorForUser(0))    != palette.end());
    CHECK(palette.find(balloonmain::AvatarColorForUser(7))    != palette.end());
    CHECK(palette.find(balloonmain::AvatarColorForUser(123))  != palette.end());
    CHECK(palette.find(balloonmain::AvatarColorForUser(-5))   != palette.end());
}

// =====================================================================
// 纯函数 ChooseBannerDisplayName 的行为断言：
//   · 历史登录列表里存过姓名 → 头像下方显示姓名；
//   · 没有姓名（没登录过 / 老版本存的账号 / 服务器昵称为空）→ 显示账号。
// =====================================================================

TEST_CASE("A7 登录过的账号有姓名时显示姓名", "[login][displayname]")
{
    CHECK(ChooseBannerDisplayName(_T("test99990"), _T("张三")) == CString(_T("张三")));
}

TEST_CASE("A8 姓名为空时退回显示账号", "[login][displayname]")
{
    // 没登录过 / 老版本客户端存下的账号 —— 姓名字段是空串。
    CHECK(ChooseBannerDisplayName(_T("test99990"), _T("")) == CString(_T("test99990")));

    // 服务器给回来的昵称全是空白 —— 同样视为没有姓名。
    CHECK(ChooseBannerDisplayName(_T("test99990"), _T("   ")) == CString(_T("test99990")));
}

TEST_CASE("A9 姓名两端的空白被去掉后再显示", "[login][displayname]")
{
    CHECK(ChooseBannerDisplayName(_T("test99990"), _T("  李四  ")) == CString(_T("李四")));
}

TEST_CASE("A10 账号与姓名都为空时显示空串（头像画通用人形占位）", "[login][displayname]")
{
    CHECK(ChooseBannerDisplayName(_T(""), _T("")).IsEmpty());
}

// =====================================================================
// 纯函数 ShouldReadAccountFromComboItem 的行为断言。
//
// 这组用例锁的是一个真实缺陷的修复：DuiComboBox 从下拉列表选中一项时，
// 先同步发出 DUIN_VALUECHANGED 通知、之后才把选中项文本写进内嵌 EDIT，
// 于是通知处理函数里 GetText() 读到的是切换<u>前</u>的旧账号。表现为
// "账号框已切到 MY0127，头像下方仍是 MY0308 的姓名，回填的密码与勾选项
// 也还是 MY0308 的"。修复办法是改用通知带来的项索引取账号，本函数即是
// 判定"该按索引取、还是该读输入框"的那一步。
// =====================================================================

TEST_CASE("A11 从下拉列表选中一项时按项索引取账号", "[login][comboaccount]")
{
    // 列表有 3 项，选中第 0 / 1 / 2 项都应当按索引去取。
    CHECK(ShouldReadAccountFromComboItem(0, 3));
    CHECK(ShouldReadAccountFromComboItem(1, 3));
    CHECK(ShouldReadAccountFromComboItem(2, 3));
}

TEST_CASE("A12 手工输入（extra 为 -1）时读输入框文本", "[login][comboaccount]")
{
    // editable 模式手输入、文本不完全匹配任何一项 —— 此时 EDIT 里就是最新
    // 文本，没有索引可用。
    CHECK_FALSE(ShouldReadAccountFromComboItem(kComboExtraNoItem, 3));

    // 非通知触发的调用点（如启动预填后刷新 Banner）同样传 kComboExtraNoItem。
    CHECK_FALSE(ShouldReadAccountFromComboItem(kComboExtraNoItem, 0));
}

TEST_CASE("A13 索引越界时退回读输入框文本", "[login][comboaccount]")
{
    // 索引正好等于项数、或超出项数 —— 都不能拿去 GetItemText。
    CHECK_FALSE(ShouldReadAccountFromComboItem(3, 3));
    CHECK_FALSE(ShouldReadAccountFromComboItem(99, 3));

    // 空列表 —— 任何非负索引都是越界。
    CHECK_FALSE(ShouldReadAccountFromComboItem(0, 0));

    // -1 以外的负数（不该出现，但不能因此取到越界项）。
    CHECK_FALSE(ShouldReadAccountFromComboItem(-7, 3));
}

// =====================================================================
// PR. 注册成功后回填登录表单（纯函数 MakePostRegisterFill）
//
// 这组用例锁的同样是一个真实缺陷的修复：注册成功后登录窗只回填了账号、
// 没有回填密码，密码框里留着的是登录窗打开时预填的上一个账号的密码（本机
// 无历史登录时则是兜底的测试账号密码）。表现为"账号是刚注册的那个、密码
// 却不是注册时填的那个，点登录报密码错误"。
// =====================================================================

TEST_CASE("PR1 密码框回填的是注册时设置的密码", "[login][postregister]")
{
    const PostRegisterFill fill =
        MakePostRegisterFill(_T("88234561"), _T("myNewPwd123"));

    REQUIRE(fill.m_shouldFill);
    // 缺陷复现点：这里若拿到的是别的密码（旧账号的 / 兜底测试密码），即为回归。
    CHECK(fill.m_password == CString(_T("myNewPwd123")));
}

TEST_CASE("PR2 账号框回填服务端下发的账号", "[login][postregister]")
{
    const PostRegisterFill fill =
        MakePostRegisterFill(_T("88234561"), _T("myNewPwd123"));

    REQUIRE(fill.m_shouldFill);
    CHECK(fill.m_account == CString(_T("88234561")));
}

TEST_CASE("PR3 回填后记住密码 / 自动登录均为未勾选", "[login][postregister]")
{
    // 用户此刻还没表达过"记住密码"的意愿，且要与同时写进历史账号列表的
    // 那条记录（空密码 + 两项皆否）保持一致。
    const PostRegisterFill fill =
        MakePostRegisterFill(_T("88234561"), _T("myNewPwd123"));

    REQUIRE(fill.m_shouldFill);
    CHECK_FALSE(fill.m_rememberPwd);
    CHECK_FALSE(fill.m_autoLogin);
}

TEST_CASE("PR4 账号为空时整体不回填", "[login][postregister]")
{
    // 注册成功的应答里理论上不会没有账号；真出现了也不能只把密码填进去 ——
    // 界面保持原样，由调用方记错误日志。
    const PostRegisterFill fill = MakePostRegisterFill(_T(""), _T("myNewPwd123"));

    CHECK_FALSE(fill.m_shouldFill);
}

TEST_CASE("PR5 账号为纯空白时同样不回填", "[login][postregister]")
{
    CHECK_FALSE(MakePostRegisterFill(_T("   "), _T("myNewPwd123")).m_shouldFill);
    CHECK_FALSE(MakePostRegisterFill(_T(" \t "), _T("myNewPwd123")).m_shouldFill);
}

TEST_CASE("PR6 密码原样回填，首尾空格不被去掉", "[login][postregister]")
{
    // 空格在密码里是有效字符，若照账号 / 公司名的口径 Trim 一道，回填出来的
    // 密码与用户注册时输入的就不是同一个了。
    const PostRegisterFill fill =
        MakePostRegisterFill(_T("88234561"), _T("  pwd with space  "));

    REQUIRE(fill.m_shouldFill);
    CHECK(fill.m_password == CString(_T("  pwd with space  ")));
}

TEST_CASE("PR7 密码为空时仍回填并清空密码框", "[login][postregister]")
{
    // 注册窗的表单校验不放空密码过，正常走不到这里；万一走到了，宁可让密码
    // 框空着，也不能把上一个账号的密码留在框里冒充"刚设的密码"。
    const PostRegisterFill fill = MakePostRegisterFill(_T("88234561"), _T(""));

    REQUIRE(fill.m_shouldFill);
    CHECK(fill.m_account == CString(_T("88234561")));
    CHECK(fill.m_password.IsEmpty());
}

// =====================================================================
// MAP —— 下拉浮层项下标 → m_items 下标的映射
//
// 这组用例锁的是"删错账号"这条风险：账号下拉开了增量搜索过滤时，浮层里
// 只显示命中的那几项，它报回来的下标是"第几个命中项"而不是"m_items 的第
// 几项"。不映射的话，用户敲几个字过滤之后点某一行的删除叉，删掉的会是另
// 一个账号 —— 比崩溃更糟，因为数据没了而用户不知道。
//
// 映射逻辑做成了 DuiComboBox 的 public static 纯函数，正好脱离控件状态直接测。
// =====================================================================

TEST_CASE("MAP1 未过滤时浮层下标与 m_items 下标 1:1", "[combo][map]")
{
    std::vector<int> none;
    CHECK(DuiComboBox::MapPopupIndexWithFilter(0, none) == 0);
    CHECK(DuiComboBox::MapPopupIndexWithFilter(3, none) == 3);
}

TEST_CASE("MAP2 过滤激活时浮层下标映射回真实下标", "[combo][map]")
{
    // 过滤命中的是 m_items 的第 0、1、4 项。
    std::vector<int> filtered;
    filtered.push_back(0);
    filtered.push_back(1);
    filtered.push_back(4);

    CHECK(DuiComboBox::MapPopupIndexWithFilter(0, filtered) == 0);
    CHECK(DuiComboBox::MapPopupIndexWithFilter(1, filtered) == 1);
    // 关键的一条：浮层第 3 行是 m_items 的第 5 项，不是第 3 项。
    CHECK(DuiComboBox::MapPopupIndexWithFilter(2, filtered) == 4);
}

TEST_CASE("MAP3 越出映射表范围时原样返回，由调用方判越界", "[combo][map]")
{
    std::vector<int> filtered;
    filtered.push_back(2);
    filtered.push_back(7);

    CHECK(DuiComboBox::MapPopupIndexWithFilter(-1, filtered) == -1);
    CHECK(DuiComboBox::MapPopupIndexWithFilter(2, filtered) == 2);
    CHECK(DuiComboBox::MapPopupIndexWithFilter(99, filtered) == 99);
}
