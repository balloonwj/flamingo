/**
 *  群文件信息缓存的拉齐进度（纯逻辑），GroupFileCacheSync.h
 *
 *  聊天记录管理器的文件页要把群文件与聊天里的文件汇总在一起（docs/聊天记录管理器方案.md §11.5）。群文件只在
 *  服务端，客户端没有跨群列出群文件的命令，于是由主面板对本人所在的每个群用 cmd_get_group_files（1803）按页
 *  拉齐，写进本地库的 t_group_file_cache。本类只管「哪些群要拉、拉到第几页、这一页应答是不是本类要的」，
 *  不发请求、不碰本地库：主面板按它的指示发请求，拿到整群列表后写库。
 *
 *  做法：
 *    · 打开文件页时主面板调 Enqueue 把全部群排进队列；最近 kGroupFileSyncFreshMs 之内拉齐过的群跳过
 *      （之后的增减由 cmd_group_files_changed_notify 推送维护）。
 *    · 同时最多 kGroupFileSyncMaxInFlight 个群在拉，每个群从第 0 条起一页一页拉，hasmore 为假时整群完成。
 *    · 请求序号由 CIUSocket::RequestGroupFiles 返回，应答里原样带回；不是本类发出的序号一律不认，群聊窗
 *      自己拉的那些页照常交给群聊窗。
 *    · 发出后超过 kGroupFileSyncTimeoutMs 还没有应答（断线等）的按失败作废，下次 Enqueue 时重新排队。
 *
 *  典型用法（主面板）：
 *      m_sync.Enqueue(groupIds, nowMs, false);
 *      int64_t gid; int offset;
 *      while (m_sync.NextRequest(nowMs, gid, offset))
 *          m_sync.MarkSent(gid, CIUSocket::GetInstance().RequestGroupFiles(gid, 0, offset, kGroupFileSyncPageSize), nowMs);
 *      ...收到应答：
 *      if (m_sync.OwnsSeq(result.m_seq)) { int next; std::vector<GroupFileCacheItem> all;
 *          switch (m_sync.OnPage(result, nowMs, next, all)) { ... } }
 *
 *  只在界面线程使用，不加锁。单测见 Tests/GroupFileCacheSyncTests.cpp（标签 [histfiles]）。在 Flamingo.vcxproj
 *  里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <stdint.h>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "HistoryLibraryQuery.h"                  // GroupFileCacheItem
#include "../../net/GroupFileProtocolClient.h"   // net::GroupFileEntry / net::GetGroupFilesResult

namespace balloonhistory {

//同时最多几个群在拉
const int kGroupFileSyncMaxInFlight = 2;
//每页请求多少条（服务端 cmd_get_group_files 单页上限 50）
const int kGroupFileSyncPageSize = 50;
//拉齐过的群多久之内不再拉（毫秒）：10 分钟。期间的增减由推送维护
const int64_t kGroupFileSyncFreshMs = 10LL * 60 * 1000;
//发出请求后多久还没有应答就按失败作废（毫秒）：30 秒
const int64_t kGroupFileSyncTimeoutMs = 30LL * 1000;

//OnPage 处理一页应答的结果
enum GroupFileSyncAction
{
    kGroupFileSyncIgnored   = 0,   //不是本类发出的请求（或已作废），调用方照原来的规则交给群聊窗
    kGroupFileSyncNextPage  = 1,   //还有后续页：调用方按 outNextOffset 发下一页请求，再 MarkSent
    kGroupFileSyncCompleted = 2,   //这个群拉齐了：outItems 是整群的列表，调用方整体写进缓存表
    kGroupFileSyncFailed    = 3,   //服务端报错（如本人已不是群成员）：这个群放弃，不改缓存表
};

/**
 *  把服务端的群文件条目转成缓存表的一行。上传者 userid 是字符串编码的整数，解析不出来时为 0；扩展名为空时
 *  按文件名补上。本机落地路径为空（由缓存表保留原值）。
 *  @param groupid 所在群
 *  @param entry   服务端条目
 *  @return 缓存表的一行
 */
GroupFileCacheItem GroupFileCacheItemFromEntry(int64_t groupid, const net::GroupFileEntry& entry);

/**
 *  GroupFileCacheSync —— 各群群文件的拉齐进度。
 */
class GroupFileCacheSync
{
public:
    GroupFileCacheSync();

    /**
     *  把一批群排进拉齐队列。已在队列里或正在拉的跳过；kGroupFileSyncFreshMs 之内拉齐过的跳过（force 为 true
     *  时不跳过）。顺带把发出超过 kGroupFileSyncTimeoutMs 还没有应答的请求作废。
     *  @param groupIds 本人所在的群；不大于 0 的跳过
     *  @param nowMs    当前时间（Unix 毫秒）
     *  @param force    是否忽略「最近拉齐过」，全部重新拉
     *  @return 新排进队列的群数
     */
    int Enqueue(const std::vector<int64_t>& groupIds, int64_t nowMs, bool force);

    /**
     *  取下一个该发请求的群：正在拉的不足 kGroupFileSyncMaxInFlight 个、且队列不空时，从队头取出一个。
     *  @param nowMs      当前时间（Unix 毫秒），用来作废超时的请求
     *  @param outGroupId [出参] 群 ID
     *  @param outOffset  [出参] 从第几条开始请求（新开始的群为 0）
     *  @return 有要发的请求时返回 true；调用方发出后须调用 MarkSent
     */
    bool NextRequest(int64_t nowMs, int64_t& outGroupId, int& outOffset);

    /**
     *  登记一个群刚发出的请求序号（NextRequest 或 OnPage 返回 kGroupFileSyncNextPage 之后调用）。
     *  @param groupid 群 ID
     *  @param seq     CIUSocket::RequestGroupFiles 的返回值；为 0 时按发送失败处理，这个群放弃
     *  @param nowMs   当前时间（Unix 毫秒），超时从此刻起算
     */
    void MarkSent(int64_t groupid, uint32_t seq, int64_t nowMs);

    /**
     *  某个序号是不是本类在等的请求。
     *  @param seq 应答里的请求序号
     *  @return 是本类发出、还在等应答的请求时返回 true
     */
    bool OwnsSeq(uint32_t seq) const;

    /**
     *  处理一页应答。
     *  @param result        应答（m_seq 须是本类发出的序号，否则返回 kGroupFileSyncIgnored）
     *  @param nowMs         当前时间（Unix 毫秒），完成时记为这个群的拉齐时间
     *  @param outNextOffset [出参] 返回 kGroupFileSyncNextPage 时下一页的起始偏移
     *  @param outItems      [出参] 返回 kGroupFileSyncCompleted 时整群的列表；其余情况被清空
     *  @return 取值见 GroupFileSyncAction
     */
    int OnPage(const net::GetGroupFilesResult& result, int64_t nowMs, int& outNextOffset,
               std::vector<GroupFileCacheItem>& outItems);

    /**
     *  不再拉某个群（本人离开、被移出或群被解散）：从队列与在途请求里拿掉，拉齐记录也一并清掉。
     *  在途请求的应答之后到达时不再被本类认领。
     */
    void Forget(int64_t groupid);

    //清空全部状态（登出时调用）。
    void Reset();

    //是否还有群在队列里或正在拉。
    bool Busy() const;

    //某个群是否在最近 kGroupFileSyncFreshMs 之内拉齐过。
    bool IsFresh(int64_t groupid, int64_t nowMs) const;

private:
    //一个正在拉的群
    struct Task
    {
        uint32_t                        m_seq;      //在等的那一页请求的序号；0 表示刚取出、还没发出
        int64_t                         m_sentMs;   //那一页请求的发出时间（Unix 毫秒）
        std::vector<GroupFileCacheItem> m_buffer;   //已经收到的各页条目

        Task()
            : m_seq(0)
            , m_sentMs(0)
        {
        }
    };

    //作废发出超过 kGroupFileSyncTimeoutMs 还没有应答的请求
    void DropTimedOut(int64_t nowMs);

private:
    std::deque<int64_t>         m_queue;     //等着开始拉的群，先进先出
    std::set<int64_t>           m_queued;    //m_queue 里有哪些群，判断重复排队用
    std::map<int64_t, Task>     m_running;   //正在拉的群 → 进度
    std::map<uint32_t, int64_t> m_seqToGroup; //在等应答的请求序号 → 群 ID
    std::map<int64_t, int64_t>  m_syncedAt;  //拉齐过的群 → 拉齐时间（Unix 毫秒）
};

} // namespace balloonhistory
