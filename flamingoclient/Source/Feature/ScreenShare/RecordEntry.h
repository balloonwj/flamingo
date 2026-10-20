/**
 *  exe 侧的录屏入口工具，RecordEntry.h
 *
 *  录屏本身在 FlamingoMedia.dll 里（docs/屏幕共享与录制方案.md 第 6 节），exe 只负责几个入口（第 2 节第 7 条）：
 *  聊天窗工具栏、主面板系统菜单、托盘菜单与全局快捷键 Ctrl+Alt+R；以及把用户设置（画质、保存目录）交给模块。
 *  这里放入口共用的几件小事：快捷键的注册与注销、菜单项状态的换算、按快捷键或菜单开始 / 停止录制、推送设置。
 *  全部经 CScreenShareHub 转给模块，只在 UI 线程调用。
 *
 *  典型用法：
 *      balloonscreenshare::ApplyRecordHotkey(m_hWnd, cfg.IsEnableRecordHotkey() != FALSE);
 *      balloonscreenshare::ToggleRecording();
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <windows.h>

namespace balloonscreenshare {

//「开始 / 停止录屏」全局快捷键的标识（主面板窗口上注册；1001 已归「显示 / 隐藏主面板」）
const int kRecordHotKeyId = 1002;

//该快捷键的主键，与 MOD_CONTROL | MOD_ALT 一起构成 Ctrl+Alt+R
const UINT kRecordHotKey = 'R';

/**
 *  按设置注册或注销录屏快捷键：先注销，enable 为 true 时再注册。
 *  @param panel  接收 WM_HOTKEY 的窗口（主面板），须是本线程创建的窗口
 *  @param enable 是否启用
 *  @return 不启用时返回 true；启用时注册成功返回 true，被别的程序占用等原因失败返回 false（已记日志）
 */
bool ApplyRecordHotkey(HWND panel, bool enable);

/**
 *  主面板菜单、托盘菜单「发起录制」一项现在的状态：取值见 balloonmain::RecordMenuState。
 *  模块没有加载或系统不支持时为不出现；正在录制时为「停止录制」。
 */
int CurrentRecordMenuState();

//菜单或快捷键触发：正在录制则停止并保存，否则开始独立录屏（不关联会话，录完在提示里选会话发送）
void ToggleRecording();

/**
 *  把录制设置交给模块（登录后、设置变化时调用）。
 *  @param quality 画质：0 标准，1 高清
 *  @param folder  保存目录；为空时用模块的默认目录
 */
void PushRecordSettings(int quality, const wchar_t* folder);

/**
 *  把屏幕共享清晰度交给模块（登录后、设置变化时调用；docs/远程桌面H264与共享画质提升方案.md 4.2）。
 *  @param quality 0 为 1080p、1 为 2K（默认）、2 为 4K
 */
void PushShareQuality(int quality);

/**
 *  把鼠标效果的设置交给模块（屏幕共享第 16 期，登录后调用）。
 *  @param text 账号配置里保存的设置文本；为空时模块按默认设置（只开光圈）
 */
void PushMouseEffectSettings(const wchar_t* text);

} // namespace balloonscreenshare
