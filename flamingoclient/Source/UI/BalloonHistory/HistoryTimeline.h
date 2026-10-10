/**
 *  聊天记录管理器时间轴模式的纯逻辑，HistoryTimeline.h
 *
 *  时间轴模式（docs/聊天记录管理器方案.md §11.7）在聊天记录页右侧加一条导航条（按年分组、每月一格，
 *  显示当月消息量与命中条数），列表改成时间轴样式（每天一个大节点，写星期几与当天条数），有关键字时
 *  不筛掉其它消息、只高亮，并可在命中之间跳转。本文件承载其中不碰界面、不碰数据库的部分：
 *    · BuildTimelineYears：把本地库按月统计出的总条数与命中条数合并成导航条的按年数据；
 *    · TimelineBarPercent：月份消息量条的长度；
 *    · LocalMonthKeyOf / LocalMonthBoundsMs / WeekdayOfDayKey / FormatDayKey：日期换算；
 *    · HistoryHitNavigator：一批命中位置与「当前第几处」，上一处 / 下一处循环跳转。
 *
 *  典型用法：
 *      std::vector<HistoryPeriodCount> counts, hits;
 *      store.CountByLocalMonth(listFilter, now, counts);
 *      store.CountByLocalMonth(keywordFilter, now, hits);
 *      std::vector<HistoryTimelineYear> years = BuildTimelineYears(counts, hits);
 *
 *      HistoryHitNavigator nav;
 *      nav.Reset(positions, positions.size() >= kTimelineMaxHits);
 *      bool wrapped = false;
 *      const int index = nav.Next(wrapped);
 *
 *  单测见 Tests/HistoryTimelineTests.cpp（标签 [timeline]）。本文件不依赖 ATL，在 Flamingo.vcxproj 里
 *  对应的 .cpp 标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <stdint.h>
#include <set>
#include <string>
#include <vector>
#include "V2MsgQuery.h"   // HistoryPeriodCount、HistoryPosition

namespace balloonhistory {

//时间轴模式下一次最多取多少处命中位置。命中更多时只列出最新的这么多处，命中条上写「超过 N 处」
const int kTimelineMaxHits = 2000;

//导航条里月份消息量条有消息时的最短长度（百分比），免得消息很少的月份看上去像没有消息
const int kTimelineBarMinPercent = 4;

//导航条里的一个月
struct HistoryTimelineMonth
{
    int     m_year;    //年，如 2026
    int     m_month;   //月，1～12
    int64_t m_count;   //当月消息条数（按列表的筛选条件，不含关键字）
    int64_t m_hits;    //当月命中关键字的条数；没有关键字时为 0

    HistoryTimelineMonth()
        : m_year(0)
        , m_month(0)
        , m_count(0)
        , m_hits(0)
    {
    }
};

//导航条里的一年
struct HistoryTimelineYear
{
    int                               m_year;     //年
    int64_t                           m_count;    //全年消息条数
    int64_t                           m_hits;     //全年命中条数
    std::vector<HistoryTimelineMonth> m_months;   //有消息的各月，按月份倒序（最近的在前）

    HistoryTimelineYear()
        : m_year(0)
        , m_count(0)
        , m_hits(0)
    {
    }
};

/**
 *  把按月统计的总条数与命中条数合并成导航条的按年数据。
 *  @param monthCounts  各月总条数（键为 年*100+月），顺序不限；键不合法（月份不在 1～12）的项忽略
 *  @param monthHits    各月命中条数（键同上）；没有关键字时传空。只在命中里出现的月份同样列出，总数按命中数补
 *  @return 各年，按年份倒序；每年里各月按月份倒序
 */
std::vector<HistoryTimelineYear> BuildTimelineYears(const std::vector<HistoryPeriodCount>& monthCounts,
                                                    const std::vector<HistoryPeriodCount>& monthHits);

/**
 *  所有年份里条数最多的那个月的条数，作为消息量条的满格值。
 *  @param years  BuildTimelineYears 的结果
 *  @return 最大月条数；没有任何月份时为 0
 */
int64_t TimelineMaxMonthCount(const std::vector<HistoryTimelineYear>& years);

/**
 *  月份消息量条的长度。
 *  @param count     当月条数
 *  @param maxCount  满格值（TimelineMaxMonthCount）
 *  @return 百分比 0～100：count 不大于 0 或 maxCount 不大于 0 时为 0；否则按比例取整，且不小于
 *          kTimelineBarMinPercent、不大于 100
 */
int TimelineBarPercent(int64_t count, int64_t maxCount);

/**
 *  某个时刻在本地时间是哪一年哪一月。
 *  @param timestampMs  Unix 毫秒
 *  @return 年*100+月（如 202610）；时间不大于 0 或换算失败时为 0
 */
int64_t LocalMonthKeyOf(int64_t timestampMs);

/**
 *  本地时间某个月的起止：当月 1 日 0 点与下月 1 日 0 点前 1 毫秒（Unix 毫秒）。
 *  @param year / month  年、月（1～12）
 *  @param outStartMs    [出参] 起
 *  @param outEndMs      [出参] 止
 *  @return 年月合法返回 true；月份不在 1～12 或换算失败时返回 false，出参为 0
 */
bool LocalMonthBoundsMs(int year, int month, int64_t& outStartMs, int64_t& outEndMs);

/**
 *  按最早、最晚一条消息的时间，列出中间每个月（含两端所在的月）在本地时间里的起止，供本地库按区间统计各月条数
 *  （不再逐行换算本地时间）。
 *  @param oldestMs  最早一条的时间（Unix 毫秒）
 *  @param newestMs  最晚一条的时间（Unix 毫秒）
 *  @return 各月区间，按月份倒序（最近的在前）；任一时间不大于 0、或 oldestMs 晚于 newestMs 时为空
 */
std::vector<HistoryMonthRange> BuildLocalMonthRanges(int64_t oldestMs, int64_t newestMs);

/**
 *  某一天是星期几。按公历计算，不涉及时区。
 *  @param dayKey  年*10000+月*100+日（如 20261001）
 *  @return 0 星期日，1 星期一 … 6 星期六；dayKey 不合法（月、日越界）时返回 -1
 */
int WeekdayOfDayKey(int64_t dayKey);

/**
 *  把日期键写成「2026-10-01」。
 *  @param dayKey  年*10000+月*100+日
 *  @return 日期文字；dayKey 不大于 0 时为空串
 */
std::string FormatDayKey(int64_t dayKey);

/**
 *  在一组分段统计里查某个键的条数。
 *  @param counts  分段统计
 *  @param key     键
 *  @return 条数；没有这个键时为 0
 */
int64_t PeriodCountOf(const std::vector<HistoryPeriodCount>& counts, int64_t key);

/**
 *  导航条默认展开哪一年：当前月份所在的那一年在导航数据里时取它，否则取最近的一年。往年默认收起。
 *  @param years            BuildTimelineYears 的结果
 *  @param currentMonthKey  当前列表所在的月份（年*100+月）；0 表示不知道
 *  @return 年份；years 为空时返回 0
 */
int DefaultExpandedYear(const std::vector<HistoryTimelineYear>& years, int64_t currentMonthKey);

/**
 *  HistoryHitNavigator —— 时间轴模式下的一批命中位置与「当前第几处」。
 *
 *  命中位置按列表的排序给出（最新在前）。「下一处」往列表下方走（时间更早），「上一处」往上走；走过头时
 *  循环到另一端，并通过出参告诉调用方绕回了一圈。只在界面线程使用。
 */
class HistoryHitNavigator
{
public:
    HistoryHitNavigator();

    //换一批命中位置并清掉当前处。
    //@param hits       命中位置，排序同列表（最新在前）
    //@param truncated  命中是否多于取到的条数（按 kTimelineMaxHits 截断过）
    void Reset(const std::vector<HistoryPosition>& hits, bool truncated);

    //清空：没有命中、没有当前处。
    void Clear();

    //取到的命中处数。
    int Count() const;

    //命中是否多于取到的条数。
    bool Truncated() const;

    //当前第几处（从 0 起）；还没有当前处时为 -1。
    int Current() const;

    //第 index 处的位置；越界时返回 NULL。返回的指针在下一次 Reset / Clear 之前有效。
    const HistoryPosition* At(int index) const;

    //跳到下一处（列表里更靠下、时间更早的一处）。还没有当前处时取第一处；已是最后一处时回到第一处。
    //@param wrapped  [出参] 是否从最后一处绕回了第一处
    //@return 新的当前处；没有任何命中时返回 -1
    int Next(bool& wrapped);

    //跳到上一处。还没有当前处时取最后一处；已是第一处时回到最后一处。
    //@param wrapped  [出参] 是否从第一处绕回了最后一处
    //@return 新的当前处；没有任何命中时返回 -1
    int Prev(bool& wrapped);

    //把当前处设为 index。
    //@return index 在 [0, Count()) 内时设置并返回 true；否则不改，返回 false
    bool SetCurrent(int index);

    //某条消息是第几处命中。
    //@return 下标；不在取到的命中里时返回 -1
    int IndexOfMsgid(int64_t msgid) const;

    //某条消息是否在取到的命中里。
    bool Contains(int64_t msgid) const;

    //从某个位置起（含它本身）往列表下方找第一处命中，供「从当前页开始找」用。
    //@param timestamp / msgid  起点的位置（排序同列表）
    //@return 下标；起点及以下都没有命中时返回 -1
    int FirstAtOrAfter(int64_t timestamp, int64_t msgid) const;

private:
    std::vector<HistoryPosition> m_hits;       //命中位置，最新在前
    std::set<int64_t>            m_msgids;     //命中消息的 msgid，判断某一行是不是命中用
    bool                         m_truncated;  //命中是否多于取到的条数
    int                          m_current;    //当前第几处；-1 表示还没有当前处
};

} // namespace balloonhistory
