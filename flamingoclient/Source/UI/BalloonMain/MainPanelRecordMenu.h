/**
 *  主面板系统菜单与托盘菜单里「屏幕录制」一项的状态，MainPanelRecordMenu.h
 *
 *  两个菜单（MainPanelSystemMenu、MainPanelTrayMenu）都有这一项（docs/屏幕共享与录制方案.md 第 2 节第 7 条录屏入口）：
 *  录屏模块（FlamingoMedia.dll）没有加载或系统不支持时不出现；没在录制时为「屏幕录制」，录制中为「停止录制」。
 *  调用方按 IMediaModule::IsFeatureSupported / IsRecording 换算成这里的取值传给两个菜单的 Build 函数。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonmain {

//「发起录制」菜单项的状态（2026-10-05 起由「屏幕录制」改名，屏幕共享第 17 期）
enum RecordMenuState
{
    kRecordMenuHidden = 0,   //不出现：录屏模块没有加载或系统不支持
    kRecordMenuStart  = 1,   //「发起录制」：点了开始独立录屏
    kRecordMenuStop   = 2,   //「停止录制」：正在录制，点了停止并保存
};

//菜单项的文字：kRecordMenuStop 为「停止录制」，其余为「发起录制」
inline const wchar_t* RecordMenuLabel(int state)
{
    return state == kRecordMenuStop ? TR(L"停止录制") : TR(L"发起录制");
}

} // namespace balloonmain
