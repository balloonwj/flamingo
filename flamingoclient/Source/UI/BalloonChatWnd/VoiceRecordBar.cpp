/**
 *  聊天窗的录音条，说明见 VoiceRecordBar.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "VoiceRecordBar.h"

#include <memory>
#include "VoiceRecordFlow.h"
#include "../../Common/I18n/I18n.h"
#include "../../../../third-party/balloonui/balloonui/DuiResMgr.h"
#include "../../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../../third-party/balloonui/balloonui/Controls/Basic/DuiBadge.h"
#include "../../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../../third-party/balloonui/balloonui/Controls/Basic/DuiSeparator.h"

namespace balloonchatwnd {

namespace {

//红点的直径（像素）与颜色：录音中为红色，暂停、录满与确认放弃时为灰色
const int kDotPx = 8;
const COLORREF kDotRecording = RGB(0xE5, 0x39, 0x35);
const COLORREF kDotStopped = RGB(0xB0, 0xB0, 0xB0);
//红点用胶囊形徽标画成圆：圆角半径取 -1 表示高度的一半
const int kCapsuleRadius = -1;
//计时文字的宽度（像素），容得下「2:00」
const int kTimerWidthPx = 40;
//试听时间文字最长时的样子（录音最长 2 分钟）：按默认字体量出它的宽度作为试听时间文字的宽度。原先固定 84 像素，
//比实际需要宽出二十多像素，暂停中试听（2026-10-05）时群聊窗最小宽度下按钮排不下
const wchar_t kPreviewTimeWidest[] = L"2:00 / 2:00";
//试听时间文字量出的宽度之外再留的空白（像素）
const int kPreviewTimePadPx = 4;
//计时与提示文字的颜色
const COLORREF kTimerColor = RGB(0x33, 0x33, 0x33);
//音量柱：每根的宽度、柱间距、所在行的高度与最矮的柱高（像素），颜色与品牌蓝一致。第 6g 步按钮由两个增加到
//四个，单聊窗最小宽度 460 下放不下原来 3 像素宽、3 像素间距的 24 根，改为各 2 像素
const int kBarWidthPx = 2;
const int kBarGapPx = 2;
const int kBarRowPx = 28;
const int kBarMinPx = 2;
const COLORREF kBarColor = RGB(0x1E, 0x88, 0xE5);
//按钮的最小宽度与高度（像素）；按文字宽度加宽时文字左右各留的空白（像素）
const int kButtonMinWidthPx = 52;
const int kButtonHeightPx = 30;
const int kButtonTextPadPx = 12;
//录音条内各部分之间的间距与左右留白（像素）
const int kItemGapPx = 8;
const int kSidePaddingPx = 6;

//用控件的默认字体测量一段文字的宽度（像素）
int measureTextWidth(const wchar_t* text)
{
    if (text == NULL || text[0] == L'\0')
        return 0;
    HDC hdc = ::GetDC(NULL);
    HGDIOBJ oldFont = ::SelectObject(hdc, balloonwjui::DuiResMgr::Inst().GetDefaultFont());
    SIZE size = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text, static_cast<int>(::wcslen(text)), &size);
    ::SelectObject(hdc, oldFont);
    ::ReleaseDC(NULL, hdc);
    return size.cx;
}

//建一个文字标签
std::unique_ptr<balloonwjui::DuiLabel> makeLabel(const wchar_t* text)
{
    std::unique_ptr<balloonwjui::DuiLabel> label(new balloonwjui::DuiLabel());
    label->SetText(text);
    label->SetTextColor(kTimerColor);
    label->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    return label;
}

} // namespace

CVoiceRecordBar::CVoiceRecordBar()
    : m_holder(NULL)
    , m_dot(NULL)
    , m_timer(NULL)
    , m_previewTime(NULL)
    , m_wave(NULL)
    , m_confirmText(NULL)
    , m_pauseBtn(NULL)
    , m_stopBtn(NULL)
    , m_previewBtn(NULL)
    , m_cancelBtn(NULL)
    , m_sendBtn(NULL)
    , m_confirmBtn(NULL)
    , m_backBtn(NULL)
    , m_segmentStartMs(0)
    , m_accumulatedMs(0)
    , m_timerPaused(false)
    , m_limitReached(false)
{
}

void CVoiceRecordBar::Build(balloonwjui::DuiHBox* holder, const VoiceRecordBarIds& ids)
{
    if (holder == NULL || m_holder != NULL)
        return;
    m_holder = holder;
    holder->SetGap(kItemGapPx);
    holder->SetPadding(kSidePaddingPx, 0, kSidePaddingPx, 0);
    typedef balloonwjui::DuiLayout::Hint Hint;

    //红点：空文字的胶囊形徽标
    std::unique_ptr<balloonwjui::DuiBadge> dot(new balloonwjui::DuiBadge());
    dot->SetText(L"");
    //徽标默认在文字为空时不画；这里要的正是一个没有文字的圆点
    dot->SetHideWhenEmpty(false);
    dot->SetBgColor(kDotRecording);
    dot->SetCornerRadius(kCapsuleRadius);
    m_dot = dot.get();
    holder->AddChild(std::move(dot), Hint().Fixed(kDotPx, kDotPx).AlignC(balloonwjui::DuiLayout::AlignCenter));

    //计时（录音中、已暂停、确认放弃）与试听时间（试听）：同一位置，按状态显示其一
    std::unique_ptr<balloonwjui::DuiLabel> timer = makeLabel(FormatVoiceElapsed(0).c_str());
    m_timer = timer.get();
    holder->AddChild(std::move(timer), Hint().Fixed(kTimerWidthPx));
    std::unique_ptr<balloonwjui::DuiLabel> previewTime = makeLabel(FormatVoiceElapsed(0).c_str());
    m_previewTime = previewTime.get();
    //宽度按最长的试听时间量出，不小于计时文字的宽度
    int previewTimeWidth = measureTextWidth(kPreviewTimeWidest) + kPreviewTimePadPx;
    if (previewTimeWidth < kTimerWidthPx)
        previewTimeWidth = kTimerWidthPx;
    holder->AddChild(std::move(previewTime), Hint().Fixed(previewTimeWidth));

    //音量柱：放在一个固定高度、纵向居中的横向布局里，每根是竖向的分隔线
    std::unique_ptr<balloonwjui::DuiHBox> wave(new balloonwjui::DuiHBox());
    wave->SetGap(kBarGapPx);
    for (int i = 0; i < kVoiceBarCount; ++i)
    {
        std::unique_ptr<balloonwjui::DuiSeparator> bar(new balloonwjui::DuiSeparator());
        bar->SetOrientation(balloonwjui::DuiSeparator::Vertical);
        bar->SetColor(kBarColor);
        bar->SetThickness(kBarWidthPx);
        bar->SetInset(VoiceBarInset(0, kBarRowPx, kBarMinPx));
        m_bars.push_back(bar.get());
        wave->AddChild(std::move(bar), Hint().Fixed(kBarWidthPx));
    }
    m_wave = wave.get();
    holder->AddChild(std::move(wave), Hint().Weight(1).Fixed(-1, kBarRowPx).AlignC(balloonwjui::DuiLayout::AlignCenter));

    //确认放弃的提示文字：与音量柱占同一位置
    std::unique_ptr<balloonwjui::DuiLabel> confirmText = makeLabel(TR(L"放弃这段录音？"));
    m_confirmText = confirmText.get();
    holder->AddChild(std::move(confirmText), Hint().Weight(1));

    //按钮：从左到右「暂停 / 继续」「停止」「试听 / 停止试听」「取消」「发送」「确认放弃」「返回」，按状态显示其中几个
    std::vector<const wchar_t*> texts;
    texts.push_back(TR(L"暂停"));
    texts.push_back(TR(L"继续"));
    m_pauseBtn = addButton(ids.m_pause, texts, button_secondary);
    texts.assign(1, TR(L"停止"));
    m_stopBtn = addButton(ids.m_stop, texts, button_secondary);
    texts.assign(1, TR(L"试听"));
    texts.push_back(TR(L"停止试听"));
    m_previewBtn = addButton(ids.m_preview, texts, button_secondary);
    texts.assign(1, TR(L"取消"));
    m_cancelBtn = addButton(ids.m_cancel, texts, button_secondary);
    texts.assign(1, TR(L"发送"));
    m_sendBtn = addButton(ids.m_send, texts, button_primary);
    texts.assign(1, TR(L"确认放弃"));
    m_confirmBtn = addButton(ids.m_confirm, texts, button_danger);
    texts.assign(1, TR(L"返回"));
    m_backBtn = addButton(ids.m_back, texts, button_secondary);

    ApplyState(voice_rec_recording, false);
}

balloonwjui::DuiButton* CVoiceRecordBar::addButton(unsigned int id, const std::vector<const wchar_t*>& texts,
                                                   int style)
{
    std::unique_ptr<balloonwjui::DuiButton> button(new balloonwjui::DuiButton());
    button->SetText(texts.empty() ? L"" : texts[0]);
    button->SetCtrlId(id);
    //DuiButton 默认是主色实心，次操作与不可逆操作要显式设置
    if (style == button_primary)
        button->SetVariant(balloonwjui::DuiButton::Variant::Primary);
    else if (style == button_danger)
        button->SetVariant(balloonwjui::DuiButton::Variant::Danger);
    else
        button->SetVariant(balloonwjui::DuiButton::Variant::Default);
    //按最宽的那段文字定宽度，只增不减：中文放得下设计宽度，译文较长时加宽（UI 约定第 8 条）
    int width = kButtonMinWidthPx;
    for (size_t i = 0; i < texts.size(); ++i)
    {
        const int needed = measureTextWidth(texts[i]) + 2 * kButtonTextPadPx;
        if (needed > width)
            width = needed;
    }
    balloonwjui::DuiButton* raw = button.get();
    m_holder->AddChild(std::move(button),
                       balloonwjui::DuiLayout::Hint().Fixed(width, kButtonHeightPx)
                           .AlignC(balloonwjui::DuiLayout::AlignCenter));
    return raw;
}

void CVoiceRecordBar::Begin(uint64_t nowMs)
{
    m_segmentStartMs = nowMs;
    m_accumulatedMs = 0;
    m_timerPaused = false;
    m_limitReached = false;
    m_history.Reset();
    if (m_dot != NULL)
        m_dot->SetBgColor(kDotRecording);
    if (m_timer != NULL)
        m_timer->SetText(FormatVoiceElapsed(0).c_str());
    refreshBars();
}

void CVoiceRecordBar::Pause(uint64_t nowMs)
{
    if (!m_timerPaused)
    {
        m_accumulatedMs += (nowMs >= m_segmentStartMs) ? static_cast<int64_t>(nowMs - m_segmentStartMs) : 0;
        m_timerPaused = true;
    }
    Tick(nowMs);
}

void CVoiceRecordBar::Resume(uint64_t nowMs)
{
    if (m_timerPaused)
    {
        m_segmentStartMs = nowMs;
        m_timerPaused = false;
    }
    Tick(nowMs);
}

int64_t CVoiceRecordBar::ElapsedMs(uint64_t nowMs) const
{
    //录满之后停在 2:00
    if (m_limitReached)
        return kVoiceElapsedCapMs;
    int64_t elapsed = m_accumulatedMs;
    if (!m_timerPaused && nowMs >= m_segmentStartMs)
        elapsed += static_cast<int64_t>(nowMs - m_segmentStartMs);
    return elapsed;
}

void CVoiceRecordBar::Tick(uint64_t nowMs)
{
    if (m_timer == NULL)
        return;
    m_timer->SetText(FormatVoiceElapsed(ElapsedMs(nowMs)).c_str());
}

void CVoiceRecordBar::PushLevel(int level, uint64_t nowMs)
{
    //暂停后的音量不再推进音量柱（语音消息中心也已丢弃暂停后取出的音量）
    if (!m_timerPaused)
    {
        m_history.Push(level);
        refreshBars();
    }
    Tick(nowMs);
}

void CVoiceRecordBar::MarkLimitReached()
{
    m_limitReached = true;
    if (m_dot != NULL)
        m_dot->SetBgColor(kDotStopped);
    Tick(0);
}

void CVoiceRecordBar::ApplyState(int state, bool previewing)
{
    if (m_holder == NULL || state == voice_rec_idle)
        return;
    const bool recording = (state == voice_rec_recording);
    const bool paused = (state == voice_rec_paused);
    const bool live = recording || paused;
    const bool preview = (state == voice_rec_preview);
    const bool confirm = (state == voice_rec_confirm_discard);
    //计时位置显示试听时间：试听状态，以及已暂停时正在试听（2026-10-05）
    const bool showPlayTime = preview || (paused && previewing);

    //红点只在录音中为红色；计时位置显示试听时间时不显示红点（群聊窗最小宽度下，暂停中试听靠这 16 像素才排得下）
    m_dot->SetVisible(!showPlayTime);
    m_dot->SetBgColor((recording && !m_limitReached) ? kDotRecording : kDotStopped);
    m_timer->SetVisible(!showPlayTime);
    m_previewTime->SetVisible(showPlayTime);
    //确认放弃时，提示文字代替音量柱
    m_wave->SetVisible(!confirm);
    m_confirmText->SetVisible(confirm);

    m_pauseBtn->SetVisible(live);
    m_pauseBtn->SetText(state == voice_rec_paused ? TR(L"继续") : TR(L"暂停"));
    //「停止」只在录音中显示：已暂停时能直接试听、取消、发送（2026-10-05），「停止」进入试听状态已无必要，
    //省下的位置让已暂停的四个按钮在群聊窗最小宽度下排得下
    m_stopBtn->SetVisible(recording);
    m_previewBtn->SetVisible(preview || paused);
    m_previewBtn->SetText(previewing ? TR(L"停止试听") : TR(L"试听"));
    m_cancelBtn->SetVisible(live || preview);
    m_sendBtn->SetVisible(live || preview);
    m_confirmBtn->SetVisible(confirm);
    m_backBtn->SetVisible(confirm);

    //SetVisible 只置标志位，不会重排（UI 约定第 5 条）：按录音条当前的矩形强制重排
    m_holder->ForceLayout(m_holder->GetRect());
    m_holder->Invalidate();
}

void CVoiceRecordBar::SetPreviewTotal(int64_t totalMs)
{
    if (m_previewTime != NULL)
        m_previewTime->SetText(FormatVoiceElapsed(totalMs).c_str());
}

void CVoiceRecordBar::SetPreviewProgress(int64_t playedMs, int64_t totalMs)
{
    if (m_previewTime != NULL)
        m_previewTime->SetText(FormatVoicePreviewProgress(playedMs, totalMs).c_str());
}

void CVoiceRecordBar::refreshBars()
{
    const std::vector<uint8_t>& levels = m_history.Levels();
    for (size_t i = 0; i < m_bars.size() && i < levels.size(); ++i)
        m_bars[i]->SetInset(VoiceBarInset(levels[i], kBarRowPx, kBarMinPx));
}

void ShowVoiceRecordBar(balloonwjui::DuiControl* inputEdit, balloonwjui::DuiControl* bottomRow,
                        balloonwjui::DuiControl* voiceBar, bool show)
{
    if (voiceBar == NULL)
        return;
    if (inputEdit != NULL)
        inputEdit->SetVisible(!show);
    if (bottomRow != NULL)
        bottomRow->SetVisible(!show);
    voiceBar->SetVisible(show);
    //SetVisible 不会重排：对输入区（录音条的父容器）按它当前的矩形强制重排
    balloonwjui::DuiControl* parent = voiceBar->GetParent();
    if (parent != NULL)
        parent->ForceLayout(parent->GetRect());
}

std::wstring FormatVoicePreviewProgress(int64_t playedMs, int64_t totalMs)
{
    return FormatVoiceElapsed(playedMs) + L" / " + FormatVoiceElapsed(totalMs);
}

} // namespace balloonchatwnd
