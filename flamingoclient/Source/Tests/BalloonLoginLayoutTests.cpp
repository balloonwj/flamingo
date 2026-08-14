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
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEditHost.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiProgressBar.h"

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

TEST_CASE("L8 账号下拉框的内嵌输入框带占位文字", "[p1a][login]")
{
    // 账号框为空时应当像密码框一样给出提示文字，否则用户看到的是一个不知道
    // 该填什么的空框。占位文字设在 DuiComboBox 内嵌的那个 EDIT 宿主控件上
    // （下拉框本身没有转发 SetPlaceholder），故这里顺着 Children() 找过去。
    std::unique_ptr<DuiControl> root = BuildLoginBody();
    REQUIRE(root != nullptr);

    DuiComboBox* combo = dynamic_cast<DuiComboBox*>(root->FindCtrlById(kIdAccountCombo));
    REQUIRE(combo != nullptr);

    DuiEditHost* accountEdit = NULL;
    const std::vector<std::unique_ptr<DuiControl> >& children = combo->Children();
    for (size_t i = 0; i < children.size(); ++i)
    {
        DuiEditHost* candidate = dynamic_cast<DuiEditHost*>(children[i].get());
        if (candidate != NULL)
        {
            accountEdit = candidate;
            break;
        }
    }

    REQUIRE(accountEdit != NULL);
    CHECK_FALSE(accountEdit->GetPlaceholder().IsEmpty());
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

// ======================= 状态区（登录中 / 登录失败）的布局 =======================
//
// 这几条量的是「登录中」那块状态区（状态文字 + 不确定进度条）在真实布局下的位置。
// 起因是实机上看到进度条右侧一直顶到窗口边框，而同一个父容器里的登录按钮左右
// 都规规矩矩留着内边距。用真窗口去核对这种问题很不划算——要起进程、模拟点击、
// 抢前台焦点，任何一环不稳都会得出"看不出问题"的错误结论（实际排查时就连着栽了
// 四次）。控件树本身是纯逻辑，给它一个固定尺寸跑一遍布局就能把坐标读出来。

namespace {

// 登录窗客户区的尺寸，与 BalloonLoginDlg.cpp 里 ResizeClient 用的一致。
// 布局结果依赖它，所以两处要对得上。
const int kTestClientWidth  = 380;
const int kTestClientHeight = 562;

// 标题栏高度，与 BalloonLoginDlg.cpp 里 SetTitleBarHeight 传的值一致。
const int kTestTitleBarHeight = 28;

// <u>客户区内容真正拿到的高度</u>。DuiFrameWindow 的骨架是「标题栏 + 客户区内容」
// 上下两块，内容那块只占标题栏以下的部分。本文件早先按整个客户区高度布局，比实机
// 多给了 28 像素，于是实机上排不下的布局在测试里排得下 —— 2026-08-14 那次
// 「登录失败后标题栏按钮消失」就是这样被漏过去的。
const int kTestContentHeight = kTestClientHeight - kTestTitleBarHeight;

// 表单 vbox 的左右内边距，与 BalloonLoginLayout.cpp 里那句
// padding="24,14,24,20" 的第 1、3 个数一致。状态区是它的子控件，
// 因此左右都不该越过这条线。
const int kFormPaddingLeft  = 24;
const int kFormPaddingRight = 24;

// 按登录窗的真实客户区尺寸把控件树布一遍，返回根控件。
std::unique_ptr<DuiControl> LayoutLoginBody()
{
    std::unique_ptr<DuiControl> root = BuildLoginBody();
    if (root == nullptr)
        return root;

    RECT rc;
    rc.left   = 0;
    rc.top    = kTestTitleBarHeight;    // 客户区内容从标题栏下方开始
    rc.right  = kTestClientWidth;
    rc.bottom = kTestClientHeight;
    // 必须用 ForceLayout：SetRect 在矩形未变时会早早 return，
    // 而这里的控件树是刚建出来的、矩形还是空的，走 SetRect 排不出来
    root->ForceLayout(rc);
    return root;
}

// 取子树里最靠下的那条边（只看可见控件）。用来判断内容有没有溢出容器底边。
int MaxVisibleBottom(const DuiControl* node)
{
    if (node == NULL || !node->IsVisible())
        return INT_MIN;

    int bottom = node->GetRect().bottom;
    const std::vector<std::unique_ptr<DuiControl> >& children = node->Children();
    for (size_t i = 0; i < children.size(); ++i)
    {
        const int childBottom = MaxVisibleBottom(children[i].get());
        if (childBottom > bottom)
            bottom = childBottom;
    }
    return bottom;
}

} // anonymous namespace

TEST_CASE("LSA1 状态区三个控件都能按 id 取到", "[login][statusarea]")
{
    std::unique_ptr<DuiControl> root = LayoutLoginBody();
    REQUIRE(root != nullptr);

    CHECK(root->FindCtrlById(kIdStatusArea) != nullptr);
    CHECK(root->FindCtrlById(kIdStatusText) != nullptr);
    CHECK(root->FindCtrlById(kIdStatusProgress) != nullptr);
}

TEST_CASE("LSA2 状态区不得越过表单的左右内边距", "[login][statusarea]")
{
    std::unique_ptr<DuiControl> root = LayoutLoginBody();
    REQUIRE(root != nullptr);

    DuiControl* area = root->FindCtrlById(kIdStatusArea);
    REQUIRE(area != nullptr);

    const RECT& rc = area->GetRect();
    INFO("status area rect = (" << rc.left << "," << rc.top << ")-("
         << rc.right << "," << rc.bottom << "), client width = " << kTestClientWidth);

    CHECK(rc.left >= kFormPaddingLeft);
    CHECK(rc.right <= kTestClientWidth - kFormPaddingRight);
}

TEST_CASE("LSA3 进度条与登录按钮左右对齐", "[login][statusarea]")
{
    std::unique_ptr<DuiControl> root = LayoutLoginBody();
    REQUIRE(root != nullptr);

    DuiControl* button = root->FindCtrlById(kIdLoginButton);
    DuiControl* bar    = root->FindCtrlById(kIdStatusProgress);
    REQUIRE(button != nullptr);
    REQUIRE(bar != nullptr);

    const RECT& rcButton = button->GetRect();
    const RECT& rcBar    = bar->GetRect();
    INFO("login button = (" << rcButton.left << "," << rcButton.right << "), "
         << "progress bar = (" << rcBar.left << "," << rcBar.right << ")");

    // 两者在同一个 vbox 里、都没有设固定宽度，所以左右边界应当一致。
    // 实机上看到的"进度条比按钮宽、右侧顶到窗口边框"若成立，这两条会失败。
    CHECK(rcBar.left == rcButton.left);
    CHECK(rcBar.right == rcButton.right);
}

TEST_CASE("LSA7 状态区显示时，内容排得下、不溢出客户区底边", "[login][statusarea]")
{
    // 状态区一显示，表单就多出一块固定高度。它若把客户区撑破，多出来的部分会挤掉
    // 别人 —— 实机上表现为登录失败后<u>标题栏的最小化 / 关闭按钮消失</u>。
    // 这条用例按客户区内容真正拿到的高度（标题栏以下）布局，把这种溢出钉在测试里。
    std::unique_ptr<DuiControl> root = LayoutLoginBody();
    REQUIRE(root != nullptr);

    DuiControl* status = root->FindCtrlById(kIdStatusArea);
    REQUIRE(status != nullptr);
    REQUIRE(status->IsVisible());   // 布局默认建出来就是可见的

    const int contentBottom = root->GetRect().bottom;
    const int deepestBottom = MaxVisibleBottom(root.get());
    INFO("content bottom = " << contentBottom << ", deepest child bottom = " << deepestBottom
         << " (overflow = " << (deepestBottom - contentBottom) << ")");
    CHECK(deepestBottom <= contentBottom);
}

TEST_CASE("LSA8 状态区隐藏时，内容同样排得下", "[login][statusarea]")
{
    // 空闲态（没在登录、也没有失败提示）是用户看得最多的一屏，同样不许溢出。
    std::unique_ptr<DuiControl> root = LayoutLoginBody();
    REQUIRE(root != nullptr);

    DuiControl* status = root->FindCtrlById(kIdStatusArea);
    REQUIRE(status != nullptr);
    status->SetVisible(false);

    RECT rc = root->GetRect();
    root->ForceLayout(rc);   // 显隐变化后必须重排，理由见 LSA5

    const int contentBottom = root->GetRect().bottom;
    const int deepestBottom = MaxVisibleBottom(root.get());
    INFO("content bottom = " << contentBottom << ", deepest child bottom = " << deepestBottom
         << " (slack = " << (contentBottom - deepestBottom) << ")");
    CHECK(deepestBottom <= contentBottom);
}

TEST_CASE("LSA6 进度条不显示百分比，且高度够画得清楚", "[login][statusarea]")
{
    // DuiProgressBar 默认在条上画 "NN%"。marquee 模式下没有确定的进度值，那个
    // 数字既无意义、条又矮，实机上挤成一团看不清（2026-08-14 用户反馈）。
    std::unique_ptr<DuiControl> root = LayoutLoginBody();
    REQUIRE(root != nullptr);

    DuiProgressBar* bar =
        dynamic_cast<DuiProgressBar*>(root->FindCtrlById(kIdStatusProgress));
    REQUIRE(bar != nullptr);

    CHECK_FALSE(bar->GetShowPercent());

    // 高度下限取 10 像素：低于这个高度，圆角与滚动块都糊成一条线。
    const int kMinBarHeight = 10;
    const RECT& rcBar = bar->GetRect();
    const int barHeight = rcBar.bottom - rcBar.top;
    INFO("progress bar height = " << barHeight);
    CHECK(barHeight >= kMinBarHeight);
}

TEST_CASE("LSA4 状态文字与进度条同宽且不重叠", "[login][statusarea]")
{
    std::unique_ptr<DuiControl> root = LayoutLoginBody();
    REQUIRE(root != nullptr);

    DuiControl* text = root->FindCtrlById(kIdStatusText);
    DuiControl* bar  = root->FindCtrlById(kIdStatusProgress);
    REQUIRE(text != nullptr);
    REQUIRE(bar != nullptr);

    const RECT& rcText = text->GetRect();
    const RECT& rcBar  = bar->GetRect();
    INFO("text = (" << rcText.left << "," << rcText.top << ")-(" << rcText.right << "," << rcText.bottom
         << "), bar = (" << rcBar.left << "," << rcBar.top << ")-(" << rcBar.right << "," << rcBar.bottom << ")");

    CHECK(rcText.left == rcBar.left);
    CHECK(rcText.right == rcBar.right);
    // 进度条在文字下方，两者不能压在一起
    CHECK(rcBar.top >= rcText.bottom);
}

TEST_CASE("LSA5 状态区隐藏后再显示，重排过才拿得到正确宽度", "[login][statusarea]")
{
    std::unique_ptr<DuiControl> root = LayoutLoginBody();
    REQUIRE(root != nullptr);

    DuiControl* area   = root->FindCtrlById(kIdStatusArea);
    DuiControl* bar    = root->FindCtrlById(kIdStatusProgress);
    DuiControl* button = root->FindCtrlById(kIdLoginButton);
    REQUIRE(area != nullptr);
    REQUIRE(bar != nullptr);
    REQUIRE(button != nullptr);

    // 复现实机的时序：登录窗建好后先把状态区藏起来（InitLoginUi 就是这么做的），
    // 藏着的时候发生一次重排（窗口尺寸变化、内容替换都会触发），点登录时再显示。
    //
    // 这一步是本组用例最初漏掉的：SetVisible 不触发重新布局，而竖直布局排版时会
    // 跳过不可见的子控件，于是状态区在隐藏期间被排成了一份过时的矩形，显示回来
    // 也不会重算。前面那几条用例因为状态区从未隐藏过，一直是绿的，实机上却能看到
    // 进度条右侧顶到窗口边框。
    area->SetVisible(false);

    RECT rc;
    rc.left   = 0;
    rc.top    = 0;
    rc.right  = kTestClientWidth;
    rc.bottom = kTestClientHeight;
    root->ForceLayout(rc);

    area->SetVisible(true);
    // 显示回来之后必须重排一次，这正是 CBalloonLoginDlg::RelayoutClientContent 做的事
    root->ForceLayout(rc);

    const RECT& rcBar    = bar->GetRect();
    const RECT& rcButton = button->GetRect();
    INFO("after hide/show: bar = (" << rcBar.left << "," << rcBar.right << "), "
         << "button = (" << rcButton.left << "," << rcButton.right << "), "
         << "client width = " << kTestClientWidth);

    CHECK(rcBar.left == rcButton.left);
    CHECK(rcBar.right == rcButton.right);
    CHECK(rcBar.right <= kTestClientWidth - kFormPaddingRight);
}
