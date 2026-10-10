/**
 *  录制条 CRecordBar 的单元测试（标签 [recordbar]），RecordBarTests.cpp
 *  背景：2026-10-05 用户反馈连续录制两次程序崩溃。转储显示第二次开始录制时 CRecordController::startStandalone 调用
 *  CRecordBar::SetPenActive，对第一次录制结束时已随窗口销毁的「画笔」按钮调用了 SetVariant（Hide 没有清空 m_penBtn、
 *  m_mouseBtn 两个指针）。
 *  覆盖：
 *    - RBAR1 录制条显示、关闭之后，按控制器的顺序先设画笔状态、再显示：不访问已销毁的控件，再次显示正常；
 *      关闭后设置时长与暂停状态同样安全。
 *    - RBAR2 独立录屏的录制条有「麦克风」「电脑声音」两个开关（2026-10-05）：文字反映当前状态；点麦克风开关向通知窗口
 *      发 kCmdMic、lParam 为想要的新状态；状态同步后文字随之变化；有开关时整条比没有时宽；
 *    - RBAR3 共享中的录制不显示两个开关，也不显示摄像头开关；
 *    - RBAR4 独立录屏的录制条有「摄像头」开关：文字反映当前状态，点一下向通知窗口发 kCmdCamera、lParam 为想要的新状态；
 *    - RBAR5 勾了「生成字幕」的独立录屏有「字幕」开关（语音字幕 14.12）：文字反映当前状态，点一下向通知窗口发 kCmdCaption、
 *      lParam 为想要的新状态；有开关时整条比没有时宽；没勾时没有这个开关；
 *    - RMIX1 混音器某一路中途关闭后输出静音、时间轴不断，重新打开后恢复。
 *  2026-10-07 起录制条的按钮改成图标按钮（ShareBarButtonArt），上面「文字」的检查改为检查悬停提示，且按钮本身没有文字。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include "../../FlamingoMedia/Record/RecordAudioMixer.h"
#include "../../FlamingoMedia/Record/RecordBar.h"
#include "../../FlamingoMedia/Record/RecordMessages.h"
#include "../Common/I18n/I18n.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include <vector>

namespace {

//录制条的参照矩形（屏幕坐标）：录制条贴在它所在显示器的顶部居中
const RECT kRbarAnchor = { 0, 0, 800, 600 };
//关闭后设置的时长（毫秒）
const int64_t kRbarElapsedMs = 12000;
//混音用例：每段采样数（20 毫秒的 48 kHz）与样值
const size_t kRmixChunk = 960;
const int16_t kRmixValue = 1000;

//收命令的通知窗口：只用来接收录制条投递的消息
HWND RbarMakeNotifyWindow()
{
    return ::CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, ::GetModuleHandleW(NULL), NULL);
}

balloonwjui::DuiButton* RbarButton(screenrecord::CRecordBar& bar, UINT id)
{
    balloonwjui::DuiControl* content = bar.GetClientContent();
    if (content == NULL)
        return NULL;
    return dynamic_cast<balloonwjui::DuiButton*>(content->FindCtrlById(id));
}

//按钮表达的文字：2026-10-07 起录制条的按钮是图标按钮，原来的按钮文字改作悬停提示，按钮本身没有文字
std::wstring RbarText(screenrecord::CRecordBar& bar, UINT id)
{
    balloonwjui::DuiButton* b = RbarButton(bar, id);
    if (b == NULL)
        return std::wstring();
    CHECK(b->GetText().IsEmpty());
    return std::wstring(balloonwjui::DuiToolTipMgr::Inst().GetText(b).GetString());
}

//模拟点一下录制条上的按钮
void RbarClick(screenrecord::CRecordBar& bar, UINT id)
{
    balloonwjui::DuiNotify n;
    n.code = static_cast<UINT>(balloonwjui::DUIN_CLICK);
    n.ctrlId = id;
    n.pCtrl = NULL;
    n.extra = 0;
    bar.SendMessage(WM_DUI_NOTIFY, static_cast<WPARAM>(id), reinterpret_cast<LPARAM>(&n));
}

int RbarWidth(screenrecord::CRecordBar& bar)
{
    RECT rc;
    bar.GetWindowRect(&rc);
    return rc.right - rc.left;
}

} // namespace

TEST_CASE("RBAR1 record bar can be shown again after hide", "[recordbar]")
{
    screenrecord::CRecordBar bar;
    bar.SetPenAvailable(true);
    //第一次录制
    bar.Show(kRbarAnchor, NULL, 0);
    REQUIRE(bar.IsWindow());
    bar.SetPenActive(true);
    bar.Hide();
    CHECK_FALSE(bar.IsWindow());
    //第二次录制：控制器先设画笔状态、更新时长，再显示录制条（与 startStandalone 的顺序相同）
    bar.SetPenActive(false);
    bar.SetState(kRbarElapsedMs, false);
    bar.Show(kRbarAnchor, NULL, 0);
    REQUIRE(bar.IsWindow());
    bar.SetPenActive(true);
    bar.Hide();
    CHECK_FALSE(bar.IsWindow());
}

TEST_CASE("RBAR2 record bar toggles microphone and system audio", "[recordbar]")
{
    const HWND notify = RbarMakeNotifyWindow();
    REQUIRE(notify != NULL);
    screenrecord::CRecordBar plain;
    plain.SetPenAvailable(true);
    plain.Show(kRbarAnchor, notify, 0);
    REQUIRE(plain.IsWindow());
    const int plainW = RbarWidth(plain);
    plain.Hide();

    screenrecord::CRecordBar bar;
    bar.SetPenAvailable(true);
    bar.SetAudioToggles(true, true, false);
    bar.Show(kRbarAnchor, notify, 0);
    REQUIRE(bar.IsWindow());
    CHECK(RbarWidth(bar) > plainW);
    CHECK(RbarText(bar, screenrecord::CRecordBar::kIdMic) == std::wstring(TR(L"麦克风：开")));
    CHECK(RbarText(bar, screenrecord::CRecordBar::kIdSystem) == std::wstring(TR(L"电脑声音：关")));
    //点麦克风开关：请求关闭麦克风
    RbarClick(bar, screenrecord::CRecordBar::kIdMic);
    MSG msg;
    bool got = false;
    while (::PeekMessageW(&msg, notify, screenrecord::kMsgRecordBarCommand, screenrecord::kMsgRecordBarCommand, PM_REMOVE))
    {
        got = true;
        CHECK(msg.wParam == static_cast<WPARAM>(screenrecord::CRecordBar::kCmdMic));
        CHECK(msg.lParam == 0);
    }
    CHECK(got);
    //控制器关掉麦克风、打开电脑声音后同步状态
    bar.SetAudioToggles(true, false, true);
    CHECK(RbarText(bar, screenrecord::CRecordBar::kIdMic) == std::wstring(TR(L"麦克风：关")));
    CHECK(RbarText(bar, screenrecord::CRecordBar::kIdSystem) == std::wstring(TR(L"电脑声音：开")));
    bar.Hide();
    ::DestroyWindow(notify);
}

TEST_CASE("RBAR3 share recording shows no audio toggles", "[recordbar]")
{
    screenrecord::CRecordBar bar;
    bar.SetPenAvailable(false);
    bar.SetAudioToggles(false, true, true);
    bar.Show(kRbarAnchor, NULL, 0);
    REQUIRE(bar.IsWindow());
    CHECK(RbarButton(bar, screenrecord::CRecordBar::kIdMic) == NULL);
    CHECK(RbarButton(bar, screenrecord::CRecordBar::kIdSystem) == NULL);
    CHECK(RbarButton(bar, screenrecord::CRecordBar::kIdCamera) == NULL);
    bar.Hide();
}

TEST_CASE("RBAR4 record bar toggles the camera", "[recordbar]")
{
    const HWND notify = RbarMakeNotifyWindow();
    REQUIRE(notify != NULL);
    screenrecord::CRecordBar bar;
    bar.SetPenAvailable(true);
    bar.SetAudioToggles(true, false, false);
    bar.SetCameraToggle(true, false);
    bar.Show(kRbarAnchor, notify, 0);
    REQUIRE(bar.IsWindow());
    CHECK(RbarText(bar, screenrecord::CRecordBar::kIdCamera) == std::wstring(TR(L"摄像头：关")));
    //点摄像头开关：请求打开摄像头
    RbarClick(bar, screenrecord::CRecordBar::kIdCamera);
    MSG msg;
    bool got = false;
    while (::PeekMessageW(&msg, notify, screenrecord::kMsgRecordBarCommand, screenrecord::kMsgRecordBarCommand, PM_REMOVE))
    {
        got = true;
        CHECK(msg.wParam == static_cast<WPARAM>(screenrecord::CRecordBar::kCmdCamera));
        CHECK(msg.lParam == 1);
    }
    CHECK(got);
    bar.SetCameraToggle(true, true);
    CHECK(RbarText(bar, screenrecord::CRecordBar::kIdCamera) == std::wstring(TR(L"摄像头：开")));
    bar.Hide();
    ::DestroyWindow(notify);
}

TEST_CASE("RMIX1 a source switched off mid-way is silent and can come back", "[recordbar]")
{
    screenrecord::RecordAudioMixer mixer;
    std::vector<int16_t> in(kRmixChunk, kRmixValue);
    std::vector<int16_t> out(kRmixChunk, 0);
    mixer.SetEnabled(screenrecord::record_audio_mic, true);
    mixer.Push(screenrecord::record_audio_mic, &in[0], in.size());
    mixer.Pull(&out[0], out.size());
    CHECK(out[0] == kRmixValue);
    CHECK(out[kRmixChunk - 1] == kRmixValue);
    //中途关闭：推进来的也丢弃，取出的是静音
    mixer.SetEnabled(screenrecord::record_audio_mic, false);
    mixer.Push(screenrecord::record_audio_mic, &in[0], in.size());
    mixer.Pull(&out[0], out.size());
    CHECK(out[0] == 0);
    CHECK(out[kRmixChunk - 1] == 0);
    //重新打开
    mixer.SetEnabled(screenrecord::record_audio_mic, true);
    mixer.Push(screenrecord::record_audio_mic, &in[0], in.size());
    mixer.Pull(&out[0], out.size());
    CHECK(out[0] == kRmixValue);
}

TEST_CASE("RBAR5 record bar toggles captions", "[recordbar]")
{
    const HWND notify = RbarMakeNotifyWindow();
    REQUIRE(notify != NULL);
    //没勾「生成字幕」：没有字幕开关
    screenrecord::CRecordBar plain;
    plain.SetAudioToggles(true, false, false);
    plain.SetCameraToggle(true, false);
    plain.Show(kRbarAnchor, notify, 0);
    REQUIRE(plain.IsWindow());
    CHECK(RbarButton(plain, screenrecord::CRecordBar::kIdCaption) == NULL);
    const int plainWidth = RbarWidth(plain);
    plain.Hide();
    //勾了：开关显示「字幕：开」，点一下请求关闭
    screenrecord::CRecordBar bar;
    bar.SetAudioToggles(true, false, false);
    bar.SetCameraToggle(true, false);
    bar.SetCaptionToggle(true, true);
    bar.Show(kRbarAnchor, notify, 0);
    REQUIRE(bar.IsWindow());
    CHECK(RbarWidth(bar) > plainWidth);
    CHECK(RbarText(bar, screenrecord::CRecordBar::kIdCaption) == std::wstring(TR(L"字幕：开")));
    RbarClick(bar, screenrecord::CRecordBar::kIdCaption);
    MSG msg;
    bool got = false;
    while (::PeekMessageW(&msg, notify, screenrecord::kMsgRecordBarCommand, screenrecord::kMsgRecordBarCommand, PM_REMOVE))
    {
        got = true;
        CHECK(msg.wParam == static_cast<WPARAM>(screenrecord::CRecordBar::kCmdCaption));
        CHECK(msg.lParam == 0);
    }
    CHECK(got);
    //控制器同步为关闭之后，再点一下请求打开
    bar.SetCaptionToggle(true, false);
    CHECK(RbarText(bar, screenrecord::CRecordBar::kIdCaption) == std::wstring(TR(L"字幕：关")));
    RbarClick(bar, screenrecord::CRecordBar::kIdCaption);
    got = false;
    while (::PeekMessageW(&msg, notify, screenrecord::kMsgRecordBarCommand, screenrecord::kMsgRecordBarCommand, PM_REMOVE))
    {
        got = true;
        CHECK(msg.lParam == 1);
    }
    CHECK(got);
    bar.Hide();
    ::DestroyWindow(notify);
}
