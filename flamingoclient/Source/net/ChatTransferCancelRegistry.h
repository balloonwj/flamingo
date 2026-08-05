/**
 *  ChatTransferCancelRegistry.h
 *
 *  单聊「收发文件」串行队列的取消簿记（纯逻辑核心，不含线程、不加锁）。
 *
 *  背景：单聊发文件 / 收文件走 net::FileTransfer 里的串行队列（m_queue + 单个
 *  worker 线程），与群文件的并发传输池（见 GroupTransferQueue）是两套。群文件那套
 *  一开始就带取消簿记，单聊这套没有——气泡上的「取消」按钮因而一直是空转的。本类
 *  给单聊补上同样的能力：登记每笔收发任务的取消标志，供 UI 线程置位、worker 线程
 *  在分片边界读取。
 *
 *  为什么单独抽成纯逻辑类：它不依赖 socket / 线程 / UI，可单元测试（见
 *  flamingoclient/Source/Tests/ChatTransferCancelRegistryTests.cpp，Suite CTCR），
 *  把「登记 / 取消在册 / 取消未知 id / 重复取消幂等 / 收尾清理 / 上传下载互不干扰」
 *  这些边界行为锁死，再放心接到真实传输流程上。
 *
 *  键的选择：上传按 clientMsgId（UUID 字符串，UI 侧「发送中」气泡正是按它定位），
 *  下载按 msgid（服务器消息ID，接收侧气泡按它定位）。二者分属两张表，互不干扰。
 *
 *  线程模型约定：本类自身<不>加锁；FileTransfer 用 m_queueMtx 保护对本类的全部调用
 *  （RegisterXxx / CancelXxx / CompleteXxx / IsXxxCancelled）。唯一例外是取消标志
 *  std::atomic<bool>：worker 在流式收发循环里<不持锁>地 load 它，UI 线程经 CancelXxx
 *  在持锁时 store 它 —— 用原子量正是为了让这一读一写无需共享那把大锁。
 *
 *  典型用法（由 FileTransfer 在持锁下驱动）：
 *    入队：RegisterSend / RegisterDownload 拿到标志，随任务一起放进队列。
 *    取消：UI 线程 CancelSend / CancelDownload 置位；worker 在下一个分片边界中止。
 *    收尾：worker 传完（成功 / 失败 / 取消）调 CompleteSend / CompleteDownload 清理。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#pragma once

#include <stdint.h>
#include <string>
#include <map>
#include <memory>
#include <atomic>

namespace net {

/**
 *  ChatTransferCancelRegistry —— 单聊收发文件的取消标志簿记（纯逻辑、不加锁）。
 *
 *  一笔任务从 RegisterXxx 起进入在册状态，到 CompleteXxx 止离开。在册期间可被
 *  CancelXxx 置位；worker 持有 RegisterXxx 返回的 shared_ptr，故即便本表已被
 *  CompleteXxx 清理，worker 手上的标志对象依旧有效，不会悬垂。
 */
class ChatTransferCancelRegistry
{
public:
    ChatTransferCancelRegistry();

    /**
     *  登记一笔发文件（上传）任务，返回它的取消标志（初值 false）。
     *  同一 clientMsgId 重复登记时，沿用并返回已经在册的那个标志对象，
     *  避免同一笔任务出现两个互不相干的标志。
     *  @param clientMsgId 客户端消息ID（UUID 字符串，非空；与「发送中」气泡对应）
     *  @return 该笔任务的取消标志；clientMsgId 为空时返回空指针（不登记）
     */
    std::shared_ptr<std::atomic<bool> > RegisterSend(const std::string& clientMsgId);

    /**
     *  登记一笔收文件（下载）任务，返回它的取消标志（初值 false）。
     *  同一 msgid 重复登记时沿用已在册的标志，理由同 RegisterSend。
     *  @param msgid 文件消息的服务器 msgid（>0；与接收侧气泡对应）
     *  @return 该笔任务的取消标志；msgid <= 0 时返回空指针（不登记）
     */
    std::shared_ptr<std::atomic<bool> > RegisterDownload(int64_t msgid);

    /**
     *  取消一笔发文件任务：置位其取消标志，worker 在下一个分片边界中止本次上传。
     *  尚未轮到的任务同样适用——worker 取出它时会先查标志、直接跳过。
     *  @param clientMsgId 要取消的任务的客户端消息ID
     *  @return 该笔在册并已置位返回 true；未知ID（已完成 / 从未登记）返回 false
     */
    bool CancelSend(const std::string& clientMsgId);

    /**
     *  取消一笔收文件任务（语义与 CancelSend 对称）。
     *  @param msgid 要取消的任务的服务器 msgid
     *  @return 该笔在册并已置位返回 true；未知ID返回 false
     */
    bool CancelDownload(int64_t msgid);

    /**
     *  查询某笔发文件任务是否已被取消。
     *  @param clientMsgId 任务的客户端消息ID
     *  @return 在册且已置位返回 true；未置位 / 未知ID一律返回 false
     */
    bool IsSendCancelled(const std::string& clientMsgId) const;

    /**
     *  查询某笔收文件任务是否已被取消。
     *  @param msgid 任务的服务器 msgid
     *  @return 在册且已置位返回 true；未置位 / 未知ID一律返回 false
     */
    bool IsDownloadCancelled(int64_t msgid) const;

    /**
     *  发文件任务收尾（成功 / 失败 / 被取消后都要调）：把它移出在册表。
     *  worker 手上的 shared_ptr 不受影响，仍可安全读取。
     *  @param clientMsgId 已结束的任务的客户端消息ID
     */
    void CompleteSend(const std::string& clientMsgId);

    /**
     *  收文件任务收尾（语义与 CompleteSend 对称）。
     *  @param msgid 已结束的任务的服务器 msgid
     */
    void CompleteDownload(int64_t msgid);

    // ---- 只读访问（诊断 / 单元测试用） ----

    // 在册的发文件任务笔数（已登记、尚未收尾）。
    int SendCount() const;

    // 在册的收文件任务笔数（已登记、尚未收尾）。
    int DownloadCount() const;

private:
    // 在册发文件任务：clientMsgId → 取消标志。RegisterSend 加入，CompleteSend 移除。
    std::map<std::string, std::shared_ptr<std::atomic<bool> > > m_sends;

    // 在册收文件任务：msgid → 取消标志。RegisterDownload 加入，CompleteDownload 移除。
    std::map<int64_t, std::shared_ptr<std::atomic<bool> > > m_downloads;
};

} // namespace net
