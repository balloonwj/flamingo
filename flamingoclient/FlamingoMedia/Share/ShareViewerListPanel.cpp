/**
 *  屏幕共享的观看者名单浮窗，说明见 ShareViewerListPanel.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "ShareViewerListPanel.h"

#include <memory>
#include "ShareMessages.h"
#include "ShareSources.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenuPlacement.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace screenshare {

namespace {

//整窗宽、标题栏高（逻辑像素）。第 14 期每行多了「移出」，由 320 加宽到 380；第 18 期发起人每行再多「允许控制」，
//加宽到 460（观看者的名单没有这个按钮，同样加宽，多出的宽度给名字）
const int kPanelW = 460;
const int kTitleBarH = 28;
//内容区内边距与行间距
const int kPad = 10;
const int kGap = 6;
//每行高、最多同时显示的行数（再多就滚动）、空名单提示行高
const int kRowH = 32;
const int kMaxVisibleRows = 8;
const int kEmptyRowH = 28;
//「全员静音」、行内「静音」按钮宽，状态文字宽
const int kMuteAllW = 88;
const int kMuteW = 64;
//行内「移出」按钮宽（第 14 期）
const int kRemoveW = 52;
//行内「允许控制 / 收回控制」按钮宽（第 18 期）
const int kControlW = 76;
const int kStatusW = 76;
//名单弹在按钮下方的间距
const int kBelowGap = 4;
//状态文字颜色：正在说话（品牌蓝）、麦克风开着（深灰）、麦克风已关（浅灰）
const COLORREF kSpeakingColor = RGB(45, 108, 223);
const COLORREF kMicOnColor = RGB(70, 76, 86);
const COLORREF kMicOffColor = RGB(150, 156, 166);
//共享描述的文字颜色（第 15 期）：与「麦克风开着」同一深灰
const COLORREF kDescColor = RGB(70, 76, 86);

//两份名单是否显示得一样（只比较界面上看得出来的字段）
bool sameRows(const std::vector<ShareViewerRow>& a, const std::vector<ShareViewerRow>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].m_userId != b[i].m_userId || a[i].m_seat != b[i].m_seat || a[i].m_name != b[i].m_name ||
            a[i].m_micOn != b[i].m_micOn || a[i].m_speaking != b[i].m_speaking || a[i].m_canMute != b[i].m_canMute ||
            a[i].m_canRemove != b[i].m_canRemove || a[i].m_canControl != b[i].m_canControl ||
            a[i].m_controlling != b[i].m_controlling)
            return false;
    }
    return true;
}

} // namespace

CShareViewerListPanel::CShareViewerListPanel()
    : m_notifyWnd(NULL)
    , m_asHost(false)
    , m_descH(0)
{
    memset(&m_anchor, 0, sizeof(m_anchor));
}

CShareViewerListPanel::~CShareViewerListPanel()
{
    Hide();
}

int CShareViewerListPanel::panelHeight() const
{
    int h = kTitleBarH + 2 * kPad;
    if (m_descH > 0)
        h += m_descH + kGap;
    if (m_asHost)
        h += kRowH + kGap;
    if (m_rows.empty())
    {
        h += kEmptyRowH;
    }
    else
    {
        const int visible = static_cast<int>(m_rows.size()) < kMaxVisibleRows ? static_cast<int>(m_rows.size())
                                                                               : kMaxVisibleRows;
        h += visible * kRowH;
    }
    return h;
}

void CShareViewerListPanel::place()
{
    if (!IsWindow())
        return;
    POINT at = { m_anchor.left, m_anchor.bottom + kBelowGap };
    SIZE size = { kPanelW, panelHeight() };
    //落点限制在锚点所在显示器的工作区内（开发约定文档「UI 约定」第 3 条）
    const POINT origin = balloonwjui::ClampMenuOriginToWorkArea(at, size);
    ::SetWindowPos(m_hWnd, HWND_TOPMOST, origin.x, origin.y, size.cx, size.cy, SWP_NOACTIVATE);
}

void CShareViewerListPanel::Show(const RECT& anchor, HWND notifyWnd, bool asHost, bool excludeFromCapture)
{
    m_anchor = anchor;
    m_notifyWnd = notifyWnd;
    if (IsWindow() && asHost == m_asHost)
    {
        place();
        return;
    }
    Hide();
    m_asHost = asHost;
    //不抢焦点：发起人正在被共享的程序里操作，观看者在全屏观看
    const DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE;
    if (Create(NULL, CWindow::rcDefault, TR(L"观看者"), WS_POPUP, exStyle) == NULL)
    {
        LOG_ERROR("screenshare: create viewer list panel failed.");
        return;
    }
    //标题栏左侧的程序图标（2026-10-07，开发约定文档 UI 约定第 10 条）：标题栏不透明，底色与它一致
    SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
    SetTitleBarHeight(kTitleBarH);
    SetButtons(false, false, true);
    SetResizable(false);
    if (excludeFromCapture)
        ExcludeWindowFromCapture(m_hWnd);
    buildUi();
    place();
    ShowWindow(SW_SHOWNOACTIVATE);
    LOG_INFO("screenshare: viewer list shown, host=%d, rows=%u.", asHost ? 1 : 0, static_cast<unsigned>(m_rows.size()));
}

void CShareViewerListPanel::Hide()
{
    if (IsWindow())
        DestroyWindow();
}

bool CShareViewerListPanel::IsShown() const
{
    return IsWindow() != FALSE;
}

void CShareViewerListPanel::SetRows(const std::vector<ShareViewerRow>& rows)
{
    if (sameRows(rows, m_rows))
        return;
    const bool heightChanged = (rows.size() != m_rows.size());
    m_rows = rows;
    if (!IsWindow())
        return;
    buildUi();
    if (heightChanged)
        place();
}

void CShareViewerListPanel::SetDescription(const std::wstring& desc)
{
    if (desc == m_desc)
        return;
    m_desc = desc;
    if (!IsWindow())
        return;
    buildUi();
    place();
}

void CShareViewerListPanel::buildUi()
{
    CString title;
    title.Format(TR(L"观看者（%u）"), static_cast<unsigned>(m_rows.size()));
    SetTitle(title);

    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetPadding(kPad);
    root->SetGap(kGap);

    //共享描述（第 15 期）：灰字自动换行，按名单内宽量出实际行高，描述最多 100 字，不再另设行数上限
    m_descH = 0;
    if (!m_desc.empty())
    {
        std::unique_ptr<balloonwjui::DuiLabel> desc(new balloonwjui::DuiLabel());
        desc->SetText(m_desc.c_str());
        desc->SetTextColor(kDescColor);
        desc->SetWordWrap(true);
        desc->SetTextAlign(DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
        m_descH = desc->MeasureHeight(kPanelW - 2 * kPad);
        root->AddChild(std::move(desc), balloonwjui::DuiLayout::Hint().Fixed(m_descH));
    }

    //发起人：顶部「全员静音」，有人开着麦克风时才能点
    if (m_asHost)
    {
        bool anyMic = false;
        for (size_t i = 0; i < m_rows.size(); ++i)
            anyMic = anyMic || m_rows[i].m_micOn;
        std::unique_ptr<balloonwjui::DuiHBox> header(new balloonwjui::DuiHBox());
        header->AddChild(std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl()),
                         balloonwjui::DuiLayout::Hint().Weight(1));
        std::unique_ptr<balloonwjui::DuiButton> muteAll(new balloonwjui::DuiButton());
        muteAll->SetButtonType(balloonwjui::DuiButton::StylePushButton);
        muteAll->SetVariant(balloonwjui::DuiButton::Variant::Default);
        muteAll->SetText(TR(L"全员静音"));
        muteAll->SetCtrlId(kIdMuteAll);
        muteAll->SetEnabled(anyMic);
        header->AddChild(std::move(muteAll), balloonwjui::DuiLayout::Hint().Fixed(kMuteAllW));
        root->AddChild(std::move(header), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
    }

    if (m_rows.empty())
    {
        std::unique_ptr<balloonwjui::DuiLabel> empty(new balloonwjui::DuiLabel());
        empty->SetText(TR(L"还没有人加入"));
        empty->SetTextColor(kMicOffColor);
        empty->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        root->AddChild(std::move(empty), balloonwjui::DuiLayout::Hint().Fixed(kEmptyRowH));
    }
    else
    {
        std::unique_ptr<balloonwjui::DuiVBox> list(new balloonwjui::DuiVBox());
        for (size_t i = 0; i < m_rows.size(); ++i)
        {
            const ShareViewerRow& r = m_rows[i];
            std::unique_ptr<balloonwjui::DuiHBox> line(new balloonwjui::DuiHBox());
            line->SetGap(kGap);
            std::unique_ptr<balloonwjui::DuiLabel> name(new balloonwjui::DuiLabel());
            name->SetText(r.m_name.c_str());
            name->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            line->AddChild(std::move(name), balloonwjui::DuiLayout::Hint().Weight(1));
            std::unique_ptr<balloonwjui::DuiLabel> status(new balloonwjui::DuiLabel());
            status->SetText(ShareViewerStatusText(r).c_str());
            status->SetTextColor(r.m_micOn && r.m_speaking ? kSpeakingColor : (r.m_micOn ? kMicOnColor : kMicOffColor));
            status->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            line->AddChild(std::move(status), balloonwjui::DuiLayout::Hint().Fixed(kStatusW));
            if (m_asHost)
            {
                std::unique_ptr<balloonwjui::DuiButton> mute(new balloonwjui::DuiButton());
                mute->SetButtonType(balloonwjui::DuiButton::StylePushButton);
                mute->SetVariant(balloonwjui::DuiButton::Variant::Default);
                mute->SetText(r.m_micOn ? TR(L"静音") : TR(L"已静音"));
                mute->SetCtrlId(static_cast<UINT>(kIdRowMuteBase + static_cast<int>(i)));
                mute->SetEnabled(r.m_canMute);
                line->AddChild(std::move(mute), balloonwjui::DuiLayout::Hint().Fixed(kMuteW));
                //移出（第 14 期）：点了先由控制器弹确认框
                std::unique_ptr<balloonwjui::DuiButton> remove(new balloonwjui::DuiButton());
                remove->SetButtonType(balloonwjui::DuiButton::StylePushButton);
                remove->SetVariant(balloonwjui::DuiButton::Variant::Default);
                remove->SetText(TR(L"移出"));
                remove->SetCtrlId(static_cast<UINT>(kIdRowRemoveBase + static_cast<int>(i)));
                remove->SetEnabled(r.m_canRemove);
                line->AddChild(std::move(remove), balloonwjui::DuiLayout::Hint().Fixed(kRemoveW));
                //允许控制 / 收回控制（第 18 期）：正在控制的那一行为「收回控制」
                std::unique_ptr<balloonwjui::DuiButton> control(new balloonwjui::DuiButton());
                control->SetButtonType(balloonwjui::DuiButton::StylePushButton);
                control->SetVariant(balloonwjui::DuiButton::Variant::Default);
                control->SetText(r.m_controlling ? TR(L"收回控制") : TR(L"允许控制"));
                control->SetCtrlId(static_cast<UINT>(kIdRowControlBase + static_cast<int>(i)));
                control->SetEnabled(r.m_canControl);
                line->AddChild(std::move(control), balloonwjui::DuiLayout::Hint().Fixed(kControlW));
            }
            list->AddChild(std::move(line), balloonwjui::DuiLayout::Hint().Fixed(kRowH));
        }
        std::unique_ptr<balloonwjui::DuiScrollView> view(new balloonwjui::DuiScrollView());
        // 滚动条用 balloonui 统一的悬浮式细滑块与默认命中带宽（2026-10-04 起不再单独设宽）
        view->SetContentHeight(static_cast<int>(m_rows.size()) * kRowH);
        view->SetContent(std::move(list));
        root->AddChild(std::move(view), balloonwjui::DuiLayout::Hint().Weight(1));
    }
    SetClientContent(std::move(root));
}

LRESULT CShareViewerListPanel::OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (n == NULL || n->code != static_cast<UINT>(balloonwjui::DUIN_CLICK) || m_notifyWnd == NULL ||
        !::IsWindow(m_notifyWnd))
    {
        bHandled = FALSE;
        return 0;
    }
    //全员静音
    if (n->ctrlId == static_cast<UINT>(kIdMuteAll))
    {
        ::PostMessage(m_notifyWnd, kMsgShareViewerListCommand, kCmdMuteAll, 0);
        return 0;
    }
    //某一行的「允许控制 / 收回控制」（第 18 期）：按行号找到座位号。最先判它，「移出」「静音」的行号区间都在它下面
    const int controlIndex = static_cast<int>(n->ctrlId) - kIdRowControlBase;
    if (controlIndex >= 0 && controlIndex < static_cast<int>(m_rows.size()))
    {
        ::PostMessage(m_notifyWnd, kMsgShareViewerListCommand, kCmdControl,
                      static_cast<LPARAM>(m_rows[controlIndex].m_seat));
        return 0;
    }
    //某一行的「移出」（第 14 期）：按行号找到座位号。先判它，「静音」的行号区间在它下面
    const int removeIndex = static_cast<int>(n->ctrlId) - kIdRowRemoveBase;
    if (removeIndex >= 0 && removeIndex < static_cast<int>(m_rows.size()))
    {
        ::PostMessage(m_notifyWnd, kMsgShareViewerListCommand, kCmdRemove,
                      static_cast<LPARAM>(m_rows[removeIndex].m_seat));
        return 0;
    }
    //某一行的「静音」：按行号找到座位号
    const int index = static_cast<int>(n->ctrlId) - kIdRowMuteBase;
    if (index >= 0 && index < static_cast<int>(m_rows.size()))
    {
        ::PostMessage(m_notifyWnd, kMsgShareViewerListCommand, kCmdMute, static_cast<LPARAM>(m_rows[index].m_seat));
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CShareViewerListPanel::OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    Hide();
    return 0;
}

} // namespace screenshare
