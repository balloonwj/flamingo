/**
 *  ChatFontUtil.h
 *
 *  聊天字体的共享工具：把字体翻译成 LOGFONT、以及把字体应用到输入框（实现"输入框
 *  所见即所得"）。单聊 / 群聊窗共用，避免各写一份。字体本身存在账号配置里（全局
 *  默认 + 各会话自定义，见 Session/ChatFontPrefs.h），不在这里读写。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#pragma once

#include "../../net/MessageProtocolClient.h"   // net::TextFont

namespace balloonwjui { class DuiRichEdit; }

namespace balloonchatwnd {

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

// 把富文本输入框恢复成默认外观：字体退回 balloonui 默认字体，文字色退回 defaultColor。
// 当前会话既没有自定义字体、也没有全局默认字体时调用。调用之后输入框不再使用此前经
// ApplyChatFontToEdit 设置的 HFONT，调用方即可 DeleteObject 那一只。
//   edit：        富文本输入框（可为 NULL，NULL 时不做事）；
//   defaultColor：输入框原本的文字色 —— 聊天窗在应用任何聊天字体之前用 GetTextColor 记下。
void ResetChatInputFont(balloonwjui::DuiRichEdit* edit, COLORREF defaultColor);

} // namespace balloonchatwnd
