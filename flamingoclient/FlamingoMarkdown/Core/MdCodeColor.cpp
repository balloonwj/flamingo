/**
 *  代码块语法着色的实现，说明见 MdCodeColor.h。
 *
 *  词法分析器 CodeLexer 是纯标准 C++（不依赖 ATL、balloonui 与 Windows API），工程里直接编译
 *  ..\Source\Common\Code\CodeLexer.cpp，不增加 DLL 的导入项（MDG2 守住导入表）。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include "MdCodeColor.h"

#include <windows.h>
#include <stdio.h>

#include "MdText.h"
#include "../../Source/Common/Code/CodeLexer.h"

namespace flamingomd {

namespace {

//各着色种类的文字颜色，下标为 codesnippet::CodeTokenKind。与聊天窗代码片段（ChatCodeBlock.cpp 的 kTokenColors）
//逐项相同，参照 Visual Studio 浅色主题；改动时两处一起改，ML19 核对
const COLORREF kTokenColors[codesnippet::code_token_kind_count] =
{
    RGB(0x1F, 0x1F, 0x1F),   // code_token_plain：普通文字
    RGB(0x00, 0x00, 0xFF),   // code_token_keyword：关键字
    RGB(0x26, 0x7F, 0x99),   // code_token_type：类型名
    RGB(0xA3, 0x15, 0x15),   // code_token_string：字符串
    RGB(0x09, 0x86, 0x58),   // code_token_number：数字
    RGB(0x00, 0x80, 0x00),   // code_token_comment：注释
    RGB(0x80, 0x80, 0x80),   // code_token_preprocessor：预处理指令、注解、宏
    RGB(0x79, 0x5E, 0x26),   // code_token_function：函数名
    RGB(0x80, 0x00, 0x00),   // code_token_tag：XML / HTML 标签
    RGB(0x04, 0x51, 0xA5),   // code_token_attr：XML / HTML 属性名、JSON 键名
    RGB(0x00, 0x10, 0x80),   // code_token_variable：$ 变量
};

//「RRGGBB」加结尾 0 的缓冲区大小
const size_t kHexColorBufferSize = 8;

//UTF-16 低位代理的范围
const wchar_t kLowSurrogateFirst = 0xDC00;
const wchar_t kLowSurrogateLast = 0xDFFF;

} // namespace

size_t MdCodeColorRunEnd(const wchar_t* text, size_t count, const unsigned char* kinds, size_t start)
{
    size_t end = start + 1;
    while (end < count && kinds[end] == kinds[start])
        ++end;
    //不拆开代理对：终点处是低位代理时并进本段
    while (end < count && text[end] >= kLowSurrogateFirst && text[end] <= kLowSurrogateLast)
        ++end;
    return end;
}

bool ComputeMdCodeTokenKinds(const std::wstring& lang, const std::wstring& code, std::vector<unsigned char>& kinds)
{
    kinds.clear();
    if (lang.empty() || code.empty())
        return false;
    //CodeLexer 按 ASCII 不区分大小写匹配语言标识与别名；认不出（含 text / plain）时为纯文本
    const int language = codesnippet::CodeLanguageFromId(WideToUtf8(lang));
    if (language == codesnippet::code_lang_plain)
        return false;

    std::vector<codesnippet::CodeToken> tokens;
    codesnippet::TokenizeCode(code, language, tokens);
    const int length = static_cast<int>(code.size());
    kinds.assign(code.size(), static_cast<unsigned char>(kMdCodeTokenPlain));
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        const codesnippet::CodeToken& t = tokens[i];
        if (t.m_kind <= kMdCodeTokenPlain || t.m_kind >= codesnippet::code_token_kind_count)
            continue;
        const int begin = (t.m_begin < 0) ? 0 : t.m_begin;
        const int end = (t.m_begin + t.m_length > length) ? length : t.m_begin + t.m_length;
        for (int c = begin; c < end; ++c)
            kinds[static_cast<size_t>(c)] = static_cast<unsigned char>(t.m_kind);
    }
    return true;
}

uint32_t MdCodeTokenColor(int kind)
{
    if (kind < 0 || kind >= codesnippet::code_token_kind_count)
        kind = kMdCodeTokenPlain;
    return static_cast<uint32_t>(kTokenColors[kind]);
}

std::string MdCodeTokenColorHex(int kind)
{
    const uint32_t color = MdCodeTokenColor(kind);
    char buf[kHexColorBufferSize] = { 0 };
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%02X%02X%02X", GetRValue(color), GetGValue(color), GetBValue(color));
    return std::string(buf);
}

} // namespace flamingomd
