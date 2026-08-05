/**
 *  GroupTransferQueue.h
 *
 *  群文件「并发传输池」的待处理队列 + 取消 / 去重簿记的纯逻辑核心（不含线程）。
 *
 *  背景：群文件上传 / 下载从原来的单线程串行，改为最多 kMaxConcurrentGroupTransfers
 *  条并发连接（每个传输独占一条新建的 fileserver 连接、传完即断、超额排队）。本类
 *  只负责「待处理队列 + 哪些在传 + 取消标志 + 下载去重」这套簿记；真正的线程池
 *  （N 个 worker + 互斥量 + 条件变量）在 net::FileTransfer 里套在本类外面。
 *
 *  之所以把这套簿记单独抽成纯逻辑类：它不依赖 socket / 线程 / UI，可单元测试
 *  （见 flamingoclient/Source/Tests/GroupTransferQueueTests.cpp，Suite BGTQ），
 *  把「id 分配唯一 / 先进先出 / 取消排队中 vs 取消在传中 / 下载按 entryid 去重 /
 *  完成后清理」这些边界行为锁死，再放心地在外层加并发线程。
 *
 *  线程模型约定：本类自身<不>加锁；FileTransfer 用单个互斥量保护对本类的全部
 *  调用（EnqueueXxx / PopNext / CancelUpload / CompleteXxx）。唯一例外是取消标志
 *  std::atomic<bool>：worker 在流式循环里<不持锁>地 load 它，UI 线程经 CancelUpload
 *  在持锁时 store 它 —— 用原子量正是为了让这一读一写无需共享那把大锁。
 *
 *  balloonwj@qq.com   2026-06-09
 */

#pragma once

#include <stdint.h>
#include <string>
#include <deque>
#include <map>
#include <set>
#include <memory>
#include <atomic>

namespace net {

// 群文件并发传输池一次并发的上限（连接数）：上传与下载合计同时最多这么多条
// fileserver 连接，超出的在队列里等待，有空位再顶上。取该值是与用户确认的折中
// （够快、又不至于给服务端 / 带宽太大压力）。
const int kMaxConcurrentGroupTransfers = 3;

/**
 *  GroupTransferItem —— 并发池的一笔待处理传输（上传或下载）。
 *
 *  worker 从队列 PopNext 取出本项后据此执行：上传读 m_localPath 传到 fileserver，
 *  下载据 m_fileId 拉下来按 m_filename 落地。上传项取出时会被装入取消标志
 *  m_cancelFlag，worker 流式循环每发 / 收一片就 load 检查一次。
 */
struct GroupTransferItem
{
    bool         m_isUpload;    // true=上传，false=下载（决定走哪条处理路径）
    int          m_uploadId;    // 上传 id（上传项唯一、自增；下载项恒为 0）
    int64_t      m_groupId;     // 目标群 id（随结果回投以路由群聊窗）
    int64_t      m_selfUserId;  // 当前登录用户 userid（上传一致性 / 下载落地目录）

    // ==== 上传字段 ====
    std::wstring m_localPath;   // 本地文件完整路径（其文件名部分即登记进库的原始名）

    // ==== 下载字段 ====
    std::string  m_entryId;     // 群文件条目 id（下载去重键 + 随结果回投关联面板行）
    std::string  m_fileId;      // 文件本体 id（传给 cmd_request_download）
    std::string  m_filename;    // 原始文件名（UTF-8，日志 / 默认名用）
    std::string  m_expectMd5;   // 期望 md5（下载完成后校验；可空则用应答里的 md5）
    int64_t      m_expectSize;  // 期望字节数（日志用，可为 0）
    std::wstring m_destPath;    // 下载落地的完整路径（用户在「另存为」对话框选定，含文件名）

    // 取消标志（仅上传项有意义）：PopNext 取出上传项时置入一个初值 false 的原子量；
    // worker 不持锁地 load 它，CancelUpload 持锁地 store true。下载项为空指针。
    std::shared_ptr<std::atomic<bool> > m_cancelFlag;

    GroupTransferItem()
        : m_isUpload(true)
        , m_uploadId(0)
        , m_groupId(0)
        , m_selfUserId(0)
        , m_expectSize(0)
    {
    }
};

/**
 *  GroupTransferQueue —— 并发池的待处理队列 + 取消 / 去重簿记（纯逻辑、不加锁）。
 *
 *  典型用法（由 FileTransfer 在持锁下驱动）：
 *    入队：EnqueueUpload 返回新上传 id；EnqueueDownload 按 entryid 去重。
 *    取活：worker 唤醒后 PopNext 取队首，记为「在传」；空则等条件变量。
 *    取消：UI 线程 CancelUpload —— 在队列里就移除（永不开始），在传就置取消标志。
 *    收尾：worker 传完调 CompleteUpload / CompleteDownload 清理在传簿记。
 */
class GroupTransferQueue
{
public:
    GroupTransferQueue();

    /**
     *  入队一笔上传，返回为它分配的上传 id（唯一、自增、从 1 起）。
     *    groupId：   目标群 id（>0）
     *    localPath： 本地文件完整路径（宽字符）
     *    selfUserId：当前登录用户 userid
     *  返回：本笔上传的 id（供回投进度 / 结果对回、供取消按 id 定位）。
     */
    int EnqueueUpload(int64_t groupId, const std::wstring& localPath, int64_t selfUserId);

    /**
     *  入队一笔下载。按 entryid 去重：该 entryid 已在队列中或正在传时忽略、返回 false，
     *  避免连点同一文件开两条连接拉同一份。
     *    groupId / entryId / fileId / filename / expectMd5 / expectSize / selfUserId / destPath：
     *      含义同 GroupTransferItem 对应字段（destPath 为用户选定的落地完整路径）。
     *  返回：真正入队返回 true；因去重被忽略返回 false。
     */
    bool EnqueueDownload(int64_t groupId, const std::string& entryId, const std::string& fileId,
                         const std::string& filename, const std::string& expectMd5,
                         int64_t expectSize, int64_t selfUserId, const std::wstring& destPath);

    /**
     *  取出队首一笔交给 worker 处理。空队列返回 false。
     *  取出上传项时：登记为「在传」并新建其取消标志装进 out.m_cancelFlag。
     *  取出下载项时：把其 entryid 登记进「在传下载」集合（去重用）。
     *    out： [出] 取出的传输项（仅返回 true 时有效）。
     *  返回：取到返回 true；队列空返回 false。
     */
    bool PopNext(GroupTransferItem& out);

    /**
     *  取消一笔上传：
     *    - 若仍在队列里（还没轮到）：直接从队列移除，使它永不开始。
     *    - 若已取出在传：置位其取消标志，worker 在下一个分片边界中止。
     *    - 若 id 未知（已完成 / 不存在）：无操作。
     *    uploadId：要取消的上传 id。
     */
    void CancelUpload(int uploadId);

    /**
     *  上传收尾（成功 / 失败 / 取消后都要调）：清理该上传的在传簿记与取消标志。
     *    uploadId：完成的上传 id。
     */
    void CompleteUpload(int uploadId);

    /**
     *  取消一笔下载（与上传取消对称）：
     *    - 若仍在队列里（还没轮到）：直接从队列移除，使它永不开始。
     *    - 若已取出在传：置位其取消标志，worker 在下一个分片边界中止。
     *    - 若 entryid 未知（已完成 / 不存在）：无操作。
     *    entryId：要取消的下载条目 id。
     */
    void CancelDownload(const std::string& entryId);

    /**
     *  下载收尾（成功 / 失败 / 取消后都要调）：把该 entryid 移出「在传下载」簿记，
     *  使之后对同一文件的下载请求能再次入队。
     *    entryId：完成的下载条目 id。
     */
    void CompleteDownload(const std::string& entryId);

    // ---- 只读访问（诊断 / 单元测试用） ----

    // 队列里待处理（尚未取出）的笔数。
    int PendingCount() const;

    // 已取出、正在传的笔数（上传 + 下载合计）。
    int RunningCount() const;

    // 某笔在传上传是否已被取消（id 不在传 / 未知一律返回 false）。
    bool IsUploadCancelled(int uploadId) const;

    // 某笔在传下载是否已被取消（entryid 不在传 / 未知一律返回 false）。
    bool IsDownloadCancelled(const std::string& entryId) const;

private:
    std::deque<GroupTransferItem> m_pending;       // 待处理队列（先进先出）
    int                           m_nextUploadId;  // 下一个要分配的上传 id（从 1 起自增）

    // 在传上传：上传 id → 取消标志。PopNext 取出上传项时加入，CompleteUpload 移除。
    std::map<int, std::shared_ptr<std::atomic<bool> > > m_runningUploads;

    // 在传下载：entryid → 取消标志。PopNext 取出下载项时加入，CompleteDownload 移除；
    // 其键集合 + 队列中的下载 entryid 构成 EnqueueDownload 的去重判据。
    std::map<std::string, std::shared_ptr<std::atomic<bool> > > m_runningDownloads;
};

} // namespace net
