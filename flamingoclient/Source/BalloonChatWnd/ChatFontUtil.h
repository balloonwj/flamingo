/**
 *  ChatFontUtil.h
 *
 *  聊天字体的共享工具：当前字体的持久化（读写 flamingo.ini 的 [chatfont] 段，
 *  全 ASCII 数字，符合 flamingo.ini 须保持 ASCII 的约束）、以及把字体应用到
 *  输入框（实现"输入框所见即所得"）。单聊 / 群聊窗共用，避免各写一份。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#pragma once

#include "../net/MessageProtocolClient.h"   // net::TextFont

namespace balloonwjui { class DuiRichEdit; }

namespace balloonchatwnd {

// 从 flamingo.ini [chatfont] 段读取"当前聊天字体"。从未设过返回 m_valid=false
// （此时消息不带字体、输入框用默认外观）。
net::TextFont LoadChatFont();

// 把"当前聊天字体"写回 flamingo.ini [chatfont] 段。m_valid 为 false 时清除标记。
//   font：要保存的字体设置。
void SaveChatFont(const net::TextFont& font);

// 把"当前聊天字体"翻译成一个 LOGFONT + 文字色（字体族固定微软雅黑）。纯函数：
// 不依赖任何控件 / ini / 全局状态，供下面的应用函数与单元测试共用。
// LOGFONT 各字段与普通输入框 SetCtlFont 内部 CreateFont 的取值一一对齐，保证
// 富文本输入框与普通输入框的渲染视觉一致。
//   font：    字体设置；
//   outLf：   出参，填好的 LOGFONT（仅返回 true 时有意义）；
//   outColor：出参，文字色（COLORREF，由 font.m_colorRgb 的 0xRRGGBB 转得）；
//   返回：    font.m_valid==true → 填好两个出参并返回 true；
//             font.m_valid==false → 不动出参、返回 false（调用方据此保留默认外观）。
bool ChatFontToLogFont(const net::TextFont& font, LOGFONT& outLf, COLORREF& outColor);

// 把字体应用到富文本输入框（无窗口富文本控件版本）。
// 因 DuiRichEdit::SetDefaultFontFromHFONT <u>不接管</u> HFONT 所有权，本函数
// 新建的 HFONT 由【调用方】持有：函数把它返回，调用方须保存该句柄，并在下次应用
// 字体 / 窗口析构时 DeleteObject 上一只，避免 GDI 句柄泄漏。
//   edit：富文本输入框（可为 NULL，NULL 时返回 NULL、不做事）；
//   font：要应用的字体；font.m_valid==false 时不改控件并返回 NULL（保留默认外观）。
//   返回：新建并已应用到控件的 HFONT（调用方所有权）；未改动时返回 NULL。
HFONT ApplyChatFontToEdit(balloonwjui::DuiRichEdit* edit, const net::TextFont& font);

} // namespace balloonchatwnd
