/**
 *  按时间范围清理聊天记录的纯逻辑实现，说明见 MessageDeleteRange.h。
 *  balloonwj@qq.com   2026-09-30
 */
#include "MessageDeleteRange.h"

#include <algorithm>
#include <sstream>

namespace msgdelete {

namespace {

//一秒的毫秒数，用来求"服务端当前这一秒的开始时刻"
const int64_t kMillisPerSecond = 1000;

//排序用：按开始时间升序，开始相同时按结束时间升序
bool LessByBegin(const DeleteRange& a, const DeleteRange& b)
{
    if (a.begintime != b.begintime)
        return a.begintime < b.begintime;
    return a.endtime < b.endtime;
}

} // anonymous namespace

bool NormalizeDeleteRange(int64_t begintime, int64_t endtime, int64_t nowMs, DeleteRange& out)
{
    if (begintime < 0 || endtime < 0)
        return false;

    //允许的最晚终点是服务端当前这一秒开始前 1 毫秒，原因见头文件说明
    const int64_t secondStartMs = nowMs - (nowMs % kMillisPerSecond);
    const int64_t latestEndMs = secondStartMs - 1;
    const int64_t clampedEnd = (endtime > latestEndMs) ? latestEndMs : endtime;

    //截断后结束早于开始：开始时刻本身就落在当前这一秒或之后，没有任何可清理的消息
    if (clampedEnd < begintime)
        return false;

    out.begintime = begintime;
    out.endtime = clampedEnd;
    return true;
}

std::vector<DeleteRange> MergeDeleteRanges(const std::vector<DeleteRange>& existing,
                                           const DeleteRange& added)
{
    std::vector<DeleteRange> all(existing);
    all.push_back(added);
    std::sort(all.begin(), all.end(), LessByBegin);

    std::vector<DeleteRange> merged;
    for (size_t i = 0; i < all.size(); ++i)
    {
        const DeleteRange& cur = all[i];
        if (merged.empty())
        {
            merged.push_back(cur);
            continue;
        }

        //与上一段重叠或首尾相接时并入上一段。写成"开始 - 1 <= 上一段结束"而不是
        //"上一段结束 + 1 >= 开始"，是为了避免上一段结束取到 INT64 上限时加 1 溢出；
        //开始时刻已校验为非负，减 1 不会下溢
        DeleteRange& last = merged.back();
        if (cur.begintime - 1 <= last.endtime)
        {
            if (cur.endtime > last.endtime)
                last.endtime = cur.endtime;
        }
        else
        {
            merged.push_back(cur);
        }
    }
    return merged;
}

std::string BuildNotClearedClause(int64_t userid, const char* tablePrefix)
{
    //会话类型取值与协议一致（Protocol.h 的 conversation_single / conversation_group）。本文件刻意
    //不引入 Protocol.h、保持纯逻辑，故在此复制两个取值
    const int kCtypeGroup = 2;
    const std::string p = tablePrefix;

    //子查询里的表别名取 mcc，避免与外层查询可能用到的短别名重名
    std::ostringstream os;
    os << " AND " << p << "f_msgid > COALESCE((SELECT mcc.f_cleared_msgid FROM t_conversation mcc"
       << " WHERE mcc.f_userid = " << userid
       << " AND mcc.f_ctype = " << p << "f_ctype"
       << " AND mcc.f_cid = (CASE WHEN " << p << "f_ctype = " << kCtypeGroup << " THEN " << p << "f_to"
       << " WHEN " << p << "f_from = " << userid << " THEN " << p << "f_to"
       << " ELSE " << p << "f_from END)), 0)";
    return os.str();
}

std::string BuildNotInDeletedRangeClause(int64_t userid, const char* timeColumn)
{
    //子查询里的表别名取 mdr，避免与外层查询可能用到的短别名（如 r、m）重名
    std::ostringstream os;
    os << " AND NOT EXISTS (SELECT 1 FROM t_message_deleted_range mdr WHERE mdr.f_userid = "
       << userid << " AND " << timeColumn << " BETWEEN mdr.f_begin_time AND mdr.f_end_time)";
    return os.str();
}

} // namespace msgdelete
