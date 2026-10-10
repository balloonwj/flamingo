/**
 *  代码块的语法着色（docs/Markdown消息方案.md 第 12 节）：按围栏后写的语言名，用客户端的代码词法分析器
 *  （Source/Common/Code/CodeLexer，直接编进本 DLL）算出代码里每个字符的着色种类，并给出各种类的颜色。
 *  排版（MdLayout）按种类切分片段，绘制（MdPaint）按种类取色，HTML（MdHtml）与 Word（MdDocx）导出按种类加颜色。
 *
 *  只在写了认得的语言名时着色；没写语言名、语言名不认得或为 text / plain 时不着色。不自动识别语言：自动识别
 *  容易把普通文字误判成代码（与 GitHub 等的做法一致）。
 *
 *  颜色与聊天窗代码片段（Source/UI/BalloonChatWnd/ChatCodeBlock.cpp 的 kTokenColors，参照 Visual Studio 浅色主题）
 *  相同，两处是否一致由用例 ML19 逐项核对。
 *
 *  典型用法：
 *      std::vector<unsigned char> kinds;
 *      if (flamingomd::ComputeMdCodeTokenKinds(block.m_codeLang, block.m_code, kinds))
 *          ...kinds[i] 是 block.m_code[i] 的着色种类，按 MdCodeTokenColor(kinds[i]) 取色...
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace flamingomd {

//不着色的种类：用排版样式里的正文颜色。其余取值与 codesnippet::CodeTokenKind 相同
const int kMdCodeTokenPlain = 0;

/**
 *  算出代码里每个字符的着色种类。
 *  @param lang  围栏后写的语言名（如 L"cpp"、L"Python"、L"ts"），大小写不限，可以是 CodeLexer 认得的别名
 *  @param code  代码文字，换行为 \n、\r\n 或 \r 均可
 *  @param kinds 输出：语言名认得时与 code 等长，kinds[i] 为 code[i] 的着色种类（取值见 codesnippet::CodeTokenKind，
 *               不着色的字符为 kMdCodeTokenPlain）；语言名不认得时清空
 *  @return 语言名认得且代码不为空时返回 true
 */
bool ComputeMdCodeTokenKinds(const std::wstring& lang, const std::wstring& code, std::vector<unsigned char>& kinds);

/**
 *  从 start 起着色种类相同的一段的终点。排版、HTML 与 Word 导出都按它切段，保证切出的每一段颜色一致，
 *  且不把代理对（emoji 等）拆成两段：终点落在低位代理之前时把它并进本段。
 *  @param text  文字
 *  @param count 本次切段的范围终点（不含），不超过 text 的长度
 *  @param kinds 与 text 逐字符对齐的着色种类，至少有 count 个
 *  @param start 起点，须小于 count
 *  @return 终点（不含），大于 start、不大于 count
 */
size_t MdCodeColorRunEnd(const wchar_t* text, size_t count, const unsigned char* kinds, size_t start);

/**
 *  着色种类的文字颜色。
 *  @param kind 着色种类，取值见 codesnippet::CodeTokenKind；越界时按普通代码文字处理
 *  @return 颜色，0x00BBGGRR（与 COLORREF 相同）
 */
uint32_t MdCodeTokenColor(int kind);

/**
 *  着色种类的颜色写成 6 位大写十六进制「RRGGBB」（不带 #），HTML 导出在前面加 # 用于 style，Word 导出直接用于 w:color。
 *  @param kind 着色种类，越界时按普通代码文字处理
 */
std::string MdCodeTokenColorHex(int kind);

} // namespace flamingomd
