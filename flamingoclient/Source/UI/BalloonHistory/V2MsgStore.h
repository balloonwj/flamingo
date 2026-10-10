/**
 *  v2 历史消息本地库——SQLite 读写封装，V2MsgStore.h
 *
 *  CV2MsgStore 把实时收到 / 同步回填的 v2 消息按 (ctype,cid,msgid) 去重落进本地
 *  SQLite（每个登录用户一个 ChatLog\msgv2.db 文件），供“历史消息记录窗口”按类型 /
 *  时间 / 关键字过滤 + 分页查询。所有 SQL 拼装等纯逻辑放在 V2MsgQuery 便于单测；
 *  本类只做数据库读写与并发自保护。对应数据库读写用例见
 *  flamingoclient/Source/Tests/V2MsgStoreTests.cpp。
 *
 *  全文索引（2026-10-04）：t_msg_fts 是 search_text 列的 FTS5 全文索引（trigram 分词、不另存
 *  文字，按 rowid 对应 t_msg），三个触发器在 t_msg 增、删、改 search_text 时同步它。存量库第一次
 *  打开时建出空索引，由后台查询线程调用 BackfillFullTextStep 分批补齐；补齐之前，以及关键字少于
 *  三个字时，跨会话搜索仍整表 LIKE 扫描。全文索引只用来缩小候选行，结果仍按 LIKE 复核，与只用
 *  LIKE 时完全相同。用例见 Tests/FullTextSearchTests.cpp（Suite FTS）。
 *
 *  balloonwj@qq.com   2026-06-09
 */
#pragma once

#include <string>
#include <set>
#include <vector>
#include <mutex>
#include <atomic>
#include "V2MsgQuery.h"
#include "HistoryCoverage.h"   //CoverRange：覆盖区间的读写接口用
#include "HistoryLibraryQuery.h"   //文件页与图片页的查询条件、结果行与群文件缓存条目
#include "HistoryStorageStats.h"   //HistoryCacheRef：本机存储占用页的缓存引用

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

//撤回快照表 t_recall_draft 的一行（docs/撤回后重新编辑方案.md §3.1）：本人撤回一条文字类消息的那一刻，把原文
//存成「重新编辑」用的快照，撤回提示行据此在 5 分钟内显示「编辑」。
struct RecallDraftRow
{
    int64_t     m_msgid;          //被撤回消息的服务器 msgid
    int64_t     m_recallTimeMs;   //撤回推送到达本机的时间（Unix 毫秒，本机时钟）
    std::string m_payload;        //快照 JSON（UTF-8，格式见 balloonchatwnd::SerializeRecallDraft）

    RecallDraftRow()
        : m_msgid(0)
        , m_recallTimeMs(0)
    {
    }
};

//v2 历史消息本地库。线程：一个实例对应一条数据库连接，内部以 std::mutex 自保护，可在多个
//线程上调用。库以 WAL 模式打开，同一个库文件可以再开一个实例（另一条连接）给后台线程只读
//查询：读与写互不阻塞，界面线程写库时不必等后台查询结束（CV2MsgSearchWorker 就是这样用的）。
//不可拷贝（持有连接句柄与互斥量）。
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

    //打开时传入的数据库路径；未打开时为空串。后台查询线程据它另开一条连接。
    std::wstring DbPath();

    //全文索引是否可用于跨会话搜索：索引表已建好，且存量数据已回填完毕。为 false 时三个跨会话
    //搜索都整表 LIKE 扫描（结果相同，只是慢）。
    bool IsFullTextReady();

    //回填一批存量数据进全文索引（由后台查询线程在空闲时反复调用，直到 IsFullTextReady 为 true）。
    //按 rowid 区间推进，每次处理 maxRows 个 rowid，进度记在库里（t_fts_meta），程序退出后下次
    //接着回填。整批放在一个写事务里，执行期间别的连接写库要等它结束（一批约几十毫秒）。
    //@param maxRows  这一批覆盖多少个 rowid，须大于 0
    //@return 这一批写进索引的行数（区间里没有行时为 0）；已经回填完毕返回 0；未打开、没有全文
    //        索引（SQLite 未带 FTS5）或出错返回 -1
    int BackfillFullTextStep(int maxRows);

    //是否允许跨会话搜索使用全文索引（默认允许）。单元测试用它比对用与不用全文索引的结果；
    //全文索引出问题时也可以据此退回 LIKE。
    void SetFullTextAllowed(bool allowed);

    //请求中断这条连接上的查询：置位中断标志并调用 sqlite3_interrupt。正在执行的语句尽快以
    //SQLITE_INTERRUPT 结束，相应的查询方法返回 false。标志一直保持到 ClearInterrupt：请求发出时
    //语句还没开始执行（sqlite3_interrupt 对这种时刻不起作用），语句一开始执行就会被进度回调
    //发现并中止。可以在任意线程调用，<u>不加锁</u>（查询方法执行期间一直持有内部锁，加锁就要
    //等查询结束，中断也就失去意义）；调用方须保证调用期间本实例不会被 Close。
    void Interrupt();

    //清除中断标志。CV2MsgSearchWorker 在每次开始执行请求之前与执行结束之后调用，标志只作用于
    //那一次请求。
    void ClearInterrupt();

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

    /**
     *  把一条语音消息标为已听（played 置 1，docs/语音消息方案.md V7）。用户播放过之后调用；红点只记在本机。
     *  @param ctype 会话类型（1 单聊 2 群聊）
     *  @param cid   会话ID（单聊=对端userid，群聊=群id）
     *  @param msgid 语音消息的服务器msgid（须 >0）
     *  @return 语句执行成功返回 true（本地没有这条消息、影响 0 行也算成功）；未打开数据库或参数非法返回 false
     */
    bool MarkVoicePlayed(int ctype, int64_t cid, int64_t msgid);

    /**
     *  取一个会话里还没听过的语音消息（对方发来的、未撤回的）的 msgid，聊天窗据此给气泡画红点、挑下一条自动播放。
     *  @param ctype 会话类型（1 单聊 2 群聊）
     *  @param cid   会话ID
     *  @param out   [出参] msgid 列表，按 msgid 升序
     *  @return 查询成功返回 true；未打开数据库返回 false
     */
    bool LoadUnplayedVoiceIds(int ctype, int64_t cid, std::vector<int64_t>& out);

    /**
     *  在一批 msgid 里挑出本地库记为已听的语音（语音消息第 3d-3 步）。聊天窗收到或翻到语音时据此决定画不画红点：
     *  库里还没有的（翻历史时先显示后入库）不在结果里，由调用方按未听处理。
     *  @param ctype      会话类型（1 单聊 2 群聊）
     *  @param cid        会话ID
     *  @param candidates 要查的 msgid
     *  @param played     [出参] 其中库里有、并且 played 为 1 的 msgid；先清空再填
     *  @return 查询成功返回 true（candidates 为空也算成功）；未打开数据库返回 false
     */
    bool LoadPlayedVoiceIds(int ctype, int64_t cid, const std::vector<int64_t>& candidates, std::set<int64_t>& played);

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

    //按主键 (ctype, cid, msgid) 取一行完整记录（含已撤回的）。收到接龙变更推送时据此取出
    //本地那张卡片合并名单，打开接龙详情窗时据此取卡片内容。
    //@param ctype  会话类型（1 单聊 2 群聊），与入库时一致
    //@param cid    会话ID（单聊=对端userid，群聊=群id），与入库时一致
    //@param msgid  消息的服务器msgid（须 >0）
    //@param out    [出参] 找到时写入该行；没找到时不改动
    //@param found  [出参] 找到为 true；本地没有这条消息为 false（历史未同步到本地属正常）
    //@return 语句执行成功返回 true（无论是否找到）；未打开数据库、msgid 非正或语句失败返回 false
    bool QueryRecord(int ctype, int64_t cid, int64_t msgid, HistoryRecord& out, bool& found);

    //只更新一条消息的 content 列，其余列（raw、search_text、Pin、撤回等）不动。收到接龙变更
    //推送、把新名单与状态合并进卡片后调用（2026-10-02 与用户确认：原始JSON保持服务端发来的
    //原样，还原时以 content 列为准，见 V2MsgQuery.h 的 RelayContentFromRecord）。
    //@param ctype       会话类型，与入库时一致
    //@param cid         会话ID，与入库时一致
    //@param msgid       消息的服务器msgid（须 >0）
    //@param contentJson 新的 content 列（V2MsgQuery 的 BuildContentJson 产出的格式）
    //@param outHit      [出参] 是否命中了本地的行；本地没有这条消息时为 false（不算失败）
    //@return 语句执行成功返回 true（含命中 0 行）；未打开数据库或 msgid 非正返回 false
    bool UpdateContent(int ctype, int64_t cid, int64_t msgid, const std::string& contentJson, bool& outHit);

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

    //删除某会话里 msgid 不大于 maxMsgid 的消息，并删掉它的覆盖区间记录。收到"本人清空了某会话"的
    //推送（cmd_messages_deleted_notify 清空形式）后调用：锚点之后才到的消息要留下。覆盖区间一并作废，
    //下次打开消息记录窗时从服务端重新验证。消息与覆盖区间在一个事务里删除，走 (ctype,cid,timestamp) 索引。
    //@param ctype       会话类型（1 单聊 2 群聊）
    //@param cid         会话ID（单聊=对端userid，群聊=群id）
    //@param maxMsgid    清空锚点（含）；为 0 时不删任何消息，只作废覆盖区间
    //@param outDeleted  [出参] 删除的消息行数；失败时为 0
    //@return 执行成功返回 true；未打开数据库或出错返回 false，出错时整体回滚
    bool DeleteConversationUpTo(int ctype, int64_t cid, int64_t maxMsgid, int& outDeleted);

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

    //取本地库里所有会话中发过言的人（fromid 为正、未撤回），供聊天记录管理器跨会话搜索的 from:名字 按显示名
    //匹配（docs/聊天记录管理器方案.md §12.3）。要扫整张表，20 万条约几十毫秒，调用方应缓存结果。
    //@param out  [出参] 发送人 userid，升序、不重复；调用前会被清空
    //@return 查询成功返回 true（即使一个人都没有）；未打开数据库或出错返回 false
    bool QueryAllSenders(std::vector<int64_t>& out);

    //聊天记录管理器「本机存储占用」页（docs/聊天记录管理器方案.md §12.6）：取全部未撤回的图片、截图、文件、语音消息
    //引用的缓存（会话 + 类别 + md5，md5 从内容 JSON 里取，见 CacheRefFromContent）。不看覆盖区间：缓存是否在本机
    //与聊天记录是否连续无关。内容里取不出合法 md5 的行跳过。
    //@param out  [出参] 引用表，按 rowid 顺序；调用前会被清空
    //@return 查询成功返回 true；未打开数据库或出错返回 false
    bool QueryCacheRefs(std::vector<HistoryCacheRef>& out);

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

    //时间轴导航条：按本地时间的年月统计符合条件的条数（恒排除已撤回；关键字筛选生效时只数命中的）。
    //@param filter  过滤条件（页号、每页条数不参与）
    //@param nowMs   当前时间（Unix毫秒）
    //@param out     [出参] 各月的条数，键为 年*100+月，按年月倒序；没有消息的月份不出现；调用前会被清空
    //@return 查询成功返回 true；未打开数据库或出错返回 false
    bool CountByLocalMonth(const HistoryFilter& filter, int64_t nowMs, std::vector<HistoryPeriodCount>& out);

    //时间轴列表：统计一个时间区间里按本地日期分组的条数，供每天那个大节点写「当天条数」。
    //@param filter  过滤条件
    //@param nowMs   当前时间（Unix毫秒）
    //@param fromMs  区间起（含，Unix毫秒）
    //@param toMs    区间止（含，Unix毫秒）
    //@param out     [出参] 各天的条数，键为 年*10000+月*100+日，按日期倒序；调用前会被清空
    //@return 查询成功返回 true；未打开数据库或出错返回 false
    bool CountByLocalDay(const HistoryFilter& filter, int64_t nowMs, int64_t fromMs, int64_t toMs,
                         std::vector<HistoryPeriodCount>& out);

    //取符合条件的各条消息的位置（时间与 msgid），排序同 QueryPage，最多 limit 条。时间轴模式下按关键字
    //找出命中位置，供「上一处 / 下一处」跳转。
    //@param filter     过滤条件（页号、每页条数不参与）
    //@param nowMs      当前时间（Unix毫秒）
    //@param limit      最多几条（不大于 0 时按 1 处理）
    //@param out        [出参] 各条的位置；调用前会被清空
    //@return 查询成功返回 true；未打开数据库或出错返回 false
    bool QueryPositions(const HistoryFilter& filter, int64_t nowMs, int limit, std::vector<HistoryPosition>& out);

    //取某会话最近的 limit 条消息，供打开聊天窗时先用本地数据显示。与 QueryPage 不同：
    //按 msgid 取（与服务端同步应答的取法一致）、<u>含</u>已撤回的消息（服务端同步应答同样会
    //返回它们），并带出原始 JSON（HistoryRecord::m_raw，老数据为空串）。
    //@param ctype  会话类型（1单聊 2群聊）
    //@param cid    会话ID（我方视角）
    //@param limit  最多取几条，须大于 0
    //@param out    [出参] 结果按 msgid 升序（最旧在前，与同步应答相同）；调用前会被清空
    //@return 查询成功返回 true（即使 0 条也算成功）；未打开数据库、limit 非法或出错返回 false
    bool QueryLatest(int ctype, int64_t cid, int limit, std::vector<HistoryRecord>& out);

    //主面板全局搜索「消息」标签：跨全部会话按会话汇总命中关键字的聊天记录。排除已撤回的消息与
    //文件、文件夹传输记录、文件共享卡片三类（它们归「文件」标签），每个会话按消息记录窗相同的
    //覆盖区间规则只计可见的行（规则见 V2MsgQuery.h 的 BuildSearchConversationsSql）。
    //@param keywordUtf8  关键字（UTF-8）；为空时直接返回成功且结果为空，不查库
    //@param limit        最多返回几个会话，须大于 0
    //@param out          [出参] 按最近一条命中的时间倒序；调用前会被清空
    //@return 查询成功返回 true（即使 0 个会话也算成功）；未打开数据库、limit 非法或出错返回 false
    bool SearchConversations(const std::string& keywordUtf8, int limit, std::vector<ConversationHit>& out);

    //主面板全局搜索「文件」标签的本地部分：跨全部会话查文件、文件夹传输记录、文件共享卡片三类中
    //search_text（文件名、文件夹名、共享名）命中关键字的消息。排除已撤回的，覆盖区间规则同上。
    //@param keywordUtf8  关键字（UTF-8）；为空时直接返回成功且结果为空，不查库
    //@param limit        最多返回几条，须大于 0
    //@param out          [出参] 按时间倒序（同毫秒按 msgid 倒序），带原始 JSON；调用前会被清空
    //@return 查询成功返回 true（即使 0 条也算成功）；未打开数据库、limit 非法或出错返回 false
    bool SearchFileMessages(const std::string& keywordUtf8, int limit, std::vector<HistoryRecord>& out);

    //聊天记录管理器左栏的本地部分：列出本地库里每个有可见记录的会话，以及它最后一条消息的概况。
    //
    //口径：每个会话取 msgid 最大的一条<u>未撤回</u>消息作为最后一条（msgid 由服务端单调分配，与
    //服务端会话摘要取 MAX(f_msgid) 一致）；这一条按覆盖区间不可见时（HistoryCoverage.h 的
    //CoverageMinVisibleMsgid），比它更早的也都不可见，整个会话不列出。
    //
    //做法：先沿主键索引 (ctype,cid,msgid) 逐个跳到下一个会话，再按主键倒序取每个会话的第一条
    //未撤回行。查询次数与会话个数成正比，与消息总条数无关；不对全表按会话分组。
    //耗时实测见 Tests/HistoryManagerStoreTests.cpp 的 HMB1。
    //@param out  [出参] 按最后时间倒序、同毫秒按 msgid 倒序（见 SortLocalConversationBriefs）；
    //            调用前会被清空，失败时为空
    //@return 查询成功返回 true（即使一个会话都没有）；未打开数据库或出错返回 false
    bool QueryConversationList(std::vector<LocalConversationBrief>& out);

    //聊天记录管理器左栏的跨会话搜索：按会话汇总命中关键字的聊天记录，可以限定在若干会话里。
    //与 SearchConversations 的区别是不排除文件类消息、可以限定范围（理由见 V2MsgQuery.h 的
    //BuildManagerSearchSql）；排除已撤回、按覆盖区间只计可见行这两条口径相同。
    //@param keywordUtf8  关键字（UTF-8）；为空时直接返回成功且结果为空，不查库
    //@param limit        最多返回几个会话，须大于 0
    //@param scope        限定的会话；为空表示所有会话
    //@param out          [出参] 按最近一条命中的时间倒序；调用前会被清空
    //@param wildcard     关键字是否按通配符解释（'*' 任意个字符、'?' 一个字符，见 HistoryWildcard.h）；
    //                    为 true 且去掉 '*' 后为空时按空关键字处理
    //@param fromIds      关键字里 from:名字 解析出的 userid（搜索语法，docs/聊天记录管理器方案.md §12.3）；关键字里
    //                    没有 from: 时不使用，有 from: 而它为空时没有结果
    //@return 查询成功返回 true（即使 0 个会话也算成功）；未打开数据库、limit 非法或出错返回 false
    //关键字按搜索语法解释：空白分隔的多个词同时包含，-词 排除，from: / type: / after: / before: 限定。
    bool SearchConversationsForManager(const std::string& keywordUtf8, int limit,
                                       const std::vector<HistoryConversationKey>& scope,
                                       std::vector<ConversationHit>& out, bool wildcard = false,
                                       const std::vector<int64_t>& fromIds = std::vector<int64_t>());

    // ---- 聊天记录管理器的文件页与图片页（docs/聊天记录管理器方案.md §11.5、§11.6） ----

    //写入或更新群文件信息缓存表里的一个群文件（收到群文件新增推送时调用）。本机落地路径不变。
    //@param item  群文件；m_groupid 须大于 0、m_entryid 不能为空
    //@return 写入成功返回 true；未打开数据库或参数非法返回 false
    bool UpsertGroupFileCache(const GroupFileCacheItem& item);

    //把某群在缓存表里的群文件整体替换成给定的列表（按群用 cmd_get_group_files 拉齐之后调用）：列表里的
    //写入或更新，缓存里有、列表里没有的删除；已有条目的本机落地路径保留。整批放在一个事务里，失败时回滚。
    //@param groupid  群 ID，须大于 0
    //@param items    该群的全部群文件；m_groupid 与 groupid 不同、或没有条目 ID 的项跳过
    //@return 提交成功返回 true；未打开数据库、参数非法或写入失败返回 false
    bool ReplaceGroupFileCache(int64_t groupid, const std::vector<GroupFileCacheItem>& items);

    //删除缓存表里的一个群文件（收到群文件删除推送时调用）。
    //@return 语句执行成功返回 true（本来就没有这一条也算成功）；未打开数据库返回 false
    bool DeleteGroupFileCacheEntry(int64_t groupid, const std::string& entryid);

    //删除缓存表里某群的全部群文件（本人离开群、被移出或群被解散后调用）。
    //@return 语句执行成功返回 true；未打开数据库返回 false
    bool DeleteGroupFileCacheOfGroup(int64_t groupid);

    //登记一个群文件在本机的落地路径（群文件下载成功后调用，不论是谁发起的下载）。
    //@param groupid        群 ID
    //@param entryid        群文件条目 ID
    //@param localPathUtf8  落地完整路径（UTF-8）
    //@param outHit         [出参] 缓存里是否有这一条；没有时为 false（还没拉齐过这个群，不算失败）
    //@return 语句执行成功返回 true；未打开数据库返回 false
    bool SetGroupFileLocalPath(int64_t groupid, const std::string& entryid, const std::string& localPathUtf8,
                               bool& outHit);

    //按主键取缓存表里的一个群文件。
    //@param out    [出参] 找到时写入
    //@param found  [出参] 是否找到
    //@return 语句执行成功返回 true（无论是否找到）；未打开数据库返回 false
    bool QueryGroupFileCache(int64_t groupid, const std::string& entryid, GroupFileCacheItem& out, bool& found);

    //取缓存表里某群的全部群文件，按登记时间倒序。
    //@param out  [出参] 群文件；调用前会被清空
    //@return 查询成功返回 true；未打开数据库返回 false
    bool QueryGroupFileCacheOfGroup(int64_t groupid, std::vector<GroupFileCacheItem>& out);

    //文件页 / 图片页：按筛选条件统计总条数（聊天消息与群文件两部分之和，规则见 HistoryLibraryQuery.h）。
    //@param filter  查询条件（页号、每页条数不参与）
    //@param nowMs   当前时间（Unix 毫秒），供「近 N 天」换算
    //@param out     [出参] 总条数
    //@return 查询成功返回 true；未打开数据库或出错返回 false
    bool CountLibrary(const HistoryLibraryFilter& filter, int64_t nowMs, int64_t& out);

    //文件页 / 图片页：按筛选条件取一页，时间倒序。聊天消息的大小、文件 ID、MD5 已从内容 JSON 补齐。
    //@param out  [出参] 本页各行；调用前会被清空
    //@return 查询成功返回 true（0 条也算成功）；未打开数据库或出错返回 false
    bool QueryLibraryPage(const HistoryLibraryFilter& filter, int64_t nowMs, std::vector<HistoryLibraryItem>& out);

    //文件页 / 图片页：按会话统计条数，供左栏每一行右侧显示（不受会话选择与「只看单聊 / 群聊」限制）。
    //@param out  [出参] 有条目的会话及条数，按 ctype、cid 升序；调用前会被清空
    //@return 查询成功返回 true；未打开数据库或出错返回 false
    bool CountLibraryByConversation(const HistoryLibraryFilter& filter, int64_t nowMs,
                                    std::vector<HistoryLibraryConvCount>& out);

    //写入一条撤回快照（docs/撤回后重新编辑方案.md §3.1）。同一主键 (ctype, cid, msgid) 已有时整行覆盖。
    //@param ctype        会话类型（1 单聊 2 群聊），与 t_msg 口径相同
    //@param cid          会话ID（单聊=对端userid，群聊=群id）
    //@param msgid        被撤回消息的服务器msgid（须 >0）
    //@param recallTimeMs 撤回推送到达本机的时间（Unix 毫秒）
    //@param payload      快照 JSON（UTF-8）
    //@return 写入成功返回 true；未打开数据库、msgid 非正或语句失败返回 false
    bool SaveRecallDraft(int ctype, int64_t cid, int64_t msgid, int64_t recallTimeMs, const std::string& payload);

    /**
     *  读同步进度表 t_sync_meta 里的一项（键 rewritten_rev 存已同步到的改写序号，见
     *  docs/消息改写离线同步方案.md 3.3）。进度表与本账号的 msgv2.db 同生共死，库被删掉重建时进度自然归零。
     *  @param key   键名（ASCII），如 "rewritten_rev"
     *  @param out   [出参] 值；没有这一项时为 0
     *  @param found [出参] 表里有没有这一项
     *  @return 库已打开且查询成功返回 true（没有这一项也算成功）
     */
    bool ReadSyncValue(const char* key, int64_t& out, bool& found);

    /**
     *  写同步进度表里的一项，已有时整项覆盖。
     *  @param key   键名（ASCII）
     *  @param value 值
     *  @return 库已打开且写入成功返回 true
     */
    bool WriteSyncValue(const char* key, int64_t value);

    /**
     *  本地库里有没有任何一条消息（含已撤回的）。按改写序号第一次同步时，本地库为空就不必逐页拉取，
     *  直接把水位设成服务端当前的最大序号（docs/消息改写离线同步方案.md 3.4）。
     *  @param out [出参] 有消息为 true
     *  @return 库已打开且查询成功返回 true
     */
    bool HasAnyMessage(bool& out);

    //按一批 msgid 取同一会话里的撤回快照。
    //@param ctype  会话类型（1 单聊 2 群聊）
    //@param cid    会话ID
    //@param msgids 要查的 msgid；非正的跳过
    //@param out    [出参] 查到的行（顺序不保证）；调用前会被清空
    //@return 查询成功返回 true（0 条也算成功）；未打开数据库或语句失败返回 false
    bool QueryRecallDrafts(int ctype, int64_t cid, const std::vector<int64_t>& msgids,
                           std::vector<RecallDraftRow>& out);

    //删除撤回时间早于给定时刻的快照（已过 5 分钟的那些，留着没有用处）。
    //@param olderThanMs 时刻（Unix 毫秒），recall_time 小于它的行被删除
    //@param deleted     [出参] 删除的行数
    //@return 语句执行成功返回 true；未打开数据库或语句失败返回 false
    bool PurgeRecallDrafts(int64_t olderThanMs, int& deleted);

private:
    //执行无结果集 SQL（假定已持锁、m_db 有效）；成功返回 true。
    bool ExecLocked(const char* sql);

    //QueryConversationList 的主体（假定已持锁、m_db 有效、调用方已开好读事务）：读覆盖区间，逐个会话
    //取最后一条未撤回消息，把可见的会话追加进 out（不排序）。
    //@return 全部语句执行成功返回 true；任何一条出错返回 false，此时 out 里可能已有部分结果
    bool QueryConversationListLocked(std::vector<LocalConversationBrief>& out);

    //给存量库补上缺失的列（假定已持锁、m_db 有效）。用 PRAGMA table_info 读回现有列名，
    //缺哪列就 ALTER TABLE ADD COLUMN。须在建表<u>之后</u>、建索引<u>之前</u>调用：
    //建表语句是 IF NOT EXISTS 形式，对已存在的库整条跳过，新加的列只能靠本方法补；
    //而索引可能用到新列，先建索引会报 no such column。
    void EnsureColumnsLocked();

    //建全文索引表、进度表与三个触发器，并读出回填是否已完成（假定已持锁、m_db 有效）。整组语句
    //放在一个事务里，任何一条失败（SQLite 未带 FTS5 等）就整组回滚，m_ftsAvailable 保持 false，
    //此后跨会话搜索一律 LIKE；不会留下引用不存在的索引表的触发器（那样每次写库都会失败）。
    void InitFullTextLocked();

    //读进度表 t_fts_meta 里 key 对应的值（假定已持锁、m_db 有效）。没有这一项或出错返回 false。
    bool ReadFtsMetaLocked(const char* key, int64_t& out);

    //本次查询是否走全文索引（假定已持锁）：索引可用、允许使用，且关键字不少于三个字；wildcard 为 true
    //时改为要求 BuildWildcardFtsQuery 切得出至少一段不少于三个字的文字。
    bool UseFullTextLocked(const std::string& keywordUtf8, bool wildcard = false) const;

    //SQLite 进度回调（sqlite3_progress_handler，每执行若干条虚拟机指令调用一次）：中断标志置位时
    //返回非 0，让正在执行的语句以 SQLITE_INTERRUPT 结束。self 为本实例。
    static int OnProgress(void* self);

    CV2MsgStore(const CV2MsgStore&);
    CV2MsgStore& operator=(const CV2MsgStore&);

private:
    sqlite3*   m_db;     //SQLite 连接句柄；NULL=未打开
    std::wstring m_dbPath;   //Open 成功时传入的数据库路径；Close 后清空
    bool m_ftsAvailable;     //全文索引表与触发器已建好（Open 时确定，Close 后复位）
    bool m_ftsReady;         //存量数据已回填完毕，跨会话搜索可以用全文索引（Open 时读出，回填完成时置位）
    bool m_ftsAllowed;       //是否允许使用全文索引，见 SetFullTextAllowed；默认 true，不随 Close 复位
    std::atomic<bool> m_interruptRequested;   //中断标志，见 Interrupt / ClearInterrupt；进度回调在别的线程读它
    std::mutex m_mutex;  //保护 m_db 的并发读写（本期 UI 线程，留作将来异步）
};

} // namespace balloonhistory
