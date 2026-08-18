/**
 *  客户端自动更新管理器，UpdateManager.h
 *
 *  把"检查更新 → 下载升级包 → 校验"这条链路收口到一个串行工作线程里，界面只管
 *  发起与取结果。本期（S2）只做到"包下载完并校验通过"为止，升级窗与解压重启分别
 *  在 S3 / S4 接入。
 *
 *  流程：
 *    ① 经 CIUSocket 发 cmd_check_update(1905)，请求体由 net::UpdateProtocolClient 组；
 *    ② 应答异步回到 CIUSocket::HandleV2Frame，再按 seq 转交本类的等待槽；
 *    ③ 若有可用新版本且调用方要求下载，用应答里<u>直接下发的下载令牌</u>连 fileserver
 *       （net::FileServerConn 独立连接），跑 net::FileStreamTransfer::downloadFile
 *       流式分片下载，收一片写一片盘并增量算 MD5；
 *    ④ 校验 MD5 与大小后原子 rename 落地到 <程序目录>\update\ 下。
 *
 *  两个与别的传输不同的地方：
 *    - <u>可能在未登录时跑</u>。版本过低的客户端会被 cmd_login 拒绝，正要靠这条链路
 *      自救，故 1905 走的是服务端的登录前白名单，下载也用独立连接而非共享文件 socket。
 *    - 下载令牌<u>无需另行申请</u>：升级包对所有人是同一份公开内容，服务端在 1905
 *      应答里就把令牌签好了，省掉一次 cmd_request_download。
 *
 *  线程模型：一条工作线程串行处理任务，与 net::FileTransfer 同构。界面线程只调
 *  CheckAndDownload 投递任务、调 TakeLastResult 取结果，不直接碰内部状态。
 *
 *  balloonwj@qq.com   2026-08-12
 */
#pragma once

#include <stdint.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include <windows.h>                      // HWND（进度 / 结果消息的投递目标）

#include "UpdateTaskResult.h"                // UpdateTaskResult / UpdateProgressInfo
#include "../../net/UpdateProtocolClient.h"  // CheckUpdateResult

namespace update {

//自动更新管理器；进程内单例。
class UpdateManager
{
public:
    static UpdateManager& GetInstance();

    //启动工作线程。与 net::FileTransfer 一样在 CIUSocket::Init 里调一次。
    void Init();

    //停工作线程并等它退出。CIUSocket::Uninit 里调。可重复调用。
    void Uninit();

    /**
     *  投递一次"检查更新"任务（立即返回，实际工作在工作线程上跑）。
     *
     *    autoDownload： true 表示发现新版本后立刻下载升级包；false 只查不下，
     *                   用于"关于页点检查更新"这种先问用户再下的场景。
     *
     *  重复投递是安全的：任务串行执行，先到先跑。
     */
    void CheckAndDownload(bool autoDownload);

    /**
     *  由 CIUSocket::HandleV2Frame 在收到 cmd_check_update 应答时调用，
     *  把应答交给正在等待的那次检查。
     *
     *    seq： 应答帧的序列号，须与发出请求时的 seq 对上
     *    r：   已解析的应答
     *
     *  seq 对不上（超时后迟到的应答）会被丢弃并记一条日志。
     */
    void OnCheckUpdateResponse(uint32_t seq, const net::CheckUpdateResult& r);

    /**
     *  取走最近一次的结果。
     *
     *    out： [出参] 最近一次结果；m_valid 为 false 表示还没有结果
     *
     *  @return 有结果返回 true。取走后内部标记清空，同一份结果不会被取到两次
     *          —— 界面层轮询时不会把上一次的旧结果当成本次的。
     */
    bool TakeLastResult(UpdateTaskResult& out);

    /**
     *  设置接收进度 / 结果消息的窗口（升级窗）。
     *
     *    hWnd： 目标窗口句柄；传 NULL 表示不再投递
     *
     *  升级窗创建后调一次传入自己的句柄，**销毁前必须再调一次传 NULL** —— 否则工作
     *  线程会继续往一个已销毁的 HWND 投消息，消息被系统丢弃，lParam 上的堆对象随之
     *  泄漏，且全程没有任何报错。
     *
     *  可在任意线程调用（内部加锁）。
     */
    void SetNotifyTarget(HWND hWnd);

    /**
     *  把一次下载进度投给升级窗（FMG_MSG_UPDATE_PROGRESS）。
     *
     *    received： 已接收并落盘的字节数
     *    total：    升级包总字节数；0 表示服务端还没告知
     *
     *  由内部的下载进度 sink 在<u>工作线程</u>上调用，故只投消息、不碰控件。放在
     *  public 段是因为 sink 是 .cpp 里的独立类，够不着私有成员；外部业务代码没有
     *  调用它的理由。未登记投递目标（SetNotifyTarget 传入过 NULL）时直接丢弃。
     */
    void PostProgress(int64_t received, int64_t total);

private:
    UpdateManager();
    ~UpdateManager();
    UpdateManager(const UpdateManager&) = delete;
    UpdateManager& operator=(const UpdateManager&) = delete;

    //工作线程主循环：从队列取任务并执行，收到停止信号后退出。
    void Run();

    //执行一次完整的检查（必要时下载）。在工作线程上跑。
    //  autoDownload：是否在发现新版本后继续下载。
    void DoCheck(bool autoDownload);

    //下载升级包并校验。在工作线程上跑。
    //  info：1905 应答里的版本信息（含令牌与 fileserver 地址）。
    //  result：[出入参] 就地回填下载结果字段。
    void DoDownload(const net::CheckUpdateResult& info, UpdateTaskResult& result);

    //把一次结果记入 m_lastResult、写日志，并投一条 FMG_MSG_UPDATE_RESULT 给升级窗。
    void PublishResult(const UpdateTaskResult& result);

    //取当前的投递目标（加锁读）。返回 NULL 表示尚未登记投递目标。
    HWND GetNotifyTarget();

private:
    std::thread              m_worker;        //工作线程
    std::atomic<bool>        m_running;       //线程运行标志；Uninit 置 false 唤醒退出

    std::deque<bool>         m_tasks;         //待处理任务队列，元素即 autoDownload 标志
    std::mutex               m_taskMtx;       //保护 m_tasks
    std::condition_variable  m_taskCv;        //任务到达 / 停止信号

    //1905 应答的等待槽。工作线程发出请求后在此等，HandleV2Frame 收到应答时填入并唤醒。
    std::mutex               m_ackMtx;        //保护以下三个成员
    std::condition_variable  m_ackCv;         //应答到达
    uint32_t                 m_ackSeq;        //正在等待的 seq；0 表示当前无人等待应答
    bool                     m_ackArrived;    //应答是否已填入
    net::CheckUpdateResult   m_ackResult;     //收到的应答

    std::mutex               m_resultMtx;     //保护 m_lastResult
    UpdateTaskResult            m_lastResult;    //最近一次结果，供界面层取走

    std::mutex               m_notifyMtx;     //保护 m_notifyWnd
    HWND                     m_notifyWnd;     //进度 / 结果消息的投递目标；NULL 表示尚未登记
};

} // namespace update
