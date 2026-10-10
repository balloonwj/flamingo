/**
 *  ChatScreenshotMenu.h
 *
 *  单聊窗与群聊窗共用的截图入口小件（docs/截图快捷键与隐藏当前窗口方案.md 第 2 节）：
 *    · 聊天窗投给自己的截图请求编号（kDoScreenshotMsg 的 wParam）；
 *    · 工具栏剪刀右边 ▾ 弹出的截图选项菜单：「截图（Alt+Z）」「截取全屏」、分隔线、
 *      勾选项「截图时隐藏当前窗口」。菜单内容由 BuildScreenshotMenu 搭建（用例 SK6），
 *      弹出与结果换算由 TrackScreenshotMenu 完成，勾选项改了之后由调用方写配置。
 *
 *  典型用法（聊天窗收到 kShotReqMenu 时）：
 *      balloonscreenshot::ScreenshotPrefs prefs = balloonscreenshot::LoadGlobalScreenshotPrefs();
 *      balloonchatwnd::ScreenshotMenuChoice c = balloonchatwnd::TrackScreenshotMenu(
 *          m_hWnd, cutBtnScreenRect, prefs.m_hotkey, prefs.m_hideWindow);
 *  截图模块不可用时隐藏剪刀与 ▾（2026-10-07）：
 *      balloonchatwnd::ApplyScreenshotButtons(root, kIdTbScreenshot, kIdTbScreenshotMenu,
 *                                             balloonscreenshot::IsScreenshotAvailable());
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <windows.h>

namespace balloonwjui {
class DuiMenu;
class DuiControl;
}

namespace balloonchatwnd {

// 聊天窗投给自己的截图请求，作为 kDoScreenshotMsg 的 wParam。三种请求共用一个窗口私有
// 消息号：都要等当前的 DUI 鼠标事件处理完再执行（模态截图界面或菜单不能在鼠标事件的
// 同步派发栈里启动），而且同一时刻只会有一个。
enum ChatScreenshotRequest
{
    kShotReqCapture    = 0,   // 普通截图：点了剪刀、菜单选了「截图」，或主面板转来的截图快捷键
    kShotReqFullScreen = 1,   // 截取全屏：菜单选了「截取全屏」，选区直接为鼠标所在的整块显示器
    kShotReqMenu       = 2,   // 弹出截图选项菜单：点了剪刀右边的 ▾
};

// 截图选项菜单的选择结果
enum ScreenshotMenuChoice
{
    kShotMenuNone = 0,     // 没有选（点到菜单外或按了 Esc）
    kShotMenuCapture,      // 选了「截图」
    kShotMenuFullScreen,   // 选了「截取全屏」
    kShotMenuToggleHide,   // 点了「截图时隐藏当前窗口」，调用方把该设置取反并保存
};

// 截图选项菜单各项的 id（TrackPopup 的返回值）
const UINT kShotMenuIdCapture    = 1;   // 「截图（Alt+Z）」
const UINT kShotMenuIdFullScreen = 2;   // 「截取全屏」
const UINT kShotMenuIdHideWindow = 3;   // 勾选项「截图时隐藏当前窗口」

/**
 *  搭建截图选项菜单的内容（不弹出）。
 *  @param menu        要填充的菜单（调用方持有）；原有内容会先被清空
 *  @param hotkey      当前的截图快捷键编码（见 Feature/Screenshot/ScreenshotHotkey.h），0 表示停用
 *  @param hideChecked 「截图时隐藏当前窗口」当前是否开启
 */
void BuildScreenshotMenu(balloonwjui::DuiMenu& menu, DWORD hotkey, bool hideChecked);

/**
 *  把菜单返回的 id 换算成选择结果。
 *  @param id TrackPopup 的返回值；0 表示没有选
 *  @return 对应的 ScreenshotMenuChoice；不认识的 id 当作没有选
 */
ScreenshotMenuChoice ScreenshotMenuChoiceFromId(UINT id);

/**
 *  在剪刀与 ▾ 下方弹出截图选项菜单，阻塞到用户选择或关闭菜单。
 *  @param owner        菜单的所有者窗口（聊天窗）
 *  @param anchorScreen 剪刀与 ▾ 两个按钮合起来的屏幕矩形；菜单左边与它的左边对齐、贴在它下方，
 *                      下方放不下时由 DuiMenu 自动翻到上方
 *  @param hotkey       同 BuildScreenshotMenu
 *  @param hideChecked  同 BuildScreenshotMenu
 *  @return 用户的选择
 */
ScreenshotMenuChoice TrackScreenshotMenu(HWND owner, const RECT& anchorScreen, DWORD hotkey, bool hideChecked);

/**
 *  按截图功能是否可用显示或隐藏工具栏的剪刀与 ▾ 两个按钮（docs/截图独立模块方案.md 3.3 节：截图模块
 *  FlamingoScreenshot.dll 没有加载成功时隐藏），并对按钮所在的工具栏强制重排，隐藏时不留空位。
 *  @param root      聊天窗的客户区根控件，可为 NULL
 *  @param shotId    剪刀按钮的编号（kIdTbScreenshot / kGIdTbScreenshot），不可为 0
 *  @param menuId    ▾ 按钮的编号（kIdTbScreenshotMenu / kGIdTbScreenshotMenu），不可为 0
 *  @param available 截图功能是否可用（balloonscreenshot::IsScreenshotAvailable）
 *  @return 找到剪刀按钮并按 available 设好时返回 available；根控件为空、编号为 0 或找不到剪刀按钮时返回 false
 */
bool ApplyScreenshotButtons(balloonwjui::DuiControl* root, UINT shotId, UINT menuId, bool available);

} // namespace balloonchatwnd
