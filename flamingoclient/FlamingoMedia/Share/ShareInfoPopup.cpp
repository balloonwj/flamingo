/**
 *  屏幕共享的「共享说明」浮窗，说明见 ShareInfoPopup.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "ShareInfoPopup.h"

#include <memory>
#include "../../Source/Common/Log/IULog.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenuPlacement.h"
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace screenshare {

namespace {

//整窗宽、标题栏高（逻辑像素）
const int kPopupW = 320;
const int kTitleBarH = 28;
//内容区内边距与标题、描述之间的间距
const int kPad = 12;
const int kGap = 6;
//标题行高、标题字号（磅，粗体）
const int kTitleLineH = 22;
const int kTitlePointSize = 10;
//描述文字颜色（深灰）
const COLORREF kDescColor = RGB(70, 76, 86);
//浮窗弹在锚点下方的间距
const int kBelowGap = 4;

} // namespace

CShareInfoPopup::CShareInfoPopup()
    : m_descH(0)
{
    memset(&m_anchor, 0, sizeof(m_anchor));
}

CShareInfoPopup::~CShareInfoPopup()
{
    Hide();
}

void CShareInfoPopup::place()
{
    if (!IsWindow())
        return;
    POINT at = { m_anchor.left, m_anchor.bottom + kBelowGap };
    //描述为空时（不应出现：没有描述时不放入口）只留标题一行；描述块的高度由 buildUi 按实际折行量出
    const int descH = m_descH > 0 ? kGap + m_descH : 0;
    SIZE size = { kPopupW, kTitleBarH + kPad * 2 + kTitleLineH + descH };
    //落点限制在锚点所在显示器的工作区内（开发约定文档「UI 约定」第 3 条）
    const POINT origin = balloonwjui::ClampMenuOriginToWorkArea(at, size);
    ::SetWindowPos(m_hWnd, HWND_TOPMOST, origin.x, origin.y, size.cx, size.cy, SWP_NOACTIVATE);
}

void CShareInfoPopup::buildUi()
{
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetPadding(kPad);
    root->SetGap(kGap);
    std::unique_ptr<balloonwjui::DuiLabel> title(new balloonwjui::DuiLabel());
    title->SetText(m_title.c_str());
    title->SetTextPointSize(kTitlePointSize, true);
    title->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    root->AddChild(std::move(title), balloonwjui::DuiLayout::Hint().Fixed(kTitleLineH));
    m_descH = 0;
    if (!m_desc.empty())
    {
        std::unique_ptr<balloonwjui::DuiLabel> desc(new balloonwjui::DuiLabel());
        desc->SetText(m_desc.c_str());
        desc->SetTextColor(kDescColor);
        desc->SetWordWrap(true);
        desc->SetTextAlign(DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
        //描述最多 100 字，按浮窗内宽量出实际行高，全文显示、不设行数上限
        m_descH = desc->MeasureHeight(kPopupW - 2 * kPad);
        root->AddChild(std::move(desc), balloonwjui::DuiLayout::Hint().Fixed(m_descH));
    }
    SetClientContent(std::move(root));
}

void CShareInfoPopup::Show(const RECT& anchor, const std::wstring& title, const std::wstring& desc)
{
    m_anchor = anchor;
    m_title = title;
    m_desc = desc;
    if (!IsWindow())
    {
        //置顶、不抢焦点、不进任务栏；不设所有者（原为让关闭按钮的通知发回本窗口；2026-10-14 起有无所有者，
        //控件通知都发回本窗口，见开发约定文档 UI 约定第 11 条）
        const DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE;
        if (Create(NULL, CWindow::rcDefault, TR(L"共享说明"), WS_POPUP, exStyle) == NULL)
        {
            LOG_ERROR("screenshare: create info popup failed.");
            return;
        }
        SetTitle(TR(L"共享说明"));
        //标题栏左侧的程序图标（2026-10-07，开发约定文档 UI 约定第 10 条）：标题栏不透明，底色与它一致
        SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
        SetTitleBarHeight(kTitleBarH);
        SetButtons(false, false, true);
        SetResizable(false);
    }
    buildUi();
    place();
    ShowWindow(SW_SHOWNOACTIVATE);
    LOG_INFO("screenshare: info popup shown, desc chars %u.", static_cast<unsigned>(m_desc.size()));
}

void CShareInfoPopup::SetContent(const std::wstring& title, const std::wstring& desc)
{
    if (!IsWindow())
        return;
    if (title == m_title && desc == m_desc)
        return;
    m_title = title;
    m_desc = desc;
    buildUi();
    place();
}

void CShareInfoPopup::Hide()
{
    if (IsWindow())
        DestroyWindow();
}

bool CShareInfoPopup::IsShown() const
{
    return IsWindow() != FALSE;
}

LRESULT CShareInfoPopup::OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    Hide();
    return 0;
}

} // namespace screenshare
