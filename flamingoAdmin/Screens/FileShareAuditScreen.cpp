/**
 *  flamingoAdmin —— 「共享审计」页实现，说明见 FileShareAuditScreen.h。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "Screens/FileShareAuditScreen.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/CommonWidgets.h"
#include "Widgets/ActionTable.h"
#include "Widgets/ColumnHeader.h"
#include "Widgets/PagerBar.h"
#include "Widgets/KeyValueGrid.h"
#include "Widgets/UiHelpers.h"
#include "Util/FileShareText.h"
#include "Util/Logger.h"

#include "Controls/Basic/DuiLabel.h"
#include "Controls/Input/DuiComboBox.h"
#include "Controls/Input/DuiEdit.h"

using namespace balloonwjui;

namespace flamingoadmin {

using Hint = balloonwjui::DuiLayout::Hint;

namespace {

// ---- 布局常量（96 dpi 逻辑像素）----
// 筛选行各控件的宽度之和（含间距）须不超过最小窗口下筛选行的宽度 828（最小宽度 1100 - 侧栏 224 - 左右外边距 48），
// 见单元测试 FAU-4
const int kSideMargin     = 24;   // 内容区左 / 右 / 下统一外边距
const int kRowGap         = 12;   // 筛选行、表格卡、详情卡之间的间距
const int kBarH           = 36;   // 筛选行高度
const int kBarGap         = 10;   // 筛选行内控件之间的间距
const int kPickerLabelW   = 40;   // 「企业」标签宽度
const int kPickerW        = 180;  // 企业筛选框宽度
const int kRangeW         = 110;  // 时间范围下拉框宽度
const int kSearchW        = 170;  // 操作人搜索框宽度
const int kActionW        = 110;  // 动作筛选下拉框宽度
const int kShareIdW       = 110;  // 共享 ID 输入框宽度
const int kTableHeaderH   = 40;   // 表头高度
const int kRowHeight      = 44;   // 审计表格行高
const int kPagerH         = 42;   // 分页条高度
const int kCellPad        = 12;   // 单元格左右内边距（九列，比其它表格略窄）
const int kDetailPad      = 16;   // 详情卡内边距
const int kDetailTitleH   = 28;   // 详情卡标题高度
const int kDetailTitleGap = 8;    // 详情卡标题与信息网格之间的间距
const int kDetailCaptionW = 84;   // 信息网格名称部分的宽度（四个字的名称之后与取值隔开）
const int kDetailColumns  = 3;    // 信息网格的列数
const int kPathLines      = 2;    // 路径最多显示的行数
const int kToPathLines    = 1;    // 新名字或目标目录显示的行数

// ---- 审计表格的列（像素）----
const int kColTimeW   = 146;  // 时间（到分钟）
const int kColActorW  = 144;  // 操作人（管理员名字后带「（管理员）」）
const int kColOwnerW  = 90;   // 分享者
const int kColShareW  = 96;   // 共享名
const int kColActionW = 96;   // 动作（够放「新建文件夹」）
const int kColSizeW   = 76;   // 大小
const int kColResultW = 56;   // 结果
const int kColSourceW = 70;   // 来源（简称：服务端 / 分享者）

// 表格里的时间只显示到分钟：「YYYY-MM-DD HH:MM」共 16 个字符
const int kShortTimeChars = 16;

// 企业筛选框：拉企业列表时的状态筛选（-1 表示全部状态）与条数
const int kEntStatusAll     = -1;
const int kEntPickerPageMax = 1000;

// 输入防抖：停止输入后多久才查询（毫秒）
const UINT kInputDebounceMs = 400;

// 审计表格的列定义，顺序与 FileShareAuditScreen::Column 一致
std::vector<ui::ColumnSpec> BuildColumns()
{
    std::vector<ui::ColumnSpec> cols;
    cols.push_back(ui::ColumnSpec(_T("时间"), kColTimeW));
    cols.push_back(ui::ColumnSpec(_T("操作人"), kColActorW));
    cols.push_back(ui::ColumnSpec(_T("分享者"), kColOwnerW));
    cols.push_back(ui::ColumnSpec(_T("共享名"), kColShareW));
    cols.push_back(ui::ColumnSpec(_T("动作"), kColActionW));
    cols.push_back(ui::ColumnSpec(_T("路径"), 0, true));
    cols.push_back(ui::ColumnSpec(_T("大小"), kColSizeW));
    cols.push_back(ui::ColumnSpec(_T("结果"), kColResultW));
    cols.push_back(ui::ColumnSpec(_T("来源"), kColSourceW));
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
std::unique_ptr<DuiLabel> MakeLabel(LPCTSTR text, COLORREF color, bool bold)
{
    auto label = std::make_unique<DuiLabel>();
    label->SetText(text);
    label->SetTextColor(color);
    label->SetTextPointSize(theme::kFontSizeNormal, bold);
    label->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    return label;
}

// 一个带边框的单行输入框
std::unique_ptr<DuiEdit> MakeEdit(int ctrlId, LPCTSTR placeholder)
{
    auto edit = std::make_unique<DuiEdit>();
    edit->SetCtrlId(ctrlId);
    edit->SetPlaceholder(placeholder);
    edit->SetBgColor(theme::kInk25);
    edit->SetShowBorder(true);
    return edit;
}

} // namespace

FileShareAuditScreen::FileShareAuditScreen()
    : m_hostWnd(nullptr)
    , m_pickerBox(nullptr)
    , m_picker(nullptr)
    , m_rangeBox(nullptr)
    , m_search(nullptr)
    , m_actionBox(nullptr)
    , m_shareIdBox(nullptr)
    , m_header(nullptr)
    , m_table(nullptr)
    , m_pager(nullptr)
    , m_detailTitle(nullptr)
    , m_detail(nullptr)
    , m_isPlatform(false)
    , m_enterpriseId(0)
    , m_range(fstext::kRange7Days)
    , m_action(0)
    , m_shareIdFilter(0)
    , m_page(1)
    , m_total(0)
    , m_loadState(kLoadIdle)
    , m_reloadPending(false)
    , m_selectedId(0)
{
    // 页头与「刷新」属于外层的文件共享页（FileShareCenterScreen，2026-10-08），本页从筛选行开始

    // ---- 筛选行：[企业]（仅平台超管） [时间范围] [操作人] [动作] [共享 ID] ……----
    auto filterRow = std::make_unique<DuiHBox>();
    filterRow->SetGap(kBarGap);
    {
        auto pickerBox = std::make_unique<DuiHBox>();
        pickerBox->AddChild(MakeLabel(_T("企业"), theme::kInk500, false), Hint().Fixed(kPickerLabelW));
        auto picker = std::make_unique<DuiComboBox>();
        picker->SetCtrlId(kIdFaEnterprise);
        picker->SetEditable(true);
        picker->SetIncrementalSearch(true);
        picker->SetIncrementalSearchSubstring(true);
        picker->AddString(_T("全部企业"));
        picker->SetCurSel(0, false);
        //可输入的下拉框显示的是内部输入框的文字，SetCurSel 不会更新它
        picker->SetText(_T("全部企业"));
        m_picker = picker.get();
        pickerBox->AddChild(std::move(picker), Hint().Fixed(kPickerW));
        m_pickerBox = pickerBox.get();
        filterRow->AddChild(std::move(pickerBox), Hint().Fixed(kPickerLabelW + kPickerW));

        auto range = std::make_unique<DuiComboBox>();
        range->SetCtrlId(kIdFaRange);
        for (int i = fstext::kRange7Days; i < fstext::kRangeCount; ++i)
            range->AddString(fstext::RangeName(i));
        range->SetCurSel(fstext::kRange7Days, false);
        m_rangeBox = range.get();
        filterRow->AddChild(std::move(range), Hint().Fixed(kRangeW));

        auto search = MakeEdit(kIdFaSearch, _T("搜索操作人"));
        m_search = search.get();
        filterRow->AddChild(std::move(search), Hint().Fixed(kSearchW));

        auto action = std::make_unique<DuiComboBox>();
        action->SetCtrlId(kIdFaAction);
        action->AddString(_T("全部动作"));
        for (int a = 1; a <= fstext::kActionCount; ++a)
            action->AddString(fstext::ActionName(a));
        action->SetCurSel(0, false);
        m_actionBox = action.get();
        filterRow->AddChild(std::move(action), Hint().Fixed(kActionW));

        auto shareId = MakeEdit(kIdFaShareId, _T("共享 ID"));
        m_shareIdBox = shareId.get();
        filterRow->AddChild(std::move(shareId), Hint().Fixed(kShareIdW));

        filterRow->AddChild(std::make_unique<DuiControl>(), Hint().Weight(1));
    }
    AddChild(std::move(filterRow), Hint().Fixed(kBarH).Margin(kSideMargin, 0, kSideMargin, kRowGap));

    // ---- 表格卡 ----
    auto card = MakeCard();
    {
        auto head = std::make_unique<ui::ColumnHeader>();
        head->SetColumns(BuildColumns());
        head->SetCellPadding(kCellPad);
        m_header = head.get();
        card->AddChild(std::move(head), Hint().Fixed(kTableHeaderH));

        auto table = std::make_unique<ui::ActionTable>();
        table->SetCtrlId(kIdFaTable);
        table->SetRowHeight(kRowHeight);
        table->SetRowPainter(&FileShareAuditScreen::PaintRowThunk, this);
        m_table = table.get();
        card->AddChild(std::move(table), Hint().Weight(1));

        auto pager = std::make_unique<ui::PagerBar>();
        pager->SetSidePadding(kCellPad);
        pager->SetPageCallback(&FileShareAuditScreen::OnPageThunk, this);
        m_pager = pager.get();
        card->AddChild(std::move(pager), Hint().Fixed(kPagerH));
    }
    AddChild(std::move(card), Hint().Weight(1).Margin(kSideMargin, 0, kSideMargin, kRowGap));

    // ---- 详情卡 ----
    auto detailCard = MakeCard();
    detailCard->SetPadding(kDetailPad, kDetailPad, kDetailPad, kDetailPad);
    int detailH = 0;
    {
        auto title = MakeLabel(_T(""), theme::kInk700, true);
        m_detailTitle = title.get();
        detailCard->AddChild(std::move(title), Hint().Fixed(kDetailTitleH).Margin(0, 0, 0, kDetailTitleGap));

        auto grid = std::make_unique<ui::KeyValueGrid>();
        grid->SetColumnCount(kDetailColumns);
        grid->SetCaptionWidth(kDetailCaptionW);
        //条目的添加顺序即 DetailItem 的取值
        grid->AddItem(_T("时间"));
        grid->AddItem(_T("操作人"));
        grid->AddItem(_T("动作"));
        grid->AddItem(_T("分享者"));
        grid->AddItem(_T("共享名"));
        grid->AddItem(_T("共享 ID"));
        grid->AddItem(_T("大小"));
        grid->AddItem(_T("结果"));
        grid->AddItem(_T("来源"));
        grid->AddItem(_T("访问会话"));
        grid->AddWideItem(_T("路径"), kPathLines);
        grid->AddWideItem(_T("目标"), kToPathLines);
        grid->EndRow();
        m_detail = grid.get();
        const int gridH = grid->ContentHeight();
        detailCard->AddChild(std::move(grid), Hint().Fixed(gridH));
        detailH = kDetailPad * 2 + kDetailTitleH + kDetailTitleGap + gridH;
    }
    AddChild(std::move(detailCard), Hint().Fixed(detailH).Margin(kSideMargin, 0, kSideMargin, kSideMargin));

    UpdateView();
    UpdateDetail();
}

// ---------------------------------------------------------------------------
// 进入界面与身份
// ---------------------------------------------------------------------------

void FileShareAuditScreen::OnEnter()
{
    const AdminSession& session = AdminSession::Instance();
    if (session.Token() != m_sessionToken)
    {
        ResetState();
        m_sessionToken = session.Token();
    }
    m_isPlatform = session.IsPlatformAdmin();

    if (m_pickerBox != nullptr)
    {
        m_pickerBox->SetVisible(m_isPlatform);
        //SetVisible 不会重新排版：对所在的筛选行重新排一次（开发约定文档 UI 约定第 5 条）
        DuiControl* row = m_pickerBox->GetParent();
        if (row != nullptr)
            row->ForceLayout(row->GetRect());
    }

    FA_LOGI(_T("enter file share audit screen, platform admin: %d"), m_isPlatform ? 1 : 0);
    if (m_isPlatform && m_enterpriseIds.empty())
        StartLoadEnterprises();
    Refresh();
}

void FileShareAuditScreen::Refresh()
{
    StartLoad();
}

void FileShareAuditScreen::ResetState()
{
    m_enterpriseIds.clear();
    m_enterpriseId = 0;
    if (m_picker != nullptr)
    {
        m_picker->ResetContent();
        m_picker->AddString(_T("全部企业"));
        m_picker->SetCurSel(0, false);
        m_picker->SetText(_T("全部企业"));
    }
    m_range = fstext::kRange7Days;
    if (m_rangeBox != nullptr)
        m_rangeBox->SetCurSel(fstext::kRange7Days, false);
    m_keyword.Empty();
    if (m_search != nullptr)
        m_search->SetText(_T(""));
    m_action = 0;
    if (m_actionBox != nullptr)
        m_actionBox->SetCurSel(0, false);
    m_shareIdFilter = 0;
    if (m_shareIdBox != nullptr)
        m_shareIdBox->SetText(_T(""));
    m_page = 1;
    m_total = 0;
    m_loadState = kLoadIdle;
    m_loadError.Empty();
    m_reloadPending = false;
    m_items.clear();
    m_selectedId = 0;
    if (m_table != nullptr)
        m_table->SetCurSel(-1, false);
    UpdateView();
    UpdateDetail();
}

// ---------------------------------------------------------------------------
// 发请求与应答
// ---------------------------------------------------------------------------

void FileShareAuditScreen::StartLoadEnterprises()
{
    HttpRequest request = adminapi::MakeEnterpriseListRequest(ServerConfig(), AdminSession::Instance().Token(),
                                                              CString(), kEntStatusAll, 1, kEntPickerPageMax);
    m_port.Send(request, kApiTagFsAuditEnterprises);
}

void FileShareAuditScreen::StartLoad()
{
    if (m_loadState == kLoadLoading)
    {
        m_reloadPending = true;
        return;
    }
    m_loadState = kLoadLoading;
    m_reloadPending = false;
    //时间范围按「现在」往前推，截止时间不限（新产生的记录也在范围内）
    const long long start = fstext::RangeStartMs(m_range, fstext::NowMs());
    HttpRequest request = adminapi::MakeFileShareAuditRequest(ServerConfig(), AdminSession::Instance().Token(),
                                                              m_enterpriseId, m_keyword, m_action, start, 0,
                                                              m_shareIdFilter, m_page, kPageSize);
    m_port.Send(request, kApiTagFsAudit);
    UpdateView();
}

void FileShareAuditScreen::OnApiResult(ApiTag tag, const HttpResult& http)
{
    switch (tag)
    {
    //企业筛选框的企业列表
    case kApiTagFsAuditEnterprises:
        OnEnterprisesLoaded(http);
        break;
    //审计列表
    case kApiTagFsAudit:
        OnAuditLoaded(http);
        break;
    //其余标签不属于本界面
    default:
        break;
    }
}

void FileShareAuditScreen::OnEnterprisesLoaded(const HttpResult& http)
{
    const EnterpriseListResult r = adminapi::ParseEnterpriseListResponse(http);
    if (!r.Succeeded())
    {
        FA_LOGW(_T("file share audit screen: load enterprises failed, code: %d"), r.m_code);
        m_port.Toast(_T("企业列表加载失败：") + r.m_message, true);
        return;
    }
    m_enterpriseIds.clear();
    if (m_picker != nullptr)
    {
        m_picker->ResetContent();
        m_picker->AddString(_T("全部企业"));
    }
    int selected = 0;
    for (size_t i = 0; i < r.m_list.size(); ++i)
    {
        const long long id = _ttoi64(r.m_list[i].m_id);
        m_enterpriseIds.push_back(id);
        if (m_picker != nullptr)
            m_picker->AddString(r.m_list[i].m_name);
        if (id == m_enterpriseId)
            selected = static_cast<int>(i) + 1;
    }
    if (m_picker != nullptr)
    {
        m_picker->SetCurSel(selected, false);
        //可输入的下拉框显示的是内部输入框的文字，SetCurSel 不会更新它
        m_picker->SetText(m_picker->GetItemText(selected));
    }
}

void FileShareAuditScreen::OnAuditLoaded(const HttpResult& http)
{
    const adminapi::FileShareAuditListResult r = adminapi::ParseFileShareAuditResponse(http);
    if (m_reloadPending)
    {
        m_reloadPending = false;
        m_loadState = kLoadIdle;
        StartLoad();
        return;
    }
    if (!r.Succeeded())
    {
        m_loadState = kLoadFailed;
        m_loadError = r.m_message;
        m_items.clear();
        m_selectedId = 0;
        FA_LOGW(_T("file share audit screen: load audit failed, code: %d"), r.m_code);
        UpdateView();
        UpdateDetail();
        return;
    }
    m_loadState = kLoadLoaded;
    m_loadError.Empty();
    m_items = r.m_list;
    m_total = r.m_total;
    FA_LOGI(_T("file share audit screen: %d records loaded (total %d, page %d)"), static_cast<int>(m_items.size()),
            m_total, m_page);
    if (m_items.empty() && m_total > 0 && m_page > PageCount())
    {
        m_page = PageCount();
        StartLoad();
        return;
    }
    //选中的记录仍在本页时保持选中，否则清空详情
    int selectedRow = -1;
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        if (m_items[i].m_id == m_selectedId)
        {
            selectedRow = static_cast<int>(i);
            break;
        }
    }
    if (selectedRow < 0)
        m_selectedId = 0;
    UpdateView();
    if (m_table != nullptr)
    {
        m_table->SetCurSel(selectedRow, false);
        //新的一页从第一行看起
        if (selectedRow < 0)
            m_table->SetScrollPos(0);
    }
    UpdateDetail();
}

// ---------------------------------------------------------------------------
// 用户操作
// ---------------------------------------------------------------------------

void FileShareAuditScreen::OnEnterpriseSelected(int index)
{
    if (index < 0 || index > static_cast<int>(m_enterpriseIds.size()))
        return;
    const long long id = (index == 0) ? 0 : m_enterpriseIds[static_cast<size_t>(index - 1)];
    if (id == m_enterpriseId)
        return;
    m_enterpriseId = id;
    m_page = 1;
    StartLoad();
}

void FileShareAuditScreen::OnRangeChanged(int index)
{
    if (index < fstext::kRange7Days || index >= fstext::kRangeCount || index == m_range)
        return;
    m_range = index;
    m_page = 1;
    StartLoad();
}

void FileShareAuditScreen::OnSearchChanged()
{
    if (m_hostWnd != nullptr && ::IsWindow(m_hostWnd))
    {
        ::SetTimer(m_hostWnd, kSearchTimerId, kInputDebounceMs, nullptr);
        return;
    }
    OnSearchTimer();
}

void FileShareAuditScreen::OnSearchTimer()
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
    StartLoad();
}

void FileShareAuditScreen::OnActionChanged(int index)
{
    if (index < 0 || index > fstext::kActionCount || index == m_action)
        return;
    m_action = index;
    m_page = 1;
    StartLoad();
}

void FileShareAuditScreen::OnShareIdChanged()
{
    if (m_hostWnd != nullptr && ::IsWindow(m_hostWnd))
    {
        ::SetTimer(m_hostWnd, kShareIdTimerId, kInputDebounceMs, nullptr);
        return;
    }
    OnShareIdTimer();
}

void FileShareAuditScreen::OnShareIdTimer()
{
    if (m_hostWnd != nullptr && ::IsWindow(m_hostWnd))
        ::KillTimer(m_hostWnd, kShareIdTimerId);
    if (m_shareIdBox == nullptr)
        return;
    const long long id = ParseShareId(m_shareIdBox->GetText());
    if (id == m_shareIdFilter)
        return;
    m_shareIdFilter = id;
    m_page = 1;
    StartLoad();
}

void FileShareAuditScreen::OnRowSelected(int index)
{
    const long long id = (index >= 0 && index < static_cast<int>(m_items.size()))
                             ? m_items[static_cast<size_t>(index)].m_id : 0;
    if (id == m_selectedId)
        return;
    m_selectedId = id;
    UpdateDetail();
}

void FileShareAuditScreen::GoToPage(int page)
{
    if (page < 1 || page > PageCount() || page == m_page)
        return;
    m_page = page;
    StartLoad();
}

// ---------------------------------------------------------------------------
// 文字与显示
// ---------------------------------------------------------------------------

void FileShareAuditScreen::RowCells(const adminapi::FileShareAuditItem& item, std::vector<CString>& cells)
{
    cells.clear();
    cells.resize(kColumnCount);
    const CString time = fstext::FormatTime(item.m_time);
    cells[kColTime] = (time.GetLength() > kShortTimeChars) ? time.Left(kShortTimeChars) : time;
    cells[kColActor] = fstext::ActorText(item.m_actorName, item.m_actorIsAdmin);
    cells[kColOwner] = item.m_ownerName.IsEmpty() ? CString(_T("-")) : item.m_ownerName;
    cells[kColShare] = item.m_shareName.IsEmpty() ? CString(_T("-")) : item.m_shareName;
    cells[kColAction] = fstext::ActionName(item.m_action);
    //重命名与移动把目标一并显示：「原路径 → 新名字或目标目录」
    CString path = item.m_path.IsEmpty() ? CString(_T("-")) : item.m_path;
    if (!item.m_toPath.IsEmpty())
        path += _T(" → ") + item.m_toPath;
    cells[kColPath] = path;
    cells[kColSize] = fstext::FormatSize(item.m_size);
    cells[kColResult] = fstext::ResultShort(item.m_result);
    cells[kColSource] = fstext::SourceShort(item.m_source);
}

long long FileShareAuditScreen::ParseShareId(const CString& text)
{
    CString t = text;
    t.Trim();
    if (t.IsEmpty())
        return 0;
    for (int i = 0; i < t.GetLength(); ++i)
    {
        if (t[i] < _T('0') || t[i] > _T('9'))
            return 0;
    }
    const long long id = _ttoi64(t);
    return (id > 0) ? id : 0;
}

CString FileShareAuditScreen::OverlayText() const
{
    if (m_loadState == kLoadFailed)
        return _T("加载失败：") + m_loadError;
    if (m_items.empty() && (m_loadState == kLoadLoading || m_loadState == kLoadIdle))
        return CString(_T("加载中…"));
    if (m_items.empty())
    {
        const bool filtered = !m_keyword.IsEmpty() || m_action != 0 || m_shareIdFilter > 0 || m_enterpriseId > 0;
        return CString(filtered ? _T("没有符合条件的审计记录") : _T("这段时间内没有审计记录"));
    }
    return CString();
}

int FileShareAuditScreen::PageCount() const
{
    const int n = (m_total + kPageSize - 1) / kPageSize;
    return (n < 1) ? 1 : n;
}

bool FileShareAuditScreen::IsEnterprisePickerVisible() const
{
    return m_pickerBox != nullptr && m_pickerBox->IsVisible();
}

CString FileShareAuditScreen::EnterprisePickerText() const
{
    return (m_picker != nullptr) ? m_picker->GetText() : CString();
}

CString FileShareAuditScreen::DetailTitle() const
{
    return (m_detailTitle != nullptr) ? m_detailTitle->GetText() : CString();
}

const adminapi::FileShareAuditItem* FileShareAuditScreen::SelectedItem() const
{
    if (m_selectedId <= 0)
        return nullptr;
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        if (m_items[i].m_id == m_selectedId)
            return &m_items[i];
    }
    return nullptr;
}

void FileShareAuditScreen::UpdateView()
{
    if (m_table != nullptr)
    {
        m_table->SetRowCount(static_cast<int>(m_items.size()));
        m_table->SetOverlayText(OverlayText());
        m_table->Invalidate();
        SyncHeaderInset();
    }
    if (m_pager != nullptr)
        m_pager->SetPageInfo(m_page, m_total, kPageSize);
}

void FileShareAuditScreen::UpdateDetail()
{
    const adminapi::FileShareAuditItem* item = SelectedItem();
    if (m_detail != nullptr)
        m_detail->ClearValues();
    if (item == nullptr)
    {
        if (m_detailTitle != nullptr)
            m_detailTitle->SetText(_T("选择一条记录查看详情"));
        return;
    }
    if (m_detailTitle != nullptr)
    {
        CString title = fstext::ActionName(item->m_action);
        if (!item->m_shareName.IsEmpty())
            title += _T(" · ") + item->m_shareName;
        m_detailTitle->SetText(title);
    }
    if (m_detail == nullptr)
        return;
    m_detail->SetValue(kDetailTime, fstext::FormatTime(item->m_time));
    m_detail->SetValue(kDetailActor, fstext::ActorText(item->m_actorName, item->m_actorIsAdmin));
    m_detail->SetValue(kDetailAction, fstext::ActionName(item->m_action));
    m_detail->SetValue(kDetailOwner, item->m_ownerName);
    m_detail->SetValue(kDetailShare, item->m_shareName);
    CString shareId;
    shareId.Format(_T("%lld"), item->m_shareId);
    m_detail->SetValue(kDetailShareId, shareId);
    m_detail->SetValue(kDetailSize, fstext::FormatSize(item->m_size));
    m_detail->SetValue(kDetailResult, fstext::ResultText(item->m_result));
    m_detail->SetValue(kDetailSource, fstext::SourceName(item->m_source));
    m_detail->SetValue(kDetailAccessId, item->m_accessId);
    m_detail->SetValue(kDetailPath, item->m_path);
    m_detail->SetValue(kDetailToPath, item->m_toPath);
}

void FileShareAuditScreen::Layout(const RECT& rcAvail)
{
    DuiVBox::Layout(rcAvail);
    SyncHeaderInset();
}

void FileShareAuditScreen::SyncHeaderInset()
{
    if (m_header != nullptr && m_table != nullptr)
        m_header->SetTrailingInset(m_table->ScrollBarWidth());
}

void FileShareAuditScreen::PaintRowThunk(void* user, HDC hdc, int rowIndex, const RECT& rowRect, bool selected,
                                         bool hover)
{
    static_cast<FileShareAuditScreen*>(user)->PaintRow(hdc, rowIndex, rowRect, selected, hover);
}

void FileShareAuditScreen::PaintRow(HDC hdc, int rowIndex, const RECT& rowRect, bool selected, bool hover)
{
    if (rowIndex < 0 || rowIndex >= static_cast<int>(m_items.size()) || m_header == nullptr)
        return;
    const adminapi::FileShareAuditItem& item = m_items[static_cast<size_t>(rowIndex)];

    if (selected)
        ui::FillRoundRect(hdc, rowRect, theme::kFlamingo50, 0);
    else if (hover)
        ui::FillRoundRect(hdc, rowRect, theme::kInk25, 0);

    std::vector<ui::ColumnBound> b;
    ui::ColumnHeader::ComputeColumnBounds(m_header->Columns(), rowRect.left, rowRect.right, b);
    if (b.size() != static_cast<size_t>(kColumnCount))
        return;

    std::vector<CString> cells;
    RowCells(item, cells);
    HFONT font = ui::UiFont(theme::kFontSizeNormal, false);
    const UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
    for (int i = 0; i < kColumnCount; ++i)
    {
        //操作人、动作与路径用正文色，失败的结果用红色，其余用次要色
        COLORREF color = theme::kInk500;
        if (i == kColActor || i == kColAction || i == kColPath)
            color = theme::kInk700;
        else if (i == kColResult && item.m_result != 0)
            color = theme::kColorDanger;
        const RECT rc = { b[i].m_left + kCellPad, rowRect.top, b[i].m_right - kCellPad, rowRect.bottom };
        if (rc.right > rc.left)
            ui::DrawTextLine(hdc, cells[static_cast<size_t>(i)], rc, color, font, flags);
    }
    ui::FillLine(hdc, rowRect.left, rowRect.bottom - 1, rowRect.right - rowRect.left, 1, theme::kInk50);
}

void FileShareAuditScreen::OnPageThunk(void* user, int newPage)
{
    static_cast<FileShareAuditScreen*>(user)->GoToPage(newPage);
}

} // namespace flamingoadmin
