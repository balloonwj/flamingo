/**
 *  flamingoAdmin —— 文件共享页（「文件共享」「共享审计」两个页签的外层页面）实现，说明见 FileShareCenterScreen.h。
 *
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"
#include "Screens/FileShareCenterScreen.h"
#include "Screens/FileSharesScreen.h"
#include "Screens/FileShareAuditScreen.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/CommonWidgets.h"
#include "Util/AdminApi.h"
#include "Util/Logger.h"

#include "Controls/Basic/DuiButton.h"

#include <memory>

using namespace balloonwjui;

namespace flamingoadmin {

using Hint = balloonwjui::DuiLayout::Hint;

namespace {

// ---- 布局常量（96 dpi 逻辑像素），与敏感词页的页头一致 ----
const int kHeaderH    = 100;  // 页头高度
const int kTabsW      = 200;  // 页签切换控件的宽度（两个页签，与敏感词页的页签控件同宽）
const int kRefreshW   = 72;   // 「刷新」按钮宽度

// 页签文字，下标与 FileShareCenterScreen::Tab 一致
const TCHAR* const kTabLabels[FileShareCenterScreen::kTabCount] =
{
    _T("文件共享"),
    _T("共享审计"),
};

// 页头副标题，下标与 FileShareCenterScreen::Tab 一致（沿用两个页面原先各自页头的说明）
const TCHAR* const kSubtitles[FileShareCenterScreen::kTabCount] =
{
    _T("查看企业成员发起的文件共享与成员权限，必要时在后台强制取消"),
    _T("按人、按动作、按时间查询文件共享的操作记录"),
};

// 页头的小标题与主标题
const TCHAR kEyebrow[] = _T("文件共享");
const TCHAR kTitle[]   = _T("文件共享");

} // namespace

FileShareCenterScreen::FileShareCenterScreen()
    : m_header(nullptr)
    , m_tabs(nullptr)
    , m_shares(nullptr)
    , m_audit(nullptr)
    , m_activeTab(kTabShares)
{
    // ---- 页头：右侧依次为页签切换「文件共享 | 共享审计」与「刷新」（刷新当前页签）----
    // 页签放在页头右侧而不另占一行：两个页签的详情卡较高，最小窗口（1100×720）下再多一行 48 像素的页签行，
    // 表格就显示不下三行（FS-6 / FAU-4）
    std::unique_ptr<ui::PageHeader> header(new ui::PageHeader());
    m_subtitle = kSubtitles[kTabShares];
    header->SetTexts(kEyebrow, kTitle, m_subtitle);
    {
        std::unique_ptr<ui::SegControl> tabs(new ui::SegControl());
        tabs->SetCtrlId(kIdFsTabs);
        for (int i = 0; i < kTabCount; ++i)
        {
            tabs->AddSegment(kTabLabels[i]);
        }
        m_tabs = tabs.get();
        header->AddAction(std::move(tabs), kTabsW);

        std::unique_ptr<DuiButton> refresh(new DuiButton());
        refresh->SetCtrlId(kIdFsRefresh);
        refresh->SetText(_T("刷新"));
        refresh->SetVariant(DuiButton::Variant::Default);
        refresh->SetTextPointSize(theme::kFontSizeNormal);
        header->AddAction(std::move(refresh), kRefreshW);
    }
    m_header = header.get();
    AddChild(std::move(header), Hint().Fixed(kHeaderH));

    // ---- 两个页签的内容：各占满剩余高度，只显示当前页签的那一个 ----
    std::unique_ptr<FileSharesScreen> shares(new FileSharesScreen());
    m_shares = shares.get();
    AddChild(std::move(shares), Hint().Weight(1));

    std::unique_ptr<FileShareAuditScreen> audit(new FileShareAuditScreen());
    m_audit = audit.get();
    AddChild(std::move(audit), Hint().Weight(1));

    ApplyTab();
}

void FileShareCenterScreen::SetHostWindow(HWND hwnd)
{
    m_shares->SetHostWindow(hwnd);
    m_audit->SetHostWindow(hwnd);
}

void FileShareCenterScreen::SetRequestSink(fsui::RequestSinkFn fn, void* user)
{
    m_shares->SetRequestSink(fn, user);
    m_audit->SetRequestSink(fn, user);
}

void FileShareCenterScreen::SetUiRequestHandler(fsui::UiRequestFn fn, void* user)
{
    m_shares->SetUiRequestHandler(fn, user);
    m_audit->SetUiRequestHandler(fn, user);
}

void FileShareCenterScreen::OnEnter()
{
    //换了登录身份：回到第一个页签（页签只在同一次登录内保持）
    const CString& token = AdminSession::Instance().Token();
    if (token != m_sessionToken)
    {
        m_sessionToken = token;
        if (m_activeTab != kTabShares)
        {
            m_activeTab = kTabShares;
            ApplyTab();
        }
    }
    FA_LOGI(_T("enter file share center, tab: %d"), m_activeTab);
    EnterActiveTab();
}

void FileShareCenterScreen::SelectTab(int tab)
{
    if (tab < 0 || tab >= kTabCount)
    {
        return;
    }
    //从外部直接打开某个页签时同样记下当前登录令牌，之后 OnEnter 不会把它当成换了身份
    m_sessionToken = AdminSession::Instance().Token();
    m_activeTab = tab;
    ApplyTab();
    FA_LOGI(_T("file share center: switch to tab %d"), m_activeTab);
    EnterActiveTab();
}

void FileShareCenterScreen::OnTabChanged(int index)
{
    SelectTab(index);
}

void FileShareCenterScreen::Refresh()
{
    if (m_activeTab == kTabAudit)
    {
        m_audit->Refresh();
    }
    else
    {
        m_shares->Refresh();
    }
}

LPCTSTR FileShareCenterScreen::SubtitleFor(int tab)
{
    if (tab < 0 || tab >= kTabCount)
    {
        return _T("");
    }
    return kSubtitles[tab];
}

void FileShareCenterScreen::ApplyTab()
{
    m_tabs->SetActiveIndex(m_activeTab);
    m_subtitle = kSubtitles[m_activeTab];
    m_header->SetTexts(kEyebrow, kTitle, m_subtitle);
    m_shares->SetVisible(m_activeTab == kTabShares);
    m_audit->SetVisible(m_activeTab == kTabAudit);
    //SetVisible 只改标志位并重绘，不会重新排版；两个页签都是本容器的直接子控件，
    //对本容器 ForceLayout 才能让新显示的那个按当前矩形重新排好（开发约定文档 UI 约定第 5 条）
    if (m_rcItem.right > m_rcItem.left)
    {
        ForceLayout(m_rcItem);
    }
    Invalidate();
}

void FileShareCenterScreen::EnterActiveTab()
{
    if (m_activeTab == kTabAudit)
    {
        m_audit->OnEnter();
    }
    else
    {
        m_shares->OnEnter();
    }
}

} // namespace flamingoadmin
