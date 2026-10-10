/**
 *  ChatRecordMenu.h
 *
 *  单聊窗与群聊窗工具栏「屏幕录制」按钮的下拉菜单（docs/语音字幕方案.md 第 2 节第 10 条：录像列表的入口之一）：
 *  点录制按钮时弹出，第一项为「开始录制…」（正在录制时为「停止录制」），录制管理窗口可用时再加一项「录制管理」。
 *  菜单内容由 BuildRecordMenu 搭建（用例 CRM1），弹出与结果换算由 TrackRecordMenu 完成。菜单不能在 DUI 鼠标事件的
 *  同步派发栈里弹，调用方先投递窗口私有消息、延后到鼠标事件之外再调 TrackRecordMenu（与截图选项菜单相同）。
 *
 *  典型用法（聊天窗收到 kOpenRecordMenuMsg 时）：
 *      balloonchatwnd::RecordMenuChoice c = balloonchatwnd::TrackRecordMenu(m_hWnd, btnScreenRect, recording, true);
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <windows.h>

namespace balloonwjui {
class DuiMenu;
}

namespace balloonchatwnd {

// 录制菜单的选择结果
enum RecordMenuChoice
{
    kRecordMenuNone    = 0,   // 没有选（点到菜单外或按了 Esc）
    kRecordMenuStart   = 1,   // 选了「开始录制…」：弹出选择要录制的内容
    kRecordMenuStop    = 2,   // 选了「停止录制」：停止并保存正在进行的录制
    kRecordMenuManager = 3,   // 选了「录制管理」：打开录制管理窗口
};

// 录制菜单各项的 id（TrackPopup 的返回值）
const UINT kRecordMenuIdStart   = 1;   // 「开始录制…」
const UINT kRecordMenuIdStop    = 2;   // 「停止录制」
const UINT kRecordMenuIdManager = 3;   // 「录制管理」

/**
 *  搭建录制菜单的内容（不弹出）。
 *  @param menu        要填充的菜单（调用方持有）；原有内容会先被清空
 *  @param recording   是否正在录制（决定第一项是「开始录制…」还是「停止录制」）
 *  @param withManager 是否加「录制管理」（录制管理窗口在 FlamingoMedia.dll 里，模块加载了才有）
 */
void BuildRecordMenu(balloonwjui::DuiMenu& menu, bool recording, bool withManager);

/**
 *  把菜单返回的 id 换算成选择结果。
 *  @param id TrackPopup 的返回值；0 表示没有选
 *  @return 对应的 RecordMenuChoice；不认识的 id 当作没有选
 */
RecordMenuChoice RecordMenuChoiceFromId(UINT id);

/**
 *  在录制按钮下方弹出录制菜单，阻塞到用户选择或关闭菜单。
 *  @param owner        菜单的所有者窗口（聊天窗）
 *  @param anchorScreen 录制按钮的屏幕矩形；菜单左边与它对齐、贴在它下方，下方放不下时由 DuiMenu 自动翻到上方
 *  @param recording    同 BuildRecordMenu
 *  @param withManager  同 BuildRecordMenu
 *  @return 用户的选择
 */
RecordMenuChoice TrackRecordMenu(HWND owner, const RECT& anchorScreen, bool recording, bool withManager);

} // namespace balloonchatwnd
