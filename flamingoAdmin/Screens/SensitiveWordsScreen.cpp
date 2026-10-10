/**
 *  flamingoAdmin —— 敏感词管理界面实现，说明见 SensitiveWordsScreen.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "Screens/SensitiveWordsScreen.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/CommonWidgets.h"
#include "Widgets/ActionTable.h"
#include "Widgets/ColumnHeader.h"
#include "Widgets/PagerBar.h"
#include "Widgets/UiHelpers.h"
#include "Util/SensitiveWordText.h"
#include "Util/TextFileSave.h"
#include "Util/Logger.h"
#include "Util/TimeText.h"   // 时间显示格式（日期 / 分钟 / 秒）

#include "Controls/Basic/DuiButton.h"
#include "Controls/Basic/DuiLabel.h"
#include "Controls/Input/DuiComboBox.h"
#include "Controls/Input/DuiEdit.h"
#include "Controls/Input/DuiSwitch.h"

using namespace balloonwjui;

namespace flamingoadmin {

using Hint = balloonwjui::DuiLayout::Hint;

namespace {

// ---- 布局常量（96 dpi 逻辑像素）----
const int kHeaderH       = 100;  // 页头高度
const int kTabsH         = 32;   // 页签行高度
const int kTabsW         = 200;  // 页签切换控件宽度（两个页签）
const int kSideMargin    = 24;   // 内容区左 / 右 / 下统一外边距
const int kRowGap        = 12;   // 页签行、顶部行、工具行、表格卡之间的间距
const int kBarH          = 36;   // 顶部行与工具行的高度
const int kPickerLabelW  = 40;   // 「企业」标签宽度
const int kPickerW       = 300;  // 企业选择框宽度
const int kUsageW        = 160;  // 「已用 N / 10000」宽度
const int kSwitchLabelW  = 96;   // 「敏感词过滤」标签宽度
const int kSwitchW       = 46;   // 总开关宽度（DuiSwitch 默认尺寸 46×24）
const int kSwitchMarginV = 6;    // 总开关上下外边距：行高 36 减去开关高 24 后上下各 6
const int kSearchW       = 280;  // 搜索框宽度
const int kCategoryW     = 140;  // 分类筛选下拉框宽度
const int kHitRangeW     = 120;  // 拦截记录时间范围下拉框宽度
const int kHitSearchW    = 220;  // 拦截记录发送者搜索框宽度
const int kHitSourceW    = 140;  // 拦截记录来源筛选下拉框宽度
const int kAddButtonW    = 96;   // 「添加」按钮宽度
const int kImportButtonW = 96;   // 「批量导入」按钮宽度
const int kExportButtonW = 72;   // 「导出」按钮宽度
const int kBarGap        = 10;   // 同一行内控件之间的间距
const int kTableHeaderH  = 40;   // 表头高度
const int kRowHeight     = 48;   // 表格行高
const int kPagerH        = 42;   // 分页条高度
const int kCellPad       = 14;   // 单元格左右内边距

// ---- 词库表格的列（像素）----
const int kColWord       = 200;  // 词条
const int kColCategory   = 100;  // 分类
const int kColCreator    = 120;  // 添加人
const int kColTime       = 150;  // 添加时间
const int kColActions    = 140;  // 操作（编辑 / 删除）

// ---- 拦截记录表格的列（像素）----
const int kColHitTime     = 170;  // 时间
const int kColHitSender   = 120;  // 发送者
const int kColHitSource   = 100;  // 来源
const int kColHitTarget   = 150;  // 会话
const int kColHitWord     = 110;  // 命中词
const int kColHitCategory = 90;   // 分类

// 时间范围对应的天数，下标为 HitRange
const int kHitRangeDays[] = { 7, 30, 180 };

// 一天的毫秒数
const long long kDayMs = 24LL * 60 * 60 * 1000;

// 词库表格的列下标，与 BuildWordColumns 的顺序一致
enum WordColumn
{
    kWcWord     = 0,   // 词条
    kWcCategory = 1,   // 分类
    kWcRemark   = 2,   // 备注（占满剩余宽度）
    kWcCreator  = 3,   // 添加人
    kWcTime     = 4,   // 添加时间
    kWcActions  = 5,   // 操作
};

// 行内操作的下标，与 AddAction 的顺序一致
enum RowAction
{
    kActionEdit   = 0,   // 编辑
    kActionDelete = 1,   // 删除
};

// 企业选择框：拉企业列表时的状态筛选（-1 表示全部状态）与条数（一次拉 1000 条，平台企业数远小于此）
const int kEntStatusAll     = -1;
const int kEntPickerPageMax = 1000;

// 搜索防抖：停止输入后多久才查询（毫秒）
const UINT kSearchDebounceMs = 400;

// Unix 毫秒换算成 FILETIME（自 1601-01-01 起的 100 纳秒数）所需的常量
const long long kEpochDiff100ns = 116444736000000000LL;  // 1601-01-01 到 1970-01-01 的 100 纳秒数
const long long k100nsPerMs     = 10000;                 // 一毫秒的 100 纳秒数

// 把 Unix 毫秒格式化成本地时间「YYYY-MM-DD HH:MM」；不大于 0 时显示「-」。
CString FormatTimeMs(long long ms)
{
    if (ms <= 0)
        return CString(_T("-"));
    ULARGE_INTEGER u;
    u.QuadPart = static_cast<ULONGLONG>(ms) * k100nsPerMs + kEpochDiff100ns;
    FILETIME utc;
    utc.dwLowDateTime = u.LowPart;
    utc.dwHighDateTime = u.HighPart;
    FILETIME local;
    SYSTEMTIME st;
    if (!::FileTimeToLocalFileTime(&utc, &local) || !::FileTimeToSystemTime(&local, &st))
        return CString(_T("-"));
    // 词条列表里的时间显示到分钟。
    return timetext::FormatSystemTime(st, timetext::TimeStyle::Minute);
}

// 词库表格的列定义
std::vector<ui::ColumnSpec> BuildWordColumns()
{
    std::vector<ui::ColumnSpec> cols;
    cols.push_back(ui::ColumnSpec(_T("词条"), kColWord));
    cols.push_back(ui::ColumnSpec(_T("分类"), kColCategory));
    cols.push_back(ui::ColumnSpec(_T("备注"), 0, true));
    cols.push_back(ui::ColumnSpec(_T("添加人"), kColCreator));
    cols.push_back(ui::ColumnSpec(_T("添加时间"), kColTime));
    cols.push_back(ui::ColumnSpec(_T("操作"), kColActions));
    return cols;
}

// 拦截记录表格的列定义，顺序与 SensitiveWordsScreen::HitColumn 一致
std::vector<ui::ColumnSpec> BuildHitColumns()
{
    std::vector<ui::ColumnSpec> cols;
    cols.push_back(ui::ColumnSpec(_T("时间"), kColHitTime));
    cols.push_back(ui::ColumnSpec(_T("发送者"), kColHitSender));
    cols.push_back(ui::ColumnSpec(_T("来源"), kColHitSource));
    cols.push_back(ui::ColumnSpec(_T("会话"), kColHitTarget));
    cols.push_back(ui::ColumnSpec(_T("命中词"), kColHitWord));
    cols.push_back(ui::ColumnSpec(_T("分类"), kColHitCategory));
    cols.push_back(ui::ColumnSpec(_T("内容摘要"), 0, true));
    return cols;
}

// 一张白色圆角卡片
std::unique_ptr<DuiVBox> MakeCard()
{
    auto card = std::make_unique<DuiVBox>();
    card->SetBgColor(theme::kInk0);
    card->SetCornerRadius(theme::kRadiusLg);
    card->SetBorderColor(theme::kInk100);
    card->SetBorderWidth(1.0f);
    return card;
}

// 一个单行文字标签
std::unique_ptr<DuiLabel> MakeLabel(LPCTSTR text, COLORREF color, DWORD align)
{
    auto label = std::make_unique<DuiLabel>();
    label->SetText(text);
    label->SetTextColor(color);
    label->SetTextPointSize(theme::kFontSizeNormal);
    label->SetTextAlign(align | DT_VCENTER | DT_SINGLELINE);
    return label;
}

} // namespace

SensitiveWordsScreen::SensitiveWordsScreen()
    : m_hostWnd(nullptr)
    , m_sink(nullptr)
    , m_sinkUser(nullptr)
    , m_uiHandler(nullptr)
    , m_uiHandlerUser(nullptr)
    , m_tabs(nullptr)
    , m_wordsPanel(nullptr)
    , m_hitsPanel(nullptr)
    , m_pickerBox(nullptr)
    , m_picker(nullptr)
    , m_usage(nullptr)
    , m_switch(nullptr)
    , m_search(nullptr)
    , m_category(nullptr)
    , m_addButton(nullptr)
    , m_importButton(nullptr)
    , m_exportButton(nullptr)
    , m_wordHeader(nullptr)
    , m_wordTable(nullptr)
    , m_pager(nullptr)
    , m_hitTable(nullptr)
    , m_hitHeader(nullptr)
    , m_hitPager(nullptr)
    , m_hitSearch(nullptr)
    , m_hitRangeBox(nullptr)
    , m_hitSourceBox(nullptr)
    , m_hitCategoryBox(nullptr)
    , m_activeTab(kTabWords)
    , m_isPlatform(false)
    , m_enterpriseId(0)
    , m_loadState(kLoadIdle)
    , m_reloadPending(false)
    , m_categoryFilter(0)
    , m_page(1)
    , m_total(0)
    , m_wordCount(0)
    , m_enabled(false)
    , m_requestedEnabled(false)
    , m_pendingDeleteId(0)
    , m_saving(false)
    , m_scrollTopOnLoad(false)
    , m_importIndex(0)
    , m_importCategory(swtext::kCategoryOther)
    , m_importAdded(0)
    , m_importing(false)
    , m_exporting(false)
    , m_hitTotal(0)
    , m_hitPage(1)
    , m_hitLoadState(kLoadIdle)
    , m_hitReloadPending(false)
    , m_hitRange(kHitRange7Days)
    , m_hitSource(0)
    , m_hitCategory(0)
{
    // ---- 页头 ----
    auto header = std::make_unique<ui::PageHeader>();
    header->SetTexts(_T("敏感词"), _T("敏感词管理"), _T("维护企业的敏感词库，查看服务端拦下的内容"));
    AddChild(std::move(header), Hint().Fixed(kHeaderH));

    // ---- 页签行：[词库 | 拦截记录] …… [企业 选择框]（仅平台超管；两个页签共用）----
    auto tabRow = std::make_unique<DuiHBox>();
    auto tabs = std::make_unique<ui::SegControl>();
    tabs->SetCtrlId(kIdSwTabs);
    tabs->AddSegment(_T("词库"));
    tabs->AddSegment(_T("拦截记录"));
    m_tabs = tabs.get();
    //页签控件比这一行矮，上下各留一半差值，与右侧的企业选择框垂直居中对齐
    tabRow->AddChild(std::move(tabs), Hint().Fixed(kTabsW).Margin(0, (kBarH - kTabsH) / 2, 0, (kBarH - kTabsH) / 2));
    tabRow->AddChild(std::make_unique<DuiControl>(), Hint().Weight(1));
    {
        auto pickerBox = std::make_unique<DuiHBox>();
        pickerBox->AddChild(MakeLabel(_T("企业"), theme::kInk500, DT_LEFT), Hint().Fixed(kPickerLabelW));
        auto picker = std::make_unique<DuiComboBox>();
        picker->SetCtrlId(kIdSwEnterprise);
        //企业多时可以输入文字筛选
        picker->SetEditable(true);
        picker->SetIncrementalSearch(true);
        picker->SetIncrementalSearchSubstring(true);
        m_picker = picker.get();
        pickerBox->AddChild(std::move(picker), Hint().Fixed(kPickerW));
        m_pickerBox = pickerBox.get();
        tabRow->AddChild(std::move(pickerBox), Hint().Fixed(kPickerLabelW + kPickerW));
    }
    AddChild(std::move(tabRow), Hint().Fixed(kBarH).Margin(kSideMargin, 0, kSideMargin, kRowGap));

    // ---- 「词库」页签 ----
    auto words = std::make_unique<DuiVBox>();

    //顶部行：……[已用 N / 10000] [敏感词过滤 开关]
    auto topRow = std::make_unique<DuiHBox>();
    topRow->SetGap(kBarGap);
    topRow->AddChild(std::make_unique<DuiControl>(), Hint().Weight(1));
    {
        auto usage = MakeLabel(_T(""), theme::kInk500, DT_RIGHT);
        m_usage = usage.get();
        topRow->AddChild(std::move(usage), Hint().Fixed(kUsageW));
        topRow->AddChild(MakeLabel(_T("敏感词过滤"), theme::kInk600, DT_RIGHT), Hint().Fixed(kSwitchLabelW));
        auto sw = std::make_unique<DuiSwitch>();
        sw->SetCtrlId(kIdSwSwitch);
        m_switch = sw.get();
        topRow->AddChild(std::move(sw), Hint().Fixed(kSwitchW).Margin(0, kSwitchMarginV, 0, kSwitchMarginV));
    }
    words->AddChild(std::move(topRow), Hint().Fixed(kBarH).Margin(0, 0, 0, kRowGap));

    //工具行：[搜索] [分类] …… [添加]
    auto toolRow = std::make_unique<DuiHBox>();
    toolRow->SetGap(kBarGap);
    {
        auto search = std::make_unique<DuiEdit>();
        search->SetCtrlId(kIdSwSearch);
        search->SetPlaceholder(_T("搜索词条"));
        search->SetBgColor(theme::kInk25);
        search->SetShowBorder(true);
        m_search = search.get();
        toolRow->AddChild(std::move(search), Hint().Fixed(kSearchW));

        auto category = std::make_unique<DuiComboBox>();
        category->SetCtrlId(kIdSwCategory);
        category->AddString(_T("全部分类"));
        for (int c = 1; c <= swtext::kCategoryCount; ++c)
            category->AddString(swtext::CategoryName(c));
        category->SetCurSel(0, false);
        m_category = category.get();
        toolRow->AddChild(std::move(category), Hint().Fixed(kCategoryW));

        toolRow->AddChild(std::make_unique<DuiControl>(), Hint().Weight(1));

        auto importButton = std::make_unique<DuiButton>();
        importButton->SetCtrlId(kIdSwImport);
        importButton->SetText(_T("批量导入"));
        importButton->SetVariant(DuiButton::Variant::Default);
        importButton->SetTextPointSize(theme::kFontSizeNormal);
        m_importButton = importButton.get();
        toolRow->AddChild(std::move(importButton), Hint().Fixed(kImportButtonW));

        auto exportButton = std::make_unique<DuiButton>();
        exportButton->SetCtrlId(kIdSwExport);
        exportButton->SetText(_T("导出"));
        exportButton->SetVariant(DuiButton::Variant::Default);
        exportButton->SetTextPointSize(theme::kFontSizeNormal);
        m_exportButton = exportButton.get();
        toolRow->AddChild(std::move(exportButton), Hint().Fixed(kExportButtonW));

        auto add = std::make_unique<DuiButton>();
        add->SetCtrlId(kIdSwAdd);
        add->SetText(_T("添加"));
        add->SetVariant(DuiButton::Variant::Primary);
        add->SetTextPointSize(theme::kFontSizeNormal);
        m_addButton = add.get();
        toolRow->AddChild(std::move(add), Hint().Fixed(kAddButtonW));
    }
    words->AddChild(std::move(toolRow), Hint().Fixed(kBarH).Margin(0, 0, 0, kRowGap));

    //表格卡：表头 + 表格 + 分页条
    auto card = MakeCard();
    {
        auto head = std::make_unique<ui::ColumnHeader>();
        head->SetColumns(BuildWordColumns());
        head->SetCellPadding(kCellPad);
        m_wordHeader = head.get();
        card->AddChild(std::move(head), Hint().Fixed(kTableHeaderH));

        auto table = std::make_unique<ui::ActionTable>();
        table->SetCtrlId(kIdSwWordTable);
        table->SetRowHeight(kRowHeight);
        table->SetRowPainter(&SensitiveWordsScreen::PaintRowThunk, this);
        table->SetActionAreaWidth(kColActions);
        table->AddAction(_T("编辑"), theme::kColorInfo);
        table->AddAction(_T("删除"), theme::kColorDanger);
        table->SetActionCallback(&SensitiveWordsScreen::OnRowActionThunk, this);
        m_wordTable = table.get();
        card->AddChild(std::move(table), Hint().Weight(1));

        auto pager = std::make_unique<ui::PagerBar>();
        pager->SetSidePadding(kCellPad);
        pager->SetPageCallback(&SensitiveWordsScreen::OnPageThunk, this);
        m_pager = pager.get();
        card->AddChild(std::move(pager), Hint().Fixed(kPagerH));
    }
    words->AddChild(std::move(card), Hint().Weight(1));

    m_wordsPanel = words.get();
    AddChild(std::move(words), Hint().Weight(1).Margin(kSideMargin, 0, kSideMargin, kSideMargin));

    // ---- 「拦截记录」页签：筛选行 + 表格卡（只读）----
    auto hits = std::make_unique<DuiVBox>();

    //筛选行：[时间范围] [发送者搜索] [来源] [分类] ……
    auto filterRow = std::make_unique<DuiHBox>();
    filterRow->SetGap(kBarGap);
    {
        auto range = std::make_unique<DuiComboBox>();
        range->SetCtrlId(kIdSwHitRange);
        for (int i = kHitRange7Days; i <= kHitRange180Days; ++i)
        {
            CString label;
            label.Format(_T("近 %d 天"), kHitRangeDays[i]);
            range->AddString(label);
        }
        range->SetCurSel(kHitRange7Days, false);
        m_hitRangeBox = range.get();
        filterRow->AddChild(std::move(range), Hint().Fixed(kHitRangeW));

        auto search = std::make_unique<DuiEdit>();
        search->SetCtrlId(kIdSwHitSearch);
        search->SetPlaceholder(_T("搜索发送者"));
        search->SetBgColor(theme::kInk25);
        search->SetShowBorder(true);
        m_hitSearch = search.get();
        filterRow->AddChild(std::move(search), Hint().Fixed(kHitSearchW));

        auto source = std::make_unique<DuiComboBox>();
        source->SetCtrlId(kIdSwHitSource);
        source->AddString(_T("全部来源"));
        for (int s = 1; s <= swtext::kHitSourceCount; ++s)
            source->AddString(swtext::HitSourceName(s));
        source->SetCurSel(0, false);
        m_hitSourceBox = source.get();
        filterRow->AddChild(std::move(source), Hint().Fixed(kHitSourceW));

        auto category = std::make_unique<DuiComboBox>();
        category->SetCtrlId(kIdSwHitCategory);
        category->AddString(_T("全部分类"));
        for (int c = 1; c <= swtext::kCategoryCount; ++c)
            category->AddString(swtext::CategoryName(c));
        category->SetCurSel(0, false);
        m_hitCategoryBox = category.get();
        filterRow->AddChild(std::move(category), Hint().Fixed(kCategoryW));

        filterRow->AddChild(std::make_unique<DuiControl>(), Hint().Weight(1));
    }
    hits->AddChild(std::move(filterRow), Hint().Fixed(kBarH).Margin(0, 0, 0, kRowGap));

    auto hitCard = MakeCard();
    {
        auto head = std::make_unique<ui::ColumnHeader>();
        head->SetColumns(BuildHitColumns());
        head->SetCellPadding(kCellPad);
        m_hitHeader = head.get();
        hitCard->AddChild(std::move(head), Hint().Fixed(kTableHeaderH));

        auto table = std::make_unique<ui::ActionTable>();
        table->SetCtrlId(kIdSwHitTable);
        table->SetRowHeight(kRowHeight);
        table->SetRowPainter(&SensitiveWordsScreen::PaintHitRowThunk, this);
        //内容摘要过长时截断，悬停显示全文
        table->SetTooltipProvider(&SensitiveWordsScreen::HitTooltipThunk, this);
        m_hitTable = table.get();
        hitCard->AddChild(std::move(table), Hint().Weight(1));

        auto pager = std::make_unique<ui::PagerBar>();
        pager->SetSidePadding(kCellPad);
        pager->SetPageCallback(&SensitiveWordsScreen::OnHitPageThunk, this);
        m_hitPager = pager.get();
        hitCard->AddChild(std::move(pager), Hint().Fixed(kPagerH));
    }
    hits->AddChild(std::move(hitCard), Hint().Weight(1));
    m_hitsPanel = hits.get();
    AddChild(std::move(hits), Hint().Weight(1).Margin(kSideMargin, 0, kSideMargin, kSideMargin));

    ApplyTabVisibility();
    UpdateView();
    UpdateHitView();
}

void SensitiveWordsScreen::SetRequestSink(RequestSinkFn fn, void* user)
{
    m_sink = fn;
    m_sinkUser = user;
}

void SensitiveWordsScreen::SetUiRequestHandler(UiRequestFn fn, void* user)
{
    m_uiHandler = fn;
    m_uiHandlerUser = user;
}

// ---------------------------------------------------------------------------
// 进入界面与身份
// ---------------------------------------------------------------------------

void SensitiveWordsScreen::OnEnter()
{
    const AdminSession& session = AdminSession::Instance();
    //换了登录身份：清掉上一个身份留下的企业列表与词库
    if (session.Token() != m_sessionToken)
    {
        ResetState();
        m_sessionToken = session.Token();
    }
    m_isPlatform = session.IsPlatformAdmin();

    if (m_pickerBox != nullptr)
    {
        m_pickerBox->SetVisible(m_isPlatform);
        //SetVisible 不会重新排版：对所在的顶部行重新排一次（开发约定文档 UI 约定第 5 条）
        DuiControl* row = m_pickerBox->GetParent();
        if (row != nullptr)
            row->ForceLayout(row->GetRect());
    }

    FA_LOGI(_T("enter sensitive words screen, platform admin: %d, enterprise: %lld"),
            m_isPlatform ? 1 : 0, m_enterpriseId);

    if (m_isPlatform && m_enterpriseIds.empty())
        StartLoadEnterprises();
    //每次进入都刷新：别的管理员可能刚改过词库；平台超管未选企业时 StartLoadWords 不发请求
    StartLoadWords();
    UpdateView();
}

void SensitiveWordsScreen::ResetState()
{
    m_enterpriseIds.clear();
    m_enterpriseNames.clear();
    m_importBatches.clear();
    m_importSkips.clear();
    m_importIndex = 0;
    m_importAdded = 0;
    m_importing = false;
    m_exporting = false;
    m_hits.clear();
    m_hitTotal = 0;
    m_hitPage = 1;
    m_hitLoadState = kLoadIdle;
    m_hitLoadError.Empty();
    m_hitReloadPending = false;
    m_hitRange = kHitRange7Days;
    m_hitKeyword.Empty();
    m_hitSource = 0;
    m_hitCategory = 0;
    if (m_hitRangeBox != nullptr)
        m_hitRangeBox->SetCurSel(kHitRange7Days, false);
    if (m_hitSearch != nullptr)
        m_hitSearch->SetText(_T(""));
    if (m_hitSourceBox != nullptr)
        m_hitSourceBox->SetCurSel(0, false);
    if (m_hitCategoryBox != nullptr)
        m_hitCategoryBox->SetCurSel(0, false);
    m_enterpriseId = 0;
    if (m_picker != nullptr)
    {
        m_picker->ResetContent();
        m_picker->SetText(_T(""));
    }
    m_words.clear();
    m_loadState = kLoadIdle;
    m_loadError.Empty();
    m_reloadPending = false;
    m_keyword.Empty();
    m_categoryFilter = 0;
    if (m_category != nullptr)
        m_category->SetCurSel(0, false);
    m_page = 1;
    m_total = 0;
    m_wordCount = 0;
    m_enabled = false;
    if (m_switch != nullptr)
        m_switch->SetChecked(false, false, false);
    m_pendingDeleteId = 0;
    m_saving = false;
    m_requestedQuery = WordQuery();
    m_shownQuery = WordQuery();
    m_scrollTopOnLoad = false;
}

// ---------------------------------------------------------------------------
// 发请求
// ---------------------------------------------------------------------------

void SensitiveWordsScreen::Send(HttpRequest& request, ApiTag tag)
{
    request.m_tag = tag;
    if (m_sink == nullptr)
    {
        FA_LOGW(_T("sensitive words screen: no request sink, request dropped, tag: %d"), static_cast<int>(tag));
        return;
    }
    m_sink(m_sinkUser, request);
}

void SensitiveWordsScreen::StartLoadEnterprises()
{
    HttpRequest request = adminapi::MakeEnterpriseListRequest(ServerConfig(), AdminSession::Instance().Token(),
                                                              CString(), kEntStatusAll, 1, kEntPickerPageMax);
    Send(request, kApiTagSwEnterprises);
}

void SensitiveWordsScreen::StartLoadWords()
{
    //平台超管未选企业时不知道该查哪个企业
    if (m_isPlatform && m_enterpriseId <= 0)
        return;
    //上一次查询还没回来：记下来，结果回来后按最新的条件再查一次
    if (m_loadState == kLoadLoading)
    {
        m_reloadPending = true;
        return;
    }
    m_loadState = kLoadLoading;
    m_reloadPending = false;
    m_requestedQuery.m_enterpriseId = m_enterpriseId;
    m_requestedQuery.m_keyword = m_keyword;
    m_requestedQuery.m_category = m_categoryFilter;
    m_requestedQuery.m_page = m_page;
    HttpRequest request = adminapi::MakeSensitiveListRequest(ServerConfig(), AdminSession::Instance().Token(),
                                                             m_enterpriseId, m_keyword, m_categoryFilter,
                                                             m_page, kPageSize);
    Send(request, kApiTagSwList);
    UpdateView();
}

// ---------------------------------------------------------------------------
// 应答
// ---------------------------------------------------------------------------

void SensitiveWordsScreen::OnApiResult(ApiTag tag, const HttpResult& http)
{
    switch (tag)
    {
    //企业选择框的企业列表
    case kApiTagSwEnterprises:
        OnEnterprisesLoaded(http);
        break;
    //词库列表
    case kApiTagSwList:
        OnWordsLoaded(http);
        break;
    //添加词条
    case kApiTagSwAdd:
        OnWordSaved(http, true);
        break;
    //修改词条
    case kApiTagSwUpdate:
        OnWordSaved(http, false);
        break;
    //拦截记录列表
    case kApiTagSwHits:
        OnHitsLoaded(http);
        break;
    //批量导入的一批
    case kApiTagSwImport:
        OnImportBatchDone(http);
        break;
    //导出
    case kApiTagSwExport:
        OnExportDone(http);
        break;
    //删除词条
    case kApiTagSwDelete:
        OnWordsDeleted(http);
        break;
    //总开关
    case kApiTagSwSetEnabled:
        OnEnabledSet(http);
        break;
    //其余标签（拦截记录等）由后续步骤处理
    default:
        break;
    }
}

void SensitiveWordsScreen::OnEnterprisesLoaded(const HttpResult& http)
{
    const EnterpriseListResult r = adminapi::ParseEnterpriseListResponse(http);
    if (!r.Succeeded())
    {
        FA_LOGW(_T("sensitive words screen: load enterprises failed, code: %d"), r.m_code);
        RaiseToast(_T("企业列表加载失败：") + r.m_message, true);
        return;
    }

    m_enterpriseIds.clear();
    m_enterpriseNames.clear();
    if (m_picker != nullptr)
        m_picker->ResetContent();
    int selected = -1;
    for (size_t i = 0; i < r.m_list.size(); ++i)
    {
        const long long id = _ttoi64(r.m_list[i].m_id);
        m_enterpriseIds.push_back(id);
        m_enterpriseNames.push_back(r.m_list[i].m_name);
        if (m_picker != nullptr)
            m_picker->AddString(r.m_list[i].m_name);
        if (id == m_enterpriseId)
            selected = static_cast<int>(i);
    }
    if (m_picker != nullptr && selected >= 0)
        m_picker->SetCurSel(selected, false);
    UpdateView();
}

void SensitiveWordsScreen::OnWordsLoaded(const HttpResult& http)
{
    const adminapi::SensitiveWordListResult r = adminapi::ParseSensitiveListResponse(http);

    //加载期间查询条件又变了：这份结果已经过时，丢弃并按最新条件重查
    if (m_reloadPending)
    {
        m_reloadPending = false;
        m_loadState = kLoadIdle;
        StartLoadWords();
        return;
    }

    if (!r.Succeeded())
    {
        m_loadState = kLoadFailed;
        m_loadError = r.m_message;
        m_words.clear();
        m_shownQuery = WordQuery();
        FA_LOGW(_T("sensitive words screen: load words failed, code: %d"), r.m_code);
        UpdateView();
        return;
    }

    m_loadState = kLoadLoaded;
    m_loadError.Empty();
    m_words = r.m_list;
    m_total = r.m_total;
    m_wordCount = r.m_wordCount;
    m_enabled = r.m_enabled;
    if (m_switch != nullptr)
        m_switch->SetChecked(m_enabled, false, false);

    //删掉最后一页的全部词条后，当前页可能超出总页数：退到最后一页重查
    if (m_words.empty() && m_total > 0 && m_page > PageCount())
    {
        m_page = PageCount();
        StartLoadWords();
        return;
    }

    //查询条件变了（换页、换企业、搜索、筛选）或刚添加了词条时从第一行看起；同样条件的重新加载
    //（删除、修改之后）保持原来的滚动位置，管理员不必再翻回刚才操作的那一行
    const bool scrollTop = m_scrollTopOnLoad || !m_shownQuery.SameAs(m_requestedQuery);
    m_shownQuery = m_requestedQuery;
    m_scrollTopOnLoad = false;
    UpdateView();
    if (scrollTop && m_wordTable != nullptr)
        m_wordTable->SetScrollPos(0);
}

void SensitiveWordsScreen::OnWordSaved(const HttpResult& http, bool isAdd)
{
    m_saving = false;
    bool ok = false;
    CString message;
    if (isAdd)
    {
        const adminapi::SensitiveWordAddResult r = adminapi::ParseSensitiveAddResponse(http);
        ok = r.Succeeded();
        message = r.m_message;
    }
    else
    {
        const EnterpriseActionResult r = adminapi::ParseEnterpriseActionResponse(http);
        ok = r.Succeeded();
        message = r.m_message;
    }

    if (!ok)
    {
        //失败（包括词条为空、超长、重复、词库已满、词条不存在）都在弹窗里提示，弹窗不关，管理员改了再提交
        UiRequest req;
        req.m_kind = kUiModalError;
        req.m_text = message;
        Raise(req);
        return;
    }

    UiRequest close;
    close.m_kind = kUiCloseModal;
    Raise(close);
    RaiseToast(isAdd ? _T("已添加敏感词") : _T("已保存修改"), false);
    //列表按添加时间倒序，新词条在第 1 页最前面：添加成功后回到那里，管理员能马上看到它
    if (isAdd)
    {
        m_page = 1;
        m_scrollTopOnLoad = true;
    }
    StartLoadWords();
}

void SensitiveWordsScreen::OnWordsDeleted(const HttpResult& http)
{
    const adminapi::SensitiveWordDeleteResult r = adminapi::ParseSensitiveDeleteResponse(http);
    if (!r.Succeeded())
    {
        RaiseToast(_T("删除失败：") + r.m_message, true);
        return;
    }
    RaiseToast(_T("已删除敏感词"), false);
    StartLoadWords();
}

void SensitiveWordsScreen::OnEnabledSet(const HttpResult& http)
{
    const EnterpriseActionResult r = adminapi::ParseEnterpriseActionResponse(http);
    if (!r.Succeeded())
    {
        //失败：开关拨回服务端的状态
        if (m_switch != nullptr)
            m_switch->SetChecked(m_enabled, false, false);
        RaiseToast(_T("切换失败：") + r.m_message, true);
        return;
    }
    m_enabled = m_requestedEnabled;
    RaiseToast(m_enabled ? _T("已启用敏感词过滤") : _T("已暂停敏感词过滤"), false);
}

// ---------------------------------------------------------------------------
// 用户操作
// ---------------------------------------------------------------------------

void SensitiveWordsScreen::OnTabChanged(int index)
{
    if (index != kTabWords && index != kTabHits)
        return;
    if (index == m_activeTab)
        return;
    m_activeTab = index;
    ApplyTabVisibility();
    //拦截记录随时在增加：每次切到这一页签都从第 1 页重新拉取
    if (m_activeTab == kTabHits)
    {
        m_hitPage = 1;
        StartLoadHits();
        UpdateHitView();
    }
}

void SensitiveWordsScreen::OnEnterpriseSelected(int index)
{
    if (index < 0 || index >= static_cast<int>(m_enterpriseIds.size()))
        return;
    const long long id = m_enterpriseIds[static_cast<size_t>(index)];
    if (id == m_enterpriseId)
        return;

    m_enterpriseId = id;
    m_words.clear();
    m_page = 1;
    m_total = 0;
    m_wordCount = 0;
    FA_LOGI(_T("sensitive words screen: enterprise selected: %lld"), id);
    StartLoadWords();
    UpdateView();

    //拦截记录同样按企业划分：清掉旧企业的记录；停在拦截记录页签时立即按新企业拉取
    m_hits.clear();
    m_hitTotal = 0;
    m_hitPage = 1;
    if (m_activeTab == kTabHits)
        StartLoadHits();
    UpdateHitView();
}

void SensitiveWordsScreen::OnSearchChanged()
{
    //停止输入一小段时间后再查，免得每敲一个字发一次请求
    if (m_hostWnd != nullptr && ::IsWindow(m_hostWnd))
    {
        ::SetTimer(m_hostWnd, kSearchTimerId, kSearchDebounceMs, nullptr);
        return;
    }
    OnSearchTimer();
}

void SensitiveWordsScreen::OnSearchTimer()
{
    if (m_hostWnd != nullptr && ::IsWindow(m_hostWnd))
        ::KillTimer(m_hostWnd, kSearchTimerId);
    if (m_search == nullptr)
        return;
    CString keyword = m_search->GetText();
    keyword.Trim();
    if (keyword == m_keyword)
        return;
    m_keyword = keyword;
    m_page = 1;
    StartLoadWords();
}

void SensitiveWordsScreen::OnCategoryChanged(int index)
{
    if (index < 0 || index > swtext::kCategoryCount)
        return;
    if (index == m_categoryFilter)
        return;
    m_categoryFilter = index;
    m_page = 1;
    StartLoadWords();
}

void SensitiveWordsScreen::OnSwitchToggled(bool on)
{
    if (!IsToolbarEnabled())
        return;
    if (on)
    {
        m_requestedEnabled = true;
        HttpRequest request = adminapi::MakeSensitiveSetEnabledRequest(ServerConfig(),
                                                                       AdminSession::Instance().Token(),
                                                                       m_enterpriseId, true);
        Send(request, kApiTagSwSetEnabled);
        return;
    }
    //关闭会让该企业的敏感词检查全部失效，先请宿主弹确认框；开关此刻已拨到关，取消时再拨回
    UiRequest req;
    req.m_kind = kUiConfirmDisable;
    Raise(req);
}

void SensitiveWordsScreen::OnAddClicked()
{
    if (!IsToolbarEnabled())
        return;
    UiRequest req;
    req.m_kind = kUiOpenAddModal;
    Raise(req);
}

void SensitiveWordsScreen::OnConfirmResult(int kind, bool confirmed)
{
    switch (kind)
    {
    //关闭总开关的确认
    case kUiConfirmDisable:
        if (!confirmed)
        {
            if (m_switch != nullptr)
                m_switch->SetChecked(m_enabled, false, false);
            return;
        }
        {
            m_requestedEnabled = false;
            HttpRequest request = adminapi::MakeSensitiveSetEnabledRequest(ServerConfig(),
                                                                           AdminSession::Instance().Token(),
                                                                           m_enterpriseId, false);
            Send(request, kApiTagSwSetEnabled);
        }
        break;

    //删除词条的确认
    case kUiConfirmDelete:
        if (confirmed && m_pendingDeleteId > 0)
        {
            std::vector<long long> ids;
            ids.push_back(m_pendingDeleteId);
            HttpRequest request = adminapi::MakeSensitiveDeleteRequest(ServerConfig(),
                                                                       AdminSession::Instance().Token(),
                                                                       m_enterpriseId, ids);
            Send(request, kApiTagSwDelete);
        }
        m_pendingDeleteId = 0;
        break;

    //其它种类没有确认框
    default:
        break;
    }
}

void SensitiveWordsScreen::SubmitWord(long long id, const CString& word, int category, const CString& remark)
{
    //上一次提交还没回来：忽略，防止连点重复提交
    if (m_saving)
        return;
    m_saving = true;
    if (id <= 0)
    {
        HttpRequest request = adminapi::MakeSensitiveAddRequest(ServerConfig(), AdminSession::Instance().Token(),
                                                                m_enterpriseId, word, category, remark);
        Send(request, kApiTagSwAdd);
        return;
    }
    HttpRequest request = adminapi::MakeSensitiveUpdateRequest(ServerConfig(), AdminSession::Instance().Token(),
                                                               m_enterpriseId, id, word, category, remark);
    Send(request, kApiTagSwUpdate);
}

void SensitiveWordsScreen::OnImportClicked()
{
    //平台超管未选企业时按钮本就不可用，这里再判一次
    if (m_isPlatform && m_enterpriseId <= 0)
        return;
    UiRequest req;
    req.m_kind = kUiOpenImportModal;
    Raise(req);
}

bool SensitiveWordsScreen::StartImport(const CString& text, int defaultCategory, CString* error)
{
    CString reason;
    std::vector<swtext::ImportBatch> batches;
    if (m_isPlatform && m_enterpriseId <= 0)
    {
        reason = _T("请先选择企业");
    }
    else if (m_importing)
    {
        reason = _T("上一次导入尚未结束");
    }
    else
    {
        //全是空白行的批次 MakeImportBatches 不会产生，一批都没有就是没有可导入的内容
        const std::vector<std::string> lines = swtext::SplitLines(text);
        if (swtext::CountNonBlankLines(lines) > 0)
            batches = swtext::MakeImportBatches(lines, swtext::kImportBatchMaxLines);
        if (batches.empty())
            reason = _T("没有可导入的内容");
    }
    if (error != nullptr)
        *error = reason;
    if (!reason.IsEmpty())
        return false;

    m_importBatches.swap(batches);
    m_importIndex = 0;
    m_importCategory = (defaultCategory >= 1 && defaultCategory <= swtext::kCategoryCount)
                           ? defaultCategory : swtext::kCategoryOther;
    m_importAdded = 0;
    m_importSkips.clear();
    m_importing = true;
    FA_LOGI(_T("sensitive words screen: import started, batches: %d, default category: %d"),
            static_cast<int>(m_importBatches.size()), m_importCategory);
    SendImportBatch();
    return true;
}

void SensitiveWordsScreen::OnExportClicked()
{
    if (m_isPlatform && m_enterpriseId <= 0)
        return;
    //上一次导出还没返回：不重复请求
    if (m_exporting)
        return;
    m_exporting = true;
    HttpRequest request = adminapi::MakeSensitiveExportRequest(ServerConfig(), AdminSession::Instance().Token(),
                                                               m_enterpriseId);
    Send(request, kApiTagSwExport);
    RaiseToast(_T("正在导出…"), false);
}

CString SensitiveWordsScreen::BuildImportSummary(int added, const std::vector<ImportSkipLine>& skips,
                                                 const CString& failure, int failedBatch, int batchCount)
{
    const int skipped = static_cast<int>(skips.size());
    CString text;
    if (failure.IsEmpty())
    {
        text.Format(_T("导入完成：成功 %d 条，跳过 %d 条。"), added, skipped);
    }
    else
    {
        //失败原因自带的句号去掉，免得与后面的句号连在一起
        CString reason = failure;
        reason.TrimRight(_T("。."));
        text.Format(_T("第 %d / %d 批导入失败：%s。在此之前已成功导入 %d 条，跳过 %d 条。"),
                    failedBatch, batchCount, reason.GetString(), added, skipped);
    }
    if (skips.empty())
        return text;

    text += _T("\r\n被跳过的行：");
    const int shown = (skipped < kImportSummaryMaxLines) ? skipped : kImportSummaryMaxLines;
    for (int i = 0; i < shown; ++i)
    {
        const ImportSkipLine& s = skips[static_cast<size_t>(i)];
        CString line;
        line.Format(_T("\r\n第 %d 行「%s」：%s"), s.m_line, s.m_word.GetString(), s.m_reason.GetString());
        text += line;
    }
    if (skipped > shown)
    {
        CString more;
        more.Format(_T("\r\n……另有 %d 行未列出"), skipped - shown);
        text += more;
    }
    return text;
}

void SensitiveWordsScreen::OnImportBatchDone(const HttpResult& http)
{
    //不在导入中（例如换了登录身份之后才到的应答）：丢弃
    if (!m_importing || m_importIndex >= m_importBatches.size())
        return;

    const adminapi::SensitiveImportResult r = adminapi::ParseSensitiveImportResponse(http);
    if (!r.Succeeded())
    {
        FA_LOGW(_T("sensitive words screen: import batch %d of %d failed, code: %d"),
                static_cast<int>(m_importIndex) + 1, static_cast<int>(m_importBatches.size()), r.m_code);
        FinishImport(r.m_message.IsEmpty() ? CString(_T("未知错误")) : r.m_message);
        return;
    }

    //服务端报回的是本批内的行号，加上本批首行行号减一才是原文里的行号
    const int firstLine = m_importBatches[m_importIndex].m_firstLine;
    m_importAdded += r.m_added;
    for (size_t i = 0; i < r.m_skipped.size(); ++i)
    {
        ImportSkipLine s;
        s.m_line = firstLine + r.m_skipped[i].m_line - 1;
        s.m_word = r.m_skipped[i].m_word;
        s.m_reason = r.m_skipped[i].m_reasonText;
        m_importSkips.push_back(s);
    }

    ++m_importIndex;
    if (m_importIndex < m_importBatches.size())
    {
        SendImportBatch();
        return;
    }
    FinishImport(CString());
}

void SensitiveWordsScreen::OnExportDone(const HttpResult& http)
{
    m_exporting = false;
    const EnterpriseExportResult r = adminapi::ParseEnterpriseExportResponse(http);
    if (!r.Succeeded())
    {
        FA_LOGW(_T("sensitive words screen: export failed, code: %d"), r.m_code);
        RaiseToast(_T("导出失败：") + r.m_message, true);
        return;
    }
    //词库为空时服务端返回空文本：只提示，不弹存盘框
    if (r.m_csvUtf8.empty())
    {
        RaiseToast(_T("词库为空，没有可导出的词条"), false);
        return;
    }

    UiRequest save;
    save.m_kind = kUiSaveExport;
    save.m_bytes = swtext::BuildExportFileBytes(r.m_csvUtf8);
    save.m_text = filesave::SensitiveExportFileName(CurrentEnterpriseName());
    FA_LOGI(_T("sensitive words screen: export fetched, bytes: %d"), static_cast<int>(save.m_bytes.size()));
    Raise(save);
}

void SensitiveWordsScreen::SendImportBatch()
{
    if (m_importIndex >= m_importBatches.size())
        return;
    const swtext::ImportBatch& batch = m_importBatches[m_importIndex];
    HttpRequest request = adminapi::MakeSensitiveImportRequest(ServerConfig(), AdminSession::Instance().Token(),
                                                               m_enterpriseId, batch.m_lines, m_importCategory);
    Send(request, kApiTagSwImport);

    UiRequest progress;
    progress.m_kind = kUiImportProgress;
    progress.m_text.Format(_T("正在导入第 %d / %d 批…"), static_cast<int>(m_importIndex) + 1,
                           static_cast<int>(m_importBatches.size()));
    Raise(progress);
}

void SensitiveWordsScreen::FinishImport(const CString& failure)
{
    UiRequest done;
    done.m_kind = kUiImportDone;
    done.m_isError = !failure.IsEmpty();
    done.m_text = BuildImportSummary(m_importAdded, m_importSkips, failure, static_cast<int>(m_importIndex) + 1,
                                     static_cast<int>(m_importBatches.size()));
    FA_LOGI(_T("sensitive words screen: import finished, added: %d, skipped: %d, failed: %d"),
            m_importAdded, static_cast<int>(m_importSkips.size()), done.m_isError ? 1 : 0);

    m_importing = false;
    m_importBatches.clear();
    m_importSkips.clear();
    m_importIndex = 0;
    m_importAdded = 0;
    Raise(done);

    //新导入的词条按添加时间排在最前：回到第 1 页顶部重新拉列表。中途失败时前面的批次已经写进库，同样要刷新
    m_page = 1;
    m_scrollTopOnLoad = true;
    StartLoadWords();
}

CString SensitiveWordsScreen::CurrentEnterpriseName() const
{
    if (!m_isPlatform)
        return CString();
    for (size_t i = 0; i < m_enterpriseIds.size() && i < m_enterpriseNames.size(); ++i)
    {
        if (m_enterpriseIds[i] == m_enterpriseId)
            return m_enterpriseNames[i];
    }
    return CString();
}

// ---------------------------------------------------------------------------
// 「拦截记录」页签
// ---------------------------------------------------------------------------

void SensitiveWordsScreen::OnHitRangeChanged(int index)
{
    const int range = (index >= kHitRange7Days && index <= kHitRange180Days) ? index : kHitRange7Days;
    if (range == m_hitRange)
        return;
    m_hitRange = range;
    m_hitPage = 1;
    StartLoadHits();
}

void SensitiveWordsScreen::OnHitSearchChanged()
{
    //与词库搜索一样：停止输入一小段时间后再查
    if (m_hostWnd != nullptr && ::IsWindow(m_hostWnd))
    {
        ::SetTimer(m_hostWnd, kHitSearchTimerId, kSearchDebounceMs, nullptr);
        return;
    }
    OnHitSearchTimer();
}

void SensitiveWordsScreen::OnHitSearchTimer()
{
    if (m_hostWnd != nullptr && ::IsWindow(m_hostWnd))
        ::KillTimer(m_hostWnd, kHitSearchTimerId);
    CString keyword = (m_hitSearch != nullptr) ? m_hitSearch->GetText() : CString();
    keyword.Trim();
    if (keyword == m_hitKeyword)
        return;
    m_hitKeyword = keyword;
    m_hitPage = 1;
    StartLoadHits();
}

void SensitiveWordsScreen::OnHitSourceChanged(int index)
{
    const int source = (index >= 0 && index <= swtext::kHitSourceCount) ? index : 0;
    if (source == m_hitSource)
        return;
    m_hitSource = source;
    m_hitPage = 1;
    StartLoadHits();
}

void SensitiveWordsScreen::OnHitCategoryChanged(int index)
{
    const int category = (index >= 0 && index <= swtext::kCategoryCount) ? index : 0;
    if (category == m_hitCategory)
        return;
    m_hitCategory = category;
    m_hitPage = 1;
    StartLoadHits();
}

void SensitiveWordsScreen::GoToHitPage(int page)
{
    if (page < 1 || page > HitPageCount() || page == m_hitPage)
        return;
    m_hitPage = page;
    StartLoadHits();
}

void SensitiveWordsScreen::HitRowCells(const adminapi::SensitiveHitItem& item, std::vector<CString>& cells)
{
    cells.clear();
    cells.resize(kHitColumnCount);
    cells[kHcTime] = FormatHitTime(item.m_time);
    cells[kHcSender] = item.m_userName.IsEmpty() ? CString(_T("-")) : item.m_userName;
    cells[kHcSource] = swtext::HitSourceName(item.m_source);
    cells[kHcTarget] = item.m_targetName.IsEmpty() ? CString(_T("-")) : item.m_targetName;
    cells[kHcWord] = item.m_word;
    cells[kHcCategory] = swtext::CategoryName(item.m_category);
    //摘要可能带换行（多行消息），表格里只有一行，换行一律换成空格
    CString snippet = item.m_snippet;
    snippet.Replace(_T("\r\n"), _T(" "));
    snippet.Replace(_T('\r'), _T(' '));
    snippet.Replace(_T('\n'), _T(' '));
    cells[kHcSnippet] = snippet;
}

CString SensitiveWordsScreen::FormatHitTime(long long ms)
{
    if (ms <= 0)
        return CString(_T("-"));
    ULARGE_INTEGER u;
    u.QuadPart = static_cast<ULONGLONG>(ms) * k100nsPerMs + kEpochDiff100ns;
    FILETIME utc;
    utc.dwLowDateTime = u.LowPart;
    utc.dwHighDateTime = u.HighPart;
    FILETIME local;
    SYSTEMTIME st;
    if (!::FileTimeToLocalFileTime(&utc, &local) || !::FileTimeToSystemTime(&local, &st))
        return CString(_T("-"));
    // 拦截记录属于日志，显示到秒。
    return timetext::FormatSystemTime(st, timetext::TimeStyle::Second);
}

long long SensitiveWordsScreen::NowMs()
{
    FILETIME ft;
    ::GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return static_cast<long long>((u.QuadPart - kEpochDiff100ns) / k100nsPerMs);
}

CString SensitiveWordsScreen::HitTooltipAt(POINT pt) const
{
    if (m_hitTable == nullptr || !m_hitTable->GetOverlayText().IsEmpty())
        return CString();
    const int row = m_hitTable->RowFromPoint(pt);
    if (row < 0 || row >= static_cast<int>(m_hits.size()))
        return CString();

    //与 PaintHitRow 用同一套列边界：数据行从表格左边加 1 起，到滚动条左边为止
    const RECT& rc = m_hitTable->GetRect();
    std::vector<ui::ColumnBound> b;
    ui::ColumnHeader::ComputeColumnBounds(BuildHitColumns(), rc.left + 1, rc.right - m_hitTable->ScrollBarWidth(), b);
    if (b.size() != static_cast<size_t>(kHitColumnCount))
        return CString();
    if (pt.x < b[kHcSnippet].m_left || pt.x >= b[kHcSnippet].m_right)
        return CString();

    std::vector<CString> cells;
    HitRowCells(m_hits[static_cast<size_t>(row)], cells);
    return cells[kHcSnippet];
}

CString SensitiveWordsScreen::HitOverlayText() const
{
    if (m_isPlatform && m_enterpriseId <= 0)
        return CString(_T("请先选择企业"));
    if (m_hitLoadState == kLoadFailed)
        return _T("加载失败：") + m_hitLoadError;
    if (m_hits.empty() && (m_hitLoadState == kLoadLoading || m_hitLoadState == kLoadIdle))
        return CString(_T("加载中…"));
    if (m_hits.empty())
    {
        const bool filtered = !m_hitKeyword.IsEmpty() || m_hitSource != 0 || m_hitCategory != 0;
        return CString(filtered ? _T("没有符合条件的拦截记录") : _T("暂无拦截记录"));
    }
    return CString();
}

int SensitiveWordsScreen::HitPageCount() const
{
    if (m_hitTotal <= 0)
        return 1;
    return (m_hitTotal + kPageSize - 1) / kPageSize;
}

void SensitiveWordsScreen::OnHitsLoaded(const HttpResult& http)
{
    const adminapi::SensitiveHitListResult r = adminapi::ParseSensitiveHitsResponse(http);

    //加载期间查询条件又变了：这份结果已经过时，丢弃并按最新条件重查
    if (m_hitReloadPending)
    {
        m_hitReloadPending = false;
        m_hitLoadState = kLoadIdle;
        StartLoadHits();
        return;
    }

    if (!r.Succeeded())
    {
        m_hitLoadState = kLoadFailed;
        m_hitLoadError = r.m_message;
        m_hits.clear();
        FA_LOGW(_T("sensitive words screen: load hits failed, code: %d"), r.m_code);
        UpdateHitView();
        return;
    }

    m_hitLoadState = kLoadLoaded;
    m_hitLoadError.Empty();
    m_hits = r.m_list;
    m_hitTotal = r.m_total;

    //当前页超出总页数（例如记录过期被清理）：退到最后一页重查
    if (m_hits.empty() && m_hitTotal > 0 && m_hitPage > HitPageCount())
    {
        m_hitPage = HitPageCount();
        StartLoadHits();
        return;
    }
    UpdateHitView();
    //每次拿到一页新数据都从第一行看起（拦截记录只读，没有「操作后保持位置」的需要）
    if (m_hitTable != nullptr)
        m_hitTable->SetScrollPos(0);
}

void SensitiveWordsScreen::StartLoadHits()
{
    //平台超管未选企业时不知道该查哪个企业
    if (m_isPlatform && m_enterpriseId <= 0)
    {
        UpdateHitView();
        return;
    }
    //上一次查询还没回来：记下来，结果回来后按最新的条件再查一次
    if (m_hitLoadState == kLoadLoading)
    {
        m_hitReloadPending = true;
        return;
    }
    m_hitLoadState = kLoadLoading;
    m_hitReloadPending = false;
    const int days = kHitRangeDays[(m_hitRange >= kHitRange7Days && m_hitRange <= kHitRange180Days)
                                       ? m_hitRange : kHitRange7Days];
    const long long beginTime = NowMs() - days * kDayMs;
    HttpRequest request = adminapi::MakeSensitiveHitsRequest(ServerConfig(), AdminSession::Instance().Token(),
                                                             m_enterpriseId, m_hitKeyword, m_hitSource,
                                                             m_hitCategory, beginTime, 0, m_hitPage, kPageSize);
    Send(request, kApiTagSwHits);
    UpdateHitView();
}

void SensitiveWordsScreen::UpdateHitView()
{
    const bool hasEnterprise = !m_isPlatform || m_enterpriseId > 0;
    if (m_hitTable != nullptr)
    {
        m_hitTable->SetRowCount(static_cast<int>(m_hits.size()));
        m_hitTable->SetOverlayText(HitOverlayText());
        m_hitTable->Invalidate();
        //行数变化可能让滚动条出现或消失
        SyncHeaderInset();
    }
    if (m_hitPager != nullptr)
        m_hitPager->SetPageInfo(m_hitPage, m_hitTotal, kPageSize);

    //筛选行：平台超管未选企业时不可用
    if (m_hitRangeBox != nullptr)
        m_hitRangeBox->SetEnabled(hasEnterprise);
    if (m_hitSearch != nullptr)
        m_hitSearch->SetEnabled(hasEnterprise);
    if (m_hitSourceBox != nullptr)
        m_hitSourceBox->SetEnabled(hasEnterprise);
    if (m_hitCategoryBox != nullptr)
        m_hitCategoryBox->SetEnabled(hasEnterprise);
}

void SensitiveWordsScreen::PaintHitRowThunk(void* user, HDC hdc, int rowIndex, const RECT& rowRect, bool selected,
                                            bool hover)
{
    (void)selected;
    static_cast<SensitiveWordsScreen*>(user)->PaintHitRow(hdc, rowIndex, rowRect, hover);
}

void SensitiveWordsScreen::PaintHitRow(HDC hdc, int rowIndex, const RECT& rowRect, bool hover)
{
    if (rowIndex < 0 || rowIndex >= static_cast<int>(m_hits.size()) || m_hitHeader == nullptr)
        return;

    if (hover)
        ui::FillRoundRect(hdc, rowRect, theme::kInk25, 0);

    std::vector<ui::ColumnBound> b;
    ui::ColumnHeader::ComputeColumnBounds(m_hitHeader->Columns(), rowRect.left, rowRect.right, b);
    if (b.size() != static_cast<size_t>(kHitColumnCount))
        return;

    std::vector<CString> cells;
    HitRowCells(m_hits[static_cast<size_t>(rowIndex)], cells);
    HFONT font = ui::UiFont(theme::kFontSizeNormal, false);
    const UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
    for (int i = 0; i < kHitColumnCount; ++i)
    {
        //命中词用红字突出，发送者与摘要用正文色，其余用次要色
        COLORREF color = theme::kInk500;
        if (i == kHcWord)
            color = theme::kColorDanger;
        else if (i == kHcSender || i == kHcSnippet)
            color = theme::kInk700;
        const RECT rc = { b[i].m_left + kCellPad, rowRect.top, b[i].m_right - kCellPad, rowRect.bottom };
        if (rc.right > rc.left)
            ui::DrawTextLine(hdc, cells[static_cast<size_t>(i)], rc, color, font, flags);
    }

    //行底分隔线
    ui::FillLine(hdc, rowRect.left, rowRect.bottom - 1, rowRect.right - rowRect.left, 1, theme::kInk50);
}

void SensitiveWordsScreen::OnHitPageThunk(void* user, int newPage)
{
    static_cast<SensitiveWordsScreen*>(user)->GoToHitPage(newPage);
}

CString SensitiveWordsScreen::HitTooltipThunk(void* user, POINT pt)
{
    return static_cast<SensitiveWordsScreen*>(user)->HitTooltipAt(pt);
}

void SensitiveWordsScreen::GoToPage(int page)
{
    if (page < 1 || page > PageCount() || page == m_page)
        return;
    m_page = page;
    StartLoadWords();
}

void SensitiveWordsScreen::OnPageThunk(void* user, int newPage)
{
    static_cast<SensitiveWordsScreen*>(user)->GoToPage(newPage);
}

void SensitiveWordsScreen::OnRowActionThunk(void* user, int rowIndex, int actionIndex)
{
    SensitiveWordsScreen* self = static_cast<SensitiveWordsScreen*>(user);
    if (rowIndex < 0 || rowIndex >= static_cast<int>(self->m_words.size()))
        return;
    const adminapi::SensitiveWordItem& word = self->m_words[static_cast<size_t>(rowIndex)];

    UiRequest req;
    req.m_word = word;
    switch (actionIndex)
    {
    //「编辑」：请宿主打开编辑弹窗
    case kActionEdit:
        req.m_kind = kUiOpenEditModal;
        self->Raise(req);
        break;
    //「删除」：记下要删的词条，请宿主弹确认框，确认后再发请求
    case kActionDelete:
        self->m_pendingDeleteId = word.m_id;
        req.m_kind = kUiConfirmDelete;
        self->Raise(req);
        break;
    //没有其它操作
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// 界面请求
// ---------------------------------------------------------------------------

void SensitiveWordsScreen::Raise(const UiRequest& request)
{
    if (m_uiHandler == nullptr)
    {
        FA_LOGW(_T("sensitive words screen: no ui handler, ui request dropped, kind: %d"), request.m_kind);
        return;
    }
    m_uiHandler(m_uiHandlerUser, request);
}

void SensitiveWordsScreen::RaiseToast(const CString& text, bool isError)
{
    UiRequest req;
    req.m_kind = kUiToast;
    req.m_text = text;
    req.m_isError = isError;
    Raise(req);
}

// ---------------------------------------------------------------------------
// 显示
// ---------------------------------------------------------------------------

void SensitiveWordsScreen::ApplyTabVisibility()
{
    if (m_wordsPanel != nullptr)
        m_wordsPanel->SetVisible(m_activeTab == kTabWords);
    if (m_hitsPanel != nullptr)
        m_hitsPanel->SetVisible(m_activeTab == kTabHits);
    //SetVisible 只改标志位并重绘，不会重新排版；两块内容区都是本容器的直接子控件，
    //对本容器 ForceLayout 才能让新显示的那块按当前矩形重新排好（开发约定文档 UI 约定第 5 条）
    ForceLayout(GetRect());
}

void SensitiveWordsScreen::Layout(const RECT& rcAvail)
{
    DuiVBox::Layout(rcAvail);
    SyncHeaderInset();
}

void SensitiveWordsScreen::SyncHeaderInset()
{
    if (m_wordHeader != nullptr && m_wordTable != nullptr)
        m_wordHeader->SetTrailingInset(m_wordTable->ScrollBarWidth());
    if (m_hitHeader != nullptr && m_hitTable != nullptr)
        m_hitHeader->SetTrailingInset(m_hitTable->ScrollBarWidth());
}

void SensitiveWordsScreen::UpdateView()
{
    const bool hasEnterprise = !m_isPlatform || m_enterpriseId > 0;

    CString overlay;
    if (!hasEnterprise)
        overlay = _T("请先选择企业");
    else if (m_loadState == kLoadFailed)
        overlay = _T("加载失败：") + m_loadError;
    else if (m_words.empty() && (m_loadState == kLoadLoading || m_loadState == kLoadIdle))
        overlay = _T("加载中…");
    else if (m_words.empty())
        overlay = (m_keyword.IsEmpty() && m_categoryFilter == 0) ? _T("暂无敏感词") : _T("没有符合条件的敏感词");

    if (m_wordTable != nullptr)
    {
        m_wordTable->SetRowCount(static_cast<int>(m_words.size()));
        m_wordTable->SetOverlayText(overlay);
        m_wordTable->Invalidate();
        //行数变化可能让滚动条出现或消失
        SyncHeaderInset();
    }
    if (m_pager != nullptr)
        m_pager->SetPageInfo(m_page, m_total, kPageSize);
    if (m_usage != nullptr)
        m_usage->SetText(hasEnterprise ? UsageText() : CString());

    //工具行与总开关：平台超管未选企业时不可用
    if (m_search != nullptr)
        m_search->SetEnabled(hasEnterprise);
    if (m_category != nullptr)
        m_category->SetEnabled(hasEnterprise);
    if (m_addButton != nullptr)
        m_addButton->SetEnabled(hasEnterprise);
    if (m_importButton != nullptr)
        m_importButton->SetEnabled(hasEnterprise);
    if (m_exportButton != nullptr)
        m_exportButton->SetEnabled(hasEnterprise);
    if (m_switch != nullptr)
        m_switch->SetEnabled(hasEnterprise);
}

bool SensitiveWordsScreen::IsEnterprisePickerVisible() const
{
    return m_pickerBox != nullptr && m_pickerBox->IsVisible();
}

bool SensitiveWordsScreen::IsToolbarEnabled() const
{
    return m_addButton != nullptr && m_addButton->IsEnabled();
}

CString SensitiveWordsScreen::OverlayText() const
{
    return (m_wordTable != nullptr) ? m_wordTable->GetOverlayText() : CString();
}

CString SensitiveWordsScreen::UsageText() const
{
    CString text;
    text.Format(_T("已用 %d / %d"), m_wordCount, swtext::kWordsPerEnterprise);
    return text;
}

bool SensitiveWordsScreen::IsSwitchChecked() const
{
    return m_switch != nullptr && m_switch->IsChecked();
}

int SensitiveWordsScreen::PageCount() const
{
    const int n = (m_total + kPageSize - 1) / kPageSize;
    return (n < 1) ? 1 : n;
}

void SensitiveWordsScreen::PaintRowThunk(void* user, HDC hdc, int rowIndex, const RECT& rowRect, bool selected,
                                         bool hover)
{
    (void)selected;
    static_cast<SensitiveWordsScreen*>(user)->PaintRow(hdc, rowIndex, rowRect, hover);
}

void SensitiveWordsScreen::PaintRow(HDC hdc, int rowIndex, const RECT& rowRect, bool hover)
{
    if (rowIndex < 0 || rowIndex >= static_cast<int>(m_words.size()) || m_wordHeader == nullptr)
        return;
    const adminapi::SensitiveWordItem& w = m_words[static_cast<size_t>(rowIndex)];

    if (hover)
        ui::FillRoundRect(hdc, rowRect, theme::kInk25, 0);

    std::vector<ui::ColumnBound> b;
    ui::ColumnHeader::ComputeColumnBounds(m_wordHeader->Columns(), rowRect.left, rowRect.right, b);
    if (b.size() <= static_cast<size_t>(kWcTime))
        return;

    HFONT font = ui::UiFont(theme::kFontSizeNormal, false);
    const UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
    const CString cells[] =
    {
        w.m_word,
        swtext::CategoryName(w.m_category),
        w.m_remark.IsEmpty() ? CString(_T("-")) : w.m_remark,
        w.m_creatorName.IsEmpty() ? CString(_T("-")) : w.m_creatorName,
        FormatTimeMs(w.m_createTime),
    };
    const COLORREF colors[] = { theme::kInk700, theme::kInk500, theme::kInk500, theme::kInk500, theme::kInk500 };
    for (int i = kWcWord; i <= kWcTime; ++i)
    {
        const RECT rc = { b[i].m_left + kCellPad, rowRect.top, b[i].m_right - kCellPad, rowRect.bottom };
        if (rc.right > rc.left)
            ui::DrawTextLine(hdc, cells[i], rc, colors[i], font, flags);
    }

    //行底分隔线
    ui::FillLine(hdc, rowRect.left, rowRect.bottom - 1, rowRect.right - rowRect.left, 1, theme::kInk50);
}

} // namespace flamingoadmin
