/**
 *  flamingoAdmin —— 登录界面实现。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Screens/LoginScreen.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/Widgets.h"
#include "Util/AppConfig.h"

#include "Controls/Layout/DuiLayout.h"
#include "Controls/Input/DuiEdit.h"
#include "Controls/Basic/DuiButton.h"

using namespace balloonwjui;

namespace flamingoadmin {

// 本文件在 namespace flamingoadmin 内，theme:: 直接解析到 flamingoadmin::theme。
using Hint = balloonwjui::DuiLayout::Hint;

namespace {

// ---- 登录界面布局常量（96 dpi 逻辑像素）----
const int kBrandWidth   = 560;   // 左侧品牌面板宽度
const int kFormWidth    = 420;   // 右侧表单列宽度
const int kFieldEditH   = 40;    // 输入框高度
const int kButtonHeight = 42;    // 登录按钮高度
const int kRememberRowH = 22;    // 「记住密码」复选框行高
// 右侧表单列总高度：标题 34 + 副标题 20 + 账户 64 + 密码 64 + 记住密码 22
// + 登录按钮 42 + TLS 脚注 44 + 网络设置行 28 + 7 个间距 ×14 = 416。
// 加减字段时这里必须同步改，否则表单会按错误的高度居中、末尾几行被裁掉。
const int kFormHeight   = 416;

// 左侧品牌面板渐变三停靠色（对应设计稿 #ff5a44 → #d93a8d → #7a3d99）。
const COLORREF kBrandC0 = RGB(0xff, 0x5a, 0x44);
const COLORREF kBrandC1 = RGB(0xd9, 0x3a, 0x8d);
const COLORREF kBrandC2 = RGB(0x7a, 0x3d, 0x99);

// 品牌面板上的浅色文字（在彩色渐变上代替「半透明白」）。
const COLORREF kBrandTextFaint = RGB(0xff, 0xdd, 0xd6);

// 构建左侧一条「卖点」：标题（白、半粗）+ 说明（浅色）两行。
std::unique_ptr<DuiControl> MakeFeature(LPCTSTR title, LPCTSTR desc)
{
    auto box = std::make_unique<DuiVBox>();
    box->SetGap(2);
    box->AddChild(ui::MakeLabel(title, theme::kFontSizeNormal, true, theme::kInk0),
                  Hint().Fixed(18));
    box->AddChild(ui::MakeLabel(desc, theme::kFontSizeSmall, false, kBrandTextFaint),
                  Hint().Fixed(16));
    return box;
}

// 构建左侧品牌渐变面板。
std::unique_ptr<DuiControl> BuildBrandPanel()
{
    auto panel = std::make_unique<ui::GradientPanel>();
    panel->SetGradient(kBrandC0, kBrandC1, kBrandC2);
    panel->SetPadding(44, 40, 44, 36);
    panel->SetGap(0);

    // 顶部 logo 行：徽标 + 名称两行。
    auto logoRow = std::make_unique<DuiHBox>();
    logoRow->SetGap(12);
    auto logo = std::make_unique<ui::LogoBadge>();
    logo->SetGlyphPointSize(theme::kFontSizeHeading);
    logoRow->AddChild(std::move(logo), Hint().Fixed(42, 42).AlignC(DuiLayout::AlignCenter));
    auto logoTitles = std::make_unique<DuiVBox>();
    logoTitles->AddChild(ui::MakeLabel(_T("FLAMINGO IM"), theme::kFontSizeSmall, true,
                                       kBrandTextFaint), Hint().Fixed(16));
    logoTitles->AddChild(ui::MakeLabel(_T("Enterprise Admin"), theme::kFontSizeTitle, true,
                                       theme::kInk0), Hint().Fixed(22));
    logoRow->AddChild(std::move(logoTitles),
                      Hint().Weight(1).Fixed(-1, 40).AlignC(DuiLayout::AlignCenter));
    panel->AddChild(std::move(logoRow), Hint().Fixed(42));

    // 中部 hero：大标题 + 说明 + 三条卖点，整体垂直居中。
    auto hero = std::make_unique<DuiVBox>();
    hero->SetGap(14);
    hero->AddChild(std::make_unique<DuiControl>(), Hint().Weight(1));   // 上弹性空白

    auto heroTitle = ui::MakeLabel(_T("统一的企业 IM\n管理控制台"),
                                   theme::kFontSizeHeading + 6, true, theme::kInk0,
                                   DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
    heroTitle->SetWordWrap(true);
    hero->AddChild(std::move(heroTitle), Hint().Fixed(76));

    auto heroDesc = ui::MakeLabel(
        _T("管理企业入驻、组织架构、员工权限与平台公告，所有操作均纳入审计。"),
        theme::kFontSizeNormal, false, kBrandTextFaint,
        DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
    heroDesc->SetWordWrap(true);
    hero->AddChild(std::move(heroDesc), Hint().Fixed(44));

    hero->AddChild(MakeFeature(_T("多租户企业接入"),
                               _T("为不同企业建立独立的组织与权限边界")),
                   Hint().Fixed(38));
    hero->AddChild(MakeFeature(_T("可视化组织树"),
                               _T("部门与员工任意层级挂载、重排")),
                   Hint().Fixed(38));
    hero->AddChild(MakeFeature(_T("全量审计日志"),
                               _T("所有管理动作留痕，可导出、可追溯")),
                   Hint().Fixed(38));

    hero->AddChild(std::make_unique<DuiControl>(), Hint().Weight(1));   // 下弹性空白
    panel->AddChild(std::move(hero), Hint().Weight(1));

    // 底部版本信息。
    panel->AddChild(ui::MakeLabel(_T("v2.0.0 · 内部构建 2026.05.18"),
                                  theme::kFontSizeSmall, false, kBrandTextFaint),
                    Hint().Fixed(18));

    return panel;
}

// 构建一个表单字段：标签 + 输入框。
//   editId：输入框控件 ID；password：是否密码框。
std::unique_ptr<DuiControl> MakeField(LPCTSTR label, int editId,
                                      LPCTSTR placeholder, bool password)
{
    auto field = std::make_unique<DuiVBox>();
    field->SetGap(6);
    field->AddChild(ui::MakeLabel(label, theme::kFontSizeNormal, false, theme::kInk500),
                    Hint().Fixed(18));

    auto edit = std::make_unique<DuiEdit>();
    edit->SetCtrlId(editId);
    if (password)
    {
        edit->SetPassword(true);          // 密码模式，字符显示为圆点
        edit->SetShowEyeToggle(true);     // 显示「眼睛」显隐切换
    }
    edit->SetPlaceholder(placeholder);
    edit->SetShowBorder(true);
    edit->SetBgColor(theme::kInk0);
    // 单行输入框的文字垂直居中是 DuiEdit 的默认行为（见 SetVerticalCenter），
    // 框高 kFieldEditH（40px）高于一行时文字 / 光标自动竖直居中，无需在此显式开启。
    field->AddChild(std::move(edit), Hint().Fixed(kFieldEditH));

    return field;
}

// 构建右侧登录表单面板。
std::unique_ptr<DuiControl> BuildFormPanel()
{
    auto side = std::make_unique<ui::SolidVBox>();
    side->SetFillColor(theme::kInk25);

    side->AddChild(std::make_unique<DuiControl>(), Hint().Weight(1));   // 上弹性空白

    // 固定宽度、水平居中的表单列。
    auto form = std::make_unique<DuiVBox>();
    form->SetGap(14);

    form->AddChild(ui::MakeLabel(_T("管理员登录"),
                                 theme::kFontSizeHeading + 4, true, theme::kInk700),
                   Hint().Fixed(34));
    form->AddChild(ui::MakeLabel(_T("使用平台分配的管理员账户登录控制台"),
                                 theme::kFontSizeNormal, false, theme::kInk400),
                   Hint().Fixed(20));
    form->AddChild(MakeField(_T("账户"), kIdLoginAccount, _T("admin / 邮箱"), false),
                   Hint().Fixed(18 + 6 + kFieldEditH));
    form->AddChild(MakeField(_T("密码"), kIdLoginPassword, _T("请输入密码"), true),
                   Hint().Fixed(18 + 6 + kFieldEditH));

    // 「记住密码」：勾上后登录成功时把账号与口令写进 exe 同目录的 flamingoAdmin.ini，
    // 口令先经 DPAPI 加密（见 Util/DpapiSecret.h），下次打开自动回填。
    // 初始勾选状态在 AdminFrame 进入登录页时按 ini 里存的值回填，这里只建控件。
    auto rememberBox = std::make_unique<balloonwjui::DuiButton>();
    rememberBox->SetButtonType(balloonwjui::DuiButton::StyleCheckbox);
    // Variant 必须选一个"透明"的（Ghost / Outlined / Text），否则 DuiButton 会按
    // 按钮那套浅色填充 + 边框来画，勾选框行上就多出一个跟输入框一样宽的方框。
    rememberBox->SetVariant(balloonwjui::DuiButton::Variant::Text);
    rememberBox->SetCtrlId(kIdLoginRememberPwd);
    rememberBox->SetText(_T("记住密码"));
    rememberBox->SetTextPointSize(theme::kFontSizeSmall);
    form->AddChild(std::move(rememberBox), Hint().Fixed(kRememberRowH));

    // 试点接入：登录按钮从 admin 私有 DuiButton 换成 balloonwjui::DuiButton
    // + SetVariant(Primary)。视觉一致（品牌蓝实色 + 白字 + hover/pressed 渐
    // 深），点击仍走 DUIN_CLICK 上报。已知局限：DuiButton 当前用默认字号
    // （DuiResMgr 字体），未接 SetFont —— 与原 FlatButton 的 kFontSizeTitle
    // 略有差异；待 DuiButton 加 SetFont(HFONT) 后可还原。
    auto loginBtn = std::make_unique<balloonwjui::DuiButton>();
    loginBtn->SetButtonType(balloonwjui::DuiButton::StylePushButton);
    loginBtn->SetVariant(balloonwjui::DuiButton::Variant::Primary);
    loginBtn->SetCtrlId(kIdLoginSubmit);
    loginBtn->SetText(_T("登录控制台"));
    form->AddChild(std::move(loginBtn), Hint().Fixed(kButtonHeight));

    auto tlsNote = ui::MakeLabel(
        _T("所有登录会话受 TLS 1.3 加密保护，并记录至审计日志。"),
        theme::kFontSizeSmall, false, theme::kInk400,
        DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
    tlsNote->SetWordWrap(true);
    form->AddChild(std::move(tlsNote), Hint().Fixed(44));

    // 网络设置入口行：左侧显示当前服务器地址，右侧「网络设置」链接按钮。
    auto netRow = std::make_unique<DuiHBox>();
    const ServerConfig& sc = AppConfig::Instance().Server();
    CString connText;
    connText.Format(_T("连接到 %s:%d"),
                    static_cast<LPCTSTR>(sc.m_host), sc.m_port);
    auto connLabel = ui::MakeLabel(connText, theme::kFontSizeSmall, false,
                                   theme::kInk400);
    connLabel->SetCtrlId(kIdLoginConnInfo);
    netRow->AddChild(std::move(connLabel),
                     Hint().Weight(1).AlignC(DuiLayout::AlignCenter));
    auto netBtn = std::make_unique<DuiButton>();
    netBtn->SetCtrlId(kIdLoginNetwork);
    netBtn->SetText(_T("网络设置"));
    netBtn->SetVariant(DuiButton::Variant::Ghost);
    netBtn->SetTextPointSize(theme::kFontSizeSmall);
    netRow->AddChild(std::move(netBtn),
                     Hint().Fixed(84, 26).AlignC(DuiLayout::AlignCenter));
    form->AddChild(std::move(netRow), Hint().Fixed(28));

    side->AddChild(std::move(form),
                   Hint().Fixed(kFormHeight, kFormWidth).AlignC(DuiLayout::AlignCenter));

    side->AddChild(std::make_unique<DuiControl>(), Hint().Weight(1));   // 下弹性空白

    return side;
}

} // anonymous namespace

std::unique_ptr<DuiControl> BuildLoginScreen()
{
    // 整体：左品牌面板（定宽）+ 右表单面板（占满剩余）。
    auto root = std::make_unique<DuiHBox>();
    root->AddChild(BuildBrandPanel(), Hint().Fixed(kBrandWidth));
    root->AddChild(BuildFormPanel(),  Hint().Weight(1));
    return root;
}

} // namespace flamingoadmin
