/**
 *  顶部浮动工具条的「收起 / 悬停展开」状态（docs/屏幕共享与录制方案.md 第 10 期 10c），ShareTopBarCollapse.h
 *
 *  屏幕共享观看端的全屏工具条、远程桌面控制端的全屏工具条、共享发起人的控制条都浮在屏幕顶部，会挡住画面最上面一块。
 *  三条工具条共用本类决定显示状态：
 *    - 一直展开（默认）：工具条最右端的按钮显示「收起」；
 *    - 收起：工具条隐藏，只在原位置顶部正中显示一个小把手；
 *    - 临时展开：鼠标移到小把手上时工具条展开，按钮显示「固定」；鼠标离开工具条 2 秒后收回小把手，点「固定」回到一直展开。
 *  本类只做状态计算，不碰窗口；工具条用一个周期定时器把「鼠标在不在把手 / 工具条上」交进来。
 *
 *  2026-10-07 起（docs/屏幕共享与录制方案.md 第 14.2 节）共享控制条、标注工具条、录制条还可以「完全隐藏」，与「收起」并存：
 *    - 隐藏：工具条与把手都不显示；
 *    - 从隐藏临时显示：鼠标移到工具条原来所在的屏幕边缘（ComputeHiddenEdgeZone 给出的感应区）时显示整条，按钮为「固定」；
 *      鼠标离开工具条与感应区 2 秒后再次隐藏，点「固定」回到一直展开。
 *  不提供「隐藏」按钮的工具条（观看端与远程桌面的全屏工具条）不会进入这两个状态，行为与原来相同。
 *
 *  典型用法（工具条窗口里）：
 *      m_collapse.OnToggleClicked();                  // 点了「收起 / 固定」
 *      ...定时器里：
 *      if (m_collapse.OnTick(now, overHandle, overBar)) applyCollapseState();
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <windows.h>
#include <stdint.h>

namespace screenshare {

//顶部浮动工具条的显示状态
enum TopBarState
{
    //一直展开（默认）：工具条显示，按钮为「收起」
    top_bar_pinned = 0,
    //收起：工具条隐藏，只显示顶部的小把手
    top_bar_collapsed = 1,
    //临时展开：鼠标移到把手上后展开，按钮为「固定」；鼠标离开工具条 kTopBarPeekHideMs 后收回
    top_bar_peek = 2,
    //隐藏（2026-10-07）：点了「隐藏」，工具条与把手都不显示；鼠标到屏幕边缘的感应区时转为 top_bar_hidden_peek
    top_bar_hidden = 3,
    //从隐藏临时显示（2026-10-07）：整条显示，按钮为「固定」；鼠标离开工具条与感应区 kTopBarPeekHideMs 后回到隐藏
    top_bar_hidden_peek = 4,
};

//隐藏后屏幕边缘感应区的厚度（像素）：鼠标推到屏幕边缘时会停在最外的这几个像素上
const int kHiddenEdgeZonePx = 3;
//感应区比工具条的横向范围左右各放宽多少（像素）：用户不必对准工具条原来的位置
const int kHiddenEdgeSlackPx = 80;

/**
 *  某个状态下是否显示整条工具条。
 *  @param state 取值见 TopBarState
 *  @return 一直展开、临时展开、从隐藏临时显示时为 true
 */
bool TopBarShowsBar(int state);

/**
 *  某个状态下是否只显示小把手。
 *  @param state 取值见 TopBarState
 *  @return 收起时为 true
 */
bool TopBarShowsHandle(int state);

/**
 *  隐藏后的屏幕边缘感应区：工具条离所在显示器的顶边近就取顶边，离底边近就取底边；厚 kHiddenEdgeZonePx，
 *  横向为工具条的范围左右各放宽 kHiddenEdgeSlackPx，不超出显示器。
 *  @param bar     工具条显示时的窗口矩形（屏幕坐标）
 *  @param monitor 工具条所在显示器的整块矩形（屏幕坐标，不是工作区：鼠标只会停在显示器的物理边缘上）
 *  @return 感应区矩形（屏幕坐标），与 PtInRect 一起使用
 */
RECT ComputeHiddenEdgeZone(const RECT& bar, const RECT& monitor);

//临时展开状态下，鼠标离开工具条多久后收回小把手（毫秒）
const int64_t kTopBarPeekHideMs = 2000;
//小把手的宽、高（像素）。收起时工具条窗口缩到这个尺寸、只剩标题栏（标题改成「▼ 屏幕共享」之类），
//高度取 balloonui 标题栏的最小高度 18
const int kTopBarHandleW = 120;
const int kTopBarHandleH = 18;
//工具条检查鼠标位置用的定时器 id：取一个不常见的值，避开 balloonui 宿主窗口内部可能使用的小编号定时器
const UINT_PTR kTopBarTimerId = 0x5442;
//收起时标题前面加的箭头（提示「往下展开」）
const wchar_t* const kTopBarHandlePrefix = L"\u25BC ";
//「收起 / 固定」按钮的宽度（像素）
const int kTopBarToggleBtnW = 56;
//工具条检查鼠标位置的周期（毫秒）：收起与临时展开状态下由工具条的定时器按此周期调用 OnTick
const unsigned int kTopBarTickMs = 200;

//顶部浮动工具条的「收起 / 悬停展开」状态
class TopBarCollapse
{
public:
    TopBarCollapse();

    //当前状态，取值见 TopBarState
    int State() const;

    //回到一直展开（工具条重新显示，如再次进入全屏、重新开始共享时调用）
    void Reset();

    /**
     *  点了「收起 / 固定」按钮：一直展开时收起；临时展开或从隐藏临时显示时固定为一直展开；收起、隐藏状态下按钮不可见，忽略。
     *  @return 状态变了返回 true
     */
    bool OnToggleClicked();

    /**
     *  点了「隐藏」按钮（2026-10-07）：整条显示时（一直展开、临时展开、从隐藏临时显示）进入隐藏；收起、隐藏状态下按钮不可见，忽略。
     *  @return 状态变了返回 true
     */
    bool OnHideClicked();

    /**
     *  周期检查（收起、临时展开、隐藏、从隐藏临时显示状态下调用）。
     *  @param nowMs      当前时刻（单调时钟，毫秒）
     *  @param overHandle 鼠标此刻是否在小把手上
     *  @param overBar    鼠标此刻是否在工具条上
     *  @param overEdge   鼠标此刻是否在隐藏后的屏幕边缘感应区里（ComputeHiddenEdgeZone）；不提供「隐藏」的工具条传 false
     *  @return 状态变了返回 true（调用方据此显示 / 隐藏工具条与把手）
     */
    bool OnTick(int64_t nowMs, bool overHandle, bool overBar, bool overEdge);

    //同上，鼠标不在感应区（不提供「隐藏」的工具条用这个写法）
    bool OnTick(int64_t nowMs, bool overHandle, bool overBar);

    //「收起 / 固定」按钮的文字：一直展开时为「收起」，临时展开与从隐藏临时显示时为「固定」
    const wchar_t* ToggleText() const;

private:
    int     m_state;         //当前状态，取值见 TopBarState
    int64_t m_lastOverMs;    //临时展开状态下鼠标最后一次在工具条上的时刻（毫秒）
};

/**
 *  小把手的位置：在工具条矩形顶边的正中，宽高为 kTopBarHandleW × kTopBarHandleH。
 *  @param bar 工具条展开时的窗口矩形（屏幕坐标）
 *  @return 小把手的窗口矩形（屏幕坐标）
 */
RECT ComputeTopBarHandleRect(const RECT& bar);

//「收起 / 固定」按钮在一直展开时的文字「收起」（已取译文）。工具条按两种文字中较宽的一个定按钮宽度
const wchar_t* TopBarCollapseText();

//「收起 / 固定」按钮在临时展开时的文字「固定」（已取译文）
const wchar_t* TopBarPinText();

//「隐藏」按钮的悬停提示（已取译文，2026-10-07）：说明隐藏后怎样让工具条重新出现
const wchar_t* TopBarHideText();

} // namespace screenshare
