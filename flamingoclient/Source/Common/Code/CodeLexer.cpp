/**
 *  CodeLexer.cpp —— 代码片段的语法着色与语言识别，见 CodeLexer.h。
 *
 *  整体结构：
 *    · kLangDefs 每种语言一行，写明关键字、类型名、注释写法与各种字符串写法（kFlag* 标志位）；
 *    · CodeScanner 按语言表对全文做一次从左到右的扫描，遇到注释、字符串、数字、标识符等就记一个着色区间；
 *      XML / HTML 的结构与程序语言差别太大，走单独的 ScanMarkup；
 *    · DetectCodeLanguage 按特征文字打分，LooksLikeCode 在它之外再看行的形态与符号密度。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用。
 *  在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "CodeLexer.h"

#include <wchar.h>
#include <set>

#include "../I18n/I18n.h"   // TR_NOOP：标记「纯文本」供提取脚本收录

namespace codesnippet {

namespace {

// ---- 语言特性标志位（LangDef::m_flags）----
const unsigned kFlagDoubleQuote     = 1u << 0;    //双引号字符串，反斜杠转义，不跨行
const unsigned kFlagSingleQuoteStr  = 1u << 1;    //单引号字符串（Python、JS、SQL、Shell、Lua、PHP）
const unsigned kFlagSingleQuoteChar = 1u << 2;    //单引号字符字面量，只含一个字符或一个转义序列（C 系、Go、Rust）
const unsigned kFlagBacktickString  = 1u << 3;    //反引号字符串，可以跨行（JS 模板字符串、Go 原始字符串、Shell 命令替换）
const unsigned kFlagTripleQuote     = 1u << 4;    //三引号字符串，可以跨行（Python）
const unsigned kFlagHashDirective   = 1u << 5;    //行首 # 开头的预处理指令（C/C++、C#）
const unsigned kFlagAtAnnotation    = 1u << 6;    //@ 开头的注解 / 装饰器（Java、Python、JS/TS）
const unsigned kFlagCaseInsensitive = 1u << 7;    //关键字与类型名不区分大小写（SQL）
const unsigned kFlagDollarVariable  = 1u << 8;    //$ 开头的变量（Shell、PHP）
const unsigned kFlagRustMacro       = 1u << 9;    //标识符后紧跟 ! 是宏调用，#[...] 是属性（Rust）
const unsigned kFlagMarkup          = 1u << 10;   //标记语言，走单独的 ScanMarkup（XML / HTML）
const unsigned kFlagLuaLongBracket  = 1u << 11;   //[[ ]] 长字符串与 --[[ ]] 块注释（Lua）
const unsigned kFlagFunctionCall    = 1u << 12;   //紧跟左括号的标识符按函数名着色
const unsigned kFlagVerbatimString  = 1u << 13;   //@"..." 逐字字符串，"" 表示一个引号，可以跨行（C#）
const unsigned kFlagQuoteDoubling   = 1u << 14;   //字符串里连写两个引号表示一个引号（SQL）
const unsigned kFlagHashNeedsSpace  = 1u << 15;   //# 只有在行首或空白、分号之后才算注释（Shell：a#b 不是注释）
const unsigned kFlagJsonKeys        = 1u << 16;   //后面紧跟冒号的字符串按键名着色（JSON）
const unsigned kFlagPhpTags         = 1u << 17;   //<?php、<?= 与 ?> 按预处理标记着色（PHP）
const unsigned kFlagCppRawString    = 1u << 18;   //R"分隔符( ... )分隔符" 原始字符串（C++）
const unsigned kFlagPythonPrefix    = 1u << 19;   //紧贴引号的前缀 r、b、f、u 及其组合并入字符串（Python）
const unsigned kFlagTypeIntro       = 1u << 20;   //class、struct 等关键字后面的标识符按类型名着色

//C 系语言（C/C++、C#、Java、JS、Go、Rust）共有的特性
const unsigned kFlagsCFamily = kFlagDoubleQuote | kFlagFunctionCall | kFlagTypeIntro;

/**
 *  一种语言的着色规则。kLangDefs 的下标与 CodeLanguage 的取值一致。
 */
struct LangDef
{
    int            m_lang;           //语言，取值见 CodeLanguage，与所在下标相同
    const char*    m_id;             //协议标识
    const char*    m_aliases;        //别名，逗号分隔、小写；没有时为空串
    const wchar_t* m_displayName;    //显示名
    const wchar_t* m_keywords;       //关键字，空格分隔；不区分大小写的语言写小写
    const wchar_t* m_types;          //内置类型与常用类型名，空格分隔；不区分大小写的语言写小写
    const wchar_t* m_lineComment;    //行注释前缀；没有时为 NULL
    const wchar_t* m_lineComment2;   //第二种行注释前缀（PHP 的 #）；没有时为 NULL
    const wchar_t* m_blockOpen;      //块注释开始；没有时为 NULL
    const wchar_t* m_blockClose;     //块注释结束；m_blockOpen 为 NULL 时无意义
    unsigned       m_flags;          //语言特性，kFlag* 的按位或
};

const LangDef kLangDefs[code_lang_count] =
{
    //纯文本：不着色
    { code_lang_plain, "text", "plain,txt,plaintext", TR_NOOP(L"纯文本"),
      NULL, NULL, NULL, NULL, NULL, NULL, 0 },

    { code_lang_cpp, "cpp", "c,cc,cxx,h,hh,hpp,hxx,c++,objc", L"C/C++",
      L"alignas alignof and asm auto break case catch class const constexpr const_cast continue co_await "
      L"co_return co_yield decltype default delete do dynamic_cast else enum explicit export extern false "
      L"final for friend goto if inline mutable namespace new noexcept not nullptr NULL operator or override "
      L"private protected public register reinterpret_cast return sizeof static static_assert static_cast "
      L"struct switch template this thread_local throw true try typedef typeid typename union using virtual "
      L"volatile while __declspec __stdcall __cdecl",
      L"bool char char8_t char16_t char32_t wchar_t double float int long short signed unsigned void size_t "
      L"ptrdiff_t int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t intptr_t uintptr_t std "
      L"string wstring vector map set list deque unordered_map unordered_set pair tuple unique_ptr shared_ptr "
      L"weak_ptr function thread mutex BOOL BYTE WORD DWORD UINT LONG HANDLE HWND HDC HRESULT LPCWSTR LPCSTR "
      L"LPWSTR LPSTR LPCTSTR WPARAM LPARAM LRESULT CString",
      L"//", NULL, L"/*", L"*/",
      kFlagsCFamily | kFlagSingleQuoteChar | kFlagHashDirective | kFlagCppRawString },

    { code_lang_csharp, "csharp", "cs,c#", L"C#",
      L"abstract as async await base break case catch checked class const continue default delegate do else "
      L"enum event explicit extern false finally fixed for foreach get goto if implicit in init interface "
      L"internal is lock nameof namespace new null operator out override params partial private protected "
      L"public readonly record ref return sealed set sizeof stackalloc static struct switch this throw true "
      L"try typeof unchecked unsafe using value var virtual volatile when where while yield",
      L"bool byte char decimal double dynamic float int long object sbyte short string uint ulong ushort void "
      L"String Int32 Int64 Task List Dictionary IEnumerable Console Exception",
      L"//", NULL, L"/*", L"*/",
      kFlagsCFamily | kFlagSingleQuoteChar | kFlagHashDirective | kFlagVerbatimString },

    { code_lang_java, "java", "", L"Java",
      L"abstract assert break case catch class const continue default do else enum extends final finally for "
      L"goto if implements import instanceof interface native new package permits private protected public "
      L"record return sealed static strictfp super switch synchronized this throw throws transient try var "
      L"volatile while yield true false null",
      L"boolean byte char double float int long short void String Integer Long Double Boolean Object List Map "
      L"Set ArrayList HashMap HashSet System Exception",
      L"//", NULL, L"/*", L"*/",
      kFlagsCFamily | kFlagSingleQuoteChar | kFlagAtAnnotation },

    { code_lang_python, "python", "py,python3,py3", L"Python",
      L"and as assert async await break case class continue def del elif else except finally for from global "
      L"if import in is lambda match nonlocal not or pass raise return try while with yield True False None "
      L"self cls",
      L"int float str bool list dict set tuple bytes bytearray object type complex frozenset Exception",
      L"#", NULL, NULL, NULL,
      kFlagDoubleQuote | kFlagSingleQuoteStr | kFlagTripleQuote | kFlagAtAnnotation | kFlagFunctionCall
          | kFlagPythonPrefix | kFlagTypeIntro },

    { code_lang_javascript, "javascript", "js,jsx,ts,tsx,typescript,node,mjs", L"JavaScript",
      L"abstract as async await break case catch class const constructor continue debugger declare default "
      L"delete do else enum export extends false finally for from function get if implements import in "
      L"instanceof interface keyof let module namespace new null of private protected public readonly return "
      L"set static super switch this throw true try type typeof undefined var void while with yield",
      L"any boolean never number object string symbol unknown bigint Array Object String Number Boolean "
      L"Promise Map Set Date RegExp Error JSON Math console window document",
      L"//", NULL, L"/*", L"*/",
      kFlagsCFamily | kFlagSingleQuoteStr | kFlagBacktickString | kFlagAtAnnotation },

    { code_lang_go, "go", "golang", L"Go",
      L"break case chan const continue default defer else fallthrough for func go goto if import interface map "
      L"package range return select struct switch type var true false nil iota",
      L"any bool byte complex64 complex128 error float32 float64 int int8 int16 int32 int64 rune string uint "
      L"uint8 uint16 uint32 uint64 uintptr",
      L"//", NULL, L"/*", L"*/",
      kFlagsCFamily | kFlagSingleQuoteChar | kFlagBacktickString },

    { code_lang_rust, "rust", "rs", L"Rust",
      L"as async await break const continue crate dyn else enum extern false fn for if impl in let loop match "
      L"mod move mut pub ref return self Self static struct super trait true type unsafe use where while",
      L"i8 i16 i32 i64 i128 isize u8 u16 u32 u64 u128 usize f32 f64 bool char str String Vec Option Result "
      L"Box Rc Arc HashMap HashSet Some None Ok Err",
      L"//", NULL, L"/*", L"*/",
      kFlagsCFamily | kFlagSingleQuoteChar | kFlagRustMacro },

    { code_lang_sql, "sql", "mysql,sqlite,postgresql,pgsql,tsql,plsql", L"SQL",
      L"add all alter and as asc auto_increment begin between by case check column commit constraint create "
      L"database default delete desc describe distinct drop else end engine exists foreign from full function "
      L"grant group having if in index inner insert into is join key left like limit not null offset on or "
      L"order outer primary procedure references replace return returns revoke right rollback select set show "
      L"table then to transaction trigger truncate union unique update use values view when where while with",
      L"bigint binary bit blob bool boolean char date datetime decimal double enum float int integer json "
      L"longtext mediumint mediumtext numeric real smallint text time timestamp tinyint tinytext varbinary "
      L"varchar count sum avg min max now",
      L"--", NULL, L"/*", L"*/",
      kFlagDoubleQuote | kFlagSingleQuoteStr | kFlagBacktickString | kFlagCaseInsensitive | kFlagQuoteDoubling
          | kFlagFunctionCall },

    { code_lang_shell, "shell", "sh,bash,zsh,ksh,console,shellscript", L"Shell",
      L"if then else elif fi case esac for while until do done in function return break continue exit export "
      L"local readonly declare unset shift source alias eval exec trap set echo printf read cd test true false",
      NULL,
      L"#", NULL, NULL, NULL,
      kFlagDoubleQuote | kFlagSingleQuoteStr | kFlagBacktickString | kFlagDollarVariable | kFlagHashNeedsSpace },

    { code_lang_json, "json", "json5,jsonc", L"JSON",
      L"true false null",
      NULL,
      NULL, NULL, NULL, NULL,
      kFlagDoubleQuote | kFlagJsonKeys },

    { code_lang_xml, "xml", "html,htm,xhtml,svg,xaml,vue,plist", L"XML/HTML",
      NULL, NULL, NULL, NULL, NULL, NULL,
      kFlagMarkup },

    { code_lang_lua, "lua", "", L"Lua",
      L"and break do else elseif end false for function goto if in local nil not or repeat return then true "
      L"until while self",
      L"string table math io os coroutine",
      L"--", NULL, NULL, NULL,
      kFlagDoubleQuote | kFlagSingleQuoteStr | kFlagLuaLongBracket | kFlagFunctionCall },

    { code_lang_php, "php", "php3,php4,php5,php7,php8", L"PHP",
      L"abstract and array as break callable case catch class clone const continue declare default do echo "
      L"else elseif empty enddeclare endfor endforeach endif endswitch endwhile enum extends false final "
      L"finally fn for foreach function global goto if implements include include_once instanceof insteadof "
      L"interface isset list match namespace new null or print private protected public readonly require "
      L"require_once return static switch throw trait true try unset use var while xor yield",
      L"bool int float string void mixed object iterable self parent",
      L"//", L"#", L"/*", L"*/",
      kFlagDoubleQuote | kFlagSingleQuoteStr | kFlagDollarVariable | kFlagPhpTags | kFlagFunctionCall
          | kFlagTypeIntro },
};

//会让下一个标识符按类型名着色的关键字（kFlagTypeIntro 的语言适用，按小写比较）
const wchar_t* const kTypeIntroWords[] =
{
    L"class", L"struct", L"interface", L"enum", L"trait", L"union", L"record", L"impl", L"extends",
    L"implements", L"new", L"type",
};

//字符字面量从开引号算起最多多长（字符）：超过这个长度还没遇到闭引号，就不当字符字面量
const size_t kMaxCharLiteralLen = 12;

//C++ 原始字符串分隔符的最大长度（字符），标准规定为 16
const size_t kMaxRawDelimiterLen = 16;

//检测语言时最多看多少个字符：特征几乎都出现在开头，限定样本长度可以让超长代码的识别仍然很快
const size_t kDetectSampleChars = 20000;

//一个特征最多计几次：同一特征反复出现说明不了更多问题，限次防止一种特征压倒其它特征
const int kMarkerMaxHits = 3;

//识别出语言所需的最低分：低于它时按纯文本处理
const int kDetectMinScore = 6;

//整体是一个 JSON 对象（以 { 或 [ 开头结尾、含 "键": 写法）时 JSON 得的分
const int kJsonWholeScore = 20;

//整体以 < 开头、以 > 结尾时 XML / HTML 得的分
const int kMarkupWholeScore = 12;

//LooksLikeCode：中文字符占非空白字符的百分比超过它时视为正文
const int kProseCjkPercent = 30;

//LooksLikeCode：识别不出语言时，至少要有几行非空行才按「形态」判断
const int kShapeMinLines = 3;

//LooksLikeCode：缩进开头或以 ; { } 结尾的行至少要占非空行的百分比
const int kShapeLinePercent = 30;

//LooksLikeCode：括号、运算符等符号至少要占非空白字符的百分比
const int kShapeSymbolPercent = 5;

/**
 *  一种语言的关键字与类型名集合，由 kLangDefs 里的空格分隔串拆出来。
 */
struct WordSets
{
    std::set<std::wstring> m_keywords;   //关键字
    std::set<std::wstring> m_types;      //类型名
};

//把空格分隔的单词串拆进集合；list 为 NULL 时什么都不做
void SplitWords(const wchar_t* list, std::set<std::wstring>& out)
{
    if (list == NULL)
        return;
    const wchar_t* p = list;
    while (*p != 0)
    {
        while (*p == L' ')
            ++p;
        const wchar_t* begin = p;
        while (*p != 0 && *p != L' ')
            ++p;
        if (p > begin)
            out.insert(std::wstring(begin, p - begin));
    }
}

//按 kLangDefs 建全部语言的单词集合，下标与 CodeLanguage 一致
std::vector<WordSets> BuildWordSets()
{
    std::vector<WordSets> sets(code_lang_count);
    for (int i = 0; i < code_lang_count; ++i)
    {
        SplitWords(kLangDefs[i].m_keywords, sets[i].m_keywords);
        SplitWords(kLangDefs[i].m_types, sets[i].m_types);
    }
    return sets;
}

//取一种语言的单词集合。第一次调用时建好全部语言的集合，C++11 起局部静态变量的初始化是线程安全的
const WordSets& WordSetsOf(int lang)
{
    static const std::vector<WordSets> s_sets = BuildWordSets();
    return s_sets[lang];
}

//语言越界时按纯文本处理
int ClampLanguage(int lang)
{
    if (lang < 0 || lang >= code_lang_count)
        return code_lang_plain;
    return lang;
}

bool IsAsciiDigit(wchar_t c)
{
    return c >= L'0' && c <= L'9';
}

bool IsAsciiLetter(wchar_t c)
{
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
}

bool IsAsciiAlnum(wchar_t c)
{
    return IsAsciiLetter(c) || IsAsciiDigit(c);
}

//空白：空格、制表符、换行，以及全角空格、不换行空格、字节序标记
bool IsSpaceChar(wchar_t c)
{
    return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' || c == 0x3000 || c == 0x00A0 || c == 0xFEFF;
}

//标识符首字符：ASCII 字母、下划线；非 ASCII 字符（如中文标识符）也算，空白除外
bool IsIdentStart(wchar_t c)
{
    if (IsAsciiLetter(c) || c == L'_')
        return true;
    return c >= 0x80 && !IsSpaceChar(c);
}

//标识符后续字符：首字符再加上数字
bool IsIdentChar(wchar_t c)
{
    return IsIdentStart(c) || IsAsciiDigit(c);
}

//ASCII 范围内转小写，其余字符原样保留
std::wstring ToLowerAscii(const std::wstring& s)
{
    std::wstring out(s);
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i] >= L'A' && out[i] <= L'Z')
            out[i] = static_cast<wchar_t>(out[i] - L'A' + L'a');
    }
    return out;
}

std::string ToLowerAscii(const std::string& s)
{
    std::string out(s);
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i] >= 'A' && out[i] <= 'Z')
            out[i] = static_cast<char>(out[i] - 'A' + 'a');
    }
    return out;
}

//去掉首尾空白
std::wstring TrimSpaces(const std::wstring& s)
{
    size_t b = 0;
    size_t e = s.size();
    while (b < e && IsSpaceChar(s[b]))
        ++b;
    while (e > b && IsSpaceChar(s[e - 1]))
        --e;
    return s.substr(b, e - b);
}

/**
 *  程序语言（非标记语言）的扫描器：从左到右扫一遍，把需要着色的区间依次追加到输出。
 *  一个对象只扫一次，用完即弃。
 */
class CodeScanner
{
public:
    /**
     *  @param code  代码原文，须活到 Run 返回
     *  @param def   语言规则
     *  @param words 该语言的单词集合
     *  @param out   [出参] 着色区间追加到这里
     */
    CodeScanner(const std::wstring& code, const LangDef& def, const WordSets& words, std::vector<CodeToken>& out)
        : m_s(code)
        , m_n(code.size())
        , m_def(def)
        , m_words(words)
        , m_out(out)
        , m_lineStart(true)
        , m_pendingTypeIntro(false)
    {
    }

    //扫描全文
    void Run();

private:
    //是否设置了某个语言特性
    bool Has(unsigned flag) const
    {
        return (m_def.m_flags & flag) != 0;
    }

    //pos 处是否以 s 开头（区分大小写）；s 为 NULL 时返回 false
    bool At(size_t pos, const wchar_t* s) const;

    //pos 所在行的行尾下标（指向 \n 或 \r，或文本末尾）
    size_t LineEnd(size_t pos) const;

    //记一个着色区间；空区间忽略
    void Emit(size_t begin, size_t end, int kind);

    //pos 处是注释时返回 true，end 为注释结束下标（不含行尾换行）
    bool TryComment(size_t pos, size_t& end) const;

    //pos 处是字符串时返回 true，end 为字符串结束下标（闭引号之后）
    bool TryString(size_t pos, size_t& end) const;

    //从开引号 pos 扫到闭引号之后。multiLine 为 false 时遇到换行即结束（未闭合的字符串只着色到行尾）；
    //escapes 为 true 时反斜杠转义下一个字符
    size_t ScanQuoted(size_t pos, wchar_t quote, bool multiLine, bool escapes) const;

    //pos 处是字符字面量（'a'、'\n'）时返回 true；Rust 的生命周期 'a 不算
    bool TryCharLiteral(size_t pos, size_t& end) const;

    //pos 处是 Lua 长括号 [[ ]] 或 [==[ ]==] 时返回 true，end 为闭括号之后（没有闭合时为文本末尾）
    bool TryLuaLongBracket(size_t pos, size_t& end) const;

    //pos 处（R 前缀之后的引号）是 C++ 原始字符串时返回 true
    bool TryCppRawString(size_t pos, size_t& end) const;

    //pos 处是数字字面量的开头
    bool IsNumberStart(size_t pos) const;

    //从 pos 扫到数字字面量之后
    size_t ScanNumber(size_t pos) const;

    //处理从 pos 开始的标识符（关键字、类型、函数名、Rust 宏、带前缀的字符串），返回处理完之后的位置
    size_t HandleIdentifier(size_t pos);

    //处理行首 # 开头的预处理指令，返回处理完之后的位置
    size_t HandleDirective(size_t pos);

    //pos 处是 Rust 属性 #[...] / #![...] 时返回 true
    bool TryRustAttribute(size_t pos, size_t& end) const;

    //pos 处是 $ 变量时返回 true
    bool TryDollarVariable(size_t pos, size_t& end) const;

    //pos 处是 @ 注解 / 装饰器时返回 true
    bool TryAnnotation(size_t pos, size_t& end) const;

    //pos 处是 PHP 起止标记时返回 true
    bool TryPhpTag(size_t pos, size_t& end) const;

    //end 之后（跳过空白）紧跟冒号：JSON 里这样的字符串是键名
    bool IsJsonKeyAt(size_t end) const;

private:
    const std::wstring&     m_s;                  //代码原文（借用）
    const size_t            m_n;                  //原文长度
    const LangDef&          m_def;                //语言规则
    const WordSets&         m_words;              //该语言的单词集合
    std::vector<CodeToken>& m_out;                //输出（借用）
    bool                    m_lineStart;          //当前位置之前、本行内只有空白（判断 # 预处理指令用）
    bool                    m_pendingTypeIntro;   //上一个词是 class / struct 等，下一个标识符按类型名着色
};

bool CodeScanner::At(size_t pos, const wchar_t* s) const
{
    if (s == NULL)
        return false;
    size_t i = 0;
    while (s[i] != 0)
    {
        if (pos + i >= m_n || m_s[pos + i] != s[i])
            return false;
        ++i;
    }
    return i > 0;
}

size_t CodeScanner::LineEnd(size_t pos) const
{
    size_t i = pos;
    while (i < m_n && m_s[i] != L'\n' && m_s[i] != L'\r')
        ++i;
    return i;
}

void CodeScanner::Emit(size_t begin, size_t end, int kind)
{
    if (end <= begin)
        return;
    m_out.push_back(CodeToken(static_cast<int>(begin), static_cast<int>(end - begin), kind));
}

bool CodeScanner::TryComment(size_t pos, size_t& end) const
{
    //Lua 的块注释 --[[ ... ]]：先于行注释 -- 判断
    if (Has(kFlagLuaLongBracket) && At(pos, L"--") && pos + 2 < m_n && m_s[pos + 2] == L'[')
    {
        size_t bracketEnd = 0;
        if (TryLuaLongBracket(pos + 2, bracketEnd))
        {
            end = bracketEnd;
            return true;
        }
    }
    //块注释：没有闭合时着色到文本末尾
    if (m_def.m_blockOpen != NULL && At(pos, m_def.m_blockOpen))
    {
        const size_t openLen = wcslen(m_def.m_blockOpen);
        const size_t close = m_s.find(m_def.m_blockClose, pos + openLen);
        end = (close == std::wstring::npos) ? m_n : close + wcslen(m_def.m_blockClose);
        return true;
    }
    const wchar_t* prefixes[2] = { m_def.m_lineComment, m_def.m_lineComment2 };
    for (int k = 0; k < 2; ++k)
    {
        if (!At(pos, prefixes[k]))
            continue;
        //Shell 的 # 只有在行首或空白、分号之后才是注释（a#b、$# 不是）
        if (Has(kFlagHashNeedsSpace) && prefixes[k][0] == L'#' && pos > 0)
        {
            const wchar_t prev = m_s[pos - 1];
            if (!IsSpaceChar(prev) && prev != L';')
                continue;
        }
        end = LineEnd(pos);
        return true;
    }
    return false;
}

size_t CodeScanner::ScanQuoted(size_t pos, wchar_t quote, bool multiLine, bool escapes) const
{
    size_t i = pos + 1;
    while (i < m_n)
    {
        const wchar_t c = m_s[i];
        if (escapes && c == L'\\' && i + 1 < m_n)
        {
            i += 2;
            continue;
        }
        if (c == quote)
        {
            if (Has(kFlagQuoteDoubling) && i + 1 < m_n && m_s[i + 1] == quote)
            {
                i += 2;
                continue;
            }
            return i + 1;
        }
        if (!multiLine && (c == L'\n' || c == L'\r'))
            return i;
        ++i;
    }
    return m_n;
}

bool CodeScanner::TryCharLiteral(size_t pos, size_t& end) const
{
    //转义序列：'\n'、'\''、'\x41'、'\u{1F600}'，在 kMaxCharLiteralLen 之内找闭引号
    if (pos + 1 < m_n && m_s[pos + 1] == L'\\')
    {
        for (size_t i = pos + 3; i < m_n && i <= pos + kMaxCharLiteralLen; ++i)
        {
            if (m_s[i] == L'\n' || m_s[i] == L'\r')
                return false;
            if (m_s[i] == L'\'')
            {
                end = i + 1;
                return true;
            }
        }
        return false;
    }
    //恰好一个字符：'a'。'a, 'b 这类 Rust 生命周期第三个字符不是引号，不当字面量
    if (pos + 2 < m_n && m_s[pos + 1] != L'\'' && m_s[pos + 1] != L'\n' && m_s[pos + 1] != L'\r'
        && m_s[pos + 2] == L'\'')
    {
        end = pos + 3;
        return true;
    }
    //一个用代理对表示的字符
    if (pos + 3 < m_n && m_s[pos + 1] >= 0xD800 && m_s[pos + 1] <= 0xDBFF && m_s[pos + 3] == L'\'')
    {
        end = pos + 4;
        return true;
    }
    return false;
}

bool CodeScanner::TryLuaLongBracket(size_t pos, size_t& end) const
{
    if (pos >= m_n || m_s[pos] != L'[')
        return false;
    size_t i = pos + 1;
    size_t level = 0;
    while (i < m_n && m_s[i] == L'=')
    {
        ++level;
        ++i;
    }
    if (i >= m_n || m_s[i] != L'[')
        return false;
    const std::wstring close = L"]" + std::wstring(level, L'=') + L"]";
    const size_t found = m_s.find(close, i + 1);
    end = (found == std::wstring::npos) ? m_n : found + close.size();
    return true;
}

bool CodeScanner::TryCppRawString(size_t pos, size_t& end) const
{
    //R"分隔符( ... )分隔符"：分隔符里不能有空白、括号与反斜杠
    const size_t open = m_s.find(L'(', pos + 1);
    if (open == std::wstring::npos || open - (pos + 1) > kMaxRawDelimiterLen)
        return false;
    for (size_t i = pos + 1; i < open; ++i)
    {
        const wchar_t c = m_s[i];
        if (IsSpaceChar(c) || c == L')' || c == L'\\' || c == L'"')
            return false;
    }
    const std::wstring close = L")" + m_s.substr(pos + 1, open - (pos + 1)) + L"\"";
    const size_t found = m_s.find(close, open + 1);
    end = (found == std::wstring::npos) ? m_n : found + close.size();
    return true;
}

bool CodeScanner::TryString(size_t pos, size_t& end) const
{
    const wchar_t c = m_s[pos];
    //Python 三引号：可以跨行
    if (Has(kFlagTripleQuote) && (At(pos, L"\"\"\"") || At(pos, L"'''")))
    {
        const std::wstring close(3, c);
        size_t i = pos + 3;
        while (i < m_n)
        {
            if (m_s[i] == L'\\' && i + 1 < m_n)
            {
                i += 2;
                continue;
            }
            if (m_s.compare(i, 3, close) == 0)
            {
                end = i + 3;
                return true;
            }
            ++i;
        }
        end = m_n;
        return true;
    }
    //C# 逐字字符串 @"..."：两个引号表示一个引号，可以跨行，不处理反斜杠
    if (Has(kFlagVerbatimString) && c == L'@' && pos + 1 < m_n && m_s[pos + 1] == L'"')
    {
        size_t i = pos + 2;
        while (i < m_n)
        {
            if (m_s[i] == L'"')
            {
                if (i + 1 < m_n && m_s[i + 1] == L'"')
                {
                    i += 2;
                    continue;
                }
                end = i + 1;
                return true;
            }
            ++i;
        }
        end = m_n;
        return true;
    }
    //Lua 长字符串 [[ ... ]]
    if (Has(kFlagLuaLongBracket) && c == L'[')
        return TryLuaLongBracket(pos, end);
    if (c == L'"' && Has(kFlagDoubleQuote))
    {
        end = ScanQuoted(pos, L'"', false, true);
        return true;
    }
    if (c == L'\'')
    {
        if (Has(kFlagSingleQuoteChar))
            return TryCharLiteral(pos, end);
        if (Has(kFlagSingleQuoteStr))
        {
            //Shell 的单引号字符串里反斜杠没有转义作用
            end = ScanQuoted(pos, L'\'', false, m_def.m_lang != code_lang_shell);
            return true;
        }
    }
    if (c == L'`' && Has(kFlagBacktickString))
    {
        //Go 的原始字符串与 SQL 的反引号标识符里反斜杠没有转义作用
        const bool escapes = (m_def.m_lang != code_lang_go && m_def.m_lang != code_lang_sql);
        end = ScanQuoted(pos, L'`', m_def.m_lang != code_lang_sql, escapes);
        return true;
    }
    return false;
}

bool CodeScanner::IsNumberStart(size_t pos) const
{
    //紧跟在标识符后面的数字是标识符的一部分（如 utf8、x2）
    if (pos > 0 && IsIdentChar(m_s[pos - 1]))
        return false;
    const wchar_t c = m_s[pos];
    if (IsAsciiDigit(c))
        return true;
    return c == L'.' && pos + 1 < m_n && IsAsciiDigit(m_s[pos + 1]);
}

size_t CodeScanner::ScanNumber(size_t pos) const
{
    const bool hex = (m_s[pos] == L'0' && pos + 1 < m_n && (m_s[pos + 1] == L'x' || m_s[pos + 1] == L'X'));
    size_t i = pos;
    while (i < m_n)
    {
        const wchar_t c = m_s[i];
        if (IsAsciiAlnum(c) || c == L'_')
        {
            ++i;
            continue;
        }
        if (c == L'.')
        {
            //Rust / Lua 的 .. 是区间或连接运算符，不属于数字
            if (i + 1 < m_n && m_s[i + 1] == L'.')
                break;
            ++i;
            continue;
        }
        //科学计数法的指数符号：1e-5、2.5E+3
        if ((c == L'+' || c == L'-') && !hex && i > pos && (m_s[i - 1] == L'e' || m_s[i - 1] == L'E'))
        {
            ++i;
            continue;
        }
        break;
    }
    return i;
}

size_t CodeScanner::HandleIdentifier(size_t pos)
{
    size_t e = pos + 1;
    while (e < m_n && IsIdentChar(m_s[e]))
        ++e;
    const std::wstring word = m_s.substr(pos, e - pos);

    //Python 的字符串前缀（r"..."、f'...'、rb"""..."""）并入字符串
    if (Has(kFlagPythonPrefix) && word.size() <= 2 && e < m_n && (m_s[e] == L'"' || m_s[e] == L'\'')
        && word.find_first_not_of(L"rRbBfFuU") == std::wstring::npos)
    {
        size_t strEnd = 0;
        if (TryString(e, strEnd))
        {
            Emit(pos, strEnd, code_token_string);
            m_pendingTypeIntro = false;
            return strEnd;
        }
    }
    //C++ 原始字符串 R"(...)"，以及带编码前缀的 u8R、LR、uR、UR
    if (Has(kFlagCppRawString) && e < m_n && m_s[e] == L'"'
        && (word == L"R" || word == L"u8R" || word == L"LR" || word == L"uR" || word == L"UR"))
    {
        size_t strEnd = 0;
        if (TryCppRawString(e, strEnd))
        {
            Emit(pos, strEnd, code_token_string);
            m_pendingTypeIntro = false;
            return strEnd;
        }
    }

    const std::wstring key = Has(kFlagCaseInsensitive) ? ToLowerAscii(word) : word;
    if (m_words.m_keywords.count(key) != 0)
    {
        Emit(pos, e, code_token_keyword);
        bool intro = false;
        if (Has(kFlagTypeIntro))
        {
            const std::wstring lower = ToLowerAscii(word);
            for (size_t k = 0; k < sizeof(kTypeIntroWords) / sizeof(kTypeIntroWords[0]); ++k)
            {
                if (lower == kTypeIntroWords[k])
                {
                    intro = true;
                    break;
                }
            }
        }
        m_pendingTypeIntro = intro;
        return e;
    }
    if (m_words.m_types.count(key) != 0)
    {
        Emit(pos, e, code_token_type);
        m_pendingTypeIntro = false;
        return e;
    }
    if (m_pendingTypeIntro)
    {
        Emit(pos, e, code_token_type);
        m_pendingTypeIntro = false;
        return e;
    }
    //Rust 宏调用：println!(...)、vec![...]；!= 不算
    if (Has(kFlagRustMacro) && e < m_n && m_s[e] == L'!' && (e + 1 >= m_n || m_s[e + 1] != L'='))
    {
        Emit(pos, e + 1, code_token_preprocessor);
        return e + 1;
    }
    //函数名：标识符后（可隔空格）紧跟左括号
    if (Has(kFlagFunctionCall))
    {
        size_t j = e;
        while (j < m_n && (m_s[j] == L' ' || m_s[j] == L'\t'))
            ++j;
        if (j < m_n && m_s[j] == L'(')
            Emit(pos, e, code_token_function);
    }
    return e;
}

size_t CodeScanner::HandleDirective(size_t pos)
{
    //# 与指令名之间允许有空白：# include、#  define
    size_t i = pos + 1;
    while (i < m_n && (m_s[i] == L' ' || m_s[i] == L'\t'))
        ++i;
    const size_t wordBegin = i;
    while (i < m_n && IsIdentChar(m_s[i]))
        ++i;
    Emit(pos, i, code_token_preprocessor);

    //#include <...> 里的头文件名按字符串着色；"..." 形式由后续的字符串扫描处理
    const std::wstring word = m_s.substr(wordBegin, i - wordBegin);
    if (word == L"include" || word == L"include_next" || word == L"import")
    {
        size_t j = i;
        while (j < m_n && (m_s[j] == L' ' || m_s[j] == L'\t'))
            ++j;
        if (j < m_n && m_s[j] == L'<')
        {
            const size_t lineEnd = LineEnd(j);
            const size_t close = m_s.find(L'>', j + 1);
            if (close != std::wstring::npos && close < lineEnd)
            {
                Emit(j, close + 1, code_token_string);
                return close + 1;
            }
        }
    }
    return i;
}

bool CodeScanner::TryRustAttribute(size_t pos, size_t& end) const
{
    size_t i = pos + 1;
    if (i < m_n && m_s[i] == L'!')
        ++i;
    if (i >= m_n || m_s[i] != L'[')
        return false;
    //按方括号配对找属性结尾；跨行的属性只着色到行尾
    int depth = 0;
    for (; i < m_n; ++i)
    {
        const wchar_t c = m_s[i];
        if (c == L'\n' || c == L'\r')
            break;
        if (c == L'[')
        {
            ++depth;
        }
        else if (c == L']')
        {
            --depth;
            if (depth == 0)
            {
                end = i + 1;
                return true;
            }
        }
    }
    end = i;
    return true;
}

bool CodeScanner::TryDollarVariable(size_t pos, size_t& end) const
{
    if (pos + 1 >= m_n)
        return false;
    const wchar_t d = m_s[pos + 1];
    if (m_def.m_lang == code_lang_shell)
    {
        //${name}、${#arr[@]}：到同一行的右花括号为止
        if (d == L'{')
        {
            const size_t close = m_s.find(L'}', pos + 2);
            if (close != std::wstring::npos && close < LineEnd(pos))
            {
                end = close + 1;
                return true;
            }
            return false;
        }
        //特殊参数：$1、$@、$#、$?、$$、$!、$*、$-
        if (IsAsciiDigit(d) || d == L'@' || d == L'#' || d == L'?' || d == L'$' || d == L'!' || d == L'*'
            || d == L'-')
        {
            end = pos + 2;
            return true;
        }
    }
    if (IsAsciiLetter(d) || d == L'_')
    {
        size_t e = pos + 2;
        while (e < m_n && (IsAsciiAlnum(m_s[e]) || m_s[e] == L'_'))
            ++e;
        end = e;
        return true;
    }
    return false;
}

bool CodeScanner::TryAnnotation(size_t pos, size_t& end) const
{
    if (pos + 1 >= m_n || !(IsAsciiLetter(m_s[pos + 1]) || m_s[pos + 1] == L'_'))
        return false;
    //@Override、@app.route：标识符之间允许用点连接
    size_t e = pos + 1;
    while (e < m_n && (IsAsciiAlnum(m_s[e]) || m_s[e] == L'_' || m_s[e] == L'.'))
        ++e;
    end = e;
    return true;
}

bool CodeScanner::TryPhpTag(size_t pos, size_t& end) const
{
    if (At(pos, L"<?php"))
    {
        end = pos + 5;
        return true;
    }
    if (At(pos, L"<?="))
    {
        end = pos + 3;
        return true;
    }
    if (At(pos, L"?>"))
    {
        end = pos + 2;
        return true;
    }
    return false;
}

bool CodeScanner::IsJsonKeyAt(size_t end) const
{
    if (!Has(kFlagJsonKeys))
        return false;
    size_t j = end;
    while (j < m_n && IsSpaceChar(m_s[j]))
        ++j;
    return j < m_n && m_s[j] == L':';
}

void CodeScanner::Run()
{
    size_t i = 0;
    //第一行的 #! 解释器声明（Shell、Python 脚本常见）整行按预处理标记着色
    if (At(0, L"#!") && !Has(kFlagRustMacro))
    {
        i = LineEnd(0);
        Emit(0, i, code_token_preprocessor);
    }
    while (i < m_n)
    {
        const wchar_t c = m_s[i];
        if (c == L'\n' || c == L'\r')
        {
            m_lineStart = true;
            ++i;
            continue;
        }
        if (IsSpaceChar(c))
        {
            ++i;
            continue;
        }

        size_t end = i;
        if (TryComment(i, end))
        {
            Emit(i, end, code_token_comment);
            i = end;
            m_lineStart = false;
            continue;
        }
        if (Has(kFlagPhpTags) && TryPhpTag(i, end))
        {
            Emit(i, end, code_token_preprocessor);
            i = end;
            m_lineStart = false;
            continue;
        }
        if (c == L'#' && Has(kFlagHashDirective) && m_lineStart)
        {
            i = HandleDirective(i);
            m_lineStart = false;
            m_pendingTypeIntro = false;
            continue;
        }
        if (c == L'#' && Has(kFlagRustMacro) && TryRustAttribute(i, end))
        {
            Emit(i, end, code_token_preprocessor);
            i = end;
            m_lineStart = false;
            continue;
        }
        if (TryString(i, end))
        {
            Emit(i, end, IsJsonKeyAt(end) ? code_token_attr : code_token_string);
            i = end;
            m_lineStart = false;
            m_pendingTypeIntro = false;
            continue;
        }
        if (IsNumberStart(i))
        {
            end = ScanNumber(i);
            Emit(i, end, code_token_number);
            i = end;
            m_lineStart = false;
            m_pendingTypeIntro = false;
            continue;
        }
        if (IsIdentStart(c))
        {
            i = HandleIdentifier(i);
            m_lineStart = false;
            continue;
        }
        if (c == L'@' && Has(kFlagAtAnnotation) && TryAnnotation(i, end))
        {
            Emit(i, end, code_token_preprocessor);
            i = end;
            m_lineStart = false;
            continue;
        }
        if (c == L'$' && Has(kFlagDollarVariable) && TryDollarVariable(i, end))
        {
            Emit(i, end, code_token_variable);
            i = end;
            m_lineStart = false;
            continue;
        }
        //其余字符（运算符、标点）不着色；它们打断 class 与类型名之间的关联
        m_pendingTypeIntro = false;
        m_lineStart = false;
        ++i;
    }
}

//XML / HTML 标签名与属性名的首字符
bool IsTagNameStart(wchar_t c)
{
    return IsAsciiLetter(c) || c == L'_' || c == L':';
}

//XML / HTML 标签名与属性名的后续字符
bool IsTagNameChar(wchar_t c)
{
    return IsAsciiAlnum(c) || c == L'_' || c == L':' || c == L'-' || c == L'.';
}

//在 s 里从 from 起找 close，返回其后的位置；找不到时返回文本末尾
size_t FindAfter(const std::wstring& s, const wchar_t* close, size_t from)
{
    const size_t found = s.find(close, from);
    if (found == std::wstring::npos)
        return s.size();
    return found + wcslen(close);
}

//追加一个着色区间；空区间忽略
void EmitToken(std::vector<CodeToken>& out, size_t begin, size_t end, int kind)
{
    if (end > begin)
        out.push_back(CodeToken(static_cast<int>(begin), static_cast<int>(end - begin), kind));
}

//扫描一个开始标签或结束标签里的属性，pos 指向标签名之后；返回标签结束（> 之后）的位置
size_t ScanMarkupAttributes(const std::wstring& s, size_t pos, std::vector<CodeToken>& out)
{
    const size_t n = s.size();
    size_t j = pos;
    while (j < n)
    {
        const wchar_t c = s[j];
        if (c == L'>')
        {
            EmitToken(out, j, j + 1, code_token_tag);
            return j + 1;
        }
        if (c == L'/' && j + 1 < n && s[j + 1] == L'>')
        {
            EmitToken(out, j, j + 2, code_token_tag);
            return j + 2;
        }
        if (c == L'"' || c == L'\'')
        {
            const size_t close = s.find(c, j + 1);
            const size_t end = (close == std::wstring::npos) ? n : close + 1;
            EmitToken(out, j, end, code_token_string);
            j = end;
            continue;
        }
        if (IsTagNameStart(c))
        {
            size_t k = j + 1;
            while (k < n && IsTagNameChar(s[k]))
                ++k;
            EmitToken(out, j, k, code_token_attr);
            j = k;
            continue;
        }
        //标签没有闭合就遇到下一个标签：交给外层从这里继续
        if (c == L'<')
            return j;
        ++j;
    }
    return n;
}

//XML / HTML 的扫描：注释、CDATA、处理指令与 DOCTYPE、标签与属性；标签之间的文字不着色
void ScanMarkup(const std::wstring& s, std::vector<CodeToken>& out)
{
    const size_t n = s.size();
    size_t i = 0;
    while (i < n)
    {
        if (s[i] != L'<')
        {
            ++i;
            continue;
        }
        if (s.compare(i, 4, L"<!--") == 0)
        {
            const size_t end = FindAfter(s, L"-->", i + 4);
            EmitToken(out, i, end, code_token_comment);
            i = end;
            continue;
        }
        if (s.compare(i, 9, L"<![CDATA[") == 0)
        {
            const size_t end = FindAfter(s, L"]]>", i + 9);
            EmitToken(out, i, end, code_token_string);
            i = end;
            continue;
        }
        if (s.compare(i, 2, L"<?") == 0)
        {
            const size_t end = FindAfter(s, L"?>", i + 2);
            EmitToken(out, i, end, code_token_preprocessor);
            i = end;
            continue;
        }
        if (s.compare(i, 2, L"<!") == 0)
        {
            const size_t end = FindAfter(s, L">", i + 2);
            EmitToken(out, i, end, code_token_preprocessor);
            i = end;
            continue;
        }
        size_t j = i + 1;
        if (j < n && s[j] == L'/')
            ++j;
        if (j < n && IsTagNameStart(s[j]))
        {
            while (j < n && IsTagNameChar(s[j]))
                ++j;
            EmitToken(out, i, j, code_token_tag);
            i = ScanMarkupAttributes(s, j, out);
            continue;
        }
        ++i;
    }
}

/**
 *  语言识别的一条特征：样本里每出现一次（最多计 kMarkerMaxHits 次），对应语言加 m_weight 分。
 */
struct DetectMarker
{
    int            m_lang;     //命中时加分的语言，取值见 CodeLanguage
    const wchar_t* m_text;     //特征文字；m_ci 为 true 时写小写
    int            m_weight;   //命中一次加的分
    bool           m_ci;       //是否不区分大小写匹配
};

const DetectMarker kMarkers[] =
{
    { code_lang_cpp, L"#include", 6, false },
    { code_lang_cpp, L"#define", 4, false },
    { code_lang_cpp, L"#pragma", 5, false },
    { code_lang_cpp, L"#ifdef", 5, false },
    { code_lang_cpp, L"#endif", 5, false },
    { code_lang_cpp, L"std::", 5, false },
    { code_lang_cpp, L"nullptr", 5, false },
    { code_lang_cpp, L"template<", 4, false },
    { code_lang_cpp, L"template <", 4, false },
    { code_lang_cpp, L"cout <<", 5, false },
    { code_lang_cpp, L"static_cast<", 6, false },
    { code_lang_cpp, L"public:", 5, false },
    { code_lang_cpp, L"private:", 5, false },
    { code_lang_cpp, L"virtual ", 3, false },
    { code_lang_cpp, L"typedef ", 3, false },
    { code_lang_cpp, L"const char*", 3, false },
    { code_lang_cpp, L"unsigned ", 2, false },
    { code_lang_cpp, L"sizeof(", 2, false },
    { code_lang_cpp, L"printf(", 2, false },
    { code_lang_cpp, L"int main(", 3, false },
    { code_lang_cpp, L"NULL", 2, false },
    { code_lang_cpp, L"->", 1, false },
    { code_lang_cpp, L"::", 1, false },

    { code_lang_csharp, L"using System", 8, false },
    { code_lang_csharp, L"Console.Write", 6, false },
    { code_lang_csharp, L"{ get;", 6, false },
    { code_lang_csharp, L"async Task", 6, false },
    { code_lang_csharp, L"foreach (var", 6, false },
    { code_lang_csharp, L"[HttpGet", 6, false },
    { code_lang_csharp, L"public override ", 2, false },
    { code_lang_csharp, L".ToString()", 2, false },
    { code_lang_csharp, L"namespace ", 1, false },
    { code_lang_csharp, L"public class ", 1, false },
    { code_lang_csharp, L"var ", 1, false },

    { code_lang_java, L"System.out.print", 8, false },
    { code_lang_java, L"public static void main(String", 8, false },
    { code_lang_java, L"import java.", 8, false },
    { code_lang_java, L"@Override", 5, false },
    { code_lang_java, L"new ArrayList", 5, false },
    { code_lang_java, L"String[] args", 5, false },
    { code_lang_java, L"throws ", 4, false },
    { code_lang_java, L"boolean ", 3, false },
    { code_lang_java, L" implements ", 3, false },
    { code_lang_java, L"private final ", 3, false },
    { code_lang_java, L" extends ", 1, false },
    { code_lang_java, L"public class ", 1, false },

    { code_lang_python, L"#!/usr/bin/env python", 14, false },
    { code_lang_python, L"#!/usr/bin/python", 14, false },
    { code_lang_python, L"if __name__", 8, false },
    { code_lang_python, L"__init__", 6, false },
    { code_lang_python, L"elif ", 6, false },
    { code_lang_python, L" in range(", 6, false },
    { code_lang_python, L"def ", 4, false },
    { code_lang_python, L"else:", 4, false },
    { code_lang_python, L"try:", 4, false },
    { code_lang_python, L"except ", 4, false },
    { code_lang_python, L"self.", 3, false },
    { code_lang_python, L"):\n", 3, false },
    { code_lang_python, L"):\r", 3, false },
    { code_lang_python, L"\"\"\"", 3, false },
    { code_lang_python, L"None", 2, false },
    { code_lang_python, L"lambda ", 2, false },
    { code_lang_python, L"import ", 1, false },
    { code_lang_python, L"from ", 1, false },
    { code_lang_python, L"print(", 1, false },
    { code_lang_python, L"True", 1, false },
    { code_lang_python, L"False", 1, false },

    { code_lang_javascript, L"module.exports", 8, false },
    { code_lang_javascript, L"import React", 8, false },
    { code_lang_javascript, L"console.log", 6, false },
    { code_lang_javascript, L"document.", 5, false },
    { code_lang_javascript, L"require(", 5, false },
    { code_lang_javascript, L"===", 5, false },
    { code_lang_javascript, L"!==", 5, false },
    { code_lang_javascript, L"undefined", 4, false },
    { code_lang_javascript, L"window.", 4, false },
    { code_lang_javascript, L"function(", 4, false },
    { code_lang_javascript, L"});", 3, false },
    { code_lang_javascript, L"function ", 2, false },
    { code_lang_javascript, L"const ", 2, false },
    { code_lang_javascript, L"let ", 2, false },
    { code_lang_javascript, L"=> ", 2, false },
    { code_lang_javascript, L"export ", 2, false },
    { code_lang_javascript, L": string", 2, false },
    { code_lang_javascript, L"async ", 1, false },
    { code_lang_javascript, L"await ", 1, false },

    { code_lang_go, L"err != nil", 8, false },
    { code_lang_go, L"fmt.", 6, false },
    { code_lang_go, L"import (", 6, false },
    { code_lang_go, L"go func", 6, false },
    { code_lang_go, L"func ", 5, false },
    { code_lang_go, L":= ", 5, false },
    { code_lang_go, L"defer ", 5, false },
    { code_lang_go, L"interface{}", 5, false },
    { code_lang_go, L"package ", 3, false },
    { code_lang_go, L"chan ", 3, false },
    { code_lang_go, L" struct {", 2, false },

    { code_lang_rust, L"let mut ", 8, false },
    { code_lang_rust, L"println!", 8, false },
    { code_lang_rust, L"use std::", 8, false },
    { code_lang_rust, L"#[derive", 8, false },
    { code_lang_rust, L"pub fn ", 6, false },
    { code_lang_rust, L"&str", 5, false },
    { code_lang_rust, L"&mut ", 5, false },
    { code_lang_rust, L".unwrap()", 5, false },
    { code_lang_rust, L"fn ", 4, false },
    { code_lang_rust, L"impl ", 4, false },
    { code_lang_rust, L"Vec<", 3, false },
    { code_lang_rust, L"Some(", 2, false },
    { code_lang_rust, L"Result<", 2, false },
    { code_lang_rust, L"mod ", 2, false },

    { code_lang_sql, L"insert into ", 8, true },
    { code_lang_sql, L"create table ", 8, true },
    { code_lang_sql, L"delete from ", 8, true },
    { code_lang_sql, L"alter table ", 8, true },
    { code_lang_sql, L"primary key", 6, true },
    { code_lang_sql, L"varchar(", 6, true },
    { code_lang_sql, L"group by ", 5, true },
    { code_lang_sql, L"order by ", 5, true },
    { code_lang_sql, L"select ", 4, true },
    { code_lang_sql, L"not null", 4, true },
    { code_lang_sql, L"values (", 4, true },
    { code_lang_sql, L"values(", 4, true },
    { code_lang_sql, L" where ", 3, true },
    { code_lang_sql, L" join ", 3, true },
    { code_lang_sql, L" from ", 2, true },

    { code_lang_shell, L"#!/bin/", 10, false },
    { code_lang_shell, L"#!/usr/bin/env bash", 10, false },
    { code_lang_shell, L"#!/usr/bin/env sh", 10, false },
    { code_lang_shell, L"esac", 8, false },
    { code_lang_shell, L"\nfi", 6, false },
    { code_lang_shell, L"; then", 6, false },
    { code_lang_shell, L"; do", 6, false },
    { code_lang_shell, L"apt-get ", 6, false },
    { code_lang_shell, L"\ndone", 5, false },
    { code_lang_shell, L"sudo ", 5, false },
    { code_lang_shell, L"chmod ", 5, false },
    { code_lang_shell, L" | grep ", 5, false },
    { code_lang_shell, L"rm -", 5, false },
    { code_lang_shell, L"mkdir ", 4, false },
    { code_lang_shell, L"$1", 3, false },
    { code_lang_shell, L"git ", 3, false },
    { code_lang_shell, L"echo ", 2, false },
    { code_lang_shell, L"$(", 2, false },
    { code_lang_shell, L"${", 2, false },
    { code_lang_shell, L"export ", 2, false },
    { code_lang_shell, L"cd ", 1, false },

    { code_lang_json, L"\": ", 2, false },
    { code_lang_json, L"\":", 1, false },

    { code_lang_xml, L"<!doctype html", 12, true },
    { code_lang_xml, L"<?xml", 10, false },
    { code_lang_xml, L"<html", 8, false },
    { code_lang_xml, L"<div", 4, false },
    { code_lang_xml, L"<span", 3, false },
    { code_lang_xml, L"</", 2, false },
    { code_lang_xml, L"/>", 2, false },

    { code_lang_lua, L"ipairs(", 8, false },
    { code_lang_lua, L"table.insert", 8, false },
    { code_lang_lua, L"--[[", 8, false },
    { code_lang_lua, L"~=", 6, false },
    { code_lang_lua, L"self:", 6, false },
    { code_lang_lua, L"pairs(", 5, false },
    { code_lang_lua, L"local ", 4, false },
    { code_lang_lua, L"\nend", 3, false },
    { code_lang_lua, L"elseif ", 3, false },
    { code_lang_lua, L"nil", 3, false },
    { code_lang_lua, L" then", 2, false },
    { code_lang_lua, L"require(\"", 2, false },
    { code_lang_lua, L"function(", 1, false },

    { code_lang_php, L"<?php", 14, false },
    { code_lang_php, L"$this->", 10, false },
    { code_lang_php, L"$_GET", 8, false },
    { code_lang_php, L"$_POST", 8, false },
    { code_lang_php, L"public function ", 6, false },
    { code_lang_php, L"namespace App", 4, false },
    { code_lang_php, L"array(", 3, false },
    { code_lang_php, L"echo ", 1, false },
    { code_lang_php, L"->", 1, false },
};

//text 里 pattern 出现的次数，最多数到 limit
int CountOccurrences(const std::wstring& text, const wchar_t* pattern, int limit)
{
    int hits = 0;
    size_t from = 0;
    const size_t len = wcslen(pattern);
    while (hits < limit)
    {
        const size_t found = text.find(pattern, from);
        if (found == std::wstring::npos)
            break;
        ++hits;
        from = found + len;
    }
    return hits;
}

//整体像一个 JSON 对象或数组：首尾是成对的括号，并且有 "键": 的写法
bool LooksLikeJsonDocument(const std::wstring& trimmed)
{
    if (trimmed.size() < 2)
        return false;
    const wchar_t first = trimmed[0];
    const wchar_t last = trimmed[trimmed.size() - 1];
    const bool object = (first == L'{' && last == L'}');
    const bool array = (first == L'[' && last == L']');
    if (!object && !array)
        return false;
    return trimmed.find(L"\":") != std::wstring::npos || trimmed.find(L"\" :") != std::wstring::npos;
}

//中日韩文字（汉字、假名、谚文、全角标点），用于判断一段文字是不是正文
bool IsCjkChar(wchar_t c)
{
    return (c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xAC00 && c <= 0xD7A3) || (c >= 0xF900 && c <= 0xFAFF)
           || (c >= 0xFF00 && c <= 0xFFEF);
}

//括号、运算符等在代码里常见、在正文里少见的符号
bool IsCodeSymbol(wchar_t c)
{
    return wcschr(L"{}()[];=<>+-*/&|!%^~:#$\\", c) != NULL && c != 0;
}

//一行（已去掉首尾空白）是否以注释前缀开头；统计正文比例时整行跳过
bool IsCommentLine(const std::wstring& trimmedLine)
{
    const wchar_t* const prefixes[] = { L"//", L"#", L"/*", L"*", L"--", L"<!--" };
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i)
    {
        if (trimmedLine.compare(0, wcslen(prefixes[i]), prefixes[i]) == 0)
            return true;
    }
    return false;
}

} // anonymous namespace

const char* CodeLanguageId(int lang)
{
    return kLangDefs[ClampLanguage(lang)].m_id;
}

int CodeLanguageFromId(const std::string& id)
{
    const std::string key = ToLowerAscii(id);
    if (key.empty())
        return code_lang_plain;
    for (int i = 0; i < code_lang_count; ++i)
    {
        if (key == kLangDefs[i].m_id)
            return i;
        //别名串逗号分隔，逐个比较
        const std::string aliases = kLangDefs[i].m_aliases;
        size_t begin = 0;
        while (begin <= aliases.size())
        {
            size_t comma = aliases.find(',', begin);
            if (comma == std::string::npos)
                comma = aliases.size();
            if (comma > begin && aliases.compare(begin, comma - begin, key) == 0)
                return i;
            begin = comma + 1;
        }
    }
    return code_lang_plain;
}

const wchar_t* CodeLanguageDisplayName(int lang)
{
    return kLangDefs[ClampLanguage(lang)].m_displayName;
}

const wchar_t* CodeLanguageFileExtension(int lang)
{
    //下标与 CodeLanguage 一致
    static const wchar_t* const kExtensions[code_lang_count] =
    {
        L"txt", L"cpp", L"cs", L"java", L"py", L"js", L"go", L"rs", L"sql", L"sh", L"json", L"xml", L"lua", L"php",
    };
    return kExtensions[ClampLanguage(lang)];
}

void TokenizeCode(const std::wstring& code, int lang, std::vector<CodeToken>& out)
{
    out.clear();
    lang = ClampLanguage(lang);
    if (lang == code_lang_plain || code.empty())
        return;
    const LangDef& def = kLangDefs[lang];
    if ((def.m_flags & kFlagMarkup) != 0)
    {
        ScanMarkup(code, out);
        return;
    }
    CodeScanner scanner(code, def, WordSetsOf(lang), out);
    scanner.Run();
}

int DetectCodeLanguage(const std::wstring& code)
{
    const std::wstring sample = code.substr(0, kDetectSampleChars);
    const std::wstring trimmed = TrimSpaces(sample);
    if (trimmed.empty())
        return code_lang_plain;

    int scores[code_lang_count] = { 0 };
    if (LooksLikeJsonDocument(trimmed))
        scores[code_lang_json] += kJsonWholeScore;
    //以 < 开头、以 > 结尾的整段是标记语言；PHP 文件也以 < 开头，由 <?php 特征区分
    if (trimmed[0] == L'<' && trimmed[trimmed.size() - 1] == L'>' && trimmed.compare(0, 5, L"<?php") != 0)
        scores[code_lang_xml] += kMarkupWholeScore;

    const std::wstring lower = ToLowerAscii(sample);
    for (size_t i = 0; i < sizeof(kMarkers) / sizeof(kMarkers[0]); ++i)
    {
        const DetectMarker& m = kMarkers[i];
        const int hits = CountOccurrences(m.m_ci ? lower : sample, m.m_text, kMarkerMaxHits);
        scores[m.m_lang] += hits * m.m_weight;
    }

    int best = code_lang_plain;
    for (int lang = 1; lang < code_lang_count; ++lang)
    {
        if (scores[lang] > scores[best])
            best = lang;
    }
    if (scores[best] < kDetectMinScore)
        return code_lang_plain;
    return best;
}

bool LooksLikeCode(const std::wstring& text)
{
    const std::wstring sample = text.substr(0, kDetectSampleChars);
    int nonEmptyLines = 0;
    int shapedLines = 0;
    long nonSpace = 0;
    long cjk = 0;
    long symbols = 0;

    size_t begin = 0;
    while (begin <= sample.size())
    {
        size_t end = sample.find_first_of(L"\r\n", begin);
        if (end == std::wstring::npos)
            end = sample.size();
        const std::wstring line = sample.substr(begin, end - begin);
        begin = end + 1;

        const std::wstring trimmed = TrimSpaces(line);
        if (trimmed.empty())
            continue;
        ++nonEmptyLines;
        //缩进开头、或以 ; { } 结尾的行是代码的典型形态
        const wchar_t last = trimmed[trimmed.size() - 1];
        if (line[0] == L' ' || line[0] == L'\t' || last == L';' || last == L'{' || last == L'}')
            ++shapedLines;
        //注释行里的中文不算正文：代码里常有整行的中文注释
        if (IsCommentLine(trimmed))
            continue;
        //行尾注释 // 之后的部分同样不计
        const size_t lineComment = trimmed.find(L"//");
        const size_t countEnd = (lineComment == std::wstring::npos) ? trimmed.size() : lineComment;
        for (size_t i = 0; i < countEnd; ++i)
        {
            const wchar_t c = trimmed[i];
            if (IsSpaceChar(c))
                continue;
            ++nonSpace;
            if (IsCjkChar(c))
                ++cjk;
            else if (IsCodeSymbol(c))
                ++symbols;
        }
    }
    if (nonEmptyLines == 0)
        return false;
    //中文占比高的是正文
    if (nonSpace > 0 && cjk * 100 > nonSpace * kProseCjkPercent)
        return false;
    if (DetectCodeLanguage(text) != code_lang_plain)
        return true;
    if (nonEmptyLines < kShapeMinLines || nonSpace == 0)
        return false;
    const bool shapeOk = shapedLines * 100 >= nonEmptyLines * kShapeLinePercent;
    const bool symbolsOk = symbols * 100 >= nonSpace * kShapeSymbolPercent;
    return shapeOk && symbolsOk;
}

} // namespace codesnippet
