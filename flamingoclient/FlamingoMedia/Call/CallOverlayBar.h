/**
 *  视频通话浮在画面底部、可淡入淡出的操作条，CallOverlayBar.h
 *
 *  docs/音视频通话-界面美化方案.md 第 3.4 节（2026-10-06 实现）。balloonui 没有能让控件互相叠放的容器，本控件仿照
 *  DuiToast 的做法：在父容器（通话窗的根 DuiVBox）里以 Fixed(0) 加入、不占纵向空间，在自己的 Layout 里改按父容器的
 *  整个矩形定位，贴在底部、高 m_height，于是浮在先加入的大画面之上（父容器按子控件顺序绘制，后加入的在上层）。
 *
 *  它就是一个 DuiHBox：按钮、信号图标、通话时长等子控件照常用布局提示加入。只覆写三个虚函数：
 *    - Layout：如上，按父容器矩形贴底定位后，再交给 DuiHBox 排子控件；
 *    - OnPaint：先把底下已画好的画面复制到内存位图，叠一层由透明到半透明黑色的渐变（让浅色按钮在亮画面上也看得清），
 *      再画子控件，最后按当前不透明度混合回窗口。不透明度由通话窗按 CallBarAutoHide 的时序设置；
 *    - HitTest：完全透明时不参与命中，鼠标直接落到底下的画面上。
 *
 *  只在视频态使用；语音态的底部栏不浮在画面上，仍是普通的 DuiHBox。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <windows.h>

#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"

namespace call {

//渐变底最下沿的不透明度（0~255）：上沿完全透明，越往下越暗
const int kCallOverlayShadeAlpha = 150;

/**
 *  浮在画面底部、可淡入淡出的操作条。线程：只在 UI 线程上使用。
 */
class CallOverlayBar : public balloonwjui::DuiHBox
{
public:
    /**
     *  @param heightPx 操作条的高（像素），贴在父容器底部
     */
    explicit CallOverlayBar(int heightPx);

    //析构：释放绘制用的内存位图
    ~CallOverlayBar();

    /**
     *  设置不透明度并重绘；与当前值相同时什么也不做。
     *  @param alpha 0（完全隐藏，且不参与命中）~ 255（完全显示），超出范围按边界处理
     */
    void SetBarAlpha(int alpha);

    //当前不透明度
    int BarAlpha() const { return m_alpha; }

    //按父容器的整个矩形贴底定位（忽略父布局给的 Fixed(0) 矩形），再排子控件；没有父容器时按 rcAvail 排
    void Layout(const RECT& rcAvail) override;

    //先复制底下的画面，再画渐变底与子控件，最后按不透明度混合回 hdc
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    //完全透明时返回 nullptr，鼠标落到底下的画面上
    balloonwjui::DuiControl* HitTest(POINT pt) override;

private:
    CallOverlayBar(const CallOverlayBar&);
    CallOverlayBar& operator=(const CallOverlayBar&);

    /**
     *  确保内存位图至少有 widthPx × heightPx 大（从客户区原点起算，子控件按客户区坐标直接画进去，不做坐标换算）。
     *  @return 内存设备上下文；创建失败返回 NULL
     */
    HDC EnsureMemDc(HDC ref, int widthPx, int heightPx);

    //释放内存位图
    void ReleaseMemDc();

    //在 dc 的 rc 区域画由透明到半透明黑色的竖直渐变
    static void DrawShade(HDC dc, const RECT& rc);

private:
    int      m_height;   //操作条的高（像素）
    int      m_alpha;    //当前不透明度（0~255）
    HDC      m_memDc;    //绘制用的内存设备上下文（本对象持有）
    HBITMAP  m_memBmp;   //选入 m_memDc 的位图（本对象持有）
    HGDIOBJ  m_memOld;   //m_memDc 原来选入的位图，释放前换回
    int      m_memW;     //m_memBmp 的宽（像素）
    int      m_memH;     //m_memBmp 的高（像素）
};

} // namespace call
