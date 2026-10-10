/**
 *  PanelDockGeometry.h
 *
 *  主面板「贴边自动隐藏」的几何计算：判断窗口靠在了哪条屏幕边缘上、吸附到边缘后的
 *  矩形、收起后滑到屏幕外的矩形，以及收起后留在屏幕内、供鼠标触碰以展开的那条细条。
 *
 *  可以贴的边只有上、左、右三条（下边通常是任务栏，不参与）。一条边能不能贴还要看
 *  边外是什么：
 *    · 边外紧挨着另一台显示器时不能贴，否则收起的窗口会滑到那台显示器上，依然可见；
 *    · 任务栏在这一侧（工作区边缘与显示器边缘不重合）时不能贴，否则收起的窗口会压在
 *      任务栏上。
 *
 *  本文件全是纯函数，不读系统状态：各显示器的矩形与工作区、吸附阈值、细条宽度都由
 *  调用方传入，因此可以完全用单元测试覆盖（见 Tests/PanelDockGeometryTests.cpp）。
 *  所有矩形都是屏幕坐标、物理像素。
 *
 *  本模块被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程共用，在 Flamingo.vcxproj
 *  里需标记 PrecompiledHeader=NotUsing。
 *
 *  典型用法（用户拖动主面板松开鼠标时）：
 *      balloonmain::DockSnapResult r = balloonmain::DetectDockEdge(wndRect, monitors, snapPx);
 *      if (r.m_edge != balloonmain::kPanelDockNone)
 *      {
 *          //先吸附到边缘；之后收起时移到 ComputeDockHiddenRect 算出的位置
 *          MoveTo(r.m_snapped);
 *      }
 *
 *  balloonwj@qq.com   2026-10-02
 */

#pragma once

#include <windows.h>
#include <vector>

namespace balloonmain {

//主面板贴在哪条屏幕边缘上
enum PanelDockEdge
{
    //未贴边：窗口没有靠在可贴的边缘上，不会自动隐藏
    kPanelDockNone = 0,
    //贴在显示器上边：收起时向上滑出屏幕，只在屏幕顶部留一条细条
    kPanelDockTop,
    //贴在显示器左边：收起时向左滑出屏幕，只在屏幕左侧留一条细条
    kPanelDockLeft,
    //贴在显示器右边：收起时向右滑出屏幕，只在屏幕右侧留一条细条
    kPanelDockRight
};

//一台显示器的几何信息，屏幕坐标、物理像素
struct DockMonitor
{
    //显示器整块矩形（GetMonitorInfo 的 rcMonitor）
    RECT m_monitor;
    //工作区，即去掉任务栏后的矩形（GetMonitorInfo 的 rcWork）
    RECT m_work;
};

//贴边判定的结果
struct DockSnapResult
{
    //贴在哪条边；为 kPanelDockNone 时其余两个字段无意义
    PanelDockEdge m_edge;
    //吸附到边缘后的窗口矩形，也就是展开状态下窗口应在的位置。尺寸与原窗口相同
    RECT m_snapped;
    //所贴的那台显示器的工作区。收起与细条的计算都以它为准
    RECT m_work;

    DockSnapResult()
        : m_edge(kPanelDockNone)
    {
        ::SetRectEmpty(&m_snapped);
        ::SetRectEmpty(&m_work);
    }
};

//吸附阈值（逻辑像素）：窗口的上 / 左 / 右边缘离屏幕边缘不超过这个距离就算贴边，
//运行时按窗口所在显示器的 DPI 换算成物理像素再传给 DetectDockEdge
const int kDockSnapLogicalPx = 10;

//收起后留在屏幕内的细条宽度（逻辑像素），运行时同样按 DPI 换算。
//太窄鼠标不容易碰到，太宽又会挡住屏幕边缘的内容
const int kDockStripLogicalPx = 4;

/**
 *  判断窗口是否靠在了某条可贴的屏幕边缘上，并算出吸附后的矩形。
 *
 *  规则：
 *    · 以与窗口重叠面积最大的那台显示器为准，窗口不与任何显示器重叠时不贴边；
 *    · 窗口的上边缘离工作区上边缘的距离不超过 snapPx 时贴上边，越过边缘（窗口有一部分
 *      在屏幕外）同样算；左、右两边同理；
 *    · 几条边同时满足时按上、左、右的顺序取第一条；
 *    · 任务栏在那一侧、或边外紧挨着另一台显示器时，那条边不能贴（见文件头说明）；
 *    · 吸附后的矩形：贴边那一侧与工作区边缘对齐，另一个方向平移到窗口落在工作区内，
 *      尺寸保持不变。
 *
 *    wnd：      窗口当前矩形。
 *    monitors： 当前所有显示器，可以为空（此时不贴边）。
 *    snapPx：   吸附阈值，物理像素，>= 0。
 *  @return 判定结果。本函数无副作用。
 */
DockSnapResult DetectDockEdge(const RECT& wnd, const std::vector<DockMonitor>& monitors, int snapPx);

/**
 *  计算收起后的窗口矩形：窗口沿贴边方向滑出屏幕，只在屏幕内留下 stripPx 宽的一条。
 *    snapped： 吸附后的矩形（DetectDockEdge 的 m_snapped）。
 *    edge：    贴在哪条边；为 kPanelDockNone 时原样返回 snapped。
 *    work：    所贴显示器的工作区（DetectDockEdge 的 m_work）。
 *    stripPx： 留在屏幕内的细条宽度，物理像素，>= 1。
 *  @return 收起后的窗口矩形，尺寸与 snapped 相同。本函数无副作用。
 */
RECT ComputeDockHiddenRect(const RECT& snapped, PanelDockEdge edge, const RECT& work, int stripPx);

/**
 *  计算收起后留在屏幕内的那条细条，鼠标碰到它时主面板展开。
 *  参数含义同 ComputeDockHiddenRect。
 *  @return 细条矩形；edge 为 kPanelDockNone 时返回空矩形。本函数无副作用。
 */
RECT ComputeDockStripRect(const RECT& snapped, PanelDockEdge edge, const RECT& work, int stripPx);

} // namespace balloonmain
