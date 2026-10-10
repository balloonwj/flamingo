/**
 *  字幕编辑器的时间轴（docs/语音字幕方案.md 第 7 节「下方时间轴」），CaptionTimelineView.h
 *
 *  自绘控件（与预览区同为 UI 约定第 2 条下的例外，理由见方案第 7 节）：上面一行刻度与时间文字，下面一行把每条字幕画成
 *  一个色块（改过的用另一种颜色，选中的加深色边框），一根红色竖线表示播放位置。
 *    - 点空白处：跳到该时刻（DUIN_TIMELINE_SEEK，extra 为毫秒）；
 *    - 点色块：选中它（DUIN_TIMELINE_SELECT，extra 为下标），同时跳到它的开始；
 *    - 拖色块本体平移、拖左右边缘改开始或结束：松开鼠标时发 DUIN_TIMELINE_DRAGGED，编辑器经 DragResult 取新的起止时刻
 *      交给 CaptionEditorModel（可撤销）；拖动过程中只在本控件里预览，不改数据；
 *    - 滚轮左右滚动，按住 Ctrl 滚轮以鼠标位置为中心缩放；播放时播放位置走出可见范围就自动翻页；
 *    - 底部浮一条横向滚动条（balloonui 的 DuiScrollBar，子控件，2026-10-06 用户要求）：内容超过一屏时才有，滚动时淡入、
 *      停止操作后淡出，可以拖动；内容的绘制裁剪在本控件矩形内，不会画到旁边的控件上。
 *  换算与限位规则在 CaptionTimelineMath.h。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  典型用法：
 *      m_timeline->SetCues(model.Track().Cues());
 *      m_timeline->SetDuration(durationMs);
 *      m_timeline->SetPosition(player.PositionMs(), true);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>

#include <vector>

#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../Model/CaptionCue.h"
#include "CaptionTimelineMath.h"

namespace caption {

/**
 *  CaptionTimelineView —— 时间轴。
 */
class CaptionTimelineView : public balloonwjui::DuiControl
{
public:
    //本控件的通知码：另开 DUIN_CUSTOM + 120 起的一段，避开其它控件常用的低段（开发约定文档 UI 约定第 4 条）
    enum NotifyCode
    {
        DUIN_TIMELINE_BASE    = balloonwjui::DUIN_CUSTOM + 120,   //起点，本身不发
        DUIN_TIMELINE_SEEK    = DUIN_TIMELINE_BASE + 1,           //点了空白处：extra 为要跳到的时刻（毫秒）
        DUIN_TIMELINE_SELECT  = DUIN_TIMELINE_BASE + 2,           //点了色块：extra 为字幕下标
        DUIN_TIMELINE_DRAGGED = DUIN_TIMELINE_BASE + 3,           //拖动结束：新的起止时刻经 DragResult 取
    };

    //横向滚动条的控件 id：避开编辑器窗口里 101～160 的控件 id；它拖动时发出的 DUIN_VALUECHANGED 带这个 id
    enum
    {
        kScrollBarCtrlId = 170,
    };

    CaptionTimelineView();

    //全部字幕（拷贝保存，按开始时刻排序）
    void SetCues(const std::vector<CaptionCue>& cues);

    //录像时长（毫秒）
    void SetDuration(int64_t durationMs);

    /**
     *  播放位置。
     *  @param ms         时刻（毫秒）
     *  @param followPage 位置走出可见范围时是否自动翻页（播放中为 true）
     */
    void SetPosition(int64_t ms, bool followPage);

    //选中的字幕下标；-1 表示没有
    void SetSelected(int index);

    //把某个时刻滚进可见范围（不在可见范围时让它居左四分之一处）
    void EnsureVisible(int64_t ms);

    /**
     *  最近一次拖动的结果（收到 DUIN_TIMELINE_DRAGGED 时调用）。
     *  @return 有结果返回 true
     */
    bool DragResult(int& index, int64_t& startMs, int64_t& endMs) const;

    virtual void OnPaint(HDC hdc, const RECT& rcDirty) override;
    //把横向滚动条摆在底部那条命中带上，并按新宽度刷新它的范围
    virtual void Layout(const RECT& rcAvail) override;
    virtual bool OnLButtonDown(POINT pt, UINT mkFlags) override;
    virtual bool OnMouseMove(POINT pt, UINT mkFlags) override;
    virtual bool OnLButtonUp(POINT pt, UINT mkFlags) override;
    virtual bool OnMouseWheel(POINT pt, short zDelta, UINT mkFlags) override;
    virtual bool OnSetCursor(POINT pt) override;

private:
    //按当前控件矩形更新视图参数（宽度、字幕块所在行）
    TimelineView view() const;
    //控件坐标（相对左上角）
    POINT local(POINT pt) const;
    /**
     *  按当前滚动位置、缩放与录像时长刷新横向滚动条（不发通知）；内容放得下时隐藏它。
     *  @param show 是否同时让滚动条淡入显示（滚轮滚动、缩放时为 true）
     */
    void syncScrollBar(bool show);
    //滚动条被拖动或点击轨道：换成左边缘时刻并重画（DuiScrollBar 的回调，user 为本控件）
    static void OnScrollBar(void* user, int pos);

    std::vector<CaptionCue> m_cues;         //全部字幕
    int64_t                 m_durationMs;   //录像时长
    int64_t                 m_positionMs;   //播放位置
    int                     m_selected;     //选中的下标
    int64_t                 m_scrollMs;     //左边缘对应的时刻
    double                  m_pxPerSec;     //每秒多少像素
    //拖动
    bool                    m_pressed;      //左键按着
    bool                    m_dragging;     //已开始拖动（移动超过阈值）
    int                     m_dragKind;     //拖的部位，取值见 TimelineHitKind
    int                     m_dragIndex;    //拖的字幕下标
    int                     m_pressX;       //按下时的横坐标（控件坐标）
    int64_t                 m_dragStart;    //拖动中的开始时刻（预览）
    int64_t                 m_dragEnd;      //拖动中的结束时刻（预览）
    bool                    m_hasResult;    //是否有拖动结果
    int                     m_resultIndex;  //拖动结果：下标
    int64_t                 m_resultStart;  //拖动结果：开始
    int64_t                 m_resultEnd;    //拖动结果：结束
    balloonwjui::DuiScrollBar* m_scrollBar; //横向滚动条（本控件的子控件，由控件树持有，本指针仅引用）
};

} // namespace caption
