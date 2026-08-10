/**
 *  flamingoAdmin —— 公告中心界面 AnnouncementsScreen 实现。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Screens/AnnouncementsScreen.h"
#include "Screens/AnnouncementPreviewPanel.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/UiHelpers.h"
#include "Widgets/IconRenderer.h"
#include "Widgets/Widgets.h"
#include "Widgets/CommonWidgets.h"
#include "Widgets/OverlayScrollbar.h"  // 公告列表 / 预览区的悬浮滚动条子控件
#include "Mock/DataStore.h"
#include "Util/ListPaging.h"           // 列表分页纯计算函数

#include "Controls/Basic/DuiButton.h"   // FlatButton 已迁到 balloonwjui::DuiButton
#include "DuiNotify.h"

using namespace balloonwjui;

namespace flamingoadmin {

// 本文件在 namespace flamingoadmin 内，theme:: 直接解析到 flamingoadmin::theme。

namespace {

// ---- 布局常量（96 dpi 逻辑像素）----
const int kHeaderH   = 100;  // 页头高度
const int kSidePad   = 24;   // 内容区左 / 右内距
const int kListW     = 340;  // 列表栏宽度
const int kListItemH = 100;  // 列表项高度
const int kFilterH   = 42;   // 列表筛选行高
const int kPagerH    = 44;   // 列表底部分页条高度
const int kPvPad     = 30;   // 预览区内距

const int kEyebrowPt   = 8;
const int kPageTitlePt = 17;

const TCHAR* const kFilters[] = { _T("全部"), _T("置顶"), _T("重要"), _T("定时") };
const int kFilterCount = 4;

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

// 量算一段文字在给定宽度下换行后的高度。
int MeasureWrappedH(LPCTSTR text, HFONT font, int width)
{
    HDC   dc = ::GetDC(nullptr);
    HFONT of = static_cast<HFONT>(::SelectObject(dc, font));
    RECT  rc = { 0, 0, width, 0 };
    ::DrawText(dc, text, -1, &rc,
               DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
    ::SelectObject(dc, of);
    ::ReleaseDC(nullptr, dc);
    return rc.bottom - rc.top;
}

// 公告严重级别 → 徽章文案。
LPCTSTR SeverityChipLabel(Severity s)
{
    switch (s)
    {
    case Severity::Warn:   return _T("提醒");
    case Severity::Danger: return _T("安全公告");
    case Severity::Info:   return _T("通知");
    case Severity::Normal:
    default:               return _T("通知");
    }
}

} // anonymous namespace

AnnouncementsScreen::AnnouncementsScreen()
{
    SetCtrlId(kIdAnnScreen);

    auto exportBtn = std::make_unique<DuiButton>();
    exportBtn->SetCtrlId(kIdAnnExport);
    exportBtn->SetText(_T("导出"));
    exportBtn->SetVariant(DuiButton::Variant::Default);
    exportBtn->SetTextPointSize(theme::kFontSizeNormal);
    m_btnExport = exportBtn.get();
    AddChild(std::move(exportBtn));

    auto newBtn = std::make_unique<DuiButton>();
    newBtn->SetCtrlId(kIdAnnNew);
    newBtn->SetText(_T("新建公告"));
    newBtn->SetVariant(DuiButton::Variant::Primary);
    // 14px 白色 "+" 图标:Primary 文字色是 kInk0(白),与 FlatButton 内部行为对齐。
    // 静态缓存,进程级生命期。
    static HBITMAP s_iconPlusWhite = ui::RenderIconToBitmap(
        ui::Icon::Plus, 14, theme::kInk0);
    newBtn->SetLeadingIcon(s_iconPlusWhite);
    newBtn->SetLeadingIconSize(14);
    newBtn->SetTextPointSize(theme::kFontSizeNormal);
    m_btnNew = newBtn.get();
    AddChild(std::move(newBtn));

    // 公告预览区的悬浮滚动条。Layout 通过具名指针定位各子控件、不依赖子控件
    // 下标，故此处追加 AddChild 不影响既有按钮的布局。
    auto sb = std::make_unique<OverlayScrollbar>();
    m_previewSb = sb.get();
    m_previewSb->SetOnScroll(&AnnouncementsScreen::OnPreviewSbScroll, this);
    AddChild(std::move(sb));

    // 左侧公告列表的悬浮滚动条。同样以具名指针定位、追加 AddChild 安全。
    auto listSb = std::make_unique<OverlayScrollbar>();
    m_listSb = listSb.get();
    m_listSb->SetOnScroll(&AnnouncementsScreen::OnListSbScroll, this);
    AddChild(std::move(listSb));

    // 右栏「公告详情」面板。整块替代原 PaintPreview 自绘 —— 文字字段(标题 /
    // 正文 / 日期 / 发送者 / 受众 / 进度 / 推送描述)由 DuiLabel(selectable) /
    // DuiRichEditHost(只读多行) 接管,鼠标拖选 + Ctrl+C 可复制。装饰图形
    // (徽章 / 头像 / 分隔线 / 进度条 / 调度卡图标底)由 PreviewPanel::OnPaint
    // 继续画。本控件只负责定位 + 滚动偏移 + 选公告时调 SetAnnouncement。
    auto pv = std::make_unique<AnnouncementPreviewPanel>();
    m_previewPanel = pv.get();
    AddChild(std::move(pv));

    const std::vector<Announcement>& list = DataStore::Instance().Announcements();
    if (!list.empty())
    {
        m_selectedId = list.front().m_id;
    }
    // 初始把当前选中公告(若有)推给详情面板。
    SyncPreviewPanel();
}

void AnnouncementsScreen::SyncPreviewPanel()
{
    if (m_previewPanel == nullptr)
    {
        return;
    }
    m_previewPanel->SetAnnouncement(Selected());

    // 切换公告后,详情面板里标题是按文本动态量算高度的(正文已固定高度,不受
    // 影响)。但点击切换只走 SetAnnouncement(SetText + Invalidate),不会触发
    // 重新布局,新标题若行数与上一条不同,高度会停在上一条的值。框架没有
    // "请求重新布局"接口,这里在本界面已经完成过首次布局(m_rcItem 有效)时,
    // 主动 ForceLayout 一次本界面,让详情面板的两阶段布局按新标题重算高度、
    // 重新摆放各子控件。首次布局之前(构造期调用)m_rcItem 为空,跳过即可。
    if (m_rcItem.right > m_rcItem.left && m_rcItem.bottom > m_rcItem.top)
    {
        ForceLayout(m_rcItem);
    }
}

void AnnouncementsScreen::Refresh()
{
    const std::vector<Announcement>& list = DataStore::Instance().Announcements();
    bool found = false;
    for (const Announcement& a : list)
    {
        if (a.m_id == m_selectedId)
        {
            found = true;
            break;
        }
    }
    if (!found)
    {
        // 选中项已不存在(如被删除):选回第一条,并把列表翻回第 1 页,使新选中
        // 项在当前页可见。
        m_selectedId = list.empty() ? CString() : list.front().m_id;
        m_previewScroll = 0;
        m_listPage      = 0;
    }
    // 数据可能更新,把当前选中公告(可能是新数据 / 也可能是新选中)推给详情面板。
    SyncPreviewPanel();
    Invalidate();
}

const Announcement* AnnouncementsScreen::Selected() const
{
    for (const Announcement& a : DataStore::Instance().Announcements())
    {
        if (a.m_id == m_selectedId)
        {
            return &a;
        }
    }
    return nullptr;
}

void AnnouncementsScreen::RaisePending(Pending pending)
{
    m_pending = pending;
    NotifyParent(DUIN_CLICK);
}

void AnnouncementsScreen::Layout(const RECT& rcAvail)
{
    m_rcItem = rcAvail;

    // 页头操作按钮。
    const int cy = rcAvail.top + 56;
    RECT rcNew = { rcAvail.right - kSidePad - 112, cy - 17,
                   rcAvail.right - kSidePad,       cy + 17 };
    RECT rcExp = { rcNew.left - 8 - 76, cy - 17, rcNew.left - 8, cy + 17 };
    m_btnExport->ForceLayout(rcExp);
    m_btnNew->ForceLayout(rcNew);

    // 预览滚动区。
    m_previewRc = RECT{ rcAvail.left + kListW, rcAvail.top + kHeaderH,
                        rcAvail.right, rcAvail.bottom };

    // 悬浮滚动条：摆在预览区右内缘的细竖条上，高 = 预览可视区高度。距右缘
    // 留 kSbInset 像素，悬浮于内容之上、不占用预览内容宽度。
    const int kSbInset = 3;   // 滚动条距区域右缘的内缩像素
    m_previewSb->ForceLayout(RECT{
        m_previewRc.right - kSbInset - OverlayScrollbar::kSuggestedWidth,
        m_previewRc.top,
        m_previewRc.right - kSbInset,
        m_previewRc.bottom });

    // 左侧公告列表的悬浮滚动条：列表栏可滚动项区域在筛选行之下，故顶部下移
    // kFilterH；高度 = 该可视区高度，与 OnMouseWheel 夹取用的 viewH 一致。
    // 列表栏右缘减 1 像素是分隔线，滚动条贴其内侧。
    const int listItemsTop    = rcAvail.top + kHeaderH + kFilterH;
    const int listItemsBottom = rcAvail.bottom;
    const int listItemsRight  = rcAvail.left + kListW - 1;
    m_listSb->ForceLayout(RECT{
        listItemsRight - kSbInset - OverlayScrollbar::kSuggestedWidth,
        listItemsTop,
        listItemsRight - kSbInset,
        listItemsBottom });

    // 「公告详情」面板:铺满整个预览区 m_previewRc。面板内正文区是弹性高度
    // (Weight),会撑满标题 / 发送者 / 分隔线之下、状态卡 / 按钮行之上的剩余
    // 竖直空间,从而把按钮行顶到预览区底部(底部对齐);正文超长时由其自带的
    // 悬浮滚动条在框内滚动。整页预览滚动(m_previewScroll)不再使用,故把
    // m_previewContentH 置为可视高,令预览区悬浮滚动条始终隐藏。
    if (m_previewPanel != nullptr)
    {
        m_previewPanel->ForceLayout(m_previewRc);
    }
    m_previewContentH = m_previewRc.bottom - m_previewRc.top;
}

void AnnouncementsScreen::OnPaint(HDC hdc, const RECT& rcDirty)
{
    if (!m_bVisible)
    {
        return;
    }
    m_hits.clear();
    ui::FillSolidRect(hdc, m_rcItem, theme::kInk0);

    // ---- 页头 ----
    const int L = m_rcItem.left;
    RECT eb = { L + kSidePad, m_rcItem.top + 22, L + 300, m_rcItem.top + 38 };
    ui::DrawTextLine(hdc, _T("公告中心"), eb, theme::kFlamingo600,
                     ui::UiFont(kEyebrowPt, true),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    RECT tb = { L + kSidePad, m_rcItem.top + 38, L + 400, m_rcItem.top + 70 };
    ui::DrawTextLine(hdc, _T("平台公告"), tb, theme::kInk700,
                     ui::UiFont(kPageTitlePt, true),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    RECT sb = { L + kSidePad, m_rcItem.top + 70, L + 600, m_rcItem.top + 90 };
    ui::DrawTextLine(hdc, _T("向企业 / 部门 / 个人发布的全局通知"), sb,
                     theme::kInk400, ui::UiFont(theme::kFontSizeNormal, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ui::FillLine(hdc, L, m_rcItem.top + kHeaderH - 1,
                 m_rcItem.right - L, 1, theme::kInk100);

    RECT listRc = { L, m_rcItem.top + kHeaderH, L + kListW, m_rcItem.bottom };
    // 列表栏右分隔线。
    ui::FillLine(hdc, listRc.right - 1, listRc.top, 1,
                 listRc.bottom - listRc.top, theme::kInk100);

    PaintList(hdc, listRc);

    // PaintList 已最终算出 m_listContentH；把列表内容总高 / 滚动量同步给左侧
    // 悬浮滚动条子控件，它将在下方 base OnPaint 中绘制自身。
    m_listSb->SetMetrics(m_listContentH, m_listScroll);

    // 公告详情面板由 m_previewPanel 接管,无需在此自绘 —— base OnPaint 会
    // 转发它的绘制(连同两个 OverlayScrollbar 与页头两个按钮)。这里只把
    // PreviewPanel 内容高度同步给悬浮滚动条:contentH = m_previewContentH
    // (Layout 阶段从 PreviewPanel.GetDesiredSize 拿到)。
    //
    // 注:本期暂不支持预览区滚动(m_previewScroll 始终为 0,m_previewSb 在
    // 内容能装下时自动隐藏)。原因:DuiRichEditHost 是真 RICHEDIT50W 子 HWND,
    // 父 HWND 的 clip 对它不生效,scroll>0 时 RichEdit 内容会越界覆盖页头 /
    // 列表区,视觉错乱。后续如有超长公告需求,加 PreviewPanel 内部 SetScrollOffset
    // 自行处理子控件位置与 RichEdit clip 即可,此处再放开 m_previewScroll 接入。
    m_previewSb->SetMetrics(m_previewContentH, m_previewScroll);

    balloonwjui::DuiControl::OnPaint(hdc, rcDirty);
}

void AnnouncementsScreen::PaintList(HDC hdc, const RECT& rc)
{
    // 筛选行。
    int fx = rc.left + 12;
    const int fy = rc.top + 8;
    for (int i = 0; i < kFilterCount; ++i)
    {
        const bool act = (i == m_filter);
        const int fw = MeasureW(kFilters[i], ui::UiFont(theme::kFontSizeSmall, act))
                       + 20;
        RECT seg = { fx, fy, fx + fw, fy + 26 };
        if (act)
        {
            ui::FillRoundRect(hdc, seg, theme::kInk100, theme::kRadiusSm);
        }
        ui::DrawTextLine(hdc, kFilters[i], seg,
                         act ? theme::kInk700 : theme::kInk500,
                         ui::UiFont(theme::kFontSizeSmall, act),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        Hit h; h.m_rc = seg; h.m_kind = 1; h.m_arg = i;
        m_hits.push_back(h);
        fx += fw + 4;
    }
    ui::FillLine(hdc, rc.left, rc.top + kFilterH - 1, kListW - 1, 1,
                 theme::kInk100);

    // 公告列表(分页)。项区域 = 列表栏去掉顶部筛选行与底部分页条;不再滚动,
    // 每页正好铺满项区域,超出部分翻页查看(底部分页条)。
    const int itemsTop = rc.top + kFilterH;        // 项区域顶部
    const int pagerTop = rc.bottom - kPagerH;      // 底部分页条顶部 = 项区域底部

    std::vector<const Announcement*> filtered;
    CollectFiltered(&filtered);
    const int total    = static_cast<int>(filtered.size());
    const int pageSize = ListPageSize();
    m_listPage = ListClampPage(m_listPage, total, pageSize);   // 夹取到合法页
    int begin = 0;
    int count = 0;
    ListPageRange(m_listPage, pageSize, total, begin, count);

    int savedList = ::SaveDC(hdc);
    ::IntersectClipRect(hdc, rc.left, itemsTop, rc.right, pagerTop);
    int ly = itemsTop;
    for (int idx = 0; idx < count; ++idx)
    {
        const Announcement& a = *filtered[begin + idx];

        const bool sel = (a.m_id == m_selectedId);
        RECT item = { rc.left, ly, rc.right, ly + kListItemH };
        if (sel)
        {
            ui::FillSolidRect(hdc, item, theme::kFlamingo50);
            RECT accent = { rc.left, ly, rc.left + 3, ly + kListItemH };
            ui::FillSolidRect(hdc, accent, theme::kFlamingo500);
        }
        ui::FillLine(hdc, rc.left, item.bottom - 1, kListW, 1, theme::kInk100);

        const int ix = rc.left + 16;
        // 顶行：级别圆点 + 定时徽章 + 日期。
        ui::DrawSeverityDot(hdc, ix + 4, ly + 16, a.m_severity);
        int metaX = ix + 14;
        if (a.m_scheduled)
        {
            const int cw = ui::ChipWidth(hdc, _T("定时"), false);
            RECT chip = { metaX, ly + 8, metaX + cw, ly + 24 };
            ui::DrawChip(hdc, chip, _T("定时"), theme::kColorInfo,
                         theme::kInk50, false, 0);
            metaX += cw + 6;
        }
        RECT dr = { metaX, ly + 8, rc.right - 14, ly + 24 };
        ui::DrawTextLine(hdc, a.m_sentAt.Mid(5, 11), dr, theme::kInk400,
                         ui::UiFont(theme::kFontSizeSmall, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        // 标题。
        RECT tr = { ix, ly + 28, rc.right - 14, ly + 48 };
        ui::DrawTextLine(hdc, a.m_title, tr,
                         sel ? theme::kFlamingo700 : theme::kInk700,
                         ui::UiFont(theme::kFontSizeNormal, true),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        // 正文摘要。
        RECT br = { ix, ly + 48, rc.right - 14, ly + 66 };
        ui::DrawTextLine(hdc, a.m_body, br, theme::kInk400,
                         ui::UiFont(theme::kFontSizeSmall, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        // 底行：受众 + 已读。
        RECT au = { ix, ly + 70, rc.left + 220, ly + 88 };
        ui::DrawTextLine(hdc, a.m_audience, au, theme::kInk400,
                         ui::UiFont(theme::kFontSizeSmall, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        CString rd;
        rd.Format(_T("%d/%d 已读"), a.m_read, a.m_total);
        RECT rr = { rc.left + 200, ly + 70, rc.right - 14, ly + 88 };
        ui::DrawTextLine(hdc, rd, rr, theme::kInk400,
                         ui::UiFont(theme::kFontSizeSmall, false),
                         DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        Hit h; h.m_rc = item; h.m_kind = 0; h.m_annId = a.m_id;
        m_hits.push_back(h);
        ly += kListItemH;
    }
    if (total == 0)
    {
        RECT empty = { rc.left, itemsTop + 20, rc.right, itemsTop + 56 };
        ui::DrawTextLine(hdc, _T("没有符合条件的公告"), empty, theme::kInk400,
                         ui::UiFont(theme::kFontSizeNormal, false),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    ::RestoreDC(hdc, savedList);

    // 分页模式不滚动,列表悬浮滚动条恒隐藏(contentH<=viewH 它自动不画)。
    m_listContentH = 0;

    // 底部分页条。
    PaintPager(hdc, RECT{ rc.left, pagerTop, rc.right, rc.bottom }, total, pageSize);
}

int AnnouncementsScreen::CollectFiltered(std::vector<const Announcement*>* out) const
{
    int n = 0;
    for (const Announcement& a : DataStore::Instance().Announcements())
    {
        if (m_filter == 1 && !a.m_pinned)
        {
            continue;
        }
        if (m_filter == 2 && a.m_severity != Severity::Warn
                          && a.m_severity != Severity::Danger)
        {
            continue;
        }
        if (m_filter == 3 && !a.m_scheduled)
        {
            continue;
        }
        if (out != nullptr)
        {
            out->push_back(&a);
        }
        ++n;
    }
    return n;
}

int AnnouncementsScreen::ListPageSize() const
{
    const int itemsTop    = m_rcItem.top + kHeaderH + kFilterH;
    const int itemsBottom = m_rcItem.bottom - kPagerH;
    const int h = itemsBottom - itemsTop;
    int n = (kListItemH > 0) ? (h / kListItemH) : 1;
    if (n < 1)
    {
        n = 1;
    }
    return n;
}

void AnnouncementsScreen::PaintPager(HDC hdc, const RECT& rc, int total, int pageSize)
{
    const int pageCount = ListPageCount(total, pageSize);

    // 顶部分隔线(与列表项区分隔)。
    ui::FillLine(hdc, rc.left, rc.top, kListW - 1, 1, theme::kInk100);

    const bool canPrev = (m_listPage > 0);
    const bool canNext = (m_listPage < pageCount - 1);

    const int kPagerBtnW = 64;   // 上一页 / 下一页 按钮宽
    const int kPagerBtnH = 26;   // 上一页 / 下一页 按钮高
    const int cy = (rc.top + rc.bottom) / 2;

    // 上一页(左)。
    RECT prev = { rc.left + 12, cy - kPagerBtnH / 2,
                  rc.left + 12 + kPagerBtnW, cy + kPagerBtnH / 2 };
    ui::StrokeRoundRect(hdc, prev,
                        canPrev ? theme::kInk200 : theme::kInk100,
                        theme::kRadiusSm, 1.0f);
    ui::DrawTextLine(hdc, _T("上一页"), prev,
                     canPrev ? theme::kInk600 : theme::kInk300,
                     ui::UiFont(theme::kFontSizeSmall, false),
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (canPrev)
    {
        Hit h; h.m_rc = prev; h.m_kind = 3; h.m_arg = -1;
        m_hits.push_back(h);
    }

    // 下一页(右)。
    RECT next = { rc.right - 12 - kPagerBtnW, cy - kPagerBtnH / 2,
                  rc.right - 12, cy + kPagerBtnH / 2 };
    ui::StrokeRoundRect(hdc, next,
                        canNext ? theme::kInk200 : theme::kInk100,
                        theme::kRadiusSm, 1.0f);
    ui::DrawTextLine(hdc, _T("下一页"), next,
                     canNext ? theme::kInk600 : theme::kInk300,
                     ui::UiFont(theme::kFontSizeSmall, false),
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (canNext)
    {
        Hit h; h.m_rc = next; h.m_kind = 3; h.m_arg = 1;
        m_hits.push_back(h);
    }

    // 中间页码 "第 X / Y 页"。
    CString pageText;
    pageText.Format(_T("第 %d / %d 页"), m_listPage + 1, pageCount);
    RECT mid = { prev.right, rc.top, next.left, rc.bottom };
    ui::DrawTextLine(hdc, pageText, mid, theme::kInk500,
                     ui::UiFont(theme::kFontSizeSmall, false),
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

// 注:原 PaintPreview(自绘 380 行)已被 AnnouncementPreviewPanel 取代 ——
// 公告详情文字可拖选 + Ctrl+C 复制,装饰图形(徽章 / 头像 / 分隔线 / 进度条 /
// 调度卡图标)由 PreviewPanel::OnPaint 接管。详见 Screens/AnnouncementPreviewPanel.h。

bool AnnouncementsScreen::OnLButtonUp(POINT pt, UINT /*mkFlags*/)
{
    for (const Hit& h : m_hits)
    {
        if (!::PtInRect(&h.m_rc, pt))
        {
            continue;
        }
        if (h.m_kind == 0)            // 选公告 → 切换并请求拉取详情
        {
            if (h.m_annId != m_selectedId)
            {
                m_selectedId    = h.m_annId;
                m_previewScroll = 0;
                // 切换选中后立即把新公告推给详情面板;之后 RaisePending
                // 触发 AdminFrame 异步拉取详情(数据回来后 Refresh 再次刷新)。
                SyncPreviewPanel();
                Invalidate();
                RaisePending(Pending::SelectDetail);
            }
        }
        else if (h.m_kind == 1)       // 切筛选
        {
            if (h.m_arg != m_filter)
            {
                m_filter   = h.m_arg;
                m_listPage = 0;        // 换筛选后回到第 1 页
                Invalidate();
            }
        }
        else if (h.m_kind == 2)       // 动作
        {
            RaisePending(static_cast<Pending>(h.m_arg));
        }
        else if (h.m_kind == 3)       // 分页:m_arg = -1 上一页 / +1 下一页
        {
            const int pageCount = ListPageCount(CollectFiltered(nullptr),
                                                ListPageSize());
            int np = m_listPage + h.m_arg;
            if (np < 0)
            {
                np = 0;
            }
            if (np > pageCount - 1)
            {
                np = pageCount - 1;
            }
            if (np != m_listPage)
            {
                m_listPage = np;
                Invalidate();
            }
        }
        return true;
    }
    return true;
}

bool AnnouncementsScreen::OnMouseWheel(POINT pt, short zDelta, UINT /*mkFlags*/)
{
    // 每格滚轮滚动的像素步长，与预览区保持一致的手感。
    const int kWheelStep = 60;

    if (::PtInRect(&m_previewRc, pt))
    {
        // 注:鼠标落在正文 RichEdit 上时,DUI 宿主(DuiHost::OnMouseWheel)按位置
        // 命中的是正文控件本身,会直接调正文 DuiRichEditHost::OnMouseWheel 滚动
        // 正文,不会冒泡到这里 —— 本分支只处理"预览区内、正文之外"的滚轮。整页
        // 预览滚动(m_previewScroll)目前实际为空转(预览内容为内容自适应高度,
        // 装得下),保留此分支以兼容未来超高预览的整页滚动需求。
        m_previewScroll -= (zDelta / WHEEL_DELTA) * kWheelStep;
        const int viewH = m_previewRc.bottom - m_previewRc.top;
        int maxScroll = m_previewContentH - viewH;
        if (maxScroll < 0)         { maxScroll = 0; }
        if (m_previewScroll < 0)   { m_previewScroll = 0; }
        if (m_previewScroll > maxScroll) { m_previewScroll = maxScroll; }
        m_previewSb->SetMetrics(m_previewContentH, m_previewScroll);
        m_previewSb->Flash();
        Invalidate();
        return true;
    }

    // 鼠标位于左侧列表栏(筛选行之下)时,用滚轮翻页:上滚→上一页,下滚→下一页。
    // 列表改为分页显示后不再整列滚动,故滚轮直接换页。
    RECT listItemsRc = { m_rcItem.left,
                         m_rcItem.top + kHeaderH + kFilterH,
                         m_rcItem.left + kListW,
                         m_rcItem.bottom };
    if (::PtInRect(&listItemsRc, pt))
    {
        const int pageCount = ListPageCount(CollectFiltered(nullptr),
                                            ListPageSize());
        const int dir = (zDelta > 0) ? -1 : 1;   // 上滚上一页 / 下滚下一页
        int np = m_listPage + dir;
        if (np < 0)
        {
            np = 0;
        }
        if (np > pageCount - 1)
        {
            np = pageCount - 1;
        }
        if (np != m_listPage)
        {
            m_listPage = np;
            Invalidate();
        }
    }
    return true;
}

bool AnnouncementsScreen::OnSetCursor(POINT /*pt*/)
{
    ::SetCursor(::LoadCursor(nullptr, IDC_ARROW));
    return true;
}

void AnnouncementsScreen::OnPreviewSbScroll(void* ctx, int newScroll)
{
    // OverlayScrollbar 拖动滑块的回调：把新滚动量写回本界面并触发整界面重绘。
    AnnouncementsScreen* self = static_cast<AnnouncementsScreen*>(ctx);
    self->m_previewScroll = newScroll;
    self->Invalidate();
}

void AnnouncementsScreen::OnListSbScroll(void* ctx, int newScroll)
{
    // 列表悬浮滚动条拖动滑块的回调：把新滚动量写回本界面并触发整界面重绘。
    AnnouncementsScreen* self = static_cast<AnnouncementsScreen*>(ctx);
    self->m_listScroll = newScroll;
    self->Invalidate();
}

} // namespace flamingoadmin
