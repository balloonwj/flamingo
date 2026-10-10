/**
 *  帖子群窗口右栏底部的留言框，见 ThreadComposer.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "ThreadComposer.h"

#include <memory>

#include "../../Source/Common/I18n/I18n.h"   // TR / TRF / TR_NOOP：界面文字的多语言查找
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/DuiHost.h"   // DuiHost：取输入框所在的窗口，把光标位置换算成屏幕坐标
#include "../Core/ThreadTypes.h"   // kCommentTextMaxChars
#include "ThreadWidgets.h"

using balloonwjui::DuiButton;
using balloonwjui::DuiControl;
using balloonwjui::DuiHBox;
using balloonwjui::DuiLabel;
using balloonwjui::DuiLayout;
using balloonwjui::DuiRichEdit;
using balloonwjui::DuiVBox;

namespace threadgroup {

namespace {

//---- 尺寸，单位：像素（样稿第 0 期） ----
const int kPadX = 14;               //留言框左右内边距
const int kPadTop = 8;              //留言框上内边距
const int kPadBottom = 10;          //留言框下内边距
const int kGap = 6;                 //相邻两部分之间的距离
const int kReplyBarH = 24;          //「回复 某某：」提示行高
const int kReplyBarRadius = 4;      //提示行圆角
const int kReplyBarPadX = 8;        //提示行左右内边距
const int kCancelWidth = 56;        //提示行「✕ 取消」宽
const int kEditHeight = 54;         //输入框高
const int kBottomRowH = 28;         //底部一行（出错说明、字数、发送）高
const int kSendPad = 16;            //「发送」文字两侧内边距
const int kCountGap = 12;           //字数与「发送」之间的距离
const int kEditMargin = 6;          //输入框文字与边框之间的内边距
const int kCaretLineHeight = 20;    //成员浮层的锚点比「@」所在位置低多少（约一行字高），单位：像素
const int kCharRangeBufChars = 2;   //按字符位置取一个字符时的缓冲区长度（一个字符加结尾 0）
const wchar_t kMentionSign = L'@';  //用来 @ 人的符号
const wchar_t kFullWidthSpace = 0x3000;   //全角空格（「@」前面是它时也算位于空白之后）

//---- 颜色 ----
const COLORREF kBg = RGB(0xFF, 0xFF, 0xFF);               //留言框底色
const COLORREF kReplyBarBg = RGB(0xF5, 0xF4, 0xF1);       //提示行底色
const COLORREF kReplyText = RGB(0x6B, 0x6B, 0x6B);        //提示行文字
const COLORREF kFaint = RGB(0x9A, 0x9A, 0x9A);            //字数
const COLORREF kErrorColor = RGB(0xD9, 0x53, 0x4F);       //出错说明、超出上限的字数
const COLORREF kEditText = RGB(0x1A, 0x1A, 0x1A);         //输入的文字

//---- 界面文字 ----
const wchar_t* const kReplyTo = TR_NOOP(L"回复 {0}：");
const wchar_t* const kCancelText = TR_NOOP(L"✕ 取消");
const wchar_t* const kSendText = TR_NOOP(L"发送");
const wchar_t* const kPlaceholder = TR_NOOP(L"写下你的留言…（Enter 发送，Ctrl+Enter 换行）");
const wchar_t* const kCountPattern = TR_NOOP(L"{0} / {1}");

} // namespace

//======================== CThreadComposer ========================

CThreadComposer::CThreadComposer()
    : m_edit(NULL)
    , m_replyBar(NULL)
    , m_replyText(NULL)
    , m_error(NULL)
    , m_count(NULL)
    , m_send(NULL)
    , m_replyTo(0)
    , m_lastLength(0)
    , m_canSpeak(true)
{
    SetBgColor(kBg);
    SetPadding(kPadX, kPadTop, kPadX, kPadBottom);
    SetGap(kGap);
}

void CThreadComposer::Build(HFONT font, HWND sendTarget, UINT sendMsg, WPARAM sendWParam)
{
    typedef DuiLayout::Hint Hint;

    //「回复 某某：  ✕ 取消」提示行（平时隐藏）
    std::unique_ptr<DuiHBox> bar(new DuiHBox());
    bar->SetPadding(kReplyBarPadX, 0, 0, 0);
    std::unique_ptr<DuiVBox> barBox(new DuiVBox());
    barBox->SetBgColor(kReplyBarBg);
    barBox->SetCornerRadius(kReplyBarRadius);
    std::unique_ptr<DuiLabel> replyText = MakeTextLabel(std::wstring(), kReplyText, font,
                                                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    m_replyText = replyText.get();
    bar->AddChild(std::unique_ptr<DuiControl>(replyText.release()), Hint().Weight(1));
    std::unique_ptr<DuiButton> cancel(new DuiButton());
    cancel->SetText(TR(kCancelText));
    cancel->SetVariant(DuiButton::Variant::Ghost);
    cancel->SetCtrlId(kIdComposerCancelReply);
    cancel->SetTabStop(false);
    bar->AddChild(std::unique_ptr<DuiControl>(cancel.release()), Hint().Fixed(kCancelWidth, kReplyBarH));
    barBox->AddChild(std::unique_ptr<DuiControl>(bar.release()), Hint().Weight(1));
    barBox->SetVisible(false);
    m_replyBar = static_cast<DuiHBox*>(barBox->Children()[0].get());
    AddChild(std::unique_ptr<DuiControl>(barBox.release()), Hint().Fixed(kReplyBarH));

    //输入框
    std::unique_ptr<CThreadCommentEdit> edit(new CThreadCommentEdit());
    edit->SetMultiLine(true);
    edit->SetWordWrap(true);
    edit->SetAutoGrow(false);
    edit->SetTextColor(kEditText);
    edit->SetMargins(kEditMargin, kEditMargin, kEditMargin, kEditMargin);
    edit->SetPlaceholder(TR(kPlaceholder));
    edit->SetCtrlId(kIdComposerEdit);
    //只接受纯文本粘贴：帖子群的留言只有文字与表情
    edit->SetPasteAsPlainTextDefault(true);
    if (font != NULL)
        edit->SetDefaultFontFromHFONT(font);
    edit->SetSendTarget(sendTarget, sendMsg, sendWParam);
    m_edit = edit.get();
    AddChild(std::unique_ptr<DuiControl>(edit.release()), Hint().Fixed(kEditHeight));

    //底部一行：出错说明 | 字数 | 发送
    std::unique_ptr<DuiHBox> row(new DuiHBox());
    std::unique_ptr<DuiLabel> error = MakeTextLabel(std::wstring(), kErrorColor, font,
                                                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    m_error = error.get();
    row->AddChild(std::unique_ptr<DuiControl>(error.release()), Hint().Weight(1));
    std::unique_ptr<DuiLabel> count = MakeTextLabel(std::wstring(), kFaint, font, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    m_count = count.get();
    const int countWidth = MeasureTextWidthPx(font, TRF(kCountPattern, kCommentTextMaxChars, kCommentTextMaxChars));
    row->AddChild(std::unique_ptr<DuiControl>(count.release()), Hint().Fixed(countWidth).Margin(0, 0, kCountGap, 0));
    std::unique_ptr<DuiButton> send(new DuiButton());
    const std::wstring sendText = TR(kSendText);
    send->SetText(sendText.c_str());
    send->SetVariant(DuiButton::Variant::Primary);
    send->SetCtrlId(kIdComposerSend);
    send->SetTabStop(false);
    m_send = send.get();
    row->AddChild(std::unique_ptr<DuiControl>(send.release()),
                  Hint().Fixed(MeasureTextWidthPx(font, sendText) + 2 * kSendPad, kBottomRowH));
    AddChild(std::unique_ptr<DuiControl>(row.release()), Hint().Fixed(kBottomRowH));
    FaceTable none;
    UpdateCount(none);
}

void CThreadComposer::SetCanSpeak(bool canSpeak, const std::wstring& reason)
{
    m_canSpeak = canSpeak;
    if (m_edit != NULL)
    {
        m_edit->SetReadOnly(!canSpeak);
        m_edit->SetPlaceholder(canSpeak ? TR(kPlaceholder) : reason.c_str());
        if (!canSpeak)
            m_edit->SetText(L"");
    }
    if (m_send != NULL)
        m_send->SetEnabled(canSpeak);
    if (!canSpeak)
        ClearReplyTarget();
}

void CThreadComposer::SetReplyTarget(int64_t commentId, const std::wstring& name)
{
    if (!m_canSpeak || m_replyBar == NULL || m_replyText == NULL)
        return;
    m_replyTo = commentId;
    m_replyText->SetText(TRF(kReplyTo, name).c_str());
    DuiControl* box = m_replyBar->GetParent();
    if (box != NULL && !box->IsVisible())
    {
        //提示行显示：SetVisible 不会重排，对本控件按当前矩形重排（UI 约定第 5 条）
        box->SetVisible(true);
        ForceLayout(GetRect());
    }
    if (m_edit != NULL)
        m_edit->SetFocus();
}

void CThreadComposer::ClearReplyTarget()
{
    m_replyTo = 0;
    DuiControl* box = (m_replyBar != NULL) ? m_replyBar->GetParent() : NULL;
    if (box != NULL && box->IsVisible())
    {
        box->SetVisible(false);
        ForceLayout(GetRect());
    }
}

int64_t CThreadComposer::ReplyTarget() const
{
    return m_replyTo;
}

std::wstring CThreadComposer::WireText(const FaceTable& faces) const
{
    return DisplayToWire(DisplayText(), faces);
}

std::wstring CThreadComposer::DisplayText() const
{
    return (m_edit != NULL) ? std::wstring(m_edit->GetText().GetString()) : std::wstring();
}

void CThreadComposer::Clear()
{
    m_atCandidates.clear();
    m_lastLength = 0;
    if (m_edit != NULL)
        m_edit->SetText(L"");
    SetError(std::wstring());
    ClearReplyTarget();
    FaceTable none;
    UpdateCount(none);
}

void CThreadComposer::UpdateCount(const FaceTable& faces)
{
    if (m_count == NULL)
        return;
    const int count = CountChars(WireText(faces));
    m_count->SetText(TRF(kCountPattern, count, kCommentTextMaxChars).c_str());
    m_count->SetTextColor(count > kCommentTextMaxChars ? kErrorColor : kFaint);
}

void CThreadComposer::SetError(const std::wstring& text)
{
    if (m_error != NULL)
        m_error->SetText(text.c_str());
}

bool CThreadComposer::DetectMentionTrigger(int& atPos, POINT& caretScreen)
{
    atPos = -1;
    caretScreen.x = 0;
    caretScreen.y = 0;
    if (m_edit == NULL)
        return false;
    const int length = m_edit->GetTextLength();
    const bool grewByOne = (length == m_lastLength + 1);
    m_lastLength = length;
    if (!grewByOne)
        return false;
    long selStart = 0;
    long selEnd = 0;
    m_edit->GetSel(selStart, selEnd);
    if (selStart != selEnd || selStart <= 0)
        return false;
    const int pos = static_cast<int>(selStart) - 1;
    if (CharAt(pos) != kMentionSign)
        return false;
    //只有位于开头或空白之后的「@」才用来 @ 人，与群聊输入框、发送前的重扫同一规则（「abc@example.com」不算）
    if (pos > 0)
    {
        const wchar_t prev = CharAt(pos - 1);
        if (prev != L' ' && prev != L'\t' && prev != L'\r' && prev != L'\n' && prev != kFullWidthSpace)
            return false;
    }
    //富文本「按字符位置取坐标」是填出参（POINTL），坐标为宿主窗口的客户区坐标；往下挪一行，浮层不压住这一行
    POINTL pt;
    pt.x = 0;
    pt.y = 0;
    m_edit->SendMessageToEngine(EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&pt), static_cast<LPARAM>(pos));
    caretScreen.x = pt.x;
    caretScreen.y = pt.y + kCaretLineHeight;
    balloonwjui::DuiHost* host = m_edit->GetHost();
    if (host != NULL && host->IsWindow())
        ::ClientToScreen(host->m_hWnd, &caretScreen);
    atPos = pos;
    return true;
}

void CThreadComposer::InsertMention(int atPos, int64_t userId, const std::wstring& name)
{
    //userid 为 0 的是「@所有人」（kThreadAtAllUserId）
    if (m_edit == NULL || userId < kThreadAtAllUserId || name.empty())
        return;
    m_edit->SetFocus();
    if (atPos >= 0 && CharAt(atPos) == kMentionSign)
        m_edit->SetSel(atPos, atPos + 1);
    const std::wstring inserted = L"@" + name + L" ";
    m_edit->ReplaceSel(inserted.c_str());
    ThreadAtCandidate cand;
    cand.m_userId = userId;
    cand.m_name = name;
    m_atCandidates.push_back(cand);
    m_lastLength = m_edit->GetTextLength();
}

const std::vector<ThreadAtCandidate>& CThreadComposer::MentionCandidates() const
{
    return m_atCandidates;
}

wchar_t CThreadComposer::CharAt(int pos) const
{
    if (m_edit == NULL || pos < 0)
        return 0;
    //按字符位置取一个字符：不用 GetText 的下标，那里的换行是两个字符，与富文本的字符位置对不上
    wchar_t buf[kCharRangeBufChars] = { 0 };
    TEXTRANGEW range;
    range.chrg.cpMin = pos;
    range.chrg.cpMax = pos + 1;
    range.lpstrText = buf;
    m_edit->SendMessageToEngine(EM_GETTEXTRANGE, 0, reinterpret_cast<LPARAM>(&range));
    return buf[0];
}

int CThreadComposer::PreferredHeight() const
{
    const bool replying = (m_replyBar != NULL && m_replyBar->GetParent() != NULL && m_replyBar->GetParent()->IsVisible());
    return kPadTop + (replying ? kReplyBarH + kGap : 0) + kEditHeight + kGap + kBottomRowH + kPadBottom;
}

} // namespace threadgroup
