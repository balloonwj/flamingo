/**
 *  消息记录窗"本地连续覆盖区间"纯逻辑实现，说明见 HistoryCoverage.h。
 *  balloonwj@qq.com   2026-09-30
 */
#include "HistoryCoverage.h"

#include <algorithm>
#include <set>
#include <stdio.h>   // swprintf_s（副标题与底部信息栏文字）
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonhistory {

namespace {

//生成副标题与底部信息栏文字用的缓冲区长度（字符数）：最长的一句约 40 个字符，留足余量
const int kHistoryTextBufLen = 128;

} // anonymous namespace

PageSpan ComputePageSpan(const SyncPageInfo& page)
{
    PageSpan span;

    if (page.m_msgids.empty())
    {
        //从最新开始拉却一条都没有：没有上界可用，无法确定范围
        if (page.m_beforeMsgid <= 0)
        {
            return span;
        }
        //往前翻却一条都没有：锚点以下服务端已经一条都不剩。服务端此时不会说"还有更多"，
        //真说了也自相矛盾，按无法确定处理
        if (page.m_hasMore)
        {
            return span;
        }
        span.m_valid = true;
        span.m_lo = 0;
        span.m_hi = page.m_beforeMsgid - 1;
        span.m_reachesOldest = true;
        return span;
    }

    //本页 msgid 的顺序不作假设，按最小、最大取
    const int64_t minId = *std::min_element(page.m_msgids.begin(), page.m_msgids.end());
    const int64_t maxId = *std::max_element(page.m_msgids.begin(), page.m_msgids.end());

    span.m_valid = true;
    span.m_reachesOldest = !page.m_hasMore;
    //服务端说没有更多了：本页最早一条之下也不存在任何消息，下界可以一直扩到 0
    span.m_lo = page.m_hasMore ? minId : 0;
    //往前翻时上界取"锚点 - 1"：服务端返回的是锚点之前、紧挨着锚点的一段，锚点与本页最新
    //一条之间不可能还有漏掉的消息；从最新开始拉时只能以本页最新一条为上界
    span.m_hi = (page.m_beforeMsgid > 0) ? (page.m_beforeMsgid - 1) : maxId;
    return span;
}

CoverRange MergeCoverage(const CoverRange& cur, const SyncPageInfo& page)
{
    const PageSpan span = ComputePageSpan(page);
    if (!span.m_valid)
    {
        return cur;
    }

    CoverRange spanCover;
    spanCover.m_valid = true;
    spanCover.m_lo = span.m_lo;
    spanCover.m_hi = span.m_hi;
    spanCover.m_reachedOldest = span.m_reachesOldest;

    const bool isLatestPage = (page.m_beforeMsgid <= 0);
    if (!cur.m_valid)
    {
        //只有从最新开始拉的一页才能确定"从最新往下"，往前翻的一页不建立区间
        return isLatestPage ? spanCover : cur;
    }

    //两段是否接得上：重叠或首尾相接。上界写成"下界 - 1 <= 对方上界"而不是
    //"上界 + 1 >= 对方下界"，是为了避免上界取到 INT64 上限时加 1 溢出
    const bool connected = (span.m_lo - 1 <= cur.m_hi) && (cur.m_lo - 1 <= span.m_hi);
    if (!connected)
    {
        //从最新开始拉的一页接不上：中间可能有断层，以本页重新起算；
        //往前翻的一页接不上：不改动原区间
        return isLatestPage ? spanCover : cur;
    }

    CoverRange merged;
    merged.m_valid = true;
    merged.m_lo = std::min(cur.m_lo, span.m_lo);
    merged.m_hi = std::max(cur.m_hi, span.m_hi);
    merged.m_reachedOldest = cur.m_reachedOldest || span.m_reachesOldest;
    if (merged.m_reachedOldest)
    {
        merged.m_lo = 0;
    }
    return merged;
}

std::vector<int64_t> ComputeReconcileDeletions(const SyncPageInfo& page,
                                               const std::vector<int64_t>& localMsgids)
{
    std::vector<int64_t> toDelete;
    const PageSpan span = ComputePageSpan(page);
    if (!span.m_valid)
    {
        return toDelete;
    }

    const std::set<int64_t> onServer(page.m_msgids.begin(), page.m_msgids.end());
    for (size_t i = 0; i < localMsgids.size(); ++i)
    {
        const int64_t id = localMsgids[i];
        //范围之外的本地行不归本页管：更早的可能还没拉到，更新的可能是对账期间才到的
        if (id < span.m_lo || id > span.m_hi)
        {
            continue;
        }
        if (onServer.find(id) == onServer.end())
        {
            toDelete.push_back(id);
        }
    }
    return toDelete;
}

int64_t CoverageMinVisibleMsgid(const CoverRange& cover, bool isPinnedCategory)
{
    //Pin 列表是服务端单独给的全集，与本地历史是否连续无关
    if (isPinnedCategory)
    {
        return 0;
    }
    //还没有区间：本地的行是否完整、之间有没有断层都还不知道，先照旧全部显示，
    //回填到达、区间建立后再收窄（口径见头文件说明）
    if (!cover.m_valid)
    {
        return 0;
    }
    return cover.m_reachedOldest ? 0 : cover.m_lo;
}

bool ShouldFetchOlder(const CoverRange& cover, bool isLastPage, bool isPinnedCategory,
                      bool hasTimeLowerBound, int64_t timeLowerBoundMs,
                      int64_t oldestCoveredTimeMs)
{
    //"要不要去拉" = 停在最后一页 且 "还有没拉的"；两者共用一套条件，免得界面上写着"已加载"
    //却不再去拉，或反过来
    return isLastPage && HasUnloadedOlder(cover, isPinnedCategory, hasTimeLowerBound,
                                          timeLowerBoundMs, oldestCoveredTimeMs);
}

bool ShouldContinueFetchOlder(int roundsDone, int maxRounds, int64_t countBefore,
                              int64_t countNow, int pageSize, bool reachedOldest)
{
    if (reachedOldest || roundsDone >= maxRounds)
    {
        return false;
    }
    //筛选结果已经多出至少一整页，用户翻到的"新末页"之前已有足够内容可看
    return (countNow - countBefore) < pageSize;
}

bool HasUnloadedOlder(const CoverRange& cover, bool isPinnedCategory,
                      bool hasTimeLowerBound, int64_t timeLowerBoundMs,
                      int64_t oldestCoveredTimeMs)
{
    if (!cover.m_valid || cover.m_reachedOldest || isPinnedCategory)
    {
        return false;
    }
    //区间内最早一条已经早于或等于筛选下界：筛选的整个时间段都已在区间内，再往前也不会
    //多出符合筛选的消息
    if (hasTimeLowerBound && timeLowerBoundMs >= oldestCoveredTimeMs)
    {
        return false;
    }
    return true;
}

std::wstring BuildHistorySubtitleText(int64_t total, bool hasUnloadedOlder)
{
    wchar_t buf[kHistoryTextBufLen] = { 0 };
    if (hasUnloadedOlder)
    {
        swprintf_s(buf, kHistoryTextBufLen, TR(L"已加载 %lld 条消息，翻到最后一页会继续加载更早的"),
                   (long long)total);
    }
    else
    {
        swprintf_s(buf, kHistoryTextBufLen, TR(L"共 %lld 条消息"), (long long)total);
    }
    return buf;
}

std::wstring BuildHistoryFooterText(int64_t total, int currentPage, int pageCount,
                                    bool hasUnloadedOlder, bool fetchingOlder)
{
    wchar_t buf[kHistoryTextBufLen] = { 0 };
    swprintf_s(buf, kHistoryTextBufLen, hasUnloadedOlder ? TR(L"已加载 %lld 条 · 第 %d/%d 页")
                                                         : TR(L"共 %lld 条 · 第 %d/%d 页"),
               (long long)total, currentPage, pageCount);
    std::wstring text = buf;
    if (fetchingOlder)
    {
        text += TR(L" · 正在加载更早的消息…");
    }
    return text;
}

} // namespace balloonhistory
