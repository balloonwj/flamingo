/**
 *  按时间范围清理聊天记录的纯逻辑：区间的校验与截断、区间合并、读取过滤条件的生成。
 *  不访问数据库，全部由单元测试覆盖（tests/test_messagedeleterange.cpp，Suite MDR）。
 *
 *  背景：cmd_delete_messages_by_time（1521）不把区间内的每条消息逐条写进 t_message_deleted，
 *  而是只在 t_message_deleted_range 里记一条区间，读取时按区间过滤。这样清理一次的写入量与
 *  消息条数无关，不会长时间占住网络 I/O 线程。方案见 docs/清理聊天记录方案.md §5。
 *
 *  典型用法（ChatSession 处理 1521 时）：
 *      msgdelete::DeleteRange range;
 *      if (!msgdelete::NormalizeDeleteRange(req.begintime, req.endtime, nowMs, range))
 *          回 error_code_bad_request;
 *      MessageManager::addDeletedRange(userid, range);   //内部调用 MergeDeleteRanges
 *
 *  balloonwj@qq.com   2026-09-30
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace msgdelete {

/**
 *  一段本人已清理的时间区间，两端都包含，单位 Unix 毫秒。
 *  与 t_chatmsg.f_create_time 同一口径：消息时间落在 [begintime, endtime] 内即视为已清理。
 */
struct DeleteRange
{
    int64_t begintime;  //区间开始，Unix毫秒（含）；0 表示从最早开始
    int64_t endtime;    //区间结束，Unix毫秒（含）

    DeleteRange()
        : begintime(0), endtime(0)
    {
    }

    DeleteRange(int64_t beginMs, int64_t endMs)
        : begintime(beginMs), endtime(endMs)
    {
    }
};

/**
 *  校验客户端请求的清理区间，并把终点截断到服务端当前时刻之前。
 *
 *  截断规则：终点不得晚于"服务端当前这一秒开始前 1 毫秒"。原因有二：一是客户端时钟可能
 *  比服务端快，按客户端时刻算出的终点落在服务端的"未来"，之后到达的新消息会落进区间而被
 *  隐藏；二是服务端写消息时间只精确到秒（time(NULL)*1000），同一秒内稍后到达的消息时间
 *  会被取整到这一秒开头，只截断到"当前毫秒"仍会把它们算进区间。代价是请求发出那一秒内已经
 *  到达的消息不会被清理。
 *
 *  @param begintime 请求里的区间开始（Unix毫秒），须非负
 *  @param endtime   请求里的区间结束（Unix毫秒），须非负
 *  @param nowMs     服务端当前时刻（Unix毫秒）。由调用方传入而不在内部取系统时间，
 *                   是为了让本函数保持纯粹、可测
 *  @param out       [出参] 截断后的实际区间；返回 false 时内容无意义
 *  @return 合法返回 true；开始或结束为负、截断后结束早于开始时返回 false
 */
bool NormalizeDeleteRange(int64_t begintime, int64_t endtime, int64_t nowMs, DeleteRange& out);

/**
 *  把新清理的区间并入本人已有的区间集合。
 *
 *  重叠的区间合并成一段；首尾相接的（前一段结束 + 1 = 后一段开始）也合并，因为两端都包含，
 *  中间没有任何毫秒被漏掉。新区间盖住多段时合并为一段。结果按开始时间升序排列、互不相交
 *  也不相接，因此一个用户在表里的行数只会是寥寥几行。
 *
 *  @param existing 本人已有的区间，顺序不限（从数据库读出的原样即可）
 *  @param added    本次新清理的区间，须已经过 NormalizeDeleteRange
 *  @return 合并后的完整区间集合；调用方用它整体替换本人在表里的全部行
 */
std::vector<DeleteRange> MergeDeleteRanges(const std::vector<DeleteRange>& existing,
                                           const DeleteRange& added);

/**
 *  生成"消息时间不在该用户任一已清理区间内"的 SQL 片段，拼在 WHERE 条件之后。
 *
 *  片段形如 " AND NOT EXISTS (SELECT 1 FROM t_message_deleted_range mdr WHERE mdr.f_userid = 123
 *  AND <timeColumn> BETWEEN mdr.f_begin_time AND mdr.f_end_time)"，自带前置空格。
 *  MessageManager 各查询的 FROM 子句都直接写 t_chatmsg、不起别名，传 "t_chatmsg.f_create_time"；
 *  MessagePinManager 把 t_chatmsg 起别名为 m，传 "m.f_create_time"。时间列必须带表名或别名，
 *  否则在相关子查询里会被解析成子查询自己的列。
 *
 *  @param userid     请求者的 userid
 *  @param timeColumn 外层查询里消息时间列的完整写法（不可为 NULL）
 *  @return SQL 片段
 */
std::string BuildNotInDeletedRangeClause(int64_t userid, const char* timeColumn);

/**
 *  生成"排除本人清空过的会话里、清空锚点之前的消息"的 SQL 片段（cmd_clear_conversation 用）。
 *
 *  清空锚点记在本人这一行 t_conversation.f_cleared_msgid：该会话里 msgid 不大于锚点的消息对本人
 *  隐藏。片段对外层的每一条消息算出它在本人视角下属于哪个会话 —— 群消息为 (2, f_to)；单聊为
 *  (1, 对端)，对端在本人是发送方时取 f_to，否则取 f_from —— 再按主键查出锚点比较 msgid；查不到行
 *  （没有清空过）按 0 处理。所以既能用于只查一个会话的语句，也能用于跨会话的语句（搜索、会话列表、
 *  按 ID 取消息）。
 *
 *  @param userid      本人 userid
 *  @param tablePrefix 外层查询里消息表各列的前缀，含末尾的点，如 "t_chatmsg." 或 "m."（不可为 NULL）
 *  @return 以 " AND " 开头的 SQL 片段，可直接拼在 WHERE 条件之后
 */
std::string BuildNotClearedClause(int64_t userid, const char* tablePrefix);

} // namespace msgdelete
