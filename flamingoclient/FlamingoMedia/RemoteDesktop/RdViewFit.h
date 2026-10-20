/**
 *  远程桌面观看端的等比显示与鼠标坐标换算（纯逻辑），RdViewFit.h
 *
 *  2026-10-12 之前观看窗把被控端整屏画面拉伸铺满画面控件，两边宽高比不同时画面变形，鼠标坐标也按拉伸后的比例换算
 *  （docs/远程桌面控制权限与观看工具条方案.md 第 3.2 节）。现在改为等比显示、四周留黑边：
 *    - ComputeRdFitRect：画面按宽高比放进画面控件，居中，算出画面实际所占的矩形；
 *    - RdMouseMapper：鼠标坐标先扣除黑边再换算成被控端的绝对像素。黑边上的移动、按下与滚轮不发送；在画面内按下、
 *      拖到画面外松开时，按住期间的移动与最后的松开都夹到画面边缘照发，被控端不会出现按下了没有松开的按钮。
 *    - MapRdRemoteToView：被控端的指针位置换到画面矩形里，观看窗在那里画出远端指针（画面本身不含指针）。
 *  单元测试见 Source/Tests/RdViewFitTests.cpp（RES1、RES2、RES5）。
 *
 *  典型用法（观看窗里）：
 *      const RECT fit = ComputeRdFitRect(canvasRect, decoder.width(), decoder.height());
 *      int rx = 0, ry = 0;
 *      if (m_mouse.Map(uMsg, pt, fit, decoder.width(), decoder.height(), rx, ry)) 编码并发送;
 *
 *  balloonwj@qq.com   2026-10-12
 */
#pragma once

#include <windows.h>
#include "../Share/ViewerMouseButtons.h"   // ViewerMouseButtons：观看端鼠标按钮的记录与是否发送（与屏幕共享共用）

namespace remotedesktop {

/**
 *  画面按宽高比放进 area 后实际所占的矩形（居中，四周黑边）。
 *  @param area   画面控件的矩形
 *  @param frameW 画面宽（被控端像素）
 *  @param frameH 画面高（被控端像素）
 *  @return 画面矩形；画面或区域的尺寸不大于 0 时返回 area 本身
 */
RECT ComputeRdFitRect(const RECT& area, int frameW, int frameH);

/**
 *  把画面矩形里的一点换算成被控端的绝对像素，超出画面时夹到边缘。
 *  @param pt     点（与 fit 同一坐标系）
 *  @param fit    画面矩形（ComputeRdFitRect 的结果）
 *  @param frameW 画面宽（被控端像素）
 *  @param frameH 画面高（被控端像素）
 *  @param remoteX [出参] 被控端横坐标，0 ~ frameW-1
 *  @param remoteY [出参] 被控端纵坐标，0 ~ frameH-1
 */
void MapRdPointClamped(POINT pt, const RECT& fit, int frameW, int frameH, int& remoteX, int& remoteY);

/**
 *  被控端的指针位置换到画面矩形里（2026-10-12，观看窗据此画出远端指针）。
 *  @param remote 指针位置（被控端主屏坐标，像素）
 *  @param frameW 画面宽（被控端像素）
 *  @param frameH 画面高（被控端像素）
 *  @param fit    画面矩形（ComputeRdFitRect 的结果）
 *  @param out    [出参] 指针在观看端的位置（与 fit 同一坐标系）
 *  @return 指针在被采集的主屏上返回 true；在别的显示器上（坐标超出主屏）或尺寸未知时返回 false，不画
 */
bool MapRdRemoteToView(POINT remote, int frameW, int frameH, const RECT& fit, POINT& out);

//观看窗的鼠标换算：扣除黑边，记住在画面内按下、还没松开的按钮
class RdMouseMapper
{
public:
    RdMouseMapper();

    /**
     *  判断一条鼠标消息要不要发给被控端，要发时算出被控端坐标。
     *  @param msg     鼠标消息（WM_MOUSEMOVE、WM_xBUTTONDOWN / UP / DBLCLK、WM_MOUSEWHEEL）
     *  @param pt      光标位置（与 fit 同一坐标系；滚轮消息由调用方先换成客户区坐标）
     *  @param fit     画面矩形
     *  @param frameW  画面宽（被控端像素）
     *  @param frameH  画面高（被控端像素）
     *  @param remoteX [出参] 被控端横坐标
     *  @param remoteY [出参] 被控端纵坐标
     *  @return 要发送返回 true：画面内的消息，以及画面内按下之后、在画面外的移动与松开（坐标夹到画面边缘）
     */
    bool Map(UINT msg, POINT pt, const RECT& fit, int frameW, int frameH, int& remoteX, int& remoteY);

    //忘掉按下的按钮（失去控制、会话结束时）
    void Reset();

    //在画面内按下、还没松开的按钮（位：1 左键、2 右键、4 中键；单元测试用）
    int ButtonsDown() const;

private:
    screenshare::ViewerMouseButtons m_buttons;   //在画面内按下、还没松开的按钮与「要不要发」的判断（与屏幕共享共用，2026-10-14）
};

} // namespace remotedesktop
