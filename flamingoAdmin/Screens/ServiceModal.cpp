/**
 *  flamingoAdmin —— 新建服务号弹窗 ServiceModal 实现。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Screens/ServiceModal.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/UiHelpers.h"
#include "Widgets/IconRenderer.h"
#include "Widgets/Widgets.h"

#include "Controls/Input/DuiEdit.h"
#include "Controls/Basic/DuiButton.h"   // FlatButton 已迁到 balloonwjui::DuiButton

using namespace balloonwjui;

namespace flamingoadmin {

// 本文件在 namespace flamingoadmin 内，theme:: 直接解析到 flamingoadmin::theme。

namespace {

// ---- 弹窗布局常量（96 dpi 逻辑像素）----
const int kPanelW    = 560;
const int kPanelH    = 548;
const int kHeaderH   = 52;
const int kFooterH   = 60;
const int kBodyPad   = 20;
const int kLabelH    = 18;
const int kFieldGap  = 6;
const int kCtrlH     = 36;
const int kRowGap    = 14;
const int kSwatchSz  = 26;   // 配色色块边长
const int kKindCardH = 74;   // 类型卡片高度

const int kBackdropAlpha = 112;
const int kModalTitlePt  = 14;

// 头像配色调色板（对应设计稿 COLOR_OPTIONS）。
const COLORREF kPalette[] =
{
    RGB(0xFF, 0x75, 0x63), RGB(0x28, 0x64, 0xD4), RGB(0x1F, 0x9D, 0x55),
    RGB(0x8E, 0x5B, 0xD9), RGB(0x0E, 0xA5, 0xA4), RGB(0xD8, 0x8A, 0x17),
    RGB(0xD6, 0x34, 0x2A), RGB(0x1C, 0x22, 0x30),
};
const int kPaletteCount = sizeof(kPalette) / sizeof(kPalette[0]);

// 服务号类型选项。
struct KindOpt { LPCTSTR m_label; LPCTSTR m_desc; ServiceKind m_kind; };
const KindOpt kKinds[] =
{
    { _T("通知型"), _T("单向推送，员工只读"),       ServiceKind::Notification },
    { _T("助手型"), _T("对话式，员工可回复"),       ServiceKind::Assistant   },
    { _T("集成型"), _T("外部系统对接（Webhook）"),  ServiceKind::Integration },
};
const int kKindCount = sizeof(kKinds) / sizeof(kKinds[0]);

} // anonymous namespace

ServiceModal::ServiceModal()
{
    SetVisible(false);

    auto mkEdit = [](int id, LPCTSTR ph) -> std::unique_ptr<DuiEdit>
    {
        auto e = std::make_unique<DuiEdit>();
        e->SetCtrlId(id);
        e->SetPlaceholder(ph);
        e->SetShowBorder(true);
        e->SetBgColor(theme::kInk0);
        return e;
    };

    auto name = mkEdit(kIdSvcName, _T("例如：明远 HR 助手"));
    m_editName = name.get();
    AddChild(std::move(name));

    auto glyph = mkEdit(kIdSvcGlyph, _T("1-2 个字符，例如 HR"));
    m_editGlyph = glyph.get();
    AddChild(std::move(glyph));

    auto desc = mkEdit(kIdSvcDesc, _T("一句话说明这个服务号做什么"));
    m_editDesc = desc.get();
    AddChild(std::move(desc));

    auto closeBtn = std::make_unique<ui::IconButton>();
    closeBtn->SetCtrlId(kIdSvcClose);
    closeBtn->SetIcon(ui::Icon::Close);
    closeBtn->SetIconSize(16);
    closeBtn->SetColors(theme::kInk400, theme::kInk700, theme::kInk100);
    AddChild(std::move(closeBtn));

    auto cancel = std::make_unique<DuiButton>();
    cancel->SetCtrlId(kIdSvcCancel);
    cancel->SetText(_T("取消"));
    cancel->SetVariant(DuiButton::Variant::Default);
    cancel->SetTextPointSize(theme::kFontSizeNormal);
    AddChild(std::move(cancel));

    auto submit = std::make_unique<DuiButton>();
    submit->SetCtrlId(kIdSvcSubmit);
    submit->SetText(_T("创建服务号"));
    submit->SetVariant(DuiButton::Variant::Primary);
    submit->SetTextPointSize(theme::kFontSizeNormal);
    AddChild(std::move(submit));
}

void ServiceModal::Reset()
{
    m_editName->SetText(_T(""));
    m_editGlyph->SetText(_T(""));
    m_editDesc->SetText(_T(""));
    m_colorIndex = 0;
    m_kindIndex  = 0;
}

bool ServiceModal::Validate() const
{
    CString n = GetName();
    CString g = GetGlyph();
    n.Trim();
    g.Trim();
    return !n.IsEmpty() && !g.IsEmpty();
}

CString ServiceModal::GetName() const  { return m_editName->GetText(); }
CString ServiceModal::GetGlyph() const { return m_editGlyph->GetText(); }
CString ServiceModal::GetDesc() const  { return m_editDesc->GetText(); }

COLORREF ServiceModal::GetColor() const
{
    return kPalette[m_colorIndex];
}

ServiceKind ServiceModal::GetKind() const
{
    return kKinds[m_kindIndex].m_kind;
}

void ServiceModal::Layout(const RECT& rcAvail)
{
    m_rcItem = rcAvail;

    const int cx = (rcAvail.left + rcAvail.right) / 2;
    const int cy = (rcAvail.top + rcAvail.bottom) / 2;
    m_panelRect.left   = cx - kPanelW / 2;
    m_panelRect.right  = cx + kPanelW / 2;
    m_panelRect.top    = cy - kPanelH / 2;
    m_panelRect.bottom = cy + kPanelH / 2;

    // 关闭按钮。
    const size_t n = m_children.size();
    m_children[n - 3]->ForceLayout(RECT{ m_panelRect.right - 12 - 30,
        m_panelRect.top + (kHeaderH - 30) / 2,
        m_panelRect.right - 12, m_panelRect.top + (kHeaderH + 30) / 2 });
    // 取消 / 创建。
    const int by = m_panelRect.bottom - kFooterH + (kFooterH - 34) / 2;
    RECT rcSubmit = { m_panelRect.right - 20 - 116, by,
                      m_panelRect.right - 20,       by + 34 };
    RECT rcCancel = { rcSubmit.left - 10 - 76, by, rcSubmit.left - 10, by + 34 };
    m_children[n - 2]->ForceLayout(rcCancel);
    m_children[n - 1]->ForceLayout(rcSubmit);

    // 表单输入框。
    const int fl = m_panelRect.left + kBodyPad;
    const int fw = kPanelW - kBodyPad * 2;
    int y = m_panelRect.top + kHeaderH + kBodyPad;
    const int fieldH = kLabelH + kFieldGap + kCtrlH;     // 60
    m_editName->ForceLayout(RECT{ fl, y + kLabelH + kFieldGap,
                                  fl + fw, y + kLabelH + kFieldGap + kCtrlH });
    y += fieldH + kRowGap;
    m_editGlyph->ForceLayout(RECT{ fl, y + kLabelH + kFieldGap,
                                   fl + fw, y + kLabelH + kFieldGap + kCtrlH });
    y += fieldH + kRowGap;
    y += (kLabelH + kFieldGap + kSwatchSz) + kRowGap;    // 跳过配色区
    m_editDesc->ForceLayout(RECT{ fl, y + kLabelH + kFieldGap,
                                  fl + fw, y + kLabelH + kFieldGap + kCtrlH });
}

void ServiceModal::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!m_bVisible)
    {
        return;
    }
    m_hits.clear();
    ui::FillTranslucent(hdc, m_rcItem, RGB(0, 0, 0), kBackdropAlpha);

    // 卡片。
    ui::FillRoundRect(hdc, m_panelRect, theme::kInk0, theme::kRadiusLg);
    ui::StrokeRoundRect(hdc, m_panelRect, theme::kInk100, theme::kRadiusLg, 1.0f);

    // 标题。
    RECT rcTitle = { m_panelRect.left + 20, m_panelRect.top,
                     m_panelRect.right - 50, m_panelRect.top + kHeaderH };
    ui::DrawTextLine(hdc, _T("新建服务号"), rcTitle, theme::kInk700,
                     ui::UiFont(kModalTitlePt, true),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ui::FillLine(hdc, m_panelRect.left, m_panelRect.top + kHeaderH,
                 kPanelW, 1, theme::kInk100);
    ui::FillLine(hdc, m_panelRect.left, m_panelRect.bottom - kFooterH,
                 kPanelW, 1, theme::kInk100);

    const int fl = m_panelRect.left + kBodyPad;
    const int fw = kPanelW - kBodyPad * 2;
    int y = m_panelRect.top + kHeaderH + kBodyPad;
    const int fieldH = kLabelH + kFieldGap + kCtrlH;

    HFONT labelFont = ui::UiFont(theme::kFontSizeSmall, false);
    auto drawLabel = [&](int ly, LPCTSTR text)
    {
        RECT rl = { fl, ly, fl + fw, ly + kLabelH };
        ui::DrawTextLine(hdc, text, rl, theme::kInk500, labelFont,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    };

    // 名称 / 头像文字字段标签（输入框由子控件绘制）。
    drawLabel(y, _T("服务号名称 *"));
    y += fieldH + kRowGap;
    drawLabel(y, _T("头像文字 *"));
    y += fieldH + kRowGap;

    // 配色。
    drawLabel(y, _T("头像配色"));
    const int swatchY = y + kLabelH + kFieldGap;
    for (int i = 0; i < kPaletteCount; ++i)
    {
        RECT sw = { fl + i * (kSwatchSz + 10), swatchY,
                    fl + i * (kSwatchSz + 10) + kSwatchSz, swatchY + kSwatchSz };
        ui::FillRoundRect(hdc, sw, kPalette[i], kSwatchSz / 2);
        if (i == m_colorIndex)
        {
            RECT ring = sw;
            ::InflateRect(&ring, 3, 3);
            ui::StrokeRoundRect(hdc, ring, theme::kInk700,
                                (kSwatchSz + 6) / 2, 2.0f);
        }
        Hit h; h.m_rc = sw; h.m_kind = 0; h.m_arg = i;
        m_hits.push_back(h);
    }
    y += (kLabelH + kFieldGap + kSwatchSz) + kRowGap;

    // 描述字段标签。
    drawLabel(y, _T("描述"));
    y += fieldH + kRowGap;

    // 类型。
    drawLabel(y, _T("类型"));
    const int cardY = y + kLabelH + kFieldGap;
    const int cardGap = 10;
    const int cardW = (fw - cardGap * 2) / 3;
    for (int i = 0; i < kKindCount; ++i)
    {
        const int kx = fl + i * (cardW + cardGap);
        RECT card = { kx, cardY, kx + cardW, cardY + kKindCardH };
        const bool sel = (i == m_kindIndex);
        ui::FillRoundRect(hdc, card, sel ? theme::kFlamingo50 : theme::kInk0,
                          theme::kRadiusMd);
        ui::StrokeRoundRect(hdc, card, sel ? theme::kFlamingo300 : theme::kInk150,
                            theme::kRadiusMd, 1.0f);
        RECT cl = { kx + 12, cardY + 12, kx + cardW - 10, cardY + 32 };
        ui::DrawTextLine(hdc, kKinds[i].m_label, cl,
                         sel ? theme::kFlamingo700 : theme::kInk700,
                         ui::UiFont(theme::kFontSizeNormal, true),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        RECT cd = { kx + 12, cardY + 34, kx + cardW - 10, cardY + kKindCardH - 8 };
        ui::DrawTextLine(hdc, kKinds[i].m_desc, cd, theme::kInk400,
                         ui::UiFont(theme::kFontSizeSmall, false),
                         DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
        Hit h; h.m_rc = card; h.m_kind = 1; h.m_arg = i;
        m_hits.push_back(h);
    }

    // 子控件（输入框 + 按钮）。
    balloonwjui::DuiControl::OnPaint(hdc, rcDirty);
}

balloonwjui::DuiControl* ServiceModal::HitTest(POINT ptHostClient)
{
    if (!m_bVisible || !::PtInRect(&m_rcItem, ptHostClient))
    {
        return nullptr;
    }
    if (::PtInRect(&m_panelRect, ptHostClient))
    {
        for (const auto& child : m_children)
        {
            if (DuiControl* hit = child->HitTest(ptHostClient))
            {
                return hit;
            }
        }
        return this;
    }
    return this;
}

bool ServiceModal::OnLButtonUp(POINT pt, UINT /*mkFlags*/)
{
    for (const Hit& h : m_hits)
    {
        if (!::PtInRect(&h.m_rc, pt))
        {
            continue;
        }
        if (h.m_kind == 0 && h.m_arg != m_colorIndex)
        {
            m_colorIndex = h.m_arg;
            Invalidate();
        }
        else if (h.m_kind == 1 && h.m_arg != m_kindIndex)
        {
            m_kindIndex = h.m_arg;
            Invalidate();
        }
        return true;
    }
    return true;   // 点遮罩 / 空白：吞掉，不关闭
}

} // namespace flamingoadmin
