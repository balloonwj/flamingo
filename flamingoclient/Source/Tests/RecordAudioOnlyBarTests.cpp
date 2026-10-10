/**
 *  只录声音的录制条与选择窗规则的单元测试（标签 [recordbar]），RecordAudioOnlyBarTests.cpp
 *
 *  覆盖第 24 期（docs/屏幕共享与录制方案.md 第 12.5 节）录制条与选择窗里与只录声音有关的部分：
 *    - RAOB1 只录声音的录制条：没有「鼠标效果」「画笔」「摄像头」；文字为「录音中 mm:ss」，暂停时为「已暂停」；
 *            麦克风、电脑声音两个开关下面各有一条电平条，SetLevels 后电平条的值随之变化；比带同样开关的录像录制条窄；
 *    - RAOB2 录像的录制条：有「鼠标效果」，文字为「录制中」，没有电平条；关闭后再以录像形态显示时不会残留电平条；
 *    - RAOB3 选择窗「只录声音」按钮的可用规则：麦克风与电脑声音至少勾一项。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include "../../FlamingoMedia/Record/RecordBar.h"
#include "../../FlamingoMedia/Share/SharePickerPolicy.h"
#include "../Common/I18n/I18n.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiProgressBar.h"

namespace {

//录制条的参照矩形（屏幕坐标）
const RECT kRaobAnchor = { 0, 0, 800, 600 };
//设置的录制时长（毫秒）：1 分 5 秒
const int64_t kRaobElapsedMs = 65000;
//设置的两路电平
const int kRaobMicLevel = 63;
const int kRaobSystemLevel = 17;

HWND RaobMakeNotifyWindow()
{
    return ::CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, ::GetModuleHandleW(NULL), NULL);
}

balloonwjui::DuiControl* RaobFind(screenrecord::CRecordBar& bar, UINT id)
{
    balloonwjui::DuiControl* content = bar.GetClientContent();
    return content != NULL ? content->FindCtrlById(id) : NULL;
}

int RaobWidth(screenrecord::CRecordBar& bar)
{
    RECT rc;
    bar.GetWindowRect(&rc);
    return rc.right - rc.left;
}

} // namespace

TEST_CASE("RAOB1 audio-only record bar", "[recordbar]")
{
    const HWND notify = RaobMakeNotifyWindow();
    REQUIRE(notify != NULL);
    //对照：带同样两个开关的录像录制条
    screenrecord::CRecordBar video;
    video.SetAudioToggles(true, true, true);
    video.Show(kRaobAnchor, notify, 0);
    REQUIRE(video.IsWindow());
    const int videoW = RaobWidth(video);
    video.Hide();

    screenrecord::CRecordBar bar;
    bar.SetAudioOnly(true);
    bar.SetPenAvailable(false);
    bar.SetAudioToggles(true, true, false);
    bar.SetCameraToggle(false, false);
    bar.Show(kRaobAnchor, notify, 0);
    REQUIRE(bar.IsWindow());
    CHECK(bar.IsAudioOnly());
    CHECK(RaobWidth(bar) < videoW);
    CHECK(RaobFind(bar, screenrecord::CRecordBar::kIdMouse) == NULL);
    CHECK(RaobFind(bar, screenrecord::CRecordBar::kIdCamera) == NULL);
    REQUIRE(RaobFind(bar, screenrecord::CRecordBar::kIdMic) != NULL);
    REQUIRE(RaobFind(bar, screenrecord::CRecordBar::kIdSystem) != NULL);
    balloonwjui::DuiProgressBar* micLevel =
        dynamic_cast<balloonwjui::DuiProgressBar*>(RaobFind(bar, screenrecord::CRecordBar::kIdMicLevel));
    balloonwjui::DuiProgressBar* systemLevel =
        dynamic_cast<balloonwjui::DuiProgressBar*>(RaobFind(bar, screenrecord::CRecordBar::kIdSystemLevel));
    REQUIRE(micLevel != NULL);
    REQUIRE(systemLevel != NULL);
    CHECK(micLevel->GetPos() == 0);
    //电平条在开关的正下方、同一列
    const RECT micRc = RaobFind(bar, screenrecord::CRecordBar::kIdMic)->GetRect();
    const RECT levelRc = micLevel->GetRect();
    CHECK(levelRc.top >= micRc.bottom);
    CHECK(levelRc.left == micRc.left);
    CHECK(levelRc.right == micRc.right);

    bar.SetLevels(kRaobMicLevel, kRaobSystemLevel);
    CHECK(micLevel->GetPos() == kRaobMicLevel);
    CHECK(systemLevel->GetPos() == kRaobSystemLevel);

    bar.SetState(kRaobElapsedMs, false);
    CHECK(bar.StatusText() == TRF(L"录音中 {0}", std::wstring(L"01:05")));
    bar.SetState(kRaobElapsedMs, true);
    CHECK(bar.StatusText() == TRF(L"已暂停 {0}", std::wstring(L"01:05")));
    bar.Hide();
    //关闭后刷新电平是安全的（指针已清空）
    bar.SetLevels(1, 1);
    ::DestroyWindow(notify);
}

TEST_CASE("RAOB2 video record bar has no level bars", "[recordbar]")
{
    const HWND notify = RaobMakeNotifyWindow();
    REQUIRE(notify != NULL);
    screenrecord::CRecordBar bar;
    //先以只录声音的形态显示一次，再按控制器收尾的顺序恢复成录像形态
    bar.SetAudioOnly(true);
    bar.SetAudioToggles(true, true, true);
    bar.Show(kRaobAnchor, notify, 0);
    bar.Hide();
    bar.SetAudioOnly(false);

    bar.SetPenAvailable(true);
    bar.SetAudioToggles(true, true, true);
    bar.Show(kRaobAnchor, notify, 0);
    REQUIRE(bar.IsWindow());
    CHECK_FALSE(bar.IsAudioOnly());
    CHECK(RaobFind(bar, screenrecord::CRecordBar::kIdMouse) != NULL);
    CHECK(RaobFind(bar, screenrecord::CRecordBar::kIdMicLevel) == NULL);
    CHECK(RaobFind(bar, screenrecord::CRecordBar::kIdSystemLevel) == NULL);
    bar.SetState(kRaobElapsedMs, false);
    CHECK(bar.StatusText() == TRF(L"录制中 {0}", std::wstring(L"01:05")));
    //没有电平条时刷新电平什么也不做
    bar.SetLevels(kRaobMicLevel, kRaobSystemLevel);
    bar.Hide();
    ::DestroyWindow(notify);
}

TEST_CASE("RAOB3 audio-only button needs at least one sound source", "[recordbar]")
{
    CHECK(screenshare::SharePickerCanRecordAudioOnly(true, false));
    CHECK(screenshare::SharePickerCanRecordAudioOnly(false, true));
    CHECK(screenshare::SharePickerCanRecordAudioOnly(true, true));
    CHECK_FALSE(screenshare::SharePickerCanRecordAudioOnly(false, false));
}
