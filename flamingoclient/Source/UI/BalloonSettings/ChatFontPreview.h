/**
 *  ChatFontPreview.h
 *
 *  系统设置「聊天」页「默认聊天字体」一行里的预览：按全局默认聊天字体的字号、样式、
 *  颜色给一个 DuiLabel 套上外观。设置过时显示预览文字，未设置时显示灰色的「未设置」。
 *
 *  单独成模块是为了能被 FlamingoUnit 直接测（见 Tests/ChatFontPrefsTests.cpp 的 CF5）：
 *  设置对话框本身不在测试工程里。本 .cpp 不 include stdafx.h，自带 ATL/WTL 前置依赖，
 *  在 Flamingo.vcxproj 里标记 PrecompiledHeader=NotUsing。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  典型用法：
 *      HFONT m_hPreviewFont = NULL;   //对话框持有，析构时 DeleteObject
 *      ApplyChatFontPreview(label, cfg.ChatFonts().GetDefault(), m_hPreviewFont);
 *
 *  balloonwj@qq.com   2026-09-30
 */

#pragma once

#include "../../net/MessageProtocolClient.h"   // net::TextFont

namespace balloonwjui { class DuiLabel; }

namespace balloonsettings {

// 全局默认聊天字体设置过时，预览标签显示的文字。
extern const TCHAR* const kChatFontPreviewText;

// 全局默认聊天字体未设置时，预览标签显示的文字。
extern const TCHAR* const kChatFontUnsetText;

// 未设置时预览文字的颜色（灰），与设置页各行副标题同色。
const COLORREF kChatFontUnsetColor = RGB(0x8A, 0x88, 0x83);

/**
 *  按字体设置给预览标签套外观。
 *    label：  预览标签（可为 NULL，NULL 时只处理 ioFont）；
 *    font：   全局默认聊天字体；m_valid 为 false 表示未设置；
 *    ioFont： [入出参] 预览标签当前使用的字体句柄，归调用方所有（DuiLabel 不接管字体
 *             所有权）。本函数先让标签换用新字体，再释放传入的旧句柄，并把新句柄写回；
 *             未设置时标签退回默认字体，ioFont 置为 NULL。
 */
void ApplyChatFontPreview(balloonwjui::DuiLabel* label, const net::TextFont& font, HFONT& ioFont);

} // namespace balloonsettings
