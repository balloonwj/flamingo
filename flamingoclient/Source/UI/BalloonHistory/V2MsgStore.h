/**
 *  v2 历史消息本地库——SQLite 读写封装，V2MsgStore.h
 *
 *  CV2MsgStore 把实时收到 / 同步回填的 v2 消息按 (ctype,cid,msgid) 去重落进本地
 *  SQLite（每个登录用户一个 ChatLog\msgv2.db 文件），供“历史消息记录窗口”按类型 /
 *  时间 / 关键字过滤 + 分页查询。所有 SQL 拼装等纯逻辑放在 V2MsgQuery 便于单测；
 *  本类只做数据库读写与并发自保护。对应数据库读写用例见
 *  flamingoclient/Source/Tests/V2MsgStoreTests.cpp。
 *
 *  balloonwj@qq.com   2026-06-09
 */
#pragma once

#include <string>
#include <vector>
#include <mutex>
#include "V2MsgQuery.h"
#include "HistoryCoverage.h"   //CoverRange：覆盖区间的读写接口用

//前置声明 SQLite 连接句柄类型，避免在本头里包含 sqlite3.h。
struct sqlite3;

namespace balloonhistory {

//对账用的一条 Pin 记录：哪条消息、什么时候被钉的。
//两个字段都来自服务端 cmd_get_pinned_messages 应答里每条消息挂的 pin 子对象 ——
//对账时把它们一并写入，本地的 Pin 时间才与服务端一致。
struct PinnedItem
{
    int64_t m_msgid;    //被Pin的消息msgid
    int64_t m_pinTime;  //Pin时间（Unix毫秒）

    PinnedItem()
        : m_msgid(0)
        , m_pinTime(0)
    {
    }
};

//v2 历史消息本地库。线程：本期在 UI 线程同步读写，内部以 std::mutex 自保护，
//为将来挪到 worker 线程留好接口。不可拷贝（持有连接句柄与互斥量）。
class CV2MsgStore
{
public:
    CV2MsgStore();
    ~CV2MsgStore();

    //打开（不存在则创建）数据库文件并建表 / 建索引；幂等，可重复调用。
    //@param dbPathUtf16  数据库全路径（UTF-16，含文件名）；其父目录须已存在
    //@return 打开并建表成功返回 true；已打开时直接返回 true
    bool Open(const wchar_t* dbPathUtf16);

    //关闭数据库（若已打开）。析构时自动调用。
    void Close();

    //是否已成功打开（粗判，用于决定是否需要 Open；不加锁）。
    bool IsOpen() const;

    //插入或更新一条消息（按主键 (ctype,cid,msgid) 去重，存在则覆盖各字段）。
    //@param rec  入库记录；m_msgid 须为有效服务器消息ID（>0）
    //@return 成功返回 true
    bool Upsert(const HistoryRecord& rec);

    //批量插入或更新一批消息，语义与按顺序逐条调用 Upsert 相同（批内同一条消息出现多次时，
    //后出现的覆盖先出现的），但整批放在<u>一个事务</u>里提交。
    //
    //为什么要有它：SQLite 默认每条语句自动提交一次，每次提交都要刷盘。历史应答一页 50 条，
    //逐条 Upsert 在 UI 线程上实测要 300 毫秒左右，聊天窗得等它写完才开始渲染；放进一个
    //事务只提交一次，耗时降到毫秒级。
    //
    //任何一条写入失败时整批回滚，库保持调用前的状态。
    //@param recs  入库记录；m_msgid 非正的记录（服务器尚未分配 msgid）被跳过，不算失败
    //@return 整批提交成功返回 true（空批次直接返回 true）；未打开数据库或写入失败返回 false
    bool UpsertBatch(const std::vector<HistoryRecord>& recs);

    //把一条消息标记为已撤回（recalled 置 1）。标记后该条即被 Count / QueryPage 排除，
    //从而不再出现在"消息记录"窗口里。收到服务端 cmd_message_recalled_notify 推送后调用。
    //本方法只改标记、不删行：保留行是为了后续同步 / 去重时仍能识别这条 msgid 已处理过。
    //@param ctype  会话类型（1 单聊 2 群聊），与入库时一致
    //@param cid    会话ID（单聊=对端userid，群聊=群id），与入库时一致
    //@param msgid  被撤回消息的服务器msgid（须 >0）
    //@return 语句执行成功返回 true（<u>包括</u>本地压根没有这条消息、影响 0 行的情况——
    //        历史未同步到本地是正常现象，不算失败）；未打开数据库或参数非法返回 false
    bool MarkRecalled(int ctype, int64_t cid, int64_t msgid);

    //设置 / 清除一条消息的 Pin 标记。收到服务端 cmd_message_pinned_notify 推送后调用。
    //Pin 是会话内共享的状态，本地库只是它的一份缓存，故本方法只负责落标记，不做任何判断。
    //@param ctype      会话类型（1 单聊 2 群聊），与入库时一致
    //@param cid        会话ID（单聊=对端userid，群聊=群id），与入库时一致
    //@param msgid      目标消息的服务器msgid（须 >0）
    //@param pinned     true 钉住、false 取消
    //@param pinTimeMs  Pin时间（Unix毫秒）；pinned 为 false 时本参数被忽略、一律写 0
    //@param outHit     [出参] 本次 UPDATE <u>是否命中了本地的行</u>。本地没有这条消息时
    //                  为 false —— 这不是失败（本机没同步过该历史属正常），但调用方
    //                  据它决定要不要向服务端补拉一次消息体，否则用户会看到"明明钉了
    //                  却在 Pin 分类里找不到"。详见 docs/消息Pin接入方案.md §4.3
    //@return 语句执行成功返回 true（含命中 0 行）；未打开数据库或 msgid 非正返回 false
    bool SetPinned(int ctype, int64_t cid, int64_t msgid, bool pinned,
                   int64_t pinTimeMs, bool& outHit);

    //把某会话的 Pin 集合<u>整体对齐</u>成给定的这一份（与服务端对账用）。
    //在一个事务里做两步：先把该会话中 timestamp 不早于 minTimestampMs 的行 pinned 与
    //pin_time 一起清零，再把 items 里的各条按其 m_pinTime 置为已 Pin。
    //
    //为什么连 Pin 时间一起对齐、而不是只对齐"钉没钉住"：清零那一步必须把 pin_time 一并
    //归零，否则会留下"没钉住却有 Pin 时间"的半截状态；既然如此，置位时就得把正确的时间
    //写回去。这两个值本来就一起来自服务端应答里每条消息挂的 pin 子对象，分开处理反而
    //要多绕一圈。
    //
    //为什么需要 minTimestampMs 这个下界：服务端一次最多返回最近 N 条被 Pin 的消息
    //（PIN_FETCH_LIMIT），会话里钉得多时应答会被截断。此时那些"更老的 Pin"并不在
    //items 里，无条件全量清零会把它们错杀。调用方的规则是——未截断（应答的 total
    //等于返回条数）传 0 表示全量清零；已截断则传<u>返回集合里最老一条消息的时间戳</u>，
    //只清这个时间点之后的，更早的一律不动。
    //@param ctype           会话类型（1 单聊 2 群聊）
    //@param cid             会话ID（我方视角）
    //@param items           本次应置为已 Pin 的消息及其 Pin 时间；可为空（该会话没有 Pin）。
    //                       m_msgid 非正的项会被跳过
    //@param minTimestampMs  清零的时间下界（含）；0 表示全量清零
    //@return 事务执行成功返回 true；未打开数据库返回 false
    bool ReplacePinnedSet(int ctype, int64_t cid, const std::vector<PinnedItem>& items,
                          int64_t minTimestampMs);

    //按 msgid 删除某会话里的若干条消息。收到服务端"本人删除了消息"推送（单条形式）后调用。
    //真删行，而不是像撤回那样只打标记：服务端已把这些消息对本人过滤掉，之后再同步不会把它们
    //带回来，保留行没有去重上的意义。整批在一个事务里完成。
    //@param ctype       会话类型（1 单聊 2 群聊），与入库时一致
    //@param cid         会话ID（单聊=对端userid，群聊=群id），与入库时一致
    //@param msgids      要删除的消息msgid；非正的项跳过
    //@param outDeleted  [出参] 实际删除的行数；本地本来就没有的 msgid 不计入
    //@return 执行成功返回 true（<u>包括</u>一行都没删到的情况 —— 本机没同步过这些消息属正常）；
    //        未打开数据库返回 false
    bool DeleteMessages(int ctype, int64_t cid, const std::vector<int64_t>& msgids, int& outDeleted);

    //删除某会话的全部消息与它的覆盖区间记录。本人退出群聊、被移出或群被解散后调用：之后已没有
    //入口查看这个群的记录，留着只会让全局搜索搜出一个打不开的群；日后重新入群时再从服务端同步。
    //消息与覆盖区间在一个事务里删除，走 (ctype,cid,timestamp) 索引，不扫全表。
    //@param ctype       会话类型（1 单聊 2 群聊）
    //@param cid         会话ID（单聊=对端userid，群聊=群id）
    //@param outDeleted  [出参] 删除的消息行数；失败时为 0
    //@return 执行成功返回 true（包括本来就没有这个会话的数据）；未打开数据库或出错返回 false，
    //        出错时整体回滚
    bool DeleteConversation(int ctype, int64_t cid, int& outDeleted);

    //按时间范围删除<u>全部会话</u>里的消息，一次最多删 batchSize 行。收到服务端"本人删除了消息"
    //推送（区间形式）后，由调用方逐批投递消息反复调用，直到 outDeleted 为 0 —— 每批之间让出
    //UI 线程，本地库再大也不会让界面长时间冻结（见 docs/清理聊天记录方案.md §7.1）。
    //@param beginMs     区间开始，Unix毫秒（含）
    //@param endMs       区间结束，Unix毫秒（含）
    //@param batchSize   本批最多删除的行数；<=0 时不删任何行
    //@param outDeleted  [出参] 本批实际删除的行数；为 0 表示区间内已经没有剩余的行
    //@return 执行成功返回 true；未打开数据库返回 false
    bool DeleteByTimeRangeBatch(int64_t beginMs, int64_t endMs, int batchSize, int& outDeleted);

    //读取某会话的覆盖区间（规则见 HistoryCoverage.h）。
    //@param ctype  会话类型（1 单聊 2 群聊）
    //@param cid    会话ID
    //@param out    [出参] 覆盖区间；没有记录时 m_valid 为 false
    //@return 查询成功返回 true（含没有记录）；未打开数据库或出错返回 false
    bool GetCoverage(int ctype, int64_t cid, CoverRange& out);

    //写入某会话的覆盖区间，已有则覆盖。cover.m_valid 为 false 时删除该会话的记录。
    //@return 执行成功返回 true；未打开数据库返回 false
    bool SetCoverage(int ctype, int64_t cid, const CoverRange& cover);

    //清空全部会话的覆盖区间。每次登录成功后调用：离线期间在别的设备上删掉的消息本机
    //收不到推送，之前验证过"完整"的区间可能已经不准，清空后各会话从最新一页重新验证。
    //本地的消息行都保留（用于去重与 Pin），只是重新验证之前不在消息记录窗里显示。
    //@return 执行成功返回 true；未打开数据库返回 false
    bool ClearAllCoverage();

    //取某会话里 msgid 落在 [loMsgid, hiMsgid] 内的全部 msgid，供对账使用。
    //@param out  [出参] msgid 列表（顺序不限）；调用前会被清空
    //@return 查询成功返回 true；未打开数据库或出错返回 false
    bool QueryMsgidsInRange(int ctype, int64_t cid, int64_t loMsgid, int64_t hiMsgid,
                            std::vector<int64_t>& out);

    //取某会话里 msgid 不小于 minMsgid 的消息中最早的时间。消息记录窗据此判断覆盖区间是否
    //已经盖住了时间筛选的下界（HistoryCoverage.h 的 ShouldFetchOlder），盖住了就不必再向
    //服务端往前拉。已撤回的行也算在内 —— 它们同样是服务端返回过的消息。
    //@param minMsgid        msgid 下界（含）
    //@param outTimestampMs  [出参] 最早的时间（Unix毫秒）；outFound 为 false 时无意义
    //@param outFound        [出参] 是否存在这样的行
    //@return 查询成功返回 true（含没有这样的行）；未打开数据库或出错返回 false
    bool QueryOldestTimestampFrom(int ctype, int64_t cid, int64_t minMsgid,
                                  int64_t& outTimestampMs, bool& outFound);

    //取某会话里发过言的人，供消息记录窗口的「发送人」下拉框使用。不含已撤回的消息，
    //也不含 fromid 不为正数的行（没有发送人的系统消息）。
    //@param ctype     会话类型（1单聊 2群聊）
    //@param cid       会话ID（我方视角）
    //@param minMsgid  显示下界（覆盖区间，见 HistoryCoverage.h）；只统计 msgid 不小于它的消息，0 表示不限
    //@param out       [出参] 发送人 userid，按各自最近一条消息的时间倒序（最近发过言的在前）；调用前会被清空
    //@return 查询成功返回 true（即使一个人都没有）；未打开数据库或出错返回 false
    bool QuerySenders(int ctype, int64_t cid, int64_t minMsgid, std::vector<int64_t>& out);

    //按过滤条件统计匹配条数（恒排除已撤回）。
    //@param filter  过滤条件
    //@param nowMs   当前时间（Unix毫秒），供“近N天”换算
    //@return 匹配条数；未打开或出错返回 0
    int64_t Count(const HistoryFilter& filter, int64_t nowMs);

    //按过滤条件统计"排在某条消息前面的有几条"（排序同 QueryPage，恒排除已撤回）。用来算
    //某条消息、或某个时间点在第几页（查看上下文、跳到某一天）。
    //@param filter     过滤条件
    //@param nowMs      当前时间（Unix毫秒）
    //@param timestamp  那条消息的时间（Unix毫秒）
    //@param msgid      那条消息的 msgid；传 INT64_MAX 时统计时间晚于 timestamp 的条数
    //@param out        [出参] 条数
    //@return 查询成功返回 true；未打开数据库或出错返回 false
    bool CountBefore(const HistoryFilter& filter, int64_t nowMs, int64_t timestamp, int64_t msgid,
                     int64_t& out);

    //按过滤条件分页查询（时间倒序，恒排除已撤回）。
    //@param filter  过滤条件（含页号 / 页大小）
    //@param nowMs   当前时间（Unix毫秒）
    //@param out     [出参] 本页记录（最新在前）；调用前会被清空
    //@return 查询成功返回 true（即使 0 条也算成功）；未打开或出错返回 false
    bool QueryPage(const HistoryFilter& filter, int64_t nowMs, std::vector<HistoryRecord>& out);

    //取某会话最近的 limit 条消息，供打开聊天窗时先用本地数据显示。与 QueryPage 不同：
    //按 msgid 取（与服务端同步应答的取法一致）、<u>含</u>已撤回的消息（服务端同步应答同样会
    //返回它们），并带出原始 JSON（HistoryRecord::m_raw，老数据为空串）。
    //@param ctype  会话类型（1单聊 2群聊）
    //@param cid    会话ID（我方视角）
    //@param limit  最多取几条，须大于 0
    //@param out    [出参] 结果按 msgid 升序（最旧在前，与同步应答相同）；调用前会被清空
    //@return 查询成功返回 true（即使 0 条也算成功）；未打开数据库、limit 非法或出错返回 false
    bool QueryLatest(int ctype, int64_t cid, int limit, std::vector<HistoryRecord>& out);

private:
    //执行无结果集 SQL（假定已持锁、m_db 有效）；成功返回 true。
    bool ExecLocked(const char* sql);

    //给存量库补上缺失的列（假定已持锁、m_db 有效）。用 PRAGMA table_info 读回现有列名，
    //缺哪列就 ALTER TABLE ADD COLUMN。须在建表<u>之后</u>、建索引<u>之前</u>调用：
    //建表语句是 IF NOT EXISTS 形式，对已存在的库整条跳过，新加的列只能靠本方法补；
    //而索引可能用到新列，先建索引会报 no such column。
    void EnsureColumnsLocked();

    CV2MsgStore(const CV2MsgStore&);
    CV2MsgStore& operator=(const CV2MsgStore&);

private:
    sqlite3*   m_db;     //SQLite 连接句柄；NULL=未打开
    std::mutex m_mutex;  //保护 m_db 的并发读写（本期 UI 线程，留作将来异步）
};

} // namespace balloonhistory
