/**
 *  帖子群窗口左栏的帖子列表 CThreadFeedList（docs/帖子群方案.md 4.6，样稿第 0 期）。
 *
 *  子类化 DuiScrollView（不改 balloonui）：内容是一个纵向排列的卡片容器，每张卡片是 CThreadPostCard，最下面一行是状态文字
 *  （「正在加载…」「滚动到底部加载更多」「没有更多了」）。
 *    - Rebuild：按窗口准备好的卡片数据整体重建内容，保持滚动位置；内容换了之后对自己 ForceLayout（UI 约定第 12 条）；
 *    - SetSelected：只改卡片的选中描边，不重建；
 *    - 翻页：DuiScrollView 不把滚动通知给业务，本类在每次绘制之后检查是否已滚到接近底部，是则调用登记的回调；
 *      回调由窗口实现，只投递一条消息，等这次绘制结束再拉下一页，不在绘制里发请求。
 *  滚动条用 DuiScrollView 自带的悬浮式细滑块（UI 约定第 1 条）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（本工程的 stdafx.h）。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"
#include "ThreadPostCard.h"

namespace balloonwjui {
class DuiLabel;
class DuiVBox;
}

namespace threadgroup {

/**
 *  CThreadFeedList —— 左栏的帖子列表。只在界面线程上使用。
 */
class CThreadFeedList : public balloonwjui::DuiScrollView
{
public:
    //滚到接近底部时的回调：user 为登记时传入的指针
    typedef void (*NearBottomFn)(void* user);

    CThreadFeedList();

    /**
     *  登记滚到接近底部时的回调。
     *  @param fn   回调，可为 NULL（不回调）
     *  @param user 回调的参数（窗口，不拥有）
     */
    void SetNearBottomCallback(NearBottomFn fn, void* user);

    /**
     *  按卡片数据整体重建列表，保持当前滚动位置（内容变矮时滚到能滚到的最底）。
     *  @param cards      卡片数据，按显示顺序
     *  @param bodyFont   正文字体（借用）
     *  @param titleFont  标题字体（借用）
     *  @param innerWidth 卡片内容区宽度（与排版卡片时用的相同，见 CardInnerWidth），单位：像素
     *  @param selectedId 选中的帖子；0 表示没有选中
     *  @param footer     最下面一行的状态文字；为空时不显示这一行
     */
    void Rebuild(const std::vector<PostCardData>& cards, HFONT bodyFont, HFONT titleFont, int innerWidth, int64_t selectedId,
                 const std::wstring& footer);

    //标出选中的帖子（只改描边，不重建）
    void SetSelected(int64_t postId);

    /**
     *  第 index 张卡片的帖子 ID。
     *  @return 帖子 ID；越界时为 0
     */
    int64_t PostIdAt(int index) const;

    //卡片张数（测试用）
    int CardCount() const;

    //卡片内容区的宽度：列表宽减去两侧外边距与卡片内边距，单位：像素。窗口据此排版卡片
    static int CardInnerWidth(int listWidth);

    /**
     *  绘制：先交给 DuiScrollView 画内容与滚动条，再检查是否已滚到接近底部。
     *  @param hdc     宿主的背缓冲 DC
     *  @param rcDirty 需要重画的区域
     */
    virtual void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    std::vector<CThreadPostCard*> m_cards;     //当前的卡片（归内容容器所有，Rebuild 时重建）
    NearBottomFn                  m_nearBottom;//滚到接近底部时的回调；为 NULL 时不回调
    void*                         m_nearUser;  //回调的参数（窗口，不拥有）
    int                           m_contentH;  //内容的总高度（卡片、间距、状态行与上下边距之和），单位：像素
};

} // namespace threadgroup
