/**
 *  备忘的右下角提醒弹窗，见 MemoReminderPopup.h。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"
#include "MemoReminderPopup.h"

#include <memory>
#include <string>

#include "../../Common/Log/IULog.h"
#include "../../Common/Util/EncodeUtil.h"
#include "../../Feature/Memo/MemoClock.h"
#include "../BalloonMain/TitleBarIcon.h"            // CreateProgramIconBitmap
#include "../BalloonMain/CornerPopupPlacement.h"    // 贴工作区右下角的落点计算（与公告、特别关注提示窗共用）
#include "MemoCenter.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

using namespace balloonwjui;
using balloonmemo::CMemoCenter;

namespace {

// ---- 控件 id。到点提醒的两个按钮按行号分配：第 i 行的「知道了」是 kIdKnownFirst + i ----
const UINT kIdKnownFirst  = 1000;   // 各行「知道了」
const UINT kIdSnoozeFirst = 2000;   // 各行「10 分钟后再提醒」
const UINT kIdTitleFirst  = 3000;   // 各行标题（点了打开这条备忘）
const UINT kIdMissedKnown = 4000;   // 错过的提醒那一行的「知道了」
const UINT kIdAllKnown    = 4001;   // 「全部知道了」

// 按钮动作，经 kMsgDeferredAction 延后执行
enum PopupAction
{
    kActionNone = 0,        //没有待执行的动作
    kActionDismiss = 1,     //某条的「知道了」
    kActionSnooze = 2,      //某条的「10 分钟后再提醒」
    kActionOpen = 3,        //点了某条的标题：打开这条备忘
    kActionMissed = 4,      //错过的提醒那一行的「知道了」
    kActionAll = 5,         //「全部知道了」
};

// 最多列出几条到点提醒；再多的合并成一行「还有 N 条」（点「全部知道了」一起关）
const size_t kMaxDueRows = 4;

// 错过的提醒那一行里最多列出几个标题
const size_t kMaxMissedTitles = 3;

// ---- 尺寸（逻辑像素）----
//2026-10-03 联调时用户反馈弹窗太小、不显眼：整窗由 360 宽加到 440，标题由 10 磅加到 13 磅，按钮加高，
//有备注时多一行备注
const int kWindowW      = 440;   // 整窗宽
const int kTitleBarH    = 36;    // 标题栏高
const int kTitleIconSz  = 16;    // 标题栏图标边长
const int kPad          = 16;    // 客户区内边距
const int kGap          = 10;    // 各行之间的间距
const int kCardPad      = 14;    // 每条提醒卡片的内边距
const int kCardGap      = 6;     // 卡片内各行的间距
const int kCardRadius   = 8;     // 卡片圆角
const int kTitleH       = 30;    // 标题行高
const int kMetaH        = 22;    // 时间行高
const int kNoteH        = 22;    // 备注行高（单行，放不下末尾省略）
const int kButtonsH     = 34;    // 按钮行高
const int kSnoozeW      = 150;   // 「10 分钟后再提醒」宽
const int kKnownW       = 92;    // 「知道了」宽
const int kAllKnownW    = 110;   // 「全部知道了」宽
const int kFooterH      = 32;    // 底部「全部知道了」一行的高
const int kMoreH        = 22;    // 「还有 N 条」一行的高
const int kTitlePt      = 13;    // 标题磅值
const int kMetaPt       = 10;    // 时间、备注、错过的标题这几行的磅值
const int kTwo          = 2;     // 上下两条内边距

// ---- 配色 ----
const COLORREF kBg        = RGB(0xFF, 0xFF, 0xFF);
const COLORREF kCardBg    = RGB(0xF5, 0xF7, 0xFA);   // 每条提醒的卡片底色
const COLORREF kMissedBg  = RGB(0xFD, 0xF6, 0xEC);   // 错过的提醒那一行的底色
const COLORREF kTitleFg   = RGB(0x2A, 0x2A, 0x2A);
const COLORREF kMetaFg    = RGB(0x8A, 0x8A, 0x8A);
const COLORREF kTitleBar  = RGB(0xFF, 0xFF, 0xFF);

// 拼文字用的缓冲区大小（宽字符个数）
const size_t kTextBufSize = 128;

std::unique_ptr<DuiControl> MakeLabel(UINT id, const std::wstring& text, COLORREF color, int pt, bool bold)
{
    std::unique_ptr<DuiLabel> label(new DuiLabel());
    if (id != 0)
    {
        label->SetCtrlId(id);
    }
    label->SetText(text.c_str());
    label->SetTextColor(color);
    if (pt > 0)
    {
        label->SetTextPointSize(pt, bold);
    }
    label->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    return std::unique_ptr<DuiControl>(label.release());
}

std::unique_ptr<DuiControl> MakeButton(UINT id, LPCTSTR text, DuiButton::Variant variant)
{
    std::unique_ptr<DuiButton> button(new DuiButton());
    button->SetButtonType(DuiButton::StylePushButton);
    button->SetVariant(variant);
    button->SetCtrlId(id);
    button->SetText(text);
    return std::unique_ptr<DuiControl>(button.release());
}

// 一条到点提醒的时间行：「今天 09:30 · 提前 10 分钟 · 群：XX」
std::wstring MetaOf(const net::MemoItem& item, memo::LocalMinute event)
{
    std::wstring text = balloonmemo::EventTimeText(event, memo::NowLocalMinute());
    if (item.m_advance > 0)
    {
        text += L" · ";
        text += balloonmemo::AdvanceText(item.m_advance);
    }
    if (item.m_ctype == net::kMemoCtypeGroup)
    {
        text += TR(L" · 群：");
        text += CMemoCenter::Instance().GroupNameOf(item.m_groupid);
    }
    return text;
}

} // anonymous namespace

CMemoReminderPopup::CMemoReminderPopup()
    : m_pendingAction(kActionNone)
    , m_pendingMemoId(0)
    , m_hTitleIcon(NULL)
{
}

CMemoReminderPopup::~CMemoReminderPopup()
{
    if (m_hTitleIcon != NULL)
    {
        ::DeleteObject(m_hTitleIcon);
        m_hTitleIcon = NULL;
    }
}

bool CMemoReminderPopup::EnsureWindow()
{
    if (IsWindow())
    {
        return true;
    }
    //owner 传 NULL：让子控件通知回到本窗口（与新公告提示窗同一处理）
    if (Create(NULL, CWindow::rcDefault, TR(L"备忘提醒"), WS_OVERLAPPEDWINDOW, WS_EX_TOOLWINDOW) == NULL)
    {
        LOG_ERROR("memo reminder popup: Create failed.");
        return false;
    }
    SetTitle(TR(L"备忘提醒"));
    SetTitleBarHeight(kTitleBarH);
    //没有关闭按钮：提醒一直留着，点「知道了」才关
    SetButtons(false, false, false);
    SetResizable(false);
    if (m_hTitleIcon == NULL)
    {
        m_hTitleIcon = balloonmain::CreateProgramIconBitmap(kTitleIconSz, kTitleBar);
    }
    if (m_hTitleIcon != NULL)
    {
        SetIcon(m_hTitleIcon);
    }
    return true;
}

void CMemoReminderPopup::ShowEntries(const std::vector<balloonmemo::ReminderEntry>& due,
                                     const std::vector<balloonmemo::ReminderEntry>& missed)
{
    if (due.empty() && missed.empty())
    {
        if (IsWindow())
        {
            ShowWindow(SW_HIDE);
        }
        return;
    }
    if (!EnsureWindow())
    {
        return;
    }

    typedef DuiLayout::Hint Hint;
    const memo::CMemoBook& book = CMemoCenter::Instance().Book();
    m_rowMemoIds.clear();
    m_allMemoIds.clear();

    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kBg);
    root->SetPadding(kPad, kPad, kPad, kPad);
    root->SetGap(kGap);
    int contentH = kPad * kTwo;
    int rows = 0;

    for (size_t i = 0; i < due.size(); ++i)
    {
        m_allMemoIds.push_back(due[i].m_memoid);
        if (i >= kMaxDueRows)
        {
            continue;
        }
        const net::MemoItem* item = book.Find(due[i].m_memoid);
        if (item == NULL)
        {
            continue;
        }
        const UINT row = (UINT)m_rowMemoIds.size();
        m_rowMemoIds.push_back(item->m_memoid);

        std::unique_ptr<DuiVBox> card(new DuiVBox());
        card->SetBgColor(kCardBg);
        card->SetCornerRadius(kCardRadius);
        card->SetPadding(kCardPad, kCardPad, kCardPad, kCardPad);
        card->SetGap(kCardGap);
        card->AddChild(MakeLabel(kIdTitleFirst + row, EncodeUtil::Utf8ToUnicode(item->m_title), kTitleFg, kTitlePt, true),
                       Hint().Fixed(kTitleH));
        card->AddChild(MakeLabel(0, MetaOf(*item, due[i].m_event), kMetaFg, kMetaPt, false), Hint().Fixed(kMetaH));
        //群备忘多一行创建人：「张勇 · 10月3日 22:59 创建」
        int creatorRows = 0;
        const std::wstring creator = CMemoCenter::Instance().CreatorLineOf(*item, memo::NowLocalMinute());
        if (!creator.empty())
        {
            card->AddChild(MakeLabel(0, creator, kMetaFg, kMetaPt, false), Hint().Fixed(kMetaH));
            creatorRows = 1;
        }
        //有备注时多一行备注；换行折成空格，单行放不下末尾省略
        int noteRows = 0;
        if (!item->m_note.empty())
        {
            card->AddChild(MakeLabel(0, balloonmemo::NoteOneLine(item->m_note), kTitleFg, kMetaPt, false),
                           Hint().Fixed(kNoteH));
            noteRows = 1;
        }
        std::unique_ptr<DuiHBox> buttons(new DuiHBox());
        buttons->SetGap(kGap);
        buttons->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
        buttons->AddChild(MakeButton(kIdSnoozeFirst + row, TR(L"10 分钟后再提醒"), DuiButton::Variant::Default),
                          Hint().Fixed(kSnoozeW));
        buttons->AddChild(MakeButton(kIdKnownFirst + row, TR(L"知道了"), DuiButton::Variant::Primary), Hint().Fixed(kKnownW));
        card->AddChild(std::unique_ptr<DuiControl>(buttons.release()), Hint().Fixed(kButtonsH));
        const int cardH = kCardPad * kTwo + kTitleH + kMetaH + kButtonsH + kCardGap * kTwo
                          + creatorRows * (kMetaH + kCardGap) + noteRows * (kNoteH + kCardGap);
        root->AddChild(std::unique_ptr<DuiControl>(card.release()), Hint().Fixed(cardH));
        contentH += cardH;
        ++rows;
    }
    if (due.size() > kMaxDueRows)
    {
        wchar_t buf[kTextBufSize] = { 0 };
        swprintf(buf, kTextBufSize, TR(L"还有 %u 条提醒"), (unsigned)(due.size() - kMaxDueRows));
        root->AddChild(MakeLabel(0, buf, kMetaFg, kMetaPt, false), Hint().Fixed(kMoreH));
        contentH += kMoreH;
        ++rows;
    }

    if (!missed.empty())
    {
        std::wstring titles;
        for (size_t i = 0; i < missed.size(); ++i)
        {
            m_allMemoIds.push_back(missed[i].m_memoid);
            const net::MemoItem* item = book.Find(missed[i].m_memoid);
            if (item == NULL || i >= kMaxMissedTitles)
            {
                continue;
            }
            if (!titles.empty())
            {
                titles += L"、";
            }
            titles += EncodeUtil::Utf8ToUnicode(item->m_title);
        }
        if (missed.size() > kMaxMissedTitles)
        {
            //{0} 为上面拼好的标题列表
            titles = TRF(L"{0} 等", titles);
        }
        wchar_t buf[kTextBufSize] = { 0 };
        swprintf(buf, kTextBufSize, TR(L"你有 %u 条错过的提醒"), (unsigned)missed.size());

        std::unique_ptr<DuiVBox> card(new DuiVBox());
        card->SetBgColor(kMissedBg);
        card->SetCornerRadius(kCardRadius);
        card->SetPadding(kCardPad, kCardPad, kCardPad, kCardPad);
        card->SetGap(kCardGap);
        card->AddChild(MakeLabel(0, buf, kTitleFg, kTitlePt, true), Hint().Fixed(kTitleH));
        card->AddChild(MakeLabel(0, titles, kMetaFg, kMetaPt, false), Hint().Fixed(kMetaH));
        std::unique_ptr<DuiHBox> buttons(new DuiHBox());
        buttons->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
        buttons->AddChild(MakeButton(kIdMissedKnown, TR(L"知道了"), DuiButton::Variant::Primary), Hint().Fixed(kKnownW));
        card->AddChild(std::unique_ptr<DuiControl>(buttons.release()), Hint().Fixed(kButtonsH));
        const int cardH = kCardPad * kTwo + kTitleH + kMetaH + kButtonsH + kCardGap * kTwo;
        root->AddChild(std::unique_ptr<DuiControl>(card.release()), Hint().Fixed(cardH));
        contentH += cardH;
        ++rows;
    }

    if (m_allMemoIds.size() > 1)
    {
        std::unique_ptr<DuiHBox> footer(new DuiHBox());
        footer->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));
        footer->AddChild(MakeButton(kIdAllKnown, TR(L"全部知道了"), DuiButton::Variant::Text), Hint().Fixed(kAllKnownW));
        root->AddChild(std::unique_ptr<DuiControl>(footer.release()), Hint().Fixed(kFooterH));
        contentH += kFooterH;
        ++rows;
    }
    contentH += (rows > 1) ? (rows - 1) * kGap : 0;

    SetClientContent(std::unique_ptr<DuiControl>(root.release()));
    //整窗高度 = 标题栏 + 内容（DuiFrameWindow::ResizeClient 设的是整窗）
    SetMinSize(kWindowW, kTitleBarH + contentH);
    ResizeClient(kWindowW, kTitleBarH + contentH);
    balloonmain::MoveWindowToWorkAreaCorner(m_hWnd, balloonmain::kCornerPopupMarginX, balloonmain::kCornerPopupMarginY);
    ShowWindow(SW_SHOWNOACTIVATE);
    //置顶，否则会被别的窗口盖住、等于没弹
    ::SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    Invalidate();
    LOG_INFO("memo reminder popup shown: due=%u, missed=%u.", (uint32_t)due.size(), (uint32_t)missed.size());
}

void CMemoReminderPopup::CloseWindow()
{
    if (IsWindow())
    {
        DestroyWindow();
    }
    m_rowMemoIds.clear();
    m_allMemoIds.clear();
}

LRESULT CMemoReminderPopup::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    //没有关闭按钮；Alt+F4 等途径发来的关闭请求一律忽略：提醒要点「知道了」才关
    return 0;
}

LRESULT CMemoReminderPopup::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const DuiNotify* pNotify = reinterpret_cast<const DuiNotify*>(lParam);
    if (pNotify == NULL || pNotify->code != DUIN_CLICK)
    {
        bHandled = FALSE;
        return 0;
    }
    const UINT id = pNotify->ctrlId;
    const UINT rowCount = (UINT)m_rowMemoIds.size();
    //动作会重建整个控件树，这里只记下来、投递给自己，等点击通知处理完再执行
    if (id >= kIdKnownFirst && id < kIdKnownFirst + rowCount)
    {
        PostAction(kActionDismiss, m_rowMemoIds[id - kIdKnownFirst]);
        return 0;
    }
    if (id >= kIdSnoozeFirst && id < kIdSnoozeFirst + rowCount)
    {
        PostAction(kActionSnooze, m_rowMemoIds[id - kIdSnoozeFirst]);
        return 0;
    }
    if (id >= kIdTitleFirst && id < kIdTitleFirst + rowCount)
    {
        PostAction(kActionOpen, m_rowMemoIds[id - kIdTitleFirst]);
        return 0;
    }
    if (id == kIdMissedKnown)
    {
        PostAction(kActionMissed, 0);
        return 0;
    }
    if (id == kIdAllKnown)
    {
        PostAction(kActionAll, 0);
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

void CMemoReminderPopup::PostAction(int action, int64_t memoid)
{
    m_pendingAction = action;
    m_pendingMemoId = memoid;
    PostMessage(kMsgDeferredAction);
}

LRESULT CMemoReminderPopup::OnDeferredAction(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    const int action = m_pendingAction;
    const int64_t memoid = m_pendingMemoId;
    m_pendingAction = kActionNone;
    m_pendingMemoId = 0;
    CMemoCenter& center = CMemoCenter::Instance();
    switch (action)
    {
    //某条的「知道了」
    case kActionDismiss:
        center.OnReminderDismissed(memoid);
        break;
    //某条的「10 分钟后再提醒」
    case kActionSnooze:
        center.OnReminderSnoozed(memoid);
        break;
    //点了标题：打开这条备忘
    case kActionOpen:
        center.OpenFromReminder(memoid);
        break;
    //错过的提醒那一行的「知道了」
    case kActionMissed:
        center.OnReminderDismissed(0);
        break;
    //「全部知道了」：先复制一份，处理过程中弹窗会重建、m_allMemoIds 随之改变
    case kActionAll:
    {
        const std::vector<int64_t> ids = m_allMemoIds;
        for (size_t i = 0; i < ids.size(); ++i)
        {
            center.OnReminderDismissed(ids[i]);
        }
        center.OnReminderDismissed(0);
        break;
    }
    //没有待执行的动作（重复投递）
    default:
        break;
    }
    return 0;
}
