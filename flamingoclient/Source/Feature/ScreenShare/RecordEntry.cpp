/**
 *  exe 侧的录屏入口工具，见 RecordEntry.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "RecordEntry.h"

#include "ScreenShareHub.h"
#include "../../UI/BalloonMain/MainPanelRecordMenu.h"
#include "../../Common/Log/IULog.h"

namespace balloonscreenshare {

bool ApplyRecordHotkey(HWND panel, bool enable)
{
    if (panel == NULL || !::IsWindow(panel))
        return false;
    ::UnregisterHotKey(panel, kRecordHotKeyId);
    if (!enable)
    {
        LOG_INFO("record hotkey: disabled.");
        return true;
    }
    if (!::RegisterHotKey(panel, kRecordHotKeyId, MOD_CONTROL | MOD_ALT, kRecordHotKey))
    {
        LOG_WARNING("record hotkey: register Ctrl+Alt+R failed, error=%u.", static_cast<unsigned>(::GetLastError()));
        return false;
    }
    LOG_INFO("record hotkey: Ctrl+Alt+R registered.");
    return true;
}

int CurrentRecordMenuState()
{
    const CScreenShareHub& hub = CScreenShareHub::GetInstance();
    if (!hub.IsRecordSupported())
        return balloonmain::kRecordMenuHidden;
    return hub.IsRecording() ? balloonmain::kRecordMenuStop : balloonmain::kRecordMenuStart;
}

void ToggleRecording()
{
    CScreenShareHub& hub = CScreenShareHub::GetInstance();
    if (!hub.IsRecordSupported())
    {
        LOG_INFO("record toggle ignored: module not ready or system not supported.");
        return;
    }
    if (hub.IsRecording())
    {
        LOG_INFO("record toggle: stopping.");
        hub.StopRecording();
        return;
    }
    hub.StartRecording(0, 0, CString());
}

void PushRecordSettings(int quality, const wchar_t* folder)
{
    CScreenShareHub::GetInstance().SetRecordSettings(quality, CString(folder != NULL ? folder : L""));
}

void PushMouseEffectSettings(const wchar_t* text)
{
    CScreenShareHub::GetInstance().SetMouseEffectSettings(CString(text != NULL ? text : L""));
}

} // namespace balloonscreenshare
