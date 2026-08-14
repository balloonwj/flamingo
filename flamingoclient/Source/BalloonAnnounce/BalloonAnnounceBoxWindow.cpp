/**
 *  「消息盒子」窗口实现，BalloonAnnounceBoxWindow.cpp
 *  设计与约定见 BalloonAnnounceBoxWindow.h。
 *  balloonwj@qq.com   2026-08-05
 */
#include "stdafx.h"
#include "BalloonAnnounceBoxWindow.h"

#include "AnnounceRow.h"
#include "../BalloonHistory/HistoryContentView.h"   //右栏正文控件（按实际宽度测高、命中链接）
#include "../FlamingoClient.h"
#include "../BalloonMain/BalloonMainPanel.h"
#include "../BalloonMain/TitleBarIcon.h"     // CreateProgramIconBitmap（标题栏左侧图标）
#include "../BalloonMain/MainPanelScrollView.h"        // 两栏的悬浮式自动隐藏滚动视口
#include "../BalloonMain/OverlayScrollBarGeometry.h"   // ComputeOverlayScrollBar / ComputeResizeBorderInsetPx
#include "../net/IUSocket.h"                 // SendMarkAnnouncementRead（已读上报）
#include "../IULog.h"
#include "../BalloonChatWnd/ChatLinkDetect.h" // IsSafeLinkUrl（打开链接前的协议白名单）
#include <shellapi.h>                        // ShellExecuteW（打开链接）

#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM

using namespace balloonannounce;

namespace {

// ---- 窗口尺寸（像素）----
const int kAnnWindowWidth   = 820;   // 初始宽：左栏 268 + 右栏正文足够宽
const int kAnnWindowHeight  = 560;   // 初始高
const int kAnnMinWidth      = 640;   // 最小宽：再窄右栏正文就没法看了
const int kAnnMinHeight     = 420;   // 最小高
const int kAnnTitleBarH     = 38;    // 标题栏高
const int kAnnTitleIconSize = 16;    // 标题栏左侧图标边长

// ---- 标题栏配色 ----
const COLORREF kAnnCaptionColor = RGB(0x33, 0x33, 0x33);   // 标题文字与 min/max/close 字形
const COLORREF kAnnTitleBarBg   = RGB(0xFF, 0xFF, 0xFF);   // 标题栏透明后穿透显示的底色（白）

//UTF-8 std::string → CString（UTF-16）。本地库里的文本都是 UTF-8。
CString Utf8ToCString(const std::string& utf8)
{
    if (utf8.empty())
    {
        return CString(_T(""));
    }
    int need = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), NULL, 0);
    if (need <= 0)
    {
        return CString(_T(""));
    }
    CString out;
    LPWSTR buf = out.GetBuffer(need + 1);
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), buf, need);
    buf[need] = L'\0';
    out.ReleaseBuffer(need);
    return out;
}

} // namespace

CBalloonAnnounceBoxWindow::CBalloonAnnounceBoxWindow(CFlamingoClient* pFMGClient)
    : m_lpFMGClient(pFMGClient)
    , m_pMainPanel(NULL)
    , m_selectedIndex(-1)
    , m_pScroll(NULL)
    , m_hTitleIcon(NULL)
{
}

CBalloonAnnounceBoxWindow::~CBalloonAnnounceBoxWindow()
{
    if (m_hTitleIcon != NULL)
    {
        ::DeleteObject(m_hTitleIcon);
        m_hTitleIcon = NULL;
    }
}

void CBalloonAnnounceBoxWindow::OpenBox(CBalloonMainPanel* pMainPanel)
{
    m_pMainPanel = pMainPanel;

    //已开着：前置并刷新一遍 —— 这期间可能收到过新公告。
    if (IsWindow())
    {
        RefreshContent();
        ::ShowWindow(m_hWnd, SW_SHOW);
        ::SetForegroundWindow(m_hWnd);
        return;
    }

    ReloadRows();

    //默认选中第一条，右栏不至于一上来就是空的。列表为空时保持 -1。
    m_selectedIndex = m_rows.empty() ? -1 : 0;

    if (!BuildWindow())
    {
        return;
    }

    //默认选中的那条若是未读，同样要标已读并上报 —— 用户确实看到它了。
    if (m_selectedIndex >= 0)
    {
        SelectRow(m_selectedIndex);
    }

    LOG_INFO(_T("消息盒子窗口已创建并显示，公告 %d 条。"), (int)m_rows.size());
}

void CBalloonAnnounceBoxWindow::RefreshIfOpen()
{
    if (!IsWindow())
    {
        return;
    }
    //延迟到事件解栈之后再刷：本方法可能在推送处理链路里被调到，而刷新会销毁
    //列表里的行控件。
    ::PostMessage(m_hWnd, WM_ANNBOX_REFRESH_CONTENT, 0, 0);
}

bool CBalloonAnnounceBoxWindow::BuildWindow()
{
    if (Create(NULL, CWindow::rcDefault, _T("FlamingoIM"),
               WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR(_T("消息盒子窗口 Create 失败。"));
        return false;
    }

    SetTitle(_T("消息盒子"));
    SetTitleBarHeight(kAnnTitleBarH);
    SetTitleBarTransparent(true);
    SetTitleTextColor(kAnnCaptionColor);
    SetCaptionGlyphColor(kAnnCaptionColor);

    //标题栏左侧图标：用程序主图标。本窗标题栏透明，穿透显示的是根控件底色（白）。
    if (m_hTitleIcon == NULL)
    {
        m_hTitleIcon = balloonmain::CreateProgramIconBitmap(kAnnTitleIconSize, kAnnTitleBarBg);
    }
    if (m_hTitleIcon != NULL)
    {
        SetIcon(m_hTitleIcon);
    }

    SetButtons(true, true, true);   //最小化 / 最大化 / 关闭
    SetResizable(true);
    SetMinSize(kAnnMinWidth, kAnnMinHeight);

    InitUi();

    ResizeClient(kAnnWindowWidth, kAnnWindowHeight);
    CenterWindow();
    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);

    //布局已落定（ResizeClient 触发过一次重排），此时控件矩形有效，正文高度才算得准。
    SyncDetailBodyHeight();
    //滚动条要给窗口缩放边框让出的宽度同样得等布局落定才算得出。
    SyncScrollBarEdgeInset();
    return true;
}

void CBalloonAnnounceBoxWindow::InitUi()
{
    AnnounceBoxViewData data;
    BuildViewData(data);

    SetClientContent(BuildAnnounceBoxBody(data));

    //捕获滚动视口指针：局部刷新与命中测试都要用它。
    balloonwjui::DuiControl* content = GetClientContent();
    if (content != NULL)
    {
        m_pScroll = dynamic_cast<balloonmain::MainPanelScrollView*>(
            content->FindCtrlById(kIdAnnScroll));
    }
}

void CBalloonAnnounceBoxWindow::ReloadRows()
{
    m_rows.clear();
    m_bodies.clear();

    std::vector<balloonhistory::AnnounceRecord> records;
    //QueryAll 已按「置顶优先、发布时间倒序」排好，与列表展示顺序一致，这里不再排。
    balloonhistory::CAnnounceStore::GetInstance().QueryAll(records);

    m_rows.reserve(records.size());
    m_bodies.reserve(records.size());

    for (size_t i = 0; i < records.size(); ++i)
    {
        const balloonhistory::AnnounceRecord& rec = records[i];

        AnnounceRowView view;
        view.m_announcementId = rec.m_announcementId;
        view.m_title    = Utf8ToCString(rec.m_title);
        view.m_severity = rec.m_severity;
        view.m_pinned   = (rec.m_pinned != 0);
        view.m_unread   = (rec.m_read == 0);

        //「发布者 · 时间」合成一行。发布者为空（老数据）时只显示时间，不留一个孤零零的分隔点。
        CString sender = Utf8ToCString(rec.m_senderName);
        //时间格式化与新公告提示窗共用同一支函数，两处显示的时间口径一致。
        CString when   = FormatAnnounceTime(rec.m_sentTimeMs);
        if (!sender.IsEmpty() && !when.IsEmpty())
        {
            view.m_metaText = sender + _T(" · ") + when;
        }
        else if (!sender.IsEmpty())
        {
            view.m_metaText = sender;
        }
        else
        {
            view.m_metaText = when;
        }

        m_rows.push_back(view);
        m_bodies.push_back(Utf8ToCString(rec.m_body));
    }

    //选中下标可能因为公告增删而失效（比如正看的那条被撤回了），夹回合法范围。
    if (m_selectedIndex >= (int)m_rows.size())
    {
        m_selectedIndex = m_rows.empty() ? -1 : (int)m_rows.size() - 1;
    }
}

void CBalloonAnnounceBoxWindow::BuildViewData(AnnounceBoxViewData& out) const
{
    out.m_rows        = m_rows;
    out.m_totalCount  = (int)m_rows.size();
    out.m_selectedIndex = m_selectedIndex;

    int unread = 0;
    for (size_t i = 0; i < m_rows.size(); ++i)
    {
        if (m_rows[i].m_unread)
        {
            ++unread;
        }
    }
    out.m_unreadCount = unread;

    if (m_selectedIndex >= 0 && m_selectedIndex < (int)m_rows.size())
    {
        out.m_detailTitle = m_rows[m_selectedIndex].m_title;
        out.m_detailMeta  = m_rows[m_selectedIndex].m_metaText;
        out.m_detailBody  = m_bodies[m_selectedIndex];
    }

    out.m_emptyTitle    = _T("暂无公告");
    out.m_emptySubtitle = _T("平台发布公告后会出现在这里");
}

void CBalloonAnnounceBoxWindow::RefreshContent()
{
    //本方法只服务于"公告的集合变了"这一类场景（收到新公告、公告被撤回），要新增 / 删除
    //行控件，只能整棵重建。单纯的选中变化与已读变化走 ApplyStateToUi 定点更新，不到这里。
    //
    //重建会新建滚动视口，滚动位置随之归零，故先把它记下来。恢复时不必自己夹范围，
    //DuiScrollBar::SetPos 会夹到 [0, 内容高 - 视口高]：公告被撤回导致列表变短时，
    //位置会自动落到新的底部。
    const int savedListScrollPos = (m_pScroll != NULL) ? m_pScroll->GetScrollPos() : 0;

    ReloadRows();

    //布局不必自己动手：SetClientContent 内部会 LayoutSkeleton()（见 DuiFrameWindow.cpp）。
    InitUi();
    //换了公告就换了正文，折行高度跟着变，重算一次滚动范围。
    SyncDetailBodyHeight();
    //控件树是新建的，让位宽度也要重新告诉这两个新的滚动视口。
    SyncScrollBarEdgeInset();

    //滚动位置要在布局落定、内容高度算好之后再恢复，否则此刻滚动范围还是 0，设了也白设。
    if (m_pScroll != NULL && savedListScrollPos > 0)
    {
        m_pScroll->SetScrollPos(savedListScrollPos);
    }

    Invalidate();
}

void CBalloonAnnounceBoxWindow::ApplyStateToUi(bool refreshDetail)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    balloonannounce::AnnounceBoxViewData data;
    BuildViewData(data);

    balloonannounce::ApplyAnnounceListState(content, data);
    if (refreshDetail)
    {
        balloonannounce::ApplyAnnounceDetail(content, data);
    }
    //各控件的 setter 自己会把变脏的那块标出来重绘，这里不必再整窗 Invalidate。
}

void CBalloonAnnounceBoxWindow::SelectRow(int index)
{
    if (index < 0 || index >= (int)m_rows.size())
    {
        return;
    }

    const bool wasUnread = m_rows[index].m_unread;
    //点的还是当前这条、而且早就读过了：界面上没有任何东西会变，直接返回。
    const bool selectionChanged = (index != m_selectedIndex);
    if (!selectionChanged && !wasUnread)
    {
        return;
    }

    m_selectedIndex = index;

    if (wasUnread)
    {
        //乐观更新：先把本地库与内存里的这条标已读，界面立刻不再显示未读点；
        //随后上报服务端。上报失败只记日志（见主面板的 OnAnnounceMarkReadResult），
        //下次登录时服务端下发的 read 位会把状态纠正回来。
        const int64_t annId = m_rows[index].m_announcementId;
        balloonhistory::CAnnounceStore::GetInstance().MarkRead(annId);
        m_rows[index].m_unread = false;

        CIUSocket::GetInstance().SendMarkAnnouncementRead(annId);
        LOG_INFO(_T("消息盒子：公告 %lld 标记已读并上报。"), (long long)annId);

        //未读数变了，Banner 上的角标要跟着刷。
        if (m_pMainPanel != NULL)
        {
            m_pMainPanel->RefreshAnnounceBadge();
        }
    }

    //定点更新，不重建控件树 —— 选中一行时公告的集合并没有变，变的只是每行的选中态与
    //未读点、顶部计数、底部按钮，以及右栏那三项。重建整棵树既要重查本地库、重新排版、
    //重新测正文高度，还会把列表的滚动位置清零：用户点了靠下的一行，列表却跳回顶部
    //（2026-08-12 用户实测反馈）。定点更新没有这个副作用。
    //
    //另外，本方法是在行控件的点击处理链路里被调到的。老做法要绕一圈 PostMessage
    //延迟到事件解栈之后才刷，正是因为重建会销毁正被点击的那个行控件；定点更新只改
    //控件状态、不销毁任何控件，可以就地做完。
    ApplyStateToUi(selectionChanged);
}

void CBalloonAnnounceBoxWindow::MarkAllRead()
{
    int marked = 0;
    for (size_t i = 0; i < m_rows.size(); ++i)
    {
        if (!m_rows[i].m_unread)
        {
            continue;
        }
        const int64_t annId = m_rows[i].m_announcementId;
        balloonhistory::CAnnounceStore::GetInstance().MarkRead(annId);
        m_rows[i].m_unread = false;
        //逐条上报：服务端的已读回执表是按 (公告ID, 用户ID) 记的，没有"批量已读"这条协议。
        CIUSocket::GetInstance().SendMarkAnnouncementRead(annId);
        ++marked;
    }

    LOG_INFO(_T("消息盒子：全部标为已读，共 %d 条。"), marked);

    if (marked > 0 && m_pMainPanel != NULL)
    {
        m_pMainPanel->RefreshAnnounceBadge();
    }

    //同样是定点更新：公告的集合没变，只是未读标记全清了 —— 各行的小圆点、顶部计数、
    //底部按钮的可用性跟着变，右栏显示的还是原来那条，不必动。
    ApplyStateToUi(false);
}

LRESULT CBalloonAnnounceBoxWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                              LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    //关闭只销毁本窗，不影响主程序。下次打开时重新建窗并重查本地库。
    DestroyWindow();
    m_pScroll = NULL;
    return 0;
}

LRESULT CBalloonAnnounceBoxWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                               LPARAM lParam, BOOL& bHandled)
{
    balloonwjui::DuiNotify* pNotify = (balloonwjui::DuiNotify*)lParam;
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == kIdAnnMarkAllRead)
    {
        MarkAllRead();
        return 0;
    }

    //其余通知交回基类。
    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonAnnounceBoxWindow::OnRefreshContentMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                       LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    RefreshContent();
    return 0;
}

LRESULT CBalloonAnnounceBoxWindow::OnMouseMoveMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                  LPARAM lParam, BOOL& bHandled)
{
    POINT pt;
    pt.x = GET_X_LPARAM(lParam);
    pt.y = GET_Y_LPARAM(lParam);
    UpdateRowHover(pt);

    //只旁听、不消费：DuiHost 还要靠这条消息驱动按钮的 hover、滚动条拖动等等。
    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonAnnounceBoxWindow::OnMouseLeaveMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                   LPARAM /*lParam*/, BOOL& bHandled)
{
    //鼠标离开整个窗口：把悬停态全撤掉，否则会留一行"亮着"的。
    POINT none;
    none.x = -1;
    none.y = -1;
    UpdateRowHover(none);

    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonAnnounceBoxWindow::OnLButtonUpMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                  LPARAM lParam, BOOL& bHandled)
{
    POINT pt;
    pt.x = GET_X_LPARAM(lParam);
    pt.y = GET_Y_LPARAM(lParam);

    const int index = HitTestRow(pt);
    if (index >= 0)
    {
        SelectRow(index);
    }

    //同样只旁听：底部按钮的点击还要靠 DuiHost 派发。
    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonAnnounceBoxWindow::OnSizeMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                             LPARAM /*lParam*/, BOOL& bHandled)
{
    //先让 DuiFrameWindow / DuiHost 走完客户区重排，本处只做正文高度的补算。
    //置 bHandled=FALSE 让消息继续走原有链路。
    bHandled = FALSE;

    //尺寸变化会重排控件，但重排发生在本消息继续传递之后；这里投递一条延迟刷新，
    //等布局落定再按新宽度算高度。
    if (IsWindow())
    {
        ::PostMessage(m_hWnd, WM_ANNBOX_SYNC_BODY_HEIGHT, 0, 0);
    }
    return 0;
}

LRESULT CBalloonAnnounceBoxWindow::OnChatOpenLinkMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                     LPARAM lParam, BOOL& /*bHandled*/)
{
    std::wstring* pUrl = (std::wstring*)lParam;
    if (pUrl == NULL)
    {
        return 0;
    }

    // 协议白名单：只放行 http / https，与聊天窗、消息记录窗、公告弹窗同一口径。
    if (balloonchatwnd::IsSafeLinkUrl(*pUrl))
    {
        ::ShellExecuteW(m_hWnd, L"open", pUrl->c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
    else
    {
        LOG_ERROR("消息盒子拒绝打开非 http/https 链接。");
    }
    delete pUrl;
    return 0;
}

LRESULT CBalloonAnnounceBoxWindow::OnSyncBodyHeightMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                       LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    SyncDetailBodyHeight();
    //尺寸变了，视口右缘与窗口缩放边框的相对关系也可能变，重算一次让位宽度。
    SyncScrollBarEdgeInset();
    return 0;
}

LRESULT CBalloonAnnounceBoxWindow::OnScrollBarAnimTimer(UINT /*uMsg*/, WPARAM wParam,
                                                        LPARAM /*lParam*/, BOOL& bHandled)
{
    if ((UINT_PTR)wParam != balloonmain::kScrollbarAnimTimerId)
    {
        //别人的定时器，放行给基类 / DefWindowProc。
        bHandled = FALSE;
        return 0;
    }

    bHandled = TRUE;

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        KillTimer(balloonmain::kScrollbarAnimTimerId);
        return 0;
    }

    //左右两栏各推进一帧。只要还有一个需要下一帧，定时器就继续跑。
    const UINT scrollIds[2] = { kIdAnnScroll, kIdAnnDetailScroll };
    bool needMore = false;
    for (int i = 0; i < 2; ++i)
    {
        balloonmain::MainPanelScrollView* sv =
            dynamic_cast<balloonmain::MainPanelScrollView*>(
                content->FindCtrlById(scrollIds[i]));
        if (sv == NULL)
        {
            continue;
        }
        if (sv->TickScrollBarFade((int)balloonmain::kScrollbarAnimIntervalMs))
        {
            needMore = true;
        }
    }

    if (!needMore)
    {
        //两条滚动条都已稳定（淡出到底 / 无内容可滚），停掉 pulse 省电；
        //下次滚动或鼠标移到滚动条上时，视口会自己把它重新起起来。
        KillTimer(balloonmain::kScrollbarAnimTimerId);
    }
    return 0;
}

void CBalloonAnnounceBoxWindow::SyncDetailBodyHeight()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    balloonwjui::DuiScrollView* sv = dynamic_cast<balloonwjui::DuiScrollView*>(
        content->FindCtrlById(kIdAnnDetailScroll));
    balloonhistory::HistoryContentView* body =
        dynamic_cast<balloonhistory::HistoryContentView*>(
            content->FindCtrlById(kIdAnnDetailBody));
    //真正的算法在布局模块里（那边才拿得到正文区内边距这些布局常量，也才能被
    //FlamingoUnit 直接测到）。本方法只负责把两个控件取出来递过去。
    balloonannounce::SyncAnnounceDetailBodyHeight(sv, body);
}

void CBalloonAnnounceBoxWindow::SyncScrollBarEdgeInset()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    //与 DuiFrameWindow::OnNcHitTestMsg 同一口径：边框宽度是 96-dpi 逻辑像素，
    //按当前显示器 DPI 折算成物理像素后才能和布局矩形对得上。
    const int borderPx = balloonmain::ComputeResizeBorderInsetPx(GetBorderPx(),
                                                                 GetDpi(),
                                                                 IsResizable());

    RECT rcClient;
    GetClientRect(&rcClient);

    const UINT scrollIds[2] = { kIdAnnScroll, kIdAnnDetailScroll };
    for (int i = 0; i < 2; ++i)
    {
        balloonmain::MainPanelScrollView* sv =
            dynamic_cast<balloonmain::MainPanelScrollView*>(
                content->FindCtrlById(scrollIds[i]));
        if (sv == NULL)
        {
            continue;
        }

        //视口右缘离客户区右缘还有多远。右栏视口本就内缩了一段内边距，左栏视口更是
        //远在窗口中间，这段距离往往已经盖过整个缩放边框，那就一点都不用让。
        const RECT svRc = sv->GetRect();
        const int gap = rcClient.right - svRc.right;
        int inset = borderPx - gap;
        if (inset < 0)
        {
            inset = 0;
        }
        sv->SetEdgeInsetPx(inset);
    }
}

bool CBalloonAnnounceBoxWindow::PointOnListScrollBar(POINT pt) const
{
    if (m_pScroll == NULL)
    {
        return false;
    }

    const balloonmain::OverlayScrollBarMetrics bar =
        balloonmain::ComputeOverlayScrollBar(m_pScroll->GetRect(),
                                             m_pScroll->GetContentHeight(),
                                             m_pScroll->GetScrollPos(),
                                             m_pScroll->GetEdgeInsetPx());
    //内容没溢出时列表根本没有滚动条，此时不该凭空排掉一条能点的区域。
    if (!bar.m_visible)
    {
        return false;
    }
    return ::PtInRect(&bar.m_rcHit, pt) != FALSE;
}

int CBalloonAnnounceBoxWindow::HitTestRow(POINT pt) const
{
    if (m_pScroll == NULL)
    {
        return -1;
    }
    balloonwjui::DuiControl* content = m_pScroll->GetContent();
    if (content == NULL)
    {
        return -1;
    }

    //落点必须同时落在滚动视口内：滚出可视区的行其矩形仍在，只按行矩形判会让
    //视口外的行也被点中。
    const RECT viewRc = m_pScroll->GetRect();
    if (pt.x < viewRc.left || pt.x >= viewRc.right
        || pt.y < viewRc.top || pt.y >= viewRc.bottom)
    {
        return -1;
    }

    //落在悬浮滚动条的命中带上就不算点中任何一行 —— 那一下是冲着滑块去的，
    //拖完松手不该顺带把滑块底下那条公告选中。
    if (PointOnListScrollBar(pt))
    {
        return -1;
    }

    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = content->Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        AnnounceRow* row = dynamic_cast<AnnounceRow*>(kids[i].get());
        if (row == NULL)
        {
            //行间分隔线等非行控件跳过。
            continue;
        }
        const RECT rc = row->GetRect();
        if (pt.x >= rc.left && pt.x < rc.right && pt.y >= rc.top && pt.y < rc.bottom)
        {
            //行下标从控件 id 反解，比"数第几个 AnnounceRow"稳妥 —— 中间夹着分隔线，
            //两者的序号并不相同。
            return RowIndexFromCtrlId(row->GetCtrlId());
        }
    }
    return -1;
}

void CBalloonAnnounceBoxWindow::UpdateRowHover(POINT pt)
{
    if (m_pScroll == NULL)
    {
        return;
    }
    balloonwjui::DuiControl* content = m_pScroll->GetContent();
    if (content == NULL)
    {
        return;
    }

    //鼠标压在悬浮滚动条上时不点亮任何一行 —— 此刻用户是在操作滚动条，
    //底下那行跟着亮起来只会让人以为要点它。
    const RECT viewRc = m_pScroll->GetRect();
    const bool inView = (pt.x >= viewRc.left && pt.x < viewRc.right
                         && pt.y >= viewRc.top && pt.y < viewRc.bottom)
                        && !PointOnListScrollBar(pt);

    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = content->Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        AnnounceRow* row = dynamic_cast<AnnounceRow*>(kids[i].get());
        if (row == NULL)
        {
            continue;
        }
        const RECT rc = row->GetRect();
        const bool hit = inView
            && pt.x >= rc.left && pt.x < rc.right
            && pt.y >= rc.top && pt.y < rc.bottom;
        //SetHovered 对"值没变"自己会短路，这里不必再判一次。
        row->SetHovered(hit);
    }
}
