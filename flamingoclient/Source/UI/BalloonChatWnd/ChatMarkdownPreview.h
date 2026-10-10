/**
 *  ChatMarkdownPreview.h
 *
 *  显示一篇 Markdown 文档渲染效果的控件（docs/Markdown消息方案.md 第 11.3 节）：Markdown 编辑器右侧的实时预览与
 *  Markdown 预览窗口的正文都用它。
 *
 *  为什么要自绘：balloonui 没有块级富文本渲染，最接近的 DuiRichEdit 显示 RTF 的效果与 DLL 的渲染不一致，用户会看到
 *  「预览一个样、发出去另一个样」。本控件只是一层外壳：解析、排版、绘制与命中全部交给 FlamingoMarkdown.dll
 *  （经 markdown::CMarkdownDocument），自己负责白底、边距、空文档提示、鼠标交互：
 *    - 拖选文字（选区为渲染后文字的下标，由 DLL 命中与绘制），双击全选，Ctrl+C 复制、Ctrl+A 全选；
 *    - 点链接在浏览器里打开（只放行 http、https），右键菜单「复制」「全选」，点在链接上时多一项「复制链接地址」；
 *    - 鼠标所在的代码块右上角显示「复制」按钮，点它复制该代码块的原文。
 *  滚动交给外层的 balloonui DuiScrollView（本控件作为它的内容子，GetDesiredSize 报告排版后的高度，滚动视图开启自动测高）。
 *  没有归入 balloonui 的计划：它的全部能力都来自 DLL，与其它业务无关。
 *
 *  典型用法：
 *      std::unique_ptr<balloonwjui::DuiScrollView> sv(new balloonwjui::DuiScrollView());
 *      std::unique_ptr<ChatMarkdownPreview> view(new ChatMarkdownPreview());
 *      ChatMarkdownPreview* preview = view.get();
 *      sv->SetContent(std::move(view));
 *      sv->SetAutoContentHeight(true);
 *      ...
 *      preview->SetSource(utf8, fontPx, textColor);
 *      sv->ForceLayout(sv->GetRect());
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <string>

#include "../../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../Feature/Markdown/MarkdownDocument.h"

namespace balloonchatwnd {

/**
 *  Markdown 文档的显示控件。
 */
class ChatMarkdownPreview : public balloonwjui::DuiControl
{
public:
    ChatMarkdownPreview();

    /**
     *  设置要显示的源文本与正文样式，清除选区并请求重画。
     *  @param utf8Source 源文本（UTF-8，已把表情换成文字）；为空或全是空白时显示「没有可预览的内容」
     *  @param fontPx     正文字号（像素）
     *  @param textColor  正文颜色（COLORREF）
     */
    void SetSource(const std::string& utf8Source, int fontPx, COLORREF textColor);

    //当前的源文本（UTF-8）
    const std::string& Source() const;

    //当前是否有可显示的渲染结果（DLL 可用、源文本非空且解析成功）
    bool HasContent() const;

    //是否有选中的文字
    bool HasSelection() const;

    //选中的文字（渲染后的文字，换行为 \r\n）；没有选区时为空串
    std::wstring GetSelectedText() const;

    //全选并重画；没有内容时什么也不做
    void SelectAll();

    /**
     *  把选中的部分写进剪贴板：纯文字，DLL 有第四版接口时再加一份这一段的 HTML（粘贴到 Word 等程序时保留格式，
     *  见 UI/BalloonMarkdown/MarkdownClipboard.h）。
     *  @return 有选区并已复制返回 true
     */
    bool CopySelection();

    //DuiControl 覆写：报告排版后的高度（宽度取外层滚动视图的宽度），供 DuiScrollView 自动测高
    SIZE GetDesiredSize() const override;

    //DuiControl 覆写：白底、渲染结果（含选区）或空文档提示
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

    //DuiControl 覆写：点在代码块「复制」按钮上时复制该代码块；否则开始拖选（点在链接上时记下，松开时没有拖动就打开它）
    bool OnLButtonDown(POINT pt, UINT mkFlags) override;

    //DuiControl 覆写：结束拖选；按下与松开都在同一个链接上且没有选出文字时打开链接
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;

    //DuiControl 覆写：双击全选
    bool OnLButtonDblClk(POINT pt, UINT mkFlags) override;

    //DuiControl 覆写：右键菜单「复制」「全选」，点在链接上时多一项「复制链接地址」
    bool OnRButtonDown(POINT pt, UINT mkFlags) override;

    //DuiControl 覆写：拖选时扩展选区；否则更新代码块「复制」按钮与链接的悬停状态
    bool OnMouseMove(POINT pt, UINT mkFlags) override;

    //DuiControl 覆写：鼠标离开后隐藏「复制」按钮、撤掉链接悬停
    bool OnMouseLeave() override;

    //DuiControl 覆写：链接与「复制」按钮上显示手型，文字上显示 I 形
    bool OnSetCursor(POINT pt) override;

    //DuiControl 覆写：有焦点时 Ctrl+C 复制选区、Ctrl+A 全选
    bool OnKeyDown(UINT vk, UINT flags) override;

private:
    //按 width 排版（宽度与样式都不变时 DLL 直接返回缓存结果），返回文档高度；失败时返回 0
    int LayoutAt(int width) const;

    //可用于排版的宽度：外层滚动视图（没有时取自身）的宽度减去左右边距
    int ContentWidth() const;

    //点（宿主坐标）在文档里的命中结果；没有内容时返回 false
    bool HitAt(POINT pt, flamingomd::MdHitResult& out) const;

    //剪贴板的属主窗口（宿主窗口）；没有宿主时为 NULL
    HWND OwnerHwnd() const;

    //在浏览器里打开第 index 个链接（只放行 http、https）
    void OpenLink(int index);

    mutable markdown::CMarkdownDocument m_doc;   //DLL 文档；GetDesiredSize 是 const，排版时需要修改，故为 mutable
    std::string m_source;                        //当前的源文本（UTF-8）
    int         m_fontPx;                        //正文字号（像素）
    COLORREF    m_textColor;                     //正文颜色
    bool        m_hasContent;                    //是否有可显示的渲染结果
    mutable int m_docWidth;                      //最近一次排版得到的文档宽度
    mutable int m_docHeight;                     //最近一次排版得到的文档高度
    int         m_hoverCode;                     //鼠标所在的代码块序号；-1 表示没有
    int         m_hoverCopy;                     //鼠标悬停在其「复制」按钮上的代码块序号；-1 表示没有
    int         m_hoverLink;                     //鼠标悬停的链接序号；-1 表示没有
    int         m_selAnchor;                     //选区起点（渲染后文字的下标）；-1 表示没有选区
    int         m_selCaret;                      //选区终点（渲染后文字的下标）；-1 表示没有选区
    bool        m_selecting;                     //左键按下后正在拖选（已捕获鼠标）
    int         m_pressLink;                     //左键按下时所在的链接序号；-1 表示没有按在链接上
};

} // namespace balloonchatwnd
