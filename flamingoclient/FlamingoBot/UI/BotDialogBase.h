/**
 *  机器人管理界面各窗口的共同基类 CBotDialogBase（DuiFrameWindow 的子类，只补几个建窗与摆放的辅助方法，不改消息处理）：
 *    - CreateFramed：按 UI 约定建窗——用 WS_OVERLAPPEDWINDOW，有无所有者控件通知都发回本窗口（第 11 条），标题栏左侧放程序图标（第 10 条），
 *      固定尺寸；之后取窗口 DPI 对应的界面字体，子类据此建控件树；
 *    - FinishFramed：装上控件树，按整窗尺寸 ResizeClient（第 7 条：尺寸含标题栏），摆在所有者窗口中央；
 *    - DisableOwner / RestoreOwner：编辑窗、规则编辑窗打开期间禁用所有者窗口（效果与模态对话框相同，但不嵌套消息循环），
 *      关闭前恢复并激活所有者。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（本工程的 stdafx.h）。
 *
 *  典型用法（子类的打开方法里）：
 *      if (!CreateFramed(owner, title, false))
 *          return false;
 *      std::unique_ptr<balloonwjui::DuiVBox> body = BuildBody(Font());
 *      FinishFramed(std::move(body), kWidth, kHeight, owner);
 *      DisableOwner(owner);
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <memory>
#include <string>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   //ProgramWindowIcons：标题栏的程序图标

namespace flamingobot {

/**
 *  CBotDialogBase —— 机器人管理界面窗口的共同基类。只在界面线程上使用。
 */
class CBotDialogBase : public balloonwjui::DuiFrameWindow
{
public:
    CBotDialogBase();
    virtual ~CBotDialogBase();

    /**
     *  把窗口提到前面（最小化时先还原）。窗口不存在时什么也不做。
     */
    void BringToFront();

protected:
    /**
     *  建窗口（还不显示）：WS_OVERLAPPEDWINDOW、固定尺寸、标题栏只有关闭按钮（minimizable 时另有最小化）、程序图标。
     *  @param owner       所有者窗口，可为 NULL；没有所有者时窗口进任务栏
     *  @param title       标题
     *  @param minimizable 是否显示最小化按钮
     *  @return 窗口建成返回 true
     */
    bool CreateFramed(HWND owner, const std::wstring& title, bool minimizable);

    /**
     *  装上控件树、设置整窗尺寸（含标题栏）、摆到所有者窗口中央。
     *  @param body   标题栏以下的控件树（所有权转入）
     *  @param width  整窗宽，单位：像素
     *  @param height 整窗高（含标题栏），单位：像素
     *  @param owner  所有者窗口，可为 NULL（此时摆在鼠标所在显示器的工作区中央）
     */
    void FinishFramed(std::unique_ptr<balloonwjui::DuiControl> body, int width, int height, HWND owner);

    //改窗口标题（标题栏上画的文字与系统界面里的窗口文字一起改）
    void UpdateTitle(const std::wstring& title);

    /**
     *  禁用所有者窗口（本窗口关闭前须调用 RestoreOwner）。
     *  @param owner 所有者窗口；为 NULL 或不是有效窗口时什么也不做
     */
    void DisableOwner(HWND owner);

    //恢复并激活 DisableOwner 禁用的所有者窗口；没有禁用过时什么也不做。可重复调用
    void RestoreOwner();

    //窗口 DPI 对应的界面字体（借用）；窗口建成之后有效
    HFONT Font() const;

private:
    //摆到所有者窗口中央；所有者不可见时摆在鼠标所在显示器的工作区中央，并保证整窗落在工作区内
    void CenterOnOwner(HWND owner);

private:
    balloonmain::ProgramWindowIcons m_programIcons;   //标题栏与任务栏的程序图标，本对象持有
    HWND                            m_disabledOwner;  //DisableOwner 禁用的所有者窗口；没有禁用时为 NULL
    HFONT                           m_font;           //界面字体（借用 balloonui 按 DPI 给的默认字体）；窗口建成之后有效
};

} // namespace flamingobot
