/**
 *  UploadInflightGate.h
 *
 *  同一文件（按内容 MD5）在本机只上传一份的门闩。
 *
 *  背景：同一个文件一次发给多个目标时（典型是录制管理窗口或聊天窗
 *  「发送给」选了多个会话，其中有群聊），单聊发文件走 FileTransfer 的串行 worker，群文件
 *  上传走并发池，几笔上传在服务端已有该文件之前同时申请上传，服务端对每一笔都回「不存在」，
 *  于是同一份字节被并行传了好几遍。服务端为此修过收尾改名的冲突（fileserversrc/
 *  UploadPartPolicy），但重复上传本身浪费带宽，客户端应当只传一份：
 *    - 某个 MD5 正在上传时，后来的同 MD5 上传在「申请上传」之前先等前一笔结束；
 *    - 前一笔成功后，后来者申请上传时服务端已有该文件，直接秒传；
 *    - 前一笔失败或被取消，后来者照常自己上传，不受影响；
 *    - 等待有上限（超时后照常并行上传，服务端可以正确处理），等待期间用户取消则立即放弃。
 *
 *  本类只做「按 MD5 排队」这一件事，不依赖 socket / UI，可单元测试
 *  （见 flamingoclient/Source/Tests/UploadInflightGateTests.cpp，标签 [uploadgate]）。
 *  线程安全：所有方法可从任意线程调用，内部加锁。
 *
 *  典型用法（FileTransfer 的各个上传流程，在申请上传之前）：
 *      UploadInflightGuard guard(m_uploadGate, md5, kUploadGateWaitMs, cancel);
 *      if (guard.Result() == UploadInflightGate::enter_cancelled)
 *          ...按用户取消回投失败...
 *      ...申请上传、传字节...
 *      //guard 析构时离开门闩，唤醒等待同一 MD5 的下一笔
 *
 *  balloonwj@qq.com   2026-10-06
 */

#pragma once

#include <string>
#include <map>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace net {

/**
 *  UploadInflightGate —— 按文件 MD5 排队的上传门闩。
 *
 *  一笔上传从 Enter 返回（结果为 enter_first / enter_after_wait / enter_timeout）起
 *  持有该 MD5，到 Leave 止。超时进入的情况下同一 MD5 可能同时有多笔持有者，
 *  内部按持有者计数，最后一笔离开时才唤醒等待者。
 */
class UploadInflightGate
{
public:
    //Enter 的结果
    enum enter_result
    {
        enter_first      = 0,   //同一 MD5 当前没有在传的上传，本笔直接进入
        enter_after_wait = 1,   //等到了同一 MD5 的前一笔上传结束后进入；前一笔若成功，本笔申请上传时会命中秒传
        enter_timeout    = 2,   //等待超过上限仍未轮到，本笔照常进入、与前一笔并行上传
        enter_cancelled  = 3,   //等待期间取消标志被置位，本笔<未进入>，调用方不得调用 Leave
    };

    UploadInflightGate();

    UploadInflightGate(const UploadInflightGate&) = delete;
    UploadInflightGate& operator=(const UploadInflightGate&) = delete;

    /**
     *  进入门闩。同一 MD5 已有持有者时阻塞等待，直到持有者全部离开、等待超时或被取消。
     *
     *  @param md5       文件内容 MD5 的十六进制串，大小写均可（按小写比较）
     *  @param timeoutMs 最长等待时间（毫秒），不大于 0 表示不等待：有持有者时立即按超时进入
     *  @param cancel    取消标志，可为 NULL；所有权归调用方，须在本调用返回前保持有效。
     *                   等待期间每隔一小段时间检查一次，置位后立即返回 enter_cancelled
     *  @return 进入结果，见 enum enter_result。除 enter_cancelled 外，调用方用完后都须调用 Leave
     *  副作用：会阻塞调用线程，不可在 UI 线程上调用。
     */
    enter_result Enter(const std::string& md5, int timeoutMs, const std::atomic<bool>* cancel);

    /**
     *  离开门闩，唤醒等待同一 MD5 的上传。对未持有的 MD5 调用不做任何事。
     *
     *  @param md5 与 Enter 时相同的 MD5 串（大小写不必一致）
     */
    void Leave(const std::string& md5);

    /**
     *  查询某个 MD5 当前是否有在传的上传，供日志与测试使用。
     *
     *  @param md5 文件内容 MD5 的十六进制串，大小写均可
     *  @return true 表示至少有一笔持有者
     */
    bool IsInflight(const std::string& md5) const;

private:
    mutable std::mutex         m_mtx;       //保护 m_inflight
    std::condition_variable    m_cv;        //某个 MD5 的持有者全部离开时通知等待者
    std::map<std::string, int> m_inflight;  //MD5（小写）→ 当前持有者数；计数归零时移除该项
};

/**
 *  UploadInflightGuard —— 在作用域内持有门闩的辅助类。
 *
 *  构造时调用 Enter，析构时若确实进入了就调用 Leave。上传流程中途失败提前 return 时
 *  也能保证离开门闩，不会让等待同一 MD5 的上传一直等到超时。
 */
class UploadInflightGuard
{
public:
    /**
     *  @param gate      门闩，所有权归调用方，生命周期须长于本对象
     *  @param md5       文件内容 MD5
     *  @param timeoutMs 最长等待时间（毫秒），含义同 UploadInflightGate::Enter
     *  @param cancel    取消标志，可为 NULL，含义同 UploadInflightGate::Enter
     *  副作用：可能阻塞调用线程，见 UploadInflightGate::Enter。
     */
    UploadInflightGuard(UploadInflightGate& gate, const std::string& md5, int timeoutMs,
                        const std::atomic<bool>* cancel);
    ~UploadInflightGuard();

    UploadInflightGuard(const UploadInflightGuard&) = delete;
    UploadInflightGuard& operator=(const UploadInflightGuard&) = delete;

    /**
     *  @return 构造时 Enter 的结果，见 UploadInflightGate::enter_result
     */
    UploadInflightGate::enter_result Result() const;

private:
    UploadInflightGate&              m_gate;     //所属门闩，不持有所有权
    std::string                      m_md5;      //本笔持有的 MD5，析构时据此离开
    UploadInflightGate::enter_result m_result;   //构造时 Enter 的结果；为 enter_cancelled 时析构不调用 Leave
};

} // namespace net
