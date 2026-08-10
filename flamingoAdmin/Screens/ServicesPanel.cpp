/**
 *  flamingoAdmin —— 服务号面板 ServicesPanel 实现。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Screens/ServicesPanel.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/UiHelpers.h"
#include "Widgets/IconRenderer.h"
#include "Widgets/CommonWidgets.h"
#include "Widgets/OverlayScrollbar.h"  // 右栏详情区的悬浮 auto-hide 滚动条子控件
#include "Controls/Basic/DuiBadge.h"   // 详情头「状态」徽章子控件（试点接入 SetLeadingDot）
#include "Mock/DataStore.h"

#include "DuiNotify.h"

using namespace balloonwjui;

namespace flamingoadmin {

// 本文件在 namespace flamingoadmin 内，theme:: 直接解析到 flamingoadmin::theme。

namespace {

// ---- 布局常量（96 dpi 逻辑像素）----
const int kPad        = 20;   // 面板左 / 右内距
const int kStatH      = 62;   // 统计卡高度
const int kListW      = 340;  // 列表栏宽度
const int kSvcRowH    = 72;   // 列表中一个服务号行高
const int kDetailPad  = 22;   // 详情区内距
const int kHeadH      = 116;  // 详情头卡片高度
const int kTabBarH    = 38;   // 详情 tab 条高度
const int kSecGap     = 16;   // 详情各区块间距

// 悬浮滚动条距详情区右内缘的间距（像素）。
const int kDetailSbInset = 3;

// 详情 4 个 tab 名。
const TCHAR* const kDetailTabs[] =
{
    _T("概览"), _T("绑定范围"), _T("推送历史"), _T("设置"),
};
const int kDetailTabCount = 4;

// 列表筛选项。
const TCHAR* const kFilters[] = { _T("全部"), _T("活跃"), _T("草稿"), _T("已停用") };
const int kFilterCount = 4;

// 量算文字宽度。
int MeasureW(LPCTSTR text, HFONT font)
{
    HDC   dc = ::GetDC(nullptr);
    HFONT of = static_cast<HFONT>(::SelectObject(dc, font));
    SIZE  sz = { 0, 0 };
    ::GetTextExtentPoint32(dc, text, lstrlen(text), &sz);
    ::SelectObject(dc, of);
    ::ReleaseDC(nullptr, dc);
    return sz.cx;
}

// 画一个 pill 形开关（展示用）。
void DrawToggle(HDC hdc, const RECT& rc, bool on)
{
    const int h = rc.bottom - rc.top;
    ui::FillRoundRect(hdc, rc, on ? theme::kFlamingo500 : theme::kInk200, h / 2);
    const int knob = h - 4;
    RECT k = { on ? (rc.right - 2 - knob) : (rc.left + 2), rc.top + 2,
               0, rc.top + 2 + knob };
    k.right = k.left + knob;
    ui::FillRoundRect(hdc, k, theme::kInk0, knob / 2);
}

// 服务号状态文案 / 颜色。
LPCTSTR ServiceStatusLabel(ServiceStatus s)
{
    switch (s)
    {
    case ServiceStatus::Active:   return _T("运行中");
    case ServiceStatus::Draft:    return _T("草稿");
    case ServiceStatus::Disabled: return _T("已停用");
    default:                      return _T("");
    }
}
COLORREF ServiceStatusColor(ServiceStatus s)
{
    switch (s)
    {
    case ServiceStatus::Active:   return theme::kColorOk;
    case ServiceStatus::Draft:    return theme::kColorWarn;
    case ServiceStatus::Disabled: return theme::kInk400;
    default:                      return theme::kInk400;
    }
}

// 服务号类型文案。
LPCTSTR ServiceKindLabel(ServiceKind k)
{
    switch (k)
    {
    case ServiceKind::Notification: return _T("通知型");
    case ServiceKind::Assistant:    return _T("助手型");
    case ServiceKind::Integration:  return _T("集成型");
    default:                        return _T("服务");
    }
}

// 绑定类型文案 / 配色。
LPCTSTR BindKindLabel(BindingKind k)
{
    switch (k)
    {
    case BindingKind::Enterprise: return _T("企业");
    case BindingKind::Dept:       return _T("部门");
    case BindingKind::User:       return _T("员工");
    default:                      return _T("");
    }
}
COLORREF BindKindColor(BindingKind k)
{
    switch (k)
    {
    case BindingKind::Enterprise: return theme::kFlamingo600;
    case BindingKind::Dept:       return theme::kColorInfo;
    case BindingKind::User:       return theme::kColorOk;
    default:                      return theme::kInk400;
    }
}

// 取某类型服务号的一条 mock 推送文案（按 index 循环取）。
CString MockPush(ServiceKind kind, int index)
{
    static const TCHAR* const kNotif[] =
    {
        _T("5 月薪资单已生成，请在客户端查看"), _T("考勤异常通知：5/19 缺卡"),
        _T("社保公积金缴纳完成"), _T("报销单 RE-2025-0832 已审批通过"),
        _T("本周财务月报已发布"), _T("高管简报 #18 已发送"),
    };
    static const TCHAR* const kAsst[] =
    {
        _T("收到打卡请求，已记录"), _T("工单 IT-0421 已分配给 梁川"),
        _T("已查询到您的休假余额：3.5 天"), _T("VPN 客户端新版本已发布"),
    };
    static const TCHAR* const kIntg[] =
    {
        _T("[P1] api-gateway 5xx 错误率突增"), _T("[P2] redis-cache 内存使用 87%"),
        _T("[INFO] 部署成功 backend@v4.2.108"), _T("[P0] cdn-edge-jp 不可达"),
    };
    const TCHAR* const* pool = kNotif;
    int n = sizeof(kNotif) / sizeof(kNotif[0]);
    if (kind == ServiceKind::Assistant)   { pool = kAsst; n = sizeof(kAsst) / sizeof(kAsst[0]); }
    if (kind == ServiceKind::Integration) { pool = kIntg; n = sizeof(kIntg) / sizeof(kIntg[0]); }
    return CString(pool[index % n]);
}

} // anonymous namespace

ServicesPanel::ServicesPanel()
{
    SetCtrlId(kIdServicesPanel);

    // 详情头「状态」徽章（试点：DuiBadge chip 形态）。先加它、
    // 再加滚动条 —— 滚动条排在子序列最末，base OnPaint 最后绘制它，
    // 不会被徽章盖住。
    auto badge = std::make_unique<balloonwjui::DuiBadge>();
    m_statusBadge = badge.get();
    // 颜色 / 文本 / 圆点色每帧由 PaintDetail 按当前选中服务号刷新。
    // 这里做一次性初始化:
    //   · HideWhenEmpty(false) —— SetVisible(true) 时哪怕文本未及时 set
    //     仍能看出徽章位置;
    //   · CornerRadius(4) —— 切到 chip 形态(圆角矩形, 与 ui::DrawChip 兜
    //     底视觉一致), 替代默认胶囊;
    //   · MaxDisplayChars(0) —— 不截, 让"审核中" "已停用"等 3-4 字状态
    //     完整显示;
    //   · 默认隐藏(SetVisible(false))。
    m_statusBadge->SetHideWhenEmpty(false);
    m_statusBadge->SetCornerRadius(4);
    m_statusBadge->SetMaxDisplayChars(0);
    m_statusBadge->SetVisible(false);
    AddChild(std::move(badge));

    // 右栏详情区的悬浮 auto-hide 滚动条子控件（最后入子，绘制在最上层）。
    auto sb = std::make_unique<OverlayScrollbar>();
    m_detailSb = sb.get();
    m_detailSb->SetOnScroll(&ServicesPanel::OnDetailSbScroll, this);
    AddChild(std::move(sb));
}

void ServicesPanel::SetEnterprise(const CString& enterpriseId)
{
    m_entId      = enterpriseId;
    m_ent        = DataStore::Instance().FindEnterprise(enterpriseId);
    m_detailTab  = 0;
    m_listFilter = 0;
    m_scrollY    = 0;
    // 默认选中第一个服务号。
    const std::vector<Service>& list = DataStore::Instance().Services(enterpriseId);
    m_selectedId = list.empty() ? CString() : list.front().m_id;
    Invalidate();
}

void ServicesPanel::Refresh()
{
    m_ent = DataStore::Instance().FindEnterprise(m_entId);
    // 选中项若已不存在，回退到第一个。
    const std::vector<Service>& list = DataStore::Instance().Services(m_entId);
    bool found = false;
    for (const Service& s : list)
    {
        if (s.m_id == m_selectedId)
        {
            found = true;
            break;
        }
    }
    if (!found)
    {
        m_selectedId = list.empty() ? CString() : list.front().m_id;
        m_detailTab  = 0;
        m_scrollY    = 0;
    }
    Invalidate();
}

const Service* ServicesPanel::SelectedService() const
{
    const std::vector<Service>& list = DataStore::Instance().Services(m_entId);
    for (const Service& s : list)
    {
        if (s.m_id == m_selectedId)
        {
            return &s;
        }
    }
    return nullptr;
}

void ServicesPanel::RaisePending(Pending pending)
{
    m_pending = pending;
    NotifyParent(DUIN_CLICK);   // ctrlId = kIdServicesPanel
}

void ServicesPanel::Layout(const RECT& rcAvail)
{
    m_rcItem = rcAvail;

    // 计算右栏详情矩形（与 OnPaint 里主体卡片 / 双栏拆分的几何完全一致），
    // 据此把悬浮滚动条摆到详情区右内缘的细竖条上，高 = 详情区可视高度。
    const int x   = m_rcItem.left + kPad;
    const int w   = m_rcItem.right - kPad - x;
    const int top = m_rcItem.top + 14 + kStatH + 14;
    RECT body     = { x, top, x + w, m_rcItem.bottom - 14 };
    m_detailRc.left   = body.left + kListW;
    m_detailRc.top    = body.top;
    m_detailRc.right  = body.right;
    m_detailRc.bottom = body.bottom;

    m_detailSb->ForceLayout(RECT{
        m_detailRc.right - kDetailSbInset - OverlayScrollbar::kSuggestedWidth,
        m_detailRc.top,
        m_detailRc.right - kDetailSbInset,
        m_detailRc.bottom });
}

void ServicesPanel::OnDetailSbScroll(void* ctx, int newScroll)
{
    // OverlayScrollbar 拖动滑块的回调：把新滚动量写回本面板并重绘。
    ServicesPanel* self = static_cast<ServicesPanel*>(ctx);
    self->m_scrollY = newScroll;
    self->Invalidate();
}

void ServicesPanel::ClampScroll(const RECT& detailRc)
{
    const int viewH = detailRc.bottom - detailRc.top;
    int maxScroll = m_detailContentH - viewH;
    if (maxScroll < 0) { maxScroll = 0; }
    if (m_scrollY < 0) { m_scrollY = 0; }
    if (m_scrollY > maxScroll) { m_scrollY = maxScroll; }
}

void ServicesPanel::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!m_bVisible)
    {
        return;
    }
    ui::FillSolidRect(hdc, m_rcItem, theme::kInk25);
    m_hits.clear();

    // 默认先隐藏「状态」徽章子控件；下面 PaintDetail 命中时再 SetRect +
    // SetVisible(true)。其它路径（无选中服务号 / 列表视图）不会再开它。
    if (m_statusBadge != nullptr)
    {
        m_statusBadge->SetVisible(false);
    }

    const int x = m_rcItem.left + kPad;
    const int w = m_rcItem.right - kPad - x;
    int top = m_rcItem.top + 14;

    // ---- 统计卡 ----
    int total = 0, active = 0, reach = 0, msg = 0;
    const std::vector<Service>& svcs = DataStore::Instance().Services(m_entId);
    total = static_cast<int>(svcs.size());
    for (const Service& s : svcs)
    {
        if (s.m_status == ServiceStatus::Active) { ++active; }
        reach += s.m_subs;
        msg   += s.m_msgCount;
    }
    struct Stat { LPCTSTR label; CString value; COLORREF clr; };
    CString vT, vA, vR, vM;
    vT.Format(_T("%d"), total);
    vA.Format(_T("%d"), active);
    vR.Format(_T("%d"), reach);
    vM.Format(_T("%d"), msg);
    Stat stats[] =
    {
        { _T("服务号总数"), vT, theme::kInk700 },
        { _T("活跃"),       vA, theme::kColorOk },
        { _T("累计触达"),   vR, theme::kColorInfo },
        { _T("本月消息"),   vM, theme::kInk700 },
    };
    const int statGap = 10;
    const int statW = (w - statGap * 3) / 4;
    for (int i = 0; i < 4; ++i)
    {
        const int sx = x + i * (statW + statGap);
        RECT card = { sx, top, sx + statW, top + kStatH };
        ui::DrawCard(hdc, card);
        RECT sl = { sx + 14, top + 10, sx + statW - 10, top + 26 };
        ui::DrawTextLine(hdc, stats[i].label, sl, theme::kInk400,
                         ui::UiFont(theme::kFontSizeSmall, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        RECT sv = { sx + 14, top + 26, sx + statW - 10, top + kStatH - 8 };
        ui::DrawTextLine(hdc, stats[i].value, sv, stats[i].clr,
                         ui::UiFont(theme::kFontSizeHeading, true),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    top += kStatH + 14;

    // ---- 主体卡片 ----
    RECT body = { x, top, x + w, m_rcItem.bottom - 14 };
    ui::DrawCard(hdc, body);

    RECT listRc   = { body.left, body.top, body.left + kListW, body.bottom };
    RECT detailRc = { listRc.right, body.top, body.right, body.bottom };
    m_detailRc = detailRc;

    // 列表栏（不滚动）。
    {
        int saved = ::SaveDC(hdc);
        ::IntersectClipRect(hdc, listRc.left, listRc.top,
                            listRc.right, listRc.bottom);
        PaintList(hdc, listRc);
        ::RestoreDC(hdc, saved);
    }
    // 分隔线。
    ui::FillLine(hdc, listRc.right - 1, body.top, 1,
                 body.bottom - body.top, theme::kInk100);
    // 详情栏（滚动）。
    {
        int saved = ::SaveDC(hdc);
        ::IntersectClipRect(hdc, detailRc.left, detailRc.top,
                            detailRc.right, detailRc.bottom);
        PaintDetail(hdc, detailRc);
        ::RestoreDC(hdc, saved);
    }

    // PaintDetail 已算出最新内容总高 m_detailContentH，把它与当前滚动量同步
    // 给悬浮滚动条；滚动条作为子控件在下面 base OnPaint 里绘制自身。
    m_detailSb->SetMetrics(m_detailContentH, m_scrollY);

    // 绘制子控件（悬浮滚动条悬浮于详情区右缘、auto-hide）。
    balloonwjui::DuiControl::OnPaint(hdc, rcDirty);
}

void ServicesPanel::PaintList(HDC hdc, const RECT& rc)
{
    // 「新建」按钮。
    RECT btnNew = { rc.left + 12, rc.top + 12, rc.right - 12, rc.top + 12 + 32 };
    ui::FillRoundRect(hdc, btnNew, theme::kFlamingo500, theme::kRadiusMd);
    ui::DrawTextLine(hdc, _T("+ 新建服务号"), btnNew, theme::kInk0,
                     ui::UiFont(theme::kFontSizeNormal, true),
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    {
        Hit h; h.m_rc = btnNew; h.m_kind = 3;
        h.m_arg = static_cast<int>(Pending::New);
        m_hits.push_back(h);
    }

    // 筛选段。
    const int fy = btnNew.bottom + 10;
    RECT filterBox = { rc.left + 12, fy, rc.right - 12, fy + 30 };
    ui::FillRoundRect(hdc, filterBox, theme::kInk50, 7);
    ui::StrokeRoundRect(hdc, filterBox, theme::kInk100, 7, 1.0f);
    const int segW = (filterBox.right - filterBox.left - 4) / kFilterCount;
    for (int i = 0; i < kFilterCount; ++i)
    {
        RECT seg = { filterBox.left + 2 + i * segW, filterBox.top + 2,
                     filterBox.left + 2 + (i + 1) * segW, filterBox.bottom - 2 };
        const bool act = (i == m_listFilter);
        if (act)
        {
            ui::FillRoundRect(hdc, seg, theme::kInk0, 5);
        }
        ui::DrawTextLine(hdc, kFilters[i], seg,
                         act ? theme::kInk700 : theme::kInk500,
                         ui::UiFont(theme::kFontSizeSmall, act),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        Hit h; h.m_rc = seg; h.m_kind = 1; h.m_arg = i;
        m_hits.push_back(h);
    }

    // 服务号列表。
    int ly = filterBox.bottom + 8;
    const std::vector<Service>& svcs = DataStore::Instance().Services(m_entId);
    int shown = 0;
    for (const Service& s : svcs)
    {
        if (m_listFilter == 1 && s.m_status != ServiceStatus::Active)   { continue; }
        if (m_listFilter == 2 && s.m_status != ServiceStatus::Draft)    { continue; }
        if (m_listFilter == 3 && s.m_status != ServiceStatus::Disabled) { continue; }
        ++shown;

        const bool sel = (s.m_id == m_selectedId);
        RECT row = { rc.left, ly, rc.right, ly + kSvcRowH };
        if (sel)
        {
            ui::FillSolidRect(hdc, row, theme::kFlamingo50);
            RECT accent = { row.left, row.top, row.left + 3, row.bottom };
            ui::FillSolidRect(hdc, accent, theme::kFlamingo500);
        }
        ui::FillLine(hdc, rc.left + 12, row.bottom - 1, rc.right - rc.left - 24,
                     1, theme::kInk100);
        const int cy = ly + kSvcRowH / 2;
        RECT av = { rc.left + 14, cy - 18, rc.left + 14 + 36, cy + 18 };
        ui::DrawAvatar(hdc, av, s.m_color, s.m_glyph);
        // 名称 + 草稿/停用徽章。
        RECT nm = { av.right + 12, ly + 12, rc.right - 14, ly + 32 };
        ui::DrawTextLine(hdc, s.m_name, nm,
                         sel ? theme::kFlamingo700 : theme::kInk700,
                         ui::UiFont(theme::kFontSizeNormal, true),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        RECT ds = { av.right + 12, ly + 31, rc.right - 14, ly + 47 };
        ui::DrawTextLine(hdc, s.m_desc, ds, theme::kInk400,
                         ui::UiFont(theme::kFontSizeSmall, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        CString metric;
        metric.Format(_T("订阅 %d · 消息 %d"), s.m_subs, s.m_msgCount);
        RECT mt = { av.right + 12, ly + 47, rc.right - 14, ly + 63 };
        ui::DrawTextLine(hdc, metric, mt, theme::kInk400,
                         ui::UiFont(theme::kFontSizeSmall, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        Hit h; h.m_rc = row; h.m_kind = 0; h.m_svcId = s.m_id;
        m_hits.push_back(h);
        ly += kSvcRowH;
    }
    if (shown == 0)
    {
        RECT empty = { rc.left, ly + 20, rc.right, ly + 60 };
        ui::DrawTextLine(hdc, svcs.empty() ? _T("该企业暂无服务号")
                                           : _T("没有匹配的服务号"),
                         empty, theme::kInk400,
                         ui::UiFont(theme::kFontSizeNormal, false),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

void ServicesPanel::PaintDetail(HDC hdc, const RECT& rc)
{
    const Service* svc = SelectedService();
    if (svc == nullptr)
    {
        // 空占位。
        const int cx = (rc.left + rc.right) / 2;
        const int cy = (rc.top + rc.bottom) / 2;
        RECT box = { cx - 36, cy - 56, cx + 36, cy + 16 };
        ui::FillRoundRect(hdc, box, theme::kInk50, 18);
        RECT ic = { cx - 16, cy - 36, cx + 16, cy - 4 };
        ui::DrawIcon(hdc, ui::Icon::Megaphone, ic, theme::kInk300);
        RECT t = { rc.left, cy + 24, rc.right, cy + 46 };
        ui::DrawTextLine(hdc, _T("选择左侧服务号查看详情"), t, theme::kInk500,
                         ui::UiFont(theme::kFontSizeTitle, false),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        m_detailContentH = rc.bottom - rc.top;
        return;
    }

    const int left  = rc.left + kDetailPad;
    const int right = rc.right - kDetailPad;
    const int w     = right - left;
    int y = rc.top + kDetailPad - m_scrollY;

    // ---- 头卡片 ----
    RECT head = { left, y, right, y + kHeadH };
    ui::DrawCard(hdc, head);
    RECT av = { left + 18, y + 18, left + 18 + 56, y + 18 + 56 };
    ui::DrawAvatar(hdc, av, svc->m_color, svc->m_glyph);
    const int tx = av.right + 16;
    // 名称。
    RECT nm = { tx, y + 18, right - 18, y + 44 };
    ui::DrawTextLine(hdc, svc->m_name, nm, theme::kInk700,
                     ui::UiFont(theme::kFontSizeHeading, true),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    {
        const int nameW = MeasureW(svc->m_name, ui::UiFont(theme::kFontSizeHeading, true));
        int chipX = tx + nameW + 12;
        // 类型徽章。
        const int kw = ui::ChipWidth(hdc, ServiceKindLabel(svc->m_kind), false);
        RECT kchip = { chipX, y + 23, chipX + kw, y + 43 };
        ui::DrawChip(hdc, kchip, ServiceKindLabel(svc->m_kind),
                     theme::kFlamingo700, theme::kFlamingo50, false, 0);
        chipX += kw + 8;
        // 状态徽章(DuiBadge chip 形态 + SetLeadingDot)。位置 / 颜色 / 文本
        // 每帧由这里刷新; 落出详情可见区时改用 ui::DrawChip 自绘兜底,
        // 避免子控件画到详情区之外(base OnPaint 不裁剪子控件)。
        // 两条路径(DuiBadge chip 与 ui::DrawChip)产出视觉一致 —— 都是
        // 4px 圆角矩形 + 浅灰底 + 深字 + 语义色 leading dot, 完整显示
        // 3-4 字状态文字, 不截断。
        LPCTSTR statusLabel = ServiceStatusLabel(svc->m_status);
        const int sw = ui::ChipWidth(hdc, statusLabel, true);
        RECT schip = { chipX, y + 23, chipX + sw, y + 43 };
        const bool fullyInside = (schip.top    >= rc.top    &&
                                  schip.bottom <= rc.bottom);
        if (m_statusBadge != nullptr && fullyInside)
        {
            m_statusBadge->SetText(statusLabel);
            m_statusBadge->SetTextColor(theme::kInk600);
            m_statusBadge->SetBgColor  (theme::kInk50);
            m_statusBadge->SetLeadingDot(ServiceStatusColor(svc->m_status));
            m_statusBadge->SetRect(schip);
            m_statusBadge->SetVisible(true);
        }
        else
        {
            ui::DrawChip(hdc, schip, statusLabel, theme::kInk600,
                         theme::kInk50, true, ServiceStatusColor(svc->m_status));
        }
    }
    // 描述。
    RECT desc = { tx, y + 48, right - 18, y + 68 };
    ui::DrawTextLine(hdc, svc->m_desc, desc, theme::kInk500,
                     ui::UiFont(theme::kFontSizeNormal, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    // 元信息。
    CString meta;
    meta.Format(_T("%s    负责人：%s    创建于 %s"),
                static_cast<LPCTSTR>(svc->m_id),
                static_cast<LPCTSTR>(svc->m_owner),
                static_cast<LPCTSTR>(svc->m_createdAt));
    RECT mr = { tx, y + 70, right - 18, y + 90 };
    ui::DrawTextLine(hdc, meta, mr, theme::kInk400,
                     ui::UiFont(theme::kFontSizeSmall, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    y += kHeadH + 12;

    // ---- tab 条 ----
    int tabX = left;
    for (int i = 0; i < kDetailTabCount; ++i)
    {
        const bool act = (i == m_detailTab);
        const int tw = MeasureW(kDetailTabs[i], ui::UiFont(theme::kFontSizeNormal, act))
                       + 28;
        RECT tab = { tabX, y, tabX + tw, y + kTabBarH };
        ui::DrawTextLine(hdc, kDetailTabs[i], tab,
                         act ? theme::kFlamingo600 : theme::kInk400,
                         ui::UiFont(theme::kFontSizeNormal, act),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (act)
        {
            RECT ul = { tabX + 6, y + kTabBarH - 2, tabX + tw - 6, y + kTabBarH };
            ui::FillSolidRect(hdc, ul, theme::kFlamingo500);
        }
        Hit h; h.m_rc = tab; h.m_kind = 2; h.m_arg = i;
        m_hits.push_back(h);
        tabX += tw;
    }
    ui::FillLine(hdc, left, y + kTabBarH - 1, w, 1, theme::kInk100);
    y += kTabBarH + kSecGap;

    // ---- tab 内容 ----
    if (m_detailTab == 0)            // 概览
    {
        // 3 个小统计。
        const int g = 10;
        const int cw = (w - g * 2) / 3;
        CString v0, v1;
        v0.Format(_T("%d"), svc->m_subs);
        v1.Format(_T("%d"), svc->m_msgCount);
        struct M { LPCTSTR l; CString v; COLORREF c; };
        M ms[] = { { _T("覆盖用户"), v0, theme::kColorInfo },
                   { _T("累计消息"), v1, theme::kInk700 },
                   { _T("本周阅读率"), CString(_T("82.4%")), theme::kColorOk } };
        for (int i = 0; i < 3; ++i)
        {
            RECT c = { left + i * (cw + g), y, left + i * (cw + g) + cw, y + 62 };
            ui::DrawCard(hdc, c);
            RECT cl = { c.left + 14, c.top + 10, c.right - 8, c.top + 26 };
            ui::DrawTextLine(hdc, ms[i].l, cl, theme::kInk400,
                             ui::UiFont(theme::kFontSizeSmall, false),
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            RECT cv = { c.left + 14, c.top + 26, c.right - 8, c.bottom - 8 };
            ui::DrawTextLine(hdc, ms[i].v, cv, ms[i].c,
                             ui::UiFont(theme::kFontSizeHeading, true),
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        y += 62 + kSecGap;

        // 当前绑定。
        RECT st = { left, y, right, y + 18 };
        ui::DrawTextLine(hdc, _T("当前绑定"), st, theme::kInk400,
                         ui::UiFont(theme::kFontSizeSmall, true),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        y += 24;
        const int bn = static_cast<int>(svc->m_bindings.size());
        if (bn == 0)
        {
            RECT box = { left, y, right, y + 48 };
            ui::FillRoundRect(hdc, box, theme::kInk0, theme::kRadiusLg);
            ui::StrokeRoundRect(hdc, box, theme::kInk150, theme::kRadiusLg, 1.0f);
            ui::DrawTextLine(hdc, _T("尚未绑定任何对象，员工不会看到此服务号"),
                             box, theme::kInk400, ui::UiFont(theme::kFontSizeNormal, false),
                             DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            y += 48;
        }
        else
        {
            RECT card = { left, y, right, y + bn * 36 + 8 };
            ui::DrawCard(hdc, card);
            for (int i = 0; i < bn; ++i)
            {
                const ServiceBinding& b = svc->m_bindings[i];
                const int by = y + 4 + i * 36;
                const int cy = by + 18;
                // 类型徽章。
                const int cw2 = ui::ChipWidth(hdc, BindKindLabel(b.m_kind), true);
                RECT bchip = { left + 14, cy - 10, left + 14 + cw2, cy + 10 };
                ui::DrawChip(hdc, bchip, BindKindLabel(b.m_kind), theme::kInk600,
                             theme::kInk50, true, BindKindColor(b.m_kind));
                RECT bl = { bchip.right + 12, by, right - 14, by + 36 };
                ui::DrawTextLine(hdc, b.m_label, bl, theme::kInk700,
                                 ui::UiFont(theme::kFontSizeNormal, false),
                                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            }
            y += bn * 36 + 8;
        }
        y += kSecGap;

        // 近期推送。
        RECT st2 = { left, y, right, y + 18 };
        ui::DrawTextLine(hdc, _T("近期推送"), st2, theme::kInk400,
                         ui::UiFont(theme::kFontSizeSmall, true),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        y += 24;
        const int pn = 5;
        RECT pcard = { left, y, right, y + pn * 34 };
        ui::DrawCard(hdc, pcard);
        for (int i = 0; i < pn; ++i)
        {
            const int py = y + i * 34;
            if (i > 0)
            {
                ui::FillLine(hdc, left + 14, py, w - 28, 1, theme::kInk100);
            }
            CString ts;
            ts.Format(_T("05-21 %02d:%02d"), 18 - i, (i * 13) % 60);
            RECT tr = { left + 14, py, left + 90, py + 34 };
            ui::DrawTextLine(hdc, ts, tr, theme::kInk400,
                             ui::UiFont(theme::kFontSizeSmall, false),
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            RECT msr = { left + 98, py, right - 110, py + 34 };
            ui::DrawTextLine(hdc, MockPush(svc->m_kind, i), msr, theme::kInk700,
                             ui::UiFont(theme::kFontSizeNormal, false),
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            CString rd;
            rd.Format(_T("%d/%d 已读"), svc->m_subs * 6 / 10, svc->m_subs);
            RECT rr = { right - 104, py, right - 14, py + 34 };
            ui::DrawTextLine(hdc, rd, rr, theme::kInk400,
                             ui::UiFont(theme::kFontSizeSmall, false),
                             DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        y += pn * 34;
    }
    else if (m_detailTab == 1)       // 绑定范围
    {
        const int reach = DataStore::Instance().ComputeReach(m_entId, svc->m_bindings);
        // 触达卡。
        RECT card = { left, y, right, y + 80 };
        ui::DrawCard(hdc, card);
        RECT ib = { left + 18, y + 16, left + 18 + 48, y + 16 + 48 };
        ui::FillRoundRect(hdc, ib, theme::kFlamingo50, theme::kRadiusMd);
        RECT ii = { (ib.left + ib.right) / 2 - 12, (ib.top + ib.bottom) / 2 - 12,
                    (ib.left + ib.right) / 2 + 12, (ib.top + ib.bottom) / 2 + 12 };
        ui::DrawIcon(hdc, ui::Icon::User, ii, theme::kFlamingo600);
        RECT rl = { ib.right + 14, y + 16, right - 130, y + 36 };
        ui::DrawTextLine(hdc, _T("当前触达"), rl, theme::kInk400,
                         ui::UiFont(theme::kFontSizeSmall, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        CString rv;
        rv.Format(_T("%d 名员工"), reach);
        RECT rr = { ib.right + 14, y + 36, right - 130, y + 64 };
        ui::DrawTextLine(hdc, rv, rr, theme::kInk700,
                         ui::UiFont(theme::kFontSizeHeading, true),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        // 修改绑定 按钮。
        RECT eb = { right - 18 - 100, y + 23, right - 18, y + 23 + 34 };
        ui::FillRoundRect(hdc, eb, theme::kFlamingo500, theme::kRadiusMd);
        ui::DrawTextLine(hdc, _T("修改绑定"), eb, theme::kInk0,
                         ui::UiFont(theme::kFontSizeNormal, true),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        {
            Hit h; h.m_rc = eb; h.m_kind = 3;
            h.m_arg = static_cast<int>(Pending::EditBindings);
            m_hits.push_back(h);
        }
        y += 80 + kSecGap;

        // 已绑定对象。
        RECT st = { left, y, right, y + 18 };
        ui::DrawTextLine(hdc, _T("已绑定对象"), st, theme::kInk400,
                         ui::UiFont(theme::kFontSizeSmall, true),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        y += 24;
        const int bn = static_cast<int>(svc->m_bindings.size());
        if (bn == 0)
        {
            RECT box = { left, y, right, y + 48 };
            ui::FillRoundRect(hdc, box, theme::kInk0, theme::kRadiusLg);
            ui::StrokeRoundRect(hdc, box, theme::kInk150, theme::kRadiusLg, 1.0f);
            ui::DrawTextLine(hdc, _T("尚未绑定任何对象"), box, theme::kInk400,
                             ui::UiFont(theme::kFontSizeNormal, false),
                             DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            y += 48;
        }
        else
        {
            RECT card2 = { left, y, right, y + bn * 36 + 8 };
            ui::DrawCard(hdc, card2);
            for (int i = 0; i < bn; ++i)
            {
                const ServiceBinding& b = svc->m_bindings[i];
                const int by = y + 4 + i * 36;
                const int cy = by + 18;
                const int cw2 = ui::ChipWidth(hdc, BindKindLabel(b.m_kind), true);
                RECT bchip = { left + 14, cy - 10, left + 14 + cw2, cy + 10 };
                ui::DrawChip(hdc, bchip, BindKindLabel(b.m_kind), theme::kInk600,
                             theme::kInk50, true, BindKindColor(b.m_kind));
                RECT bl = { bchip.right + 12, by, right - 100, by + 36 };
                ui::DrawTextLine(hdc, b.m_label, bl, theme::kInk700,
                                 ui::UiFont(theme::kFontSizeNormal, false),
                                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                RECT bid = { right - 100, by, right - 14, by + 36 };
                ui::DrawTextLine(hdc, b.m_id, bid, theme::kInk300,
                                 ui::UiFont(theme::kFontSizeSmall, false),
                                 DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            y += bn * 36 + 8;
        }
    }
    else if (m_detailTab == 2)       // 推送历史
    {
        RECT st = { left, y, right, y + 18 };
        ui::DrawTextLine(hdc, _T("推送历史"), st, theme::kInk400,
                         ui::UiFont(theme::kFontSizeSmall, true),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        y += 24;
        const int hn = 12;
        RECT hcard = { left, y, right, y + hn * 34 };
        ui::DrawCard(hdc, hcard);
        for (int i = 0; i < hn; ++i)
        {
            const int py = y + i * 34;
            if (i > 0)
            {
                ui::FillLine(hdc, left + 14, py, w - 28, 1, theme::kInk100);
            }
            CString ts;
            ts.Format(_T("05-%02d %02d:%02d"), 21 - i / 3,
                      18 - (i % 9), (i * 17) % 60);
            RECT tr = { left + 14, py, left + 96, py + 34 };
            ui::DrawTextLine(hdc, ts, tr, theme::kInk400,
                             ui::UiFont(theme::kFontSizeSmall, false),
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            RECT msr = { left + 104, py, right - 110, py + 34 };
            ui::DrawTextLine(hdc, MockPush(svc->m_kind, i), msr, theme::kInk700,
                             ui::UiFont(theme::kFontSizeNormal, false),
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            CString rd;
            rd.Format(_T("%d/%d 已读"), svc->m_subs * (5 + i % 5) / 10, svc->m_subs);
            RECT rr = { right - 104, py, right - 14, py + 34 };
            ui::DrawTextLine(hdc, rd, rr, theme::kInk400,
                             ui::UiFont(theme::kFontSizeSmall, false),
                             DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        y += hn * 34;
    }
    else                             // 设置
    {
        // 推送设置。
        RECT st = { left, y, right, y + 18 };
        ui::DrawTextLine(hdc, _T("推送设置"), st, theme::kInk400,
                         ui::UiFont(theme::kFontSizeSmall, true),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        y += 24;
        struct Opt { LPCTSTR label; LPCTSTR hint; bool on; };
        const Opt opts[] =
        {
            { _T("允许员工取消订阅"), _T("关闭后员工无法在客户端隐藏本服务号"), false },
            { _T("消息支持回复"),     _T("允许员工对服务号消息直接回复"),
              svc->m_kind == ServiceKind::Assistant },
            { _T("推送通知（系统横幅）"), _T("关闭后消息仅在应用内出现"), true },
            { _T("工作时间外静音"),   _T("21:00 – 08:00 不发送横幅"), true },
        };
        RECT setCard = { left, y, right, y + 4 * 56 };
        ui::DrawCard(hdc, setCard);
        for (int i = 0; i < 4; ++i)
        {
            const int oy = y + i * 56;
            if (i > 0)
            {
                ui::FillLine(hdc, left + 14, oy, w - 28, 1, theme::kInk100);
            }
            RECT ol = { left + 16, oy + 10, right - 70, oy + 30 };
            ui::DrawTextLine(hdc, opts[i].label, ol, theme::kInk700,
                             ui::UiFont(theme::kFontSizeNormal, false),
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            RECT oh = { left + 16, oy + 30, right - 70, oy + 48 };
            ui::DrawTextLine(hdc, opts[i].hint, oh, theme::kInk400,
                             ui::UiFont(theme::kFontSizeSmall, false),
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            RECT tog = { right - 16 - 36, oy + (56 - 20) / 2,
                         right - 16,      oy + (56 + 20) / 2 };
            DrawToggle(hdc, tog, opts[i].on);
        }
        y += 4 * 56 + kSecGap;

        // 开发者。
        RECT st2 = { left, y, right, y + 18 };
        ui::DrawTextLine(hdc, _T("开发者"), st2, theme::kInk400,
                         ui::UiFont(theme::kFontSizeSmall, true),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        y += 24;
        RECT dcard = { left, y, right, y + 72 };
        ui::DrawCard(hdc, dcard);
        CString webhook;
        webhook.Format(_T("https://im.flamingo.cn/svc/%s/inbound"),
                       static_cast<LPCTSTR>(svc->m_id));
        ui::DrawTextLine(hdc, _T("Webhook 接收地址"),
                         RECT{ left + 16, y + 12, left + 180, y + 32 },
                         theme::kInk400, ui::UiFont(theme::kFontSizeSmall, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        ui::DrawTextLine(hdc, webhook,
                         RECT{ left + 180, y + 12, right - 16, y + 32 },
                         theme::kInk700, ui::UiFont(theme::kFontSizeSmall, false),
                         DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        ui::DrawTextLine(hdc, _T("API Token"),
                         RECT{ left + 16, y + 40, left + 180, y + 60 },
                         theme::kInk400, ui::UiFont(theme::kFontSizeSmall, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        ui::DrawTextLine(hdc, _T("sk_live_••••••••••mA9q"),
                         RECT{ left + 180, y + 40, right - 16, y + 60 },
                         theme::kInk700, ui::UiFont(theme::kFontSizeSmall, false),
                         DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        y += 72 + kSecGap;

        // 危险操作。
        RECT st3 = { left, y, right, y + 18 };
        ui::DrawTextLine(hdc, _T("危险操作"), st3, theme::kColorDanger,
                         ui::UiFont(theme::kFontSizeSmall, true),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        y += 24;
        const bool disabled = (svc->m_status == ServiceStatus::Disabled);
        // 停用 / 启用 行。
        RECT row1 = { left, y, right, y + 60 };
        ui::DrawCard(hdc, row1);
        ui::DrawTextLine(hdc, disabled ? _T("重新启用服务号") : _T("停用服务号"),
                         RECT{ left + 16, y + 11, right - 110, y + 31 },
                         theme::kInk700, ui::UiFont(theme::kFontSizeNormal, true),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        ui::DrawTextLine(hdc, _T("停用后停止接收新消息，但保留历史记录"),
                         RECT{ left + 16, y + 31, right - 110, y + 49 },
                         theme::kInk400, ui::UiFont(theme::kFontSizeSmall, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        RECT tb = { right - 16 - 88, y + 13, right - 16, y + 13 + 34 };
        ui::FillRoundRect(hdc, tb, theme::kInk0, theme::kRadiusMd);
        ui::StrokeRoundRect(hdc, tb, theme::kInk150, theme::kRadiusMd, 1.0f);
        ui::DrawTextLine(hdc, disabled ? _T("启用") : _T("停用"), tb, theme::kInk600,
                         ui::UiFont(theme::kFontSizeNormal, false),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        {
            Hit h; h.m_rc = tb; h.m_kind = 3;
            h.m_arg = static_cast<int>(Pending::ToggleStatus);
            m_hits.push_back(h);
        }
        y += 60 + 10;
        // 删除 行。
        RECT row2 = { left, y, right, y + 60 };
        ui::DrawCard(hdc, row2);
        ui::DrawTextLine(hdc, _T("删除服务号"),
                         RECT{ left + 16, y + 11, right - 110, y + 31 },
                         theme::kInk700, ui::UiFont(theme::kFontSizeNormal, true),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        ui::DrawTextLine(hdc, _T("永久删除，所有员工客户端将立即移除此服务号"),
                         RECT{ left + 16, y + 31, right - 110, y + 49 },
                         theme::kInk400, ui::UiFont(theme::kFontSizeSmall, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        RECT db = { right - 16 - 88, y + 13, right - 16, y + 13 + 34 };
        ui::FillRoundRect(hdc, db, theme::kColorDanger, theme::kRadiusMd);
        ui::DrawTextLine(hdc, _T("删除"), db, theme::kInk0,
                         ui::UiFont(theme::kFontSizeNormal, true),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        {
            Hit h; h.m_rc = db; h.m_kind = 3;
            h.m_arg = static_cast<int>(Pending::Delete);
            m_hits.push_back(h);
        }
        y += 60;
    }

    y += kDetailPad;
    m_detailContentH = y - (rc.top - m_scrollY);
}

bool ServicesPanel::OnLButtonUp(POINT pt, UINT /*mkFlags*/)
{
    for (const Hit& h : m_hits)
    {
        if (!::PtInRect(&h.m_rc, pt))
        {
            continue;
        }
        if (h.m_kind == 0)            // 选服务号
        {
            if (h.m_svcId != m_selectedId)
            {
                m_selectedId = h.m_svcId;
                m_detailTab  = 0;
                m_scrollY    = 0;
                Invalidate();
            }
        }
        else if (h.m_kind == 1)       // 切筛选
        {
            if (h.m_arg != m_listFilter)
            {
                m_listFilter = h.m_arg;
                Invalidate();
            }
        }
        else if (h.m_kind == 2)       // 切详情 tab
        {
            if (h.m_arg != m_detailTab)
            {
                m_detailTab = h.m_arg;
                m_scrollY   = 0;
                Invalidate();
            }
        }
        else if (h.m_kind == 3)       // 动作
        {
            RaisePending(static_cast<Pending>(h.m_arg));
        }
        return true;
    }
    return true;
}

bool ServicesPanel::OnMouseWheel(POINT pt, short zDelta, UINT /*mkFlags*/)
{
    // 仅当指针在详情栏内时滚动详情。
    if (::PtInRect(&m_detailRc, pt))
    {
        m_scrollY -= (zDelta / WHEEL_DELTA) * 60;
        ClampScroll(m_detailRc);
        // 同步滚动量给悬浮滚动条并令其淡入（停止滚动后自动淡出）。
        m_detailSb->SetMetrics(m_detailContentH, m_scrollY);
        m_detailSb->Flash();
        Invalidate();
    }
    return true;
}

bool ServicesPanel::OnSetCursor(POINT /*pt*/)
{
    ::SetCursor(::LoadCursor(nullptr, IDC_ARROW));
    return true;
}

} // namespace flamingoadmin
