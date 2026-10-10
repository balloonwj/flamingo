/**
 *  「公告中心」公告列表里的一行（可悬停 / 可选中），AnnounceRow.h
 *
 *  在 DuiHBox 的基础上补三件事：
 *    · 鼠标停在本行上时铺一层极淡的悬停底色；
 *    · 当前正在右栏查看的那一行铺一层选中底色（比悬停色更实），并在最左侧画一道
 *      强调竖条，扫一列行时一眼看得出"我在看哪条"；
 *    · 未读的公告在标题左边画一个小圆点 —— 未读是这个界面里最要紧的信息，
 *      靠文字颜色深浅区分不够醒目。
 *
 *  存在理由：balloonui 的 DuiHBox 只是布局容器，没有"悬停 / 选中态"这一说；而
 *  DuiListBox 是单列纯文本列表，装不下"标题 + 发布者·时间 + 未读点"这样的两行富行。
 *  本类按仓库 UI 约定走「次选：子类化扩展」这一档，只覆写 OnPaint 补齐差异，
 *  <u>不改 balloonui</u>。（2026-08-05 与用户确认过走这一档。）
 *
 *  与 BalloonHistory 的 HistoryRow 的关系：两者都是"可悬停的列表行容器"，但共同
 *  部分只有"铺一层底色"这几行，各自的差异（那边悬停显示操作按钮 + 双击定位，这边
 *  选中态 + 级别色条 + 未读点）都不小，故不抽公共基类、各自实现——同样与用户确认过。
 *
 *  <u>悬停与选中都由宿主窗按鼠标位置驱动</u>，不靠 OnMouseEnter / OnMouseLeave：
 *  DuiHost::TrackHover 只把 enter/leave 发给<u>最深</u>的那个控件，鼠标停在行内的
 *  标题文字上时收到事件的是那个 DuiLabel，行容器永远收不到。这个坑 HistoryRow
 *  已经踩过，见其 SetHovered 的注释。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-08-05
 */
#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"

namespace balloonannounce {

/**
 *  AnnounceRow —— 公告中心公告列表的一行。
 */
class AnnounceRow : public balloonwjui::DuiHBox
{
public:
    AnnounceRow();

    /**
     *  置 / 撤悬停态。由宿主窗按鼠标落点驱动（理由见文件头）。
     *  重复设同一个值不做任何事（不重绘）。
     *    hovered：true 进入悬停态。
     */
    void SetHovered(bool hovered);

    // 当前是否处于悬停态。
    bool IsHovered() const { return m_hovered; }

    /**
     *  置 / 撤选中态。选中的那一行即右栏正在显示的那条公告。
     *  重复设同一个值不做任何事（不重绘）。
     *    selected：true 进入选中态。
     */
    void SetSelected(bool selected);

    // 当前是否处于选中态。
    bool IsSelected() const { return m_selected; }

    /**
     *  设置本行是否为未读公告 —— 未读时在标题左侧画一个实心小圆点。
     *  只改内存状态，调用方负责随后 Invalidate（一般在构建时就设好，不必单独刷）。
     *    unread：true 表示未读。
     */
    void SetUnread(bool unread) { m_unread = unread; }

    // 本行是否未读。供宿主窗在点开某行时判断要不要发已读上报。
    bool IsUnread() const { return m_unread; }

    /**
     *  设置本行底部要不要画一条分隔线。列表里除最后一行外都要画。
     *
     *  这条线由行自己画、而不是在列表里插一个独立的分隔线控件，于是列表的直接子控件
     *  与公告条目一一对应（ANB17 用例钉住这一点）。该写法最初是为绕开「滚轮落在分隔线
     *  控件上就丢失」的问题；2026-08-14 库里补上滚轮沿父链上冒（DuiHost::
     *  DispatchMouseWheel）之后那个问题已不存在，此处沿用原写法。
     *
     *  只改内存状态，调用方负责随后 Invalidate（一般在构建时就设好，不必单独刷）。
     *    show：true 表示画。
     */
    void SetShowBottomLine(bool show) { m_showBottomLine = show; }

    // 本行底部是否画分隔线。
    bool IsShowBottomLine() const { return m_showBottomLine; }

    /**
     *  设置公告级别，决定最左侧那道强调竖条的颜色。
     *    severity：取值见 net/Protocol.h 的 announcement_severity
     *              （0 普通 1 提醒 2 警告）；越界值按"普通"处理。
     */
    void SetSeverity(int severity) { m_severity = severity; }

    /**
     *  DuiControl 覆写：先铺底色 / 画竖条与未读点，再交基类画子控件。
     *  顺序不能反 —— 反过来底色会把刚画好的文字盖掉。
     */
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    bool m_hovered;         // 是否悬停
    bool m_selected;        // 是否选中（右栏正在显示这一条）
    bool m_unread;          // 是否未读（画标题左侧的小圆点）
    int  m_severity;        // 公告级别（决定左侧竖条颜色）
    bool m_showBottomLine;  // 是否在行底画分隔线（列表里除最后一行外都画）
};

} // namespace balloonannounce
