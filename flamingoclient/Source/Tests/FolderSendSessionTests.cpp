/**
 *  文件夹传输发送端状态机的纯逻辑单元测试（Suite FSS），FolderSendSessionTests.cpp
 *
 *  FolderSendSession 里能脱离网络与磁盘验证的是三块，本文件覆盖的就是它们：
 *    - 待传文件队列的分派（FolderSendFileQueue）：单条通道顺序取完、多条通道并发取时
 *      不重不漏、取空之后全部通道都能正常结束；
 *    - 续传起点的应用（FolderSendFileQueue::ApplyResumePoints）：已完整落盘的文件被跳过（0 字节文件除外）、
 *      半成品从给定偏移续、全新文件从 0 开始，以及各种非法回执一律被拒；
 *    - 流控窗口的推进（FolderSendFlowWindow）：在途分片不超过 4 片、收到回执才继续、
 *      对方回退位点时能正确回退重发、越界位点被判非法。
 *  另外覆盖清单帧的长度核对 —— 明文帧必须给加密膨胀（net::STREAM_OVERHEAD_BYTES）让出
 *  位置，否则压线的那一段清单会"构造得出来却发不出去"。
 *
 *  建通道、发帧、读盘、算 MD5、线程编排不在单元测试覆盖范围（与 FolderRelayClient 一致，
 *  靠真机联调验证），本文件不起任何网络连接、不读写任何文件。
 *
 *  框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-08-14
 */

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>
#include <set>
#include <thread>
#include <memory>

#include "../Feature/FolderTransfer/FolderSendSession.h"

//被测的第二个头文件<u>放在最后</u>：它引入 <winsock2.h>，而后者会带进 <windows.h> 里的
//min / max 宏。这两个宏一旦先定义，Catch2 与标准库里形如 std::numeric_limits<T>::min()
//的写法就会被当成宏调用，编译期报出一串 C4003"函数式宏的参数不足"。本文件只用到它里面
//的单帧上限常量 kFolderRelayMaxAppFrameBytes。
#include "../Feature/FolderTransfer/FolderRelayClient.h"

using namespace foldertransfer;

namespace {

//造一份文件列表：count 个文件，第 i 个的字节数为 (i + 1) * sizeUnit。
//路径带中文，顺带覆盖非 ASCII 相对路径在队列里的原样保留。
std::vector<FolderRelayFileEntry> makeFiles(size_t count, int64_t sizeUnit)
{
    std::vector<FolderRelayFileEntry> files;
    for (size_t i = 0; i < count; ++i)
    {
        FolderRelayFileEntry entry;
        entry.m_relpath = "子目录/文件" + std::to_string(i) + ".bin";
        entry.m_size = static_cast<int64_t>(i + 1) * sizeUnit;
        files.push_back(entry);
    }
    return files;
}

//造一条续传位点。
FolderRelayResumePoint makePoint(int64_t index, int64_t offset)
{
    FolderRelayResumePoint point;
    point.m_index = index;
    point.m_offset = offset;
    return point;
}

//一条"工作线程"在并发取件测试里的上下文：共用的队列，以及本线程取到的全部文件序号。
struct TakeWorker
{
    FolderSendFileQueue* m_queue;   //共用的待传队列（不拥有）
    std::vector<int64_t> m_taken;   //本线程取到的文件序号，按取到的先后排列

    TakeWorker()
        : m_queue(NULL)
    {
    }
};

//并发取件测试的线程体：一直取到队列取空为止。刻意不用 lambda（见仓库代码规范第 12 条）。
void takeAllProc(TakeWorker* worker)
{
    FolderSendFileTask task;
    while (worker->m_queue->TakeNext(task))
        worker->m_taken.push_back(task.m_index);
}

//把窗口推进到"在途满员"：连发 maxInflight 片，返回实际发出的片数。
size_t fillWindow(FolderSendFlowWindow& window)
{
    size_t sent = 0;
    while (window.CanSendMore())
    {
        window.OnChunkSent(window.NextChunkBytes());
        ++sent;
    }
    return sent;
}

} // namespace

// ======================== 队列的分派 ========================

TEST_CASE("FSS1 队列分派：单条通道按序取完，不重不漏")
{
    FolderSendFileQueue queue;
    queue.Reset(makeFiles(5, 100));

    CHECK(queue.TotalCount() == 5);
    CHECK(queue.PendingCount() == 5);
    CHECK(queue.TotalBytes() == 100 + 200 + 300 + 400 + 500);

    FolderSendFileTask task;
    for (int64_t i = 0; i < 5; ++i)
    {
        REQUIRE(queue.TakeNext(task));
        CHECK(task.m_index == i);
        CHECK(task.m_size == (i + 1) * 100);
        CHECK(task.m_startOffset == 0);
        CHECK(task.m_relpath == "子目录/文件" + std::to_string(i) + ".bin");
    }

    //取空之后一律返回 false，工作线程据此结束
    CHECK_FALSE(queue.TakeNext(task));
    CHECK_FALSE(queue.TakeNext(task));
    CHECK(queue.PendingCount() == 0);
}

TEST_CASE("FSS2 队列分派：空清单一个也取不到")
{
    FolderSendFileQueue queue;
    queue.Reset(std::vector<FolderRelayFileEntry>());

    CHECK(queue.TotalCount() == 0);
    CHECK(queue.TotalBytes() == 0);

    FolderSendFileTask task;
    CHECK_FALSE(queue.TakeNext(task));
}

TEST_CASE("FSS3 队列分派：四条通道并发取，合起来不重不漏且都能结束")
{
    //文件数远多于通道数，才能真正让四条线程交错取件
    const size_t kFileCount = 500;
    const size_t kLaneCount = 4;

    FolderSendFileQueue queue;
    queue.Reset(makeFiles(kFileCount, 1));

    std::vector<TakeWorker> workers(kLaneCount);
    std::vector<std::unique_ptr<std::thread> > threads;
    for (size_t i = 0; i < kLaneCount; ++i)
    {
        workers[i].m_queue = &queue;
        threads.push_back(std::unique_ptr<std::thread>(new std::thread(takeAllProc, &workers[i])));
    }
    //每条线程都取到 TakeNext 返回 false 才退出，能 join 上就说明"取完后正确结束"
    for (size_t i = 0; i < kLaneCount; ++i)
        threads[i]->join();

    std::set<int64_t> all;
    size_t total = 0;
    for (size_t i = 0; i < kLaneCount; ++i)
    {
        total += workers[i].m_taken.size();
        for (size_t j = 0; j < workers[i].m_taken.size(); ++j)
            all.insert(workers[i].m_taken[j]);
    }

    //取到的总条数与去重后的条数都等于文件数：既没有两条通道取到同一个文件，也没有漏掉谁
    CHECK(total == kFileCount);
    CHECK(all.size() == kFileCount);
    for (int64_t i = 0; i < static_cast<int64_t>(kFileCount); ++i)
        CHECK(all.count(i) == 1);

    CHECK(queue.PendingCount() == 0);
}

// ======================== 续传起点的应用 ========================

TEST_CASE("FSS4 续传起点：已完成的跳过、半成品接着传、全新的从 0 开始")
{
    //三个文件：0 号 100 字节、1 号 200 字节、2 号 300 字节
    FolderSendFileQueue queue;
    queue.Reset(makeFiles(3, 100));

    FolderRelayManifestAck ack;
    ack.m_totalfiles = 3;
    ack.m_points.push_back(makePoint(0, 100));   //已完整落盘，本次应跳过
    ack.m_points.push_back(makePoint(1, 50));    //半成品，从 50 接着传
    //2 号不出现在位点表里，按 0 处理（位点表是稀疏的）
    REQUIRE(queue.ApplyResumePoints(ack));

    CHECK(queue.SkippedCount() == 1);
    CHECK(queue.ResumedBytes() == 150);
    CHECK(queue.PendingCount() == 2);
    CHECK(queue.TotalCount() == 3);

    FolderSendFileTask task;
    REQUIRE(queue.TakeNext(task));
    CHECK(task.m_index == 1);
    CHECK(task.m_startOffset == 50);
    CHECK(task.m_size == 200);

    REQUIRE(queue.TakeNext(task));
    CHECK(task.m_index == 2);
    CHECK(task.m_startOffset == 0);
    CHECK(task.m_size == 300);

    CHECK_FALSE(queue.TakeNext(task));
}

TEST_CASE("FSS5 续传起点：首次传输的空位点表等于全部从 0 开始")
{
    FolderSendFileQueue queue;
    queue.Reset(makeFiles(4, 10));

    FolderRelayManifestAck ack;
    ack.m_totalfiles = 4;
    REQUIRE(queue.ApplyResumePoints(ack));

    CHECK(queue.SkippedCount() == 0);
    CHECK(queue.ResumedBytes() == 0);
    CHECK(queue.PendingCount() == 4);

    FolderSendFileTask task;
    for (int64_t i = 0; i < 4; ++i)
    {
        REQUIRE(queue.TakeNext(task));
        CHECK(task.m_index == i);
        CHECK(task.m_startOffset == 0);
    }
}

TEST_CASE("FSS6 续传起点：全部文件都已完成时队列直接取空")
{
    FolderSendFileQueue queue;
    queue.Reset(makeFiles(3, 100));

    FolderRelayManifestAck ack;
    ack.m_totalfiles = 3;
    ack.m_points.push_back(makePoint(0, 100));
    ack.m_points.push_back(makePoint(1, 200));
    ack.m_points.push_back(makePoint(2, 300));
    REQUIRE(queue.ApplyResumePoints(ack));

    CHECK(queue.SkippedCount() == 3);
    CHECK(queue.ResumedBytes() == 600);
    CHECK(queue.ResumedBytes() == queue.TotalBytes());
    CHECK(queue.PendingCount() == 0);

    FolderSendFileTask task;
    CHECK_FALSE(queue.TakeNext(task));
}

TEST_CASE("FSS7 续传起点：0 字节文件照样要发（走 begin / end），不被当作已完成跳过")
{
    std::vector<FolderRelayFileEntry> files;
    FolderRelayFileEntry empty;
    empty.m_relpath = "空文件.txt";
    empty.m_size = 0;
    files.push_back(empty);

    FolderRelayFileEntry normal;
    normal.m_relpath = "有内容.bin";
    normal.m_size = 128;
    files.push_back(normal);

    FolderSendFileQueue queue;
    queue.Reset(files);

    FolderRelayManifestAck ack;
    ack.m_totalfiles = 2;
    REQUIRE(queue.ApplyResumePoints(ack));

    //0 字节文件的起传偏移 0 虽然等于它的长度，也不算已完成：接收端要靠它的 begin / end 才会建出文件并计为完成，
    //跳过它会让接收端一直等下去（2026-10-04 修正，原先的预期是跳过）
    CHECK(queue.SkippedCount() == 0);
    CHECK(queue.PendingCount() == 2);

    FolderSendFileTask task;
    REQUIRE(queue.TakeNext(task));
    CHECK(task.m_index == 0);
    CHECK(task.m_size == 0);
    REQUIRE(queue.TakeNext(task));
    CHECK(task.m_index == 1);
    CHECK_FALSE(queue.TakeNext(task));

    //续传回执里给 0 字节文件的位点（0，即它的长度）：照样要发
    FolderSendFileQueue again;
    again.Reset(files);
    FolderRelayManifestAck resumed;
    resumed.m_totalfiles = 2;
    FolderRelayResumePoint point;
    point.m_index = 0;
    point.m_offset = 0;
    resumed.m_points.push_back(point);
    REQUIRE(again.ApplyResumePoints(resumed));
    CHECK(again.SkippedCount() == 0);
    CHECK(again.PendingCount() == 2);
}

TEST_CASE("FSS8 续传起点：非法回执一律被拒")
{
    const std::vector<FolderRelayFileEntry> files = makeFiles(3, 100);

    SECTION("文件总数与本地清单对不上")
    {
        FolderSendFileQueue queue;
        queue.Reset(files);
        FolderRelayManifestAck ack;
        ack.m_totalfiles = 4;
        CHECK_FALSE(queue.ApplyResumePoints(ack));
    }

    SECTION("文件序号越界")
    {
        FolderSendFileQueue queue;
        queue.Reset(files);
        FolderRelayManifestAck ack;
        ack.m_totalfiles = 3;
        ack.m_points.push_back(makePoint(3, 0));
        CHECK_FALSE(queue.ApplyResumePoints(ack));
    }

    SECTION("文件序号为负")
    {
        FolderSendFileQueue queue;
        queue.Reset(files);
        FolderRelayManifestAck ack;
        ack.m_totalfiles = 3;
        ack.m_points.push_back(makePoint(-1, 0));
        CHECK_FALSE(queue.ApplyResumePoints(ack));
    }

    SECTION("偏移为负")
    {
        FolderSendFileQueue queue;
        queue.Reset(files);
        FolderRelayManifestAck ack;
        ack.m_totalfiles = 3;
        ack.m_points.push_back(makePoint(0, -1));
        CHECK_FALSE(queue.ApplyResumePoints(ack));
    }

    SECTION("偏移超过文件长度")
    {
        FolderSendFileQueue queue;
        queue.Reset(files);
        FolderRelayManifestAck ack;
        ack.m_totalfiles = 3;
        ack.m_points.push_back(makePoint(0, 101));
        CHECK_FALSE(queue.ApplyResumePoints(ack));
    }

    SECTION("同一个序号出现两次")
    {
        FolderSendFileQueue queue;
        queue.Reset(files);
        FolderRelayManifestAck ack;
        ack.m_totalfiles = 3;
        ack.m_points.push_back(makePoint(1, 10));
        ack.m_points.push_back(makePoint(1, 20));
        CHECK_FALSE(queue.ApplyResumePoints(ack));
    }
}

// ======================== 流控窗口 ========================

TEST_CASE("FSS9 流控窗口：在途分片不超过 4 片，收到回执才能继续")
{
    //10 片的文件：分片 1000 字节、共 10000 字节，方便手算偏移
    const int64_t kChunk = 1000;
    const int64_t kSize = 10000;

    FolderSendFlowWindow window;
    window.Begin(kSize, 0, static_cast<size_t>(kChunk), kFolderSendMaxInflightChunks);

    //先把窗口填满：只能发出 4 片，第 5 片必须等回执
    const size_t sent = fillWindow(window);
    CHECK(sent == kFolderSendMaxInflightChunks);
    CHECK(window.InflightCount() == kFolderSendMaxInflightChunks);
    CHECK_FALSE(window.CanSendMore());
    CHECK(window.AckedOffset() == 0);
    CHECK(window.NextSendOffset() == kChunk * 4);

    //收到第一片的回执：在途减一，可以再发一片
    CHECK(window.OnChunkAck(kChunk) == kFolderSendAckAdvanced);
    CHECK(window.InflightCount() == kFolderSendMaxInflightChunks - 1);
    CHECK(window.AckedOffset() == kChunk);
    REQUIRE(window.CanSendMore());

    window.OnChunkSent(static_cast<size_t>(window.NextChunkBytes()));
    CHECK(window.InflightCount() == kFolderSendMaxInflightChunks);
    CHECK_FALSE(window.CanSendMore());
    CHECK(window.NextSendOffset() == kChunk * 5);
}

TEST_CASE("FSS10 流控窗口：逐片回执直到全部确认，末片长度按剩余字节算")
{
    //2500 字节 / 1000 字节一片 = 两整片加一个 500 字节的尾片
    const size_t kChunk = 1000;
    const int64_t kSize = 2500;

    FolderSendFlowWindow window;
    window.Begin(kSize, 0, kChunk, kFolderSendMaxInflightChunks);

    CHECK(window.NextChunkBytes() == kChunk);
    window.OnChunkSent(kChunk);
    CHECK(window.NextChunkBytes() == kChunk);
    window.OnChunkSent(kChunk);
    //尾片只剩 500 字节
    CHECK(window.NextChunkBytes() == 500);
    window.OnChunkSent(500);

    //字节已全部发出，但还没被确认，此时既不能再发也还没排空
    CHECK(window.AllBytesSent());
    CHECK_FALSE(window.CanSendMore());
    CHECK_FALSE(window.IsDrained());

    CHECK(window.OnChunkAck(1000) == kFolderSendAckAdvanced);
    CHECK_FALSE(window.IsDrained());
    CHECK(window.OnChunkAck(2000) == kFolderSendAckAdvanced);
    CHECK_FALSE(window.IsDrained());
    CHECK(window.OnChunkAck(2500) == kFolderSendAckAdvanced);

    //全部确认且无在途分片，这时才可以发 ft_file_end
    CHECK(window.IsDrained());
    CHECK(window.AckedOffset() == kSize);
    CHECK(window.InflightCount() == 0);
}

TEST_CASE("FSS11 流控窗口：对方回退位点时回退重发")
{
    const size_t kChunk = 1000;
    const int64_t kSize = 10000;

    FolderSendFlowWindow window;
    window.Begin(kSize, 0, kChunk, kFolderSendMaxInflightChunks);
    CHECK(fillWindow(window) == kFolderSendMaxInflightChunks);
    CHECK(window.NextSendOffset() == 4000);

    //对方回执说只落盘了 300 字节（第一片没写完），发送端必须以它为准回退
    CHECK(window.OnChunkAck(300) == kFolderSendAckRepositioned);
    CHECK(window.AckedOffset() == 300);
    CHECK(window.NextSendOffset() == 300);
    //在途记账全部作废，窗口重新腾空
    CHECK(window.InflightCount() == 0);
    REQUIRE(window.CanSendMore());

    //从回退后的位点重新发，其后一切照常推进
    CHECK(window.NextChunkBytes() == kChunk);
    window.OnChunkSent(kChunk);
    CHECK(window.NextSendOffset() == 1300);
    CHECK(window.OnChunkAck(1300) == kFolderSendAckAdvanced);
    CHECK(window.AckedOffset() == 1300);
}

TEST_CASE("FSS12 流控窗口：回退之后迟到的旧回执不会让位点倒退成负数或乱跳")
{
    const size_t kChunk = 1000;
    const int64_t kSize = 10000;

    FolderSendFlowWindow window;
    window.Begin(kSize, 0, kChunk, kFolderSendMaxInflightChunks);
    fillWindow(window);

    //先回退到 300
    CHECK(window.OnChunkAck(300) == kFolderSendAckRepositioned);
    CHECK(window.NextSendOffset() == 300);

    //随后那三片的迟到回执陆续到达：在途记账已经清空，一律按重新定位处理，
    //定位到对方给的位点，不会把窗口带到文件长度之外
    CHECK(window.OnChunkAck(2000) == kFolderSendAckRepositioned);
    CHECK(window.AckedOffset() == 2000);
    CHECK(window.NextSendOffset() == 2000);
    CHECK(window.InflightCount() == 0);
    CHECK(window.NextChunkBytes() == kChunk);
}

TEST_CASE("FSS13 流控窗口：越界回执判非法且窗口状态不变")
{
    const size_t kChunk = 1000;
    const int64_t kSize = 5000;

    FolderSendFlowWindow window;
    window.Begin(kSize, 0, kChunk, kFolderSendMaxInflightChunks);
    window.OnChunkSent(kChunk);

    SECTION("位点为负")
    {
        CHECK(window.OnChunkAck(-1) == kFolderSendAckInvalid);
    }

    SECTION("位点大于文件长度")
    {
        CHECK(window.OnChunkAck(kSize + 1) == kFolderSendAckInvalid);
    }

    //判非法之后窗口不受影响：这一片仍算在途，起传偏移也没动
    CHECK(window.AckedOffset() == 0);
    CHECK(window.NextSendOffset() == kChunk);
    CHECK(window.InflightCount() == 1);
}

TEST_CASE("FSS14 流控窗口：续传起点非 0 时从该位点开始推进")
{
    const size_t kChunk = 1000;
    const int64_t kSize = 5000;
    const int64_t kStart = 2000;

    FolderSendFlowWindow window;
    window.Begin(kSize, kStart, kChunk, kFolderSendMaxInflightChunks);

    CHECK(window.AckedOffset() == kStart);
    CHECK(window.NextSendOffset() == kStart);
    CHECK_FALSE(window.IsDrained());
    REQUIRE(window.CanSendMore());

    //剩下 3000 字节，正好三片，一次填不满四片的窗口
    CHECK(fillWindow(window) == 3);
    CHECK(window.AllBytesSent());
    CHECK(window.OnChunkAck(3000) == kFolderSendAckAdvanced);
    CHECK(window.OnChunkAck(4000) == kFolderSendAckAdvanced);
    CHECK(window.OnChunkAck(5000) == kFolderSendAckAdvanced);
    CHECK(window.IsDrained());
}

TEST_CASE("FSS15 流控窗口：0 字节文件一开始就是排空状态")
{
    FolderSendFlowWindow window;
    window.Begin(0, 0, kFolderRelayChunkBytes, kFolderSendMaxInflightChunks);

    //一片都不用发，直接可以发 ft_file_end
    CHECK_FALSE(window.CanSendMore());
    CHECK(window.AllBytesSent());
    CHECK(window.IsDrained());
    CHECK(window.AckedOffset() == 0);
    CHECK(window.InflightCount() == 0);
}

TEST_CASE("FSS16 流控窗口：默认分片与窗口上限即 512 KiB × 4 = 2 MiB")
{
    //方案 5.3 定死的两个数：分片 512 KiB、在途至多 4 片
    CHECK(kFolderRelayChunkBytes == 512u * 1024u);
    CHECK(kFolderSendMaxInflightChunks == 4u);

    FolderSendFlowWindow window;
    window.Begin(100 * 1024 * 1024, 0, kFolderRelayChunkBytes, kFolderSendMaxInflightChunks);
    CHECK(fillWindow(window) == kFolderSendMaxInflightChunks);
    //在途字节数正好 2 MiB
    CHECK(window.NextSendOffset() - window.AckedOffset() == 2 * 1024 * 1024);
}

// ======================== 清单帧的长度核对 ========================

TEST_CASE("FSS17 清单帧核对：超出单帧上限的那一段能被指出来")
{
    std::vector<std::string> frames;
    frames.push_back(std::string(10, 'a'));
    frames.push_back(std::string(20, 'b'));
    frames.push_back(std::string(30, 'c'));

    size_t bad = 12345;   //先填一个显眼的值，验证成功时会被清零
    CHECK(CheckManifestFramesFitLimit(frames, 30, bad));
    CHECK(bad == 0);

    //上限压到 25：第 2 段（下标 2，30 字节）超限
    CHECK_FALSE(CheckManifestFramesFitLimit(frames, 25, bad));
    CHECK(bad == 2);

    //上限压到 15：第 1 段（下标 1，20 字节）先超限，返回的是第一个超限的那段
    CHECK_FALSE(CheckManifestFramesFitLimit(frames, 15, bad));
    CHECK(bad == 1);

    //空清单帧列表恒为通过
    CHECK(CheckManifestFramesFitLimit(std::vector<std::string>(), 1, bad));
}

TEST_CASE("FSS18 清单帧核对：默认分段预算下每一帧都给加密膨胀留足了余量")
{
    //明文上限比中转单帧上限少 net::STREAM_OVERHEAD_BYTES 字节，压线的帧会"构造得出来
    //却发不出去"。这里用默认预算切一份大清单，核对每一帧都落在明文上限之内
    CHECK(kFolderRelayMaxAppFrameBytes == kFolderRelayMaxBodyBytes - net::STREAM_OVERHEAD_BYTES);

    const size_t kDirCount = 3000;
    const size_t kFileCount = 8000;

    std::vector<std::string> dirs;
    for (size_t i = 0; i < kDirCount; ++i)
        dirs.push_back("部门资料" + std::to_string(i) + "/年度报告");

    std::vector<FolderRelayFileEntry> files;
    for (size_t i = 0; i < kFileCount; ++i)
    {
        FolderRelayFileEntry entry;
        entry.m_relpath = "部门资料" + std::to_string(i % kDirCount) + "/年度报告/明细表"
                          + std::to_string(i) + ".xlsx";
        entry.m_size = static_cast<int64_t>(i) * 4096 + 1;
        files.push_back(entry);
    }

    std::vector<std::string> frames;
    REQUIRE(relayframing::BuildManifestFrames("企业年度资料", dirs, files,
                                              kFolderRelayManifestSegmentBytes, frames));
    REQUIRE(frames.size() > 1);   //这个规模必然要分成多段，否则这条用例就没在测分段

    size_t bad = 0;
    CHECK(CheckManifestFramesFitLimit(frames, kFolderRelayMaxAppFrameBytes, bad));
}
