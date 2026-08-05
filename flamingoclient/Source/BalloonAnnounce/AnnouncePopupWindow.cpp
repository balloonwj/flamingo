/**
 *  「新公告」桌面提示窗实现，AnnouncePopupWindow.cpp
 *  设计与约定见 AnnouncePopupWindow.h。
 *  balloonwj@qq.com   2026-08-05
 */
#include "stdafx.h"
#include "AnnouncePopupWindow.h"

#include <memory>

#include "../BalloonMain/TitleBarIcon.h"   // CreateProgramIconBitmap（标题栏左侧图标）
#include "../IULog.h"                      // LOG_INFO / LOG_ERROR
#include "../UserSessionData.h"            // FMG_MSG_OPEN_ANNOUNCE_BOX

#include "../../balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiBadge.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../balloonui/balloonui/Controls/Basic/DuiSeparator.h"

using balloonwjui::DuiControl;
using balloonwjui::DuiLayout;
using balloonwjui::DuiVBox;
using balloonwjui::DuiHBox;
using balloonwjui::DuiLabel;
using balloonwjui::DuiBadge;
using balloonwjui::DuiButton;
using balloonwjui::DuiSeparator;

namespace {

// ---- 控件 id ----
const UINT kIdPopupKnown   = 901;   // 「知道了」按钮
const UINT kIdPopupViewAll = 902;   // 「查看全部」按钮

// ---- 尺寸（像素）----
const int kDlgWidth      = 360;   // 客户区宽
const int kDlgHeight     = 250;   // 客户区高
const int kDlgTitleBarH  = 32;    // 标题栏高
const int kTitleIconSize = 16;    // 标题栏图标边长

const int kRootPad   = 18;   // 客户区四周内边距
const int kRootGap   = 10;   // 各行之间的间距
const int kBadgeRowH = 22;   // 顶部徽章行高
const int kTitleH    = 26;   // 公告标题行高
const int kMetaH     = 20;   // 「发布者 · 时间」行高
const int kButtonH   = 34;   // 底部按钮高
const int kBadgeW    = 52;   // 级别徽章宽

//弹窗距工作区右 / 下边缘的留白。留一点缝，贴死了像是被裁掉半截。
const int kCornerMarginX = 16;
const int kCornerMarginY = 16;

// ---- 配色 ----
const COLORREF kBgColor      = RGB(0xFF, 0xFF, 0xFF);   // 客户区白底
const COLORREF kSourceColor  = RGB(0x9A, 0x98, 0x93);   // 顶部「平台公告」四个字（弱化）
const COLORREF kTitleColor   = RGB(0x2A, 0x2A, 0x2A);   // 公告标题
const COLORREF kContentColor = RGB(0x55, 0x55, 0x55);   // 公告正文
const COLORREF kMetaColor    = RGB(0x9A, 0x98, 0x93);   // 发布者 · 时间
const COLORREF kSepColor     = RGB(0xEC, 0xEA, 0xE6);   // 正文与落款之间的分隔线
const COLORREF kTitleBarBg   = RGB(0xFF, 0xFF, 0xFF);   // 标题栏底色

// ---- 级别徽章的文字与配色。取值对应 announcement_severity（0 普通 1 提醒 2 警告）----
const COLORREF kBadgeInfoBg    = RGB(0xEF, 0xEE, 0xEB);
const COLORREF kBadgeInfoFg    = RGB(0x7A, 0x78, 0x74);
const COLORREF kBadgeNoticeBg  = RGB(0xFD, 0xF0, 0xDC);
const COLORREF kBadgeNoticeFg  = RGB(0xB5, 0x6A, 0x14);
const COLORREF kBadgeWarningBg = RGB(0xFD, 0xE4, 0xE4);
const COLORREF kBadgeWarningFg = RGB(0xC0, 0x2B, 0x2B);

//按级别取徽章文字。越界值按「普通」处理。
LPCTSTR SeverityText(int severity)
{
    if (severity == 1)
    {
        return _T("提醒");
    }
    if (severity == 2)
    {
        return _T("警告");
    }
    return _T("通知");
}

} // namespace

CAnnouncePopupWindow::CAnnouncePopupWindow()
    : m_panelHwnd(NULL)
    , m_announcementId(0)
    , m_built(false)
    , m_severityBadge(NULL)
    , m_title(NULL)
    , m_body(NULL)
    , m_meta(NULL)
    , m_titleIconBmp(NULL)
{
}

CAnnouncePopupWindow::~CAnnouncePopupWindow()
{
    if (m_titleIconBmp != NULL)
    {
        ::DeleteObject(m_titleIconBmp);
        m_titleIconBmp = NULL;
    }
}

void CAnnouncePopupWindow::ShowAnnouncement(HWND ownerPanel, int64_t announcementId,
                                            LPCTSTR title, LPCTSTR body, int severity,
                                            LPCTSTR senderName, LPCTSTR timeText)
{
    m_panelHwnd      = ownerPanel;
    m_announcementId = announcementId;

    if (!IsWindow())
    {
        //owner 传 NULL：让子控件通知自路由回本窗口（与群公告提示窗同处理）。
        if (Create(NULL, CWindow::rcDefault, _T("新公告"),
                   WS_OVERLAPPEDWINDOW, 0) == NULL)
        {
            LOG_ERROR(_T("新公告提示窗 Create 失败。"));
            return;
        }
        SetTitle(_T("新公告"));
        SetTitleBarHeight(kDlgTitleBarH);
        SetButtons(false, false, true);   //只留关闭按钮
        SetResizable(false);
        SetMinSize(kDlgWidth, kDlgHeight + kDlgTitleBarH);

        if (m_titleIconBmp == NULL)
        {
            m_titleIconBmp = balloonmain::CreateProgramIconBitmap(kTitleIconSize, kTitleBarBg);
        }
        if (m_titleIconBmp != NULL)
        {
            SetIcon(m_titleIconBmp);
        }

        BuildUiOnce();
        ResizeClient(kDlgWidth, kDlgHeight);
    }

    //改写内容。骨架只建一次，这里只换文本 —— 上一条还没关就来了新的，就地替换。
    if (m_severityBadge != NULL)
    {
        m_severityBadge->SetText(SeverityText(severity));
        if (severity == 2)
        {
            m_severityBadge->SetBgColor(kBadgeWarningBg);
            m_severityBadge->SetTextColor(kBadgeWarningFg);
        }
        else if (severity == 1)
        {
            m_severityBadge->SetBgColor(kBadgeNoticeBg);
            m_severityBadge->SetTextColor(kBadgeNoticeFg);
        }
        else
        {
            m_severityBadge->SetBgColor(kBadgeInfoBg);
            m_severityBadge->SetTextColor(kBadgeInfoFg);
        }
    }
    if (m_title != NULL)
    {
        m_title->SetText(title != NULL ? title : _T(""));
    }
    if (m_body != NULL)
    {
        m_body->SetText(body != NULL ? body : _T(""));
    }
    if (m_meta != NULL)
    {
        //发布者为空时只显示时间，不留一个孤零零的分隔点。
        CString meta;
        const bool hasSender = (senderName != NULL && senderName[0] != _T('\0'));
        const bool hasTime   = (timeText != NULL && timeText[0] != _T('\0'));
        if (hasSender && hasTime)
        {
            meta.Format(_T("%s · %s"), senderName, timeText);
        }
        else if (hasSender)
        {
            meta = senderName;
        }
        else if (hasTime)
        {
            meta = timeText;
        }
        m_meta->SetText(meta);
    }

    //每次弹出都重新贴一次右下角：显示器分辨率 / 任务栏位置可能已经变了。
    MoveToWorkAreaCorner();

    ShowWindow(SW_SHOWNOACTIVATE);   //不抢前台焦点，别打断用户正在做的事
    //置顶，否则会被别的窗口盖住、等于没弹。
    ::SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    Invalidate();

    LOG_INFO(_T("弹出新公告提示窗，公告ID %lld。"), (long long)announcementId);
}

void CAnnouncePopupWindow::MoveToWorkAreaCorner()
{
    //取鼠标所在显示器的工作区（已排除任务栏），多显示器下弹在用户正看的那块屏上。
    POINT pt;
    ::GetCursorPos(&pt);
    HMONITOR mon = ::MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);

    MONITORINFO mi;
    ::ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!::GetMonitorInfo(mon, &mi))
    {
        //取不到就退回不挪窗，至少别把窗口扔到屏幕外。
        return;
    }

    RECT rcWnd;
    ::GetWindowRect(m_hWnd, &rcWnd);
    const int w = rcWnd.right - rcWnd.left;
    const int h = rcWnd.bottom - rcWnd.top;

    //贴工作区右下角，各留一段边距。按工作区反推坐标，天然落在可见范围内
    //（开发约定文档 UI 约定第 3 条：弹层必须整体落在工作区内）。
    int x = mi.rcWork.right - w - kCornerMarginX;
    int y = mi.rcWork.bottom - h - kCornerMarginY;

    //窗口比工作区还大时（极端小分辨率），退到工作区左上角，保证标题栏可见、关得掉。
    if (x < mi.rcWork.left)
    {
        x = mi.rcWork.left;
    }
    if (y < mi.rcWork.top)
    {
        y = mi.rcWork.top;
    }

    ::SetWindowPos(m_hWnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void CAnnouncePopupWindow::BuildUiOnce()
{
    if (m_built)
    {
        return;
    }

    typedef DuiLayout::Hint Hint;

    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetPadding(kRootPad, kRootPad, kRootPad, kRootPad);
    root->SetGap(kRootGap);
    root->SetBgColor(kBgColor);

    // ---- 顶部：「平台公告」+ 级别徽章 ----
    {
        std::unique_ptr<DuiHBox> row(new DuiHBox());
        row->SetGap(8);

        std::unique_ptr<DuiLabel> source(new DuiLabel());
        source->SetText(_T("平台公告"));
        source->SetTextColor(kSourceColor);
        source->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        row->AddChild(std::move(source), Hint().Weight(1));

        std::unique_ptr<DuiBadge> badge(new DuiBadge());
        badge->SetMaxDisplayChars(0);    //不截断（chip 用法）
        badge->SetCornerRadius(4);
        badge->SetHideWhenEmpty(false);
        m_severityBadge = badge.get();
        row->AddChild(std::move(badge),
                      Hint().Fixed(kBadgeW).AlignC(DuiLayout::AlignCenter));

        root->AddChild(std::move(row), Hint().Fixed(kBadgeRowH));
    }

    // ---- 公告标题 ----
    {
        std::unique_ptr<DuiLabel> title(new DuiLabel());
        title->SetTextColor(kTitleColor);
        //标题放不下就末尾省略，不换行 —— 换行会把正文挤出窗口。
        title->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        m_title = title.get();
        root->AddChild(std::move(title), Hint().Fixed(kTitleH));
    }

    // ---- 公告正文（占满中间，自动换行；放不下的部分去消息盒子看全文）----
    {
        std::unique_ptr<DuiLabel> body(new DuiLabel());
        body->SetTextColor(kContentColor);
        //与消息盒子右栏同一个坑：必须显式 SetWordWrap(true)，光给 DT_WORDBREAK 不生效。
        body->SetWordWrap(true);
        body->SetTextAlign(DT_LEFT | DT_TOP | DT_WORDBREAK);
        m_body = body.get();
        root->AddChild(std::move(body), Hint().Weight(1));
    }

    // ---- 分隔线 ----
    {
        std::unique_ptr<DuiSeparator> sep(new DuiSeparator());
        sep->SetOrientation(DuiSeparator::Horizontal);
        sep->SetColor(kSepColor);
        root->AddChild(std::move(sep), Hint().Fixed(1));
    }

    // ---- 发布者 · 时间 ----
    {
        std::unique_ptr<DuiLabel> meta(new DuiLabel());
        meta->SetTextColor(kMetaColor);
        meta->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        m_meta = meta.get();
        root->AddChild(std::move(meta), Hint().Fixed(kMetaH));
    }

    // ---- 底部按钮栏：右对齐的「知道了」「查看全部」----
    {
        std::unique_ptr<DuiHBox> row(new DuiHBox());
        row->SetGap(8);
        //左侧留白把两枚按钮顶到右边。
        row->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));

        std::unique_ptr<DuiButton> known(new DuiButton());
        known->SetButtonType(DuiButton::StylePushButton);
        known->SetText(_T("知道了"));
        known->SetCtrlId(kIdPopupKnown);
        row->AddChild(std::unique_ptr<DuiControl>(known.release()), Hint().Fixed(84));

        std::unique_ptr<DuiButton> viewAll(new DuiButton());
        viewAll->SetButtonType(DuiButton::StylePushButton);
        viewAll->SetText(_T("查看全部"));
        viewAll->SetCtrlId(kIdPopupViewAll);
        row->AddChild(std::unique_ptr<DuiControl>(viewAll.release()), Hint().Fixed(90));

        root->AddChild(std::move(row), Hint().Fixed(kButtonH));
    }

    SetClientContent(std::unique_ptr<DuiControl>(root.release()));
    m_built = true;
}

LRESULT CAnnouncePopupWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                         LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    //只隐藏、不销毁：骨架与控件指针留着，下一条公告来了直接换文本再显示，
    //省掉重建。窗口对象由主面板持有，随主面板一起析构。
    ShowWindow(SW_HIDE);
    return 0;
}

LRESULT CAnnouncePopupWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                          LPARAM lParam, BOOL& bHandled)
{
    balloonwjui::DuiNotify* pNotify = (balloonwjui::DuiNotify*)lParam;
    if (pNotify != NULL && pNotify->code == balloonwjui::DUIN_CLICK)
    {
        if (pNotify->ctrlId == kIdPopupKnown)
        {
            ShowWindow(SW_HIDE);
            return 0;
        }
        if (pNotify->ctrlId == kIdPopupViewAll)
        {
            //请主面板打开消息盒子。用 PostMessage 而不是直接调：本处仍在按钮的事件
            //回调栈上，而打开窗口会走一串建窗 / 布局，投递出去更稳妥。
            if (m_panelHwnd != NULL)
            {
                ::PostMessage(m_panelHwnd, FMG_MSG_OPEN_ANNOUNCE_BOX, 0, 0);
            }
            ShowWindow(SW_HIDE);
            return 0;
        }
    }

    bHandled = FALSE;
    return 0;
}
