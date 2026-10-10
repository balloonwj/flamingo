/**
 *  聊天记录管理器通配符搜索与时间轴模式的耗时基准（Suite WCB），HistoryWildcardBenchTests.cpp
 *
 *  回答「通配符搜索在消息多的时候慢不慢」（docs/聊天记录管理器方案.md §11.4、§11.7）。标签带 [.]，常规运行不执行，
 *  需要时单独运行：FlamingoUnit.exe "[wildcardbench]"。只打印耗时，不按耗时判失败（机器负载不同，数字会差很多）；
 *  断言只核对命中条数，保证测的是正确的查询。
 *
 *  数据（一个临时库，合计 30 万条）：
 *    · 大会话：单聊 cid=10，10 万条，时间跨度约 3 年。每 100 条有一条「测试环境的数据库我重置了一下…」，
 *      每 50 条有一条文件消息「季度报告-附件-vN.pdf」，每 1000 条有一条 2000 个字母 a 的长正文（专门给
 *      「a*a*a*…*b」这种多个星号、又匹配不上的模式用，观察 SQLite LIKE 的回溯代价）；
 *    · 400 个小会话（单聊、群聊交替），各 500 条，命中与文件消息的间隔同上。
 *
 *  测量项：
 *    WCB1 右侧会话内搜索（大会话，Count + 取第一页，在界面线程上执行）；
 *    WCB2 左栏跨会话搜索（SearchConversationsForManager，在后台线程上执行）；关键字是否走全文索引一并打印；
 *    WCB3 文件页的文件名搜索（CountLibrary + QueryLibraryPage，在界面线程上执行）；
 *    WCB4 时间轴模式一次完整刷新的几项统计（大会话：列表总数、各月条数、各月命中数、命中位置、当前页各天条数）；
 *    WCB5 高亮（FindWildcardHighlightRanges）：一页 30 行的常见文字，以及 2000 字的长文字配多星号的模式。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-05
 */
#include "catch_amalgamated.hpp"
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include "../UI/BalloonHistory/HistoryWildcard.h"
#include "../UI/BalloonHistory/HistoryLibraryQuery.h"
#include "../UI/BalloonHistory/HistoryTimeline.h"
#include "../UI/BalloonHistory/V2MsgQuery.h"
#include "../UI/BalloonHistory/V2MsgStore.h"

using namespace balloonhistory;

namespace {

//会话类型与消息类型（与 net/Protocol.h 一致）
const int kSingle = 1;
const int kGroup = 2;
const int kTypeText = 1;
const int kTypeFile = 3;
//大会话
const int64_t kBigCid = 10;
const int kBigRows = 100000;
//大会话相邻两条消息的时间间隔（毫秒）：约 15.8 分钟，10 万条跨约 3 年，导航条上约 36 个月
const int64_t kBigStepMs = 946000;
//小会话的个数与每个的条数
const int kSmallConversations = 400;
const int kSmallPerConversation = 500;
const int64_t kSmallCidBase = 1000;
//小会话相邻两条消息的时间间隔（毫秒）
const int64_t kSmallStepMs = 1000;
//每多少条放一条命中消息、一条文件消息、一条长正文
const int kHitEvery = 100;
const int kFileEvery = 50;
const int kLongEvery = 1000;
//长正文的字母个数
const int kLongTextChars = 2000;
//第一条消息的时间：2023-11-14 前后
const int64_t kBaseTs = 1700000000000LL;
//统计用的「当前时间」：数据之后（「近 N 天」不参与，本基准只用全部时间）
const int64_t kNowMs = 1800000000000LL;
//每批写入多少条（UpsertBatch 一个事务）
const size_t kBatch = 5000;
//每种查询重复几次取平均
const int kRepeat = 3;
//列表每页条数（消息记录窗 30、文件页 50）与跨会话搜索的会话上限
const int kHistoryPage = 30;
const int kLibraryPage = 50;
const int kManagerLimit = 50;

//命中消息与普通消息的正文
const char* const kHitText = "测试环境的数据库我重置了一下，账号还是原来那几个。";
const char* const kPhrases[] =
{
    "明天上午十点开周会，记得带上本周的进度",
    "收到，我晚点把材料整理好发到群里",
    "这个问题我跟一下，下午给你答复",
    "接口文档已经更新到最新版本了",
    "这一版的改动比较多，主要集中在消息列表和输入区两块，大家回归的时候重点看一下。",
};
const int kPhraseCount = (int)(sizeof(kPhrases) / sizeof(kPhrases[0]));

//一种关键字写法
struct BenchPattern
{
    const char* m_keyword;    //关键字（UTF-8）
    bool        m_wildcard;   //是否按通配符解释
    const char* m_note;       //打印时的说明
};

//当前时刻（毫秒，带小数），基于高精度计数器
double BenchNowMs()
{
    LARGE_INTEGER freq;
    LARGE_INTEGER now;
    ::QueryPerformanceFrequency(&freq);
    ::QueryPerformanceCounter(&now);
    return (double)now.QuadPart * 1000.0 / (double)freq.QuadPart;
}

//取一个临时目录下的 db 全路径，并先删掉同名旧文件与 WAL 残留（文件名带进程号）
std::wstring FreshBenchDb(const wchar_t* name)
{
    wchar_t dir[MAX_PATH] = { 0 };
    ::GetTempPathW(MAX_PATH, dir);
    wchar_t prefix[32] = { 0 };
    ::swprintf_s(prefix, L"%u_", static_cast<unsigned int>(::GetCurrentProcessId()));
    std::wstring path = dir;
    path += prefix;
    path += name;
    ::DeleteFileW(path.c_str());
    ::DeleteFileW((path + L"-wal").c_str());
    ::DeleteFileW((path + L"-shm").c_str());
    return path;
}

//构造一条消息：文件消息的内容 JSON 与 BuildContentJson 的格式相同，搜索文本是文件名
HistoryRecord BenchRecord(int64_t msgid, int ctype, int64_t cid, int msgtype, int64_t ts, const std::string& text)
{
    HistoryRecord rec;
    rec.m_msgid = msgid;
    rec.m_ctype = ctype;
    rec.m_cid = cid;
    rec.m_fromid = cid;
    rec.m_msgtype = msgtype;
    rec.m_timestamp = ts;
    rec.m_searchText = text;
    if (msgtype == kTypeFile)
    {
        rec.m_content = "{\"file\":{\"fileid\":\"F" + std::to_string((long long)msgid) + "\",\"name\":\"" + text
                      + "\",\"size\":\"4096\",\"md5\":\"m\"}}";
    }
    else
    {
        rec.m_content = "{}";
    }
    return rec;
}

//第 i 条消息（从 0 起）的类型与正文：按固定间隔放命中消息、文件消息、长正文，其余从几句话里轮换
void PickContent(int i, const std::string& longText, int& msgtype, std::string& text)
{
    msgtype = kTypeText;
    if (i % kHitEvery == 0)
    {
        text = kHitText;
    }
    else if (i % kFileEvery == 2)
    {
        msgtype = kTypeFile;
        text = "季度报告-附件-v" + std::to_string((long long)(i % kHitEvery)) + ".pdf";
    }
    else if (i % kLongEvery == 1)
    {
        text = longText;
    }
    else
    {
        text = kPhrases[i % kPhraseCount];
    }
}

//造数据：大会话 10 万条 + 400 个小会话各 500 条
void SeedBenchStore(CV2MsgStore& store)
{
    const std::string longText(kLongTextChars, 'a');
    const double t0 = BenchNowMs();
    std::vector<HistoryRecord> batch;
    batch.reserve(kBatch);
    int64_t msgid = 1;
    for (int i = 0; i < kBigRows; ++i)
    {
        int msgtype = kTypeText;
        std::string text;
        PickContent(i, longText, msgtype, text);
        batch.push_back(BenchRecord(msgid++, kSingle, kBigCid, msgtype, kBaseTs + (int64_t)i * kBigStepMs, text));
        if (batch.size() >= kBatch)
        {
            REQUIRE(store.UpsertBatch(batch));
            batch.clear();
        }
    }
    for (int c = 0; c < kSmallConversations; ++c)
    {
        const int ctype = (c % 2 == 0) ? kSingle : kGroup;
        const int64_t cid = kSmallCidBase + c;
        for (int i = 0; i < kSmallPerConversation; ++i)
        {
            int msgtype = kTypeText;
            std::string text;
            PickContent(i, longText, msgtype, text);
            batch.push_back(BenchRecord(msgid, ctype, cid, msgtype, kBaseTs + msgid * kSmallStepMs, text));
            ++msgid;
            if (batch.size() >= kBatch)
            {
                REQUIRE(store.UpsertBatch(batch));
                batch.clear();
            }
        }
    }
    if (!batch.empty())
    {
        REQUIRE(store.UpsertBatch(batch));
    }
    std::printf("WCB seed %lld rows: %.0f ms, full text ready %d\n", (long long)(msgid - 1), BenchNowMs() - t0,
                (int)store.IsFullTextReady());
}

//关键字在跨会话搜索里能否走全文索引（与 CV2MsgStore::UseFullTextLocked 同一判断，另需索引已就绪）
bool CanUseFullText(const BenchPattern& p)
{
    if (p.m_wildcard)
    {
        std::string query;
        return BuildWildcardFtsQuery(p.m_keyword, query);
    }
    return CanUseTrigramSearch(p.m_keyword);
}

//本基准的各种关键字写法
const BenchPattern kPatterns[] =
{
    { "测试环境",                  false, "plain, 4 chars" },
    { "测试",                      false, "plain, 2 chars" },
    { "测试*环境",                 true,  "wildcard, 2+2 chars" },
    { "测试环境*数据库",           true,  "wildcard, 4+3 chars" },
    { "测?环境",                   true,  "wildcard with ?, 1+2 chars" },
    { "*测*试*环*境*数*据*库*",    true,  "wildcard, 7 stars, 1 char each" },
    { "a*a*a*a*a*a*a*a*b",         true,  "wildcard, 8 stars, no match on 2000-char text" },
    { "一条都不会命中的关键字",    false, "plain, no match" },
};
const int kPatternCount = (int)(sizeof(kPatterns) / sizeof(kPatterns[0]));

} // namespace

TEST_CASE("WCB 通配符搜索与时间轴统计在 30 万条记录下的耗时（基准，不参与常规运行）", "[.][wildcardbench]")
{
    CV2MsgStore store;
    REQUIRE(store.Open(FreshBenchDb(L"flamingo_wcb.db").c_str()));
    SeedBenchStore(store);

    //WCB1 右侧会话内搜索：大会话里 Count + 取第一页
    for (int p = 0; p < kPatternCount; ++p)
    {
        HistoryFilter f;
        f.m_ctype = kSingle;
        f.m_cid = kBigCid;
        f.m_keyword = kPatterns[p].m_keyword;
        f.m_wildcard = kPatterns[p].m_wildcard;
        f.m_pageSize = kHistoryPage;
        double countMs = 0.0;
        double pageMs = 0.0;
        int64_t total = 0;
        std::vector<HistoryRecord> page;
        for (int r = 0; r < kRepeat; ++r)
        {
            const double t0 = BenchNowMs();
            total = store.Count(f, kNowMs);
            const double t1 = BenchNowMs();
            REQUIRE(store.QueryPage(f, kNowMs, page));
            countMs += t1 - t0;
            pageMs += BenchNowMs() - t1;
        }
        std::printf("WCB1 session search [%s] \"%s\": total %lld, count %.1f ms, first page %.1f ms\n",
                    kPatterns[p].m_note, kPatterns[p].m_keyword, (long long)total, countMs / kRepeat,
                    pageMs / kRepeat);
        //命中「测试环境」的条数是固定的，几种等价写法应当得到同一个数
        if (p == 0 || p == 2 || p == 3 || p == 4 || p == 5)
        {
            CHECK(total == kBigRows / kHitEvery);
        }
    }

    //WCB2 左栏跨会话搜索（全部会话，最多 50 个会话）
    for (int p = 0; p < kPatternCount; ++p)
    {
        std::vector<ConversationHit> hits;
        double total = 0.0;
        for (int r = 0; r < kRepeat; ++r)
        {
            const double t0 = BenchNowMs();
            REQUIRE(store.SearchConversationsForManager(kPatterns[p].m_keyword, kManagerLimit,
                                                        std::vector<HistoryConversationKey>(), hits,
                                                        kPatterns[p].m_wildcard));
            total += BenchNowMs() - t0;
        }
        std::printf("WCB2 manager search [%s] \"%s\": full text %d, %u conversations, avg %.1f ms\n",
                    kPatterns[p].m_note, kPatterns[p].m_keyword, (int)CanUseFullText(kPatterns[p]),
                    (unsigned)hits.size(), total / kRepeat);
    }

    //WCB3 文件页的文件名搜索（全部会话，第一页）
    const BenchPattern kFilePatterns[] =
    {
        { "",              false, "no keyword" },
        { "报告",          false, "plain, 2 chars" },
        { "季度报告",      false, "plain, 4 chars" },
        { "季度*附件",     true,  "wildcard, 2+2 chars" },
        { "附件-v5?.pdf",  true,  "wildcard with ?" },
        { "不存在的文件名", false, "plain, no match" },
    };
    for (size_t p = 0; p < sizeof(kFilePatterns) / sizeof(kFilePatterns[0]); ++p)
    {
        HistoryLibraryFilter f;
        f.m_kind = kHistoryLibraryFiles;
        f.m_keyword = kFilePatterns[p].m_keyword;
        f.m_wildcard = kFilePatterns[p].m_wildcard;
        f.m_pageSize = kLibraryPage;
        double countMs = 0.0;
        double pageMs = 0.0;
        int64_t total = 0;
        std::vector<HistoryLibraryItem> items;
        for (int r = 0; r < kRepeat; ++r)
        {
            const double t0 = BenchNowMs();
            REQUIRE(store.CountLibrary(f, kNowMs, total));
            const double t1 = BenchNowMs();
            REQUIRE(store.QueryLibraryPage(f, kNowMs, items));
            countMs += t1 - t0;
            pageMs += BenchNowMs() - t1;
        }
        std::printf("WCB3 file search [%s] \"%s\": total %lld, count %.1f ms, first page %.1f ms\n",
                    kFilePatterns[p].m_note, kFilePatterns[p].m_keyword, (long long)total, countMs / kRepeat,
                    pageMs / kRepeat);
    }

    //WCB4 时间轴模式一次完整刷新的几项统计（大会话）
    const BenchPattern kTimelinePatterns[] =
    {
        { "",          false, "no keyword" },
        { "测试环境",  false, "plain, 4 chars" },
        { "测试*环境", true,  "wildcard, 2+2 chars" },
    };
    for (size_t p = 0; p < sizeof(kTimelinePatterns) / sizeof(kTimelinePatterns[0]); ++p)
    {
        HistoryFilter list;
        list.m_ctype = kSingle;
        list.m_cid = kBigCid;
        list.m_pageSize = kHistoryPage;
        HistoryFilter full = list;
        full.m_keyword = kTimelinePatterns[p].m_keyword;
        full.m_wildcard = kTimelinePatterns[p].m_wildcard;
        const bool hasKeyword = HasKeywordFilter(full);
        double totalMs = 0.0;
        double monthMs = 0.0;
        double hitMonthMs = 0.0;
        double positionsMs = 0.0;
        double dayMs = 0.0;
        size_t months = 0;
        size_t positions = 0;
        for (int r = 0; r < kRepeat; ++r)
        {
            //列表总数（不含关键字）
            double t0 = BenchNowMs();
            const int64_t total = store.Count(list, kNowMs);
            totalMs += BenchNowMs() - t0;
            CHECK(total == kBigRows);
            //各月条数
            std::vector<HistoryPeriodCount> counts;
            t0 = BenchNowMs();
            REQUIRE(store.CountByLocalMonth(list, kNowMs, counts));
            monthMs += BenchNowMs() - t0;
            months = counts.size();
            if (hasKeyword)
            {
                //各月命中数与命中位置
                std::vector<HistoryPeriodCount> hits;
                t0 = BenchNowMs();
                REQUIRE(store.CountByLocalMonth(full, kNowMs, hits));
                hitMonthMs += BenchNowMs() - t0;
                std::vector<HistoryPosition> pos;
                t0 = BenchNowMs();
                REQUIRE(store.QueryPositions(full, kNowMs, kTimelineMaxHits + 1, pos));
                positionsMs += BenchNowMs() - t0;
                positions = pos.size();
            }
            //当前页各天条数：第一页大约跨两天
            std::vector<HistoryPeriodCount> days;
            const int64_t newest = kBaseTs + (int64_t)(kBigRows - 1) * kBigStepMs;
            t0 = BenchNowMs();
            REQUIRE(store.CountByLocalDay(list, kNowMs, newest - (int64_t)kHistoryPage * kBigStepMs, newest, days));
            dayMs += BenchNowMs() - t0;
        }
        std::printf("WCB4 timeline [%s] \"%s\": months %u, positions %u; list count %.1f ms, month counts %.1f ms, "
                    "month hits %.1f ms, hit positions %.1f ms, page day counts %.1f ms, sum %.1f ms\n",
                    kTimelinePatterns[p].m_note, kTimelinePatterns[p].m_keyword, (unsigned)months,
                    (unsigned)positions, totalMs / kRepeat, monthMs / kRepeat, hitMonthMs / kRepeat,
                    positionsMs / kRepeat, dayMs / kRepeat,
                    (totalMs + monthMs + hitMonthMs + positionsMs + dayMs) / kRepeat);
    }

    //WCB5 高亮：一页 30 行的常见文字，与 2000 字的长文字配多星号的模式
    {
        const int kHighlightLoops = 100;
        const std::wstring hitLine = L"测试环境的数据库我重置了一下，账号还是原来那几个。这一版的改动比较多。";
        const std::wstring longLine(kLongTextChars, L'a');
        const wchar_t* const kHighlightPatterns[] = { L"测试*环境", L"*测*试*环*境*数*据*库*", L"a*a*a*a*a*a*a*a*b" };
        for (size_t p = 0; p < sizeof(kHighlightPatterns) / sizeof(kHighlightPatterns[0]); ++p)
        {
            size_t ranges = 0;
            const double t0 = BenchNowMs();
            for (int loop = 0; loop < kHighlightLoops; ++loop)
            {
                for (int row = 0; row < kHistoryPage; ++row)
                {
                    ranges += FindWildcardHighlightRanges(hitLine, kHighlightPatterns[p]).size();
                }
            }
            const double pageMs = (BenchNowMs() - t0) / kHighlightLoops;
            const double t1 = BenchNowMs();
            for (int loop = 0; loop < kHighlightLoops; ++loop)
            {
                ranges += FindWildcardHighlightRanges(longLine, kHighlightPatterns[p]).size();
            }
            const double longMs = (BenchNowMs() - t1) / kHighlightLoops;
            //宽字符的中文模式不经 printf 输出（C 运行库默认区域设置下会中断输出），只打印序号
            std::printf("WCB5 highlight pattern #%u: 30 short rows %.3f ms per page, one 2000-char row %.3f ms "
                        "(ranges %u)\n", (unsigned)p, pageMs, longMs, (unsigned)ranges);
        }
    }
}
