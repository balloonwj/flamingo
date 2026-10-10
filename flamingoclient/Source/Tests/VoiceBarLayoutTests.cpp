/**
 *  语音消息录音条布局的单元测试（前缀 VM，标签 [voicemsg]），见 docs/语音消息方案.md 第 3.1 节与第 3b、6g 步：
 *    - VM20 单聊窗与群聊窗：建好时录音条隐藏；录音时输入框与底部发送行隐藏、录音条显示，按窗口允许的最小
 *           尺寸也排得下（按钮不被压扁、音量柱不与按钮重叠）；结束后恢复，输入框的位置与录音前相同；
 *    - VM42 录音条的四种状态（第 6g 步）：每种状态下出现哪些按钮和文字；暂停时红点变灰、计时停住；试听时时间
 *           文字显示「播放进度 / 总时长」；四种状态在单聊窗与群聊窗的最小宽度下都排得下（按钮不被压扁、
 *           互不重叠、都在录音条之内，音量柱与提示文字不压到按钮）。
 *  用真实的聊天窗布局（BuildChatWindowBody / BuildGroupChatWindowBody），不建窗口。框架 Catch2 v3。
 *
 *  典型用法：FlamingoUnit.exe "[voicemsg]"
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <memory>
#include <vector>

#include "../UI/BalloonChatWnd/ChatWindowLayout.h"
#include "../UI/BalloonChatWnd/GroupChatWindowLayout.h"
#include "../UI/BalloonChatWnd/VoiceRecordBar.h"
#include "../UI/BalloonChatWnd/VoiceRecordFlow.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiBadge.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/DuiResMgr.h"

using namespace balloonchatwnd;

namespace {

//单聊窗允许的最小尺寸（2026-10-06 起直接取 ChatWindowLayout.h 的常量）
const int kVbSingleMinWidth = kChatWindowMinWidth;
const int kVbSingleMinHeight = kChatWindowMinHeight;
//暂停时红点的灰色（与 VoiceRecordBar.cpp 相同）
const COLORREF kVbDotStopped = RGB(0xB0, 0xB0, 0xB0);
//计时用的时间点（毫秒）
const uint64_t kVbT0 = 100000;
const uint64_t kVbOneSecond = 1000;

int rectWidth(const RECT& rc)
{
    return rc.right - rc.left;
}

int rectHeight(const RECT& rc)
{
    return rc.bottom - rc.top;
}

//单聊窗录音条的按钮编号
VoiceRecordBarIds singleIds()
{
    VoiceRecordBarIds ids;
    ids.m_pause = kIdVoicePause;
    ids.m_stop = kIdVoiceStop;
    ids.m_preview = kIdVoicePreview;
    ids.m_cancel = kIdVoiceCancel;
    ids.m_send = kIdVoiceSend;
    ids.m_confirm = kIdVoiceConfirm;
    ids.m_back = kIdVoiceBack;
    return ids;
}

//群聊窗录音条的按钮编号
VoiceRecordBarIds groupIds()
{
    VoiceRecordBarIds ids;
    ids.m_pause = kGIdVoicePause;
    ids.m_stop = kGIdVoiceStop;
    ids.m_preview = kGIdVoicePreview;
    ids.m_cancel = kGIdVoiceCancel;
    ids.m_send = kGIdVoiceSend;
    ids.m_confirm = kGIdVoiceConfirm;
    ids.m_back = kGIdVoiceBack;
    return ids;
}

//一个按钮：可见与否、文字
balloonwjui::DuiButton* buttonOf(balloonwjui::DuiControl* root, unsigned int id)
{
    balloonwjui::DuiButton* b = dynamic_cast<balloonwjui::DuiButton*>(root->FindCtrlById(id));
    REQUIRE(b != NULL);
    return b;
}

//控件及其所有上级都可见
//用控件的默认字体量一段文字的宽度（像素），与录音条量文字宽度的方法相同
int textWidthPx(const wchar_t* text)
{
    HDC hdc = ::GetDC(NULL);
    HGDIOBJ oldFont = ::SelectObject(hdc, balloonwjui::DuiResMgr::Inst().GetDefaultFont());
    SIZE size = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text, static_cast<int>(::wcslen(text)), &size);
    ::SelectObject(hdc, oldFont);
    ::ReleaseDC(NULL, hdc);
    return size.cx;
}

bool shown(balloonwjui::DuiControl* c)
{
    for (balloonwjui::DuiControl* p = c; p != NULL; p = p->GetParent())
    {
        if (!p->IsVisible())
            return false;
    }
    return true;
}

/**
 *  检查录音条当前的排版：expectedIds 里的按钮可见、其余按钮隐藏；可见按钮的宽度等于布局给它的固定宽度
 *  （没有被压扁）、都在录音条之内、从左到右互不重叠；leftPart（音量柱或提示文字）不压到第一个按钮。
 */
void checkButtons(balloonwjui::DuiControl* root, balloonwjui::DuiHBox* holder, const VoiceRecordBarIds& ids,
                  const std::vector<unsigned int>& expectedIds, balloonwjui::DuiControl* leftPart)
{
    const unsigned int all[] = { ids.m_pause, ids.m_stop, ids.m_preview, ids.m_cancel, ids.m_send, ids.m_confirm,
                                 ids.m_back };
    const RECT barRc = holder->GetRect();
    std::vector<RECT> visibleRects;
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); ++i)
    {
        balloonwjui::DuiButton* b = buttonOf(root, all[i]);
        bool expected = false;
        for (size_t k = 0; k < expectedIds.size(); ++k)
        {
            if (expectedIds[k] == all[i])
                expected = true;
        }
        CHECK(b->IsVisible() == expected);
        if (!expected)
            continue;
        const RECT rc = b->GetRect();
        const balloonwjui::DuiLayout::Hint hint = holder->GetHint(b);
        CHECK(hint.fixedMain > 0);
        CHECK(rectWidth(rc) == hint.fixedMain);
        CHECK(rc.left >= barRc.left);
        CHECK(rc.right <= barRc.right);
        CHECK(rc.top >= barRc.top);
        CHECK(rc.bottom <= barRc.bottom);
        visibleRects.push_back(rc);
    }
    REQUIRE(visibleRects.size() == expectedIds.size());
    for (size_t i = 1; i < visibleRects.size(); ++i)
        CHECK(visibleRects[i - 1].right <= visibleRects[i].left);
    if (leftPart != NULL && !visibleRects.empty())
    {
        CHECK(leftPart->GetRect().right <= visibleRects[0].left);
        CHECK(leftPart->GetRect().left >= barRc.left);
    }
}

//检查一棵聊天窗布局的录音条：建好隐藏、显示后排得下、恢复后输入框位置不变
void checkVoiceBarLayout(balloonwjui::DuiControl* root, UINT inputId, UINT bottomRowId, UINT barId,
                         const VoiceRecordBarIds& ids, int width, int height)
{
    REQUIRE(root != NULL);
    balloonwjui::DuiControl* input = root->FindCtrlById(inputId);
    balloonwjui::DuiControl* bottomRow = root->FindCtrlById(bottomRowId);
    balloonwjui::DuiHBox* holder = dynamic_cast<balloonwjui::DuiHBox*>(root->FindCtrlById(barId));
    REQUIRE(input != NULL);
    REQUIRE(bottomRow != NULL);
    REQUIRE(holder != NULL);
    //建好时录音条隐藏，输入框与底部行可见
    CHECK_FALSE(holder->IsVisible());
    CHECK(input->IsVisible());
    CHECK(bottomRow->IsVisible());

    CVoiceRecordBar bar;
    bar.Build(holder, ids);
    const RECT area = { 0, 0, width, height };
    root->ForceLayout(area);
    const RECT inputBefore = input->GetRect();

    //录音：输入框与底部行隐藏，录音条显示并排得下
    bar.Begin(kVbT0);
    bar.ApplyState(voice_rec_recording, false);
    ShowVoiceRecordBar(input, bottomRow, holder, true);
    root->ForceLayout(area);
    CHECK_FALSE(input->IsVisible());
    CHECK_FALSE(bottomRow->IsVisible());
    CHECK(holder->IsVisible());
    const RECT barRc = holder->GetRect();
    CHECK(rectWidth(barRc) > 0);
    CHECK(rectHeight(barRc) > 0);
    std::vector<unsigned int> recordingIds;
    recordingIds.push_back(ids.m_pause);
    recordingIds.push_back(ids.m_stop);
    recordingIds.push_back(ids.m_cancel);
    recordingIds.push_back(ids.m_send);
    checkButtons(root, holder, ids, recordingIds, static_cast<balloonwjui::DuiControl*>(bar.Wave()));

    //结束：恢复输入框与底部行，录音条隐藏，输入框位置与录音前相同
    ShowVoiceRecordBar(input, bottomRow, holder, false);
    root->ForceLayout(area);
    CHECK(input->IsVisible());
    CHECK(bottomRow->IsVisible());
    CHECK_FALSE(holder->IsVisible());
    const RECT inputAfter = input->GetRect();
    CHECK(inputAfter.left == inputBefore.left);
    CHECK(inputAfter.top == inputBefore.top);
    CHECK(inputAfter.right == inputBefore.right);
    CHECK(inputAfter.bottom == inputBefore.bottom);
}

//检查录音条四种状态的显示内容与排版
void checkVoiceBarStates(balloonwjui::DuiControl* root, UINT inputId, UINT bottomRowId, UINT barId,
                         const VoiceRecordBarIds& ids, int width, int height)
{
    REQUIRE(root != NULL);
    balloonwjui::DuiHBox* holder = dynamic_cast<balloonwjui::DuiHBox*>(root->FindCtrlById(barId));
    REQUIRE(holder != NULL);
    CVoiceRecordBar bar;
    bar.Build(holder, ids);
    const RECT area = { 0, 0, width, height };
    ShowVoiceRecordBar(root->FindCtrlById(inputId), root->FindCtrlById(bottomRowId), holder, true);
    balloonwjui::DuiControl* wave = static_cast<balloonwjui::DuiControl*>(bar.Wave());
    balloonwjui::DuiControl* confirmText = static_cast<balloonwjui::DuiControl*>(bar.ConfirmLabel());
    REQUIRE(wave != NULL);
    REQUIRE(confirmText != NULL);

    //录音中：红点、计时、音量柱可见，「暂停」「停止」「取消」「发送」
    bar.Begin(kVbT0);
    bar.ApplyState(voice_rec_recording, false);
    root->ForceLayout(area);
    std::vector<unsigned int> recordingIds;
    recordingIds.push_back(ids.m_pause);
    recordingIds.push_back(ids.m_stop);
    recordingIds.push_back(ids.m_cancel);
    recordingIds.push_back(ids.m_send);
    checkButtons(root, holder, ids, recordingIds, wave);
    CHECK(buttonOf(root, ids.m_pause)->GetText() == L"暂停");
    CHECK(shown(static_cast<balloonwjui::DuiControl*>(bar.Dot())));
    //红点是空文字的徽标：须关掉「空文字时隐藏」，否则根本不会画出来（第 6h 步实机发现）
    CHECK(bar.Dot()->IsShowing());
    //按钮分主次：「发送」主色、「确认放弃」危险红，其余为次操作的白底灰边
    CHECK(buttonOf(root, ids.m_send)->GetVariant() == balloonwjui::DuiButton::Variant::Primary);
    CHECK(buttonOf(root, ids.m_confirm)->GetVariant() == balloonwjui::DuiButton::Variant::Danger);
    CHECK(buttonOf(root, ids.m_pause)->GetVariant() == balloonwjui::DuiButton::Variant::Default);
    CHECK(buttonOf(root, ids.m_stop)->GetVariant() == balloonwjui::DuiButton::Variant::Default);
    CHECK(buttonOf(root, ids.m_preview)->GetVariant() == balloonwjui::DuiButton::Variant::Default);
    CHECK(buttonOf(root, ids.m_cancel)->GetVariant() == balloonwjui::DuiButton::Variant::Default);
    CHECK(buttonOf(root, ids.m_back)->GetVariant() == balloonwjui::DuiButton::Variant::Default);
    CHECK(shown(static_cast<balloonwjui::DuiControl*>(bar.TimerLabel())));
    CHECK(shown(wave));
    CHECK_FALSE(shown(confirmText));
    CHECK_FALSE(shown(static_cast<balloonwjui::DuiControl*>(bar.PreviewTimeLabel())));
    CHECK(bar.Dot()->GetBgColor() != kVbDotStopped);
    bar.Tick(kVbT0 + 3 * kVbOneSecond);
    CHECK(bar.TimerLabel()->GetText() == L"0:03");

    //已暂停：「继续」「试听」「取消」「发送」（2026-10-05 起暂停中可以试听，「停止」不再显示），红点变灰，计时停住
    //（暂停期间的时间不计入，继续后接着累计）
    bar.Pause(kVbT0 + 3 * kVbOneSecond);
    bar.ApplyState(voice_rec_paused, false);
    root->ForceLayout(area);
    std::vector<unsigned int> pausedIds;
    pausedIds.push_back(ids.m_pause);
    pausedIds.push_back(ids.m_preview);
    pausedIds.push_back(ids.m_cancel);
    pausedIds.push_back(ids.m_send);
    checkButtons(root, holder, ids, pausedIds, wave);
    CHECK(buttonOf(root, ids.m_pause)->GetText() == L"继续");
    CHECK(buttonOf(root, ids.m_preview)->GetText() == L"试听");
    CHECK(bar.Dot()->GetBgColor() == kVbDotStopped);
    bar.Tick(kVbT0 + 10 * kVbOneSecond);
    CHECK(bar.TimerLabel()->GetText() == L"0:03");
    CHECK(bar.ElapsedMs(kVbT0 + 10 * kVbOneSecond) == static_cast<int64_t>(3 * kVbOneSecond));
    //暂停中试听：按钮为「停止试听」，计时位置换成「播放进度 / 已录时长」、不显示红点，最小宽度下仍排得下、不压到音量柱
    bar.ApplyState(voice_rec_paused, true);
    bar.SetPreviewProgress(1 * kVbOneSecond, 3 * kVbOneSecond);
    root->ForceLayout(area);
    checkButtons(root, holder, ids, pausedIds, wave);
    CHECK(buttonOf(root, ids.m_preview)->GetText() == L"停止试听");
    CHECK_FALSE(shown(static_cast<balloonwjui::DuiControl*>(bar.Dot())));
    CHECK(shown(static_cast<balloonwjui::DuiControl*>(bar.PreviewTimeLabel())));
    CHECK_FALSE(shown(static_cast<balloonwjui::DuiControl*>(bar.TimerLabel())));
    CHECK(bar.PreviewTimeLabel()->GetText() == L"0:01 / 0:03");
    CHECK(bar.PreviewTimeLabel()->GetRect().right <= wave->GetRect().left);
    //试听结束：计时位置换回已录时长，红点重新显示
    bar.ApplyState(voice_rec_paused, false);
    root->ForceLayout(area);
    CHECK(shown(static_cast<balloonwjui::DuiControl*>(bar.TimerLabel())));
    CHECK(shown(static_cast<balloonwjui::DuiControl*>(bar.Dot())));
    CHECK_FALSE(shown(static_cast<balloonwjui::DuiControl*>(bar.PreviewTimeLabel())));
    CHECK(buttonOf(root, ids.m_preview)->GetText() == L"试听");
    bar.Resume(kVbT0 + 10 * kVbOneSecond);
    bar.ApplyState(voice_rec_recording, false);
    CHECK(bar.Dot()->GetBgColor() != kVbDotStopped);
    CHECK(bar.ElapsedMs(kVbT0 + 12 * kVbOneSecond) == static_cast<int64_t>(5 * kVbOneSecond));

    //确认放弃：「放弃这段录音？」与「确认放弃」「返回」，音量柱隐藏
    bar.Pause(kVbT0 + 12 * kVbOneSecond);
    bar.ApplyState(voice_rec_confirm_discard, false);
    root->ForceLayout(area);
    std::vector<unsigned int> confirmIds;
    confirmIds.push_back(ids.m_confirm);
    confirmIds.push_back(ids.m_back);
    checkButtons(root, holder, ids, confirmIds, confirmText);
    CHECK(shown(confirmText));
    CHECK(bar.ConfirmLabel()->GetText() == L"放弃这段录音？");
    CHECK_FALSE(shown(wave));

    //试听：时间文字写总时长，「试听」「取消」「发送」；播放中写「播放进度 / 总时长」、按钮为「停止试听」
    bar.SetPreviewTotal(12 * kVbOneSecond);
    bar.ApplyState(voice_rec_preview, false);
    root->ForceLayout(area);
    std::vector<unsigned int> previewIds;
    previewIds.push_back(ids.m_preview);
    previewIds.push_back(ids.m_cancel);
    previewIds.push_back(ids.m_send);
    checkButtons(root, holder, ids, previewIds, wave);
    CHECK(buttonOf(root, ids.m_preview)->GetText() == L"试听");
    CHECK(shown(static_cast<balloonwjui::DuiControl*>(bar.PreviewTimeLabel())));
    CHECK_FALSE(shown(static_cast<balloonwjui::DuiControl*>(bar.TimerLabel())));
    CHECK(bar.PreviewTimeLabel()->GetText() == L"0:12");
    bar.ApplyState(voice_rec_preview, true);
    bar.SetPreviewProgress(3 * kVbOneSecond, 12 * kVbOneSecond);
    root->ForceLayout(area);
    checkButtons(root, holder, ids, previewIds, wave);
    CHECK(buttonOf(root, ids.m_preview)->GetText() == L"停止试听");
    CHECK(bar.PreviewTimeLabel()->GetText() == L"0:03 / 0:12");
    //试听时间文字放得下「0:03 / 0:12」：不压到音量柱
    CHECK(bar.PreviewTimeLabel()->GetRect().right <= wave->GetRect().left);
    //试听时间文字的宽度按最长的「2:00 / 2:00」量出（2026-10-05 起），放得下这串字
    const RECT previewRc = bar.PreviewTimeLabel()->GetRect();
    CHECK(previewRc.right - previewRc.left >= textWidthPx(L"2:00 / 2:00"));
}

} // namespace

TEST_CASE("VM20 录音条布局：单聊窗与群聊窗按最小尺寸排得下，结束后输入框复原", "[voicemsg]")
{
    SECTION("单聊窗")
    {
        std::unique_ptr<balloonwjui::DuiControl> root = BuildChatWindowBody(false);
        checkVoiceBarLayout(root.get(), kIdInputEdit, kIdInputBottomRow, kIdVoiceBar, singleIds(),
                            kVbSingleMinWidth, kVbSingleMinHeight);
    }
    SECTION("群聊窗")
    {
        std::unique_ptr<balloonwjui::DuiControl> root = BuildGroupChatWindowBody(false);
        checkVoiceBarLayout(root.get(), kGIdInputEdit, kGIdInputBottomRow, kGIdVoiceBar, groupIds(),
                            kGroupChatWindowMinWidth, kGroupChatWindowMinHeight);
    }
}

TEST_CASE("VM42 录音条的四种状态：按钮与文字、暂停计时、试听进度，最小宽度下排得下", "[voicemsg]")
{
    SECTION("单聊窗")
    {
        std::unique_ptr<balloonwjui::DuiControl> root = BuildChatWindowBody(false);
        checkVoiceBarStates(root.get(), kIdInputEdit, kIdInputBottomRow, kIdVoiceBar, singleIds(),
                            kVbSingleMinWidth, kVbSingleMinHeight);
    }
    SECTION("群聊窗")
    {
        std::unique_ptr<balloonwjui::DuiControl> root = BuildGroupChatWindowBody(false);
        checkVoiceBarStates(root.get(), kGIdInputEdit, kGIdInputBottomRow, kGIdVoiceBar, groupIds(),
                            kGroupChatWindowMinWidth, kGroupChatWindowMinHeight);
    }
    //试听时间文字的写法
    CHECK(FormatVoicePreviewProgress(3 * kVbOneSecond, 12 * kVbOneSecond) == L"0:03 / 0:12");
    CHECK(FormatVoicePreviewProgress(0, 65 * kVbOneSecond) == L"0:00 / 1:05");
}
