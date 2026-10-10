/**
 *  群文件信息缓存拉齐进度的单元测试（Suite GFS），GroupFileCacheSyncTests.cpp
 *
 *  覆盖 UI/BalloonHistory/GroupFileCacheSync.h（docs/聊天记录管理器方案.md §11.5）：
 *    GFS1 排队去重、最近拉齐过的跳过、并发上限、只认本类发出的序号；
 *    GFS2 多页累积、整群完成后给出完整列表并记下拉齐时间，force 时重新排队；
 *    GFS3 服务端报错放弃、陌生序号不认领、Forget 之后迟到的应答不认领；
 *    GFS4 发出后超时作废、发送失败放弃、「还有后续页」却是空页时按完成处理；
 *    GFS5 服务端条目转缓存表的一行。
 *  纯逻辑，不连网络、不碰本地库。框架 Catch2 v3，标签 [histfiles]。
 *  balloonwj@qq.com   2026-10-05
 */
#include "catch_amalgamated.hpp"
#include <string>
#include <vector>
#include "../UI/BalloonHistory/GroupFileCacheSync.h"

using namespace balloonhistory;

namespace {

//测试用的起始时间（Unix 毫秒）
const int64_t kT0 = 1000000;

//造一页应答：groupid、偏移、本页条数、是否还有后续页；条目 ID 按偏移顺序编号
net::GetGroupFilesResult Page(uint32_t seq, int64_t groupid, int offset, int count, bool hasmore)
{
    net::GetGroupFilesResult r;
    r.m_code = 0;
    r.m_seq = seq;
    r.m_groupid = std::to_string((long long)groupid);
    r.m_offset = offset;
    r.m_hasmore = hasmore;
    for (int i = 0; i < count; ++i)
    {
        net::GroupFileEntry e;
        e.m_entryid = std::to_string((long long)(offset + i + 1));
        e.m_fileid = "F" + e.m_entryid;
        e.m_name = "文件" + e.m_entryid + ".docx";
        e.m_size = 10;
        e.m_filetype = 1;
        e.m_uploaderid = "42";
        e.m_uploadername = "张三";
        e.m_uploadtime = 100 + i;
        r.m_files.push_back(e);
    }
    return r;
}

//三个群的 ID 列表
std::vector<int64_t> ThreeGroups()
{
    std::vector<int64_t> g;
    g.push_back(300);
    g.push_back(301);
    g.push_back(302);
    return g;
}

} // namespace

TEST_CASE("GFS1 排队去重、最近拉齐过的跳过、并发上限、只认本类发出的序号", "[histfiles]")
{
    GroupFileCacheSync sync;
    CHECK_FALSE(sync.Busy());
    std::vector<int64_t> groups = ThreeGroups();
    groups.push_back(300);   //重复的
    groups.push_back(0);     //非法的
    CHECK(sync.Enqueue(groups, kT0, false) == 3);
    CHECK(sync.Busy());
    //再排一次：都已在队列里
    CHECK(sync.Enqueue(ThreeGroups(), kT0, false) == 0);

    //同时最多两个群在拉
    int64_t gid = 0;
    int offset = -1;
    REQUIRE(sync.NextRequest(kT0, gid, offset));
    CHECK(gid == 300);
    CHECK(offset == 0);
    sync.MarkSent(300, 11, kT0);
    REQUIRE(sync.NextRequest(kT0, gid, offset));
    CHECK(gid == 301);
    sync.MarkSent(301, 12, kT0);
    CHECK_FALSE(sync.NextRequest(kT0, gid, offset));

    CHECK(sync.OwnsSeq(11));
    CHECK(sync.OwnsSeq(12));
    CHECK_FALSE(sync.OwnsSeq(13));
    CHECK_FALSE(sync.OwnsSeq(0));

    //正在拉的群再排队也跳过
    CHECK(sync.Enqueue(ThreeGroups(), kT0, false) == 0);

    //300 拉齐后让出一个名额，302 才开始
    int next = 0;
    std::vector<GroupFileCacheItem> all;
    CHECK(sync.OnPage(Page(11, 300, 0, 1, false), kT0, next, all) == kGroupFileSyncCompleted);
    REQUIRE(sync.NextRequest(kT0, gid, offset));
    CHECK(gid == 302);
}

TEST_CASE("GFS2 多页累积、整群完成后给出完整列表并记下拉齐时间，force 时重新排队", "[histfiles]")
{
    GroupFileCacheSync sync;
    std::vector<int64_t> one(1, 300);
    sync.Enqueue(one, kT0, false);
    int64_t gid = 0;
    int offset = 0;
    REQUIRE(sync.NextRequest(kT0, gid, offset));
    sync.MarkSent(gid, 21, kT0);

    int next = -1;
    std::vector<GroupFileCacheItem> all;
    CHECK(sync.OnPage(Page(21, 300, 0, 2, true), kT0, next, all) == kGroupFileSyncNextPage);
    CHECK(next == 2);
    CHECK(all.empty());
    //这一页的序号用过了，不再认领
    CHECK_FALSE(sync.OwnsSeq(21));
    sync.MarkSent(300, 22, kT0);
    CHECK(sync.OnPage(Page(22, 300, 2, 1, false), kT0 + 5, next, all) == kGroupFileSyncCompleted);
    REQUIRE(all.size() == 3);
    CHECK(all[0].m_entryid == "1");
    CHECK(all[2].m_entryid == "3");
    CHECK(all[2].m_groupid == 300);
    CHECK_FALSE(sync.Busy());

    //拉齐时间之后 kGroupFileSyncFreshMs 之内不再排队；force 时照排
    CHECK(sync.IsFresh(300, kT0 + 5));
    CHECK(sync.Enqueue(one, kT0 + 10, false) == 0);
    CHECK(sync.Enqueue(one, kT0 + 5 + kGroupFileSyncFreshMs, false) == 1);
    sync.Reset();
    sync.Enqueue(one, kT0, false);
    REQUIRE(sync.NextRequest(kT0, gid, offset));
    sync.MarkSent(gid, 23, kT0);
    sync.OnPage(Page(23, 300, 0, 0, false), kT0, next, all);
    CHECK(sync.Enqueue(one, kT0 + 1, false) == 0);
    CHECK(sync.Enqueue(one, kT0 + 1, true) == 1);
}

TEST_CASE("GFS3 服务端报错放弃、陌生序号不认领、Forget 之后迟到的应答不认领", "[histfiles]")
{
    GroupFileCacheSync sync;
    sync.Enqueue(ThreeGroups(), kT0, false);
    int64_t gid = 0;
    int offset = 0;
    REQUIRE(sync.NextRequest(kT0, gid, offset));
    sync.MarkSent(gid, 31, kT0);
    REQUIRE(sync.NextRequest(kT0, gid, offset));
    sync.MarkSent(gid, 32, kT0);

    int next = 0;
    std::vector<GroupFileCacheItem> all;
    //陌生序号：交给群聊窗
    CHECK(sync.OnPage(Page(99, 300, 0, 1, false), kT0, next, all) == kGroupFileSyncIgnored);

    //服务端报错（例如本人已不是群成员）
    net::GetGroupFilesResult err = Page(31, 300, 0, 0, false);
    err.m_code = 1;
    CHECK(sync.OnPage(err, kT0, next, all) == kGroupFileSyncFailed);
    CHECK_FALSE(sync.IsFresh(300, kT0));

    //Forget：在途的 301 与排队的 302 都拿掉，迟到的应答不再认领
    sync.Forget(301);
    sync.Forget(302);
    CHECK_FALSE(sync.OwnsSeq(32));
    CHECK(sync.OnPage(Page(32, 301, 0, 1, false), kT0, next, all) == kGroupFileSyncIgnored);
    CHECK_FALSE(sync.Busy());
}

TEST_CASE("GFS4 发出后超时作废、发送失败放弃、「还有后续页」却是空页时按完成处理", "[histfiles]")
{
    GroupFileCacheSync sync;
    std::vector<int64_t> two;
    two.push_back(300);
    two.push_back(301);
    sync.Enqueue(two, kT0, false);
    int64_t gid = 0;
    int offset = 0;
    REQUIRE(sync.NextRequest(kT0, gid, offset));
    sync.MarkSent(gid, 41, kT0);
    //发送失败：这个群放弃
    REQUIRE(sync.NextRequest(kT0, gid, offset));
    CHECK(gid == 301);
    sync.MarkSent(gid, 0, kT0);
    CHECK_FALSE(sync.OwnsSeq(0));

    //超时之前还在等
    CHECK(sync.OwnsSeq(41));
    CHECK_FALSE(sync.NextRequest(kT0 + kGroupFileSyncTimeoutMs - 1, gid, offset));
    CHECK(sync.OwnsSeq(41));
    //超时后作废，迟到的应答不认领；可以重新排队
    CHECK_FALSE(sync.NextRequest(kT0 + kGroupFileSyncTimeoutMs, gid, offset));
    CHECK_FALSE(sync.OwnsSeq(41));
    CHECK_FALSE(sync.Busy());
    CHECK(sync.Enqueue(two, kT0 + kGroupFileSyncTimeoutMs, false) == 2);

    //「还有后续页」却一条都没有：不再往下拉，按完成处理
    REQUIRE(sync.NextRequest(kT0, gid, offset));
    sync.MarkSent(gid, 42, kT0);
    int next = 0;
    std::vector<GroupFileCacheItem> all;
    CHECK(sync.OnPage(Page(42, gid, 0, 0, true), kT0, next, all) == kGroupFileSyncCompleted);
    CHECK(all.empty());
}

TEST_CASE("GFS5 服务端条目转缓存表的一行", "[histfiles]")
{
    net::GroupFileEntry e;
    e.m_entryid = "123456789012";
    e.m_fileid = "987654321098";
    e.m_name = "季度报告.PDF";
    e.m_size = 2048;
    e.m_md5 = "abc";
    e.m_filetype = 1;
    e.m_uploaderid = "10086";
    e.m_uploadername = "李四";
    e.m_uploadtime = 1700000000000LL;
    GroupFileCacheItem item = GroupFileCacheItemFromEntry(300, e);
    CHECK(item.m_groupid == 300);
    CHECK(item.m_entryid == "123456789012");
    CHECK(item.m_fileid == "987654321098");
    CHECK(item.m_name == "季度报告.PDF");
    //扩展名为空时按文件名补上
    CHECK(item.m_ext == "pdf");
    CHECK(item.m_size == 2048);
    CHECK(item.m_md5 == "abc");
    CHECK(item.m_filetype == 1);
    CHECK(item.m_uploaderid == 10086);
    CHECK(item.m_uploaderName == "李四");
    CHECK(item.m_uploadTime == 1700000000000LL);
    CHECK(item.m_localPath.empty());

    e.m_ext = "pdf";
    e.m_uploaderid = "not a number";
    item = GroupFileCacheItemFromEntry(300, e);
    CHECK(item.m_ext == "pdf");
    CHECK(item.m_uploaderid == 0);
}
