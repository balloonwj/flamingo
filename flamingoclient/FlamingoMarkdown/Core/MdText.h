/**
 *  FlamingoMarkdown.dll 内部的文字工具：UTF-8 与 UTF-16 互转、HTML 实体解码、Tab 展开。
 *
 *  典型用法：
 *
 *      std::wstring text = flamingomd::Utf8ToWide(p, n);
 *      std::wstring decoded;
 *      if (!flamingomd::DecodeEntity(L"&amp;", decoded))
 *          decoded = L"&amp;";     //不认识的实体按原样显示
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <string>

namespace flamingomd {

/**
 *  UTF-8 转 UTF-16。非法字节序列替换为 U+FFFD。
 *  @param utf8  源文字，bytes 为 0 时可为 NULL
 *  @param bytes 字节数
 *  @return 转换结果
 */
std::wstring Utf8ToWide(const char* utf8, size_t bytes);

/**
 *  UTF-16 转 UTF-8。不成对的代理项替换为 U+FFFD。
 *  @param text 源文字
 *  @return 转换结果
 */
std::string WideToUtf8(const std::wstring& text);

/**
 *  解码一个 HTML 实体：数字实体（&#123;、&#x1F600;）与常用的命名实体（&amp;、&lt;、&nbsp; 等）。
 *  @param entity 完整的实体文字，含开头的 & 与结尾的 ;
 *  @param out    [出参] 解码成功时写入对应的字符（可能是一对代理项）
 *  @return 认识并解码成功时返回 true；不认识或数值非法时返回 false，out 不变
 */
bool DecodeEntity(const std::wstring& entity, std::wstring& out);

/**
 *  按制表位把 Tab 展开为空格，换行后列号从 0 重新计算。
 *  @param text     源文字，可含 '\n'
 *  @param tabWidth 制表位宽度（列数），大于 0
 *  @return 展开后的文字
 */
std::wstring ExpandTabs(const std::wstring& text, int tabWidth);

/**
 *  判断一个 UTF-16 码元是否为高位代理项（代理对的前半）。
 */
inline bool IsHighSurrogate(wchar_t ch)
{
    return ch >= 0xD800 && ch <= 0xDBFF;
}

/**
 *  判断一个 UTF-16 码元是否为低位代理项（代理对的后半）。
 */
inline bool IsLowSurrogate(wchar_t ch)
{
    return ch >= 0xDC00 && ch <= 0xDFFF;
}

} // namespace flamingomd
