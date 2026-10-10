/**
 *  ChatMarkdownCardControl.h
 *
 *  可以单独放进任意布局里的 Markdown 消息卡片控件（docs/Markdown消息方案.md 第 12 节）：内容与聊天窗里的 Markdown
 *  卡片完全一致（宽度、标题与摘要、高度与内容绘制都转调 ChatMarkdownCard.h 的共用函数），底色与圆角与聊天窗里
 *  「别人发的消息」气泡相同。消息记录窗、聊天记录管理器与聊天记录查看窗的 Markdown 消息行用它。
 *
 *  交互与聊天窗相同：单击卡片向宿主窗口投递 WM_CHAT_OPEN_MARKDOWN_PREVIEW（lParam 为堆上的
 *  MarkdownPreviewOpenRequest，宿主处理后 delete），宿主据此打开 Markdown 预览窗口。双击不再往下传，免得所在的行
 *  把它当作行双击（「定位到聊天窗」）再处理一次。
 *
 *  存在理由：与 ChatCodeCardControl 相同，balloonui 里没有能画这张卡片的控件，聊天气泡里的绘制又与整行消息（头像、
 *  名字、气泡方向）绑在一起。本类只包一层 DuiControl，内容全部来自与气泡共用的 ChatMarkdownCard，不另起一套绘制。
 *
 *  典型用法（列表按宽度算行高，再按同一宽度建控件）：
 *      const SIZE size = ChatMarkdownCardControl::MeasureCard(summary, maxWidth);
 *      std::unique_ptr<ChatMarkdownCardControl> card(new ChatMarkdownCardControl());
 *      card->SetCard(info);
 *      layout->AddChild(std::move(card), Hint().Fixed(size.cy, size.cx).AlignC(DuiLayout::AlignNear));
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <string>

#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "ChatMarkdownCard.h"   // MarkdownCardInfo

namespace balloonchatwnd {

/**
 *  Markdown 消息卡片控件。宽高由布局给定，应为 MeasureCard 按同一最大宽度量出的尺寸。
 */
class ChatMarkdownCardControl : public balloonwjui::DuiControl
{
public:
    ChatMarkdownCardControl();

    /**
     *  量一张卡片的尺寸：宽度为卡片宽度（kMarkdownCardWidth）与 maxWidth 中较小的一个，高度随摘要的行数变化（最多三行）。
     *  只用屏幕 DC 测量，无副作用。
     *  @param summary  卡片摘要，可为空
     *  @param maxWidth 允许的最大宽度（像素）
     *  @return 卡片宽高（像素）
     */
    static SIZE MeasureCard(const std::wstring& summary, int maxWidth);

    /**
     *  设置卡片内容并重画。
     *  @param info 卡片内容，按值保存
     */
    void SetCard(const MarkdownCardInfo& info);

    //取卡片内容（单元测试核对用）
    const MarkdownCardInfo& GetCard() const;

    //DuiControl 覆写：画与「别人发的消息」气泡相同的圆角底色，再画卡片内容；摘要高度按控件宽度测量（宽度不变时沿用）。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;
    //DuiControl 覆写：光标在卡片上时显示手形并返回 true；否则返回 false。
    bool OnSetCursor(POINT pt) override;
    //DuiControl 覆写：按在卡片上时请宿主打开 Markdown 预览窗口。处理了返回 true。
    bool OnLButtonDown(POINT pt, UINT mkFlags) override;
    //DuiControl 覆写：忽略双击并返回 true。双击的第一下已经作为单击处理过；不忽略的话双击会继续传给所在的行，
    //消息记录窗会把它当作行双击再处理一次。
    bool OnLButtonDblClk(POINT pt, UINT mkFlags) override;

private:
    //请宿主窗口打开 Markdown 预览窗口；宿主窗口无效时什么都不做。
    void PostPreviewOpen();

private:
    MarkdownCardInfo m_info;          //卡片内容，由 SetCard 设置
    int              m_summaryH;      //摘要折行后的高度（像素），按 m_measuredWidth 测量；-1 表示需要重新测量
    int              m_measuredWidth; //最近一次测量摘要时的控件宽度（像素）
};

} // namespace balloonchatwnd
