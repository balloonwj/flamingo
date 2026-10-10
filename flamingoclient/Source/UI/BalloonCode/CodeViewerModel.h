/**
 *  CodeViewerModel.h
 *
 *  代码查看窗的纯逻辑（docs/代码片段方案.md §3.3、§5.4）：把一段代码按着色结果生成 RTF、算出每一行在
 *  RichEdit 里的起始字符位置（行号栏据此向引擎查询每行的纵坐标）、窗口标题。另存为见 BalloonChatWnd/ChatCodeMessage.h。
 *  不碰窗口与控件，FlamingoUnit 可以直接断言（Tests/CodeViewerTests.cpp，标签 [codeview]）。
 *
 *  典型用法：
 *      const CodeBlockContent content = PrepareCodeBlockContent(code, lang);
 *      const std::string rtf = BuildCodeRtf(content, kCodeViewerFontHalfPoints, tabTwips, L"Microsoft YaHei");
 *      richEdit->LoadRTF(CStringA(rtf.c_str()));
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 <windows.h>（Flamingo 侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <string>
#include <vector>

#include "../BalloonChatWnd/ChatCodeBlock.h"   // CodeBlockContent

namespace ballooncode {

//查看窗正文的字号（半磅，RTF 的 \fs 单位）：10 磅，RichEdit 按窗口的 DPI 换算成像素
const int kCodeViewerFontHalfPoints = 20;

/**
 *  把代码生成 RTF：一张颜色表（每种着色种类一种颜色，见 balloonchatwnd::CodeTokenColor）、全文统一的制表位、
 *  等宽字体 Consolas；中文等宽字符改用 fallbackFont 指定的字体，避免 Consolas 缺字形。
 *  @param content         卡片内容（规范化后的代码与着色区间）
 *  @param fontHalfPoints  字号，半磅；小于 1 时按 kCodeViewerFontHalfPoints
 *  @param tabTwips        一个制表位的宽度（缇）；小于 1 时不写制表位，引擎按默认的半英寸
 *  @param fallbackFont    中文等宽字符用的字体名；只认 ASCII 字符，含其它字符或为空时用 "Microsoft YaHei"
 *  @return RTF 文本，只含 ASCII 字符（非 ASCII 字符写成 \uN?）
 */
std::string BuildCodeRtf(const balloonchatwnd::CodeBlockContent& content, int fontHalfPoints, int tabTwips,
                         const std::wstring& fallbackFont);

/**
 *  每个逻辑行在 RichEdit 里的起始字符位置：RichEdit 用一个 \r 表示段落结束，所以第 k 行的起点是前面各行的
 *  长度（UTF-16 字符数）加上换行数。
 *  @param code 规范化后的代码（换行为 \n）
 *  @param out  [出参] 各行起点，个数等于行数；空代码时只有一个 0
 */
void BuildLineStartPositions(const std::wstring& code, std::vector<long>& out);

/**
 *  在升序的行起点里找出 cp 所在的逻辑行。
 *  @param starts BuildLineStartPositions 的结果
 *  @param cp     字符位置
 *  @return 逻辑行号（从 0 起）；starts 为空时返回 0
 */
int LineOfPosition(const std::vector<long>& starts, long cp);

/**
 *  窗口标题，如「代码 · C/C++ · 42 行」。
 *  @param lang  语言，取值见 codesnippet::CodeLanguage
 *  @param lines 行数
 */
std::wstring CodeViewerTitle(int lang, int lines);

} // namespace ballooncode
