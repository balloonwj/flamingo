/**
 *  单聊「发送文件夹」的发送端状态机，FolderSendSession.h
 *
 *  本模块负责一次文件夹传输中<u>从拿到中转参数开始</u>的全过程：发起邀请、等待对方
 *  同意这些信令交互由上层（chatserver 主连接那条链路）完成，本模块不参与。拿到
 *  cmd_folder_relay_info 下发的中转地址、端口、令牌与数据通道条数之后，本模块依次做
 *  六件事（方案见 docs/单聊文件夹传输方案.md 第 4.2、4.3、5.3、7、8 节）：
 *
 *    1. 建通道 —— 用同一枚令牌建 1 条控制通道（lane 0）与 N 条数据通道（lane 1..N），
 *       每条通道一个 FolderRelayClient 实例；
 *    2. 发清单 —— 把上层给的清单用 relayframing::BuildManifestFrames 分段后在控制通道发出；
 *    3. 等目录就绪 —— 阻塞等待接收端回 ft_manifest_ack。接收端把目录<u>全部建好之后</u>
 *       才会回，回执里带每个文件的续传起点。<u>收到之前一个字节都不发</u>，让"没有写
 *       权限、路径过长、磁盘只读"这类环境问题在传字节之前就暴露；
 *    4. 并行传文件 —— 待传文件排成一个队列，每条数据通道一个工作线程，取队首未传文件、
 *       在本通道上把它整个传完、再取下一个。文件级并行，单个大文件不切段；
 *    5. 收尾 —— 全部文件完成后在控制通道发 ft_complete，向上层回报成功；
 *    6. 失败即整体失败 —— 任一文件任一步出错（读盘失败、通道断开、对方回文件校验不
 *       通过、收到 ft_abort、用户取消）即停下全部通道、发 ft_abort，向上层回报失败原因。
 *       按产品定稿<u>不做自动重连</u>：失败就如实告知双方，由用户手动重试。
 *
 *  线程模型：Start() 立即返回，全部工作在本模块自建的线程上完成。
 *    · 驱动线程 1 条 —— 建通道、发清单、等目录就绪、拉起并回收工作线程、收尾；
 *    · 工作线程 N 条 —— 每条数据通道一条，串行地"取一个文件、传完、再取下一个"；
 *    · 接收线程 N+1 条 —— 由各 FolderRelayClient 自建，把对端帧交回本模块。
 *  <u>三类回调（进度、完成、失败）都在上述线程上触发，不在 UI 线程上</u> —— 完成与失败
 *  在驱动线程上，进度则可能来自工作线程，也可能来自某条通道的接收线程（分片回执一到就
 *  更新进度）。上层实现 IFolderSendSessionHandler 时须自行把数据拷出后转投 UI 线程（典型
 *  做法是 PostMessage 给窗口）。本模块<u>不</u>引用任何窗口类、也不自行 PostMessage，是为了
 *  让它能脱离界面单独测试与复用。
 *
 *  可脱离网络与磁盘验证的两块纯逻辑做成了本头文件里的内联类，由
 *  Source/Tests/FolderSendSessionTests.cpp（Suite FSS）覆盖：
 *    · FolderSendFileQueue —— 待传文件队列的分派与续传起点的应用；
 *    · FolderSendFlowWindow —— 每条数据通道各自的流控窗口。
 *  做成内联而不是放进 .cpp，与 FolderRelayClient.h 里 folderrelay 命名空间的做法同因：
 *  .cpp 要用 FolderRelayClient 与 CIULog，而测试工程不编译那两者的实现，放 .cpp 里
 *  测试工程链接不到。
 *
 *  典型用法：
 *      foldertransfer::FolderSendSessionParams params;
 *      params.m_relayIp    = info.m_relayip;
 *      params.m_relayPort  = info.m_relayport;
 *      params.m_relayToken = info.m_token;
 *      params.m_dataLanes  = info.m_lanes;
 *      params.m_rootPath   = L"D:\\work\\报告";
 *      params.m_manifest   = scanResult.m_manifest;   //ScanFolderTree 的产物
 *
 *      foldertransfer::FolderSendSession session;
 *      if (!session.Start(params, this))
 *          ...（参数非法，未起线程）
 *      ...
 *      session.Cancel();   //用户点了取消，按失败收场、错误码 error_code_folder_canceled
 *      session.Stop();     //阻塞等全部线程退出；析构函数也会调它
 *
 *  balloonwj@qq.com   2026-08-14
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>
#include <deque>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <chrono>

#include "FolderManifest.h"       // FolderManifest / MakeLongPath / Utf8ToUtf16
#include "FolderRelayFraming.h"   // 应用帧结构体与 kFolderRelayChunkBytes 等常量
#include "FolderTransferHooks.h"  // IFolderSendHooks：文件共享的传输子会话用来定制前置交换与清单
#include "FolderLaneTransport.h"  // IFolderLaneTransportFactory：通道走中转还是 P2P（P2P 方案第 5 期）

namespace foldertransfer {

// ======================== 常量 ========================

//每条数据通道允许的在途分片数上限：4 片。512 KiB × 4 = 2 MiB，即一条通道最多有 2 MiB
//的数据发出去而尚未收到回执。出处：docs/单聊文件夹传输方案.md 5.3 末段。
const size_t kFolderSendMaxInflightChunks = 4;

//数据通道条数的下限：1 条。服务端下发的 lanes 若为 0 或负数（配置写错、老版本服务端），
//按 1 条处理而不是直接失败 —— 一条数据通道也能把文件夹传完，只是慢些。
const int kFolderSendMinDataLanes = 1;

//等待 ft_manifest_ack 的超时，单位：秒。取 300 秒（5 分钟）。
//接收端要先把整棵目录树建出来才回这个回执，服务端配置的文件数上限 foldermaxfiles 默认
//为 10000，对应的目录数在机械硬盘上逐级 CreateDirectory 可能耗时数十秒；留 5 分钟是让
//正常的大目录不会被误判超时，同时挡住"对方卡死后永远等下去"。
const int kFolderSendManifestAckTimeoutSec = 300;

//等待某一片 ft_chunk_ack 的超时，单位：秒。取 60 秒。
//一片只有 512 KiB，接收端写盘加增量 MD5 的正常耗时在毫秒级；60 秒还没有任何一片被回执
//即可判定链路或对端已经不工作了。本超时是"距上一次窗口推进"的间隔，不是单片的绝对时限。
const int kFolderSendChunkAckTimeoutSec = 60;

//等待某个文件 ft_file_result 的超时，单位：秒。取 120 秒。
//接收端的 MD5 是边写边增量算的，最后一步只剩关闭文件、去掉 .part 后缀这两件事，正常在
//秒级完成；留 120 秒是给大文件落盘时操作系统刷缓存留出余量。
const int kFolderSendFileResultTimeoutSec = 120;

//两次进度回调之间的最小间隔，单位：毫秒。取 200 毫秒（每秒至多 5 次）。
//每片 512 KiB 都回调一次会在高速链路上把 UI 线程的消息队列塞满，而人眼也分辨不出比这
//更密的刷新。文件完成、整体完成这类事件不受本间隔限制，一律立即回调。
const int kFolderSendProgressIntervalMs = 200;

//流式计算文件 MD5 时的读盘缓冲字节数：256 KiB。与 net/FileTransfer.cpp 里上传路径的
//做法同口径（分片读盘、不把整文件读进内存）。取值只影响读盘的系统调用次数，不影响结果。
const size_t kFolderSendMd5ReadBufBytes = 256u * 1024u;

// ======================== 纯逻辑：待传文件队列 ========================

/**
 *  待传队列里的一个文件任务。
 *  m_index 即整份清单文件列表的下标，也是后续 ft_file_begin / ft_file_chunk 等帧里的
 *  文件序号；发送端与接收端靠它对上同一个文件，故不能重排、不能压缩。
 */
struct FolderSendFileTask
{
    int64_t     m_index;         // 文件序号（清单文件列表的下标，从 0 起）
    std::string m_relpath;       // 相对根文件夹的路径（UTF-8，正斜杠分隔），用于拼本地路径与写日志
    int64_t     m_size;          // 文件字节数（0 字节文件合法，照样要走一遍 begin/end 流程）
    int64_t     m_startOffset;   // 本次起传偏移：首次传输为 0，续传时为接收端给出的位点

    FolderSendFileTask()
        : m_index(0)
        , m_size(0)
        , m_startOffset(0)
    {
    }
};

/**
 *  待传文件队列：文件级并行的分派器。
 *
 *  N 条数据通道的工作线程共用同一个队列实例，各自调用 TakeNext 取下一个未传文件，取完
 *  即返回 false 结束。不预先按大小把文件分给各通道，谁先传完谁先接下一个，天然负载均衡
 *  （方案 4.3）。队列内部自带一把锁，故<u>可以被多条工作线程并发调用</u>。
 *
 *  续传起点由 ApplyResumePoints 一次性应用：已在接收端完整落盘的文件（位点等于文件长度）
 *  直接从队列里跳过，半成品从给定偏移续传，其余从 0 开始。0 字节文件不跳过，见 IsAlreadyComplete。
 *
 *  生命周期：一次文件夹传输一个实例，Reset → ApplyResumePoints → 若干次 TakeNext。
 */
class FolderSendFileQueue
{
public:
    FolderSendFileQueue()
        : m_cursor(0)
        , m_totalBytes(0)
        , m_resumedBytes(0)
        , m_skippedCount(0)
    {
    }

    /**
     *  用清单的文件列表重建队列，全部文件的起传偏移置 0。
     *  @param files 清单里的文件条目，下标即文件序号；顺序<u>必须</u>与发给接收端的清单一致
     *  副作用：清空上一次的全部状态（游标、续传位点、统计值）。
     */
    void Reset(const std::vector<FolderRelayFileEntry>& files)
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        m_tasks.clear();
        m_cursor = 0;
        m_totalBytes = 0;
        m_resumedBytes = 0;
        m_skippedCount = 0;

        m_tasks.reserve(files.size());
        for (size_t i = 0; i < files.size(); ++i)
        {
            FolderSendFileTask task;
            task.m_index = static_cast<int64_t>(i);
            task.m_relpath = files[i].m_relpath;
            task.m_size = files[i].m_size;
            task.m_startOffset = 0;
            m_tasks.push_back(task);
            m_totalBytes += files[i].m_size;
        }
    }

    /**
     *  应用接收端在 ft_manifest_ack 里给出的续传起点。
     *  必须在 Reset 之后、任何一次 TakeNext 之前调用一次；回执里没有出现的文件一律按
     *  偏移 0 处理（位点表是稀疏的，见 FolderRelayManifestAck 的注释）。
     *  @param ack 接收端回执；m_totalfiles 须与队列里的文件数相等，否则说明两端对的不是
     *             同一份清单
     *  @return 全部位点合法返回 true；文件总数对不上、序号越界、偏移为负、偏移超过文件
     *          长度、同一个序号出现两次，一律返回 false（调用方应据此判整体失败，错误码
     *          取 error_code_bad_request）。返回 false 时已应用的部分位点<u>不</u>回滚，
     *          调用方本就不该在失败后继续用这个队列
     */
    bool ApplyResumePoints(const FolderRelayManifestAck& ack)
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        if (ack.m_totalfiles != static_cast<int64_t>(m_tasks.size()))
            return false;

        //每个序号只允许出现一次：重复位点意味着对端状态自相矛盾，宁可整体失败也不猜
        std::vector<bool> seen(m_tasks.size(), false);
        for (size_t i = 0; i < ack.m_points.size(); ++i)
        {
            const int64_t index = ack.m_points[i].m_index;
            const int64_t offset = ack.m_points[i].m_offset;
            if (index < 0 || index >= static_cast<int64_t>(m_tasks.size()))
                return false;
            if (seen[static_cast<size_t>(index)])
                return false;
            seen[static_cast<size_t>(index)] = true;

            FolderSendFileTask& task = m_tasks[static_cast<size_t>(index)];
            if (offset < 0 || offset > task.m_size)
                return false;
            task.m_startOffset = offset;
        }

        //统计：已落在接收端的字节数计入进度基数；已完整落盘的文件直接算完成
        m_resumedBytes = 0;
        m_skippedCount = 0;
        for (size_t i = 0; i < m_tasks.size(); ++i)
        {
            m_resumedBytes += m_tasks[i].m_startOffset;
            if (IsAlreadyComplete(m_tasks[i]))
                ++m_skippedCount;
        }
        return true;
    }

    /**
     *  取下一个待传文件。<u>线程安全</u>，N 条工作线程并发调用不会取到同一个文件，也不会
     *  漏掉任何一个：内部游标只前进不后退，已完整落盘的文件在这里被跳过。
     *  @param out [出参] 取到的任务；返回 false 时不改动它
     *  @return 取到返回 true；队列已取空返回 false（工作线程据此结束）
     */
    bool TakeNext(FolderSendFileTask& out)
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        while (m_cursor < m_tasks.size())
        {
            const size_t pos = m_cursor;
            ++m_cursor;
            //起传偏移已等于文件长度：接收端上次已完整收下，本次跳过不再传
            if (IsAlreadyComplete(m_tasks[pos]))
                continue;
            out = m_tasks[pos];
            return true;
        }
        return false;
    }

    /**
     *  一个文件是否已在接收端完整落盘、本次可以跳过：起传偏移已到文件末尾。
     *  0 字节文件例外，永远不算已完成：接收端要靠它的 ft_file_begin / ft_file_end 才会建出这个文件并计为完成，
     *  跳过它会让接收端一直等下去（2026-10-04 P2P 第 5 期的用例 FP2P4 查出）。续传时把已收过的空文件再发一遍
     *  没有副作用，接收端按「重传一个已完成的文件」重新收下。
     *  @param task 待传文件
     *  @return 可以跳过返回 true
     */
    static bool IsAlreadyComplete(const FolderSendFileTask& task)
    {
        return task.m_size > 0 && task.m_startOffset >= task.m_size;
    }

    //队列里的文件总数（含被续传位点跳过的那些）。可在任意线程调用。
    size_t TotalCount() const
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        return m_tasks.size();
    }

    //尚未被 TakeNext 取走、且确实需要传的文件数。仅供日志与断言，取数瞬间即可能变化。
    size_t PendingCount() const
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        size_t count = 0;
        for (size_t i = m_cursor; i < m_tasks.size(); ++i)
        {
            if (!IsAlreadyComplete(m_tasks[i]))
                ++count;
        }
        return count;
    }

    //因续传位点已等于文件长度而被跳过的文件数。进度里的"已完成文件数"以它为基数。
    size_t SkippedCount() const
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        return m_skippedCount;
    }

    //全部文件的字节数合计（不扣除续传已完成的部分）。进度分母用它。
    int64_t TotalBytes() const
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        return m_totalBytes;
    }

    //续传位点合计，即本次开始前就已经落在接收端的字节数。进度分子的初值用它。
    int64_t ResumedBytes() const
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        return m_resumedBytes;
    }

private:
    mutable std::mutex              m_mtx;           // 保护以下全部成员；mutable 是为了让几个 const 取数方法也能加锁
    std::vector<FolderSendFileTask> m_tasks;         // 全部文件任务，下标即文件序号，构建后只改 m_startOffset
    size_t                          m_cursor;        // 分派游标：下一个待考察的任务下标，只前进不后退
    int64_t                         m_totalBytes;    // 全部文件字节数合计，Reset 时算好
    int64_t                         m_resumedBytes;  // 续传位点合计，ApplyResumePoints 时算好
    size_t                          m_skippedCount;  // 因已完整落盘而跳过的文件数，ApplyResumePoints 时算好
};

// ======================== 纯逻辑：流控窗口 ========================

/**
 *  收到一个 ft_chunk_ack 之后窗口的处置结果。
 */
enum FolderSendAckResult
{
    //回执位点与在途最早那片的末尾吻合，窗口正常向前推进一片
    kFolderSendAckAdvanced = 0,
    //回执位点与预期不符，按对方给的位点重新定位：起传偏移回退到该位点，在途记账全部作废，
    //此后从该位点重新读盘发送。接收端给的 nextoffset 是权威续传位点，发送端不得自行累加
    kFolderSendAckRepositioned = 1,
    //回执位点越界（为负，或大于文件长度），属于协议错误，调用方应判整体失败
    kFolderSendAckInvalid = -1,
};

/**
 *  一条数据通道的流控窗口：管住"在途分片不超过 kFolderSendMaxInflightChunks 片"，并按
 *  接收端回执推进（或回退）起传偏移。
 *
 *  两个偏移的分工要分清：m_sendOffset 是"下一片该从哪儿读盘发出"，随发送前进；
 *  m_ackedOffset 是"接收端确认已落盘到哪儿"，随回执前进，进度显示与断点续传都以它为准。
 *  正常情况下 m_ackedOffset <= m_sendOffset，两者之差即在途字节数。
 *
 *  <u>本类自身不加锁</u>：它同时被本通道的工作线程（发送侧）与接收线程（回执侧）访问，
 *  由持有它的通道上下文用同一把互斥量保护，故这里不再套一层锁。
 *
 *  生命周期：每条数据通道一个实例，每开始传一个文件调一次 Begin 重置。
 */
class FolderSendFlowWindow
{
public:
    FolderSendFlowWindow()
        : m_fileSize(0)
        , m_sendOffset(0)
        , m_ackedOffset(0)
        , m_chunkBytes(kFolderRelayChunkBytes)
        , m_maxInflight(kFolderSendMaxInflightChunks)
    {
    }

    /**
     *  开始一个文件，重置窗口。
     *  @param fileSize    文件总字节数，须 >= 0
     *  @param startOffset 本次起传偏移（续传位点），须落在 [0, fileSize]
     *  @param chunkBytes  单片数据字节数，须 > 0，典型取 kFolderRelayChunkBytes
     *  @param maxInflight 在途分片数上限，须 > 0，典型取 kFolderSendMaxInflightChunks
     *  副作用：清空在途记账。
     */
    void Begin(int64_t fileSize, int64_t startOffset, size_t chunkBytes, size_t maxInflight)
    {
        m_fileSize = fileSize;
        m_sendOffset = startOffset;
        m_ackedOffset = startOffset;
        m_chunkBytes = chunkBytes;
        m_maxInflight = maxInflight;
        m_inflightEnds.clear();
    }

    //现在能不能再发一片：在途未满且还有字节没发出去。返回 false 时调用方应等回执。
    bool CanSendMore() const
    {
        return m_inflightEnds.size() < m_maxInflight && m_sendOffset < m_fileSize;
    }

    //下一片该从文件的哪个偏移读起。仅在 CanSendMore() 为 true 时有意义。
    int64_t NextSendOffset() const
    {
        return m_sendOffset;
    }

    //下一片该读多少字节：正常是 m_chunkBytes，文件末尾不足一片时是剩下那点。
    //仅在 CanSendMore() 为 true 时有意义。
    size_t NextChunkBytes() const
    {
        const int64_t remain = m_fileSize - m_sendOffset;
        if (remain <= 0)
            return 0;
        if (remain < static_cast<int64_t>(m_chunkBytes))
            return static_cast<size_t>(remain);
        return m_chunkBytes;
    }

    /**
     *  记账"又发出去一片"。<u>必须在真正写 socket 之前调用</u>：回执可能在写 socket 返回
     *  之前就已经被接收线程处理，先记账才不会让那个回执因为查不到在途记录而被误判成回退。
     *  @param bytes 本片字节数，须与 NextChunkBytes() 一致
     */
    void OnChunkSent(size_t bytes)
    {
        m_sendOffset += static_cast<int64_t>(bytes);
        m_inflightEnds.push_back(m_sendOffset);
    }

    /**
     *  处理一个 ft_chunk_ack。
     *  与在途最早那片的末尾一致即正常推进；不一致则一律以接收端给的位点为准重新定位，
     *  在途记账作废，此后从该位点重新读盘发送（这就是"对方回退位点时能正确回退重发"）。
     *  @param nextOffset 接收端给出的下一片起始偏移，即它已落盘的字节数
     *  @return 见 FolderSendAckResult；返回 kFolderSendAckInvalid 时窗口状态不变
     */
    FolderSendAckResult OnChunkAck(int64_t nextOffset)
    {
        //越界位点：接收端不可能落盘出负数或多于文件长度的字节，属于协议错误
        if (nextOffset < 0 || nextOffset > m_fileSize)
            return kFolderSendAckInvalid;

        bool matched = false;
        if (!m_inflightEnds.empty())
        {
            matched = (m_inflightEnds.front() == nextOffset);
            m_inflightEnds.pop_front();
        }

        //与在途最早那片的末尾吻合：窗口正常前进一片
        if (matched)
        {
            m_ackedOffset = nextOffset;
            return kFolderSendAckAdvanced;
        }

        //其余一律按对方给的位点重新定位：既覆盖"只写进去一部分"的回退，也覆盖在途记账
        //已被上一次回退清空之后又来一个回执的情形。重新定位到当前位置本身是空操作，不
        //会造成多余重发
        m_ackedOffset = nextOffset;
        m_sendOffset = nextOffset;
        m_inflightEnds.clear();
        return kFolderSendAckRepositioned;
    }

    //接收端已确认落盘到哪个偏移。进度显示与"该文件是否传完"都以它为准。
    int64_t AckedOffset() const
    {
        return m_ackedOffset;
    }

    //在途分片数（已发出、尚未收到回执）。
    size_t InflightCount() const
    {
        return m_inflightEnds.size();
    }

    //本文件的字节是否已全部发出（不代表已被确认）。
    bool AllBytesSent() const
    {
        return m_sendOffset >= m_fileSize;
    }

    //本文件是否已全部被确认落盘且无在途分片。为 true 时可以发 ft_file_end 了。
    bool IsDrained() const
    {
        return m_ackedOffset >= m_fileSize && m_inflightEnds.empty();
    }

private:
    int64_t            m_fileSize;      // 本文件总字节数，Begin 时置入
    int64_t            m_sendOffset;    // 下一片的读盘 / 发送偏移，随发送前进、随回退倒退
    int64_t            m_ackedOffset;   // 接收端已确认落盘的字节数，权威续传位点
    size_t             m_chunkBytes;    // 单片数据字节数，Begin 时置入
    size_t             m_maxInflight;   // 在途分片数上限，Begin 时置入
    std::deque<int64_t> m_inflightEnds; // 在途各片的末尾偏移，按发出顺序排队；回执逐个从队首比对
};

// ======================== 纯逻辑：清单帧长度核对 ========================

/**
 *  逐帧核对清单分段封帧后的字节数是否都在单次发送允许的上限之内。
 *
 *  这道核对是必要的，因为两个上限并不相同：relayframing 的封帧函数按<u>加密前</u>的
 *  2 MiB 判上限，而中转通道按<u>加密后</u>的大小判上限，密文比明文固定多出
 *  net::STREAM_OVERHEAD_BYTES 字节。压线的那一段清单会出现"构造得出来却发不出去"，
 *  表现为传输在发清单这一步莫名失败。正常预算（kFolderRelayManifestSegmentBytes，
 *  512 KiB）下远达不到上限，本函数拦的是预算被调大之后的意外。
 *
 *  @param frames      待发送的清单帧
 *  @param limitBytes  单帧允许的最大字节数，调用方传 kFolderRelayMaxAppFrameBytes
 *  @param outBadIndex [出参] 第一个超限帧的下标，供写日志；返回 true 时置 0
 *  @return 全部帧都在上限内返回 true；有超限帧返回 false
 */
inline bool CheckManifestFramesFitLimit(const std::vector<std::string>& frames,
                                        size_t limitBytes,
                                        size_t& outBadIndex)
{
    outBadIndex = 0;
    for (size_t i = 0; i < frames.size(); ++i)
    {
        if (frames[i].size() > limitBytes)
        {
            outBadIndex = i;
            return false;
        }
    }
    return true;
}

//清单重发（2026-10-05）：fileserver 在两端的同序号通道都报到之前收到的帧直接丢弃，而发送端连好通道就发
//清单；接收端晚一步报到时清单就丢了，发送端等满 kFolderSendManifestAckTimeoutSec 后整笔失败。现在发送端在收到
//ft_manifest_ack 之前按递增的间隔重发整份清单，接收端的重组器只认收齐的第一份（见 FolderRelayManifestAssembler）。

//第一次重发前的等待，单位：毫秒。接收端通常比发送端晚报到几百毫秒以内，1 秒足以覆盖
const int kFolderManifestResendFirstMs = 1000;

//重发间隔的上限，单位：毫秒。间隔每次翻倍，到这个值后不再增长；接收端真的在建一棵很大的目录树时，
//每 16 秒重发一份清单的开销可以忽略
const int kFolderManifestResendMaxMs = 16000;

/**
 *  第几次重发清单之前要等多久。
 *  @param resendCount 已经重发过的次数，从 0 起（0 表示第一次发出之后、第一次重发之前）；为负时按 0 处理
 *  @return 等待时长，单位：毫秒。依次为 1000、2000、4000、8000、16000，之后一直是 16000
 */
inline int FolderManifestResendDelayMs(int resendCount)
{
    //从第一次的等待起每次翻倍，翻到上限为止；逐次相乘而不是移位，次数再大也不会溢出
    int delay = kFolderManifestResendFirstMs;
    for (int i = 0; i < resendCount && delay < kFolderManifestResendMaxMs; ++i)
        delay *= 2;
    return delay < kFolderManifestResendMaxMs ? delay : kFolderManifestResendMaxMs;
}

// ======================== 与上层的接口 ========================

/**
 *  一次进度更新的完整内容。
 *  分子 m_sentBytes 以接收端的回执为准（不是"已写进 socket 的字节数"），故它只会随对方
 *  确实落盘而增长；对方回退位点时它会相应减少，这是正确的表现，界面上不必特殊处理。
 */
struct FolderSendProgress
{
    int64_t     m_sentBytes;      // 已被接收端确认落盘的字节数（含续传时上次已落盘的部分）
    int64_t     m_totalBytes;     // 全部文件字节数合计
    int64_t     m_finishedFiles;  // 已完成的文件数（含续传时跳过的那些）
    int64_t     m_totalFiles;     // 文件总数
    std::string m_currentFile;    // 最近一个开始传输的文件的相对路径（UTF-8）；多条通道并行时取最近开始的那个

    FolderSendProgress()
        : m_sentBytes(0)
        , m_totalBytes(0)
        , m_finishedFiles(0)
        , m_totalFiles(0)
    {
    }
};

/**
 *  发送端状态机向上层回报结果的接口。
 *
 *  <u>三个回调都在本模块的工作线程、驱动线程或通道的接收线程上触发，不在 UI 线程上</u>。
 *  实现方须把需要的数据拷贝出来后转投 UI 线程（客户端惯例是 PostMessage 给窗口），不得在
 *  回调里直接改界面，也不得在回调里做耗时阻塞操作 —— 进度回调阻塞会拖慢该通道的收字节。
 *
 *  另外<u>不得在回调里调用本会话的 Stop()</u>：那等于让驱动线程 join 自己。需要在完成 /
 *  失败之后销毁会话，应当把销毁动作转投到 UI 线程去做。
 */
class IFolderSendSessionHandler
{
public:
    virtual ~IFolderSendSessionHandler() {}

    /**
     *  进度更新。两次之间至少间隔 kFolderSendProgressIntervalMs 毫秒，但每个文件完成时
     *  会立即回调一次，不受该间隔限制。
     *  @param progress 本次进度快照；<u>仅在本次回调内有效</u>，需留存须自行拷贝
     */
    virtual void OnFolderSendProgress(const FolderSendProgress& progress) = 0;

    /**
     *  整体完成：全部文件都通过了接收端的校验，ft_complete 也已发出。
     *  @param totalFiles 本次清单里的文件总数（含续传时跳过的那些）
     *  @param totalBytes 全部文件字节数合计
     *  本回调之后不会再有任何回调。
     */
    virtual void OnFolderSendCompleted(int64_t totalFiles, int64_t totalBytes) = 0;

    /**
     *  整体失败：任一文件任一步出错，全部通道已停下，ft_abort 已尽力发出。
     *  @param errcode 错误码，取值见 Source/net/Protocol.h 的 enum error_code
     *                 （606~620 那一组是文件夹传输专用，用户取消为 error_code_folder_canceled）
     *  @param errmsg  英文诊断文本，供写日志；界面上给用户看的中文文案由 UI 层按 errcode 另行给出
     *  本回调之后不会再有任何回调。
     */
    virtual void OnFolderSendFailed(int errcode, const std::string& errmsg) = 0;
};

/**
 *  启动一次发送所需的全部参数。
 *  中转地址、端口、令牌、通道条数四项来自 cmd_folder_relay_info 的下发，客户端不自行决定。
 */
struct FolderSendSessionParams
{
    std::string    m_relayIp;             // 中转地址（relayip 字段）
    int            m_relayPort;           // 中转端口（relayport 字段），即 fileserver 现有的监听端口
    std::string    m_relayToken;          // 中转令牌（token 字段），本端 N+1 条通道共用同一枚
    int            m_dataLanes;           // 数据通道条数 N（lanes 字段，不含控制通道）；小于 1 按 1 处理，超过硬上限按硬上限截断
    std::wstring   m_rootPath;            // 本地根目录的完整路径，与生成清单时用的那个一致；读源文件时与相对路径拼接
    FolderManifest m_manifest;            // 待发送的清单（ScanFolderTree 的产物），含根文件夹名、目录列表与文件列表
    int            m_connectTimeoutSec;   // 单条通道"连接 + 加密握手 + 报到"的总超时（秒）；填 0 表示用通道层默认值
    // 服务器长期公钥（32 字节），用于各条通道的协议加密握手；NULL 表示服务端未配置公钥、通道全程明文。
    // 不接管所有权：指向的内存须在 Start 返回之后仍然有效（主程序取自 CIUSocket，进程内常驻；
    // 文件共享 DLL 取自宿主接口，同样进程内常驻）。由调用方传入而不是本模块自己去取，
    // 是为了让本模块不依赖主程序的全局对象，主程序与文件共享 DLL 可以编译同一份源文件
    const unsigned char* m_serverPubKey;
    // 扩展点（不拥有，生命周期须长于会话）；NULL 表示单聊文件夹传输，行为不变。文件共享的传输子会话用它在发清单之前
    // 交换 st_job / st_job_ack，下载时由它给出清单（见 FolderTransferHooks.h）
    IFolderSendHooks*    m_hooks;
    // 通道工厂（不拥有，生命周期须长于会话）；NULL 表示各条通道都是到 fileserver 的中转连接（按上面的中转参数建），
    // 行为不变。单聊文件夹传输在 1816 带 p2psessionid 时给出 P2P 的工厂（先等 P2P 链路，建链失败再退回中转）
    IFolderLaneTransportFactory* m_transportFactory;

    FolderSendSessionParams()
        : m_relayPort(0)
        , m_dataLanes(0)
        , m_connectTimeoutSec(0)
        , m_serverPubKey(NULL)
        , m_hooks(NULL)
        , m_transportFactory(NULL)
    {
    }
};

/**
 *  发送端状态机：一次「发送文件夹」中从拿到中转参数到整体成功 / 失败的全过程。
 *
 *  一个实例只能跑一次传输；重试续传应当在拿到重新下发的中转参数后新建一个实例，而不是
 *  复用已结束的这个。本机可以同时跑多笔文件夹传输（上限见客户端配置 foldermaxconcurrent
 *  与服务端配置 foldermaxperuser，默认各 5 笔），每笔各持有自己的一个本类实例。
 *
 *  除 Start 之外的公开方法都可以在任意线程调用。Start 之后不要再改 params —— 参数在
 *  Start 内部已整份拷走，改外面那份不起作用。
 */
class FolderSendSession : private IFolderControlSender
{
public:
    FolderSendSession();
    ~FolderSendSession();

    FolderSendSession(const FolderSendSession&) = delete;
    FolderSendSession& operator=(const FolderSendSession&) = delete;

    /**
     *  启动一次发送。<u>立即返回</u>，建通道、发清单、传文件全在本模块自建的线程上进行，
     *  结果经 handler 回调，故可以直接在 UI 线程上调用。
     *
     *  @param params  全部参数，见 FolderSendSessionParams；函数内部整份拷贝一份自用
     *  @param handler 结果回调（非空，<u>不接管所有权</u>）；其生命周期须长于本会话，
     *                 至少持续到收到完成 / 失败回调、或 Stop() 返回
     *  @return 线程已拉起返回 true；参数非法（handler 为空、中转地址或令牌为空、端口非正、
     *          根目录路径为空）或本实例已经跑过一次返回 false，此时<u>不会</u>有任何回调。
     *          清单里一个文件都没有（只有空目录）是合法的，接收端把目录建完即整体成功
     *  副作用：建齐 N+1 个通道对象并起 1 条驱动线程；随后驱动线程会再建 N+1 条中转连接
     *  与 N 条工作线程。
     */
    bool Start(const FolderSendSessionParams& params, IFolderSendSessionHandler* handler);

    /**
     *  取消本次传输。可在任意线程、任意时刻调用，重复调用安全。
     *  取消在分片边界生效（正在写 socket 的那一片会发完），随后按失败收场：错误码为
     *  error_code_folder_canceled，双方气泡显示"已取消"。
     *  本方法<u>不阻塞</u>，真正的收尾由驱动线程完成，结果仍经 OnFolderSendFailed 回调。
     */
    void Cancel();

    /**
     *  停止并等待全部线程退出。<u>阻塞</u>，故不宜在 UI 线程上直接调用（正在发的那一片
     *  发完才会退出）。重复调用安全；析构函数会自动调它，上层忘记调也不会漏掉线程回收。
     *  在回调线程内调用本方法会被识别出来并<u>跳过 join</u>（否则等于让驱动线程 join 自己），
     *  但仍不建议那样写，见 IFolderSendSessionHandler 的注释。
     */
    void Stop();

    //本次传输是否仍在进行（驱动线程尚未走完收尾）。可在任意线程调用。
    bool IsRunning() const;

private:
    //一条中转通道的上下文（连接、回调适配、本通道的传输状态）。定义在 .cpp 里，因为它
    //要持有 FolderRelayClient 并实现 FolderRelayClientHandler，而那两者都来自引入
    //<winsock2.h> 的头文件，不必让本头文件的使用方一并背上。
    class LaneContext;

    //驱动线程主体：建通道 → 发清单 → 等目录就绪 → 拉起并回收工作线程 → 收尾回调。
    void RunProc();

    //数据通道工作线程主体：循环取待传文件并在本通道上整个传完。
    //@param laneSlot 本线程负责的通道在 m_lanes 里的下标（1..N，0 是控制通道）
    void WorkerProc(size_t laneSlot);

    //逐条连接 1 条控制通道与 N 条数据通道（通道对象本身已在 Start 里建齐）。
    //任一条连不上即整体失败 —— 少一条通道就有一批文件没人传。
    bool OpenLanes();

    //建好通道之后、发清单之前执行扩展点的前置交换（没有扩展点时直接返回 true）；成功时按扩展点给出的清单重建发送队列
    bool RunPreamble();

    //按 m_params.m_manifest 建文件列表与发送队列（Start 与前置交换之后各调一次）
    void BuildFileQueue();

    //IFolderControlSender：经控制通道（lane 0）发扩展点的自定义帧
    virtual bool SendControl(const std::string& frame) override;

    //把清单分段后在控制通道逐段发出。
    bool SendManifest();

    //阻塞等待接收端回 ft_manifest_ack（对方建完目录才会回），并把续传位点应用到队列。回执到达之前按
    //FolderManifestResendDelayMs 的间隔重发整份清单，总时限仍是 kFolderSendManifestAckTimeoutSec。
    bool WaitManifestAck();

    //在指定数据通道上把一个文件从起传偏移传到末尾，并等对方的文件校验结果。
    //@return 该文件通过校验返回 true；任一步出错返回 false（失败原因已记进本会话）
    bool SendOneFile(size_t laneSlot, const FolderSendFileTask& task);

    //流式计算一个本地文件的完整内容 MD5（32 位小写十六进制）。
    //@return 成功返回 true；打开或读取失败返回 false
    bool ComputeFileMd5(const std::wstring& path, std::string& md5out);

    //某条通道收到一个应用帧（在该通道的接收线程上被调用），按帧类型分流到下面四个处理函数。
    void OnLaneFrame(size_t laneSlot, const unsigned char* data, size_t len);

    //控制通道收到目录就绪回执：记下续传位点并唤醒等待中的驱动线程。
    void OnManifestAckFrame(const FolderRelayManifestAck& ack);

    //数据通道收到分片回执：推进（或按对方给的位点回退）该通道的流控窗口，并更新进度。
    void OnChunkAckFrame(size_t laneSlot, const FolderRelayChunkAck& ack);

    //数据通道收到文件校验结果：记下结果码并唤醒等待中的工作线程。
    void OnFileResultFrame(size_t laneSlot, const FolderRelayFileResult& result);

    //收到对端的中止帧：按对端给出的原因整体失败。
    void OnAbortFrame(const FolderRelayAbort& abort);

    //某条通道异常断开（在该通道的接收线程上被调用）。按整体失败处理，不做自动重连。
    void OnLaneClosed(size_t laneSlot);

    //记录一次失败并叫停全部线程。只有<u>第一个</u>失败原因会被记住，后续调用直接返回。
    void Fail(int errcode, const std::string& errmsg);

    //在控制通道上尽力发出 ft_abort（通道已断则发不出去，由信令层的 cmd_folder_close 兜底）。
    void SendAbort();

    //关闭全部通道的连接。
    void CloseAllLanes();

    //唤醒全部正在等回执 / 等文件结果的工作线程（失败或取消时用）。
    void WakeAllLanes();

    //向上层回报一次进度。
    //@param force       true 表示忽略最小间隔立即回报（文件完成时用）
    //@param currentFile 当前文件的相对路径；空串表示沿用上一次记录的那个
    void ReportProgress(bool force, const std::string& currentFile);

    //把清单里的相对路径拼成本地完整路径（已加长路径前缀）。
    std::wstring MakeLocalPath(const std::string& relpath) const;

    //是否已经该停下来了（失败或取消）。工作线程在每个分片边界查它。
    bool ShouldStop() const;

private:
    FolderSendSessionParams   m_params;         // Start 时拷入的全部参数，此后只读
    IFolderSendSessionHandler* m_handler;       // 结果回调（不拥有；生命周期由上层保证）
    int                       m_dataLaneCount;  // 实际使用的数据通道条数（已按下限 / 硬上限修正）

    FolderSendFileQueue       m_queue;          // 待传文件队列，N 条工作线程共用
    std::vector<FolderRelayFileEntry> m_files;  // 清单文件列表（从 FolderManifest 转成协议结构），下标即文件序号

    std::vector<std::unique_ptr<LaneContext> >  m_lanes;    // 全部通道，下标即 lane 序号：0 控制、1..N 数据
    std::unique_ptr<std::thread>                m_driver;   // 驱动线程
    std::vector<std::unique_ptr<std::thread> >  m_workers;  // 数据通道工作线程，与 m_lanes[1..N] 一一对应
    std::thread::id                             m_driverId; // 驱动线程的线程号，供 Stop 识别"在回调线程里调 Stop"

    std::atomic<bool>         m_started;        // Start 是否已经成功执行过（一个实例只跑一次）
    std::atomic<bool>         m_running;        // 驱动线程是否仍在运行
    std::atomic<bool>         m_stop;           // 是否已经该停下来（失败或取消都会置它）

    mutable std::mutex        m_stateMtx;       // 保护 m_failed / m_errcode / m_errmsg / m_currentFile
    bool                      m_failed;         // 是否已经失败（只记第一个原因）
    int                       m_errcode;        // 失败错误码，取值见 net/Protocol.h 的 error_code
    std::string               m_errmsg;         // 失败诊断文本（英文）
    std::string               m_currentFile;    // 最近一个开始传输的文件的相对路径，进度回调里带给上层

    std::mutex                m_manifestMtx;    // 保护 m_manifestAckReady / m_manifestAck
    std::condition_variable   m_manifestCv;     // 目录就绪回执的等待点：控制通道收到 ft_manifest_ack 后唤醒驱动线程
    bool                      m_manifestAckReady; // 是否已收到 ft_manifest_ack
    FolderRelayManifestAck    m_manifestAck;    // 收到的续传位点集合

    std::atomic<int64_t>      m_sentBytes;      // 已被接收端确认落盘的字节数（进度分子），随回执增减
    std::atomic<int64_t>      m_finishedFiles;  // 已完成的文件数（进度分子），含续传时跳过的那些

    std::mutex                m_progressMtx;    // 保护 m_lastProgressAt
    std::chrono::steady_clock::time_point m_lastProgressAt; // 上一次进度回调的时刻，用于按 kFolderSendProgressIntervalMs 限流
};

} // namespace foldertransfer
