/**
 *  BalloonAnnouncePopupDlg.cpp
 *
 *  新群公告提示窗的实现：一次性搭好骨架，之后每来一条新公告只改写文本。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#include "stdafx.h"
#include "BalloonAnnouncePopupDlg.h"

#include <memory>

#include "../BalloonMain/TitleBarIcon.h"   // CreateProgramIconBitmap（标题栏左侧图标）
#include "../../Common/Log/IULog.h"                   // LOG_INFO / LOG_ERROR
#include "../../Session/UserSessionData.h"         // FMG_MSG_OPEN_GROUP_ANNOUNCE
#include "GroupManageModel.h"           // AnnounceSeverity

#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiBadge.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiSeparator.h"

using namespace balloonwjui;

namespace {

// ---- 尺寸（逻辑像素）-------------------------------------------------

const int kDlgWidth      = 380;   // 客户区宽
const int kDlgHeight     = 300;   // 客户区高
const int kDlgTitleBarH  = 32;    // 标题栏高
const int kTitleIconSize = 16;    // 标题栏图标边长

const int kRootPad   = 18;   // 客户区四周内边距
const int kRootGap   = 10;   // 各行之间的间距
const int kGroupRowH = 22;   // 顶部「群名 + 优先级徽章」行高
const int kTitleH    = 26;   // 公告标题行高
const int kMetaH     = 20;   // 「发布人 · 时间」行高
const int kButtonH   = 34;   // 底部按钮高
const int kBadgeW    = 52;   // 优先级徽章宽

// ---- 配色 ------------------------------------------------------------

const COLORREF kBgColor      = RGB(0xFF, 0xFF, 0xFF);   // 客户区白底
const COLORREF kGroupColor   = RGB(0x9A, 0x98, 0x93);   // 顶部群名（弱化）
const COLORREF kTitleColor   = RGB(0x2A, 0x2A, 0x2A);   // 公告标题
const COLORREF kContentColor = RGB(0x55, 0x55, 0x55);   // 公告正文
const COLORREF kMetaColor    = RGB(0x9A, 0x98, 0x93);   // 发布人 · 时间
const COLORREF kSepColor     = RGB(0xEC, 0xEA, 0xE6);   // 正文与落款之间的分隔线
const COLORREF kTitleBarBg   = RGB(0xFF, 0xFF, 0xFF);   // 标题栏底色

// ---- 控件 id ---------------------------------------------------------

enum
{
    kIdBtnGotIt   = 1,   // 知道了（关窗）
    kIdBtnViewAll = 2    // 查看全部（打开群聊窗的公告边栏）
};

// 把优先级翻译成徽章文案与配色。与群管理页公告卡上那枚徽章同一套取值，
// 两处显示不一致会让人以为是两个东西。
//   severity：取值见 AnnounceSeverity；
//   bgOut / fgOut：[出参] 徽章底色与文字色。
// 返回：徽章文案（编译期常量，不需释放）。
LPCTSTR SeverityBadgeStyle(int severity, COLORREF& bgOut, COLORREF& fgOut)
{
    if (severity == balloongroupmanage::kSeverityUrgent)
    {
        bgOut = RGB(0xFD, 0xE3, 0xE3);
        fgOut = RGB(0xD9, 0x3F, 0x3F);
        return _T("紧急");
    }
    if (severity == balloongroupmanage::kSeverityImportant)
    {
        bgOut = RGB(0xFD, 0xE9, 0xDD);
        fgOut = RGB(0xE0, 0x6A, 0x2E);
        return _T("重要");
    }
    bgOut = RGB(0xE6, 0xEC, 0xFB);
    fgOut = RGB(0x45, 0x6C, 0xDF);
    return _T("通知");
}

// 注：标题栏图标的合成逻辑已抽到 BalloonMain/TitleBarIcon.h 共用，见下方调用处。

} // anonymous namespace

// =======================================================================
// 构造 / 析构
// =======================================================================

CBalloonAnnouncePopupDlg::CBalloonAnnouncePopupDlg()
    : m_panelHwnd(NULL)
    , m_groupId(0)
    , m_built(false)
    , m_groupLabel(NULL)
    , m_severity(NULL)
    , m_title(NULL)
    , m_content(NULL)
    , m_meta(NULL)
    , m_titleIconBmp(NULL)
{
}

CBalloonAnnouncePopupDlg::~CBalloonAnnouncePopupDlg()
{
    if (m_titleIconBmp != NULL)
    {
        ::DeleteObject(m_titleIconBmp);
        m_titleIconBmp = NULL;
    }
}

// =======================================================================
// 显示
// =======================================================================

void CBalloonAnnouncePopupDlg::ShowAnnouncement(HWND ownerPanel, int64_t groupId,
                                                LPCTSTR groupName, LPCTSTR title,
                                                LPCTSTR content, int severity,
                                                LPCTSTR publisherName, LPCTSTR timeText)
{
    m_panelHwnd = ownerPanel;
    m_groupId   = groupId;

    if (!IsWindow())
    {
        // owner 传 NULL：让子控件通知自路由回本对话框（与关闭确认 / 新建群对话框同处理）。
        if (Create(NULL, CWindow::rcDefault, _T("新群公告"),
                   WS_OVERLAPPEDWINDOW, 0) == NULL)
        {
            LOG_ERROR(_T("新群公告提示窗 Create 失败。"));
            return;
        }
        SetTitle(_T("新群公告"));
        SetTitleBarHeight(kDlgTitleBarH);
        SetButtons(false, false, true);   // 只留关闭按钮
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

        m_built = false;
        BuildUiOnce();
        ResizeClient(kDlgWidth, kDlgHeight);
        CenterWindow(m_panelHwnd);
    }

    // 换上这一条公告的内容。骨架不重建 —— 上一条还没关就又来一条时，直接换内容
    // 并前置，弹一摞窗口出来比漏掉一条更糟。
    if (m_groupLabel != NULL)
    {
        m_groupLabel->SetText((groupName != NULL) ? groupName : _T(""));
    }
    if (m_severity != NULL)
    {
        COLORREF bg = 0;
        COLORREF fg = 0;
        LPCTSTR  label = SeverityBadgeStyle(severity, bg, fg);
        m_severity->SetText(label);
        m_severity->SetBgColor(bg);
        m_severity->SetTextColor(fg);
    }
    if (m_title != NULL)
    {
        m_title->SetText((title != NULL) ? title : _T(""));
    }
    if (m_content != NULL)
    {
        m_content->SetText((content != NULL) ? content : _T(""));
    }
    if (m_meta != NULL)
    {
        // 发布人昵称取不到时只显示时间，不留一个孤零零的间隔点
        CString text;
        if (publisherName != NULL && publisherName[0] != _T('\0'))
        {
            text.Format(_T("%s · %s"), publisherName,
                        (timeText != NULL) ? timeText : _T(""));
        }
        else
        {
            text = (timeText != NULL) ? timeText : _T("");
        }
        m_meta->SetText(text);
    }

    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);

    LOG_INFO(_T("新群公告提示窗已弹出，groupid=%lld。"), (long long)groupId);
}

// =======================================================================
// 骨架构建（一次）
// =======================================================================

void CBalloonAnnouncePopupDlg::BuildUiOnce()
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

    // ---- 顶部：群名 + 优先级徽章 ----
    {
        std::unique_ptr<DuiHBox> row(new DuiHBox());
        row->SetGap(8);

        std::unique_ptr<DuiLabel> groupLabel(new DuiLabel());
        groupLabel->SetTextColor(kGroupColor);
        groupLabel->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        m_groupLabel = groupLabel.get();
        row->AddChild(std::move(groupLabel), Hint().Weight(1));

        std::unique_ptr<DuiBadge> badge(new DuiBadge());
        badge->SetMaxDisplayChars(0);    // 不截断（chip 用法）
        badge->SetCornerRadius(4);
        badge->SetHideWhenEmpty(false);
        m_severity = badge.get();
        row->AddChild(std::move(badge),
                      Hint().Fixed(kBadgeW).AlignC(DuiLayout::AlignCenter));

        root->AddChild(std::move(row), Hint().Fixed(kGroupRowH));
    }

    // ---- 公告标题 ----
    {
        std::unique_ptr<DuiLabel> title(new DuiLabel());
        title->SetTextColor(kTitleColor);
        //标题放不下就末尾省略，不换行 —— 换行会把正文挤出窗口
        title->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        m_title = title.get();
        root->AddChild(std::move(title), Hint().Fixed(kTitleH));
    }

    // ---- 公告正文（占满中间，自动换行）----
    {
        std::unique_ptr<DuiLabel> content(new DuiLabel());
        content->SetTextColor(kContentColor);
        content->SetWordWrap(true);
        content->SetTextAlign(DT_LEFT | DT_TOP | DT_WORDBREAK);
        m_content = content.get();
        root->AddChild(std::move(content), Hint().Weight(1));
    }

    // ---- 分隔线 ----
    {
        std::unique_ptr<DuiSeparator> sep(new DuiSeparator());
        sep->SetOrientation(DuiSeparator::Horizontal);
        sep->SetColor(kSepColor);
        root->AddChild(std::move(sep), Hint().Fixed(1));
    }

    // ---- 发布人 · 时间 ----
    {
        std::unique_ptr<DuiLabel> meta(new DuiLabel());
        meta->SetTextColor(kMetaColor);
        meta->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        m_meta = meta.get();
        root->AddChild(std::move(meta), Hint().Fixed(kMetaH));
    }

    // ---- 底部按钮栏 ----
    {
        std::unique_ptr<DuiHBox> row(new DuiHBox());
        row->SetGap(10);
        row->AddChild(std::unique_ptr<DuiControl>(new DuiControl()), Hint().Weight(1));

        std::unique_ptr<DuiButton> viewAll(new DuiButton());
        viewAll->SetCtrlId(kIdBtnViewAll);
        viewAll->SetText(_T("查看全部"));
        viewAll->SetButtonType(DuiButton::StylePushButton);
        row->AddChild(std::move(viewAll), Hint().Fixed(96, kButtonH));

        std::unique_ptr<DuiButton> gotIt(new DuiButton());
        gotIt->SetCtrlId(kIdBtnGotIt);
        gotIt->SetText(_T("知道了"));
        gotIt->SetButtonType(DuiButton::StylePushButton);
        gotIt->SetVariant(DuiButton::Variant::Primary);
        row->AddChild(std::move(gotIt), Hint().Fixed(96, kButtonH));

        root->AddChild(std::move(row), Hint().Fixed(kButtonH));
    }

    SetClientContent(std::unique_ptr<DuiControl>(root.release()));
    m_built = true;
}

// =======================================================================
// 消息处理
// =======================================================================

LRESULT CBalloonAnnouncePopupDlg::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                             LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 只隐藏不销毁：下一条公告来时直接换内容再显示，省掉重建窗口与控件树。
    // 实例的生命周期归主面板管。
    if (IsWindow())
    {
        ShowWindow(SW_HIDE);
    }
    return 0;
}

LRESULT CBalloonAnnouncePopupDlg::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                              LPARAM lParam, BOOL& bHandled)
{
    const DuiNotify* pNotify = reinterpret_cast<const DuiNotify*>(lParam);
    if (pNotify == NULL || pNotify->code != DUIN_CLICK)
    {
        bHandled = FALSE;
        return 0;
    }

    switch (pNotify->ctrlId)
    {
    case kIdBtnGotIt:
        if (IsWindow())
        {
            ShowWindow(SW_HIDE);
        }
        return 0;

    case kIdBtnViewAll:
        // 打开群聊窗并切到公告边栏归主面板管（群聊窗集合在它手里），本窗口只发
        // 一条窗口间消息请它代劳。没有宿主面板时静默跳过。
        if (m_panelHwnd != NULL && ::IsWindow(m_panelHwnd))
        {
            ::PostMessage(m_panelHwnd, FMG_MSG_OPEN_GROUP_ANNOUNCE,
                          (WPARAM)m_groupId, 0);
        }
        if (IsWindow())
        {
            ShowWindow(SW_HIDE);
        }
        return 0;

    default:
        break;
    }

    bHandled = FALSE;
    return 0;
}
