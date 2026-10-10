/**
 *  flamingoAdmin —— 「文件共享」页实现，说明见 FileSharesScreen.h。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "Screens/FileSharesScreen.h"
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
const int kSideMargin     = 24;   // 内容区左 / 右 / 下统一外边距
const int kRowGap         = 12;   // 筛选行、表格卡、详情卡之间的间距
const int kBarH           = 36;   // 筛选行高度
const int kBarGap         = 10;   // 筛选行内控件之间的间距
const int kPickerLabelW   = 40;   // 「企业」标签宽度
const int kPickerW        = 260;  // 企业筛选框宽度
const int kSearchW        = 240;  // 分享者搜索框宽度
const int kStateW         = 120;  // 状态筛选下拉框宽度
const int kTableHeaderH   = 40;   // 表头高度
const int kRowHeight      = 48;   // 共享表格行高
const int kPagerH         = 42;   // 分页条高度
const int kCellPad        = 14;   // 单元格左右内边距
const int kDetailPad      = 16;   // 详情卡内边距
const int kDetailTitleH   = 28;   // 详情卡标题高度
const int kDetailTitleGap = 8;    // 详情卡标题与信息网格之间的间距
const int kDetailCaptionW = 84;   // 信息网格名称部分的宽度（四个字的名称之后与取值隔开）
const int kDetailColumns  = 2;    // 信息网格的列数
const int kPathLines      = 2;    // 显示路径最多显示的行数
const int kMembersW       = 460;  // 详情卡右侧成员区的宽度（够放五项权限的全称）
const int kDetailSplitGap = 24;   // 详情卡左右两部分之间的间距
const int kMemberTitleH   = 28;   // 「成员与权限」标题高度
const int kMemberRowH     = 36;   // 成员表格行高
const int kMemberNameW    = 190;  // 成员表格里名称一列的宽度（含左内边距）

// ---- 共享表格的列（像素）----
const int kColOwnerW        = 140;  // 分享者
const int kColConversationW = 160;  // 所在会话
const int kColMembersW      = 70;   // 成员数
const int kColStateW        = 80;   // 状态
const int kColCreateTimeW   = 170;  // 发起时间
const int kColActionsW      = 96;   // 操作（强制取消）

// 企业筛选框：拉企业列表时的状态筛选（-1 表示全部状态）与条数（一次拉 1000 条，平台企业数远小于此）
const int kEntStatusAll     = -1;
const int kEntPickerPageMax = 1000;

// 搜索防抖：停止输入后多久才查询（毫秒）
const UINT kSearchDebounceMs = 400;

// 共享表格的列定义，顺序与 FileSharesScreen::Column 一致；最后加一列空标题对齐操作列
std::vector<ui::ColumnSpec> BuildColumns()
{
    std::vector<ui::ColumnSpec> cols;
    cols.push_back(ui::ColumnSpec(_T("分享者"), kColOwnerW));
    cols.push_back(ui::ColumnSpec(_T("显示路径"), 0, true));
    cols.push_back(ui::ColumnSpec(_T("所在会话"), kColConversationW));
    cols.push_back(ui::ColumnSpec(_T("成员数"), kColMembersW));
    cols.push_back(ui::ColumnSpec(_T("状态"), kColStateW));
    cols.push_back(ui::ColumnSpec(_T("发起时间"), kColCreateTimeW));
    cols.push_back(ui::ColumnSpec(_T("操作"), kColActionsW));
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
std::unique_ptr<DuiLabel> MakeLabel(LPCTSTR text, COLORREF color, int pointSize, bool bold)
{
    auto label = std::make_unique<DuiLabel>();
    label->SetText(text);
    label->SetTextColor(color);
    label->SetTextPointSize(pointSize, bold);
    label->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    return label;
}

} // namespace

FileSharesScreen::FileSharesScreen()
    : m_hostWnd(nullptr)
    , m_pickerBox(nullptr)
    , m_picker(nullptr)
    , m_search(nullptr)
    , m_stateBox(nullptr)
    , m_header(nullptr)
    , m_table(nullptr)
    , m_pager(nullptr)
    , m_detailTitle(nullptr)
    , m_detail(nullptr)
    , m_memberTitle(nullptr)
    , m_memberTable(nullptr)
    , m_isPlatform(false)
    , m_enterpriseId(0)
    , m_stateFilter(kFilterActive)
    , m_page(1)
    , m_total(0)
    , m_loadState(kLoadIdle)
    , m_reloadPending(false)
    , m_selectedShareId(0)
    , m_membersInFlight(0)
    , m_memberLoadState(kLoadIdle)
    , m_cancelling(false)
{
    // 页头与「刷新」属于外层的文件共享页（FileShareCenterScreen，2026-10-08），本页从筛选行开始

    // ---- 筛选行：[企业 筛选框]（仅平台超管） [搜索分享者] [状态] ……----
    auto filterRow = std::make_unique<DuiHBox>();
    filterRow->SetGap(kBarGap);
    {
        auto pickerBox = std::make_unique<DuiHBox>();
        pickerBox->AddChild(MakeLabel(_T("企业"), theme::kInk500, theme::kFontSizeNormal, false),
                            Hint().Fixed(kPickerLabelW));
        auto picker = std::make_unique<DuiComboBox>();
        picker->SetCtrlId(kIdFsEnterprise);
        //企业多时可以输入文字筛选
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

        auto search = std::make_unique<DuiEdit>();
        search->SetCtrlId(kIdFsSearch);
        search->SetPlaceholder(_T("搜索分享者的昵称或账号"));
        search->SetBgColor(theme::kInk25);
        search->SetShowBorder(true);
        m_search = search.get();
        filterRow->AddChild(std::move(search), Hint().Fixed(kSearchW));

        auto state = std::make_unique<DuiComboBox>();
        state->SetCtrlId(kIdFsState);
        state->AddString(_T("全部状态"));
        state->AddString(fstext::StateText(fstext::kStateActive));
        state->AddString(fstext::StateText(fstext::kStateCanceled));
        state->SetCurSel(kFilterActive, false);
        m_stateBox = state.get();
        filterRow->AddChild(std::move(state), Hint().Fixed(kStateW));

        filterRow->AddChild(std::make_unique<DuiControl>(), Hint().Weight(1));
    }
    AddChild(std::move(filterRow), Hint().Fixed(kBarH).Margin(kSideMargin, 0, kSideMargin, kRowGap));

    // ---- 表格卡：表头 + 表格 + 分页条 ----
    auto card = MakeCard();
    {
        auto head = std::make_unique<ui::ColumnHeader>();
        head->SetColumns(BuildColumns());
        head->SetCellPadding(kCellPad);
        m_header = head.get();
        card->AddChild(std::move(head), Hint().Fixed(kTableHeaderH));

        auto table = std::make_unique<ui::ActionTable>();
        table->SetCtrlId(kIdFsTable);
        table->SetRowHeight(kRowHeight);
        table->SetRowPainter(&FileSharesScreen::PaintRowThunk, this);
        table->SetActionAreaWidth(kColActionsW);
        table->AddAction(_T("强制取消"), theme::kColorDanger);
        table->SetActionCallback(&FileSharesScreen::OnRowActionThunk, this);
        table->SetActionVisibility(&FileSharesScreen::IsActionVisibleThunk, this);
        m_table = table.get();
        card->AddChild(std::move(table), Hint().Weight(1));

        auto pager = std::make_unique<ui::PagerBar>();
        pager->SetSidePadding(kCellPad);
        pager->SetPageCallback(&FileSharesScreen::OnPageThunk, this);
        m_pager = pager.get();
        card->AddChild(std::move(pager), Hint().Fixed(kPagerH));
    }
    AddChild(std::move(card), Hint().Weight(1).Margin(kSideMargin, 0, kSideMargin, kRowGap));

    // ---- 详情卡：左边共享信息，右边成员与权限 ----
    auto detailCard = MakeCard();
    detailCard->SetPadding(kDetailPad, kDetailPad, kDetailPad, kDetailPad);
    int detailH = 0;
    {
        auto split = std::make_unique<DuiHBox>();
        split->SetGap(kDetailSplitGap);

        auto info = std::make_unique<DuiVBox>();
        auto title = MakeLabel(_T(""), theme::kInk700, theme::kFontSizeNormal, true);
        m_detailTitle = title.get();
        info->AddChild(std::move(title), Hint().Fixed(kDetailTitleH).Margin(0, 0, 0, kDetailTitleGap));

        auto grid = std::make_unique<ui::KeyValueGrid>();
        grid->SetColumnCount(kDetailColumns);
        grid->SetCaptionWidth(kDetailCaptionW);
        //条目的添加顺序即 DetailItem 的取值
        grid->AddItem(_T("分享者"));
        grid->AddItem(_T("所在会话"));
        grid->AddItem(_T("共享类型"));
        grid->AddItem(_T("状态"));
        grid->AddItem(_T("发起时间"));
        grid->AddItem(_T("取消时间"));
        grid->AddItem(_T("取消原因"));
        grid->AddItem(_T("所属企业"));
        grid->AddWideItem(_T("显示路径"), kPathLines);
        grid->EndRow();
        m_detail = grid.get();
        //详情卡的高度按信息网格的实际行数算：内边距 + 标题 + 网格
        detailH = kDetailPad * 2 + kDetailTitleH + kDetailTitleGap + grid->ContentHeight();
        info->AddChild(std::move(grid), Hint().Weight(1));
        split->AddChild(std::move(info), Hint().Weight(1));

        auto members = std::make_unique<DuiVBox>();
        auto memberTitle = MakeLabel(_T("成员与权限"), theme::kInk700, theme::kFontSizeNormal, true);
        m_memberTitle = memberTitle.get();
        members->AddChild(std::move(memberTitle), Hint().Fixed(kMemberTitleH).Margin(0, 0, 0, kDetailTitleGap));
        auto memberTable = std::make_unique<ui::ActionTable>();
        memberTable->SetCtrlId(kIdFsMemberTable);
        memberTable->SetRowHeight(kMemberRowH);
        memberTable->SetRowPainter(&FileSharesScreen::PaintMemberRowThunk, this);
        m_memberTable = memberTable.get();
        members->AddChild(std::move(memberTable), Hint().Weight(1));
        split->AddChild(std::move(members), Hint().Fixed(kMembersW));

        detailCard->AddChild(std::move(split), Hint().Weight(1));
    }
    AddChild(std::move(detailCard), Hint().Fixed(detailH).Margin(kSideMargin, 0, kSideMargin, kSideMargin));

    UpdateView();
    UpdateDetail();
}

// ---------------------------------------------------------------------------
// 进入界面与身份
// ---------------------------------------------------------------------------

void FileSharesScreen::OnEnter()
{
    const AdminSession& session = AdminSession::Instance();
    //换了登录身份：清掉上一个身份留下的企业列表与共享
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

    FA_LOGI(_T("enter file shares screen, platform admin: %d, enterprise filter: %lld"), m_isPlatform ? 1 : 0,
            m_enterpriseId);
    if (m_isPlatform && m_enterpriseIds.empty())
        StartLoadEnterprises();
    //每次进入都刷新：分享者随时可能发起或取消共享
    Refresh();
}

void FileSharesScreen::Refresh()
{
    StartLoadShares();
    if (m_selectedShareId > 0)
        StartLoadMembers();
}

void FileSharesScreen::ResetState()
{
    m_enterpriseIds.clear();
    m_enterpriseNames.clear();
    m_enterpriseId = 0;
    if (m_picker != nullptr)
    {
        m_picker->ResetContent();
        m_picker->AddString(_T("全部企业"));
        m_picker->SetCurSel(0, false);
        m_picker->SetText(_T("全部企业"));
    }
    m_keyword.Empty();
    if (m_search != nullptr)
        m_search->SetText(_T(""));
    m_stateFilter = kFilterActive;
    if (m_stateBox != nullptr)
        m_stateBox->SetCurSel(kFilterActive, false);
    m_page = 1;
    m_total = 0;
    m_loadState = kLoadIdle;
    m_loadError.Empty();
    m_reloadPending = false;
    m_shares.clear();
    m_selectedShareId = 0;
    m_members.clear();
    m_memberLoadState = kLoadIdle;
    m_memberError.Empty();
    m_pendingCancel = adminapi::FileShareItem();
    m_cancelling = false;
    if (m_table != nullptr)
        m_table->SetCurSel(-1, false);
    UpdateView();
    UpdateDetail();
}

// ---------------------------------------------------------------------------
// 发请求
// ---------------------------------------------------------------------------

void FileSharesScreen::StartLoadEnterprises()
{
    HttpRequest request = adminapi::MakeEnterpriseListRequest(ServerConfig(), AdminSession::Instance().Token(),
                                                              CString(), kEntStatusAll, 1, kEntPickerPageMax);
    m_port.Send(request, kApiTagFsEnterprises);
}

void FileSharesScreen::StartLoadShares()
{
    //上一次查询还没回来：记下来，结果回来后按最新的条件再查一次
    if (m_loadState == kLoadLoading)
    {
        m_reloadPending = true;
        return;
    }
    m_loadState = kLoadLoading;
    m_reloadPending = false;
    HttpRequest request = adminapi::MakeFileShareListRequest(ServerConfig(), AdminSession::Instance().Token(),
                                                             m_enterpriseId, m_keyword, m_stateFilter, m_page,
                                                             kPageSize);
    m_port.Send(request, kApiTagFsList);
    UpdateView();
}

void FileSharesScreen::StartLoadMembers()
{
    if (m_selectedShareId <= 0)
        return;
    ++m_membersInFlight;
    m_memberLoadState = kLoadLoading;
    HttpRequest request = adminapi::MakeFileShareMembersRequest(ServerConfig(), AdminSession::Instance().Token(),
                                                                m_selectedShareId);
    m_port.Send(request, kApiTagFsMembers);
    UpdateDetail();
}

// ---------------------------------------------------------------------------
// 应答
// ---------------------------------------------------------------------------

void FileSharesScreen::OnApiResult(ApiTag tag, const HttpResult& http)
{
    switch (tag)
    {
    //企业筛选框的企业列表
    case kApiTagFsEnterprises:
        OnEnterprisesLoaded(http);
        break;
    //共享列表
    case kApiTagFsList:
        OnSharesLoaded(http);
        break;
    //选中共享的成员
    case kApiTagFsMembers:
        OnMembersLoaded(http);
        break;
    //强制取消
    case kApiTagFsCancel:
        OnCancelDone(http);
        break;
    //其余标签不属于本界面
    default:
        break;
    }
}

void FileSharesScreen::OnEnterprisesLoaded(const HttpResult& http)
{
    const EnterpriseListResult r = adminapi::ParseEnterpriseListResponse(http);
    if (!r.Succeeded())
    {
        FA_LOGW(_T("file shares screen: load enterprises failed, code: %d"), r.m_code);
        m_port.Toast(_T("企业列表加载失败：") + r.m_message, true);
        return;
    }
    m_enterpriseIds.clear();
    m_enterpriseNames.clear();
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
        m_enterpriseNames.push_back(r.m_list[i].m_name);
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
    //详情卡里的「所属企业」要用企业名
    UpdateDetail();
}

void FileSharesScreen::OnSharesLoaded(const HttpResult& http)
{
    const adminapi::FileShareListResult r = adminapi::ParseFileShareListResponse(http);

    //加载期间查询条件又变了：这份结果已经过时，丢弃并按最新条件重查
    if (m_reloadPending)
    {
        m_reloadPending = false;
        m_loadState = kLoadIdle;
        StartLoadShares();
        return;
    }
    if (!r.Succeeded())
    {
        m_loadState = kLoadFailed;
        m_loadError = r.m_message;
        m_shares.clear();
        FA_LOGW(_T("file shares screen: load shares failed, code: %d"), r.m_code);
        UpdateView();
        UpdateDetail();
        return;
    }

    m_loadState = kLoadLoaded;
    m_loadError.Empty();
    m_shares = r.m_list;
    m_total = r.m_total;
    FA_LOGI(_T("file shares screen: %d shares loaded (total %d, page %d)"), static_cast<int>(m_shares.size()),
            m_total, m_page);

    //当前页超出总页数（例如共享被取消后不再满足筛选条件）：退到最后一页重查
    if (m_shares.empty() && m_total > 0 && m_page > PageCount())
    {
        m_page = PageCount();
        StartLoadShares();
        return;
    }

    //选中的共享仍在本页时保持选中（强制取消、刷新之后），否则清空详情
    int selectedRow = -1;
    for (size_t i = 0; i < m_shares.size(); ++i)
    {
        if (m_shares[i].m_shareId == m_selectedShareId)
        {
            selectedRow = static_cast<int>(i);
            break;
        }
    }
    if (selectedRow < 0 && m_selectedShareId > 0)
    {
        m_selectedShareId = 0;
        m_members.clear();
        m_memberLoadState = kLoadIdle;
    }
    UpdateView();
    if (m_table != nullptr)
        m_table->SetCurSel(selectedRow, false);
    UpdateDetail();
}

void FileSharesScreen::OnMembersLoaded(const HttpResult& http)
{
    if (m_membersInFlight > 0)
        --m_membersInFlight;
    //请求按发出的顺序返回（HttpWorker 单线程串行）：还有更新的请求在路上时，这份结果属于之前选中的共享
    if (m_membersInFlight > 0 || m_selectedShareId <= 0)
        return;
    const adminapi::FileShareMembersResult r = adminapi::ParseFileShareMembersResponse(http);
    if (!r.Succeeded())
    {
        m_memberLoadState = kLoadFailed;
        m_memberError = r.m_message;
        m_members.clear();
        FA_LOGW(_T("file shares screen: load members failed, share: %lld, code: %d"), m_selectedShareId, r.m_code);
        UpdateDetail();
        return;
    }
    m_memberLoadState = kLoadLoaded;
    m_memberError.Empty();
    m_members = r.m_members;
    UpdateDetail();
}

void FileSharesScreen::OnCancelDone(const HttpResult& http)
{
    m_cancelling = false;
    const EnterpriseActionResult r = adminapi::ParseEnterpriseActionResponse(http);
    const adminapi::FileShareItem target = m_pendingCancel;
    m_pendingCancel = adminapi::FileShareItem();
    if (!r.Succeeded())
    {
        FA_LOGW(_T("file shares screen: force cancel failed, share: %lld, code: %d"), target.m_shareId, r.m_code);
        m_port.Toast(_T("强制取消失败：") + r.m_message, true);
        //已被别人取消、或共享已不存在：列表里的状态过时了，重新拉取
        Refresh();
        return;
    }
    FA_LOGI(_T("file shares screen: share %lld force canceled"), target.m_shareId);
    m_port.Toast(_T("已强制取消「") + target.m_name + _T("」"), false);
    Refresh();
}

// ---------------------------------------------------------------------------
// 用户操作
// ---------------------------------------------------------------------------

void FileSharesScreen::OnEnterpriseSelected(int index)
{
    //下标 0 是「全部企业」，其余是企业列表下标加 1
    if (index < 0 || index > static_cast<int>(m_enterpriseIds.size()))
        return;
    const long long id = (index == 0) ? 0 : m_enterpriseIds[static_cast<size_t>(index - 1)];
    if (id == m_enterpriseId)
        return;
    m_enterpriseId = id;
    m_page = 1;
    FA_LOGI(_T("file shares screen: enterprise filter: %lld"), id);
    StartLoadShares();
}

void FileSharesScreen::OnSearchChanged()
{
    //停止输入一小段时间后再查，免得每敲一个字发一次请求
    if (m_hostWnd != nullptr && ::IsWindow(m_hostWnd))
    {
        ::SetTimer(m_hostWnd, kSearchTimerId, kSearchDebounceMs, nullptr);
        return;
    }
    OnSearchTimer();
}

void FileSharesScreen::OnSearchTimer()
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
    StartLoadShares();
}

void FileSharesScreen::OnStateChanged(int index)
{
    if (index < kFilterAll || index > kFilterCanceled || index == m_stateFilter)
        return;
    m_stateFilter = index;
    m_page = 1;
    StartLoadShares();
}

void FileSharesScreen::OnRowSelected(int index)
{
    const long long id = (index >= 0 && index < static_cast<int>(m_shares.size()))
                             ? m_shares[static_cast<size_t>(index)].m_shareId : 0;
    if (id == m_selectedShareId)
        return;
    m_selectedShareId = id;
    m_members.clear();
    m_memberLoadState = kLoadIdle;
    m_memberError.Empty();
    if (id > 0)
        StartLoadMembers();
    UpdateDetail();
}

void FileSharesScreen::GoToPage(int page)
{
    if (page < 1 || page > PageCount() || page == m_page)
        return;
    m_page = page;
    StartLoadShares();
}

void FileSharesScreen::OnCancelClicked(int rowIndex)
{
    if (rowIndex < 0 || rowIndex >= static_cast<int>(m_shares.size()))
        return;
    const adminapi::FileShareItem& item = m_shares[static_cast<size_t>(rowIndex)];
    if (item.m_state != fstext::kStateActive)
        return;
    if (m_cancelling || m_pendingCancel.m_shareId > 0)
    {
        m_port.Toast(_T("上一个强制取消还在处理，请稍候"), false);
        return;
    }
    m_pendingCancel = item;
    m_port.Confirm(_T("强制取消共享"), CancelConfirmText(item), _T("强制取消"), true, this);
}

void FileSharesScreen::OnConfirmResult(bool confirmed)
{
    if (m_pendingCancel.m_shareId <= 0)
        return;
    if (!confirmed)
    {
        m_pendingCancel = adminapi::FileShareItem();
        return;
    }
    m_cancelling = true;
    FA_LOGI(_T("file shares screen: force cancel share %lld"), m_pendingCancel.m_shareId);
    HttpRequest request = adminapi::MakeFileShareCancelRequest(ServerConfig(), AdminSession::Instance().Token(),
                                                               m_pendingCancel.m_shareId);
    m_port.Send(request, kApiTagFsCancel);
}

// ---------------------------------------------------------------------------
// 文字
// ---------------------------------------------------------------------------

void FileSharesScreen::RowCells(const adminapi::FileShareItem& item, std::vector<CString>& cells)
{
    cells.clear();
    cells.resize(kColumnCount);
    CString owner = item.m_ownerName.IsEmpty() ? item.m_ownerAccount : item.m_ownerName;
    if (!item.m_ownerName.IsEmpty() && !item.m_ownerAccount.IsEmpty())
        owner += _T("（") + item.m_ownerAccount + _T("）");
    cells[kColOwner] = owner.IsEmpty() ? CString(_T("-")) : owner;
    cells[kColPath] = item.m_displayPath.IsEmpty() ? item.m_name : item.m_displayPath;
    cells[kColConversation] = fstext::ConversationText(item.m_ctype, item.m_conversationName, item.m_cid);
    cells[kColMembers].Format(_T("%d"), item.m_memberCount);
    cells[kColState] = fstext::StateText(item.m_state);
    cells[kColCreateTime] = fstext::FormatTime(item.m_createTime);
}

CString FileSharesScreen::CancelConfirmText(const adminapi::FileShareItem& item)
{
    CString owner = item.m_ownerName.IsEmpty() ? item.m_ownerAccount : item.m_ownerName;
    //只有一位成员时不说「全部」
    CString members;
    if (item.m_memberCount == 1)
        members = _T("这位成员");
    else
        members.Format(_T("全部 %d 位成员"), item.m_memberCount);
    CString text;
    text.Format(_T("确定强制取消 %s 共享的「%s」吗？取消后%s都不能再访问，正在进行的访问与传输会立即结束，")
                _T("分享者的客户端会删除这条共享。此操作不能撤销。"),
                owner.GetString(), item.m_name.GetString(), members.GetString());
    return text;
}

// ---------------------------------------------------------------------------
// 显示
// ---------------------------------------------------------------------------

CString FileSharesScreen::OverlayText() const
{
    if (m_loadState == kLoadFailed)
        return _T("加载失败：") + m_loadError;
    if (m_shares.empty() && (m_loadState == kLoadLoading || m_loadState == kLoadIdle))
        return CString(_T("加载中…"));
    if (m_shares.empty())
    {
        const bool filtered = !m_keyword.IsEmpty() || m_stateFilter != kFilterAll || m_enterpriseId > 0;
        return CString(filtered ? _T("没有符合条件的共享") : _T("暂无文件共享"));
    }
    return CString();
}

int FileSharesScreen::PageCount() const
{
    const int n = (m_total + kPageSize - 1) / kPageSize;
    return (n < 1) ? 1 : n;
}

bool FileSharesScreen::IsEnterprisePickerVisible() const
{
    return m_pickerBox != nullptr && m_pickerBox->IsVisible();
}

CString FileSharesScreen::EnterprisePickerText() const
{
    return (m_picker != nullptr) ? m_picker->GetText() : CString();
}

const adminapi::FileShareItem* FileSharesScreen::SelectedShare() const
{
    if (m_selectedShareId <= 0)
        return nullptr;
    for (size_t i = 0; i < m_shares.size(); ++i)
    {
        if (m_shares[i].m_shareId == m_selectedShareId)
            return &m_shares[i];
    }
    return nullptr;
}

CString FileSharesScreen::EnterpriseName(long long enterpriseId) const
{
    //企业管理员拉不到企业列表（enterprise/list.do 要求平台管理员），而他只看得到本企业的共享
    if (!m_isPlatform)
        return CString(_T("本企业"));
    for (size_t i = 0; i < m_enterpriseIds.size(); ++i)
    {
        if (m_enterpriseIds[i] == enterpriseId)
            return m_enterpriseNames[i];
    }
    CString text;
    text.Format(_T("%lld"), enterpriseId);
    return text;
}

CString FileSharesScreen::DetailTitle() const
{
    return (m_detailTitle != nullptr) ? m_detailTitle->GetText() : CString();
}

CString FileSharesScreen::MemberOverlayText() const
{
    return (m_memberTable != nullptr) ? m_memberTable->GetOverlayText() : CString();
}

void FileSharesScreen::UpdateView()
{
    if (m_table != nullptr)
    {
        m_table->SetRowCount(static_cast<int>(m_shares.size()));
        m_table->SetOverlayText(OverlayText());
        m_table->Invalidate();
        //行数变化可能让滚动条出现或消失
        SyncHeaderInset();
    }
    if (m_pager != nullptr)
        m_pager->SetPageInfo(m_page, m_total, kPageSize);
}

void FileSharesScreen::UpdateDetail()
{
    const adminapi::FileShareItem* share = SelectedShare();
    if (m_detail != nullptr)
        m_detail->ClearValues();
    if (share == nullptr)
    {
        if (m_detailTitle != nullptr)
            m_detailTitle->SetText(_T("选择一个共享查看详情"));
        if (m_memberTitle != nullptr)
            m_memberTitle->SetText(_T("成员与权限"));
        if (m_memberTable != nullptr)
        {
            m_memberTable->SetRowCount(0);
            m_memberTable->SetOverlayText(_T("未选择共享"));
        }
        return;
    }

    if (m_detailTitle != nullptr)
        m_detailTitle->SetText(share->m_name.IsEmpty() ? CString(_T("-")) : share->m_name);
    if (m_detail != nullptr)
    {
        std::vector<CString> cells;
        RowCells(*share, cells);
        m_detail->SetValue(kDetailOwner, cells[kColOwner]);
        m_detail->SetValue(kDetailConversation, cells[kColConversation]);
        m_detail->SetValue(kDetailKind, fstext::KindName(share->m_kind));
        m_detail->SetValue(kDetailState, cells[kColState]);
        m_detail->SetValue(kDetailCreateTime, cells[kColCreateTime]);
        m_detail->SetValue(kDetailCancelTime, fstext::FormatTime(share->m_cancelTime));
        m_detail->SetValue(kDetailCancelReason, fstext::CancelReasonText(share->m_cancelReason));
        m_detail->SetValue(kDetailEnterprise, EnterpriseName(share->m_enterpriseId));
        m_detail->SetValue(kDetailPath, share->m_displayPath);
    }

    if (m_memberTitle != nullptr)
    {
        CString title;
        if (m_memberLoadState == kLoadLoaded)
            title.Format(_T("成员与权限（%d）"), static_cast<int>(m_members.size()));
        else
            title = _T("成员与权限");
        m_memberTitle->SetText(title);
    }
    if (m_memberTable != nullptr)
    {
        CString overlay;
        if (m_memberLoadState == kLoadFailed)
            overlay = _T("加载失败：") + m_memberError;
        else if (m_memberLoadState != kLoadLoaded)
            overlay = _T("加载中…");
        else if (m_members.empty())
            overlay = _T("没有成员");
        m_memberTable->SetRowCount(static_cast<int>(m_members.size()));
        m_memberTable->SetOverlayText(overlay);
        m_memberTable->Invalidate();
    }
}

void FileSharesScreen::Layout(const RECT& rcAvail)
{
    DuiVBox::Layout(rcAvail);
    SyncHeaderInset();
}

void FileSharesScreen::SyncHeaderInset()
{
    if (m_header != nullptr && m_table != nullptr)
        m_header->SetTrailingInset(m_table->ScrollBarWidth());
}

void FileSharesScreen::PaintRowThunk(void* user, HDC hdc, int rowIndex, const RECT& rowRect, bool selected,
                                     bool hover)
{
    static_cast<FileSharesScreen*>(user)->PaintRow(hdc, rowIndex, rowRect, selected, hover);
}

void FileSharesScreen::PaintRow(HDC hdc, int rowIndex, const RECT& rowRect, bool selected, bool hover)
{
    if (rowIndex < 0 || rowIndex >= static_cast<int>(m_shares.size()) || m_header == nullptr)
        return;
    const adminapi::FileShareItem& item = m_shares[static_cast<size_t>(rowIndex)];

    //选中行用品牌色的浅底，悬停行用灰底
    if (selected)
        ui::FillRoundRect(hdc, rowRect, theme::kFlamingo50, 0);
    else if (hover)
        ui::FillRoundRect(hdc, rowRect, theme::kInk25, 0);

    std::vector<ui::ColumnBound> b;
    ui::ColumnHeader::ComputeColumnBounds(m_header->Columns(), rowRect.left, rowRect.right, b);
    if (b.size() < static_cast<size_t>(kColumnCount))
        return;

    std::vector<CString> cells;
    RowCells(item, cells);
    HFONT font = ui::UiFont(theme::kFontSizeNormal, false);
    const UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
    for (int i = 0; i < kColumnCount; ++i)
    {
        //分享者与路径用正文色，有效状态用绿色，其余用次要色
        COLORREF color = theme::kInk500;
        if (i == kColOwner || i == kColPath)
            color = theme::kInk700;
        else if (i == kColState && item.m_state == fstext::kStateActive)
            color = theme::kColorOk;
        //路径按中间省略显示，保留开头的盘符与末尾的文件夹名
        const UINT cellFlags = (i == kColPath) ? ((flags & ~DT_END_ELLIPSIS) | DT_PATH_ELLIPSIS) : flags;
        const RECT rc = { b[i].m_left + kCellPad, rowRect.top, b[i].m_right - kCellPad, rowRect.bottom };
        if (rc.right > rc.left)
            ui::DrawTextLine(hdc, cells[static_cast<size_t>(i)], rc, color, font, cellFlags);
    }

    //行底分隔线
    ui::FillLine(hdc, rowRect.left, rowRect.bottom - 1, rowRect.right - rowRect.left, 1, theme::kInk50);
}

void FileSharesScreen::OnRowActionThunk(void* user, int rowIndex, int actionIndex)
{
    FileSharesScreen* self = static_cast<FileSharesScreen*>(user);
    switch (actionIndex)
    {
    //「强制取消」：请宿主弹确认框，确认后再发请求
    case kActionCancel:
        self->OnCancelClicked(rowIndex);
        break;
    //没有其它操作
    default:
        break;
    }
}

bool FileSharesScreen::IsActionVisibleThunk(void* user, int rowIndex, int actionIndex)
{
    const FileSharesScreen* self = static_cast<const FileSharesScreen*>(user);
    if (rowIndex < 0 || rowIndex >= static_cast<int>(self->m_shares.size()))
        return false;
    //「强制取消」只对有效的共享显示
    if (actionIndex == kActionCancel)
        return self->m_shares[static_cast<size_t>(rowIndex)].m_state == fstext::kStateActive;
    return true;
}

void FileSharesScreen::OnPageThunk(void* user, int newPage)
{
    static_cast<FileSharesScreen*>(user)->GoToPage(newPage);
}

void FileSharesScreen::PaintMemberRowThunk(void* user, HDC hdc, int rowIndex, const RECT& rowRect, bool selected,
                                           bool hover)
{
    (void)selected;
    (void)hover;
    static_cast<FileSharesScreen*>(user)->PaintMemberRow(hdc, rowIndex, rowRect);
}

void FileSharesScreen::PaintMemberRow(HDC hdc, int rowIndex, const RECT& rowRect)
{
    if (rowIndex < 0 || rowIndex >= static_cast<int>(m_members.size()))
        return;
    const adminapi::FileShareMemberItem& m = m_members[static_cast<size_t>(rowIndex)];
    CString name = m.m_name.IsEmpty() ? m.m_account : m.m_name;
    if (!m.m_name.IsEmpty() && !m.m_account.IsEmpty())
        name += _T("（") + m.m_account + _T("）");
    const UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
    //与表格边框之间留出单元格内边距
    const RECT nameRc = { rowRect.left + kCellPad, rowRect.top, rowRect.left + kMemberNameW, rowRect.bottom };
    ui::DrawTextLine(hdc, name, nameRc, theme::kInk700, ui::UiFont(theme::kFontSizeNormal, false), flags);
    const RECT permRc = { nameRc.right + kCellPad, rowRect.top, rowRect.right - kCellPad, rowRect.bottom };
    if (permRc.right > permRc.left)
    {
        ui::DrawTextLine(hdc, fstext::PermsText(m.m_perms), permRc, m.m_perms == 0 ? theme::kInk400 : theme::kInk600,
                         ui::UiFont(theme::kFontSizeSmall, false), flags);
    }
    ui::FillLine(hdc, rowRect.left, rowRect.bottom - 1, rowRect.right - rowRect.left, 1, theme::kInk50);
}

} // namespace flamingoadmin
