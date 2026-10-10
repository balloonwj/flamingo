/**
 *  ChatCodeCardControl.h
 *
 *  可以单独放进任意布局里的代码片段卡片控件（docs/代码片段方案.md §3.4、§5.6）：外观与聊天窗里的代码卡片
 *  完全一致（排版、绘制、点击判断都转调 ChatCodeBlock.h 的共用函数）。消息记录窗与聊天记录查看窗的代码片段行用它。
 *
 *  交互与聊天窗相同：点「复制」把原文写进剪贴板，顶部改为「已复制」，鼠标离开卡片后恢复；点卡片正文或底栏
 *  「查看全部（共 N 行）」时向宿主窗口投递 WM_CHAT_OPEN_CODE_VIEWER（lParam 为堆上的 CodeViewerOpenRequest，
 *  宿主处理后 delete）。超过折叠行数时只显示前面几行：列表的行高是预先按宽度算好的，不在卡片里展开，
 *  看全文改为打开代码查看窗。
 *
 *  存在理由：与 ChatFileCardControl 相同，balloonui 里没有能画这张卡片的控件，聊天气泡里的绘制又与整行消息
 *  （头像、名字、气泡方向）绑在一起。本类只包一层 DuiControl，内容全部来自与气泡共用的 ChatCodeBlock，
 *  不另起一套绘制（方案第 5.6 节已确认）。
 *
 *  典型用法（列表按宽度算行高，再按同一宽度建控件）：
 *      const SIZE size = ChatCodeCardControl::MeasureCard(code, lang, maxWidth);
 *      std::unique_ptr<ChatCodeCardControl> card(new ChatCodeCardControl());
 *      card->SetCode(code, lang, key);
 *      layout->AddChild(std::move(card), Hint().Fixed(size.cy, size.cx).AlignC(DuiLayout::AlignNear));
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <string>

#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "ChatCodeBlock.h"   // CodeBlockContent / CodeBlockLayout

namespace balloonchatwnd {

/**
 *  代码片段卡片控件。宽高由布局给定，应为 MeasureCard 按同一最大宽度量出的尺寸。
 */
class ChatCodeCardControl : public balloonwjui::DuiControl
{
public:
    ChatCodeCardControl();

    /**
     *  量一张卡片的尺寸（与本控件绘制时的排版相同：折叠显示、底栏为「查看全部」）。只用屏幕 DC 测量，无副作用。
     *  @param code     代码原文（换行可以是 CRLF、CR 或 LF）
     *  @param lang     语言，取值见 codesnippet::CodeLanguage；越界按纯文本
     *  @param maxWidth 卡片最大宽度（像素）；卡片按最长的行收窄，但不小于卡片的最小宽度（maxWidth 更小时取 maxWidth）
     *  @param notes    批注（顶部的说明多出「· N 条批注」，卡片可能变宽）；没有批注时为空
     *  @return 卡片宽高（像素）
     */
    static SIZE MeasureCard(const std::wstring& code, int lang, int maxWidth,
                            const std::vector<codesnippet::CodeNote>& notes = std::vector<codesnippet::CodeNote>());

    /**
     *  设置卡片显示的代码并重画。
     *  @param code 代码原文，按值保存（规范化后保存）
     *  @param lang 语言，取值见 codesnippet::CodeLanguage
     *  @param key  打开代码查看窗时用的消息标识（服务器 msgid 的十进制串）：同一条消息再次打开时调到前台，不重复创建
     *  @param notes 批注，行号指规范化后的代码：卡片标出被批注的行，打开代码查看窗时一并带上；没有批注时为空
     */
    void SetCode(const std::wstring& code, int lang, const std::string& key,
                 const std::vector<codesnippet::CodeNote>& notes = std::vector<codesnippet::CodeNote>());

    //DuiControl 覆写：按控件宽度排版（宽度不变时沿用上次结果）并画卡片。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;
    //DuiControl 覆写：按光标位置更新悬停的部位（「复制」与底栏加下划线），变化时重画。返回 true。
    bool OnMouseMove(POINT pt, UINT mkFlags) override;
    //DuiControl 覆写：光标离开时撤掉悬停效果，「已复制」恢复为「复制」。返回 true。
    bool OnMouseLeave() override;
    //DuiControl 覆写：光标在卡片上时显示手形并返回 true；否则返回 false。
    bool OnSetCursor(POINT pt) override;
    //DuiControl 覆写：按在「复制」上时复制原文；按在正文或底栏上时请宿主打开代码查看窗。处理了返回 true。
    bool OnLButtonDown(POINT pt, UINT mkFlags) override;
    //DuiControl 覆写：忽略双击并返回 true。双击的第一下已经作为单击处理过；不忽略的话双击会继续传给所在的行，
    //消息记录窗会把它当作行双击再处理一次。
    bool OnLButtonDblClk(POINT pt, UINT mkFlags) override;

private:
    //控件宽度变了（或还没有排过版）时重新排版；hdc 只用于测量。
    void EnsureLayout(HDC hdc);

    //光标所在的部位，取值见 CodeBlockPart；还没有排过版或光标在卡片外时为 kCodePartNone。
    //  pt：宿主客户区坐标
    int PartAtPoint(POINT pt) const;

    //把原文写进剪贴板，顶部改为「已复制」。
    void CopyCode();

    //请宿主窗口打开代码查看窗；宿主窗口无效时什么都不做。
    void PostViewerOpen();

private:
    CodeBlockContent m_content;       //卡片内容（规范化后的代码、着色区间、行数），由 SetCode 生成
    std::string      m_key;           //打开代码查看窗时用的消息标识，由 SetCode 设置
    std::vector<codesnippet::CodeNote> m_notes;   //批注（含说明文字），由 SetCode 设置，打开代码查看窗时带上
    CodeBlockLayout  m_layout;        //最近一次排版的结果；m_layoutWidth 为 -1 时无效
    int              m_layoutWidth;   //最近一次排版时的控件宽度（像素）；-1 表示需要重新排版
    int              m_hoverPart;     //光标悬停的部位，取值见 CodeBlockPart（只记「复制」与底栏）
    bool             m_copied;        //刚点过「复制」：顶部显示「已复制」，光标离开卡片时恢复
};

} // namespace balloonchatwnd
