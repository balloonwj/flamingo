/**
 *  群文件并发传输池「待处理队列 + 取消/去重簿记」单元测试（Suite BGTQ），
 *  GroupTransferQueueTests.cpp
 *
 *  覆盖 net::GroupTransferQueue 的纯逻辑边界：
 *    BGTQ1 上传入队返回唯一且自增的 uploadId，先进先出取出；
 *    BGTQ2 上传 / 下载混在同一队列按入队顺序取出；
 *    BGTQ3 取消「仍在队列里（未轮到）」的上传 → 之后取不到它；
 *    BGTQ4 取消「已取出在传」的上传 → 其取消标志被置位（可查询）；
 *    BGTQ5 下载按 entryid 去重（队列中或在传中已有则忽略），完成后可再入队；
 *    BGTQ6 complete 后簿记清理（RunningCount 归零、取消查询不再命中）。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-06-09
 */
#include "catch_amalgamated.hpp"
#include "../net/GroupTransferQueue.h"

using namespace net;

TEST_CASE("BGTQ1 上传入队返回唯一自增 id 且先进先出")
{
    GroupTransferQueue q;
    int id1 = q.EnqueueUpload(100, L"C:\\a.txt", 9);
    int id2 = q.EnqueueUpload(100, L"C:\\b.txt", 9);
    int id3 = q.EnqueueUpload(100, L"C:\\c.txt", 9);

    // id 唯一且递增
    REQUIRE(id1 != id2);
    REQUIRE(id2 != id3);
    REQUIRE(id1 < id2);
    REQUIRE(id2 < id3);

    REQUIRE(q.PendingCount() == 3);
    REQUIRE(q.RunningCount() == 0);

    // 先进先出：按入队顺序取出
    GroupTransferItem it;
    REQUIRE(q.PopNext(it));
    REQUIRE(it.m_isUpload);
    REQUIRE(it.m_uploadId == id1);
    REQUIRE(it.m_localPath == L"C:\\a.txt");
    REQUIRE(it.m_cancelFlag);                 // 上传项取出时应带取消标志
    REQUIRE(it.m_cancelFlag->load() == false);

    REQUIRE(q.PopNext(it));
    REQUIRE(it.m_uploadId == id2);

    REQUIRE(q.PendingCount() == 1);
    REQUIRE(q.RunningCount() == 2);

    REQUIRE(q.PopNext(it));
    REQUIRE(it.m_uploadId == id3);
    REQUIRE(q.PendingCount() == 0);

    // 空队列再取应失败
    REQUIRE_FALSE(q.PopNext(it));
}

TEST_CASE("BGTQ2 上传下载混排按入队顺序取出")
{
    GroupTransferQueue q;
    int up1 = q.EnqueueUpload(100, L"C:\\a.txt", 9);
    REQUIRE(q.EnqueueDownload(100, "e1", "f1", "a.pdf", "md5a", 123, 9, L"C:\\dl\\f.bin"));
    int up2 = q.EnqueueUpload(100, L"C:\\b.txt", 9);
    REQUIRE(q.EnqueueDownload(100, "e2", "f2", "b.pdf", "md5b", 456, 9, L"C:\\dl\\f.bin"));

    REQUIRE(q.PendingCount() == 4);

    GroupTransferItem it;
    REQUIRE(q.PopNext(it));
    REQUIRE(it.m_isUpload);
    REQUIRE(it.m_uploadId == up1);

    REQUIRE(q.PopNext(it));
    REQUIRE_FALSE(it.m_isUpload);
    REQUIRE(it.m_entryId == "e1");
    REQUIRE(it.m_fileId == "f1");
    REQUIRE(it.m_filename == "a.pdf");
    REQUIRE(it.m_expectMd5 == "md5a");
    REQUIRE(it.m_expectSize == 123);
    REQUIRE(it.m_cancelFlag);                  // 下载项取出时也带取消标志（下载也可取消）
    REQUIRE(it.m_cancelFlag->load() == false);

    REQUIRE(q.PopNext(it));
    REQUIRE(it.m_isUpload);
    REQUIRE(it.m_uploadId == up2);

    REQUIRE(q.PopNext(it));
    REQUIRE_FALSE(it.m_isUpload);
    REQUIRE(it.m_entryId == "e2");
}

TEST_CASE("BGTQ3 取消仍在队列里的上传则不再被取出")
{
    GroupTransferQueue q;
    int upA = q.EnqueueUpload(100, L"C:\\a.txt", 9);
    int upB = q.EnqueueUpload(100, L"C:\\b.txt", 9);

    // 取消队首 A（尚未取出）
    q.CancelUpload(upA);
    REQUIRE(q.PendingCount() == 1);   // A 被移除，只剩 B

    GroupTransferItem it;
    REQUIRE(q.PopNext(it));
    REQUIRE(it.m_uploadId == upB);    // 取到的是 B，不是被取消的 A
    REQUIRE_FALSE(q.PopNext(it));     // 没有别的了
}

TEST_CASE("BGTQ4 取消已取出在传的上传则置位其取消标志")
{
    GroupTransferQueue q;
    int up = q.EnqueueUpload(100, L"C:\\a.txt", 9);

    GroupTransferItem it;
    REQUIRE(q.PopNext(it));
    REQUIRE(it.m_uploadId == up);
    REQUIRE(it.m_cancelFlag->load() == false);
    REQUIRE_FALSE(q.IsUploadCancelled(up));

    q.CancelUpload(up);

    // worker 手里那份取消标志被置位（worker 据此在分片边界中止）
    REQUIRE(it.m_cancelFlag->load() == true);
    REQUIRE(q.IsUploadCancelled(up));

    // 取消一个未知 id 不应崩、不应影响
    q.CancelUpload(99999);
    REQUIRE(q.IsUploadCancelled(up));
}

TEST_CASE("BGTQ5 下载按 entryid 去重，完成后可再入队")
{
    GroupTransferQueue q;
    // 同一 entryid 在队列里时，重复入队被忽略
    REQUIRE(q.EnqueueDownload(100, "e1", "f1", "a.pdf", "md5a", 1, 9, L"C:\\dl\\f.bin"));
    REQUIRE_FALSE(q.EnqueueDownload(100, "e1", "f1", "a.pdf", "md5a", 1, 9, L"C:\\dl\\f.bin"));
    REQUIRE(q.PendingCount() == 1);

    // 取出后进入「在传」，仍去重
    GroupTransferItem it;
    REQUIRE(q.PopNext(it));
    REQUIRE(it.m_entryId == "e1");
    REQUIRE(q.RunningCount() == 1);
    REQUIRE_FALSE(q.EnqueueDownload(100, "e1", "f1", "a.pdf", "md5a", 1, 9, L"C:\\dl\\f.bin"));
    REQUIRE(q.PendingCount() == 0);

    // 完成后该 entryid 释放，可再次入队
    q.CompleteDownload("e1");
    REQUIRE(q.RunningCount() == 0);
    REQUIRE(q.EnqueueDownload(100, "e1", "f1", "a.pdf", "md5a", 1, 9, L"C:\\dl\\f.bin"));
    REQUIRE(q.PendingCount() == 1);

    // 不同 entryid 各自独立、不互相去重
    REQUIRE(q.EnqueueDownload(100, "e2", "f2", "b.pdf", "md5b", 1, 9, L"C:\\dl\\f.bin"));
    REQUIRE(q.PendingCount() == 2);
}

TEST_CASE("BGTQ6 完成后清理在传簿记")
{
    GroupTransferQueue q;
    int up = q.EnqueueUpload(100, L"C:\\a.txt", 9);
    REQUIRE(q.EnqueueDownload(100, "e1", "f1", "a.pdf", "md5a", 1, 9, L"C:\\dl\\f.bin"));

    GroupTransferItem it;
    REQUIRE(q.PopNext(it));   // 取出上传
    REQUIRE(q.PopNext(it));   // 取出下载
    REQUIRE(q.RunningCount() == 2);

    q.CancelUpload(up);
    REQUIRE(q.IsUploadCancelled(up));

    q.CompleteUpload(up);
    q.CompleteDownload("e1");
    REQUIRE(q.RunningCount() == 0);
    // 上传完成后簿记清掉，取消查询不再命中
    REQUIRE_FALSE(q.IsUploadCancelled(up));
}

TEST_CASE("BGTQ7 取消仍在队列里的下载则不再被取出")
{
    GroupTransferQueue q;
    REQUIRE(q.EnqueueDownload(100, "eA", "fA", "a.pdf", "mA", 1, 9, L"C:\\dl\\f.bin"));
    REQUIRE(q.EnqueueDownload(100, "eB", "fB", "b.pdf", "mB", 1, 9, L"C:\\dl\\f.bin"));

    // 取消队首 eA（尚未取出）
    q.CancelDownload("eA");
    REQUIRE(q.PendingCount() == 1);   // eA 被移除，只剩 eB

    GroupTransferItem it;
    REQUIRE(q.PopNext(it));
    REQUIRE(it.m_entryId == "eB");    // 取到的是 eB，不是被取消的 eA
    REQUIRE_FALSE(q.PopNext(it));
}

TEST_CASE("BGTQ8 取消已取出在传的下载则置位其取消标志")
{
    GroupTransferQueue q;
    REQUIRE(q.EnqueueDownload(100, "eX", "fX", "x.pdf", "mX", 1, 9, L"C:\\dl\\f.bin"));

    GroupTransferItem it;
    REQUIRE(q.PopNext(it));
    REQUIRE(it.m_entryId == "eX");
    REQUIRE(it.m_cancelFlag->load() == false);
    REQUIRE_FALSE(q.IsDownloadCancelled("eX"));

    q.CancelDownload("eX");

    // worker 手里那份取消标志被置位（worker 据此在分片边界中止下载）
    REQUIRE(it.m_cancelFlag->load() == true);
    REQUIRE(q.IsDownloadCancelled("eX"));

    // 取消未知 entryid 不崩、不影响
    q.CancelDownload("nope");
    REQUIRE(q.IsDownloadCancelled("eX"));

    // 完成后簿记清掉，取消查询不再命中
    q.CompleteDownload("eX");
    REQUIRE_FALSE(q.IsDownloadCancelled("eX"));
    REQUIRE(q.RunningCount() == 0);
}
