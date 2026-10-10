/**
 *  ChatMarkdownIcons.h
 *
 *  Markdown 相关的线性图标（docs/Markdown消息方案.md 第 11.3 节）：聊天窗工具栏的「Markdown 编辑器」按钮与消息卡片
 *  底部的「M↓」、Markdown 编辑器的格式按钮。由 ChatIcons.cpp 的 DrawChatIcon
 *  在遇到 kChatIconMarkdown 起的那一段种类时转调。几何同样按 viewBox 24 给出，调用方已经把 viewBox 映射到目标区域。
 *
 *  本文件遵循 ChatIcons.h 的约定：包含它之前调用方须已引入 windows.h 与 gdiplus.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <windows.h>
#include <gdiplus.h>

namespace balloonchatwnd {

/**
 *  画一个 Markdown 相关的图标。坐标系已由调用方变换为 viewBox 24。
 *  @param g     目标 Graphics（已开启抗锯齿、已完成 viewBox 变换）
 *  @param kind  图标种类，取值为 ChatIconKind 里 kChatIconMarkdown ~ kChatIconMdTable 与 kChatIconMdUnderline ~ kChatIconMdAlert；其它值不绘制
 *  @param pen   描边用的笔（圆头圆角，颜色与线宽已设好）
 *  @param color 图标颜色（填充用）
 *  @return 认识该种类并绘制了返回 true
 */
bool DrawMarkdownChatIcon(Gdiplus::Graphics& g, int kind, Gdiplus::Pen& pen, const Gdiplus::Color& color);

} // namespace balloonchatwnd
