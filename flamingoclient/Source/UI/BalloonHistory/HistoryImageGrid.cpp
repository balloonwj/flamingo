/**
 *  HistoryImageGrid.cpp —— 图片网格排版与批量下载筛选的实现，说明见头文件。
 *  balloonwj@qq.com   2026-10-01
 */
#include "HistoryImageGrid.h"
#include <sstream>

namespace balloonhistory {

int GridColumnCount(int width, const HistoryGridMetrics& metrics)
{
    //n 张缩略图占 n*边长 + (n-1)*间距，加一个间距后正好是 n*(边长+间距)
    const int inner = width - 2 * metrics.m_padding;
    const int step = metrics.m_cellSize + metrics.m_gap;
    if (step <= 0)
    {
        return 1;
    }
    const int columns = (inner + metrics.m_gap) / step;
    return (columns >= 1) ? columns : 1;
}

HistoryGridLayout LayoutImageGrid(const std::vector<int64_t>& dayKeys, int width, const HistoryGridMetrics& metrics)
{
    HistoryGridLayout layout;
    layout.m_columns = GridColumnCount(width, metrics);
    layout.m_cells.reserve(dayKeys.size());

    int y = metrics.m_padding;   //当前行（或下一个标题）的顶边
    int col = 0;                 //当前行已经放了几张
    bool lineOpen = false;       //当前行是否已有缩略图（有才需要在换行时让出这一行的高度）
    for (size_t i = 0; i < dayKeys.size(); ++i)
    {
        //新的一天：先收掉上一组的最后一行，再放日期标题
        if (i == 0 || dayKeys[i] != dayKeys[i - 1])
        {
            if (lineOpen)
            {
                y += metrics.m_cellSize + metrics.m_gap;
                lineOpen = false;
            }
            col = 0;
            HistoryGridHeader header;
            header.m_firstIndex = i;
            header.m_rc.left = metrics.m_padding;
            header.m_rc.top = y;
            header.m_rc.right = width - metrics.m_padding;
            header.m_rc.bottom = y + metrics.m_headerH;
            layout.m_headers.push_back(header);
            y += metrics.m_headerH;
        }
        //这一行满了：换行
        if (col == layout.m_columns)
        {
            y += metrics.m_cellSize + metrics.m_gap;
            col = 0;
        }
        RECT rc;
        rc.left = metrics.m_padding + col * (metrics.m_cellSize + metrics.m_gap);
        rc.top = y;
        rc.right = rc.left + metrics.m_cellSize;
        rc.bottom = rc.top + metrics.m_cellSize;
        layout.m_cells.push_back(rc);
        ++col;
        lineOpen = true;
    }
    if (lineOpen)
    {
        y += metrics.m_cellSize;
    }
    layout.m_height = y + metrics.m_padding;
    return layout;
}

std::vector<int64_t> PickFilesToDownload(const std::vector<HistoryFileRowState>& rows)
{
    std::vector<int64_t> out;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const HistoryFileRowState& r = rows[i];
        if (r.m_isFile && r.m_msgid > 0 && !r.m_hasLocal && !r.m_downloading)
        {
            out.push_back(r.m_msgid);
        }
    }
    return out;
}

HistoryBatchDownload::HistoryBatchDownload()
    : m_succeeded(0)
    , m_failed(0)
    , m_cancelled(0)
{
}

void HistoryBatchDownload::Add(int64_t msgid)
{
    if (msgid <= 0)
    {
        return;
    }
    m_pending.insert(msgid);
}

HistoryBatchFinish HistoryBatchDownload::OnFinished(int64_t msgid, bool ok, bool cancelled)
{
    HistoryBatchFinish result;
    std::set<int64_t>::iterator it = m_pending.find(msgid);
    if (it == m_pending.end())
    {
        return result;
    }
    m_pending.erase(it);
    result.m_inBatch = true;

    if (cancelled)
    {
        ++m_cancelled;
    }
    else if (ok)
    {
        ++m_succeeded;
    }
    else
    {
        ++m_failed;
    }

    if (m_pending.empty())
    {
        //本批最后一笔：带出整批计数，并清零，使下一次 Add 开始新的一批
        result.m_batchDone = true;
        result.m_succeeded = m_succeeded;
        result.m_failed = m_failed;
        result.m_cancelled = m_cancelled;
        Clear();   //此时本批已没有未结束的下载，返回值必为空
    }
    return result;
}

std::vector<int64_t> HistoryBatchDownload::Clear()
{
    const std::vector<int64_t> unfinished(m_pending.begin(), m_pending.end());
    m_pending.clear();
    m_succeeded = 0;
    m_failed = 0;
    m_cancelled = 0;
    return unfinished;
}

size_t HistoryBatchDownload::PendingCount() const
{
    return m_pending.size();
}

HistoryBatchNoticeKind BatchNoticeKind(const HistoryBatchFinish& finish)
{
    if (!finish.m_batchDone)
    {
        return kHistoryBatchNoticeNone;
    }
    if (finish.m_failed > 0)
    {
        return kHistoryBatchNoticeFailed;
    }
    if (finish.m_succeeded > 0)
    {
        return kHistoryBatchNoticeAllDone;
    }
    //整批都被用户取消：用户清楚自己做了什么，不提示
    return kHistoryBatchNoticeNone;
}

std::wstring BuildBatchNoticeText(const HistoryBatchFinish& finish)
{
    std::wostringstream text;
    switch (BatchNoticeKind(finish))
    {
    //有失败：先说失败几个，若同时有成功的再在括号里补一句
    case kHistoryBatchNoticeFailed:
        text << finish.m_failed << L" 个文件下载失败，请检查网络后重试";
        if (finish.m_succeeded > 0)
        {
            text << L"（另有 " << finish.m_succeeded << L" 个已下载完成）";
        }
        text << L"。";
        break;

    //全部成功
    case kHistoryBatchNoticeAllDone:
        text << finish.m_succeeded << L" 个文件已下载完成";
        break;

    //整批尚未结束，或整批都被取消：没有要说的
    default:
        break;
    }
    return text.str();
}

} // namespace balloonhistory
