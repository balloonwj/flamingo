/**
 *  文件共享本机存储单元测试（Suite SLS，标签 [sharestore]），ShareLocalStoreTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 1 期第 11 步的 11e，2026-10-02 与用户确认的用例）：
 *    - SLS1 先以 clientshareid 写入共享根记录、拿到 shareid 后补上；按两种 id 都能查到；
 *    - SLS2 共享取消后删除对应记录；
 *    - SLS3 下载续传记录只在源文件大小与修改时间都没变时有效，任何一项变了都作废；
 *    - SLS4 最近操作记录只保留最近 N 条；
 *    - SLS5 列出一个共享上的全部续传记录（第 1 期第 14 步补充，下载开始前据此判断顶层条目能否沿用原名续传）。
 *  全部用 :memory: 内存库，不落盘。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <string>
#include <vector>

#include "../Engine/ShareLocalStore.h"
#include "ShareTestFs.h"

using namespace fileshare;

namespace {

//内存库路径
const wchar_t* const kMemoryDb = L":memory:";

//几个共享 id、时间与尺寸
const int64_t kShareId = 42;
const int64_t kOtherShareId = 43;
const int64_t kCreateTime = 1759370000000LL;
const int64_t kSrcSize = 3000000;
const int64_t kSrcMtime = 1759360000000LL;
const int64_t kOffset = 1048576;
const int64_t kVisitorId = 10002;
const int kKindFolder = 2;
const int kActionDownload = 4;

//SLS4：保留的条数与写入的条数
const size_t kActivityKeep = 5;
const int kActivitiesWritten = 12;

//一段 UTF-16 中文路径：D:\项目
const wchar_t* const kChineseRoot = L"D:\\\x9879\x76EE";

} // namespace

TEST_CASE("SLS1 先以 clientshareid 写入、再补上 shareid，按两种 id 都能查到", "[sharestore]")
{
    CShareLocalStore store;
    REQUIRE(store.Open(kMemoryDb));

    REQUIRE(store.AddPendingShare("u-1", kChineseRoot, kKindFolder, true, kCreateTime));
    ShareLocalRecord rec;
    REQUIRE(store.GetByClientShareId("u-1", rec));
    CHECK(rec.m_shareId == 0);
    CHECK(rec.m_rootPath == kChineseRoot);
    CHECK(rec.m_kind == kKindFolder);
    CHECK(rec.m_includeHidden);
    CHECK(rec.m_createTime == kCreateTime);
    CHECK_FALSE(store.GetByShareId(kShareId, rec));

    //服务端应答回来：补上 shareid
    REQUIRE(store.BindShareId("u-1", kShareId));
    REQUIRE(store.GetByShareId(kShareId, rec));
    CHECK(rec.m_clientShareId == "u-1");
    CHECK(rec.m_rootPath == kChineseRoot);

    //再发起一个（磁盘共享）：两条记录并存
    REQUIRE(store.AddPendingShare("u-2", L"E:\\", 1, false, kCreateTime));
    std::vector<ShareLocalRecord> all;
    REQUIRE(store.ListShares(all));
    CHECK(all.size() == 2);

    //不存在的 clientshareid 补 shareid：失败
    CHECK_FALSE(store.BindShareId("u-9", kOtherShareId));
}

TEST_CASE("SLS2 共享取消后删除对应记录", "[sharestore]")
{
    CShareLocalStore store;
    REQUIRE(store.Open(kMemoryDb));
    REQUIRE(store.AddPendingShare("u-1", L"D:\\a", kKindFolder, false, kCreateTime));
    REQUIRE(store.BindShareId("u-1", kShareId));
    REQUIRE(store.AddPendingShare("u-2", L"D:\\b", kKindFolder, false, kCreateTime));
    REQUIRE(store.BindShareId("u-2", kOtherShareId));

    REQUIRE(store.RemoveShare(kShareId));
    ShareLocalRecord rec;
    CHECK_FALSE(store.GetByShareId(kShareId, rec));
    CHECK_FALSE(store.GetByClientShareId("u-1", rec));
    CHECK(store.GetByShareId(kOtherShareId, rec));

    //发起失败时按 clientshareid 删除还没拿到 shareid 的记录
    REQUIRE(store.AddPendingShare("u-3", L"D:\\c", kKindFolder, false, kCreateTime));
    REQUIRE(store.RemovePendingShare("u-3"));
    CHECK_FALSE(store.GetByClientShareId("u-3", rec));
}

TEST_CASE("SLS3 下载续传记录只在源文件大小与修改时间都没变时有效", "[sharestore]")
{
    CShareLocalStore store;
    REQUIRE(store.Open(kMemoryDb));

    ShareDownloadRecord rec;
    rec.m_shareId = kShareId;
    rec.m_relPath = "docs/a.zip";
    rec.m_srcSize = kSrcSize;
    rec.m_srcMtime = kSrcMtime;
    rec.m_localPath = L"C:\\Downloads\\a.zip";
    rec.m_offset = kOffset;
    rec.m_updateTime = kCreateTime;
    REQUIRE(store.SaveDownloadProgress(rec));

    std::wstring localPath;
    int64_t offset = 0;
    REQUIRE(store.GetResumePoint(kShareId, "docs/a.zip", kSrcSize, kSrcMtime, localPath, offset));
    CHECK(offset == kOffset);
    CHECK(localPath == L"C:\\Downloads\\a.zip");

    //进度推进：同一条记录更新偏移
    rec.m_offset = kOffset * 2;
    REQUIRE(store.SaveDownloadProgress(rec));
    REQUIRE(store.GetResumePoint(kShareId, "docs/a.zip", kSrcSize, kSrcMtime, localPath, offset));
    CHECK(offset == kOffset * 2);

    //源文件大小变了：作废，并且记录被删除
    CHECK_FALSE(store.GetResumePoint(kShareId, "docs/a.zip", kSrcSize + 1, kSrcMtime, localPath, offset));
    CHECK_FALSE(store.GetResumePoint(kShareId, "docs/a.zip", kSrcSize, kSrcMtime, localPath, offset));

    //修改时间变了：同样作废
    REQUIRE(store.SaveDownloadProgress(rec));
    CHECK_FALSE(store.GetResumePoint(kShareId, "docs/a.zip", kSrcSize, kSrcMtime + 1, localPath, offset));

    //下载完成后删除记录
    REQUIRE(store.SaveDownloadProgress(rec));
    REQUIRE(store.RemoveDownload(kShareId, "docs/a.zip"));
    CHECK_FALSE(store.GetResumePoint(kShareId, "docs/a.zip", kSrcSize, kSrcMtime, localPath, offset));

    //别的共享里同名路径互不影响
    REQUIRE(store.SaveDownloadProgress(rec));
    CHECK_FALSE(store.GetResumePoint(kOtherShareId, "docs/a.zip", kSrcSize, kSrcMtime, localPath, offset));
    CHECK(store.GetResumePoint(kShareId, "docs/a.zip", kSrcSize, kSrcMtime, localPath, offset));
}

TEST_CASE("SLS4 最近操作记录只保留最近 N 条", "[sharestore]")
{
    CShareLocalStore store;
    REQUIRE(store.Open(kMemoryDb));
    store.SetActivityKeep(kActivityKeep);

    for (int i = 0; i < kActivitiesWritten; ++i)
    {
        ShareActivityRecord a;
        a.m_shareId = kShareId;
        a.m_visitorId = kVisitorId;
        a.m_action = kActionDownload;
        a.m_path = "file" + std::to_string(static_cast<long long>(i)) + ".txt";
        a.m_result = 0;
        a.m_time = kCreateTime + i;
        REQUIRE(store.AddActivity(a));
    }

    std::vector<ShareActivityRecord> list;
    REQUIRE(store.ListActivities(kActivityKeep * 2, list));
    REQUIRE(list.size() == kActivityKeep);
    //最新的在前
    CHECK(list[0].m_path == "file11.txt");
    CHECK(list[kActivityKeep - 1].m_path == "file7.txt");
    CHECK(list[0].m_visitorId == kVisitorId);
    CHECK(list[0].m_time == kCreateTime + kActivitiesWritten - 1);
}

TEST_CASE("SLS5 列出一个共享上的全部续传记录", "[sharestore]")
{
    CShareLocalStore store;
    REQUIRE(store.Open(L":memory:"));

    ShareDownloadRecord rec;
    rec.m_shareId = 21;
    rec.m_relPath = "b/2.bin";
    rec.m_srcSize = 10;
    rec.m_srcMtime = 1;
    rec.m_localPath = L"D:\\dl\\b\\2.bin";
    rec.m_offset = 5;
    REQUIRE(store.SaveDownloadProgress(rec));
    rec.m_relPath = "a.bin";
    rec.m_localPath = L"D:\\dl\\a.bin";
    REQUIRE(store.SaveDownloadProgress(rec));
    //别的共享的记录不在其中
    rec.m_shareId = 22;
    rec.m_relPath = "c.bin";
    REQUIRE(store.SaveDownloadProgress(rec));

    std::vector<ShareDownloadRecord> list;
    REQUIRE(store.ListDownloads(21, list));
    REQUIRE(list.size() == 2);
    CHECK(list[0].m_relPath == "a.bin");
    CHECK(list[1].m_relPath == "b/2.bin");
    CHECK(list[1].m_localPath == L"D:\\dl\\b\\2.bin");
    CHECK(list[1].m_offset == 5);
    REQUIRE(store.ListDownloads(23, list));
    CHECK(list.empty());
}

TEST_CASE("SLS6 批量写入与批量删除上万条续传记录（第 18 步验收发现的缺陷）", "[sharestore]")
{
    //用磁盘上的库（不用内存库）：逐条写时每条都要单独落盘，这正是变慢的原因
    const std::wstring dir = sharetestfs::ExeDir() + L"\\sls6_test";
    sharetestfs::DeleteTree(dir);
    REQUIRE(sharetestfs::MakeDirs(dir));
    CShareLocalStore store;
    REQUIRE(store.Open((dir + L"\\fileshare.db").c_str()));

    const size_t kFiles = 10000;
    std::vector<ShareDownloadRecord> batch;
    std::vector<std::string> paths;
    for (size_t i = 0; i < kFiles; ++i)
    {
        ShareDownloadRecord rec;
        rec.m_shareId = kShareId;
        rec.m_relPath = "many/f" + std::to_string(i) + ".txt";
        rec.m_srcSize = kSrcSize;
        rec.m_srcMtime = kSrcMtime;
        rec.m_localPath = L"D:\\dl\\f.txt";
        rec.m_offset = kOffset;
        batch.push_back(rec);
        paths.push_back(rec.m_relPath);
    }
    //逐条写一万条要几分钟；一个事务里写应在几秒之内（宽松的上限，只为挡住退回逐条写）
    const ULONGLONG kBatchLimitMs = 10000;
    const ULONGLONG start = ::GetTickCount64();
    REQUIRE(store.SaveDownloadProgressBatch(batch));
    CHECK(::GetTickCount64() - start < kBatchLimitMs);
    std::vector<ShareDownloadRecord> list;
    REQUIRE(store.ListDownloads(kShareId, list));
    CHECK(list.size() == kFiles);

    //再写一次同样的记录：覆盖而不是重复
    REQUIRE(store.SaveDownloadProgressBatch(batch));
    REQUIRE(store.ListDownloads(kShareId, list));
    CHECK(list.size() == kFiles);

    const ULONGLONG start2 = ::GetTickCount64();
    REQUIRE(store.RemoveDownloads(kShareId, paths));
    CHECK(::GetTickCount64() - start2 < kBatchLimitMs);
    REQUIRE(store.ListDownloads(kShareId, list));
    CHECK(list.empty());
    CHECK(store.SaveDownloadProgressBatch(std::vector<ShareDownloadRecord>()));
    CHECK(store.RemoveDownloads(kShareId, std::vector<std::string>()));
    store.Close();
    sharetestfs::DeleteTree(dir);
}
