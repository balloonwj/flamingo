/**
 *  机器人管理界面用到的字符串小工具（纯函数，不含界面文字）：UTF-8 与 UTF-16 互转、按 Unicode 字符计数、去掉首尾空白、
 *  「HH:MM」与一天中第几分钟的互转、关键词的拆分与拼接。
 *
 *  计数口径与服务端 BotRules.cpp 的 Utf8Length 相同（一个 Unicode 码点算一个字），本地校验与服务端的上限据此保持一致。
 *
 *  典型用法：
 *      int minute = 0;
 *      if (flamingobot::ParseHhMm("09:00", minute))
 *          ...   // minute == 540
 *      std::vector<std::string> kws = flamingobot::SplitKeywords(L"VPN，密码\n打印");
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace flamingobot {

/**
 *  UTF-8 转 UTF-16。
 *  @param text UTF-8 文本；不合法的字节按系统的替换规则转换
 *  @return UTF-16 文本；输入为空时返回空串
 */
std::wstring Utf8ToWide(const std::string& text);

/**
 *  UTF-16 转 UTF-8。
 *  @param text UTF-16 文本
 *  @return UTF-8 文本；输入为空时返回空串
 */
std::string WideToUtf8(const std::wstring& text);

/**
 *  UTF-8 文本的 Unicode 字符数（码点数）。
 */
int Utf8CharCount(const std::string& text);

/**
 *  UTF-16 文本的 Unicode 字符数（一对代理项算一个字）。
 */
int WideCharCount(const std::wstring& text);

/**
 *  去掉 UTF-16 文本首尾的空白（半角空格、全角空格、制表符与换行）。
 */
std::wstring TrimWide(const std::wstring& text);

/**
 *  去掉 UTF-8 文本首尾的空白（与 TrimWide 同一组空白字符）。
 */
std::string TrimUtf8(const std::string& text);

/**
 *  解析「HH:MM」（小时一位或两位、分钟两位，与服务端 ParseHhMm 的口径相同）。
 *  @param text   如 "9:05"、"18:00"
 *  @param minute [出参] 一天中的第几分钟（0～1439）；解析失败时为 0
 *  @return 格式正确且在一天之内返回 true
 */
bool ParseHhMm(const std::string& text, int& minute);

/**
 *  一天中的第几分钟写成「HH:MM」；负数按 0 处理，超过一天的取余。
 */
std::string FormatHhMm(int minute);

/**
 *  把输入框里的关键词拆成列表：按换行、半角或全角逗号、分号、顿号分隔，每项去掉首尾空白，丢掉空项与重复项。
 *  @param text 输入框里的文字
 *  @return 关键词（UTF-8），按输入顺序
 */
std::vector<std::string> SplitKeywords(const std::wstring& text);

/**
 *  把关键词列表拼成输入框里的文字：每行一个。
 */
std::wstring JoinKeywords(const std::vector<std::string>& keywords);

/**
 *  把十进制文字解析成非负整数（只允许数字，首尾空白忽略）。
 *  @param text  输入框里的文字
 *  @param value [出参] 解析结果
 *  @return 是十进制非负整数且不超过 int 范围时返回 true
 */
bool ParseNonNegativeInt(const std::wstring& text, int& value);

/**
 *  "#RRGGBB" 解析成 COLORREF 的三个分量。
 *  @param text  颜色文字（大小写均可）
 *  @param color [出参] 0x00BBGGRR（与 Windows 的 RGB 宏相同的排列）
 *  @return 格式正确返回 true
 */
bool ParseHexColor(const std::string& text, uint32_t& color);

/**
 *  COLORREF 排列（0x00BBGGRR）的颜色写成 "#RRGGBB"（大写）。
 */
std::string FormatHexColor(uint32_t color);

} // namespace flamingobot
