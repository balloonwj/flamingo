/**
 *  「发起接龙」窗口，见 RelayComposeDlg.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "RelayComposeDlg.h"

#include <objbase.h>   // CoCreateGuid（生成 clientmsgid）
#include <stdio.h>     // sprintf_s
#include <time.h>      // time() / localtime_s：截止时间的初始值与年份范围按当前时刻计算

#include "../../Common/Log/IULog.h"                        // LOG_INFO / LOG_ERROR
#include "../../net/IUSocket.h"                            // CIUSocket::SendRelayCard / JoinRelay
#include "../../net/CardProtocolClient.h"                  // 标题、说明、每条接龙的长度上限
#include "../../net/Protocol.h"                            // conversation_group
#include "RelayComposeLayout.h"                           // BuildRelayComposeBody / 窗口尺寸 / 控件 id
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSpinBox.h"
#include "../BalloonMain/TitleBarIcon.h"                   // CreateProgramIconBitmap（标题栏左侧图标）
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

#pragma comment(lib, "ole32.lib")     // CoCreateGuid

using namespace balloonwjui;

namespace {

// ---- 标题栏：透明、穿透显示客户区白底，与接龙详情窗一致。窗口尺寸与标题栏高度见 RelayComposeLayout.h ----
const COLORREF kCaptionColor   = RGB(0x55, 0x55, 0x55);
const int      kTitleIconSize  = 16;
const COLORREF kTitleBarBg     = RGB(0xFF, 0xFF, 0xFF);

// Unix 毫秒与秒的换算
const int64_t kMsPerSecond = 1000;

// 截止时间的年份最多填到今年之后几年（年微调框的上限）
const int kDeadlineYearsAhead = 10;
// struct tm 的 tm_year 从 1900 年起算
const int kTmYearBase = 1900;

// 当前时间，Unix 毫秒
int64_t NowMs()
{
    return (int64_t)::time(NULL) * kMsPerSecond;
}

// 把微调框输入框里手输的数字写回微调框（SetValue 会限制在范围内），返回最终的值
int CommitTypedValue(DuiSpinBox* spin)
{
    if (spin == NULL)
    {
        return 0;
    }
    //输入框为空或不是数字时 _ttoi 返回 0，同样被限制到下限
    if (spin->GetEdit() != NULL)
    {
        spin->SetValue(_ttoi(spin->GetEdit()->GetText()), false);
    }
    return spin->GetValue();
}

// 窗口标题
const wchar_t* const kWindowTitle = TR_NOOP(L"发起接龙");

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

// 生成一个客户端消息ID（去横杠的 GUID 十六进制串，纯 ASCII），与群聊窗发消息时的写法相同
std::string GenClientMsgId()
{
    GUID guid = { 0 };
    if (::CoCreateGuid(&guid) != S_OK)
    {
        return std::string("cmid");
    }
    char buf[40] = { 0 };
    sprintf_s(buf, sizeof(buf), "%08lX%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X",
              guid.Data1, guid.Data2, guid.Data3,
              guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
              guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return std::string(buf);
}

} // anonymous namespace

CRelayComposeDlg::CRelayComposeDlg()
    : m_groupId(0)
    , m_ownerHwnd(NULL)
    , m_pTitle(NULL)
    , m_pNote(NULL)
    , m_pDeadlineYear(NULL)
    , m_pDeadlineMonth(NULL)
    , m_pDeadlineDay(NULL)
    , m_pDeadlineHour(NULL)
    , m_pDeadlineMinute(NULL)
    , m_pDeadlineSecond(NULL)
    , m_pEntry(NULL)
    , m_pError(NULL)
    , m_pSubmit(NULL)
    , m_hTitleIcon(NULL)
{
}

CRelayComposeDlg::~CRelayComposeDlg()
{
    if (m_hTitleIcon != NULL)
    {
        ::DeleteObject(m_hTitleIcon);
        m_hTitleIcon = NULL;
    }
}

bool CRelayComposeDlg::Open(HWND owner, int64_t groupId)
{
    if (groupId <= 0)
    {
        return false;
    }
    //已经开着：同一个群就调到前台、保留已填写的内容
    if (IsWindow() && groupId == m_groupId)
    {
        ShowWindow(SW_SHOWNORMAL);
        ::SetForegroundWindow(m_hWnd);
        return true;
    }
    if (IsWindow())
    {
        RestoreOwner();
        DestroyWindow();
    }
    m_groupId = groupId;
    m_ownerHwnd = ::IsWindow(owner) ? owner : NULL;
    if (!BuildWindow(m_ownerHwnd))
    {
        return false;
    }
    FillDefaultDeadline();
    ShowError(CString());
    SetSending(false);
    if (m_ownerHwnd != NULL)
    {
        CenterWindow(m_ownerHwnd);
    }
    //放在 ShowWindow 之前：群聊窗可点与本窗口已弹出之间不留空隙
    DisableOwner();
    ShowWindow(SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
    if (m_pTitle != NULL)
    {
        m_pTitle->SetFocus();
    }
    LOG_INFO("relay compose dialog opened: group=%lld, owner=0x%p, owner disabled=%d.",
             (long long)m_groupId, m_ownerHwnd, (int)m_ownerGuard.IsHostDisabled());
    return true;
}

bool CRelayComposeDlg::BuildWindow(HWND owner)
{
    //以群聊窗为 owner：本窗口在 z 序上恒在群聊窗之上，群聊窗销毁时本窗口被系统一并销毁。
    //控件通知仍回到本窗口：DuiHost 用 ::GetParent 找投递目标，WS_OVERLAPPEDWINDOW 的顶层窗口
    //即便带 owner，::GetParent 也返回 NULL（见 CBalloonClosePromptDlg 的说明与用例 CPD15）
    if (Create(owner, CWindow::rcDefault, TR(kWindowTitle), WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR("relay compose dialog: Create failed.");
        return false;
    }
    SetTitle(TR(kWindowTitle));
    SetTitleBarHeight(ballooncard::kRelayComposeTitleBarH);
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
    SetButtons(false, false, true);   //只留关闭按钮
    SetResizable(false);

    BuildClient();
    //整窗尺寸（含标题栏），见 RelayComposeLayout.h；布局单测 RCL1 校验按这个尺寸排得下
    ResizeClient(ballooncard::kRelayComposeWindowWidth, ballooncard::kRelayComposeWindowHeight);
    return true;
}

void CRelayComposeDlg::BuildClient()
{
    SetClientContent(ballooncard::BuildRelayComposeBody());
    DuiControl* root = GetClientContent();
    if (root == NULL)
    {
        return;
    }
    m_pTitle    = dynamic_cast<DuiEdit*>(root->FindCtrlById(ballooncard::kRelayComposeIdTitle));
    m_pNote     = dynamic_cast<DuiEdit*>(root->FindCtrlById(ballooncard::kRelayComposeIdNote));
    m_pDeadlineYear   = dynamic_cast<DuiSpinBox*>(root->FindCtrlById(ballooncard::kRelayComposeIdDeadlineYear));
    m_pDeadlineMonth  = dynamic_cast<DuiSpinBox*>(root->FindCtrlById(ballooncard::kRelayComposeIdDeadlineMonth));
    m_pDeadlineDay    = dynamic_cast<DuiSpinBox*>(root->FindCtrlById(ballooncard::kRelayComposeIdDeadlineDay));
    m_pDeadlineHour   = dynamic_cast<DuiSpinBox*>(root->FindCtrlById(ballooncard::kRelayComposeIdDeadlineHour));
    m_pDeadlineMinute = dynamic_cast<DuiSpinBox*>(root->FindCtrlById(ballooncard::kRelayComposeIdDeadlineMinute));
    m_pDeadlineSecond = dynamic_cast<DuiSpinBox*>(root->FindCtrlById(ballooncard::kRelayComposeIdDeadlineSecond));
    m_pEntry    = dynamic_cast<DuiEdit*>(root->FindCtrlById(ballooncard::kRelayComposeIdEntry));
    m_pError    = dynamic_cast<DuiLabel*>(root->FindCtrlById(ballooncard::kRelayComposeIdError));
    m_pSubmit   = dynamic_cast<DuiButton*>(root->FindCtrlById(ballooncard::kRelayComposeIdSubmit));
}

void CRelayComposeDlg::FillDefaultDeadline()
{
    if (m_pDeadlineYear == NULL || m_pDeadlineMonth == NULL || m_pDeadlineDay == NULL
        || m_pDeadlineHour == NULL || m_pDeadlineMinute == NULL || m_pDeadlineSecond == NULL)
    {
        return;
    }
    const int64_t nowMs = NowMs();
    const time_t nowSecs = (time_t)(nowMs / kMsPerSecond);
    struct tm nowTm;
    const ballooncard::RelayDeadlineFields f = ballooncard::DefaultRelayDeadlineFields(nowMs);
    if (localtime_s(&nowTm, &nowSecs) == 0)
    {
        //年份从今年起：截止时间不能早于当前时间，填更早的年份没有意义
        const int thisYear = nowTm.tm_year + kTmYearBase;
        m_pDeadlineYear->SetRange(thisYear, thisYear + kDeadlineYearsAhead);
    }
    m_pDeadlineYear->SetValue(f.m_year, false);
    m_pDeadlineMonth->SetValue(f.m_month, false);
    m_pDeadlineDay->SetValue(f.m_day, false);
    m_pDeadlineHour->SetValue(f.m_hour, false);
    m_pDeadlineMinute->SetValue(f.m_minute, false);
    m_pDeadlineSecond->SetValue(f.m_second, false);
}

ballooncard::RelayDeadlineFields CRelayComposeDlg::ReadDeadlineFields()
{
    ballooncard::RelayDeadlineFields f;
    f.m_year   = CommitTypedValue(m_pDeadlineYear);
    f.m_month  = CommitTypedValue(m_pDeadlineMonth);
    f.m_day    = CommitTypedValue(m_pDeadlineDay);
    f.m_hour   = CommitTypedValue(m_pDeadlineHour);
    f.m_minute = CommitTypedValue(m_pDeadlineMinute);
    f.m_second = CommitTypedValue(m_pDeadlineSecond);
    return f;
}

void CRelayComposeDlg::OnSubmit()
{
    if (m_pTitle == NULL || m_pNote == NULL || m_pEntry == NULL || m_pDeadlineYear == NULL)
    {
        return;
    }
    if (!m_pendingClientMsgId.empty() && m_pSubmit != NULL && !m_pSubmit->IsEnabled())
    {
        return;   //上一张还在等回执
    }

    //截止时间按微调框里填的年月日时分秒换算；「不早于当前时间 + 5 分钟」按点「发起」这一刻判断
    const ballooncard::RelayDeadlineFields fields = ReadDeadlineFields();
    const int64_t deadlineMs = ballooncard::RelayDeadlineFieldsToMs(fields);
    const int64_t nowMs = NowMs();

    std::string content;
    std::string entry;
    const int check = ballooncard::CheckRelayCompose(CStrToUtf8(m_pTitle->GetText()), CStrToUtf8(m_pNote->GetText()),
                                                     deadlineMs, nowMs, CStrToUtf8(m_pEntry->GetText()),
                                                     content, entry);
    if (check != ballooncard::kRelayComposeOk)
    {
        LOG_INFO("relay compose check failed: group=%lld, check=%d, deadline=%04d-%02d-%02d %02d:%02d:%02d.",
                 (long long)m_groupId, check, fields.m_year, fields.m_month, fields.m_day,
                 fields.m_hour, fields.m_minute, fields.m_second);
        ShowError(ballooncard::RelayComposeCheckText(check));
        return;
    }

    const std::string clientMsgId = GenClientMsgId();
    if (!CIUSocket::GetInstance().SendRelayCard(conversation_group, m_groupId, clientMsgId, content))
    {
        ShowError(TR(L"网络未连接，请稍后重试。"));
        return;
    }
    m_pendingClientMsgId = clientMsgId;
    m_pendingEntry = entry;
    ShowError(CString());
    SetSending(true);
    LOG_INFO("relay card sent: group=%lld, clientmsgid=%s, deadline=%lld, with entry=%d.",
             (long long)m_groupId, clientMsgId.c_str(), (long long)deadlineMs, entry.empty() ? 0 : 1);
}

bool CRelayComposeDlg::OnSendAck(const std::string& clientMsgId, int code, int64_t msgid)
{
    if (m_pendingClientMsgId.empty() || clientMsgId != m_pendingClientMsgId)
    {
        return false;
    }
    const std::string entry = m_pendingEntry;
    m_pendingClientMsgId.clear();
    m_pendingEntry.clear();

    if (code != 0)
    {
        LOG_ERROR("relay card send failed: group=%lld, clientmsgid=%s, code=%d.",
                  (long long)m_groupId, clientMsgId.c_str(), code);
        if (IsWindow())
        {
            ShowError(ballooncard::RelaySendFailureText(code));
            SetSending(false);
        }
        return true;
    }

    //卡片已发出：填了「我的接龙」就补发一次参与，发起人因此排在第 1 位。参与的结果走接龙操作
    //应答的通用路径，失败时由主面板提示
    if (!entry.empty() && msgid > 0)
    {
        const bool queued = CIUSocket::GetInstance().JoinRelay(conversation_group, m_groupId, msgid, entry);
        LOG_INFO("relay card sent ok, creator entry %s: group=%lld, msgid=%lld.",
                 queued ? "requested" : "not queued", (long long)m_groupId, (long long)msgid);
    }
    else
    {
        LOG_INFO("relay card sent ok: group=%lld, msgid=%lld.", (long long)m_groupId, (long long)msgid);
    }
    if (IsWindow())
    {
        RestoreOwner();
        DestroyWindow();
    }
    return true;
}

void CRelayComposeDlg::DisableOwner()
{
    const bool ownerIsWindow  = (m_ownerHwnd != NULL) && (::IsWindow(m_ownerHwnd) != FALSE);
    const bool ownerIsEnabled = ownerIsWindow && (::IsWindowEnabled(m_ownerHwnd) != FALSE);
    if (m_ownerGuard.OnOpen(ownerIsWindow, ownerIsEnabled))
    {
        ::EnableWindow(m_ownerHwnd, FALSE);
    }
}

void CRelayComposeDlg::RestoreOwner()
{
    const bool ownerIsWindow = (m_ownerHwnd != NULL) && (::IsWindow(m_ownerHwnd) != FALSE);
    if (!m_ownerGuard.OnClose(ownerIsWindow))
    {
        return;
    }
    ::EnableWindow(m_ownerHwnd, TRUE);
    LOG_INFO("relay compose dialog: owner restored, group=%lld, owner=0x%p.", (long long)m_groupId, m_ownerHwnd);
}

void CRelayComposeDlg::ShowError(const CString& text)
{
    if (m_pError != NULL)
    {
        m_pError->SetText(text);
    }
}

void CRelayComposeDlg::SetSending(bool sending)
{
    if (m_pSubmit != NULL)
    {
        m_pSubmit->SetEnabled(!sending);
        m_pSubmit->SetText(sending ? TR(L"正在发起") : TR(L"发起"));
    }
}

LRESULT CRelayComposeDlg::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const DuiNotify* pNotify = reinterpret_cast<const DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }

    if (pNotify->code == DUIN_CLICK)
    {
        switch (pNotify->ctrlId)
        {
        //「发起」
        case ballooncard::kRelayComposeIdSubmit:
            OnSubmit();
            return 0;

        //「取消」：直接关闭，已发出等回执的那张照常处理（见 OnSendAck）
        case ballooncard::kRelayComposeIdCancel:
            PostMessage(WM_CLOSE);
            return 0;

        //其它控件的点击：交给框架
        default:
            break;
        }
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CRelayComposeDlg::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    if (IsWindow())
    {
        RestoreOwner();
        DestroyWindow();
    }
    return 0;
}

LRESULT CRelayComposeDlg::OnDestroyMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    //群聊窗关闭时会直接 DestroyWindow 本窗口，不经过 WM_CLOSE，在这里兜底恢复（已恢复过时不做事）
    RestoreOwner();
    bHandled = FALSE;
    return 0;
}
