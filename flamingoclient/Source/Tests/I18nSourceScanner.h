/**
 *  多语言守卫用例使用的 C++ 源码扫描器（只供 FlamingoUnit 使用）。
 *
 *  词法规则与提取脚本 tools/i18n_extract.py 保持一致：跳过注释与字符字面量（预处理指令照常扫描，宏定义里的
 *  TR_NOOP 也要收录），识别普通字符串
 *  字面量（L / u8 / u / U 前缀与转义）和原始字符串字面量（R"delim(...)delim"），识别 TR / TRC / TRF / TR_NOOP
 *  以及 UTF-8 版本 TR8 / TRC8 / TRF8 调用的字面量参数，以及原始字符串里内嵌 XML 的 text / placeholder / title / tooltip 属性。两边规则不一致时，
 *  守卫用例 I18G3（.po 与源码同步）会失败，以此发现漂移。
 *
 *  典型用法：
 *      i18nscan::FileScanResult r;
 *      i18nscan::ScanSource(sourceText, r);
 *      // r.m_marked：标记过的文字；r.m_unmarked：没标记的中文字面量；r.m_hasXmlText：是否有含中文的内嵌 XML
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <string>
#include <vector>

namespace i18nscan {

/**
 *  一条标记过（或内嵌 XML 里）的界面文字。字符串均为 UTF-8。
 */
struct MarkedText
{
    std::string m_context;  //上下文（TRC 的第一个参数）；没有时为空串
    std::string m_source;   //原文
    int         m_line;     //所在行号（从 1 起）
};

/**
 *  一处没有标记的中文字面量。
 */
struct UnmarkedLiteral
{
    std::string m_text;     //字面量内容（UTF-8，已还原转义）
    int         m_line;     //所在行号（从 1 起）
};

/**
 *  一个源文件的扫描结果。
 */
struct FileScanResult
{
    std::vector<MarkedText>      m_marked;      //TR 系列标记的文字与内嵌 XML 文字属性里的中文
    std::vector<UnmarkedLiteral> m_unmarked;    //没有标记、不在日志调用里、所在行也没有 i18n-ignore 豁免的中文字面量
    bool                         m_hasXmlText;  //是否有含中文文字属性的内嵌 XML
    bool                         m_callsTranslateXml; //是否调用了 TranslateXml

    FileScanResult() : m_hasXmlText(false), m_callsTranslateXml(false)
    {
    }
};

/**
 *  扫描一个源文件的内容。
 *  @param text   源文件内容（UTF-8，可以带 BOM）
 *  @param result [出参] 扫描结果；调用前的内容会被清空
 */
void ScanSource(const std::string& text, FileScanResult& result);

/**
 *  判断 UTF-8 文字里是否有汉字（U+3400 ~ U+9FFF、U+F900 ~ U+FAFF）。
 */
bool ContainsCjk(const std::string& utf8);

/**
 *  列出客户端生产代码中要扫描的源文件（与 i18n_extract.py 的 SCAN_ROOTS / SKIP_DIRS 一致）。
 *  @param clientRoot flamingoclient 目录的完整路径，不带结尾分隔符
 *  @return 相对 clientRoot 的路径，用正斜杠分隔，如 "Source/App/Startup.cpp"
 */
std::vector<std::string> ListSourceFiles(const std::string& clientRoot);

} // namespace i18nscan
