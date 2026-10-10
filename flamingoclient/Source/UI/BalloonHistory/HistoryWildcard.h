/**
 *  聊天记录管理器通配符搜索的纯逻辑，HistoryWildcard.h
 *
 *  聊天记录管理器里的搜索框旁有一个「通配符」复选框，默认不勾（docs/聊天记录管理器方案.md §11.4）。
 *  勾上之后：
 *    · '*' 匹配任意个字符（含零个），'?' 匹配恰好一个字符（一个 Unicode 字符，汉字、表情都算一个）；
 *    · 仍按「包含」匹配，相当于输入的两端各有一个 '*'；
 *    · 大小写规则与不勾时相同：只忽略 ASCII 字母的大小写（SQLite LIKE 的规则）。
 *  不勾时 '*'、'?' 按普通字符处理，走 V2MsgQuery.h 的 MakeLikePattern 与 HistoryView.h 的
 *  FindHighlightRanges。
 *
 *  本文件提供四件事：把输入转成 LIKE 匹配串、切出可交给全文索引的文字段、判断输入是否只剩 '*'、
 *  在显示文字里找命中区间供高亮。另有两个按「是否勾选通配符」分派的包装函数，调用方不必自己判断。
 *  都是纯计算，不依赖 SQLite、ATL 与界面，被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用（单测见
 *  Tests/HistoryWildcardTests.cpp，标签 [wildcard]）。在 Flamingo.vcxproj 里本文件需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  典型用法：
 *      if (!WildcardHasContent(kw))   ... 按没有关键字处理
 *      std::string like = MakeSearchLikePattern(kw, wildcard);      // 绑定到 "LIKE ? ESCAPE '\'"
 *      std::string fts;
 *      if (wildcard && BuildWildcardFtsQuery(kw, fts))   ... 先用 MATCH fts 找候选行
 *      ranges = FindSearchHighlightRanges(text, kwW, wildcard);    // 高亮
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <string>
#include <vector>
#include "HistoryView.h"   // HighlightRange

namespace balloonhistory {

//通配符里表示「任意个字符（含零个）」的字符
const char kWildcardAnyChar = '*';
//通配符里表示「恰好一个字符」的字符
const char kWildcardOneChar = '?';

/**
 *  判断通配符输入去掉全部 '*' 之后是否还有内容。只有 '*'（或为空）的输入匹配一切，按「没有关键字」处理。
 *  首尾空白由调用方先去掉，本函数不处理。
 *  @param patternUtf8 用户输入（UTF-8）
 *  @return 除 '*' 以外还有别的字符（包括 '?'）时返回 true
 */
bool WildcardHasContent(const std::string& patternUtf8);

/**
 *  把通配符输入转成 SQLite LIKE 的匹配串，配合语句里的 ESCAPE '\' 使用：
 *  先在 '\'、'%'、'_' 前面加反斜杠（它们按普通字符匹配），再把 '*' 换成 '%'、'?' 换成 '_'，
 *  前后各加一个 '%'。连续的 '*' 以及首尾的 '*' 与外加的 '%' 合并成一个，不出现 "%%"。
 *  例：「季度*报告」→「%季度%报告%」，「a?c」→「%a_c%」，「50%*」→「%50\%%」。
 *  UTF-8 多字节字符的每个字节都不小于 0x80，不会与这几个 ASCII 字符混淆，逐字节处理即可。
 *  @param patternUtf8 用户输入（UTF-8）；只有 '*' 时结果为 "%"（匹配一切），调用方应先用
 *                     WildcardHasContent 排除这种输入
 *  @return 可直接绑定到 LIKE ? 的匹配串
 */
std::string MakeWildcardLikePattern(const std::string& patternUtf8);

/**
 *  按是否勾选通配符生成 LIKE 匹配串：勾选时为 MakeWildcardLikePattern，否则为 MakeLikePattern。
 *  两者都要求语句里写 ESCAPE '\'。
 *  @param keywordUtf8 用户输入（UTF-8，调用方已去掉首尾空白）
 *  @param wildcard    是否勾选了「通配符」
 *  @return 可直接绑定到 LIKE ? 的匹配串
 */
std::string MakeSearchLikePattern(const std::string& keywordUtf8, bool wildcard);

/**
 *  为通配符输入生成全文索引（t_msg_fts，trigram 分词）的 MATCH 查询串，用来先缩小候选行。
 *  按 '*'、'?' 把输入切成若干段普通文字，取不少于 kTrigramMinChars 个字的段，各自按 MakeFtsPhrase
 *  放进双引号，再用 " AND " 连起来。不够 3 个字的段不放进去：trigram 索引查不了这么短的片段。
 *  LIKE 能匹配的行一定包含每一段文字，所以候选行包含 LIKE 的全部命中，调用方仍须用 LIKE 复核。
 *  例：「第三季度*工作报告」→ "第三季度" AND "工作报告"；「季度*报告」→ 没有够长的段。
 *  @param patternUtf8 用户输入（UTF-8）
 *  @param outQuery    [出参] MATCH 查询串；返回 false 时被清空
 *  @return 至少有一段够 3 个字时返回 true；一段都没有时返回 false，调用方应整表 LIKE 扫描
 */
bool BuildWildcardFtsQuery(const std::string& patternUtf8, std::string& outQuery);

/**
 *  在一段显示文字里找出通配符输入的全部命中区间，供搜索结果高亮。
 *  规则与 LIKE 一致：'*' 匹配任意个字符，'?' 匹配一个 Unicode 字符（UTF-16 代理对算一个），只忽略
 *  ASCII 字母的大小写，其余字符（包括 '%'、'_'、'\'）按原样匹配。首尾的 '*' 不影响结果。
 *  每一处命中取最左边起点上最短的那一段（不让一个 '*' 把整行都标上），命中之间不重叠：找到一处后
 *  从该处末尾继续找。
 *  @param text    被搜索的文字（UTF-16）
 *  @param pattern 用户输入（UTF-16）；为空或只有 '*' 时返回空集
 *  @return 全部命中区间（UTF-16 下标），按出现顺序；没有命中返回空集
 */
std::vector<HighlightRange> FindWildcardHighlightRanges(const std::wstring& text, const std::wstring& pattern);

/**
 *  找搜索框输入在一段文字里的高亮区间：先按搜索语法（HistorySearchSyntax.h，docs/聊天记录管理器方案.md §12.3）
 *  取出各正向词，排除词与 from: 等限定词不高亮；每个词按是否勾选通配符分别用 FindWildcardHighlightRanges 或
 *  FindHighlightRanges 找命中处，最后合并成互不重叠的区间。
 *  @param text     被搜索的文字（UTF-16）
 *  @param keyword  搜索框里的输入（UTF-16）
 *  @param wildcard 是否勾选了「通配符」
 *  @return 全部命中区间（UTF-16 下标），按起点升序、互不重叠
 */
std::vector<HighlightRange> FindSearchHighlightRanges(const std::wstring& text, const std::wstring& keyword,
                                                      bool wildcard);

/**
 *  把若干高亮区间合并成按起点升序、互不重叠的区间：重叠或首尾相接的并成一段，长度不为正的丢弃。
 *  @param ranges  区间（顺序不限）
 *  @return 合并后的区间
 */
std::vector<HighlightRange> MergeHighlightRanges(const std::vector<HighlightRange>& ranges);

} // namespace balloonhistory
