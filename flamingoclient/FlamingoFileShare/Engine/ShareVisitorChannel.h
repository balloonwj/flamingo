/**
 *  访问者一端的中转通道：一条到 fileserver 的连接（角色 10，lane 0），ShareVisitorChannel.h
 *
 *  CShareVisitor 收到推给访问者的 2222 后经传输层调用 StartRelay，ShareService 为它建一个本类对象：后台线程连接中转、
 *  完成加密握手与报到（FolderRelayClient::Connect 是阻塞的），结果与之后收到的每个应用帧都作为事件交给
 *  IShareSessionEventSink（ShareService），排队后回到 UI 线程再交给 CShareVisitor。
 *
 *  关闭的做法与分享者一端的 CShareProviderSession 相同：连接线程已结束时 Close 当场关闭并回收线程；仍在 Connect
 *  里阻塞时只做标记、返回 false，等它的连接结果事件到达后再关一次；进程退出时改调 WaitConnectThread。
 *
 *  典型用法（ShareService 内部）：
 *      std::unique_ptr<CShareVisitorChannel> ch(new CShareVisitorChannel(relay, pubKey, this));
 *      ch->StartConnect();
 *      ch->SendFrame(frame);          //UI 线程
 *      if (ch->Close()) ...           //已关闭，可以销毁
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <mutex>
#include <string>
#include <thread>

#include "ShareProtocolCodec.h"     //ShareRelayParams
#include "ShareProviderSession.h"   //ShareSessionEvent / IShareSessionEventSink
#include "../../Source/Feature/FolderTransfer/FolderRelayClient.h"

namespace fileshare {

/**
 *  CShareVisitorChannel —— 访问者一端的一条中转通道。不可拷贝；只能在 UI 线程上创建、关闭与销毁。
 */
class CShareVisitorChannel : public foldertransfer::FolderRelayClientHandler
{
public:
    /**
     *  @param relay        2222 推给访问者的中转参数（拷贝保存）
     *  @param serverPubKey 服务器公钥（32 字节，拷贝保存）；NULL 表示连接全程明文
     *  @param sink         事件接收方（不拥有，须在本对象销毁之前一直有效），不可为 NULL
     */
    CShareVisitorChannel(const ShareRelayParams& relay, const unsigned char* serverPubKey, IShareSessionEventSink* sink);

    //销毁前须已经 Close 成功或 WaitConnectThread 返回；析构时仍会兜底关闭连接、回收线程
    virtual ~CShareVisitorChannel();

    CShareVisitorChannel(const CShareVisitorChannel&) = delete;
    CShareVisitorChannel& operator=(const CShareVisitorChannel&) = delete;

    //启动后台连接线程。只能调用一次
    void StartConnect();

    /**
     *  经 lane 0 发一个应用帧（线程安全）。
     *  @return 已发出返回 true；尚未连上或已断开返回 false
     */
    bool SendFrame(const std::string& frame);

    /**
     *  关闭通道（UI 线程）。
     *  @return 已经关闭（可以销毁本对象）返回 true；连接线程仍在进行时返回 false，待其事件到达后再调用一次
     */
    bool Close();

    //等待连接线程结束（进程退出时使用，最长约 5 秒），之后 Close 必然返回 true
    void WaitConnectThread();

    //访问会话 id
    const std::string& GetAccessId() const;

private:
    //连接线程主体：连接、握手、报到，投递连接结果事件
    void ConnectProc();

    //FolderRelayClientHandler：中转接收线程上收到一个应用帧，拷贝后作为事件投递
    virtual void OnFolderRelayAppFrame(const unsigned char* data, size_t len) override;

    //FolderRelayClientHandler：中转连接异常断开
    virtual void OnFolderRelayClosed() override;

    //投递一个本通道的事件
    void Post(int type, const char* data, size_t len);

private:
    ShareRelayParams                  m_relay;           //中转参数；构造后只读
    unsigned char                     m_serverPubKey[net::IDENTITY_PUBLICKEY_BYTES];   //服务器公钥的副本
    bool                              m_hasServerPubKey; //是否带了服务器公钥
    IShareSessionEventSink*           m_sink;            //事件接收方（不拥有）
    foldertransfer::FolderRelayClient m_client;          //lane 0 中转连接

    std::mutex                        m_mutex;           //保护下面两项
    bool                              m_closing;         //UI 线程是否已要求关闭
    bool                              m_connectDone;     //连接线程是否已结束
    std::thread                       m_connectThread;   //连接线程；Close 或 WaitConnectThread 时回收
};

} // namespace fileshare
