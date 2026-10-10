/**
 *  语音字幕的时间格式（docs/语音字幕方案.md 6.2、第 8 节），CaptionTime.h
 *
 *  SRT 文件里的时间写成「时:分:秒,毫秒」（如 00:01:02,345），读取时也接受用点号分隔毫秒；
 *  编辑器界面上的时间写成「分:秒.十分之一秒」（不足一小时）或「时:分:秒.十分之一秒」。
 *
 *  典型用法：
 *      std::string s = caption::FormatSrtTime(62345);        //"00:01:02,345"
 *      int64_t ms = 0;
 *      caption::ParseSrtTime("00:01:02,345", &ms);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <string>

namespace caption {

/**
 *  格式化为 SRT 的时间「HH:MM:SS,mmm」；负数按 0 处理，小时超过 99 时照常写出更多位。
 */
std::string FormatSrtTime(int64_t ms);

/**
 *  解析 SRT 的时间：「H:MM:SS,mmm」或「H:MM:SS.mmm」，小时可为 1 位或多位，毫秒可为 1～3 位（按小数理解）。
 *  @param text 时间文字，前后可以有空白
 *  @param ms   [出参] 毫秒数
 *  @return 格式正确返回 true
 */
bool ParseSrtTime(const std::string& text, int64_t* ms);

/**
 *  格式化为界面上的时间：不足一小时为「M:SS.d」，否则为「H:MM:SS.d」（d 为十分之一秒，向下取整）。
 */
std::wstring FormatClock(int64_t ms);

} // namespace caption
