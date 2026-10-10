/**
 *  CodeFormatter.h —— 代码片段的格式化（docs/代码片段方案.md 第 10.4 节）。
 *
 *  不引入第三方库，按语言分几档处理：
 *    · 所有语言：换行统一为 LF，去掉行尾空白、开头与末尾的空白行，连续三行以上的空行压成一行，
 *      去掉所有非空行共同的前导缩进；
 *    · C/C++、C#、Java、JavaScript/TypeScript、Go、Rust、PHP：按括号重新缩进（字符串与注释里的括号不计数），
 *      C/C++ 与 C# 的预处理行顶格，跨行的注释与字符串保持相对缩进；
 *    · JSON：完整美化，键的顺序与数字、字符串的写法原样保留；不是合法 JSON 时只做通用整理；
 *    · XML / HTML：按标签层数重新缩进，不拆行；
 *    · SQL：关键字转大写；SELECT / INSERT / UPDATE / DELETE / WITH 开头的语句按子句分行；
 *    · Python、Shell、Lua、纯文本：只做通用整理。
 *  缩进单位沿用代码原有的风格（DetectIndentUnit）。全部是纯函数，不碰控件，FlamingoUnit 直接断言（[codeformat]）。
 *
 *  典型用法（发送代码窗口的「格式化」）：
 *      const codesnippet::CodeFormatResult r = codesnippet::FormatCode(text, lang);
 *      if (r.m_changedLines > 0)
 *          editor->ReplaceSel(r.m_text.c_str(), true);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <string>

namespace codesnippet {

/**
 *  格式化的结果。
 */
struct CodeFormatResult
{
    std::wstring m_text;          //格式化后的代码：换行为 LF，末尾不带换行
    int          m_changedLines;  //与原文相比变了的行数（按行做最长公共子序列比较，不在公共子序列里的输出行数）；0 表示没有变化
    bool         m_jsonInvalid;   //按 JSON 处理时原文不是合法的 JSON，只做了通用整理

    CodeFormatResult()
        : m_changedLines(0)
        , m_jsonInvalid(false)
    {
    }
};

/**
 *  格式化一段代码。
 *  @param code 代码原文（换行可以是 CRLF、CR 或 LF）
 *  @param lang 语言，取值见 codesnippet::CodeLanguage；越界按纯文本
 *  @return 格式化结果；原文全是空白时 m_text 为空串
 */
CodeFormatResult FormatCode(const std::wstring& code, int lang);

/**
 *  格式化选中的几行：把它们当作一段独立的代码格式化，再给每个非空行加上第一个非空行原有的缩进，与上下文对齐。
 *  @param lines 选中内容覆盖到的整行（换行可以是 CRLF、CR 或 LF）
 *  @param lang  语言，取值见 codesnippet::CodeLanguage
 *  @return 格式化结果；m_changedLines 按选中的几行计
 */
CodeFormatResult FormatCodeSelection(const std::wstring& lines, int lang);

/**
 *  判断代码的缩进单位：多数缩进行以 Tab 开头时为一个 Tab；否则取各行行首空格数的最大公约数，
 *  为 2 或 4 时用该数目的空格；判断不出时为 4 个空格。
 *  @param code 代码（换行可以是 CRLF、CR 或 LF）
 *  @return 一级缩进的字符串
 */
std::wstring DetectIndentUnit(const std::wstring& code);

} // namespace codesnippet
