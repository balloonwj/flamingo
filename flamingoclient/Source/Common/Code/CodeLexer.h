/**
 *  CodeLexer.h
 *
 *  聊天「代码片段」的语法着色与语言识别（纯逻辑，不依赖界面与 GDI）。
 *
 *  不引入第三方高亮库：聊天里的高亮只需要分出关键字、类型、字符串、注释、数字、预处理指令等几类，
 *  按语言配一张表、对全文做一次线性扫描即可。这是近似着色，不是语法分析：JS 的正则字面量、
 *  Shell 的 heredoc 等写法可能着色不准，但不会影响代码原文的显示与复制。
 *
 *  典型用法：
 *      std::vector<codesnippet::CodeToken> tokens;
 *      const int lang = codesnippet::DetectCodeLanguage(code);
 *      codesnippet::TokenizeCode(code, lang, tokens);
 *      //tokens 只含需要着色的区间（按起点升序、互不重叠），区间之外的文字按普通文字画
 *
 *  语言的协议标识（CodeLanguageId 的返回值）将来写进消息 content，取值固定，不随显示名变化。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <string>
#include <vector>

namespace codesnippet {

/**
 *  代码片段的语言。数值只在本进程内使用，跨进程（消息协议）一律用 CodeLanguageId 给出的字符串标识。
 */
enum CodeLanguage
{
    code_lang_plain      = 0,    //纯文本：不着色，只按等宽字体保持格式；识别不出语言时也取这个值
    code_lang_cpp        = 1,    //C / C++（含头文件）
    code_lang_csharp     = 2,    //C#
    code_lang_java       = 3,    //Java
    code_lang_python     = 4,    //Python
    code_lang_javascript = 5,    //JavaScript 与 TypeScript（两者共用一张表）
    code_lang_go         = 6,    //Go
    code_lang_rust       = 7,    //Rust
    code_lang_sql        = 8,    //SQL（关键字不区分大小写）
    code_lang_shell      = 9,    //Shell 脚本（sh / bash）
    code_lang_json       = 10,   //JSON（键名与字符串值分开着色）
    code_lang_xml        = 11,   //XML 与 HTML（标签、属性、属性值分开着色）
    code_lang_lua        = 12,   //Lua
    code_lang_php        = 13,   //PHP
    code_lang_count      = 14,   //语言总数，不是一种语言；用于遍历与越界判断
};

/**
 *  词元的着色种类。只有需要着色的种类才会出现在 TokenizeCode 的输出里，普通文字不输出。
 */
enum CodeTokenKind
{
    code_token_plain        = 0,    //普通文字（标识符、运算符、空白）：不输出，绘制时用默认文字色
    code_token_keyword      = 1,    //关键字，如 if、return、class
    code_token_type         = 2,    //内置类型与常用类型名，如 int、string；以及 class / struct 后面声明的类型名
    code_token_string       = 3,    //字符串与字符字面量（含 #include <...> 里的头文件名）
    code_token_number       = 4,    //数字字面量
    code_token_comment      = 5,    //注释（行注释与块注释）
    code_token_preprocessor = 6,    //预处理指令、注解 / 装饰器、Rust 宏与属性、PHP 起止标记、Shell 的 #! 行
    code_token_function     = 7,    //函数名：紧跟左括号的标识符
    code_token_tag          = 8,    //XML / HTML 的标签名与尖括号
    code_token_attr         = 9,    //XML / HTML 的属性名、JSON 的键名
    code_token_variable     = 10,   //Shell 与 PHP 的 $ 变量
    code_token_kind_count   = 11,   //种类总数，不是一种种类；用于遍历与越界判断
};

/**
 *  一个需要着色的区间。下标与长度按 UTF-16 字符计，指向 TokenizeCode 的输入串。
 */
struct CodeToken
{
    int m_begin;    //起始下标
    int m_length;   //长度，恒大于 0
    int m_kind;     //着色种类，取值见 CodeTokenKind（不会是 code_token_plain）

    CodeToken()
        : m_begin(0)
        , m_length(0)
        , m_kind(code_token_plain)
    {
    }

    CodeToken(int begin, int length, int kind)
        : m_begin(begin)
        , m_length(length)
        , m_kind(kind)
    {
    }
};

/**
 *  语言的协议标识，如 "cpp"、"python"；纯文本为 "text"。
 *  @param lang 语言，取值见 CodeLanguage；越界时按纯文本处理
 *  @return 静态字符串，调用方不得释放
 */
const char* CodeLanguageId(int lang);

/**
 *  由协议标识或常见别名（"c"、"h"、"py"、"js"、"ts"、"sh"、"html" 等，不区分大小写）得到语言。
 *  @param id 标识；空串或不认识的标识返回纯文本
 *  @return 语言，取值见 CodeLanguage
 */
int CodeLanguageFromId(const std::string& id);

/**
 *  语言的显示名，如 "C/C++"、"Python"。纯文本返回经 TR_NOOP 标记的原文「纯文本」，
 *  调用方显示前须再经 TR 翻译（本模块不依赖语言包，便于单测）。
 *  @param lang 语言，取值见 CodeLanguage；越界时按纯文本处理
 *  @return 静态字符串，调用方不得释放
 */
const wchar_t* CodeLanguageDisplayName(int lang);

/**
 *  另存为文件时用的扩展名（不含点），如 "cpp"、"py"；纯文本为 "txt"。
 *  @param lang 语言，取值见 CodeLanguage；越界时按纯文本处理
 *  @return 静态字符串，调用方不得释放
 */
const wchar_t* CodeLanguageFileExtension(int lang);

/**
 *  对代码做语法着色。
 *  @param code 代码原文（换行可以是 \n、\r\n 或 \r）
 *  @param lang 语言，取值见 CodeLanguage；纯文本或越界时不输出任何词元
 *  @param out  [出参] 先清空，再按起点升序填入互不重叠的着色区间
 */
void TokenizeCode(const std::wstring& code, int lang, std::vector<CodeToken>& out);

/**
 *  按特征猜测代码的语言（只看前 kDetectSampleChars 个字符）。
 *  @param code 代码原文
 *  @return 语言，取值见 CodeLanguage；特征不足时返回 code_lang_plain
 */
int DetectCodeLanguage(const std::wstring& code);

/**
 *  判断一段文字像不像代码，供粘贴时决定是否按代码片段显示。
 *  规则：能识别出语言的算代码；识别不出的，至少三行、行首缩进或行尾为 ; { } 的行占三成以上、
 *  且括号与运算符等符号足够密集的也算代码；中文占比超过三成的一般是正文，不算代码。
 *  @param text 文字原文
 *  @return 像代码时返回 true
 */
bool LooksLikeCode(const std::wstring& text);

} // namespace codesnippet
