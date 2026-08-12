/**
 *  SettingsBinding.cpp —— 系统设置下拉/开关与配置之间的纯映射实现 + 选项文本表。
 *
 *  纯逻辑，与 FlamingoUnit 测试工程共用，<u>不</u> include stdafx.h，保持 PCH 无关。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#include "SettingsBinding.h"

namespace balloonsettings {

// ---- 选项文本表（静态常量，进程级生命周期）----------------------------
static const wchar_t* const kCloseActionOpts[] =
{
    L"最小化到托盘",   // kCloseActionMinimize
    L"退出程序",       // kCloseActionExit
};

static const wchar_t* const kSendKeyOpts[] =
{
    L"Enter",          // kSendKeyEnter
    L"Ctrl + Enter",   // kSendKeyCtrlEnter
};

static const wchar_t* const kLanguageOpts[] =
{
    L"简体中文",       // 本期仅一项（占位，暂不真正切换）
};

static const wchar_t* const kFontScaleOpts[] =
{
    L"小",             // kFontScaleSmall
    L"中",             // kFontScaleMedium
    L"大",             // kFontScaleLarge
};

static const wchar_t* const kAutoAwayDelayOpts[] =
{
    L"5 分钟",         // kAutoAwayDelay5Min（默认）
    L"10 分钟",        // kAutoAwayDelay10Min
    L"15 分钟",        // kAutoAwayDelay15Min
    L"30 分钟",        // kAutoAwayDelay30Min
};

// 各档对应的分钟数，逐项与 kAutoAwayDelayOpts 对齐（第 i 项文本 ↔ 第 i 项分钟数）。
// 分钟数只在这一处写死，别处一律经 AutoAwayDelayMinutesFromIndex 取。
static const int kAutoAwayDelayMinutes[] = { 5, 10, 15, 30 };

// ---- 关闭主面板时 ------------------------------------------------------

const wchar_t* const* CloseActionOptions()
{
    return kCloseActionOpts;
}

int CloseActionOptionCount()
{
    return (int)(sizeof(kCloseActionOpts) / sizeof(kCloseActionOpts[0]));
}

int CloseActionIndexFromExitWhenClose(bool exitWhenClose)
{
    return exitWhenClose ? kCloseActionExit : kCloseActionMinimize;
}

bool ExitWhenCloseFromCloseActionIndex(int index)
{
    return index == kCloseActionExit;
}

// ---- 发送消息快捷键 ----------------------------------------------------

const wchar_t* const* SendKeyOptions()
{
    return kSendKeyOpts;
}

int SendKeyOptionCount()
{
    return (int)(sizeof(kSendKeyOpts) / sizeof(kSendKeyOpts[0]));
}

int SendKeyIndexFromPressEnter(bool pressEnterToSend)
{
    return pressEnterToSend ? kSendKeyEnter : kSendKeyCtrlEnter;
}

bool PressEnterFromSendKeyIndex(int index)
{
    return index == kSendKeyEnter;
}

// ---- 语言 --------------------------------------------------------------

const wchar_t* const* LanguageOptions()
{
    return kLanguageOpts;
}

int LanguageOptionCount()
{
    return (int)(sizeof(kLanguageOpts) / sizeof(kLanguageOpts[0]));
}

int ClampLanguageIndex(int index)
{
    int count = LanguageOptionCount();
    if (index < 0 || index >= count)
    {
        return 0;
    }
    return index;
}

// ---- 字体大小 ----------------------------------------------------------

const wchar_t* const* FontScaleOptions()
{
    return kFontScaleOpts;
}

int FontScaleOptionCount()
{
    return (int)(sizeof(kFontScaleOpts) / sizeof(kFontScaleOpts[0]));
}

int ClampFontScaleIndex(int index)
{
    int count = FontScaleOptionCount();
    if (index < 0 || index >= count)
    {
        return kFontScaleMedium;   // 越界回落到默认「中」
    }
    return index;
}

// ---- 自动离开等待时长 --------------------------------------------------

const wchar_t* const* AutoAwayDelayOptions()
{
    return kAutoAwayDelayOpts;
}

int AutoAwayDelayOptionCount()
{
    return (int)(sizeof(kAutoAwayDelayOpts) / sizeof(kAutoAwayDelayOpts[0]));
}

int ClampAutoAwayDelayIndex(int index)
{
    int count = AutoAwayDelayOptionCount();
    if (index < 0 || index >= count)
    {
        return kAutoAwayDelay5Min;   // 越界回落到默认「5 分钟」
    }
    return index;
}

int AutoAwayDelayMinutesFromIndex(int index)
{
    // 先归一再取，故下标一定落在表内 —— 配置文件被手工改成 99 也不会越界读。
    return kAutoAwayDelayMinutes[ClampAutoAwayDelayIndex(index)];
}

// ---- 提示音 ↔ 静音 -----------------------------------------------------

bool SoundOnFromMute(bool mute)
{
    return !mute;
}

bool MuteFromSoundOn(bool soundOn)
{
    return !soundOn;
}

} // namespace balloonsettings
