/**
 *  SettingsBinding.h
 *
 *  "系统设置"三页里下拉框 / 开关与 CUserConfig 之间的<u>纯映射逻辑</u>，以及各下拉
 *  框的选项文本表。把"下拉索引 ↔ 配置布尔值"、"提示音 ↔ 静音取反"这类容易写反
 *  的转换抽成无副作用的纯函数，便于被 FlamingoUnit 直接断言；对话框侧只管把
 *  控件值喂进来 / 取出来，不再各处手写转换。
 *
 *  设计稿：design/系统设置1.png（通用）、系统设置3.png（聊天）的下拉框选项。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#pragma once

#include <windows.h>      // wchar_t 常量串类型一致；本头不依赖 GDI/HWND

namespace balloonsettings {

// =====================================================================
// 关闭主面板时（design 系统设置1.png：下拉「最小化到托盘 / 退出」）
// 对应 CUserConfig::IsEnableExitWhenCloseMainDlg（TRUE=退出 / FALSE=最小化）。
// =====================================================================

/**
 *  下拉选项索引。顺序即下拉项顺序。
 */
enum CloseActionIndex
{
    kCloseActionMinimize = 0,   // 最小化到系统托盘（保留后台）
    kCloseActionExit     = 1,   // 关闭即退出程序
};

// 选项文本表首元素指针 + 项数（供对话框填下拉、供测试断言）。
const wchar_t* const* CloseActionOptions();
int                   CloseActionOptionCount();

/**
 *  配置布尔 → 下拉索引。
 *    exitWhenClose：true=关闭即退出；false=最小化到托盘。
 *  返回：kCloseActionExit(1) 或 kCloseActionMinimize(0)。
 */
int  CloseActionIndexFromExitWhenClose(bool exitWhenClose);

/**
 *  下拉索引 → 配置布尔。
 *    index：下拉选中索引。
 *  返回：是否"关闭即退出"（index==kCloseActionExit 时 true，其余 false）。
 */
bool ExitWhenCloseFromCloseActionIndex(int index);

// =====================================================================
// 发送消息快捷键（design 系统设置3.png：下拉「Enter / Ctrl+Enter」）
// 对应 CUserConfig::IsEnablePressEnterToSend（TRUE=Enter 发送）。
// =====================================================================

/**
 *  下拉选项索引。
 */
enum SendKeyIndex
{
    kSendKeyEnter     = 0,   // Enter 直接发送（换行用 Shift+Enter）
    kSendKeyCtrlEnter = 1,   // Ctrl+Enter 发送（Enter 换行）
};

const wchar_t* const* SendKeyOptions();
int                   SendKeyOptionCount();

/**
 *  配置布尔 → 下拉索引。
 *    pressEnterToSend：true=Enter 发送；false=Ctrl+Enter 发送。
 *  返回：kSendKeyEnter(0) 或 kSendKeyCtrlEnter(1)。
 */
int  SendKeyIndexFromPressEnter(bool pressEnterToSend);

/**
 *  下拉索引 → 配置布尔。
 *    index：下拉选中索引。
 *  返回：是否"Enter 直接发送"（index==kSendKeyEnter 时 true，其余 false）。
 */
bool PressEnterFromSendKeyIndex(int index);

// =====================================================================
// 自动离开的等待时长（通用页：下拉「5 / 10 / 15 / 30 分钟」）
// 对应 CUserConfig::GetAutoAwayDelayIndex —— 存的是<u>索引</u>不是分钟数，
// 与语言等下拉项同口径。分钟数只在这里映射一次，别处不再写死。
// =====================================================================

/**
 *  下拉选项索引。顺序即下拉项顺序，取值必须与 AutoAwayDelayMinutesFromIndex 一一对上。
 */
enum AutoAwayDelayIndex
{
    kAutoAwayDelay5Min  = 0,   // 空闲 5 分钟后切到离开（默认）
    kAutoAwayDelay10Min = 1,   // 空闲 10 分钟
    kAutoAwayDelay15Min = 2,   // 空闲 15 分钟
    kAutoAwayDelay30Min = 3,   // 空闲 30 分钟
};

// 选项文本表首元素指针 + 项数（供对话框填下拉、供测试断言）。
const wchar_t* const* AutoAwayDelayOptions();
int                   AutoAwayDelayOptionCount();

/**
 *  把等待时长索引夹到合法区间 [0, AutoAwayDelayOptionCount())。
 *    index：任意取值，含配置文件被手工改坏后读出来的越界值。
 *  返回：越界 / 负数一律回落到默认档 kAutoAwayDelay5Min(0)。
 */
int ClampAutoAwayDelayIndex(int index);

/**
 *  下拉索引 → 等待分钟数。
 *    index：下拉选中索引；越界值先经 ClampAutoAwayDelayIndex 归一，故一定有返回值。
 *  返回：5 / 10 / 15 / 30 之一（分钟）。调用方自行乘 60 * 1000 换成毫秒阈值。
 */
int AutoAwayDelayMinutesFromIndex(int index);

// =====================================================================
// 提示音 ↔ 静音（design 系统设置2.png：开关「提示音」对应 CUserConfig 的静音，取反）
// =====================================================================

/**
 *  静音标志 → 提示音开关。提示音开 = 未静音。
 *    mute：true=已静音。
 *  返回：提示音是否开启（= !mute）。
 */
bool SoundOnFromMute(bool mute);

/**
 *  提示音开关 → 静音标志。
 *    soundOn：true=提示音开启。
 *  返回：是否静音（= !soundOn）。
 */
bool MuteFromSoundOn(bool soundOn);

} // namespace balloonsettings
