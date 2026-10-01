/**
 *  会话已读位点上报的去重，说明见 ReadReportDedupe.h。
 *  balloonwj@qq.com   2026-10-01
 */

#include "ReadReportDedupe.h"

namespace balloonmain {

bool ReadReportDedupe::ShouldReport(int ctype, int64_t cid, int64_t readmsgid)
{
    //会话还没有已知消息时不上报，也不留记录（与 ReportConversationRead 内部跳过的口径一致）
    if (readmsgid <= 0)
        return false;

    const std::pair<int, int64_t> key(ctype, cid);
    std::map<std::pair<int, int64_t>, int64_t>::iterator it = m_lastReported.find(key);
    //服务端的已读位点只往前推进，同样或更小的值报了也不起作用
    if (it != m_lastReported.end() && readmsgid <= it->second)
        return false;

    m_lastReported[key] = readmsgid;
    return true;
}

void ReadReportDedupe::Reset()
{
    m_lastReported.clear();
}

} // namespace balloonmain
