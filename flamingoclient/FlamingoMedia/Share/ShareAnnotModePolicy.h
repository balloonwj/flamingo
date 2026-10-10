/**
 *  标注模式下的输入面与工具条锚点，ShareAnnotModePolicy.h
 *
 *  屏幕共享的标注有几处可能接住鼠标的「输入面」：主持人盖在共享内容上的输入层、观看窗的画面、共享者盖在自己共享内容上的
 *  输入层，以及本人白板窗口里的画布（画布总是接收鼠标，控制器按是否在标注决定画不画）。以前 setAnnotMode 在主持人一侧
 *  无条件打开共享内容上的输入层，主持人在自己的白板上标注时，这层几乎透明的置顶窗口盖住整块屏幕，白板窗口的标题栏被
 *  压在下面拖不动（2026-10-07 用户实测）。本文件用一个纯函数给出各输入面的开关与标注工具条的锚点，控制器照着执行：
 *    - 在本人的白板上标注：不打开任何屏幕输入层，工具条锚在白板窗口上；
 *    - 「鼠标」状态（标注工具条最左边的按钮，2026-10-07）：标注模式与工具条保留，输入面全部放开，点击穿透到下面的程序；
 *      再选任意画图工具时回到标注；
 *    - 主持人在「其他人的屏幕」窗口里对别人的画面标注（2026-10-07，方案第 14.3 节第 4 条）：不打开共享内容上的输入层，
 *      由那个窗口的画面接住标注输入，工具条锚在那个窗口上。
 *  单元测试见 Source/Tests/ShareAnnotModeTests.cpp（标签 [annotmode]）。
 *
 *  典型用法（CShareController::setAnnotMode）：
 *      const ShareAnnotSurfacePlan plan = DecideAnnotSurfaces(on, hosting, watching, ownScreen, onBoard, pointerMode);
 *      m_annotOverlay.SetInputMode(plan.m_hostOverlayInput, this);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <windows.h>   // LPCWSTR、IDC_* 系统光标名

namespace screenshare {

//标注工具条的锚点
enum ShareAnnotAnchor
{
    annot_anchor_none        = 0,   //不在标注模式，没有工具条
    annot_anchor_host_source = 1,   //主持人共享的内容
    annot_anchor_own_source  = 2,   //共享者自己共享的内容（多人共享）
    annot_anchor_board       = 3,   //本人的白板窗口
    annot_anchor_viewer      = 4,   //观看窗
    annot_anchor_screens     = 5,   //主持人的「其他人的屏幕」窗口（2026-10-07）
};

//标注模式下各输入面的开关与工具条锚点
struct ShareAnnotSurfacePlan
{
    bool m_hostOverlayInput;      //主持人共享内容上的输入层是否接住鼠标
    bool m_viewerCanvasInput;     //观看窗的画面是否接住标注输入
    bool m_publishOverlayInput;   //共享者自己共享内容上的输入层是否接住鼠标
    bool m_boardInput;            //本人白板窗口的画布上的鼠标动作是否当作标注
    bool m_screensCanvasInput;    //主持人「其他人的屏幕」窗口的画面是否接住标注输入（2026-10-07）
    int  m_anchor;                //标注工具条的锚点，取值见 ShareAnnotAnchor

    ShareAnnotSurfacePlan();
};

/**
 *  算出标注模式下各输入面的开关与工具条锚点。
 *  @param on          是否在标注模式
 *  @param hosting     本人是否以主持人身份在推流
 *  @param watching    本人是否在观看（观看窗开着）
 *  @param ownScreen   是否是共享者在自己共享的内容上标注（多人共享）
 *  @param onBoard     是否在本人的白板上标注（白板窗口开着）
 *  @param pointerMode 是否处于「鼠标」状态：保留工具条与标注，但不拦截鼠标
 *  @param onScreens   主持人是否在「其他人的屏幕」窗口里标注（2026-10-07）；只在 hosting 时有效，白板优先于它
 *  @return 各输入面的开关与锚点；不在标注模式时全部关闭、锚点为 annot_anchor_none
 */
ShareAnnotSurfacePlan DecideAnnotSurfaces(bool on, bool hosting, bool watching, bool ownScreen, bool onBoard,
                                          bool pointerMode, bool onScreens = false);

/**
 *  选择工具的系统光标（2026-10-07）：悬停在标注对象（或单选对象的句柄）上时是四向移动光标，空白处是十字光标（可以框选）；
 *  原先一律是系统箭头，看起来与普通鼠标一样。
 *  @param overObject 光标下是否有对象
 *  @return 系统光标的资源名（交给 LoadCursor(NULL, ...)）
 */
LPCWSTR AnnotSelectCursorName(bool overObject);

} // namespace screenshare
