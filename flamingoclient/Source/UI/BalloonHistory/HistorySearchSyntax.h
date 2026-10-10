/**
 *  聊天记录管理器的搜索语法，HistorySearchSyntax.h
 *
 *  把搜索框里的一行输入拆成几类条件（docs/聊天记录管理器方案.md §12.3）：
 *    · 空白（含全角空格）分隔的多个词，同时包含才算命中；"…" 或 “…” 括起的部分是一个词，里面可以有空格；
 *    · -词：排除含这个词的消息（单独一个 - 按普通字符）；
 *    · from:名字 / 发送人:名字：只看这个人发的；
 *    · type:类型 / 类型:类型：文字、图片、文件、语音、链接、Pin（及对应英文）；
 *    · after:日期 / before:日期：日期写成 YYYY-MM-DD、YYYY/MM/DD 或 YYYYMMDD，before 含当天。
 *  限定词名不区分大小写，冒号可以是半角或全角；认不出的「xxx:」整个按普通词处理（http://… 不受影响）。
 *  勾选通配符时每个词各自按通配符解释（规则见 HistoryWildcard.h），去掉 '*' 后为空的词丢弃。
 *
 *  本模块只解析，不拼 SQL：V2MsgQuery 的 BuildWhereClause 等据解析结果为每个正向词加一个 LIKE、每个排除词加一个
 *  NOT LIKE，并把限定条件写进语句；高亮由 FindSearchHighlightRanges（HistoryWildcard.h）合并各正向词的命中区间。
 *
 *  典型用法：
 *      const HistorySearchQuery q = ParseHistorySearch("季度 报告 -草稿 from:张三 type:文件", false);
 *      // q.m_terms = {"季度","报告"}，q.m_excludes = {"草稿"}，q.m_fromNames = {"张三"}，q.m_type = kHistoryTypeFile
 *
 *  纯逻辑，不依赖 ATL。单测见 Tests/HistorySearchSyntaxTests.cpp（标签 [searchsyntax]）。在 Flamingo.vcxproj 里
 *  本文件对应的 .cpp 标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace balloonhistory {

//type: 没有写、或写了认不出的值时 HistorySearchQuery::m_type 的取值
const int kSearchTypeNone = -1;

//一行搜索输入解析出的条件
struct HistorySearchQuery
{
    std::vector<std::string> m_terms;      //正向词（UTF-8，引号已去掉），每个都要包含
    std::vector<std::string> m_excludes;   //排除词（UTF-8），含任何一个都不算命中
    std::vector<std::string> m_fromNames;  //from: 后面的名字（UTF-8）；写了多个时命中其中任何一人即可
    int                      m_type;       //type: 对应的 HistoryTypeFilter 取值；kSearchTypeNone 表示没有限定
    int64_t                  m_afterMs;    //after: 那天 0 点（Unix 毫秒）；0 表示没有限定
    int64_t                  m_beforeMs;   //before: 那天最后一毫秒（Unix 毫秒）；0 表示没有限定
    //有写错的限定值（日期不合法、type 认不出、from: 后面是空的）。为 true 时这次搜索不应有任何结果，
    //界面据此提示，而不是悄悄忽略写错的条件
    bool                     m_badModifier;

    HistorySearchQuery()
        : m_type(kSearchTypeNone)
        , m_afterMs(0)
        , m_beforeMs(0)
        , m_badModifier(false)
    {
    }
};

/**
 *  解析一行搜索输入。
 *  @param inputUtf8  搜索框里的文字（UTF-8）
 *  @param wildcard   是否勾选了通配符：为 true 时去掉 '*' 后为空的词（正向与排除）丢弃
 *  @return 解析结果；输入为空或只有空白时各项都为空
 */
HistorySearchQuery ParseHistorySearch(const std::string& inputUtf8, bool wildcard);

/**
 *  解析结果里有没有任何条件（正向词、排除词、from、type、日期，或写错的限定值）。
 *  @param q  ParseHistorySearch 的结果
 *  @return 有条件时返回 true；为 false 时等于没有输入关键字
 */
bool SearchQueryHasCondition(const HistorySearchQuery& q);

/**
 *  只取正向词（高亮与全文索引加速用），宽字符版本。
 *  @param input     搜索框里的文字（UTF-16）
 *  @param wildcard  是否勾选了通配符
 *  @return 正向词（UTF-16），顺序同输入
 */
std::vector<std::wstring> SearchTermsForHighlight(const std::wstring& input, bool wildcard);

/**
 *  type: 后面的值换成 HistoryTypeFilter 的取值。
 *  @param valueUtf8  值（UTF-8），如「图片」「image」「Pin」；英文不区分大小写
 *  @return 取值；认不出时返回 kSearchTypeNone
 */
int SearchTypeFromText(const std::string& valueUtf8);

/**
 *  日期文字换成年月日。
 *  @param textUtf8  YYYY-MM-DD、YYYY/MM/DD 或 YYYYMMDD（月、日可以是一位数）
 *  @param year / month / day  [出参] 年月日；返回 false 时不改
 *  @return 格式正确时返回 true（不检查这一天是否存在，交给调用方换算时判断）
 */
bool ParseSearchDate(const std::string& textUtf8, int& year, int& month, int& day);

/**
 *  一个显示名是否与 from: 写的名字匹配：包含即算，ASCII 字母不区分大小写。
 *  @param displayNameUtf8  某人的显示名（UTF-8）
 *  @param fromNameUtf8     from: 后面写的名字（UTF-8），不能为空
 *  @return 匹配时返回 true
 */
bool SearchNameMatches(const std::string& displayNameUtf8, const std::string& fromNameUtf8);

/**
 *  把 from: 写的名字换成 userid：在候选人里挑出显示名与任一 from: 名字匹配（SearchNameMatches）的人。
 *  @param q             ParseHistorySearch 的结果
 *  @param candidates    候选人 userid（会话内搜索是本会话发过言的人，跨会话搜索是本地库里发过言的所有人）
 *  @param namesUtf8     与 candidates 一一对应的显示名（UTF-8）；条数少于 candidates 时多出的候选人不参与
 *  @return 匹配上的 userid，顺序同 candidates、不重复；q 里没有 from: 时返回空表
 */
std::vector<int64_t> MatchSearchFromIds(const HistorySearchQuery& q, const std::vector<int64_t>& candidates,
                                        const std::vector<std::string>& namesUtf8);

} // namespace balloonhistory
