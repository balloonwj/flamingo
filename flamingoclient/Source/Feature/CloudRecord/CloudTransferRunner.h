/**
 *  云端录像文件的上传与下载（exe 一侧），CloudTransferRunner.h
 *
 *  第 27 期（docs/屏幕共享与录制方案.md 12.6）：FlamingoMedia 经 IMediaHost::StartCloudTransfer 请 exe 上传合成后的 MP4、下载原始帧
 *  文件与最终文件。本类每次传输开一条工作线程，另建一条到 imgserver 的连接（net::FileServerConn，连接时完成协议加密握手），
 *  用 net::FileStreamTransfer 按 512 KiB 分片流式收发（上传按服务端的位点续传），传完即断。进度与结束经本类自己的消息窗口
 *  投回 UI 线程，再交给媒体模块的 OnCloudTransferProgress / OnCloudTransferFinished。
 *  取消在分片边界生效：置取消标志并关掉连接，阻塞中的收发随之返回。
 *
 *  典型用法：
 *      CCloudTransferRunner& runner = CCloudTransferRunner::GetInstance();
 *      runner.SetMediaModule(module);                 //AppShell 加载 FlamingoMedia 之后
 *      uint32_t id = runner.Start(req);               //CMediaHostAdapter::StartCloudTransfer
 *      runner.Cancel(id);
 *      runner.Shutdown();                             //卸载 FlamingoMedia 之前
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <windows.h>
#include "../../../FlamingoMedia/Api/FlamingoMediaApi.h"

namespace net {
class FileServerConn;
}

namespace ballooncloud {

//一次传输的结果（OnCloudTransferFinished 的 result）：0 成功、-1 被取消，其余为 error_code
const int kCloudTransferCancelled = -1;

//云端录像文件传输；单例，只在 UI 线程上调用公开方法
class CCloudTransferRunner
{
public:
    //连 imgserver 的超时（秒）
    static const int kConnectTimeoutSec = 5;
    //消息窗口收到的两种消息：工作线程交来的进度与结束（窗口私有消息，见开发约定文档的 WM_APP 段）
    static const UINT kMsgProgress = WM_APP + 0x150;
    static const UINT kMsgFinished = WM_APP + 0x151;

    static CCloudTransferRunner& GetInstance();

    /**
     *  设置回报进度与结束的媒体模块（不拥有）。传 NULL 时之后的回报丢弃（卸载 FlamingoMedia 之前）。
     */
    void SetMediaModule(flamingomedia::IMediaModule* module);

    /**
     *  开始一次传输。
     *  @param req 请求，见 FlamingoMediaApi.h 的 MediaCloudTransfer；指针成员只在本次调用期间有效（本方法复制）
     *  @return 传输 id（>0）；参数不合法或消息窗口建不起来时返回 0
     */
    uint32_t Start(const flamingomedia::MediaCloudTransfer& req);

    //取消一次传输；不认识的 id 什么也不做。取消后仍会回报一次结束（结果为 kCloudTransferCancelled）
    void Cancel(uint32_t id);

    //取消全部传输并等工作线程结束（卸载 FlamingoMedia 之前、退出程序时调用）；之后不再回报
    void Shutdown();

    //进行中的传输数（日志与测试用）
    size_t ActiveCount() const;

private:
    //一次传输
    struct Task
    {
        uint32_t                      m_id;          //传输 id
        int                           m_direction;   //方向，取值见 MediaCloudTransferDirection
        std::string                   m_token;       //文件令牌
        std::string                   m_host;        //imgserver 地址
        int                           m_port;        //imgserver 图片端口
        std::wstring                  m_localPath;   //本地文件
        int64_t                       m_bytes;       //文件字节数
        std::atomic<bool>             m_cancel;      //取消标志（UI 线程写，工作线程在分片边界读）
        net::FileServerConn*          m_conn;        //工作线程上正在用的连接（受 m_mutex 保护）；取消时关掉它让阻塞的收发返回，NULL 表示没有
        std::thread                   m_thread;      //工作线程

        Task();
    };

    CCloudTransferRunner();
    ~CCloudTransferRunner();
    CCloudTransferRunner(const CCloudTransferRunner&) = delete;
    CCloudTransferRunner& operator=(const CCloudTransferRunner&) = delete;

    //建消息窗口（第一次 Start 时）
    bool ensureWindow();
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    //工作线程
    void run(std::shared_ptr<Task> task);
    //在 UI 线程上处理结束：回报模块并回收线程
    void onFinished(uint32_t id, int result);

    HWND                                       m_hWnd;       //消息窗口（HWND_MESSAGE）；只在 UI 线程上建
    flamingomedia::IMediaModule*               m_module;     //回报对象（不拥有）；只在 UI 线程上读写
    mutable std::mutex                         m_mutex;      //保护 m_tasks
    std::map<uint32_t, std::shared_ptr<Task> > m_tasks;      //进行中的传输
    uint32_t                                   m_nextId;     //下一个传输 id
};

} // namespace ballooncloud
