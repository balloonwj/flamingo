/**
 *  文件共享的右下角通知弹窗，说明见 ShareNoticePopup.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"

#include "ShareNoticePopup.h"

#include "../../Source/Common/Log/IULog.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/DuiDpi.h"   //DuiDpi：取显示器 DPI、把逻辑像素换算成物理像素
#include "../../Source/Common/I18n/I18n.h"   // TR / TR_NOOP：界面文字的多语言查找

using balloonwjui::DuiControl;
using balloonwjui::DuiLabel;
using balloonwjui::DuiLayout;
using balloonwjui::DuiVBox;
using balloonwjui::DuiHBox;

namespace fileshare {

namespace {

//以下尺寸都是 100% 缩放（96 DPI）下的逻辑像素，使用时经 Px 按弹窗所在显示器的 DPI 换算（2026-10-07 起：字体随 DPI
//放大，窗口尺寸不换算时 125% 缩放下说明一栏放不下保存位置）。
//整窗宽度与标题栏高。宽度按访问者一条的第二行定：「已保存到」加常见的下载目录（如 C:\Users\<名字>\Downloads）与
//右端「打开」「打开所在文件夹」两个链接能排在一行里完整显示；更长的路径省略中间。2026-10-07 由 380 加宽到 480
const int kPopupW = 480;
const int kTitleBarH = 28;
//内容区四周的内边距、各条之间的间距
const int kPadding = 12;
const int kRowGap = 8;
//一条的高度：两行各 kLineH，行间 kLineGap（换算后的一条高度见 RowHeightAt）
const int kLineH = 20;
const int kLineGap = 2;
//底部一行（「还有 N 条」「查看全部」）的高度
const int kFooterH = 20;
//第一行右端时间的宽度（「23:59」加留白）
const int kTimeW = 44;
//第二行说明与链接、链接与链接之间的间距
const int kLinkGap = 10;
//链接宽度在量出的文字宽度上再加的留白，免得字体取整误差把最后一个字挤掉
const int kLinkSlack = 4;
//标题的磅值（加粗）
const int kTitlePointSize = 9;

//链接与「查看全部」的控件 id（避开 DuiFrameWindow 标题栏保留的 1 / 2 / 3）：第 row 条第 link 个链接为
//kIdLinkBase + row * kLinkIdStride + link
const UINT kIdViewAll = 20;
const UINT kIdLinkBase = 100;
const UINT kLinkIdStride = 10;

//颜色：标题深灰、说明与时间浅灰、失败红字、链接品牌蓝（与「文件共享」窗口一致）
const COLORREF kTitleColor = RGB(0x1F, 0x1F, 0x1F);
const COLORREF kWeakColor = RGB(0x8A, 0x8F, 0x99);
const COLORREF kFailColor = RGB(0xD9, 0x3B, 0x3B);
const COLORREF kLinkColor = RGB(45, 108, 223);
const COLORREF kLinkHoverColor = RGB(30, 86, 190);

//两种弹窗的标题
const wchar_t* const kOwnerTitle = TR_NOOP(L"文件共享动态");
const wchar_t* const kVisitorTitle = TR_NOOP(L"文件共享传输");
const wchar_t* const kViewAllText = TR_NOOP(L"查看全部");

//量一段文字在给定字体下的宽度（像素）
int MeasureTextWidth(HFONT font, const std::wstring& text)
{
    HDC hdc = ::GetDC(NULL);
    HGDIOBJ oldFont = (font != NULL) ? ::SelectObject(hdc, font) : NULL;
    SIZE size = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &size);
    if (oldFont != NULL)
        ::SelectObject(hdc, oldFont);
    ::ReleaseDC(NULL, hdc);
    return size.cx;
}

//把 100% 缩放下的逻辑像素按 dpi 换算成物理像素（dpi 不大于 0 时按 96，不缩放）
int Px(int logical, int dpi)
{
    return balloonwjui::DuiDpi::Scale(logical, dpi);
}

//换算后一条的高度：两行加行间距，各自取整后相加
int RowHeightAt(int dpi)
{
    return Px(kLineH, dpi) * 2 + Px(kLineGap, dpi);
}

} // namespace

CShareNoticePopup::CShareNoticePopup()
    : m_kind(share_notice_popup_visitor)
    , m_listener(NULL)
    , m_viewAll(false)
    , m_dpi(balloonwjui::DuiDpi::kDefaultDpi)
{
}

CShareNoticePopup::~CShareNoticePopup()
{
    Hide();
}

void CShareNoticePopup::SetKind(int kind, IShareNoticePopupListener* listener)
{
    m_kind = kind;
    m_listener = listener;
}

int CShareNoticePopup::PopupWidth(int dpi)
{
    return Px(kPopupW, dpi);
}

int CShareNoticePopup::HeightFor(size_t rows, bool footer, int dpi)
{
    int height = Px(kTitleBarH, dpi) + 2 * Px(kPadding, dpi) + static_cast<int>(rows) * RowHeightAt(dpi);
    if (rows > 1)
        height += static_cast<int>(rows - 1) * Px(kRowGap, dpi);
    if (footer)
        height += Px(kRowGap, dpi) + Px(kFooterH, dpi);
    return height;
}

int CShareNoticePopup::GetDpi() const
{
    return m_dpi;
}

UINT CShareNoticePopup::ViewAllCtrlId()
{
    return kIdViewAll;
}

bool CShareNoticePopup::EnsureCreated()
{
    if (IsWindow())
        return true;
    //没有所有者的置顶浮层：不进任务栏、显示时不抢焦点（与录制完成提示相同）
    const DWORD exStyle = WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    const wchar_t* title = TR(m_kind == share_notice_popup_owner ? kOwnerTitle : kVisitorTitle);
    if (Create(NULL, CWindow::rcDefault, title, WS_POPUP, exStyle) == NULL)
    {
        LOG_ERROR("share notice popup: create failed, kind: %d, error: %lu.", m_kind, ::GetLastError());
        return false;
    }
    SetTitleBarHeight(Px(kTitleBarH, m_dpi));
    //只留关闭按钮：通知不自动关闭，看完由用户关掉
    SetButtons(false, false, true);
    SetResizable(false);
    SetIcon(m_icons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
    SetTitle(title);
    return true;
}

bool CShareNoticePopup::ShowEntries(const std::vector<ShareNoticeEntry>& entries, size_t overflow)
{
    if (entries.empty())
    {
        Hide();
        return false;
    }
    //按鼠标所在显示器的 DPI 换算尺寸：调用方随后用 MoveWindowToWorkAreaCorner 把弹窗摆到这块显示器的右下角。
    //每次显示都重新取，换了显示器（或改了缩放比例）之后尺寸随之更新
    POINT cursor = { 0, 0 };
    ::GetCursorPos(&cursor);
    m_dpi = balloonwjui::DuiDpi::GetDpiForPoint(cursor);
    if (m_dpi <= 0)
        m_dpi = balloonwjui::DuiDpi::kDefaultDpi;
    if (!EnsureCreated())
        return false;
    SetTitleBarHeight(Px(kTitleBarH, m_dpi));
    BuildContent(entries, overflow);
    //DuiFrameWindow 的 ResizeClient 设的是整窗尺寸，标题栏也在其内（UI 约定第 7 条）
    const bool footer = !m_footer.empty() || m_viewAll;
    const int width = PopupWidth(m_dpi);
    const int height = HeightFor(entries.size(), footer, m_dpi);
    ResizeClient(width, height);
    LOG_INFO("share notice popup sized, kind: %d, rows: %u, dpi: %d, size: %dx%d.", m_kind,
             static_cast<unsigned>(entries.size()), m_dpi, width, height);
    return true;
}

void CShareNoticePopup::ShowNoActivate()
{
    if (IsWindow())
        ShowWindow(SW_SHOWNA);
}

void CShareNoticePopup::Hide()
{
    m_linkTargets.clear();
    m_rows.clear();
    m_footer.clear();
    m_viewAll = false;
    if (IsWindow())
        DestroyWindow();
}

bool CShareNoticePopup::IsShowing() const
{
    return IsWindow() && ::IsWindowVisible(m_hWnd) != FALSE;
}

std::unique_ptr<DuiLabel> CShareNoticePopup::MakeLink(const std::wstring& text, UINT ctrlId, int& width) const
{
    std::unique_ptr<DuiLabel> link(new DuiLabel());
    link->SetMode(DuiLabel::ModeLink);
    link->SetAutoNavigate(false);
    link->SetText(text.c_str());
    link->SetLinkColor(kLinkColor);
    link->SetHoverColor(kLinkHoverColor);
    //点过的链接不变紫：同一条可以反复点（如「打开所在文件夹」）
    link->SetVisitedColor(kLinkColor);
    link->SetTextAlign(DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    link->SetCtrlId(ctrlId);
    width = MeasureTextWidth(link->GetDefaultFont(), text) + Px(kLinkSlack, m_dpi);
    return link;
}

void CShareNoticePopup::BuildContent(const std::vector<ShareNoticeEntry>& entries, size_t overflow)
{
    m_linkTargets.clear();
    m_rows.clear();
    m_footer = (overflow > 0) ? ShareNoticeModel::OverflowText(overflow) : std::wstring();
    m_viewAll = (m_kind == share_notice_popup_owner);

    //本次显示所用的各尺寸（物理像素）
    const int lineH = Px(kLineH, m_dpi);
    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetPadding(Px(kPadding, m_dpi));
    root->SetGap(Px(kRowGap, m_dpi));
    for (size_t r = 0; r < entries.size(); ++r)
    {
        const ShareNoticeEntry& e = entries[r];
        RowView view;
        view.m_title = e.m_title;
        view.m_detail = e.m_detail;

        std::unique_ptr<DuiVBox> row(new DuiVBox());
        row->SetGap(Px(kLineGap, m_dpi));
        //第一行：标题（加粗，太长时省略末尾）与右端的时间
        std::unique_ptr<DuiHBox> first(new DuiHBox());
        std::unique_ptr<DuiLabel> title(new DuiLabel());
        title->SetText(e.m_title.c_str());
        title->SetTextPointSize(kTitlePointSize, true);
        title->SetTextColor(kTitleColor);
        title->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        first->AddChild(std::move(title), DuiLayout::Hint().Weight(1));
        std::unique_ptr<DuiLabel> time(new DuiLabel());
        time->SetText(e.m_time.c_str());
        time->SetTextColor(kWeakColor);
        time->SetTextAlign(DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        first->AddChild(std::move(time), DuiLayout::Hint().Fixed(Px(kTimeW, m_dpi)));
        row->AddChild(std::move(first), DuiLayout::Hint().Fixed(lineH));

        //第二行：说明（失败为红字）与右端的操作链接
        std::unique_ptr<DuiHBox> second(new DuiHBox());
        second->SetGap(Px(kLinkGap, m_dpi));
        std::unique_ptr<DuiLabel> detail(new DuiLabel());
        detail->SetText(e.m_detail.c_str());
        detail->SetTextColor(e.m_failed ? kFailColor : kWeakColor);
        //本机路径（「已保存到 …」）放不下时省略路径中间，保留盘符与最后的文件夹名；其余说明省略末尾
        const DWORD ellipsis = e.m_detailIsPath ? DT_PATH_ELLIPSIS : DT_END_ELLIPSIS;
        view.m_detailAlign = DT_LEFT | DT_VCENTER | DT_SINGLELINE | ellipsis | DT_NOPREFIX;
        detail->SetTextAlign(view.m_detailAlign);
        second->AddChild(std::move(detail), DuiLayout::Hint().Weight(1));
        for (size_t k = 0; k < e.m_links.size(); ++k)
        {
            const UINT ctrlId = kIdLinkBase + static_cast<UINT>(r) * kLinkIdStride + static_cast<UINT>(k);
            int width = 0;
            std::unique_ptr<DuiLabel> link = MakeLink(e.m_links[k].m_text, ctrlId, width);
            second->AddChild(std::move(link), DuiLayout::Hint().Fixed(width));
            m_linkTargets.push_back(std::make_pair(ctrlId, std::make_pair(e.m_id, e.m_links[k].m_action)));
            view.m_links.push_back(e.m_links[k].m_text);
            view.m_ids.push_back(ctrlId);
        }
        row->AddChild(std::move(second), DuiLayout::Hint().Fixed(lineH));
        root->AddChild(std::move(row), DuiLayout::Hint().Fixed(RowHeightAt(m_dpi)));
        m_rows.push_back(view);
    }

    //底部一行：「还有 N 条」与（分享者的弹窗）「查看全部」
    if (!m_footer.empty() || m_viewAll)
    {
        std::unique_ptr<DuiHBox> footer(new DuiHBox());
        std::unique_ptr<DuiLabel> more(new DuiLabel());
        more->SetText(m_footer.c_str());
        more->SetTextColor(kWeakColor);
        more->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        footer->AddChild(std::move(more), DuiLayout::Hint().Weight(1));
        if (m_viewAll)
        {
            int width = 0;
            std::unique_ptr<DuiLabel> viewAll = MakeLink(TR(kViewAllText), kIdViewAll, width);
            footer->AddChild(std::move(viewAll), DuiLayout::Hint().Fixed(width));
        }
        root->AddChild(std::move(footer), DuiLayout::Hint().Fixed(Px(kFooterH, m_dpi)));
    }
    SetClientContent(std::move(root));
}

LRESULT CShareNoticePopup::OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    //「查看全部」：code 与 ctrlId 写在同一个条件里（UI 约定第 4 条）
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK) && n->ctrlId == kIdViewAll)
    {
        PostMessage(kMsgNoticeLink, 0, 0);
        return 0;
    }
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DUIN_CLICK))
    {
        for (size_t i = 0; i < m_linkTargets.size(); ++i)
        {
            if (m_linkTargets[i].first != n->ctrlId)
                continue;
            //等这条点击通知处理完再执行：动作可能重建本窗口的内容
            PostMessage(kMsgNoticeLink, static_cast<WPARAM>(m_linkTargets[i].second.first),
                        static_cast<LPARAM>(m_linkTargets[i].second.second));
            return 0;
        }
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CShareNoticePopup::OnNoticeLinkMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)bHandled;
    if (m_listener == NULL)
        return 0;
    const uint32_t entryId = static_cast<uint32_t>(wParam);
    const int action = static_cast<int>(lParam);
    LOG_INFO("share notice popup link clicked, kind: %d, entry: %u, action: %d.", m_kind, static_cast<unsigned>(entryId),
             action);
    if (entryId == 0 && action == 0)
        m_listener->OnNoticeViewAll(m_kind);
    else
        m_listener->OnNoticeLink(m_kind, entryId, action);
    return 0;
}

LRESULT CShareNoticePopup::OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    LOG_INFO("share notice popup closed by the user, kind: %d.", m_kind);
    Hide();
    if (m_listener != NULL)
        m_listener->OnNoticePopupClosed(m_kind);
    return 0;
}

size_t CShareNoticePopup::GetRowCount() const
{
    return m_rows.size();
}

std::wstring CShareNoticePopup::GetRowTitle(size_t row) const
{
    return (row < m_rows.size()) ? m_rows[row].m_title : std::wstring();
}

std::wstring CShareNoticePopup::GetRowDetail(size_t row) const
{
    return (row < m_rows.size()) ? m_rows[row].m_detail : std::wstring();
}

DWORD CShareNoticePopup::GetRowDetailAlign(size_t row) const
{
    return (row < m_rows.size()) ? m_rows[row].m_detailAlign : 0;
}

std::vector<std::wstring> CShareNoticePopup::GetRowLinkTexts(size_t row) const
{
    return (row < m_rows.size()) ? m_rows[row].m_links : std::vector<std::wstring>();
}

std::wstring CShareNoticePopup::GetFooterText() const
{
    return m_footer;
}

bool CShareNoticePopup::HasViewAllLink() const
{
    return m_viewAll;
}

UINT CShareNoticePopup::GetLinkCtrlId(size_t row, size_t link) const
{
    if (row >= m_rows.size() || link >= m_rows[row].m_ids.size())
        return 0;
    return m_rows[row].m_ids[link];
}

} // namespace fileshare
