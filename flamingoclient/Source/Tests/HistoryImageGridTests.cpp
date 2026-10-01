/**
 *  消息记录「图片」分类缩略图网格与「文件」分类批量下载的单元测试（HGV1、HGV3、HBD1~HBD7），
 *  HistoryImageGridTests.cpp
 *
 *  覆盖：列数随宽度变化、缩略图互不重叠且不越出宽度、新的一天另起一组并有日期标题；
 *  批量下载只收本地没有、也没在下载中的文件；「全部下载」的一批下载不逐条提示失败，
 *  整批结束后按成功 / 失败 / 取消的笔数汇总提示一次。网格控件 id 与记录的对应见 HistoryWindowLayoutTests 的 HGV2。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-01
 */
#include "catch_amalgamated.hpp"
#include <string>
#include <vector>
#include "../UI/BalloonHistory/HistoryImageGrid.h"

using namespace balloonhistory;

namespace {

HistoryGridMetrics TestMetrics()
{
    HistoryGridMetrics m;
    m.m_cellSize = 120;
    m.m_gap = 8;
    m.m_headerH = 28;
    m.m_padding = 12;
    return m;
}

bool Overlaps(const RECT& a, const RECT& b)
{
    RECT x;
    return ::IntersectRect(&x, &a, &b) != FALSE;
}

} // namespace

TEST_CASE("HGV1 网格排版：列数随宽度变化，缩略图互不重叠，新的一天另起一组", "[historygrid]")
{
    const HistoryGridMetrics m = TestMetrics();
    //可用宽度 = 2*12 内边距 + n*120 + (n-1)*8
    CHECK(GridColumnCount(24 + 120 * 5 + 8 * 4, m) == 5);
    CHECK(GridColumnCount(24 + 120 * 5 + 8 * 4 - 1, m) == 4);
    CHECK(GridColumnCount(10, m) == 1);   //再窄也至少一列

    //两天：第一天 7 张、第二天 2 张；宽度放得下 5 列
    std::vector<int64_t> days;
    for (int i = 0; i < 7; ++i)
    {
        days.push_back(20260930);
    }
    days.push_back(20260929);
    days.push_back(20260929);
    const int width = 24 + 120 * 5 + 8 * 4;
    const HistoryGridLayout layout = LayoutImageGrid(days, width, m);
    CHECK(layout.m_columns == 5);
    REQUIRE(layout.m_cells.size() == days.size());
    REQUIRE(layout.m_headers.size() == 2);
    CHECK(layout.m_headers[0].m_firstIndex == 0);
    CHECK(layout.m_headers[1].m_firstIndex == 7);

    //互不重叠、不越出宽度、都是正方形
    for (size_t i = 0; i < layout.m_cells.size(); ++i)
    {
        INFO("cell " << i);
        const RECT& rc = layout.m_cells[i];
        CHECK(rc.right - rc.left == 120);
        CHECK(rc.bottom - rc.top == 120);
        CHECK(rc.left >= 12);
        CHECK(rc.right <= width - 12);
        for (size_t j = i + 1; j < layout.m_cells.size(); ++j)
        {
            CHECK_FALSE(Overlaps(rc, layout.m_cells[j]));
        }
    }
    //第一天第 6 张换到第二行行首
    CHECK(layout.m_cells[5].left == layout.m_cells[0].left);
    CHECK(layout.m_cells[5].top > layout.m_cells[0].top);
    //第二天另起一组：标题在第一天最后一行之下，第二天第一张在标题之下、回到行首
    CHECK(layout.m_headers[1].m_rc.top >= layout.m_cells[6].bottom);
    CHECK(layout.m_cells[7].top >= layout.m_headers[1].m_rc.bottom);
    CHECK(layout.m_cells[7].left == layout.m_cells[0].left);
    //总高度盖住最后一行
    CHECK(layout.m_height >= layout.m_cells[8].bottom + 12);

    //空输入
    const HistoryGridLayout empty = LayoutImageGrid(std::vector<int64_t>(), width, m);
    CHECK(empty.m_cells.empty());
    CHECK(empty.m_headers.empty());
}

TEST_CASE("HGV3 批量下载只收本地没有、也没在下载中的文件", "[historygrid]")
{
    std::vector<HistoryFileRowState> rows(5);
    rows[0].m_msgid = 1;
    rows[0].m_isFile = true;                 //要下载
    rows[1].m_msgid = 2;
    rows[1].m_isFile = true;
    rows[1].m_hasLocal = true;               //本地已有
    rows[2].m_msgid = 3;
    rows[2].m_isFile = true;
    rows[2].m_downloading = true;            //正在下载
    rows[3].m_msgid = 4;
    rows[3].m_isFile = false;                //不是文件（如图片）
    rows[4].m_msgid = 0;
    rows[4].m_isFile = true;                 //msgid 未分配
    const std::vector<int64_t> picked = PickFilesToDownload(rows);
    REQUIRE(picked.size() == 1);
    CHECK(picked[0] == 1);
}

namespace {

//构造一条"整批已结束"的判定，用于测提示方式与文案
HistoryBatchFinish DoneWith(int succeeded, int failed, int cancelled)
{
    HistoryBatchFinish f;
    f.m_inBatch = true;
    f.m_batchDone = true;
    f.m_succeeded = succeeded;
    f.m_failed = failed;
    f.m_cancelled = cancelled;
    return f;
}

} // namespace

TEST_CASE("HBD1 批量中的一笔失败：属于本批、不单独提示，本批尚未结束", "[historygrid]")
{
    HistoryBatchDownload batch;
    batch.Add(1);
    batch.Add(2);
    batch.Add(3);
    const HistoryBatchFinish f = batch.OnFinished(1, false, false);
    CHECK(f.m_inBatch);
    CHECK_FALSE(f.m_batchDone);
    CHECK(BatchNoticeKind(f) == kHistoryBatchNoticeNone);
    CHECK(batch.PendingCount() == 2);
}

TEST_CASE("HBD2 本批最后一笔结束：汇总成功与失败的笔数，之后本批清空", "[historygrid]")
{
    HistoryBatchDownload batch;
    batch.Add(1);
    batch.Add(2);
    batch.Add(3);
    batch.OnFinished(1, false, false);
    const HistoryBatchFinish f2 = batch.OnFinished(2, true, false);
    CHECK(f2.m_inBatch);
    CHECK_FALSE(f2.m_batchDone);
    const HistoryBatchFinish f3 = batch.OnFinished(3, true, false);
    CHECK(f3.m_inBatch);
    CHECK(f3.m_batchDone);
    CHECK(f3.m_failed == 1);
    CHECK(f3.m_succeeded == 2);
    CHECK(f3.m_cancelled == 0);
    CHECK(BatchNoticeKind(f3) == kHistoryBatchNoticeFailed);
    CHECK(batch.PendingCount() == 0);
    //本批已结束：同一个 msgid 再次结束不再属于任何批次
    CHECK_FALSE(batch.OnFinished(3, false, false).m_inBatch);
}

TEST_CASE("HBD3 不在本批里的下载：判定为不属于本批，由调用方逐条提示", "[historygrid]")
{
    HistoryBatchDownload batch;
    //没有任何批次
    const HistoryBatchFinish none = batch.OnFinished(99, false, false);
    CHECK_FALSE(none.m_inBatch);
    CHECK_FALSE(none.m_batchDone);
    //有批次，但这一笔不在其中（例如从右键菜单单独点的「下载」）
    batch.Add(1);
    const HistoryBatchFinish other = batch.OnFinished(99, false, false);
    CHECK_FALSE(other.m_inBatch);
    CHECK(batch.PendingCount() == 1);
    //不大于 0 的 msgid 不计入
    batch.Add(0);
    batch.Add(-5);
    CHECK(batch.PendingCount() == 1);
}

TEST_CASE("HBD4 用户取消的不算失败；整批都被取消时不提示", "[historygrid]")
{
    HistoryBatchDownload batch;
    batch.Add(1);
    batch.Add(2);
    batch.OnFinished(1, false, true);
    const HistoryBatchFinish f = batch.OnFinished(2, true, false);
    REQUIRE(f.m_batchDone);
    CHECK(f.m_cancelled == 1);
    CHECK(f.m_failed == 0);
    CHECK(f.m_succeeded == 1);
    CHECK(BatchNoticeKind(f) == kHistoryBatchNoticeAllDone);

    HistoryBatchDownload allCancelled;
    allCancelled.Add(1);
    allCancelled.Add(2);
    allCancelled.OnFinished(1, false, true);
    const HistoryBatchFinish g = allCancelled.OnFinished(2, false, true);
    REQUIRE(g.m_batchDone);
    CHECK(g.m_cancelled == 2);
    CHECK(BatchNoticeKind(g) == kHistoryBatchNoticeNone);
    CHECK(BuildBatchNoticeText(g).empty());
}

TEST_CASE("HBD5 本批未结束时再次「全部下载」：并入同一批，结束后只汇总一次", "[historygrid]")
{
    HistoryBatchDownload batch;
    batch.Add(1);
    batch.Add(2);
    CHECK_FALSE(batch.OnFinished(1, true, false).m_batchDone);
    //再次点击：新发起的 3 并入；重复的 2 不重复计入
    batch.Add(2);
    batch.Add(3);
    CHECK(batch.PendingCount() == 2);
    CHECK_FALSE(batch.OnFinished(2, false, false).m_batchDone);
    const HistoryBatchFinish f = batch.OnFinished(3, true, false);
    REQUIRE(f.m_batchDone);
    CHECK(f.m_succeeded == 2);
    CHECK(f.m_failed == 1);
}

TEST_CASE("HBD6 关窗或切换会话时作废本批：之后结束的下载都不再计入、也不提示", "[historygrid]")
{
    HistoryBatchDownload batch;
    batch.Add(1);
    batch.Add(2);
    batch.OnFinished(1, false, false);
    //返回尚未结束的那几笔，窗口据此停止跟踪它们
    const std::vector<int64_t> unfinished = batch.Clear();
    REQUIRE(unfinished.size() == 1);
    CHECK(unfinished[0] == 2);
    CHECK(batch.PendingCount() == 0);
    const HistoryBatchFinish f = batch.OnFinished(2, false, false);
    CHECK_FALSE(f.m_inBatch);
    CHECK_FALSE(f.m_batchDone);
    CHECK(BatchNoticeKind(f) == kHistoryBatchNoticeNone);
    //作废后的新一批从零计数，不带上作废那一批的失败
    batch.Add(3);
    const HistoryBatchFinish g = batch.OnFinished(3, true, false);
    REQUIRE(g.m_batchDone);
    CHECK(g.m_failed == 0);
    CHECK(g.m_succeeded == 1);
}

TEST_CASE("HBD7 整批结束后的提示文案", "[historygrid]")
{
    CHECK(BuildBatchNoticeText(DoneWith(3, 2, 0))
          == std::wstring(L"2 个文件下载失败，请检查网络后重试（另有 3 个已下载完成）。"));
    CHECK(BuildBatchNoticeText(DoneWith(0, 2, 0))
          == std::wstring(L"2 个文件下载失败，请检查网络后重试。"));
    CHECK(BuildBatchNoticeText(DoneWith(5, 0, 0)) == std::wstring(L"5 个文件已下载完成"));
    //取消的笔数不出现在文案里
    CHECK(BuildBatchNoticeText(DoneWith(5, 0, 1)) == std::wstring(L"5 个文件已下载完成"));
    //整批尚未结束：没有提示
    HistoryBatchFinish running;
    running.m_inBatch = true;
    running.m_failed = 1;
    CHECK(BatchNoticeKind(running) == kHistoryBatchNoticeNone);
    CHECK(BuildBatchNoticeText(running).empty());
}
