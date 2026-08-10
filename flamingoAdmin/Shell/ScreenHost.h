/**
 *  flamingoAdmin —— 内容区屏宿主 ScreenHost。
 *
 *  主界面外壳的内容区（顶栏下方那块）由本控件承载。它持有全部主界面
 *  （仪表板 / 企业管理 / 公告中心 / 审计日志 / 平台设置），任一时刻只
 *  显示其中一个。
 *
 *  关键设计——屏常驻、切换只切显隐：所有主界面一经 AddScreen 加入便常驻，
 *  切换时只改可见性、<u>绝不销毁</u>。原因与 RootContainer 一致：销毁一棵
 *  「宿主仍在跟踪（hover / 焦点）的控件树」会留下悬空指针 —— 而触发屏切换
 *  的控件（侧栏导航项、屏内的「查看全部」链接）可能正位于被切走的屏里。
 *  全程不销毁，从根本上杜绝按钮点击崩溃。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

#include "DuiControl.h"

#include <memory>
#include <vector>

namespace flamingoadmin {

// 内容区屏宿主。
class ScreenHost : public balloonwjui::DuiControl
{
public:
    // 加入一个主界面。加入时即隐藏；之后用 ShowScreen 切换显示。
    // 屏一经加入便常驻，不会被销毁。
    void AddScreen(std::unique_ptr<balloonwjui::DuiControl> screen);

    // 显示 screen、隐藏其它所有已加入的屏。切换过来的屏会在显示前按当前
    // 内容区尺寸补排一次布局（隐藏期间它不参与逐帧 Layout，见 Layout 注释）。
    //   screen：须是此前经 AddScreen 加入过的屏。
    void ShowScreen(balloonwjui::DuiControl* screen);

    // 当前显示的屏（未显示任何屏时为 nullptr）。
    balloonwjui::DuiControl* CurrentScreen() const { return m_current; }

    // 排布内容区。<u>只排当前显示的那个屏</u> —— 隐藏屏不逐帧参与布局，
    // 改由 ShowScreen 在切过去的那一刻补排，避免拖动改变窗口尺寸时每帧
    // 白做 5 份看不见的布局。
    //   rcAvail：内容区可用矩形（宿主客户区坐标）。
    void                     Layout(const RECT& rcAvail) override;
    void                     OnPaint(HDC hdc, const RECT& rcDirty) override;
    balloonwjui::DuiControl*  HitTest(POINT ptHostClient) override;

private:
    std::vector<balloonwjui::DuiControl*> m_screens;            // 借用（均在 m_children 中）
    balloonwjui::DuiControl*              m_current = nullptr;   // 当前显示的屏
};

} // namespace flamingoadmin
