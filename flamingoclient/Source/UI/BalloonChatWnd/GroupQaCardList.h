/**
 *  GroupQaCardList.h
 *
 *  群聊窗「问答」页签（docs/群问答方案.md）的卡片列表：每条问答一张卡片，依次是问题（加粗）、来源与修改人、
 *  答案全文、答案里的网址链接（点了用浏览器打开），以及底部「发到群里」「修改」「删除」按钮。
 *
 *  控件全部是 balloonui 现成控件的组合（开发约定文档 UI 约定第 2 条）：滚动视口复用备忘页签的
 *  balloonmemo::CreateMemoCardScroll（DuiScrollView 子类，统一的悬浮式细滑块）；卡片是 DuiVBox 圆角浅底，
 *  文字是 DuiLabel，链接是链接模式的 DuiLabel，按钮是 DuiButton。不新增自绘控件。
 *
 *  卡片高度随问题与答案的长短变化，按视口宽度用 DuiLabel::MeasureHeight 量出；宿主在面板宽度变化时重新调用
 *  SetQaCards。卡片里每个控件的 ctrlId 都落在本卡片的 id 段内（GroupQaModel.h 的 QaCardCtrlId），
 *  宿主用 QaCardFromCtrlId 反推是哪张卡片的哪个按钮。
 *
 *  典型用法：
 *      std::vector<QaCardText> cards = ...;
 *      SetQaCards(scroll, cards, kPanelBg);
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <string>
#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // DuiScrollView

namespace balloonchatwnd {

// 一张问答卡片要显示的内容（均已是界面文字）
struct QaCardText
{
    std::wstring              m_question;  // 问题
    std::wstring              m_meta;      // 来源与修改人一行，如「企业问答」「张三 修改于 10-07 14:30」
    std::wstring              m_answer;    // 答案全文
    std::vector<std::wstring> m_links;     // 答案里的网址（ExtractQaUrls 的结果），最多 kQaMaxLinksPerCard 个
    bool                      m_editable;  // 是否显示「修改」「删除」

    QaCardText()
        : m_editable(false)
    {
    }
};

/**
 *  用 cards 整体替换视口里的卡片，保留原来的滚动位置。旧卡片全部销毁。
 *  不要在卡片里某个控件发来的通知处理过程中调用：那个控件会被销毁。按钮命令先 PostMessage 给自己再处理。
 *    scroll： balloonmemo::CreateMemoCardScroll 建的视口，借用；为 NULL 或宽度为 0（还没排过版）时什么都不做；
 *    cards：  各卡片的内容，按显示顺序；超过 kQaCardMaxCount 的部分不显示；
 *    outerBg：卡片外面的底色，与面板一致。
 *  @return 实际建出的卡片数；视口不可用时返回 -1，宿主应在视口排过版之后再调一次
 */
int SetQaCards(balloonwjui::DuiScrollView* scroll, const std::vector<QaCardText>& cards, COLORREF outerBg);

/**
 *  按给定内容宽度算一张卡片的高度（逻辑像素）。问题与答案按宽度折行后量高，网址每个一行。
 *    card：卡片内容；
 *    cardWidth：卡片的宽度（含左右内边距）。
 */
int QaCardHeight(const QaCardText& card, int cardWidth);

} // namespace balloonchatwnd
