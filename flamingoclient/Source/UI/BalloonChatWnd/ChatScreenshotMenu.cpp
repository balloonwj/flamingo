/**
 *  ChatScreenshotMenu.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "ChatScreenshotMenu.h"

#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"
#include "../../Feature/Screenshot/ScreenshotHotkey.h"   // ScreenshotCommandText：「截图（Alt+Z）」
#include "../../Common/I18n/I18n.h"                        // TR：菜单文字的多语言查找
#include "../../Common/Log/IULog.h"                        // LOG_INFO

namespace balloonchatwnd {

namespace {

// 菜单与按钮下边之间的间隙（像素）
const int kMenuGapPx = 4;

} // anonymous namespace

void BuildScreenshotMenu(balloonwjui::DuiMenu& menu, DWORD hotkey, bool hideChecked)
{
    menu.Clear();
    CString captureText = balloonscreenshot::ScreenshotCommandText(hotkey);
    menu.AppendItem(kShotMenuIdCapture, captureText);
    menu.AppendItem(kShotMenuIdFullScreen, TR(L"截取全屏"));
    menu.AppendSeparator();
    menu.AppendChecked(kShotMenuIdHideWindow, TR(L"截图时隐藏当前窗口"), hideChecked);
}

ScreenshotMenuChoice ScreenshotMenuChoiceFromId(UINT id)
{
    switch (id)
    {
    //「截图（Alt+Z）」
    case kShotMenuIdCapture:
        return kShotMenuCapture;
    //「截取全屏」
    case kShotMenuIdFullScreen:
        return kShotMenuFullScreen;
    //勾选项「截图时隐藏当前窗口」
    case kShotMenuIdHideWindow:
        return kShotMenuToggleHide;
    //0（没有选）或不认识的 id
    default:
        return kShotMenuNone;
    }
}

ScreenshotMenuChoice TrackScreenshotMenu(HWND owner, const RECT& anchorScreen, DWORD hotkey, bool hideChecked)
{
    balloonwjui::DuiMenu menu;
    BuildScreenshotMenu(menu, hotkey, hideChecked);
    // 落点超出工作区时由 DuiMenu 自己翻向上方 / 左方（UI 约定第 3 条），这里只给期望位置
    UINT chosen = menu.TrackPopup(anchorScreen.left, anchorScreen.bottom + kMenuGapPx, owner);
    ScreenshotMenuChoice choice = ScreenshotMenuChoiceFromId(chosen);
    LOG_INFO("chat screenshot menu: chosen id=%u, choice=%d.", chosen, (int)choice);
    return choice;
}

bool ApplyScreenshotButtons(balloonwjui::DuiControl* root, UINT shotId, UINT menuId, bool available)
{
    //编号 0 是「未设编号」：FindCtrlById(0) 会找到任意一个没设编号的控件，不能拿它当按钮
    if (root == NULL || shotId == 0 || menuId == 0)
        return false;
    balloonwjui::DuiControl* shot = root->FindCtrlById(shotId);
    if (shot == NULL)
        return false;
    shot->SetVisible(available);
    balloonwjui::DuiControl* menu = root->FindCtrlById(menuId);
    if (menu != NULL)
        menu->SetVisible(available);
    //SetVisible 不会重排：对按钮所在的工具栏按当前矩形强制重排（开发约定文档 UI 约定第 5 条）
    balloonwjui::DuiControl* parent = shot->GetParent();
    if (parent != NULL)
        parent->ForceLayout(parent->GetRect());
    return available;
}

} // namespace balloonchatwnd
