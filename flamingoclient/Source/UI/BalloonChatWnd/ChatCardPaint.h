/**
 *  ChatCardPaint.h
 *
 *  聊天卡片共用的绘制函数（docs/聊天卡片美化方案.md 第 3 节）：白色底板、类型色图标块、状态标签、三级按钮、
 *  底部一行（左边类型标签、右边按钮）、说明块与进度条。各类卡片的绘制代码只决定「画什么、放在哪」，
 *  外观一律交给这里，保证所有卡片逐像素同一套样式。
 *
 *  形状用 GDI+，文字用 chatpaint::DrawGdiText（GDI，与气泡文字同一种渲染）。调用方传入的 Graphics 须已开启抗锯齿。
 *  底部一行的排版是纯函数（LayoutCardFooter），绘制与命中测试用同一份结果，不会出现按钮画在一处、点在另一处。
 *  典型用法：
 *      cardpaint::PaintCardFrame(g, cardRc);
 *      cardpaint::PaintCardTile(g, left, top, ChatCardTypeStyleOf(msg));
 *      const cardpaint::CardFooterLayout lay = cardpaint::PaintCardFooter(hdc, g, footerRc, style, label, buttons, hover);
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL（CString）与 <gdiplus.h>。
 *
 *  balloonwj@qq.com   2026-10-14
 */
#pragma once

#include <vector>

#include "ChatCardModel.h"   // ChatCardTypeStyle / ChatCardButton / 标签语义与按钮级别

namespace balloonchatwnd { namespace cardpaint {

/**
 *  状态标签的两种颜色。
 */
struct CardChipColors
{
    COLORREF m_text;   // 文字与圆点的颜色
    COLORREF m_fill;   // 底色
};

/**
 *  取状态标签的颜色（3.4 节）。
 *  @param tone 标签语义，取值见 ChatCardChipTone；kCardChipNone 与未知值按「已结束」的灰色处理
 *  @return 文字色与底色
 */
CardChipColors CardChipColorsOf(int tone);

/**
 *  按钮在某个状态下的颜色（3.5 节）。
 */
struct CardButtonColors
{
    bool     m_hasFill;     // 是否填充底色（次要按钮没有底色）
    COLORREF m_fill;        // 底色，m_hasFill 为 true 时有效
    bool     m_hasBorder;   // 是否描 1 像素边框（只有查看按钮有）
    COLORREF m_border;      // 边框色，m_hasBorder 为 true 时有效
    COLORREF m_text;        // 文字色
    bool     m_bold;        // 文字是否半粗（只有主要按钮）
};

/**
 *  取按钮的颜色。
 *  @param level 级别，取值见 ChatCardButtonLevel；未知值按查看按钮处理
 *  @param state 显示状态，取值见 ChatCardButtonState
 *  @return 颜色
 */
CardButtonColors CardButtonColorsOf(int level, int state);

/**
 *  画卡片底板：向下 1 像素的淡投影、白色底、1 像素暖灰边框、圆角 10。
 *  @param g  目标 Graphics（已开启抗锯齿）
 *  @param rc 底板矩形（不含投影；投影画在矩形下方 1 像素处）
 */
void PaintCardFrame(Gdiplus::Graphics& g, const RECT& rc);

/**
 *  画标题前的图标块：36×36、圆角 9 的浅色底，居中一个 20 像素的线条图标。style.m_hasTile 为 false 时不画。
 *  @param g     目标 Graphics（已开启抗锯齿）
 *  @param left  图标块左边 x
 *  @param top   图标块顶边 y
 *  @param style 卡片的类型样式（ChatCardTypeStyleOf 的结果）
 */
void PaintCardTile(Gdiplus::Graphics& g, int left, int top, const ChatCardTypeStyle& style);

/**
 *  量一个状态标签的宽度（左右内边距 + 圆点 + 间距 + 文字）。
 *  @param hdc  用于量宽的设备上下文
 *  @param text 标签文字；为空时返回 0
 *  @return 宽度（像素）
 */
int MeasureCardChipWidth(HDC hdc, const CString& text);

/**
 *  画一个状态标签：高 20、两端半圆的浅色底，左侧一个 6 像素圆点，后面是 11 像素文字；在 rowTop ~ rowBottom 之间垂直居中。
 *  @param hdc       目标设备上下文（画文字）
 *  @param g         同一设备上下文上的 Graphics（画底与圆点，已开启抗锯齿）
 *  @param left      标签左边 x
 *  @param rowTop    所在行的上边
 *  @param rowBottom 所在行的下边
 *  @param text      标签文字；为空或 tone 为 kCardChipNone 时不画
 *  @param tone      语义，取值见 ChatCardChipTone
 *  @return 画了返回标签右边的 x；没画返回 left
 */
int PaintCardChip(HDC hdc, Gdiplus::Graphics& g, int left, int rowTop, int rowBottom, const CString& text, int tone);

/**
 *  量一个按钮的宽度：主要、查看按钮为文字宽加左右各 14 像素内边距，次要按钮就是文字宽。
 *  @param hdc   用于量宽的设备上下文
 *  @param text  按钮文字
 *  @param level 级别，取值见 ChatCardButtonLevel
 *  @return 宽度（像素）
 */
int MeasureCardButtonWidth(HDC hdc, const CString& text, int level);

/**
 *  画一个按钮（胶囊或文字），文字在 rc 里水平、垂直居中。
 *  @param hdc   目标设备上下文（画文字）
 *  @param g     同一设备上下文上的 Graphics（画胶囊，已开启抗锯齿）
 *  @param rc    按钮矩形（高为 cardstyle::kButtonH）
 *  @param text  按钮文字
 *  @param level 级别，取值见 ChatCardButtonLevel
 *  @param state 显示状态，取值见 ChatCardButtonState
 */
void PaintCardButton(HDC hdc, Gdiplus::Graphics& g, const RECT& rc, const CString& text, int level, int state);

/**
 *  底部一行的排版结果。
 */
struct CardFooterLayout
{
    RECT              m_labelRect;     // 左侧类型标签（图标 + 文字）占用的矩形；放不下时为空矩形
    bool              m_labelTruncated;// 类型标签是否因为按钮太宽而被截短（文字以省略号收尾）
    std::vector<RECT> m_buttonRects;   // 各按钮的矩形，与按钮列表一一对应、顺序相同；也是按钮的命中矩形
};

/**
 *  排底部一行（纯函数，单测 CS5）：按钮在右侧从右往左排，最右一个距右边 14、相邻间距 14、垂直居中；
 *  类型标签从左边 14 处开始，最多排到最左边那个按钮左侧再留 10 像素的位置，放不下就截短，按钮始终完整。
 *  @param footerRc     底部一行的矩形（分隔线下方，高为 cardstyle::kFooterH，左右为卡片两边）
 *  @param labelWidth   类型标签的完整宽度（图标 + 间距 + 文字）
 *  @param buttonWidths 各按钮的宽度，按显示顺序从左到右
 *  @return 排版结果
 */
CardFooterLayout LayoutCardFooter(const RECT& footerRc, int labelWidth, const std::vector<int>& buttonWidths);

/**
 *  画底部一行：上方 1 像素分隔线，左侧类型标签（图标 + 文字），右侧按钮。
 *  @param hdc        目标设备上下文
 *  @param g          同一设备上下文上的 Graphics（已开启抗锯齿）
 *  @param footerRc   底部一行的矩形（高为 cardstyle::kFooterH，左右为卡片两边）；分隔线画在它上方紧邻的一行，
 *                    调用方排版时在它上面留出 cardstyle::kFooterDividerH
 *  @param style      卡片的类型样式，取类型标签的图标与颜色
 *  @param label      类型标签文字（ChatCardTypeLabel 的结果）
 *  @param buttons    按钮列表，按显示顺序从左到右
 *  @param hoverIndex 鼠标悬停的按钮下标；-1 表示没有。置灰的按钮即使悬停也按置灰画
 *  @return 排版结果，命中测试照它的 m_buttonRects 判断
 */
CardFooterLayout PaintCardFooter(HDC hdc, Gdiplus::Graphics& g, const RECT& footerRc, const ChatCardTypeStyle& style,
                                 const CString& label, const std::vector<ChatCardButton>& buttons, int hoverIndex);

/**
 *  按点击位置找底部一行里的按钮：只认可用的按钮，置灰的不命中。
 *  @param layout  PaintCardFooter / LayoutCardFooter 的结果
 *  @param buttons 按钮列表，与排版时相同
 *  @param pt      点击位置（与排版矩形同一坐标系）
 *  @return 命中的按钮下标；没有命中可用按钮时为 -1
 */
int CardFooterButtonAt(const CardFooterLayout& layout, const std::vector<ChatCardButton>& buttons, POINT pt);

/**
 *  画说明块的浅色底（圆角 6），文字由调用方画在 rc 内缩 kDescPadH / kDescPadV 的区域里。
 *  @param g  目标 Graphics（已开启抗锯齿）
 *  @param rc 说明块矩形
 */
void PaintCardDescBlock(Gdiplus::Graphics& g, const RECT& rc);

/**
 *  画进度条：浅色底槽，已完成部分为品牌橙（上传、下载、文件夹传输一律用它）。
 *  @param g       目标 Graphics（已开启抗锯齿）
 *  @param rc      进度条矩形（高为 cardstyle::kProgressH）
 *  @param percent 进度 0~100，超出范围时按边界处理
 */
void PaintCardProgress(Gdiplus::Graphics& g, const RECT& rc, int percent);

}} // namespace balloonchatwnd::cardpaint
