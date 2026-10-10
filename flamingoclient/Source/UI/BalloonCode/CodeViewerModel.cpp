/**
 *  CodeViewerModel.cpp —— 代码查看窗的纯逻辑，见 CodeViewerModel.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用。
 *  在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-06
 */

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601

#include <windows.h>
#include <stdio.h>

#include <algorithm>
#include <string>
#include <vector>

#include "CodeViewerModel.h"
#include "../../Common/Code/CodeLexer.h"
#include "../../Common/I18n/I18n.h"   // TR / TRF：窗口标题

namespace ballooncode {

namespace {

//RTF 里写多少个制表位：每行缩进超过这么多级的代码很少见，再往后引擎按最后一个制表位的间距继续
const int kRtfTabStopCount = 32;

//中文等宽字符用的字体名含非 ASCII 字符或为空时，退回这个字体
const char* const kDefaultFallbackFont = "Microsoft YaHei";

//RTF 的 \uN 控制字取有符号 16 位整数：码位超过它时要减去 0x10000
const int kRtfUnicodeSignedLimit = 0x7FFF;
const int kRtfUnicodeWrap = 0x10000;

//字体名只认 ASCII 可见字符，其它情况返回默认字体
std::string AsciiFontName(const std::wstring& name)
{
    if (name.empty())
        return kDefaultFallbackFont;
    std::string out;
    for (size_t i = 0; i < name.size(); ++i)
    {
        const wchar_t c = name[i];
        if (c < 0x20 || c >= 0x7F || c == L';' || c == L'{' || c == L'}' || c == L'\\')
            return kDefaultFallbackFont;
        out += static_cast<char>(c);
    }
    return out;
}

//追加一个整数的十进制写法
void AppendInt(std::string& out, int value)
{
    char buf[16];
    sprintf_s(buf, "%d", value);
    out += buf;
}

//把一个字符按 RTF 规则追加：控制字符转义、Tab 与换行写成控制字、非 ASCII 字符写成 \uN?
void AppendRtfChar(std::string& out, wchar_t c)
{
    switch (c)
    {
    //反斜杠是 RTF 控制字的起始字符，要转义
    case L'\\':
        out += "\\\\";
        return;
    //左花括号开始一个 RTF 分组，要转义
    case L'{':
        out += "\\{";
        return;
    //右花括号结束一个 RTF 分组，要转义
    case L'}':
        out += "\\}";
        return;
    //Tab 写成 \tab，按段落里的制表位对齐
    case L'\t':
        out += "\\tab ";
        return;
    //换行写成段落结束
    case L'\n':
        out += "\\par\r\n";
        return;
    //其余字符：ASCII 原样写，其它写成 \uN? 并以问号作不认识 Unicode 时的替代字符
    default:
        break;
    }
    if (c >= 0x20 && c < 0x7F)
    {
        out += static_cast<char>(c);
        return;
    }
    int code = static_cast<int>(c);
    if (code > kRtfUnicodeSignedLimit)
        code -= kRtfUnicodeWrap;
    out += "\\u";
    AppendInt(out, code);
    out += "?";
}

} // anonymous namespace

std::string BuildCodeRtf(const balloonchatwnd::CodeBlockContent& content, int fontHalfPoints, int tabTwips,
                         const std::wstring& fallbackFont)
{
    if (fontHalfPoints < 1)
        fontHalfPoints = kCodeViewerFontHalfPoints;

    std::string rtf;
    rtf.reserve(content.m_code.size() * 2 + 512);
    rtf += "{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1";
    rtf += "{\\fonttbl{\\f0\\fmodern\\fprq1\\fcharset0 Consolas;}{\\f1\\fnil\\fcharset134 ";
    rtf += AsciiFontName(fallbackFont);
    rtf += ";}}";

    //颜色表：第 0 项是「自动」，第 k + 1 项是着色种类 k 的颜色
    rtf += "{\\colortbl;";
    for (int kind = 0; kind < codesnippet::code_token_kind_count; ++kind)
    {
        const COLORREF c = balloonchatwnd::CodeTokenColor(kind);
        rtf += "\\red";
        AppendInt(rtf, GetRValue(c));
        rtf += "\\green";
        AppendInt(rtf, GetGValue(c));
        rtf += "\\blue";
        AppendInt(rtf, GetBValue(c));
        rtf += ";";
    }
    rtf += "}";

    rtf += "\\viewkind4\\pard";
    if (tabTwips > 0)
    {
        for (int i = 1; i <= kRtfTabStopCount; ++i)
        {
            rtf += "\\tx";
            AppendInt(rtf, i * tabTwips);
        }
    }
    rtf += "\\f0\\fs";
    AppendInt(rtf, fontHalfPoints);
    rtf += "\\cf1 ";

    //逐字符写出：着色种类或字体变了才写控制字
    const std::wstring& code = content.m_code;
    const std::vector<codesnippet::CodeToken>& tokens = content.m_tokens;
    size_t cursor = 0;
    int currentKind = codesnippet::code_token_plain;
    bool currentFallback = false;
    for (size_t i = 0; i < code.size(); ++i)
    {
        while (cursor < tokens.size()
               && static_cast<size_t>(tokens[cursor].m_begin + tokens[cursor].m_length) <= i)
        {
            ++cursor;
        }
        int kind = codesnippet::code_token_plain;
        if (cursor < tokens.size() && static_cast<size_t>(tokens[cursor].m_begin) <= i)
            kind = tokens[cursor].m_kind;
        const wchar_t c = code[i];
        //换行不带颜色与字体，不必为它切换
        if (c != L'\n')
        {
            if (kind != currentKind)
            {
                rtf += "\\cf";
                AppendInt(rtf, kind + 1);
                rtf += " ";
                currentKind = kind;
            }
            const bool fallback = (balloonchatwnd::CodeCharColumns(c) == 2);
            if (fallback != currentFallback)
            {
                rtf += fallback ? "\\f1 " : "\\f0 ";
                currentFallback = fallback;
            }
        }
        AppendRtfChar(rtf, c);
    }
    rtf += "}";
    return rtf;
}

void BuildLineStartPositions(const std::wstring& code, std::vector<long>& out)
{
    out.clear();
    out.push_back(0);
    for (size_t i = 0; i < code.size(); ++i)
    {
        //RichEdit 用一个 \r 表示段落结束，与 \n 同样占一个字符位置
        if (code[i] == L'\n')
            out.push_back(static_cast<long>(i + 1));
    }
}

int LineOfPosition(const std::vector<long>& starts, long cp)
{
    if (starts.empty())
        return 0;
    std::vector<long>::const_iterator it = std::upper_bound(starts.begin(), starts.end(), cp);
    if (it == starts.begin())
        return 0;
    return static_cast<int>((it - starts.begin()) - 1);
}

std::wstring CodeViewerTitle(int lang, int lines)
{
    const wchar_t* name = codesnippet::CodeLanguageDisplayName(lang);
    const std::wstring display = (lang == codesnippet::code_lang_plain) ? std::wstring(TR(name)) : std::wstring(name);
    return TRF(L"代码 · {0} · {1} 行", display, static_cast<unsigned int>(lines));
}

} // namespace ballooncode
