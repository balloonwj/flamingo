/**
 *  观看端控制对方电脑时鼠标按钮的记录与「要不要发送」的判断（纯逻辑），ViewerMouseButtons.h
 *
 *  远程桌面观看窗（RdMouseMapper）与屏幕共享观看窗（ShareViewerInputTranslator）都要决定一条鼠标消息发不发给对方，
 *  2026-10-14 之前两边各写一份、规则不同：屏幕共享在画面内按下、拖到画面外时移动不发。现在统一按远程桌面的做法
 *  （docs/远程桌面控制权限与观看工具条方案.md 8.2 第 1 条），坐标换算仍由两边各自做（远程桌面换成被控端像素，
 *  屏幕共享换成归一化坐标），屏幕共享的移动限频也保留在它那一侧。规则：
 *    - 按下（双击按按下处理）：画面内才发，并记下这个按钮；画面外按下不发、不记；
 *    - 松开：记过的按钮在哪里松开都发（画面外时由调用方把坐标夹到画面边缘），没记过的按钮只在画面内松开时发；
 *    - 移动：画面内照发；有按钮按住时画面外的移动也发（夹到边缘）；
 *    - 滚轮等其余消息：只在画面内发。
 *  单元测试见 Source/Tests/ViewerMouseButtonsTests.cpp（MBT1～MBT3，标签 [mousebuttons]）。
 *
 *  典型用法：
 *      const bool inside = (::PtInRect(&picture, pt) != FALSE);
 *      if (m_buttons.Decide(msg, inside)) 换算坐标（画面外时夹到边缘）并发送;
 *
 *  balloonwj@qq.com   2026-10-14
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <vector>

namespace screenshare {

//鼠标按钮位：ViewerMouseButtons::ButtonsDown 的返回值由它们组合而成
const int kViewerButtonLeft = 1;     //左键
const int kViewerButtonRight = 2;    //右键
const int kViewerButtonMiddle = 4;   //中键

/**
 *  按下类消息（含双击）对应的按钮位。
 *  @param msg 窗口消息
 *  @return kViewerButtonLeft / Right / Middle；不是按下类消息返回 0
 */
int ViewerButtonOfDown(UINT msg);

/**
 *  松开类消息对应的按钮位。
 *  @param msg 窗口消息
 *  @return kViewerButtonLeft / Right / Middle；不是松开类消息返回 0
 */
int ViewerButtonOfUp(UINT msg);

//观看端在画面内按下、还没松开的鼠标按钮，以及一条鼠标消息要不要发
class ViewerMouseButtons
{
public:
    ViewerMouseButtons();

    /**
     *  判断一条鼠标消息要不要发给对方，并更新按下的按钮记录。规则见文件头。
     *  @param msg    鼠标消息（WM_MOUSEMOVE、WM_xBUTTONDOWN / UP / DBLCLK、WM_MOUSEWHEEL 等）
     *  @param inside 光标是否在画面矩形内
     *  @return 要发送返回 true；画面外的消息要发送时，调用方须把坐标夹到画面边缘
     */
    bool Decide(UINT msg, bool inside);

    //忘掉按下的按钮（失去控制、会话结束时）
    void Reset();

    //在画面内按下、还没松开的按钮（位组合，见 kViewerButtonLeft 等）；没有时为 0
    int ButtonsDown() const;

    /**
     *  还没松开的按钮对应的松开消息，失去控制时调用方据此补发松开。
     *  @return WM_LBUTTONUP / WM_RBUTTONUP / WM_MBUTTONUP，按左、右、中的顺序；没有按住的按钮时为空
     */
    std::vector<uint32_t> PendingButtonUps() const;

private:
    int m_buttonsDown;   //在画面内按下、还没松开的按钮（位组合）
};

} // namespace screenshare
