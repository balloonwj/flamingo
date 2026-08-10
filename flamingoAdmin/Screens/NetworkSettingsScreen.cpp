/**
 *  flamingoAdmin —— 网络设置界面实现。
 *
 *  balloonwj@qq.com   2026-05-21
 */
#include "stdafx.h"
#include "Screens/NetworkSettingsScreen.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/UiHelpers.h"
#include "Widgets/IconRenderer.h"
#include "Widgets/Widgets.h"
#include "Widgets/CommonWidgets.h"
#include "Util/AppConfig.h"

#include "Controls/Input/DuiEditHost.h"
#include "Controls/Basic/DuiButton.h"   // FlatButton 已迁到 balloonwjui::DuiButton

using namespace balloonwjui;

namespace flamingoadmin {

// 本文件在 namespace flamingoadmin 内，theme:: 直接解析到 flamingoadmin::theme。

namespace {

// ---- 卡片布局常量（96 dpi 逻辑像素），均相对卡片左上角 ----
const int kPanelW    = 460;
const int kPanelH    = 404;
const int kCardPad   = 24;
const int kEditH     = 36;

const int kYBack      = 16;    // 返回链接
const int kYTitle     = 52;    // 标题
const int kYSub       = 84;    // 副标题
const int kYHostLabel = 120;   // 主机标签
const int kYHostEdit  = 142;   // 主机输入框
const int kYPortLabel = 190;   // 端口标签
const int kYPortEdit  = 212;   // 端口输入框
const int kYTls       = 262;   // TLS 开关行
const int kYStatus    = 304;   // 状态行
const int kYButtons   = 342;   // 按钮行

const int kTlsRowH    = 26;    // TLS 开关行高
const int kBtnH       = 38;    // 按钮高度

} // anonymous namespace

NetworkSettingsScreen::NetworkSettingsScreen()
{
    auto mkEdit = [](int id, LPCTSTR ph) -> std::unique_ptr<DuiEditHost>
    {
        auto e = std::make_unique<DuiEditHost>();
        e->SetCtrlId(id);
        e->SetPlaceholder(ph);
        e->SetShowBorder(true);
        e->SetBgColor(theme::kInk0);
        return e;
    };

    auto host = mkEdit(kIdNetHost, _T("服务器主机名 / IP，如 192.168.1.10"));
    m_hostEdit = host.get();
    AddChild(std::move(host));

    auto port = mkEdit(kIdNetPort, _T("端口，如 12345"));
    m_portEdit = port.get();
    AddChild(std::move(port));

    auto back = std::make_unique<DuiButton>();
    back->SetCtrlId(kIdNetBack);
    back->SetText(_T("返回登录"));
    back->SetVariant(DuiButton::Variant::Ghost);
    // 14px 深灰 "<" 图标:Ghost 文字色是 kInk500, 与 FlatButton 内部行为对齐。
    static HBITMAP s_iconChevronLeftInk500 = ui::RenderIconToBitmap(
        ui::Icon::ChevronLeft, 14, theme::kInk500);
    back->SetLeadingIcon(s_iconChevronLeftInk500);
    back->SetLeadingIconSize(14);
    back->SetTextPointSize(theme::kFontSizeNormal);
    AddChild(std::move(back));

    auto test = std::make_unique<DuiButton>();
    test->SetCtrlId(kIdNetTest);
    test->SetText(_T("测试连接"));
    test->SetVariant(DuiButton::Variant::Default);
    test->SetTextPointSize(theme::kFontSizeNormal);
    AddChild(std::move(test));

    auto save = std::make_unique<DuiButton>();
    save->SetCtrlId(kIdNetSave);
    save->SetText(_T("保存并返回"));
    save->SetVariant(DuiButton::Variant::Primary);
    save->SetTextPointSize(theme::kFontSizeNormal);
    AddChild(std::move(save));
}

void NetworkSettingsScreen::LoadFromConfig()
{
    const ServerConfig& cfg = AppConfig::Instance().Server();
    m_hostEdit->SetText(cfg.m_host);
    CString portStr;
    portStr.Format(_T("%d"), cfg.m_port);
    m_portEdit->SetText(portStr);
    m_useTls = cfg.m_useTls;
    m_status.Empty();
    Invalidate();
}

CString NetworkSettingsScreen::GetHost() const
{
    CString h = m_hostEdit->GetText();
    h.Trim();
    return h;
}

CString NetworkSettingsScreen::GetPortText() const
{
    CString p = m_portEdit->GetText();
    p.Trim();
    return p;
}

void NetworkSettingsScreen::SetStatus(LPCTSTR text, COLORREF color)
{
    m_status      = text;
    m_statusColor = color;
    Invalidate();
}

void NetworkSettingsScreen::Layout(const RECT& rcAvail)
{
    m_rcItem = rcAvail;

    const int cx = (rcAvail.left + rcAvail.right) / 2;
    const int cy = (rcAvail.top + rcAvail.bottom) / 2;
    m_panelRect.left   = cx - kPanelW / 2;
    m_panelRect.right  = cx + kPanelW / 2;
    m_panelRect.top    = cy - kPanelH / 2;
    m_panelRect.bottom = cy + kPanelH / 2;

    const int cardLeft  = m_panelRect.left + kCardPad;
    const int cardRight = m_panelRect.right - kCardPad;
    const int fieldW    = cardRight - cardLeft;

    // 返回链接（左上）。
    m_children[2]->ForceLayout(RECT{ cardLeft - 6, m_panelRect.top + kYBack,
        cardLeft - 6 + 104, m_panelRect.top + kYBack + 26 });

    // 主机 / 端口输入框。
    m_hostEdit->ForceLayout(RECT{ cardLeft, m_panelRect.top + kYHostEdit,
        cardRight, m_panelRect.top + kYHostEdit + kEditH });
    m_portEdit->ForceLayout(RECT{ cardLeft, m_panelRect.top + kYPortEdit,
        cardLeft + 160, m_panelRect.top + kYPortEdit + kEditH });

    // TLS 开关行（整行可点）。
    m_tlsToggleRect = RECT{ cardLeft, m_panelRect.top + kYTls,
                            cardRight, m_panelRect.top + kYTls + kTlsRowH };

    // 按钮：测试连接 + 保存并返回（右对齐）。
    const int btnY = m_panelRect.top + kYButtons;
    RECT rcSave = { cardRight - 116, btnY, cardRight, btnY + kBtnH };
    RECT rcTest = { rcSave.left - 10 - 96, btnY, rcSave.left - 10, btnY + kBtnH };
    m_children[3]->ForceLayout(rcTest);
    m_children[4]->ForceLayout(rcSave);

    (void)fieldW;
}

void NetworkSettingsScreen::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!m_bVisible)
    {
        return;
    }
    // 页面背景。
    ui::FillSolidRect(hdc, m_rcItem, theme::kInk25);

    // 居中卡片。
    ui::DrawCard(hdc, m_panelRect);

    const int cardLeft  = m_panelRect.left + kCardPad;
    const int cardRight = m_panelRect.right - kCardPad;

    // 标题 + 副标题。
    ui::DrawTextLine(hdc, _T("网络设置"),
                     RECT{ cardLeft, m_panelRect.top + kYTitle,
                           cardRight, m_panelRect.top + kYTitle + 28 },
                     theme::kInk700, ui::UiFont(theme::kFontSizeHeading + 2, true),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ui::DrawTextLine(hdc, _T("配置管理控制台要连接的服务器地址"),
                     RECT{ cardLeft, m_panelRect.top + kYSub,
                           cardRight, m_panelRect.top + kYSub + 18 },
                     theme::kInk400, ui::UiFont(theme::kFontSizeNormal, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // 字段标签（输入框由子控件绘制）。
    HFONT labelFont = ui::UiFont(theme::kFontSizeSmall, false);
    ui::DrawTextLine(hdc, _T("服务器地址"),
                     RECT{ cardLeft, m_panelRect.top + kYHostLabel,
                           cardRight, m_panelRect.top + kYHostLabel + 16 },
                     theme::kInk500, labelFont,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ui::DrawTextLine(hdc, _T("端口"),
                     RECT{ cardLeft, m_panelRect.top + kYPortLabel,
                           cardRight, m_panelRect.top + kYPortLabel + 16 },
                     theme::kInk500, labelFont,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // TLS 开关行：pill 开关 + 文字。
    const int tlsCy = m_tlsToggleRect.top + kTlsRowH / 2;
    RECT pill = { cardLeft, tlsCy - 10, cardLeft + 36, tlsCy + 10 };
    ui::FillRoundRect(hdc, pill,
                      m_useTls ? theme::kFlamingo500 : theme::kInk200, 10);
    const int knob = 16;
    RECT k = { m_useTls ? (pill.right - 2 - knob) : (pill.left + 2),
               pill.top + 2, 0, pill.top + 2 + knob };
    k.right = k.left + knob;
    ui::FillRoundRect(hdc, k, theme::kInk0, knob / 2);
    ui::DrawTextLine(hdc, _T("使用 TLS 加密连接"),
                     RECT{ pill.right + 12, m_tlsToggleRect.top,
                           cardRight, m_tlsToggleRect.bottom },
                     theme::kInk600, ui::UiFont(theme::kFontSizeNormal, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // 状态行：有内容显示状态，否则显示一句灰色提示。
    RECT rcStatus = { cardLeft, m_panelRect.top + kYStatus,
                      cardRight, m_panelRect.top + kYStatus + 22 };
    if (!m_status.IsEmpty())
    {
        ui::DrawTextLine(hdc, m_status, rcStatus, m_statusColor,
                         ui::UiFont(theme::kFontSizeSmall, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    else
    {
        ui::DrawTextLine(hdc, _T("修改后点「保存并返回」生效，配置保存到本地。"),
                         rcStatus, theme::kInk400,
                         ui::UiFont(theme::kFontSizeSmall, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // 子控件（输入框 + 三个按钮）。
    balloonwjui::DuiControl::OnPaint(hdc, rcDirty);
}

bool NetworkSettingsScreen::OnLButtonUp(POINT pt, UINT /*mkFlags*/)
{
    // 仅处理 TLS 开关行；输入框 / 按钮由各自子控件处理。
    if (::PtInRect(&m_tlsToggleRect, pt))
    {
        m_useTls = !m_useTls;
        Invalidate();
    }
    return true;
}

} // namespace flamingoadmin
