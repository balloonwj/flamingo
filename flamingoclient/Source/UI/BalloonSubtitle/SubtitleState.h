/**
 *  SubtitleState.h
 *
 *  字幕窗口的交互状态：锁定与解锁、鼠标悬停、底色与工具按钮的淡入淡出、锁定后小锁按钮的出现与消失，
 *  以及窗口此刻是否应当点击穿透。交互规则（docs/聊天字幕模式方案.md 第 1 节，对应截图 t1 / t2）：
 *    · 解锁时：光标移到字幕上，圆角半透明底色与工具按钮淡入；离开后稍等片刻再淡出；拖动、调宽、
 *      设置菜单弹出期间保持显示。窗口不穿透，可以按住拖动。
 *    · 锁定时：不画底色与工具按钮，整个窗口点击穿透。光标在字幕上停留一会儿后，浮出一个小锁按钮；
 *      光标正好落在小锁按钮上时窗口暂时不穿透，这一下点击才能落到按钮上用来解锁；光标离开字幕后
 *      小锁按钮稍等片刻再淡出。
 *  锁定时窗口收不到鼠标消息，光标位置由窗口的定时器查询后经 OnCursor 报告。本类不读系统时间，
 *  时刻由调用方传入，因此可以完全用单元测试覆盖（见 Tests/SubtitleTests.cpp 的 SUBS 组）。
 *
 *  典型用法（窗口定时器里）：
 *      state.OnCursor(part, now);
 *      subtitle::SubtitleVisual v = state.Tick(now);
 *      ApplyClickThrough(v.m_clickThrough);  if (v 与上次不同) Redraw(v);
 *
 *  balloonwj@qq.com   2026-10-05
 */

#pragma once

#include <stdint.h>

namespace subtitle {

//光标所在的部位（由字幕窗口按布局做命中测试得出，见 SubtitleLayout.h 的 HitTestSubtitle）
enum SubtitlePart
{
    //光标不在字幕窗口上
    kPartNone = 0,
    //字幕上的文字或空白处：解锁时按住可以拖动
    kPartBody = 1,
    //左边缘：解锁时按住左右拖动调宽度
    kPartLeftEdge = 2,
    //右边缘：同左边缘
    kPartRightEdge = 3,
    //锁定 / 解锁按钮：解锁时在工具按钮里，锁定时是浮出的那个小锁按钮
    kPartLock = 4,
    //设置按钮：弹出字号、行数、共享时隐藏的菜单
    kPartSettings = 5,
    //回到聊天窗按钮
    kPartBack = 6,
    //退出字幕模式按钮
    kPartClose = 7,
};

//光标在字幕上停留这么久（毫秒），锁定状态下的小锁按钮才浮出，避免光标路过时闪一下
const uint32_t kLockButtonHoverDelayMs = 500;
//光标离开字幕后再等这么久（毫秒）才开始淡出底色、工具按钮或小锁按钮，光标短暂移出又移回时不闪烁
const uint32_t kChromeHideDelayMs = 400;
//淡入、淡出各用多久（毫秒）：从全透明到全不透明、从全不透明到全透明
const uint32_t kChromeFadeInMs  = 150;
const uint32_t kChromeFadeOutMs = 250;
//不透明度的最大值
const int kAlphaOpaque = 255;

//字幕窗口此刻应当呈现的样子
struct SubtitleVisual
{
    bool m_clickThrough;     //窗口是否应当点击穿透（带 WS_EX_TRANSPARENT）
    int  m_chromeAlpha;      //底色与工具按钮的不透明度，0~255；只在解锁时可能大于 0
    int  m_lockButtonAlpha;  //锁定状态下小锁按钮的不透明度，0~255；只在锁定时可能大于 0
    bool m_buttonsActive;    //工具按钮此刻能否点击：底色完全显示时才响应，看不见的按钮不能被误点
    bool m_animating;        //是否还在渐变或等待计时中：为 true 时窗口应保持较高的刷新频率

    SubtitleVisual()
        : m_clickThrough(false)
        , m_chromeAlpha(0)
        , m_lockButtonAlpha(0)
        , m_buttonsActive(false)
        , m_animating(false)
    {
    }
};

/**
 *  CSubtitleState —— 一个字幕窗口的交互状态。
 *
 *  由 CSubtitleWindow 持有，只在界面线程上使用。
 */
class CSubtitleState
{
public:
    //构造后为解锁、光标不在窗口上、底色全透明
    CSubtitleState();

    /**
     *  锁定或解锁。锁定后底色与工具按钮立刻消失（不渐变），解锁后若光标就在字幕上，底色随即淡入。
     *    locked：true 锁定，false 解锁。
     *    nowMs： 当前时刻，毫秒（单调递增，如 GetTickCount64）。
     */
    void SetLocked(bool locked, uint64_t nowMs);

    //是否锁定
    bool IsLocked() const;

    /**
     *  报告光标此刻所在的部位。
     *    part： 取值见 SubtitlePart；kPartNone 表示光标不在窗口上。
     *    nowMs：当前时刻，毫秒。
     */
    void OnCursor(int part, uint64_t nowMs);

    /**
     *  开始或结束拖动 / 调宽。期间底色与工具按钮保持显示。
     *    dragging：true 开始，false 结束。
     *    nowMs：   当前时刻，毫秒。
     */
    void SetDragging(bool dragging, uint64_t nowMs);

    /**
     *  设置菜单弹出或关闭。期间底色与工具按钮保持显示。
     *    open： true 弹出，false 关闭。
     *    nowMs：当前时刻，毫秒。
     */
    void SetMenuOpen(bool open, uint64_t nowMs);

    /**
     *  推进渐变并返回此刻应当呈现的样子。
     *    nowMs：当前时刻，毫秒，不小于上一次调用时传入的值（更小时按没有经过时间处理）。
     *  返回：此刻的样子。
     */
    SubtitleVisual Tick(uint64_t nowMs);

    //光标此刻所在的部位（最近一次 OnCursor 报告的值）
    int CursorPart() const;

private:
    //某一时刻两个不透明度的目标值，以及渐变从哪个时刻开始算
    struct Targets
    {
        int      m_chromeTarget;  //底色与工具按钮的目标不透明度，0 或 255
        uint64_t m_chromeFrom;    //底色渐变的起算时刻，毫秒（离开延时到期时从到期那一刻算起）
        int      m_lockTarget;    //小锁按钮的目标不透明度，0 或 255
        uint64_t m_lockFrom;      //小锁按钮渐变的起算时刻，毫秒（出现延时、离开延时到期时从到期那一刻算起）
        bool     m_waiting;       //是否有出现延时或离开延时正在计时
    };

    /**
     *  按当前状态算出 nowMs 时刻的目标值。渐变起算时刻不早于上一次推进的时刻。
     *    nowMs：时刻，毫秒，不小于上一次推进的时刻。
     *  返回：目标值。
     */
    Targets computeTargets(uint64_t nowMs) const;

    /**
     *  按当前状态把两个不透明度推进到 nowMs 时刻。每次改变状态之前都先调用，保证渐变按状态实际
     *  改变的时刻计算，而不是按定时器碰巧下一次触发的时刻。nowMs 不大于上一次推进的时刻时什么都不做。
     *    nowMs：时刻，毫秒。
     */
    void advance(uint64_t nowMs);

    /**
     *  按目标值推进一个不透明度。
     *    current：当前值，0~255。
     *    target： 目标值，0 或 255。
     *    elapsed：经过的毫秒数。
     *  返回：推进后的值，不越过目标值。
     */
    static double stepAlpha(double current, int target, uint64_t elapsed);

    bool     m_locked;        //是否锁定
    int      m_part;          //光标所在的部位，取值见 SubtitlePart
    bool     m_inside;        //光标是否在窗口上（m_part != kPartNone）
    uint64_t m_enterAt;       //光标最近一次进入窗口（或光标在窗口上时被锁定）的时刻，毫秒；用于小锁按钮的出现延时
    uint64_t m_leaveAt;       //光标最近一次离开窗口的时刻，毫秒；0 表示还没有离开过；用于小锁按钮的离开延时
    uint64_t m_releaseAt;     //最近一次不再需要显示底色的时刻（光标离开、拖动结束、菜单关闭），毫秒；0 表示没有过
    bool     m_dragging;      //是否正在拖动或调宽
    bool     m_menuOpen;      //设置菜单是否正弹出
    double   m_chromeAlpha;   //底色与工具按钮当前的不透明度，0~255
    double   m_lockAlpha;     //小锁按钮当前的不透明度，0~255
    uint64_t m_lastTick;      //上一次推进的时刻，毫秒；0 表示还没有推进过
};

} // namespace subtitle
