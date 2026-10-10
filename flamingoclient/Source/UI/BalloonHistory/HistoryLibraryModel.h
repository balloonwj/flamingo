/**
 *  聊天记录管理器「文件」「图片」「链接」「与我有关」四页的浏览模型，HistoryLibraryModel.h
 *
 *  持有一次浏览的筛选条件与页号（HistoryLibraryFilter），把 CV2MsgStore 的统计、分页与按会话统计三个查询编排起来，
 *  对外给出总数、页数、当前页各行与各会话的条数，供窗口绑定界面（docs/聊天记录管理器方案.md §11.5、§11.6、
 *  §12.4、§12.5）。切换任一筛选条件都回到第 1 页；切换页时每页条数随之改变（图片页 kHistoryLibraryImagePageSize，
 *  其余 kHistoryLibraryFilePageSize）。与聊天记录页的 HistoryModel 是同一种写法。
 *
 *  只在界面线程使用。单测见 Tests/HistoryLibraryTests.cpp（标签 [histfiles] / [histimages]）。在 Flamingo.vcxproj
 *  里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include "HistoryLibraryQuery.h"

namespace balloonhistory {

class CV2MsgStore;

//文件页每页条数（与 HistoryLibraryLayout.h 的同名常量取值一致，模型不依赖界面头文件，故另行定义）
const int kHistoryLibraryModelFilePageSize = 50;
//图片页每页张数
const int kHistoryLibraryModelImagePageSize = 120;

/**
 *  HistoryLibraryModel —— 文件 / 图片页的浏览状态与查询编排。不持有本地库（只借用）。
 */
class HistoryLibraryModel
{
public:
    HistoryLibraryModel();

    //注入本地库（借用，不持有；调用方保证其生命周期覆盖本模型）。可传 NULL 解绑。
    void SetStore(CV2MsgStore* store);

    //设置本人 userid（判断「我发送的 / 我接收的」用）。不改页号。
    void SetSelfUserId(int64_t selfUserId);

    //切换文件页 / 图片页 / 链接页 / 与我有关页（取值见 HistoryLibraryKind，越界按文件页）：每页条数随之改变
    //（图片页 kHistoryLibraryModelImagePageSize，其余 kHistoryLibraryModelFilePageSize），回到第 1 页。
    //取值不变时什么都不做。其余筛选条件保留。
    void SetKind(int kind);

    //设置方向筛选（取值见 HistoryLibraryDirection），回到第 1 页。
    void SetDirection(int direction);

    //设置与我有关页的关系筛选（取值见 HistoryLibraryRelation，越界按「全部」），回到第 1 页。
    void SetRelation(int relation);

    //设置关键字里 from:名字 解析出的 userid（链接页、与我有关页用，见 HistoryLibraryFilter::m_syntaxFromIds）；
    //与当前取值相同时什么都不做，不同时回到第 1 页。
    void SetSyntaxFromIds(const std::vector<int64_t>& ids);

    //设置来源筛选（取值见 HistoryLibrarySource），回到第 1 页。
    void SetSource(int source);

    //设置类型筛选（取值见 HistoryFileCategory），回到第 1 页。
    void SetCategory(int category);

    //设置时间范围（自定义区间不在此列），回到第 1 页。
    void SetTimeRange(HistoryTimeRange range);

    //设置文件名关键字（UTF-8，空串表示不筛选），回到第 1 页。
    void SetKeyword(const std::string& keyword);

    //设置关键字是否按通配符解释；与当前取值不同且关键字非空时回到第 1 页。
    void SetWildcard(bool wildcard);

    //只看某个会话（左栏点了某个会话）；cid 为 0 表示全部会话。回到第 1 页。
    void SetConversation(int ctype, int64_t cid);

    //全部会话时只看单聊（1）或群聊（2），0 表示不限（左栏的类型胶囊），回到第 1 页。
    void SetCtypeOnly(int ctypeOnly);

    //把筛选条件恢复为初始状态：全部方向、全部关系、全部来源、全部类型、全部时间、没有关键字（含 from: 解析结果）、
    //不勾通配符、全部会话、不限会话类型，回到第 1 页。汇总哪一类、本人 userid 不变。窗口新打开时调用。
    void ResetFilters();

    //翻到指定页（按上次统计的总数夹到 [1, PageCount]）。
    void GoToPage(int page);

    //按当前条件统计总数并取当前页（页号先按新总数夹取）。
    //@param nowMs  当前时间（Unix 毫秒）
    //@return 查询成功返回 true；未注入本地库或出错返回 false
    bool Refresh(int64_t nowMs);

    //只换页：沿用上次统计的总数，只取当前页。筛选条件变过时与 Refresh 相同。
    //@return 查询成功返回 true；未注入本地库或出错返回 false
    bool RefreshPage(int64_t nowMs);

    //按当前条件统计各会话的条数（左栏每一行右侧的数字）。
    //@return 查询成功返回 true；未注入本地库或出错返回 false
    bool RefreshConvCounts(int64_t nowMs);

    //当前条件（含页号、每页条数）。
    const HistoryLibraryFilter& Filter() const;

    //上次统计得到的总条数。
    int64_t Total() const;

    //总页数：至少 1。
    int PageCount() const;

    //当前页号（从 1 起）。
    int CurrentPage() const;

    //当前页各行（时间倒序）。
    const std::vector<HistoryLibraryItem>& Items() const;

    //上次按会话统计的结果（按 ctype、cid 升序）。
    const std::vector<HistoryLibraryConvCount>& ConvCounts() const;

    //某个会话在上次按会话统计里的条数；没有时为 0。
    int CountOf(int ctype, int64_t cid) const;

    //上次按会话统计的各会话条数之和（「全部会话」那一行的数字）。
    int64_t CountAll() const;

    //自创建以来执行过几次统计总数的查询，供单测核对只换页时没有重新统计。
    int64_t CountQueryTimes() const;

private:
    //回到第 1 页
    void ResetPage();

    //按 m_total 把页号夹到 [1, PageCount()]
    void ClampPage();

    //两组条件统计出的总数是否必然相同（页号、每页条数除外的字段都相同）
    static bool SameCountFilter(const HistoryLibraryFilter& a, const HistoryLibraryFilter& b);

private:
    CV2MsgStore*                         m_store;         //本地库（借用）；NULL 表示未注入
    HistoryLibraryFilter                 m_filter;        //当前条件与页号
    int64_t                              m_total;         //上次统计的总条数
    std::vector<HistoryLibraryItem>      m_items;         //当前页各行
    std::vector<HistoryLibraryConvCount> m_convCounts;    //上次按会话统计的结果
    HistoryLibraryFilter                 m_countedFilter; //上次统计总数时的条件；RefreshPage 据它判断条件是否变过
    bool                                 m_countValid;    //m_total 是否来自一次成功的统计
    int64_t                              m_countQueries;  //执行过的统计总数查询次数，只增不减
};

} // namespace balloonhistory
