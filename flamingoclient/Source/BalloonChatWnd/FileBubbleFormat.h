/**
 *  FileBubbleFormat.h
 *
 *  聊天文件气泡的纯逻辑显示格式化助手：把文件字节数格式化成人类可读的大小文字
 *  （如 4404019 → "4.2 MB"），以及从文件名取出小写扩展名作类型标签（如
 *  "季度报告.PDF" → "pdf"）。两者都不依赖 ATL / balloonui / 网络，只用 std::wstring，
 *  便于单元测试，见 flamingoclient/Source/Tests/FileBubbleFormatTests.cpp。
 *
 *  balloonwj@qq.com   2026-06-05
 */
#pragma once

#include <string>

namespace balloonchatwnd {

/**
 *  把文件字节数格式化成人类可读的大小文字。
 *  规则：< 1024 字节按整数 B；KB / MB / GB 各保留一位小数、四舍五入。
 *  例：0→"0 B"，512→"512 B"，1024→"1.0 KB"，4404019→"4.2 MB"，2147483648→"2.0 GB"。
 *  @param bytes 文件字节数（负数按 0 处理）
 *  @return 大小文字（宽字符）
 */
std::wstring FormatFileSize(long long bytes);

/**
 *  从文件名取出小写扩展名，作文件气泡左侧的类型标签（如 "PDF"/"DOCX" 图标里的字）。
 *  取最后一个 '.' 之后的部分并转小写、只保留 ASCII 字符；多重扩展名取最后一段
 *  （"x.tar.gz"→"gz"）；无扩展名（无 '.'、'.' 在首位的隐藏文件、'.' 在末尾）返回空串。
 *  @param fileName 文件名（含扩展名，宽字符；不应含目录部分）
 *  @return 小写扩展名；无扩展名返回空串
 */
std::wstring FileTypeLabelFromName(const std::wstring& fileName);

/**
 *  把一个时间戳格式化成「相对当下」的简短文字，用于群文件行的「上传时间」展示。
 *  规则（均按本地时区的日历日判断）：
 *    · 与 now 同一天 → "今天 HH:MM"；
 *    · now 的前一天 → "昨天"；
 *    · 更早但在 6 天内 → 中文星期几（"周一".."周日"）；
 *    · 更早、同一年 → "M/D"（如 "5/15"）；
 *    · 更早、跨年 → "YYYY/M/D"（如 "2025/12/3"）。
 *  取本地时区是有意为之——展示给本地用户看。把 now 作参数传入（而非内部取系统时间），
 *  既便于单元测试，也让同一次渲染里多条目用同一基准时刻。
 *  @param uploadMs 待格式化的时间戳（Unix 毫秒）；<=0 返回空串
 *  @param nowMs    当下基准时间戳（Unix 毫秒），通常传 time(NULL)*1000
 *  @return 相对时间文字（宽字符）
 */
std::wstring FormatRelativeDayTime(long long uploadMs, long long nowMs);

} // namespace balloonchatwnd
