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

    //按过滤条件统计匹配条数（恒排除已撤回）。
    //@param filter  过滤条件
    //@param nowMs   当前时间（Unix毫秒），供“近N天”换算
    //@return 匹配条数；未打开或出错返回 0
    int64_t Count(const HistoryFilter& filter, int64_t nowMs);

    //按过滤条件分页查询（时间倒序，恒排除已撤回）。
    //@param filter  过滤条件（含页号 / 页大小）
    //@param nowMs   当前时间（Unix毫秒）
    //@param out     [出参] 本页记录（最新在前）；调用前会被清空
    //@return 查询成功返回 true（即使 0 条也算成功）；未打开或出错返回 false
    bool QueryPage(const HistoryFilter& filter, int64_t nowMs, std::vector<HistoryRecord>& out);

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
