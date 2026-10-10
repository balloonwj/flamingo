/**
 *  gettext .po 译文文件的解析。
 *
 *  只支持本项目用到的子集：msgctxt / msgid / msgstr 三个关键字、多行字符串拼接、\" \\ \n \t \r 五种转义、
 *  # 开头的注释与 #~ 开头的作废条目。不支持复数形式（msgid_plural / msgstr[n]），遇到时按坏条目处理。
 *  解析结果保持 UTF-8 原样，转成 UTF-16 由 CLangTable 负责。
 *
 *  坏条目（缺引号、缺 msgstr、未知关键字、中途截断等）整条跳过并计数，不影响其它条目；任何输入都不会使解析崩溃。
 *
 *  典型用法：
 *      flamingolang::PoParseResult parsed;
 *      flamingolang::ParsePo(data, size, parsed);
 *      table.Build(parsed);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace flamingolang {

/**
 *  .po 里的一个有效条目（不含文件头条目、空译文条目与作废条目）。字符串均为 UTF-8，已还原转义。
 */
struct PoEntry
{
    std::string m_context;  //msgctxt 的值；没有 msgctxt 时为空串（空上下文与没有上下文同等对待）
    std::string m_msgid;    //原文，非空
    std::string m_msgstr;   //译文，非空
    uint32_t    m_line;     //条目第一行的行号（从 1 起），报告问题时用

    PoEntry() : m_line(0)
    {
    }
};

/**
 *  一次解析的结果。
 */
struct PoParseResult
{
    std::vector<PoEntry> m_entries;         //有效条目，顺序与文件中一致
    uint32_t             m_errors;          //跳过的坏条目数
    uint32_t             m_emptyCount;      //译文为空、视为尚未翻译的条目数（不算错误）
    uint32_t             m_firstErrorLine;  //第一个坏条目所在的行号（从 1 起）；没有坏条目时为 0

    PoParseResult() : m_errors(0), m_emptyCount(0), m_firstErrorLine(0)
    {
    }
};

/**
 *  解析一段 .po 文本。开头的 UTF-8 BOM 会被跳过，LF 与 CRLF 换行都可以。
 *  @param data UTF-8 文本，可以不以 '\0' 结尾；为 NULL 时视为空文本
 *  @param size data 的字节数
 *  @param out  [出参] 解析结果；调用前的内容会被清空
 */
void ParsePo(const char* data, size_t size, PoParseResult& out);

} // namespace flamingolang
