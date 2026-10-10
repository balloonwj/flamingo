/**
 *  PanelDockPolicy.h
 *
 *  主面板「贴边自动隐藏」的收起 / 展开判定：一个不碰窗口、不读系统状态的状态机。
 *  调用方（MainPanelDockController）按固定间隔采集一次输入（开关、鼠标位置、鼠标键、
 *  菜单、文字输入），交给 Tick，再按返回的动作去移动窗口。
 *
 *  判定规则：
 *    · 展开状态下，鼠标离开主面板（及其弹出的浮层）满 kDockHideDelayMs 后收起；
 *    · 鼠标左键按着、有弹出菜单开着、正在主面板上输入文字时都不收起，离开计时也随之重新开始；
 *    · 从托盘或快捷键唤出（NotifyShownExternally）后，鼠标要先进入过主面板，离开后才会
 *      再收起 —— 否则唤出时鼠标还在托盘处，主面板刚出来就会被收回去；
 *    · 收起状态下，鼠标碰到屏幕边缘的细条时展开；
 *    · 开关被关掉时退出贴边状态（若已收起则先展开）。
 *
 *  时间一律取调用方传入的 GetTickCount() 值，用无符号减法计算间隔，GetTickCount 约
 *  49.7 天回绕一次也不影响判定。单元测试见 Tests/PanelDockPolicyTests.cpp。
 *
 *  本模块被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程共用，在 Flamingo.vcxproj
 *  里需标记 PrecompiledHeader=NotUsing。
 *
 *  典型用法：
 *      //用户把主面板拖到屏幕上边松开鼠标
 *      policy.Dock(balloonmain::kPanelDockTop);
 *      //轮询定时器里
 *      balloonmain::PanelDockAction act = policy.Tick(input);
 *      if (act == balloonmain::kDockActionHide)
 *          SlideTo(hiddenRect);
 *
 *  balloonwj@qq.com   2026-10-02
 */

#pragma once

#include <windows.h>

#include "PanelDockGeometry.h"   // balloonmain::PanelDockEdge

namespace balloonmain {

//鼠标离开主面板多久之后收起，单位毫秒
const DWORD kDockHideDelayMs = 500;

//一次 Tick 之后调用方应当执行的动作
enum PanelDockAction
{
    //保持现状，不移动窗口
    kDockActionNone = 0,
    //收起：把窗口滑到屏幕外，只留细条
    kDockActionHide,
    //展开：把窗口滑回吸附位置
    kDockActionShow,
    //退出贴边状态：窗口回到吸附位置（已收起时即为展开）并取消置顶。贴边开关被关掉时返回
    kDockActionUndock
};

//一次 Tick 的输入，由调用方在轮询定时器里采集
struct PanelDockInput
{
    //贴边自动隐藏开关是否开启（系统设置「通用」页）
    bool m_enabled;
    //展开状态下：鼠标是否在主面板或它弹出的浮层上；收起状态下：鼠标是否在屏幕边缘的细条上
    bool m_cursorOver;
    //鼠标左键是否按着（拖动或拉伸窗口时为 true）
    bool m_mouseButtonDown;
    //是否有弹出菜单开着（balloonui 菜单或系统原生菜单）
    bool m_menuOpen;
    //是否正在主面板上输入文字（搜索框有焦点且内容不为空、搜索下拉开着，或个性签名编辑框开着）
    bool m_editingText;
    //当前时刻，GetTickCount() 的返回值，毫秒
    DWORD m_nowTick;

    PanelDockInput()
        : m_enabled(true)
        , m_cursorOver(false)
        , m_mouseButtonDown(false)
        , m_menuOpen(false)
        , m_editingText(false)
        , m_nowTick(0)
    {
    }
};

/**
 *  主面板贴边状态机。
 *
 *  状态有三种：未贴边、贴边且展开、贴边且收起。本类只记录状态并给出动作，移动窗口、
 *  置顶等实际操作都由调用方完成。由 MainPanelDockController 按值持有，生命周期与之相同。
 */
class CPanelDockPolicy
{
public:
    CPanelDockPolicy();

    /**
     *  进入贴边状态（展开）。用户把主面板拖到可贴的边缘松开鼠标时调用；已在贴边状态时
     *  调用则改贴到新的边。此刻鼠标就在主面板上，因此视为已经进入过主面板。
     *    edge：贴在哪条边；传 kPanelDockNone 等同于 Undock。
     */
    void Dock(PanelDockEdge edge);

    /**
     *  退出贴边状态。用户把主面板拖离边缘、或者注销时调用。可重复调用。
     */
    void Undock();

    /**
     *  主面板被托盘、快捷键等外部入口唤出时调用。贴边状态下转为展开，并要求鼠标先进入过
     *  主面板才会再次收起；未贴边时什么都不做。
     *  @return true 表示调用前处于收起状态，调用方须把窗口移回吸附位置。
     */
    bool NotifyShownExternally();

    /**
     *  推进一次状态机。
     *    in：本次采集到的输入，见 PanelDockInput。
     *  @return 调用方应执行的动作。返回 kDockActionHide / kDockActionShow / kDockActionUndock
     *          时内部状态已经切换完毕，调用方只需移动窗口。
     */
    PanelDockAction Tick(const PanelDockInput& in);

    //当前贴在哪条边；未贴边时为 kPanelDockNone
    PanelDockEdge GetEdge() const
    {
        return m_edge;
    }

    //是否处于贴边状态（展开或收起）
    bool IsDocked() const
    {
        return m_edge != kPanelDockNone;
    }

    //是否处于贴边且收起的状态
    bool IsHidden() const
    {
        return m_hidden;
    }

private:
    //贴在哪条边；kPanelDockNone 表示未贴边
    PanelDockEdge m_edge;
    //是否已收起。只在贴边状态下可能为 true
    bool m_hidden;
    //本次展开以来鼠标是否进入过主面板。为 false 时鼠标在外面也不收起（见 NotifyShownExternally）
    bool m_armed;
    //是否正在计算鼠标离开的时长
    bool m_leaving;
    //鼠标离开主面板的起始时刻，GetTickCount() 值，毫秒；只在 m_leaving 为 true 时有意义
    DWORD m_leaveStartTick;
};

} // namespace balloonmain
