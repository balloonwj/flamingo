/**
 *  汉字 → 拼音首字母 转换（客户端搜索用）。
 *
 *  把一段文本逐字符转成"拼音首字母串"，与原文**等长（1:1 映射）**——便于在
 *  首字母串上做子串匹配后，把命中区间原样映射回原文做高亮。
 *
 *  覆盖范围：GB2312 一级常用汉字（约 3755 字）用区位码边界法映射；ASCII 字母
 *  取小写；其它字符（数字/标点/未覆盖汉字）保持 1:1 但不参与字母匹配。未覆盖
 *  的生僻字/GBK 扩展字得不到首字母（仍可由昵称原文子串命中），属已知取舍。
 *
 *  纯逻辑，不依赖 UI，便于单元测试，见
 *  flamingoclient/Source/Tests/PinyinInitialsTests.cpp。
 *
 *  balloonwj@qq.com   2026-06-01
 */
#pragma once

#include <string>

namespace pinyin {

/**
 *  取文本的拼音首字母串（与 text 等长，1:1 映射）。
 *  @param text 源文本（UTF-16）
 *  @return 首字母串（小写）；第 i 个字符对应 text 的第 i 个字符：汉字为其拼音
 *          首字母（GB2312 一级字），ASCII 字母为其小写，其它字符为占位符
 *          '\x01'（不会与正常查询匹配）。
 */
std::string Initials(const std::wstring& text);

} // namespace pinyin
