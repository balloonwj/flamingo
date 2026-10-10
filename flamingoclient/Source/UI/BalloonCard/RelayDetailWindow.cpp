/**
 *  接龙详情窗，见 RelayDetailWindow.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "RelayDetailWindow.h"

#include <time.h>   // time()：按本机时间判断是否已截止

#include "../../Common/Log/IULog.h"                        // LOG_INFO / LOG_ERROR
#include "../../net/IUSocket.h"                            // CIUSocket::JoinRelay / WithdrawRelay / CloseRelay
#include "../../net/Protocol.h"                            // cmd_*、error_code_*、relay_state
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiSeparator.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // DuiScrollView
#include "../BalloonChatWnd/ChatMessageList.h"             // PutTextToClipboard（复制为文本）
#include "../BalloonMain/TitleBarIcon.h"                   // CreateProgramIconBitmap（标题栏左侧图标）
#include "../BalloonMain/OverlayScrollBarGeometry.h"       // ComputeResizeBorderInsetPx
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

using namespace balloonwjui;

namespace {

// ---- 窗口尺寸（逻辑像素）----
const int kWindowWidth     = 440;   // 初始宽
const int kWindowHeight    = 620;   // 初始高
const int kWindowMinWidth  = 380;   // 最小宽：再窄输入框与按钮挤不下
const int kWindowMinHeight = 460;   // 最小高：至少能看到几条名单

// ---- 标题栏：透明、穿透显示客户区白底，与聊天记录查看窗一致 ----
const int      kTitleBarHeight = 32;
const COLORREF kCaptionColor   = RGB(0x55, 0x55, 0x55);
const int      kTitleIconSize  = 16;
const COLORREF kTitleBarBg     = RGB(0xFF, 0xFF, 0xFF);

// ---- 颜色 ----
const COLORREF kColorWhite    = RGB(0xFF, 0xFF, 0xFF);
const COLORREF kColorText     = RGB(0x2A, 0x2A, 0x2A);   // 标题、名单
const COLORREF kColorGray     = RGB(0x8A, 0x8A, 0x8A);   // 说明、状态行、空名单、「已复制」
const COLORREF kColorSelf     = RGB(0xFF, 0x7A, 0x45);   // 本人那一条（主题橙）
const COLORREF kColorError    = RGB(0xE5, 0x39, 0x35);   // 错误提示
const COLORREF kColorSepLine  = RGB(0xEC, 0xEA, 0xE6);   // 分隔线

// ---- 版式 ----
const int kPadH           = 20;   // 客户区左右内边距
const int kPadTop         = 4;    // 客户区上内边距（标题栏下方）
const int kPadBottom      = 16;   // 客户区下内边距
const int kRootGap        = 6;    // 各行之间的间距
const int kTitlePt        = 12;   // 标题字号（磅）
const int kTitleH         = 28;   // 标题行高
const int kNoteMinH       = 18;   // 说明的最小高度（一行）
const int kStatusH        = 20;   // 状态行高
const int kEntryMinH      = 24;   // 名单每条的最小高度（一行）
const int kEntryGap       = 2;    // 名单条目之间的间距
const int kHintH          = 20;   // 输入区上方说明的行高
const int kInputRowH      = 34;   // 输入框与提交按钮这一行的高
const int kInputGap       = 8;    // 输入框与提交按钮之间的间距
const int kSubmitW        = 72;   // 「提交 / 修改」按钮宽
const int kErrorH         = 20;   // 错误提示行高
const int kBottomRowH     = 32;   // 底部按钮行高
const int kBottomGap      = 8;    // 底部按钮之间的间距
const int kWithdrawW      = 88;   // 「撤回我的」宽
const int kCloseRelayW    = 88;   // 「结束接龙」宽
const int kCopyW          = 96;   // 「复制为文本」宽

// ---- 控件 id：取值与不能使用 1~3 的原因见 RelayDetailModel.h 的 RelayDetailCtrlId ----
const UINT kIdInput      = ballooncard::kRelayDetailIdInput;        // 本人那一条的输入框
const UINT kIdSubmit     = ballooncard::kRelayDetailIdSubmit;       // 「提交 / 修改」
const UINT kIdWithdraw   = ballooncard::kRelayDetailIdWithdraw;     // 「撤回我的」
const UINT kIdCloseRelay = ballooncard::kRelayDetailIdCloseRelay;   // 「结束接龙」
const UINT kIdCopy       = ballooncard::kRelayDetailIdCopy;         // 「复制为文本」

// 截止定时器 id、最短与最长等待（毫秒），口径与群聊窗的接龙截止定时器相同
const UINT_PTR  kExpiryTimerId    = 1;
const long long kExpiryMinDelayMs = 1000;
const long long kExpiryMaxDelayMs = 3600LL * 1000;

// Unix 毫秒与秒的换算
const int64_t kMsPerSecond = 1000;

// 窗口标题与提示框标题
const wchar_t* const kWindowTitle = TR_NOOP(L"接龙详情");

// 当前时间，Unix 毫秒
int64_t NowMs()
{
    return (int64_t)::time(NULL) * kMsPerSecond;
}

// CString(UTF-16) → UTF-8。空串安全
std::string CStrToUtf8(const CString& text)
{
    const int wlen = text.GetLength();
    if (wlen <= 0)
    {
        return std::string();
    }
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)text, wlen, NULL, 0, NULL, NULL);
    if (len <= 0)
    {
        return std::string();
    }
    std::string out((size_t)len, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)text, wlen, &out[0], len, NULL, NULL);
    return out;
}

// UTF-8 → CString(UTF-16)。空串安全
CString Utf8ToCStr(const std::string& utf8)
{
    if (utf8.empty())
    {
        return CString();
    }
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), NULL, 0);
    if (len <= 0)
    {
        return CString();
    }
    CString out;
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), out.GetBuffer(len), len);
    out.ReleaseBuffer(len);
    return out;
}

// 造一个单行标签
std::unique_ptr<DuiLabel> MakeLabel(COLORREF color)
{
    std::unique_ptr<DuiLabel> label(new DuiLabel());
    label->SetTextColor(color);
    label->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    return label;
}

// 造一个按钮
std::unique_ptr<DuiButton> MakeButton(UINT id, LPCTSTR text, DuiButton::Variant variant)
{
    std::unique_ptr<DuiButton> button(new DuiButton());
    button->SetButtonType(DuiButton::StylePushButton);
    button->SetVariant(variant);
    button->SetCtrlId(id);
    button->SetText(text);
    return button;
}

// 造一条水平分隔线
std::unique_ptr<DuiControl> MakeSeparator()
{
    std::unique_ptr<DuiSeparator> sep(new DuiSeparator());
    sep->SetOrientation(DuiSeparator::Horizontal);
    sep->SetColor(kColorSepLine);
    return std::unique_ptr<DuiControl>(sep.release());
}

} // anonymous namespace

CRelayDetailWindow::CRelayDetailWindow()
    : m_selfId(0)
    , m_selfMuted(false)
    , m_pendingCmd(0)
    , m_rebuildPending(false)
    , m_pTitle(NULL)
    , m_pNote(NULL)
    , m_pStatus(NULL)
    , m_pScroll(NULL)
    , m_pHint(NULL)
    , m_pInputRow(NULL)
    , m_pInput(NULL)
    , m_pSubmit(NULL)
    , m_pError(NULL)
    , m_pWithdraw(NULL)
    , m_pCloseRelay(NULL)
    , m_ownerHwnd(NULL)
    , m_hTitleIcon(NULL)
{
}

CRelayDetailWindow::~CRelayDetailWindow()
{
    if (m_hTitleIcon != NULL)
    {
        ::DeleteObject(m_hTitleIcon);
        m_hTitleIcon = NULL;
    }
}

int64_t CRelayDetailWindow::CardMsgid() const
{
    return m_request.m_msgid;
}

bool CRelayDetailWindow::Open(const ballooncard::RelayDetailRequest& request, int64_t selfId,
                              std::unique_ptr<balloonchatwnd::IRelayNameResolver> names)
{
    net::RelayCardContent card;
    if (!names || !net::CardProtocolClient::parseRelayCardContent(request.m_contentJson, card))
    {
        LOG_ERROR("relay detail window: content not parsable or no name resolver, msgid=%lld, length=%u.",
                  (long long)request.m_msgid, (unsigned)request.m_contentJson.size());
        return false;
    }

    //换了一张卡片（或同一张卡片重新打开）：上一次的等待状态、禁言结论都不再沿用
    const bool sameCard = (m_request.m_msgid == request.m_msgid);
    if (!sameCard)
    {
        m_selfMuted = false;
        m_pendingCmd = 0;
    }
    m_request = request;
    m_request.m_contentJson.clear();   //内容以 m_card 为准，这里不再保留一份
    m_card    = card;
    m_selfId  = selfId;
    m_names   = std::move(names);

    if (!IsWindow())
    {
        m_ownerHwnd = ::IsWindow(request.m_ownerHwnd) ? request.m_ownerHwnd : NULL;
        if (!BuildWindow(m_ownerHwnd))
        {
            return false;
        }
    }
    ShowError(CString());
    //放在 ShowWindow 之前：群聊窗可点与本窗口已弹出之间不留空隙；已禁用着时内部不重复禁用
    DisableOwner();
    ShowWindow(SW_SHOW);
    ApplyView(false);
    ::SetForegroundWindow(m_hWnd);

    if (request.m_focusInput && m_pInput != NULL && m_view.m_inputEnabled)
    {
        m_pInput->SetFocus();
    }
    LOG_INFO("relay detail window opened: hwnd=0x%p, ctype=%d, cid=%lld, msgid=%lld, items=%u, state=%d, focus=%d, "
             "owner=0x%p, owner disabled=%d.",
             m_hWnd, m_request.m_ctype, (long long)m_request.m_cid, (long long)m_request.m_msgid,
             (unsigned)m_card.m_items.size(), m_view.m_state, request.m_focusInput ? 1 : 0,
             m_ownerHwnd, (int)m_ownerGuard.IsHostDisabled());
    return true;
}

bool CRelayDetailWindow::BuildWindow(HWND owner)
{
    //以群聊窗为 owner：本窗口在 z 序上恒在群聊窗之上，群聊窗销毁时本窗口被系统一并销毁（主面板
    //查找详情窗时会清掉窗口已销毁的对象）。控件通知仍回到本窗口，理由见 CRelayComposeDlg::BuildWindow
    if (Create(owner, CWindow::rcDefault, TR(kWindowTitle), WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR("relay detail window: Create failed.");
        return false;
    }
    SetTitle(TR(kWindowTitle));
    SetTitleBarHeight(kTitleBarHeight);
    SetTitleBarTransparent(true);
    SetTitleTextColor(kCaptionColor);
    SetCaptionGlyphColor(kCaptionColor);
    //标题栏透明，穿透显示的是客户区根控件的白底，故程序图标按白底合成
    if (m_hTitleIcon == NULL)
    {
        m_hTitleIcon = balloonmain::CreateProgramIconBitmap(kTitleIconSize, kTitleBarBg);
    }
    if (m_hTitleIcon != NULL)
    {
        SetIcon(m_hTitleIcon);
    }
    //只留关闭按钮：名单可以滚动，不需要最大化；模态窗口带 owner、在任务栏上没有按钮，最小化后
    //只剩桌面角落的一小条，而群聊窗这时还处于禁用状态，所以也不提供最小化
    SetButtons(false, false, true);
    SetResizable(true);
    SetMinSize(kWindowMinWidth, kWindowMinHeight);

    BuildClient();
    ResizeClient(kWindowWidth, kWindowHeight);
    CenterWindow();
    return true;
}

void CRelayDetailWindow::BuildClient()
{
    typedef DuiLayout::Hint Hint;

    //右侧给窗口缩放边框让出位置，否则滚动条贴着窗口右缘，鼠标移上去只会变成缩放箭头
    const int inset = balloonmain::ComputeResizeBorderInsetPx(GetBorderPx(), GetDpi(), IsResizable());
    const int rightPad = kPadH + ((inset > 0) ? inset : 0);

    std::unique_ptr<DuiVBox> root(new DuiVBox());
    root->SetBgColor(kColorWhite);
    root->SetPadding(kPadH, kPadTop, rightPad, kPadBottom);
    root->SetGap(kRootGap);

    std::unique_ptr<DuiLabel> title = MakeLabel(kColorText);
    title->SetTextPointSize(kTitlePt, true);
    m_pTitle = title.get();
    root->AddChild(std::unique_ptr<DuiControl>(title.release()), Hint().Fixed(kTitleH));

    //说明可多行：按宽度折行，高度在 ApplyView 里量出来再设
    std::unique_ptr<DuiLabel> note = MakeLabel(kColorGray);
    note->SetWordWrap(true);
    m_pNote = note.get();
    root->AddChild(std::unique_ptr<DuiControl>(note.release()), Hint().Fixed(kNoteMinH));

    std::unique_ptr<DuiLabel> status = MakeLabel(kColorGray);
    m_pStatus = status.get();
    root->AddChild(std::unique_ptr<DuiControl>(status.release()), Hint().Fixed(kStatusH));

    root->AddChild(MakeSeparator(), Hint().Fixed(1));

    //完整名单：DuiScrollView 包一个竖排容器，每条一个可折行的标签，由 RebuildEntries 生成
    std::unique_ptr<DuiScrollView> scroll(new DuiScrollView());
    scroll->SetAutoContentHeight(true);
    // 滚动条用 balloonui 统一的悬浮式细滑块与默认命中带宽（2026-10-04 起不再单独设宽）
    m_pScroll = scroll.get();
    root->AddChild(std::unique_ptr<DuiControl>(scroll.release()), Hint().Weight(1));

    root->AddChild(MakeSeparator(), Hint().Fixed(1));

    std::unique_ptr<DuiLabel> hint = MakeLabel(kColorGray);
    m_pHint = hint.get();
    root->AddChild(std::unique_ptr<DuiControl>(hint.release()), Hint().Fixed(kHintH));

    //输入框 + 提交按钮
    std::unique_ptr<DuiHBox> inputRow(new DuiHBox());
    inputRow->SetGap(kInputGap);
    std::unique_ptr<DuiEdit> input(new DuiEdit());
    input->SetCtrlId(kIdInput);
    input->SetPlaceholder(TR(L"填写你的接龙内容"));
    input->SetBgColor(kColorWhite);
    input->SetMaxLength((int)net::kRelayTextMaxChars);
    m_pInput = input.get();
    inputRow->AddChild(std::unique_ptr<DuiControl>(input.release()), Hint().Weight(1));
    std::unique_ptr<DuiButton> submit = MakeButton(kIdSubmit, TR(L"提交"), DuiButton::Variant::Primary);
    m_pSubmit = submit.get();
    inputRow->AddChild(std::unique_ptr<DuiControl>(submit.release()), Hint().Fixed(kSubmitW));
    m_pInputRow = inputRow.get();
    root->AddChild(std::unique_ptr<DuiControl>(inputRow.release()), Hint().Fixed(kInputRowH));

    std::unique_ptr<DuiLabel> error = MakeLabel(kColorError);
    m_pError = error.get();
    root->AddChild(std::unique_ptr<DuiControl>(error.release()), Hint().Fixed(kErrorH));

    //底部按钮：撤回我的、结束接龙靠左，复制为文本靠右
    std::unique_ptr<DuiHBox> bottom(new DuiHBox());
    bottom->SetGap(kBottomGap);
    std::unique_ptr<DuiButton> withdraw = MakeButton(kIdWithdraw, TR(L"撤回我的"), DuiButton::Variant::Default);
    m_pWithdraw = withdraw.get();
    bottom->AddChild(std::unique_ptr<DuiControl>(withdraw.release()), Hint().Fixed(kWithdrawW));
    std::unique_ptr<DuiButton> closeRelay = MakeButton(kIdCloseRelay, TR(L"结束接龙"), DuiButton::Variant::Danger);
    m_pCloseRelay = closeRelay.get();
    bottom->AddChild(std::unique_ptr<DuiControl>(closeRelay.release()), Hint().Fixed(kCloseRelayW));
    bottom->AddChild(std::unique_ptr<DuiControl>(new DuiLabel()), Hint().Weight(1));   //占位，把复制按钮推到右边
    bottom->AddChild(std::unique_ptr<DuiControl>(
        MakeButton(kIdCopy, TR(L"复制为文本"), DuiButton::Variant::Default).release()), Hint().Fixed(kCopyW));
    root->AddChild(std::unique_ptr<DuiControl>(bottom.release()), Hint().Fixed(kBottomRowH));

    SetClientContent(std::move(root));
}

void CRelayDetailWindow::ApplyView(bool keepInput)
{
    if (!IsWindow() || !m_names)
    {
        return;
    }
    const std::string oldSelfText = m_view.m_selfText;
    ballooncard::BuildRelayDetailView(m_card, m_request.m_creatorId, m_selfId, m_request.m_selfRole,
                                      m_selfMuted, NowMs(), *m_names, m_view);

    m_pTitle->SetText(m_view.m_title);
    m_pNote->SetText(m_view.m_note);
    m_pNote->SetVisible(!m_view.m_note.IsEmpty());
    m_pStatus->SetText(m_view.m_statusText);
    m_pHint->SetText(m_view.m_inputHint);
    m_pHint->SetVisible(!m_view.m_inputHint.IsEmpty());

    //输入区：进行中才显示；被禁言、名额已满时置灰；等应答期间按钮置灰
    const bool waiting = (m_pendingCmd != 0);
    m_pInputRow->SetVisible(m_view.m_showInput);
    m_pInput->SetEnabled(m_view.m_inputEnabled);
    m_pSubmit->SetText(m_view.m_submitLabel);
    m_pSubmit->SetEnabled(m_view.m_inputEnabled && !waiting);
    m_pWithdraw->SetVisible(m_view.m_showWithdraw);
    m_pWithdraw->SetEnabled(!waiting);
    m_pCloseRelay->SetVisible(m_view.m_showClose);
    m_pCloseRelay->SetEnabled(!waiting);

    //输入框文字：打开时预填本人那一条；之后只在用户没改过（还等于原来那条）时跟着推送更新，
    //免得把正在输入的内容冲掉
    const CString current = m_pInput->GetText();
    if (!keepInput || CStrToUtf8(current) == oldSelfText)
    {
        m_pInput->SetTextNoNotify(Utf8ToCStr(m_view.m_selfText));
    }

    //显隐变化之后要自己触发重排（开发约定文档 UI 约定第 5 条）：根容器排一次，底部按钮行的
    //矩形没变、根容器排版时不会往下排，要单独再排一次
    DuiControl* root = GetClientContent();
    if (root != NULL)
    {
        root->ForceLayout(root->GetRect());
        FitLabelHeight(m_pNote, kNoteMinH);
        if (m_pWithdraw->GetParent() != NULL)
        {
            m_pWithdraw->GetParent()->ForceLayout(m_pWithdraw->GetParent()->GetRect());
        }
        root->Invalidate();
    }
    RebuildEntries();
    ScheduleExpiryTimer();
}

void CRelayDetailWindow::FitLabelHeight(DuiLabel* label, int minHeight)
{
    if (label == NULL || !label->IsVisible())
    {
        return;
    }
    DuiLayout* parent = dynamic_cast<DuiLayout*>(label->GetParent());
    const RECT rc = label->GetRect();
    const int width = rc.right - rc.left;
    if (parent == NULL || width <= 0)
    {
        return;
    }
    int height = label->MeasureHeight(width);
    if (height < minHeight)
    {
        height = minHeight;
    }
    DuiLayout::Hint hint = parent->GetHint(label);
    if (hint.fixedMain != height)
    {
        hint.fixedMain = height;
        parent->SetHint(label, hint);
        parent->ForceLayout(parent->GetRect());
    }
}

void CRelayDetailWindow::RebuildEntries()
{
    if (m_pScroll == NULL)
    {
        return;
    }
    typedef DuiLayout::Hint Hint;

    std::unique_ptr<DuiVBox> list(new DuiVBox());
    list->SetGap(kEntryGap);
    std::vector<DuiLabel*> labels;
    if (m_view.m_entries.empty())
    {
        std::unique_ptr<DuiLabel> empty = MakeLabel(kColorGray);
        empty->SetText(TR(L"还没有人参与"));
        list->AddChild(std::unique_ptr<DuiControl>(empty.release()), Hint().Fixed(kEntryMinH));
    }
    for (size_t i = 0; i < m_view.m_entries.size(); ++i)
    {
        const ballooncard::RelayDetailEntry& entry = m_view.m_entries[i];
        std::unique_ptr<DuiLabel> label = MakeLabel(entry.m_isSelf ? kColorSelf : kColorText);
        label->SetWordWrap(true);
        label->SetText(entry.m_text);
        //名单里的内容可能要复制，允许拖选
        label->SetSelectable(true);
        labels.push_back(label.get());
        list->AddChild(std::unique_ptr<DuiControl>(label.release()), Hint().Fixed(kEntryMinH));
    }

    //先按一行高排一次版，各标签拿到实际宽度后再量折行高度。滚动条占用的宽度一并让出，
    //所以先算好内容宽度再排
    const int keepPos = m_pScroll->GetScrollPos();
    m_pScroll->SetContent(std::unique_ptr<DuiControl>(list.release()));
    m_pScroll->ForceLayout(m_pScroll->GetRect());
    for (size_t i = 0; i < labels.size(); ++i)
    {
        DuiLayout* parent = dynamic_cast<DuiLayout*>(labels[i]->GetParent());
        const RECT rc = labels[i]->GetRect();
        // 滚动条为悬浮式、不占内容宽度（2026-10-04 起），按标签的整宽测量
        const int width = rc.right - rc.left;
        if (parent == NULL || width <= 0)
        {
            continue;
        }
        int height = labels[i]->MeasureHeight(width);
        if (height < kEntryMinH)
        {
            height = kEntryMinH;
        }
        if (height != kEntryMinH)
        {
            DuiLayout::Hint hint = parent->GetHint(labels[i]);
            hint.fixedMain = height;
            parent->SetHint(labels[i], hint);
        }
    }
    m_pScroll->ForceLayout(m_pScroll->GetRect());
    m_pScroll->SetScrollPos(keepPos);
    m_pScroll->Invalidate();
}

void CRelayDetailWindow::OnRelayChanged(const net::RelayChangedNotify& notify)
{
    if ((int64_t)notify.m_msgid != m_request.m_msgid)
    {
        return;
    }
    balloonchatwnd::ApplyRelayNotify(m_card, notify);
    //推送到达说明本窗口发出的请求已经生效（或被别人的操作抢先），等待状态不再需要
    m_pendingCmd = 0;
    ApplyView(true);
    LOG_INFO("relay detail window refreshed by notify: msgid=%lld, items=%u, state=%d.",
             (long long)m_request.m_msgid, (unsigned)m_card.m_items.size(), m_card.m_state);
}

void CRelayDetailWindow::OnOpResult(int cmd, int code)
{
    m_pendingCmd = 0;
    if (code == 0)
    {
        ShowError(CString());
    }
    else
    {
        //被禁言：之后输入区一直置灰，直到窗口关掉重开。原因已由输入区上方的说明写出，
        //下方不再重复写一遍
        if (code == error_code_member_muted)
        {
            m_selfMuted = true;
            ShowError(CString());
        }
        else
        {
            ShowError(balloonchatwnd::RelayOpErrorText(cmd, code));
        }
    }
    ApplyView(true);
    LOG_INFO("relay detail window op result: msgid=%lld, cmd=%d, code=%d.",
             (long long)m_request.m_msgid, cmd, code);
}

void CRelayDetailWindow::OnSubmit()
{
    if (m_pInput == NULL || !m_view.m_inputEnabled || m_pendingCmd != 0)
    {
        return;
    }
    std::string text;
    const int check = ballooncard::CheckRelaySubmit(CStrToUtf8(m_pInput->GetText()), m_view.m_joined,
                                                    m_view.m_selfText, text);
    if (check != ballooncard::kRelaySubmitOk)
    {
        ShowError(ballooncard::RelaySubmitCheckText(check));
        return;
    }
    if (!CIUSocket::GetInstance().JoinRelay(m_request.m_ctype, m_request.m_cid, m_request.m_msgid, text))
    {
        ShowError(TR(L"网络未连接，请稍后重试。"));
        return;
    }
    m_pendingCmd = cmd_join_relay;
    ShowError(CString());
    ApplyView(true);
    LOG_INFO("relay detail window: join requested, msgid=%lld, joined=%d.",
             (long long)m_request.m_msgid, m_view.m_joined ? 1 : 0);
}

void CRelayDetailWindow::OnWithdraw()
{
    if (!m_view.m_showWithdraw || m_pendingCmd != 0)
    {
        return;
    }
    //撤回会丢掉名次，重新参与要排到最后，先确认
    if (::MessageBox(m_hWnd, TR(L"撤回后名次不再保留，重新参与会排到最后。确定撤回你的内容吗？"),
                     TR(kWindowTitle), MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2) != IDOK)
    {
        return;
    }
    if (!CIUSocket::GetInstance().WithdrawRelay(m_request.m_ctype, m_request.m_cid, m_request.m_msgid))
    {
        ShowError(TR(L"网络未连接，请稍后重试。"));
        return;
    }
    m_pendingCmd = cmd_withdraw_relay;
    ShowError(CString());
    ApplyView(true);
    LOG_INFO("relay detail window: withdraw requested, msgid=%lld.", (long long)m_request.m_msgid);
}

void CRelayDetailWindow::OnCloseRelayClicked()
{
    if (!m_view.m_showClose || m_pendingCmd != 0)
    {
        return;
    }
    if (::MessageBox(m_hWnd, ballooncard::BuildRelayCloseConfirmText(m_card), TR(kWindowTitle),
                     MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
    {
        return;
    }
    if (!CIUSocket::GetInstance().CloseRelay(m_request.m_ctype, m_request.m_cid, m_request.m_msgid))
    {
        ShowError(TR(L"网络未连接，请稍后重试。"));
        return;
    }
    m_pendingCmd = cmd_close_relay;
    ShowError(CString());
    ApplyView(true);
    LOG_INFO("relay detail window: close requested, msgid=%lld.", (long long)m_request.m_msgid);
}

void CRelayDetailWindow::OnCopy()
{
    if (!m_names)
    {
        return;
    }
    const CString text = balloonchatwnd::BuildRelayCopyText(m_card, NowMs(), *m_names);
    balloonchatwnd::PutTextToClipboard(m_hWnd, (LPCWSTR)text);
    //借错误提示那一行给个回执，用灰色，与错误区分开
    if (m_pError != NULL)
    {
        m_pError->SetTextColor(kColorGray);
        m_pError->SetText(TR(L"已复制到剪贴板。"));
        m_pError->SetVisible(true);
        DuiControl* root = GetClientContent();
        if (root != NULL)
        {
            root->ForceLayout(root->GetRect());
            root->Invalidate();
        }
    }
}

void CRelayDetailWindow::ShowError(const CString& text)
{
    if (m_pError == NULL)
    {
        return;
    }
    m_pError->SetTextColor(kColorError);
    m_pError->SetText(text);
    const bool visible = !text.IsEmpty();
    if (m_pError->IsVisible() != visible)
    {
        m_pError->SetVisible(visible);
        DuiControl* root = GetClientContent();
        if (root != NULL)
        {
            root->ForceLayout(root->GetRect());
            root->Invalidate();
        }
    }
}

void CRelayDetailWindow::ScheduleExpiryTimer()
{
    if (!IsWindow())
    {
        return;
    }
    if (m_view.m_state != relay_state_open || m_card.m_deadline <= 0)
    {
        KillTimer(kExpiryTimerId);
        return;
    }
    long long delay = (long long)m_card.m_deadline - (long long)NowMs();
    if (delay < kExpiryMinDelayMs)
    {
        delay = kExpiryMinDelayMs;
    }
    if (delay > kExpiryMaxDelayMs)
    {
        delay = kExpiryMaxDelayMs;
    }
    SetTimer(kExpiryTimerId, (UINT)delay);
}

LRESULT CRelayDetailWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const DuiNotify* pNotify = reinterpret_cast<const DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }

    //输入框里按回车：等同点「提交 / 修改」。按开发约定文档 UI 约定第 4 条，ctrlId 写进分支条件
    if (pNotify->code == (UINT)DuiEdit::DUIN_EDIT_ENTER && pNotify->ctrlId == kIdInput)
    {
        OnSubmit();
        return 0;
    }

    if (pNotify->code == DUIN_CLICK)
    {
        switch (pNotify->ctrlId)
        {
        //「提交 / 修改」
        case kIdSubmit:
            OnSubmit();
            return 0;

        //「撤回我的」
        case kIdWithdraw:
            OnWithdraw();
            return 0;

        //「结束接龙」
        case kIdCloseRelay:
            OnCloseRelayClicked();
            return 0;

        //「复制为文本」
        case kIdCopy:
            OnCopy();
            return 0;

        //其它控件的点击：交给框架
        default:
            break;
        }
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CRelayDetailWindow::OnTimerMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    if (wParam != kExpiryTimerId)
    {
        bHandled = FALSE;
        return 0;
    }
    //截止时间到了（或到了每小时一次的复查）：按当前时间重画，到期的就显示为已截止
    KillTimer(kExpiryTimerId);
    ApplyView(true);
    return 0;
}

LRESULT CRelayDetailWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    if (IsWindow())
    {
        KillTimer(kExpiryTimerId);
        RestoreOwner();
        DestroyWindow();
    }
    return 0;
}

LRESULT CRelayDetailWindow::OnDestroyMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    //主面板退出时会直接 DestroyWindow 本窗口，不经过 WM_CLOSE，在这里兜底恢复（已恢复过时不做事）
    RestoreOwner();
    bHandled = FALSE;
    return 0;
}

void CRelayDetailWindow::DisableOwner()
{
    const bool ownerIsWindow  = (m_ownerHwnd != NULL) && (::IsWindow(m_ownerHwnd) != FALSE);
    const bool ownerIsEnabled = ownerIsWindow && (::IsWindowEnabled(m_ownerHwnd) != FALSE);
    if (m_ownerGuard.OnOpen(ownerIsWindow, ownerIsEnabled))
    {
        ::EnableWindow(m_ownerHwnd, FALSE);
    }
}

void CRelayDetailWindow::RestoreOwner()
{
    const bool ownerIsWindow = (m_ownerHwnd != NULL) && (::IsWindow(m_ownerHwnd) != FALSE);
    if (!m_ownerGuard.OnClose(ownerIsWindow))
    {
        return;
    }
    ::EnableWindow(m_ownerHwnd, TRUE);
    LOG_INFO("relay detail window: owner restored, msgid=%lld, owner=0x%p.",
             (long long)m_request.m_msgid, m_ownerHwnd);
}

LRESULT CRelayDetailWindow::OnSizeMsg(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    //最小化时不必重排；同一轮拖动里只投一条，处理时再按最新宽度折行
    if (wParam != SIZE_MINIMIZED && !m_rebuildPending)
    {
        m_rebuildPending = true;
        PostMessage(WM_RELAY_DETAIL_REBUILD, 0, 0);
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CRelayDetailWindow::OnRebuildMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    m_rebuildPending = false;
    DuiControl* root = GetClientContent();
    if (root != NULL)
    {
        FitLabelHeight(m_pNote, kNoteMinH);
    }
    RebuildEntries();
    return 0;
}
