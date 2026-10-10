/**
 *  flamingoAdmin —— 企业问答页实现，说明见 EnterpriseQaScreen.h。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "Screens/EnterpriseQaScreen.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/CommonWidgets.h"
#include "Widgets/ActionTable.h"
#include "Widgets/ColumnHeader.h"
#include "Widgets/UiHelpers.h"
#include "Util/Logger.h"
#include "Util/TimeText.h"   // 时间显示格式

#include "Controls/Basic/DuiButton.h"
#include "Controls/Basic/DuiLabel.h"
#include "Controls/Input/DuiComboBox.h"
#include "Controls/Input/DuiEdit.h"

using namespace balloonwjui;

namespace flamingoadmin {

using Hint = balloonwjui::DuiLayout::Hint;

namespace {

// ---- 布局常量（96 dpi 逻辑像素），与敏感词页一致 ----
const int kHeaderH       = 100;  // 页头高度
const int kSideMargin    = 24;   // 内容区左 / 右 / 下统一外边距
const int kRowGap        = 12;   // 各行之间的间距
const int kBarH          = 36;   // 企业选择行与工具行的高度
const int kPickerLabelW  = 40;   // 「企业」标签宽度
const int kPickerW       = 300;  // 企业选择框宽度
const int kSearchW       = 280;  // 搜索框宽度
const int kButtonW       = 96;   // 「刷新」「新增问答」按钮宽度
const int kButtonGap     = 8;    // 按钮之间的间距
const int kTableHeaderH  = 40;   // 表头高度
const int kRowHeight     = 60;   // 表格行高（问题与答案摘要两行）
const int kCellPad       = 14;   // 单元格左右内边距
const int kLineH         = 22;   // 行内问题、答案各占的高度
const int kColScope      = 240;  // 应用范围
const int kColCreator    = 120;  // 修改人
const int kColTime       = 150;  // 修改时间
const int kColActions    = 120;  // 操作（编辑 / 删除）

// 表格的列下标，与 BuildColumns 的顺序一致
enum QaColumn
{
    kQcQuestion = 0,   // 问题与答案摘要（占满剩余宽度）
    kQcScope    = 1,   // 应用范围
    kQcCreator  = 2,   // 修改人
    kQcTime     = 3,   // 修改时间
    kQcActions  = 4,   // 操作
};

// 行内操作的下标，与 AddAction 的顺序一致
enum RowAction
{
    kActionEdit   = 0,   // 编辑
    kActionDelete = 1,   // 删除
};

// 企业选择框：拉企业列表时的状态筛选（-1 表示全部状态）与条数
const int kEntStatusAll     = -1;
const int kEntPickerPageMax = 1000;

// Unix 毫秒换算成 FILETIME（自 1601-01-01 起的 100 纳秒数）所需的常量
const ULONGLONG k100nsPerMs     = 10000ULL;
const ULONGLONG kEpochDiff100ns = 116444736000000000ULL;

// 把 Unix 毫秒格式化成本地时间「YYYY-MM-DD HH:MM」；不大于 0 时显示「-」
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
    return timetext::FormatSystemTime(st, timetext::TimeStyle::Minute);
}

std::vector<ui::ColumnSpec> BuildColumns()
{
    std::vector<ui::ColumnSpec> cols;
    cols.push_back(ui::ColumnSpec(_T("问题"), 0, true));
    cols.push_back(ui::ColumnSpec(_T("应用范围"), kColScope));
    cols.push_back(ui::ColumnSpec(_T("修改人"), kColCreator));
    cols.push_back(ui::ColumnSpec(_T("修改时间"), kColTime));
    cols.push_back(ui::ColumnSpec(_T("操作"), kColActions));
    return cols;
}

std::unique_ptr<DuiVBox> MakeCard()
{
    auto card = std::make_unique<DuiVBox>();
    card->SetBgColor(theme::kInk0);
    card->SetCornerRadius(theme::kRadiusLg);
    card->SetBorderColor(theme::kInk100);
    card->SetBorderWidth(1.0f);
    return card;
}

std::unique_ptr<DuiLabel> MakeLabel(LPCTSTR text, COLORREF color, DWORD align)
{
    auto label = std::make_unique<DuiLabel>();
    label->SetText(text);
    label->SetTextColor(color);
    label->SetTextAlign(align | DT_VCENTER | DT_SINGLELINE);
    return label;
}

std::unique_ptr<DuiButton> MakeButton(UINT id, LPCTSTR text, DuiButton::Variant variant)
{
    auto button = std::make_unique<DuiButton>();
    button->SetCtrlId(id);
    button->SetText(text);
    button->SetVariant(variant);
    button->SetTextPointSize(theme::kFontSizeNormal);
    return button;
}

// 答案摘要：换行换成空格，便于单行显示
CString AnswerDigest(const CString& answer)
{
    CString digest = answer;
    digest.Replace(_T("\r\n"), _T(" "));
    digest.Replace(_T('\n'), _T(' '));
    digest.Replace(_T('\r'), _T(' '));
    return digest;
}

} // namespace

EnterpriseQaScreen::EnterpriseQaScreen()
    : m_sink(nullptr)
    , m_sinkUser(nullptr)
    , m_uiHandler(nullptr)
    , m_uiHandlerUser(nullptr)
    , m_pickerRow(nullptr)
    , m_picker(nullptr)
    , m_search(nullptr)
    , m_addButton(nullptr)
    , m_header(nullptr)
    , m_table(nullptr)
    , m_isPlatform(false)
    , m_enterpriseId(0)
    , m_loadState(kLoadIdle)
    , m_saving(false)
{
    // ---- 页头 ----
    auto header = std::make_unique<ui::PageHeader>();
    header->SetTexts(_T("企业问答"), _T("企业问答"),
                     _T("维护显示在群聊「问答」页签里的问答，可应用到全部群或所选的群；群成员只读"));
    AddChild(std::move(header), Hint().Fixed(kHeaderH));

    // ---- 企业选择行（仅平台超管）----
    {
        auto row = std::make_unique<DuiHBox>();
        row->AddChild(MakeLabel(_T("企业"), theme::kInk500, DT_LEFT), Hint().Fixed(kPickerLabelW));
        auto picker = std::make_unique<DuiComboBox>();
        picker->SetCtrlId(kIdQaEnterprise);
        picker->SetEditable(true);
        picker->SetIncrementalSearch(true);
        picker->SetIncrementalSearchSubstring(true);
        m_picker = picker.get();
        row->AddChild(std::move(picker), Hint().Fixed(kPickerW));
        row->AddChild(std::make_unique<DuiControl>(), Hint().Weight(1));
        m_pickerRow = row.get();
        AddChild(std::move(row), Hint().Fixed(kBarH).Margin(kSideMargin, 0, kSideMargin, kRowGap));
    }

    // ---- 工具行：搜索框 …… 「刷新」「新增问答」----
    {
        auto row = std::make_unique<DuiHBox>();
        row->SetGap(kButtonGap);
        auto search = std::make_unique<DuiEdit>();
        search->SetCtrlId(kIdQaSearch);
        search->SetPlaceholder(_T("搜索问题或答案"));
        search->SetShowBorder(true);
        search->SetBgColor(theme::kInk0);
        m_search = search.get();
        row->AddChild(std::move(search), Hint().Fixed(kSearchW));
        row->AddChild(std::make_unique<DuiControl>(), Hint().Weight(1));
        row->AddChild(MakeButton(kIdQaRefresh, _T("刷新"), DuiButton::Variant::Default), Hint().Fixed(kButtonW));
        auto add = MakeButton(kIdQaAdd, _T("新增问答"), DuiButton::Variant::Primary);
        m_addButton = add.get();
        row->AddChild(std::move(add), Hint().Fixed(kButtonW));
        AddChild(std::move(row), Hint().Fixed(kBarH).Margin(kSideMargin, 0, kSideMargin, kRowGap));
    }

    // ---- 表格卡：表头 + 表格 ----
    auto card = MakeCard();
    {
        auto head = std::make_unique<ui::ColumnHeader>();
        head->SetColumns(BuildColumns());
        head->SetCellPadding(kCellPad);
        m_header = head.get();
        card->AddChild(std::move(head), Hint().Fixed(kTableHeaderH));

        auto table = std::make_unique<ui::ActionTable>();
        table->SetCtrlId(kIdQaTable);
        table->SetRowHeight(kRowHeight);
        table->SetRowPainter(&EnterpriseQaScreen::PaintRowThunk, this);
        table->SetActionAreaWidth(kColActions);
        table->AddAction(_T("编辑"), theme::kColorInfo);
        table->AddAction(_T("删除"), theme::kColorDanger);
        table->SetActionCallback(&EnterpriseQaScreen::OnRowActionThunk, this);
        m_table = table.get();
        card->AddChild(std::move(table), Hint().Weight(1));
    }
    AddChild(std::move(card), Hint().Weight(1).Margin(kSideMargin, 0, kSideMargin, kSideMargin));
}

void EnterpriseQaScreen::SetRequestSink(RequestSinkFn fn, void* user)
{
    m_sink = fn;
    m_sinkUser = user;
}

void EnterpriseQaScreen::SetUiRequestHandler(UiRequestFn fn, void* user)
{
    m_uiHandler = fn;
    m_uiHandlerUser = user;
}

void EnterpriseQaScreen::OnEnter()
{
    const AdminSession& session = AdminSession::Instance();
    //换了登录身份：清掉上一个身份留下的企业列表与问答
    if (session.Token() != m_sessionToken)
    {
        m_enterpriseIds.clear();
        m_enterpriseId = 0;
        m_items.clear();
        m_groups.clear();
        m_loadState = kLoadIdle;
        if (m_picker != nullptr)
            m_picker->ResetContent();
        m_sessionToken = session.Token();
    }
    m_isPlatform = session.IsPlatformAdmin();
    if (m_pickerRow != nullptr)
    {
        m_pickerRow->SetVisible(m_isPlatform);
        //SetVisible 不会重新排版：对本界面按当前矩形重新排一次（开发约定文档 UI 约定第 5 条）
        ForceLayout(GetRect());
    }
    FA_LOGI(_T("enter enterprise qa screen, platform admin: %d, enterprise: %lld"), m_isPlatform ? 1 : 0, m_enterpriseId);
    if (m_isPlatform && m_enterpriseIds.empty())
        StartLoadEnterprises();
    //每次进入都刷新：别的管理员可能刚改过；平台超管未选企业时 StartLoad 不发请求
    StartLoad();
    UpdateView();
}

long long EnterpriseQaScreen::RequestEnterpriseId() const
{
    return m_isPlatform ? m_enterpriseId : 0;
}

void EnterpriseQaScreen::Send(HttpRequest& request, ApiTag tag)
{
    request.m_tag = static_cast<UINT_PTR>(tag);
    if (m_sink == nullptr)
    {
        FA_LOGW(_T("enterprise qa screen: no request sink, request dropped, tag: %d"), static_cast<int>(tag));
        return;
    }
    m_sink(m_sinkUser, request);
}

void EnterpriseQaScreen::Raise(const UiRequest& request)
{
    if (m_uiHandler == nullptr)
    {
        FA_LOGW(_T("enterprise qa screen: no ui handler, ui request dropped, kind: %d"), request.m_kind);
        return;
    }
    m_uiHandler(m_uiHandlerUser, request);
}

void EnterpriseQaScreen::RaiseToast(const CString& text, bool isError)
{
    UiRequest req;
    req.m_kind = kUiToast;
    req.m_text = text;
    req.m_isError = isError;
    Raise(req);
}

void EnterpriseQaScreen::StartLoadEnterprises()
{
    HttpRequest request = adminapi::MakeEnterpriseListRequest(ServerConfig(), AdminSession::Instance().Token(),
                                                              CString(), kEntStatusAll, 1, kEntPickerPageMax);
    Send(request, kApiTagQaEnterprises);
}

void EnterpriseQaScreen::StartLoad()
{
    if (m_isPlatform && m_enterpriseId <= 0)
        return;
    m_loadState = kLoadLoading;
    const CString token = AdminSession::Instance().Token();
    HttpRequest list = qaapi::MakeQaListRequest(ServerConfig(), token, RequestEnterpriseId());
    Send(list, kApiTagQaList);
    HttpRequest groups = qaapi::MakeQaGroupsRequest(ServerConfig(), token, RequestEnterpriseId());
    Send(groups, kApiTagQaGroups);
}

void EnterpriseQaScreen::OnApiResult(ApiTag tag, const HttpResult& http)
{
    switch (tag)
    {
    //企业选择框的企业列表
    case kApiTagQaEnterprises:
    {
        const EnterpriseListResult r = adminapi::ParseEnterpriseListResponse(http);
        if (!r.Succeeded())
        {
            FA_LOGW(_T("enterprise qa screen: load enterprises failed, code: %d"), r.m_code);
            RaiseToast(_T("企业列表加载失败：") + r.m_message, true);
            break;
        }
        m_enterpriseIds.clear();
        if (m_picker != nullptr)
            m_picker->ResetContent();
        int selected = -1;
        for (size_t i = 0; i < r.m_list.size(); ++i)
        {
            const long long id = _ttoi64(r.m_list[i].m_id);
            m_enterpriseIds.push_back(id);
            if (m_picker != nullptr)
                m_picker->AddString(r.m_list[i].m_name);
            if (id == m_enterpriseId)
                selected = static_cast<int>(i);
        }
        if (m_picker != nullptr && selected >= 0)
            m_picker->SetCurSel(selected, false);
        UpdateView();
        break;
    }

    //问答列表
    case kApiTagQaList:
    {
        const qaapi::QaListResult r = qaapi::ParseQaListResponse(http);
        if (!r.Succeeded())
        {
            m_loadState = kLoadFailed;
            m_loadError = r.m_message;
            FA_LOGW(_T("enterprise qa screen: load list failed, code: %d"), r.m_code);
        }
        else
        {
            m_loadState = kLoadLoaded;
            m_loadError.Empty();
            m_items = r.m_items;
            FA_LOGI(_T("enterprise qa screen: list loaded, count: %u"), static_cast<unsigned>(m_items.size()));
        }
        UpdateView();
        break;
    }

    //本企业的群：应用范围一栏显示群名、弹窗选择群
    case kApiTagQaGroups:
    {
        const qaapi::QaGroupsResult r = qaapi::ParseQaGroupsResponse(http);
        if (!r.Succeeded())
        {
            FA_LOGW(_T("enterprise qa screen: load groups failed, code: %d"), r.m_code);
            break;
        }
        m_groups = r.m_groups;
        UiRequest req;
        req.m_kind = kUiGroupsLoaded;
        Raise(req);
        UpdateView();
        break;
    }

    //新增或修改：成功关弹窗并重拉，失败在弹窗里提示
    case kApiTagQaSave:
    {
        m_saving = false;
        const qaapi::QaWriteResult r = qaapi::ParseQaWriteResponse(http);
        UiRequest req;
        if (!r.Succeeded())
        {
            req.m_kind = kUiModalError;
            req.m_text = r.m_message;
            Raise(req);
            FA_LOGW(_T("enterprise qa screen: save failed, code: %d"), r.m_code);
            break;
        }
        req.m_kind = kUiCloseModal;
        Raise(req);
        RaiseToast(_T("已保存"), false);
        StartLoad();
        break;
    }

    //删除：成功后重拉
    case kApiTagQaDelete:
    {
        const qaapi::QaWriteResult r = qaapi::ParseQaWriteResponse(http);
        if (!r.Succeeded())
        {
            RaiseToast(_T("删除失败：") + r.m_message, true);
            FA_LOGW(_T("enterprise qa screen: delete failed, code: %d"), r.m_code);
            break;
        }
        RaiseToast(_T("已删除"), false);
        StartLoad();
        break;
    }

    //其它标签不归本界面
    default:
        break;
    }
}

void EnterpriseQaScreen::OnEnterpriseSelected(int index)
{
    if (index < 0 || index >= static_cast<int>(m_enterpriseIds.size()))
        return;
    const long long id = m_enterpriseIds[static_cast<size_t>(index)];
    if (id == m_enterpriseId)
        return;
    m_enterpriseId = id;
    m_items.clear();
    m_groups.clear();
    StartLoad();
    UpdateView();
}

void EnterpriseQaScreen::OnSearchChanged()
{
    UpdateView();
}

void EnterpriseQaScreen::OnRefreshClicked()
{
    StartLoad();
    UpdateView();
}

void EnterpriseQaScreen::OnAddClicked()
{
    if (m_isPlatform && m_enterpriseId <= 0)
    {
        RaiseToast(_T("请先选择企业"), true);
        return;
    }
    UiRequest req;
    req.m_kind = kUiOpenAddModal;
    Raise(req);
}

void EnterpriseQaScreen::OnRowAction(int rowIndex, int actionIndex)
{
    if (rowIndex < 0 || rowIndex >= static_cast<int>(m_shown.size()))
        return;
    const qaapi::QaItem& item = m_items[m_shown[static_cast<size_t>(rowIndex)]];
    UiRequest req;
    req.m_item = item;
    switch (actionIndex)
    {
    //编辑：打开编辑弹窗
    case kActionEdit:
        req.m_kind = kUiOpenEditModal;
        Raise(req);
        break;

    //删除：先请宿主弹确认框
    case kActionDelete:
        m_pendingDelete = item;
        req.m_kind = kUiConfirmDelete;
        Raise(req);
        break;

    //其它操作：不处理
    default:
        break;
    }
}

void EnterpriseQaScreen::OnConfirmDelete(bool confirmed)
{
    if (!confirmed || m_pendingDelete.m_id <= 0)
    {
        m_pendingDelete = qaapi::QaItem();
        return;
    }
    HttpRequest request = qaapi::MakeQaDeleteRequest(ServerConfig(), AdminSession::Instance().Token(),
                                                     RequestEnterpriseId(), m_pendingDelete.m_id);
    FA_LOGI(_T("enterprise qa screen: delete, id: %lld"), m_pendingDelete.m_id);
    m_pendingDelete = qaapi::QaItem();
    Send(request, kApiTagQaDelete);
}

void EnterpriseQaScreen::SubmitQa(const qaapi::QaItem& item)
{
    if (m_saving)
        return;
    m_saving = true;
    HttpRequest request = qaapi::MakeQaSaveRequest(ServerConfig(), AdminSession::Instance().Token(),
                                                   RequestEnterpriseId(), item);
    FA_LOGI(_T("enterprise qa screen: save, id: %lld, scope: %d, groups: %u"), item.m_id, item.m_scope,
            static_cast<unsigned>(item.m_groupIds.size()));
    Send(request, kApiTagQaSave);
}

std::vector<size_t> EnterpriseQaScreen::FilterItems(const std::vector<qaapi::QaItem>& items, const CString& keyword)
{
    std::vector<size_t> out;
    CString key = keyword;
    key.Trim();
    key.MakeLower();
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (key.IsEmpty())
        {
            out.push_back(i);
            continue;
        }
        CString q = items[i].m_question;
        CString a = items[i].m_answer;
        q.MakeLower();
        a.MakeLower();
        if (q.Find(key) >= 0 || a.Find(key) >= 0)
            out.push_back(i);
    }
    return out;
}

void EnterpriseQaScreen::UpdateView()
{
    const CString keyword = (m_search != nullptr) ? m_search->GetText() : CString();
    m_shown = FilterItems(m_items, keyword);
    if (m_table == nullptr)
        return;
    CString overlay;
    if (m_isPlatform && m_enterpriseId <= 0)
        overlay = _T("请先在上方选择企业");
    else if (m_loadState == kLoadLoading && m_items.empty())
        overlay = _T("正在加载…");
    else if (m_loadState == kLoadFailed)
        overlay = _T("加载失败：") + m_loadError;
    else if (m_items.empty())
        overlay = _T("还没有企业问答，点右上角「新增问答」添加");
    else if (m_shown.empty())
        overlay = _T("没有找到匹配的问答");
    m_table->SetRowCount(overlay.IsEmpty() ? static_cast<int>(m_shown.size()) : 0);
    m_table->SetOverlayText(overlay);
    m_table->Invalidate();
}

void EnterpriseQaScreen::PaintRowThunk(void* user, HDC hdc, int rowIndex, const RECT& rowRect, bool selected, bool hover)
{
    (void)selected;
    static_cast<EnterpriseQaScreen*>(user)->PaintRow(hdc, rowIndex, rowRect, hover);
}

void EnterpriseQaScreen::OnRowActionThunk(void* user, int rowIndex, int actionIndex)
{
    static_cast<EnterpriseQaScreen*>(user)->OnRowAction(rowIndex, actionIndex);
}

void EnterpriseQaScreen::PaintRow(HDC hdc, int rowIndex, const RECT& rowRect, bool hover)
{
    if (rowIndex < 0 || rowIndex >= static_cast<int>(m_shown.size()) || m_header == nullptr)
        return;
    const qaapi::QaItem& item = m_items[m_shown[static_cast<size_t>(rowIndex)]];
    if (hover)
        ui::FillRoundRect(hdc, rowRect, theme::kInk25, 0);

    std::vector<ui::ColumnBound> b;
    ui::ColumnHeader::ComputeColumnBounds(m_header->Columns(), rowRect.left, rowRect.right, b);
    if (b.size() <= static_cast<size_t>(kQcTime))
        return;

    const UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
    //问题一列：上面问题（加粗），下面答案摘要（灰字）
    {
        const int midY = (rowRect.top + rowRect.bottom) / 2;
        const RECT q = { b[kQcQuestion].m_left + kCellPad, midY - kLineH, b[kQcQuestion].m_right - kCellPad, midY };
        const RECT a = { b[kQcQuestion].m_left + kCellPad, midY, b[kQcQuestion].m_right - kCellPad, midY + kLineH };
        ui::DrawTextLine(hdc, item.m_question, q, theme::kInk700, ui::UiFont(theme::kFontSizeNormal, true), flags);
        ui::DrawTextLine(hdc, AnswerDigest(item.m_answer), a, theme::kInk400, ui::UiFont(theme::kFontSizeSmall, false), flags);
    }
    HFONT font = ui::UiFont(theme::kFontSizeNormal, false);
    const CString cells[] =
    {
        qaapi::QaScopeText(item, m_groups),
        item.m_creatorName.IsEmpty() ? CString(_T("-")) : item.m_creatorName,
        FormatTimeMs(item.m_updateTime),
    };
    for (int i = kQcScope; i <= kQcTime; ++i)
    {
        const RECT rc = { b[i].m_left + kCellPad, rowRect.top, b[i].m_right - kCellPad, rowRect.bottom };
        if (rc.right > rc.left)
            ui::DrawTextLine(hdc, cells[i - kQcScope], rc, theme::kInk500, font, flags);
    }
    //行底分隔线
    ui::FillLine(hdc, rowRect.left, rowRect.bottom - 1, rowRect.right - rowRect.left, 1, theme::kInk50);
}

} // namespace flamingoadmin
