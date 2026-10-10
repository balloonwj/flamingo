/**
 *  ChatMarkdownFormatBar.h
 *
 *  Markdown 编辑器顶部格式按钮的横向布局（docs/Markdown消息方案.md 第 11.3 节；最初为聊天窗输入区的格式条而写，
 *  输入区的 Markdown 模式已删除，见第 11.2 节）：在 balloonui DuiHBox 的基础上，宽度不够时从末尾往前隐藏格式按钮，
 *  不让按钮越过容器的右边缘。
 *
 *  为什么要子类化：DuiHBox 在固定宽度的子控件总宽超过容器时不压缩，后面的子控件直接越过右边缘。编辑器工具栏里，
 *  格式按钮这一段夹在「打开 / 保存 / 导出」与状态文字、「预览」按钮之间，按剩余宽度伸缩（MarkdownEditorLayout.cpp），
 *  窗口缩窄或译文较长时一行放不下。这里只覆写 Layout：先算出需要的宽度，放不下时隐藏排在后面的格式按钮，再交给
 *  DuiHBox 排版；容器变宽后恢复。被隐藏的格式只是少了按钮，用户仍可直接输入 Markdown 标记。
 *
 *  典型用法（子控件顺序：前 n 个为可隐藏的格式按钮，其后可再放不参与隐藏的控件，如弹性空白）：
 *      std::unique_ptr<ChatMarkdownFormatBar> bar(new ChatMarkdownFormatBar());
 *      bar->AddChild(...格式按钮 x n...);
 *      bar->SetCollapsibleCount(n);
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include "../../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"

namespace balloonchatwnd {

/**
 *  宽度不够时从末尾往前隐藏前 n 个子控件的横向布局。子控件的显隐由本类在 Layout 里管理，调用方不要再对前 n 个
 *  子控件调用 SetVisible。
 */
class ChatMarkdownFormatBar : public balloonwjui::DuiHBox
{
public:
    ChatMarkdownFormatBar();

    /**
     *  设置可隐藏的子控件个数：按添加顺序的前 count 个子控件，空间不够时从第 count 个往前依次隐藏。
     *  @param count 个数；小于 0 按 0 处理，超过子控件数时按子控件数处理（在 Layout 时截断）
     */
    void SetCollapsibleCount(int count);

    //当前因宽度不够而隐藏的子控件个数（最近一次 Layout 的结果）
    int GetHiddenCount() const;

    /**
     *  按 rcAvail 的宽度决定隐藏几个可隐藏的子控件，再按 DuiHBox 的规则排版。
     *  @param rcAvail 本控件的矩形（宿主坐标）
     */
    void Layout(const RECT& rcAvail) override;

private:
    //子控件 child 在主轴上占用的固定宽度（含左右外边距；自动档取期望宽度；按权重分配宽度的子控件为外边距之和）
    int FixedWidthOf(balloonwjui::DuiControl* child) const;

    int m_collapsibleCount;   //可隐藏的子控件个数（前 m_collapsibleCount 个），默认 0
    int m_hiddenCount;        //最近一次 Layout 隐藏的个数，0 表示全部显示
};

} // namespace balloonchatwnd
