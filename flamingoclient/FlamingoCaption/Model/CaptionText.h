/**
 *  语音字幕的文字工具（docs/语音字幕方案.md 6.2、3.5），CaptionText.h
 *
 *  纯函数，不依赖界面与识别引擎：
 *    - 显示宽度：中日韩文字与全角字符算 2 个单位，其余算 1 个单位，字幕条切分按这个宽度控制每行长度；
 *    - 文字整理：去掉首尾空白、合并连续空白、去掉全角标点前后多余的空格（识别结果常带「模式， 两边」这种空格）；
 *    - 标点判断：句末与句中断开处的标点、行首标点（端点切在标点之前时，下一句以「。」「，」开头）；
 *    - 查找：区分或不区分大小写（只对 ASCII 字母生效），可要求英文整词匹配；
 *    - UTF-8 与 UTF-16 的互转。
 *
 *  典型用法：
 *      std::wstring t = caption::NormalizeText(raw);
 *      int units = caption::DisplayUnits(t);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stddef.h>
#include <string>

namespace caption {

//一个中日韩文字或全角字符所占的显示宽度单位
const int kWideCharUnits = 2;
//其余字符（ASCII 字母、数字、半角标点、空格）所占的显示宽度单位
const int kNarrowCharUnits = 1;

/**
 *  是否「宽字符」：中日韩统一表意文字、平假名片假名、韩文音节、全角标点与全角形式。
 *  @param ch UTF-16 码元；代理对的高位码元按宽字符计（扩展区汉字），低位码元由 DisplayUnits 跳过
 */
bool IsWideChar(wchar_t ch);

/**
 *  文字的显示宽度（单位见 kWideCharUnits / kNarrowCharUnits）；换行符不计宽度。
 */
int DisplayUnits(const std::wstring& text);

/**
 *  是否「断开处标点」：句末标点（。！？.!?）与句中停顿标点（，；：、,;:），字幕条切分优先在它们之后断开。
 */
bool IsBreakPunct(wchar_t ch);

/**
 *  是否句末标点（。！？.!?…），断开的优先级高于句中停顿标点。
 */
bool IsSentenceEndPunct(wchar_t ch);

/**
 *  文字开头连续的断开处标点的个数（端点切在标点之前时，下一句会以这些标点开头）。
 */
size_t LeadingPunctLength(const std::wstring& text);

/**
 *  整理识别结果或用户输入的文字：去掉首尾空白，连续空白合并为一个空格，去掉全角标点前后的空格，
 *  去掉两个宽字符之间的空格。换行符保留（用户手工分行）。
 */
std::wstring NormalizeText(const std::wstring& text);

/**
 *  拼接两段文字：两边相邻处都是 ASCII 字母或数字时中间加一个空格，否则直接相连。
 */
std::wstring JoinText(const std::wstring& left, const std::wstring& right);

/**
 *  在 text 中从 from 开始查找 needle。
 *  @param matchCase  为 false 时 ASCII 字母不区分大小写（其余字符照常比较）
 *  @param wholeWord  为 true 时，若 needle 首尾是 ASCII 字母或数字，则要求匹配处两侧不是 ASCII 字母或数字
 *  @return 匹配处的下标；找不到或 needle 为空返回 std::wstring::npos
 */
size_t FindInText(const std::wstring& text, const std::wstring& needle, size_t from, bool matchCase, bool wholeWord);

/**
 *  把 text 中所有匹配 needle 的地方换成 replacement（匹配规则同 FindInText），从左到右、不重叠。
 *  @param count [出参，可为 NULL] 替换的次数
 */
std::wstring ReplaceInText(const std::wstring& text, const std::wstring& needle, const std::wstring& replacement,
                           bool matchCase, bool wholeWord, int* count);

//UTF-8 转 UTF-16；非法字节按 Windows 的默认处理（替换为 U+FFFD）
std::wstring Utf8ToWide(const std::string& utf8);

//UTF-16 转 UTF-8
std::string WideToUtf8(const std::wstring& wide);

} // namespace caption
