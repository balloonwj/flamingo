/**
 *  本人的备忘窗口，见 BalloonMemoWindow.h。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "BalloonMemoWindow.h"

#include <set>
#include <vector>
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM

#include "../../Common/Log/IULog.h"
#include "../../Feature/Memo/MemoClock.h"
#include "../../net/ContactsProtocolClient.h"   // net::ContactItem（打开编辑对话框时传空的候选列表）
#include "../BalloonMain/TitleBarIcon.h"   // CreateProgramIconBitmap（标题栏左侧图标）
#include "MemoCenter.h"
#include "MemoCalendarControls.h"
#include "MemoCardList.h"
#include "MemoWindowLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

using namespace balloonwjui;
using balloonmemo::CMemoCenter;

namespace {

// ---- 标题栏：透明、穿透显示客户区白底 ----
const COLORREF kCaptionColor  = RGB(0x55, 0x55, 0x55);
const int      kTitleIconSize = 16;
const COLORREF kTitleBarBg    = RGB(0xFF, 0xFF, 0xFF);

// 卡片外面的底色：客户区白底，见 MemoWindowLayout.cpp
const COLORREF kListBg = RGB(0xFF, 0xFF, 0xFF);

// 窗口标题
const wchar_t* const kWindowTitle = TR_NOOP(L"备忘与提醒");

// 右键菜单的命令 id
const UINT kMenuIdOpen   = 1;   // 修改 / 查看
const UINT kMenuIdDelete = 2;   // 删除

// 拼日期标题、条数提示用的缓冲区大小（宽字符个数）
const size_t kTextBufSize = 64;

// 星期的完整名称：0 周一 ... 6 周日。写成整词而不是「星期」+ 单字，是为了让各语言能整词翻译；
// 这里只做标记（TR_NOOP），显示处再用 TR 取译文
const wchar_t* const kWeekdayNames[memo::kDaysPerWeek] =
{
    TR_NOOP(L"星期一"), TR_NOOP(L"星期二"), TR_NOOP(L"星期三"), TR_NOOP(L"星期四"),
    TR_NOOP(L"星期五"), TR_NOOP(L"星期六"), TR_NOOP(L"星期日")
};

} // anonymous namespace

CBalloonMemoWindow::CBalloonMemoWindow()
    : m_year(0)
    , m_month(0)
    , m_selectedDay(0)
    , m_comboYearFirst(0)
    , m_comboYearLast(0)
    , m_pendingMemoId(0)
    , m_pYearCombo(NULL)
    , m_pMonthCombo(NULL)
    , m_pDayTitle(NULL)
    , m_pDayHint(NULL)
    , m_pList(NULL)
    , m_pAdd(NULL)
    , m_hTitleIcon(NULL)
{
    for (int i = 0; i < balloonmemo::kCalendarCells; ++i)
    {
        m_pCells[i] = NULL;
    }
}

CBalloonMemoWindow::~CBalloonMemoWindow()
{
    if (m_hTitleIcon != NULL)
    {
        ::DeleteObject(m_hTitleIcon);
        m_hTitleIcon = NULL;
    }
}

bool CBalloonMemoWindow::Open(HWND owner)
{
    if (IsWindow())
    {
        if (IsIconic())
        {
            ShowWindow(SW_RESTORE);
        }
        ShowWindow(SW_SHOW);
        ::SetForegroundWindow(m_hWnd);
        Refresh();
        return true;
    }
    if (!BuildWindow(owner))
    {
        return false;
    }
    const int64_t today = memo::NowLocalMinute() / memo::kMinutesPerDay;
    int day = 0;
    memo::CivilFromDays(today, m_year, m_month, day);
    m_selectedDay = today;
    CMemoCenter::Instance().RegisterObserver(m_hWnd);
    Refresh();
    if (::IsWindow(owner))
    {
        CenterWindow(owner);
    }
    else
    {
        CenterWindow();
    }
    ShowWindow(SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
    LOG_INFO("memo window opened: memos=%u, loaded=%d, list timed out=%d.",
             (uint32_t)CMemoCenter::Instance().Book().Items().size(), (int)CMemoCenter::Instance().Book().IsLoaded(),
             (int)CMemoCenter::Instance().ListTimedOut());
    return true;
}

void CBalloonMemoWindow::CloseWindow()
{
    if (IsWindow())
    {
        DestroyWindow();
    }
}

bool CBalloonMemoWindow::BuildWindow(HWND /*owner*/)
{
    //不设 owner：备忘窗口是独立窗口，任务栏上有自己的按钮，主面板收到托盘里时它照常留着
    if (Create(NULL, CWindow::rcDefault, TR(kWindowTitle), WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR("memo window: Create failed.");
        return false;
    }
    SetTitle(TR(kWindowTitle));
    SetTitleBarHeight(balloonmemo::kMemoWindowTitleBarH);
    SetTitleBarTransparent(true);
    SetTitleTextColor(kCaptionColor);
    SetCaptionGlyphColor(kCaptionColor);
    if (m_hTitleIcon == NULL)
    {
        m_hTitleIcon = balloonmain::CreateProgramIconBitmap(kTitleIconSize, kTitleBarBg);
    }
    if (m_hTitleIcon != NULL)
    {
        SetIcon(m_hTitleIcon);
    }
    //没有所有者、会出现在任务栏：任务栏与 Alt+Tab 也用程序图标（2026-10-07，UI 约定第 10 条）
    m_taskIcons.ApplyTaskIcons(m_hWnd);
    SetButtons(true, true, true);
    SetResizable(true);
    SetMinSize(balloonmemo::kMemoWindowWidth, balloonmemo::kMemoWindowHeight);
    //双击卡片打开修改：宿主默认不派发双击
    EnableDoubleClick(true);

    SetClientContent(balloonmemo::BuildMemoWindowBody());
    CaptureControls();
    //整窗尺寸（含标题栏），见 MemoWindowLayout.h；布局单测校验按这个尺寸排得下
    ResizeClient(balloonmemo::kMemoWindowWidth, balloonmemo::kMemoWindowHeight);
    return true;
}

void CBalloonMemoWindow::CaptureControls()
{
    DuiControl* root = GetClientContent();
    if (root == NULL)
    {
        return;
    }
    m_pYearCombo = dynamic_cast<DuiComboBox*>(root->FindCtrlById(balloonmemo::kMemoWinIdYear));
    m_pMonthCombo = dynamic_cast<DuiComboBox*>(root->FindCtrlById(balloonmemo::kMemoWinIdMonth));
    m_pDayTitle = dynamic_cast<DuiLabel*>(root->FindCtrlById(balloonmemo::kMemoWinIdDayTitle));
    m_pDayHint = dynamic_cast<DuiLabel*>(root->FindCtrlById(balloonmemo::kMemoWinIdDayHint));
    m_pList = dynamic_cast<DuiScrollView*>(root->FindCtrlById(balloonmemo::kMemoWinIdList));
    m_pAdd = dynamic_cast<DuiButton*>(root->FindCtrlById(balloonmemo::kMemoWinIdAdd));
    for (int i = 0; i < balloonmemo::kCalendarCells; ++i)
    {
        m_pCells[i] = dynamic_cast<balloonmemo::CMemoDayButton*>(
            root->FindCtrlById(balloonmemo::kMemoWinIdDayFirst + i));
    }
}

void CBalloonMemoWindow::Refresh()
{
    if (!IsWindow())
    {
        return;
    }
    const CMemoCenter& center = CMemoCenter::Instance();
    const memo::CMemoBook& book = center.Book();
    const memo::LocalMinute now = memo::NowLocalMinute();
    const int64_t today = now / memo::kMinutesPerDay;

    //月历
    int64_t firstDay = 0;
    int64_t endDay = 0;
    balloonmemo::MonthGridRange(m_year, m_month, firstDay, endDay);
    std::set<int64_t> eventDays;
    book.CollectEventDays(firstDay, endDay, eventDays);
    std::vector<balloonmemo::CalendarCell> cells;
    balloonmemo::BuildMonthCells(m_year, m_month, today, m_selectedDay, eventDays, cells);
    for (size_t i = 0; i < cells.size() && i < (size_t)balloonmemo::kCalendarCells; ++i)
    {
        if (m_pCells[i] != NULL)
        {
            m_pCells[i]->SetCell(cells[i]);
        }
    }
    RefreshMonthCombos();

    //右侧：选中那天
    int y = 0;
    int m = 0;
    int d = 0;
    memo::CivilFromDays(m_selectedDay, y, m, d);
    wchar_t buf[kTextBufSize] = { 0 };
    swprintf(buf, kTextBufSize, TR(L"%d月%d日 %s%s"), m, d, TR(kWeekdayNames[memo::WeekdayOfDays(m_selectedDay)]),
             (m_selectedDay == today) ? TR(L" · 今天") : L"");
    if (m_pDayTitle != NULL)
    {
        m_pDayTitle->SetText(buf);
    }

    //当天的卡片：群备忘带所属群名与创建人，只能查看
    std::vector<memo::MemoDayEntry> entries;
    book.ListDayEntries(m_selectedDay, entries);
    std::vector<balloonmemo::MemoCardText> cards;
    m_cardMemoIds.clear();
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const net::MemoItem* item = book.Find(entries[i].m_memoid);
        if (item == NULL)
        {
            continue;
        }
        std::wstring groupName;
        std::wstring creatorName;
        memo::LocalMinute createdAt = 0;
        if (item->m_ctype == net::kMemoCtypeGroup)
        {
            groupName = center.GroupNameOf(item->m_groupid);
            creatorName = center.UserNameOf(item->m_groupid, item->m_creator);
            createdAt = CMemoCenter::CreatedAtOf(*item);
        }
        cards.push_back(balloonmemo::BuildDayCard(*item, entries[i].m_event, groupName, creatorName, createdAt, now));
        m_cardMemoIds.push_back(item->m_memoid);
    }
    balloonmemo::SetMemoCards(m_pList, cards, kListBg);
    RefreshHoverAtCursor();

    //条数提示与「＋ 添加备忘」：拉取超时时提示没有加载出来，并置灰新建（服务端多半不认识备忘命令，存不了）
    const bool timedOut = center.ListTimedOut();
    if (m_pDayHint != NULL)
    {
        if (timedOut)
        {
            m_pDayHint->SetText(TR(L"备忘没有加载出来，服务器可能还不支持备忘"));
        }
        else if (!book.IsLoaded())
        {
            m_pDayHint->SetText(TR(L"正在加载备忘……"));
        }
        else if (cards.empty())
        {
            m_pDayHint->SetText(TR(L"这一天还没有备忘"));
        }
        else
        {
            m_pDayHint->SetText(TRF(L"共 {0} 条", (unsigned)cards.size()).c_str());
        }
    }
    if (m_pAdd != NULL)
    {
        m_pAdd->SetEnabled(!timedOut);
    }
}

void CBalloonMemoWindow::SelectDay(int64_t day)
{
    m_selectedDay = day;
    int y = 0;
    int m = 0;
    int d = 0;
    memo::CivilFromDays(day, y, m, d);
    m_year = y;
    m_month = m;
    Refresh();
}

void CBalloonMemoWindow::GoToMonth(int year, int month)
{
    if (year < balloonmemo::kCalendarYearMin || year > balloonmemo::kCalendarYearMax)
    {
        return;
    }
    m_selectedDay = balloonmemo::KeepDayOfMonth(year, month, m_selectedDay);
    m_year = year;
    m_month = month;
    Refresh();
}

void CBalloonMemoWindow::RefreshMonthCombos()
{
    if (m_pYearCombo != NULL)
    {
        int thisYear = 0;
        int thisMonth = 0;
        int thisDay = 0;
        memo::CivilFromDays(memo::NowLocalMinute() / memo::kMinutesPerDay, thisYear, thisMonth, thisDay);
        int first = 0;
        int last = 0;
        balloonmemo::CalendarYearRange(thisYear, m_year, first, last);
        //只扩不缩：已经列出过的年份保留。显示的年份就在现有的项里时不重填，于是在年份下拉框自己的
        //选择通知里切年（选的必然是现有的项）时不会清空它的项
        if (m_comboYearFirst > 0)
        {
            if (m_comboYearFirst < first)
            {
                first = m_comboYearFirst;
            }
            if (m_comboYearLast > last)
            {
                last = m_comboYearLast;
            }
        }
        if (first != m_comboYearFirst || last != m_comboYearLast)
        {
            m_pYearCombo->ResetContent();
            for (int y = first; y <= last; ++y)
            {
                m_pYearCombo->AddString(TRF(L"{0}年", y).c_str());
            }
            m_comboYearFirst = first;
            m_comboYearLast = last;
        }
        const int yearIndex = m_year - m_comboYearFirst;
        if (m_pYearCombo->GetCurSel() != yearIndex)
        {
            m_pYearCombo->SetCurSel(yearIndex, false);
        }
    }
    if (m_pMonthCombo != NULL && m_pMonthCombo->GetCurSel() != m_month - 1)
    {
        m_pMonthCombo->SetCurSel(m_month - 1, false);
    }
}

void CBalloonMemoWindow::OnAdd()
{
    if (CMemoCenter::Instance().ListTimedOut())
    {
        return;
    }
    const net::MemoItem item = balloonmemo::DefaultMemoForDay(m_selectedDay, memo::NowLocalMinute(),
                                                              net::kMemoCtypePersonal, 0);
    CMemoCenter::Instance().OpenEditor(m_hWnd, item, balloonmemo::kMemoEditNew, std::vector<net::ContactItem>());
}

void CBalloonMemoWindow::PostCardAction(int action, int cardIndex)
{
    if (cardIndex < 0 || cardIndex >= (int)m_cardMemoIds.size())
    {
        return;
    }
    m_pendingMemoId = m_cardMemoIds[cardIndex];
    PostMessage(kMsgMemoCardAction, (WPARAM)action, 0);
}

void CBalloonMemoWindow::OpenMemo(int64_t memoid)
{
    const net::MemoItem* item = CMemoCenter::Instance().Book().Find(memoid);
    if (item == NULL)
    {
        return;
    }
    //群备忘在本人的备忘窗口里只能查看，修改到群聊窗的「备忘」页签里去（那里有本群成员可选）
    const balloonmemo::MemoEditMode mode = (item->m_ctype == net::kMemoCtypeGroup)
                                               ? balloonmemo::kMemoEditReadOnly : balloonmemo::kMemoEditExisting;
    CMemoCenter::Instance().OpenEditor(m_hWnd, *item, mode, std::vector<net::ContactItem>());
}

void CBalloonMemoWindow::ShowCardMenu(int64_t memoid)
{
    const net::MemoItem* item = CMemoCenter::Instance().Book().Find(memoid);
    if (item == NULL)
    {
        return;
    }
    const bool personal = (item->m_ctype == net::kMemoCtypePersonal);
    DuiMenu menu;
    menu.AppendItem(kMenuIdOpen, personal ? TR(L"修改") : TR(L"查看"));
    if (personal)
    {
        menu.AppendSeparator();
        menu.AppendItem(kMenuIdDelete, TR(L"删除"));
    }
    POINT pt;
    ::GetCursorPos(&pt);
    const UINT chosen = menu.TrackPopup(pt.x, pt.y, m_hWnd);
    if (chosen == kMenuIdOpen)
    {
        OpenMemo(memoid);
    }
    else if (chosen == kMenuIdDelete)
    {
        CMemoCenter::Instance().ConfirmAndDelete(m_hWnd, memoid);
    }
}

void CBalloonMemoWindow::RefreshHoverAtCursor()
{
    POINT screen;
    ::GetCursorPos(&screen);
    POINT pt = screen;
    ScreenToClient(&pt);
    //光标不在本窗口上（被别的窗口挡住、或在窗口外）时撤掉全部悬停
    if (::WindowFromPoint(screen) != m_hWnd)
    {
        pt.x = -1;
        pt.y = -1;
    }
    balloonmemo::UpdateMemoCardHover(m_pList, pt);
}

LRESULT CBalloonMemoWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const DuiNotify* pNotify = reinterpret_cast<const DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }
    const UINT id = pNotify->ctrlId;
    int cardIndex = 0;
    int cardPart = 0;
    const bool onCard = balloonmemo::MemoCardFromCtrlId(id, cardIndex, cardPart);
    const bool onCardButton = onCard
        && (cardPart == balloonmemo::kMemoCardPartEdit || cardPart == balloonmemo::kMemoCardPartDelete);

    //卡片上的「修改」：与双击相同
    if (pNotify->code == DUIN_CLICK && onCard && cardPart == balloonmemo::kMemoCardPartEdit)
    {
        PostCardAction(kCardActionOpen, cardIndex);
        return 0;
    }
    //卡片上的「删除」：确认后删除
    if (pNotify->code == DUIN_CLICK && onCard && cardPart == balloonmemo::kMemoCardPartDelete)
    {
        PostCardAction(kCardActionDelete, cardIndex);
        return 0;
    }
    //双击卡片（落在任何一行文字上都算，两个按钮除外）：修改或查看
    if (pNotify->code == DUIN_DBLCLK && onCard && !onCardButton)
    {
        PostCardAction(kCardActionOpen, cardIndex);
        return 0;
    }
    //右键卡片：弹「修改 / 删除」菜单
    if (pNotify->code == DUIN_RCLICK && onCard)
    {
        PostCardAction(kCardActionMenu, cardIndex);
        return 0;
    }
    //年份下拉框选了一项：切到那一年的同一个月
    if (pNotify->code == DUIN_VALUECHANGED && id == (UINT)balloonmemo::kMemoWinIdYear && m_pYearCombo != NULL)
    {
        const int index = m_pYearCombo->GetCurSel();
        if (index >= 0 && m_comboYearFirst > 0)
        {
            GoToMonth(m_comboYearFirst + index, m_month);
        }
        return 0;
    }
    //月份下拉框选了一项：切到当年的那个月
    if (pNotify->code == DUIN_VALUECHANGED && id == (UINT)balloonmemo::kMemoWinIdMonth && m_pMonthCombo != NULL)
    {
        const int index = m_pMonthCombo->GetCurSel();
        if (index >= 0)
        {
            GoToMonth(m_year, index + 1);
        }
        return 0;
    }

    if (pNotify->code == DUIN_CLICK)
    {
        //日期格：选中这一天（上个月末、下个月初的格子会切到那个月）
        if (id >= (UINT)balloonmemo::kMemoWinIdDayFirst
            && id < (UINT)(balloonmemo::kMemoWinIdDayFirst + balloonmemo::kCalendarCells))
        {
            balloonmemo::CMemoDayButton* cell = m_pCells[id - balloonmemo::kMemoWinIdDayFirst];
            if (cell != NULL)
            {
                SelectDay(cell->Cell().m_day);
            }
            return 0;
        }
        switch (id)
        {
        //上个月：选中日随之移到上个月的同一天（没有这一天时取最后一天）
        case balloonmemo::kMemoWinIdPrev:
        //下个月：同上
        case balloonmemo::kMemoWinIdNext:
        {
            int y = m_year;
            int m = m_month;
            balloonmemo::ShiftMonth(y, m, (id == balloonmemo::kMemoWinIdPrev) ? -1 : 1);
            GoToMonth(y, m);
            return 0;
        }

        //今天
        case balloonmemo::kMemoWinIdToday:
            SelectDay(memo::NowLocalMinute() / memo::kMinutesPerDay);
            return 0;

        //「＋ 添加备忘」
        case balloonmemo::kMemoWinIdAdd:
            OnAdd();
            return 0;

        //其它控件的点击：交给框架
        default:
            break;
        }
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonMemoWindow::OnCardActionMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    const int64_t memoid = m_pendingMemoId;
    m_pendingMemoId = 0;
    if (memoid == 0)
    {
        return 0;
    }
    switch ((int)wParam)
    {
    //「修改」或双击
    case kCardActionOpen:
        OpenMemo(memoid);
        break;
    //「删除」：中枢弹确认框，删除成功后备忘簿变化、本窗口随之重绘
    case kCardActionDelete:
        CMemoCenter::Instance().ConfirmAndDelete(m_hWnd, memoid);
        break;
    //右键
    case kCardActionMenu:
        ShowCardMenu(memoid);
        break;
    //未定义的操作：不会出现，忽略
    default:
        break;
    }
    return 0;
}

LRESULT CBalloonMemoWindow::OnMouseMoveMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    POINT pt;
    pt.x = GET_X_LPARAM(lParam);
    pt.y = GET_Y_LPARAM(lParam);
    balloonmemo::UpdateMemoCardHover(m_pList, pt);
    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonMemoWindow::OnMouseLeaveMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    //鼠标离开整个窗口：撤掉全部悬停，否则会留一张卡片的按钮一直显示
    POINT none;
    none.x = -1;
    none.y = -1;
    balloonmemo::UpdateMemoCardHover(m_pList, none);
    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonMemoWindow::OnMemoChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    Refresh();
    return 0;
}

LRESULT CBalloonMemoWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    CloseWindow();
    return 0;
}

LRESULT CBalloonMemoWindow::OnDestroyMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    CMemoCenter::Instance().UnregisterObserver(m_hWnd);
    bHandled = FALSE;
    return 0;
}
