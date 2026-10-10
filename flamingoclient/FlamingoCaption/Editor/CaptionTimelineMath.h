/**
 *  字幕编辑器时间轴的纯计算（docs/语音字幕方案.md 第 7 节「下方时间轴」），CaptionTimelineMath.h
 *
 *  时间轴控件（CaptionTimelineView）只负责画与收鼠标，换算与规则都在这里：
 *    - 毫秒与横坐标互换（左边缘对应 m_scrollMs，每秒 m_pxPerSec 像素）；
 *    - 命中：点在字幕块的左边缘、右边缘（各 edgePx 像素宽）还是本体，或是空白处；
 *    - 拖动：整块平移保持时长、拖边缘改开始或结束，都限制在前一条的结束与后一条的开始之间、不超出录像时长、
 *      不短于 kCaptionMinCueMs；
 *    - 滚轮缩放的范围、滚动位置的范围、刻度间隔（相邻刻度至少隔 kTimelineMinTickPx 像素）。
 *  全部为纯函数，单测见 Tests/CaptionEditorTests.cpp（CAPTL1～CAPTL4）。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <windows.h>
#include <stdint.h>

#include <vector>

#include "../Model/CaptionCue.h"

namespace caption {

//缩放范围：每秒最少、最多多少像素；默认每秒多少像素
const double kTimelineMinPxPerSec = 2.0;
const double kTimelineMaxPxPerSec = 400.0;
const double kTimelineDefaultPxPerSec = 40.0;
//滚轮每格缩放的倍数
const double kTimelineZoomStep = 1.25;
//相邻刻度的最小间隔（像素）
const int kTimelineMinTickPx = 70;

//时间轴的视图参数
struct TimelineView
{
    int64_t m_scrollMs;    //左边缘对应的时刻（毫秒）
    double  m_pxPerSec;    //每秒多少像素
    int     m_width;       //可用宽度（像素）
    int     m_trackTop;    //字幕块所在行的上边（相对控件，像素）
    int     m_trackBottom; //字幕块所在行的下边

    TimelineView()
        : m_scrollMs(0)
        , m_pxPerSec(kTimelineDefaultPxPerSec)
        , m_width(0)
        , m_trackTop(0)
        , m_trackBottom(0)
    {
    }
};

//命中的部位
enum TimelineHitKind
{
    timeline_hit_none  = 0,   //空白处（点它跳到该时刻）
    timeline_hit_body  = 1,   //字幕块本体（拖动整块平移）
    timeline_hit_left  = 2,   //字幕块左边缘（拖动改开始时刻）
    timeline_hit_right = 3,   //字幕块右边缘（拖动改结束时刻）
};

//命中结果
struct TimelineHit
{
    int m_kind;    //部位，取值见 TimelineHitKind
    int m_index;   //字幕下标；空白处为 -1

    TimelineHit()
        : m_kind(timeline_hit_none)
        , m_index(-1)
    {
    }
};

//一个刻度
struct TimelineTick
{
    int64_t m_ms;     //时刻
    int     m_x;      //横坐标
    bool    m_major;  //是否为主刻度（带时间文字）
};

/**
 *  时刻换成横坐标（可能落在可见范围之外）。
 */
int TimelineMsToX(const TimelineView& v, int64_t ms);

/**
 *  横坐标换成时刻（小于 0 时按 0）。
 */
int64_t TimelineXToMs(const TimelineView& v, int x);

/**
 *  命中测试。纵坐标不在字幕块那一行时算空白处；多条重叠时取后面（开始较晚）的一条。
 *  @param v      视图
 *  @param cues   全部字幕（按开始时刻排序）
 *  @param x、y   鼠标位置（相对控件）
 *  @param edgePx 边缘的判定宽度（像素）；块太窄时边缘各占块宽的三分之一
 */
TimelineHit TimelineHitTest(const TimelineView& v, const std::vector<CaptionCue>& cues, int x, int y, int edgePx);

/**
 *  拖动后的起止时刻。
 *  @param kind       拖的部位（body / left / right）
 *  @param cues       全部字幕（按开始时刻排序）
 *  @param index      被拖的字幕下标
 *  @param deltaMs    鼠标移动换算成的时间差（毫秒）
 *  @param durationMs 录像时长（毫秒）；不大于 0 时不限制结束时刻
 *  @param startMs    [出参] 新的开始时刻
 *  @param endMs      [出参] 新的结束时刻
 *  @return 下标越界或部位不对时返回 false
 */
bool TimelineDragTimes(int kind, const std::vector<CaptionCue>& cues, int index, int64_t deltaMs, int64_t durationMs,
                       int64_t& startMs, int64_t& endMs);

/**
 *  滚轮缩放后的每秒像素数（夹在最小、最大值之间）。
 *  @param pxPerSec 当前值
 *  @param steps    滚轮格数，正数放大、负数缩小
 */
double TimelineZoom(double pxPerSec, int steps);

/**
 *  滚动位置的合法范围：不小于 0，不超过「录像时长减去一屏能显示的时长」（不足一屏时为 0）。
 */
int64_t TimelineClampScroll(const TimelineView& v, int64_t scrollMs, int64_t durationMs);

/**
 *  可见范围内的刻度：主刻度间隔从 1、2、5、10、15、30 秒，1、2、5、10 分钟里选能让相邻主刻度至少隔
 *  kTimelineMinTickPx 像素的最小一档，每两个主刻度之间加一个次刻度。
 */
std::vector<TimelineTick> TimelineTicks(const TimelineView& v, int64_t durationMs);

//时间轴横向滚动条的参数（单位为像素）：最大位置、页大小、当前位置（2026-10-06 用户要求加滚动条）
struct TimelineScrollBarState
{
    int m_maxPos;   //最大位置（DuiScrollBar 的 max，即内容总宽减去可见宽度）；内容放得下时为 0，此时不显示滚动条
    int m_page;     //页大小（可见宽度）
    int m_pos;      //当前位置（左边缘离内容起点的像素数）

    TimelineScrollBarState()
        : m_maxPos(0)
        , m_page(0)
        , m_pos(0)
    {
    }
};

/**
 *  按视图参数与录像时长算出横向滚动条的参数：内容总宽为录像时长按当前缩放换成的像素数。
 *  @param v          视图参数（用到 m_scrollMs、m_pxPerSec、m_width）
 *  @param durationMs 录像时长（毫秒）
 *  @return 滚动条参数；内容放得下或宽度为 0 时 m_maxPos 为 0
 */
TimelineScrollBarState TimelineScrollBar(const TimelineView& v, int64_t durationMs);

/**
 *  滚动条位置（像素）换成左边缘对应的时刻，并夹在合法范围内（见 TimelineClampScroll）。
 *  @param v          视图参数（用到 m_pxPerSec、m_width）
 *  @param pos        滚动条位置（像素）
 *  @param durationMs 录像时长（毫秒）
 *  @return 左边缘对应的时刻（毫秒）
 */
int64_t TimelineScrollFromBar(const TimelineView& v, int pos, int64_t durationMs);

/**
 *  主刻度的间隔（毫秒），规则见 TimelineTicks。
 */
int64_t TimelineMajorStepMs(double pxPerSec);

} // namespace caption
