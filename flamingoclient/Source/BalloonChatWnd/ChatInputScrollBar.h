/**
 *  ChatInputScrollBar.h
 *
 *  给聊天输入框（DuiEditHost 内部托管的 Win32 EDIT）装一条「与接收区一致」的
 *  细圆角半透明灰色滚动条：默认不显示，仅当内容超出框高时才出现；浮在右侧、
 *  只占一条很窄的 gutter，样式对齐 ChatMessageList 自绘的那条细滑块。
 *
 *  做法（不改 balloonui）：
 *    1) 去掉 EDIT 的原生竖直滚动条（创建期带的 WS_VSCROLL），改由我们自绘；
 *       EDIT 仍保留 ES_AUTOVSCROLL，照常用滚轮/光标滚动，只是没有原生灰条。
 *    2) 复用 DuiEditHost 的右侧 gutter 图标 API（SetIcon(RightIcon,…)）预留一条
 *       窄 gutter，并在其 painter 里按 EDIT 当前滚动状态自绘细圆角灰滑块；内容
 *       不溢出时 painter 什么都不画。
 *    3) 另挂一个子类过程在内容/滚动变化时让 gutter 局部重绘，使滑块跟手。
 *
 *  单聊窗 / 群聊窗共用本模块。
 *
 *  balloonwj@qq.com   2026-06-03
 */
#pragma once

#include <windows.h>

namespace balloonwjui { class DuiEditHost; }
namespace balloonwjui { class DuiRichEditHost; }

namespace balloonchatwnd {

/**
 *  给聊天输入框装自绘滚动条。须在 input->EnsureCreated() 成功之后调用一次
 *  （此时内部 EDIT 子窗口才存在）；若因 SetMultiLine 等触发 HWND 重建，重建后
 *  需再调一次。
 *    edit：聊天输入框控件（DuiEditHost，已 EnsureCreated）；为 NULL 时直接返回。
 *  副作用：去掉 EDIT 的 WS_VSCROLL、占用其右侧约 9px 作 gutter（输入文字区相应
 *          右缩这点宽度）、并在 EDIT 子窗口挂一段重绘子类过程（EDIT 销毁时自摘）。
 *
 *  说明：聊天输入框现已迁成富文本 DuiRichEditHost（见下方同名重载）；本 DuiEditHost
 *  版保留给仍用 Win32 EDIT 输入框的旧场景，新代码用 RichEdit 重载。
 */
void AttachChatInputScrollBar(balloonwjui::DuiEditHost* edit);

/**
 *  富文本输入框（DuiRichEditHost / 内嵌 RICHEDIT50W）版：装一条与接收区
 *  ChatMessageList 完全一致的细圆角半透明灰滑块——内容不溢出不出现，溢出时浮在
 *  右沿（overlay，不占内容宽度），可拖动、点轨道翻页。
 *
 *  做法（不改 balloonui）：RichEdit 子窗口盖在 DUI 合成层之上，无法靠 DUI OnPaint
 *  在其内容上叠画，故给 RichEdit 子窗口挂一段子类过程——WM_PAINT 默认绘制后在右沿
 *  叠画细滑块、右侧窄条命中拖拽、滚动/编辑后局部标脏。滚动度量与定位复用
 *  DuiRichEditHost 的像素级接口（GetVScrollMetrics / SetVScrollPos）。
 *
 *    re：富文本输入框控件（DuiRichEditHost，已 EnsureCreated）；NULL 时直接返回。
 *  前置：调用方须在 EnsureCreated 之前 re->SetShowVScroll(false) 关掉 RichEdit
 *        原生竖直滚动条（本函数只负责自绘滑块，不动窗口风格位）。
 *  副作用：在 RichEdit 子窗口挂一段子类过程（子窗口销毁时自摘并释放其状态）。
 *  单聊窗 / 群聊窗共用。
 */
void AttachChatInputScrollBar(balloonwjui::DuiRichEditHost* re);

} // namespace balloonchatwnd
