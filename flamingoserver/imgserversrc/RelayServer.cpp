/**
 *  远程桌面中转服务器实现，RelayServer.cpp
 *  balloonwj@qq.com   2026-06-08
 */
#include "RelayServer.h"
#include "../net/InetAddress.h"
#include "../base/AsyncLog.h"

bool RelayServer::init(const char* ip, short port, EventLoop* loop, const char* tokenKey,
                       int ioThreadCount)
{
    m_tokenKey = (tokenKey != NULL) ? tokenKey : "";

    InetAddress addr(ip, port);
    m_server.reset(new TcpServer(loop, addr, "ZYL-MYRelayServer", TcpServer::kReusePort));
    m_server->setConnectionCallback(std::bind(&RelayServer::onConnected, this, std::placeholders::_1));
    //线程数由调用方从配置项 relayiothreadnum 取得，非法取值已在那里回落成 kDefaultIoThreadCount
    m_server->start(ioThreadCount);

    return true;
}

void RelayServer::setScreenShareViewerBufferBytes(size_t bytes)
{
    m_roomRelay.setViewerBufferCapBytes(bytes);
}

void RelayServer::configureCloudRecord(const std::string& dir, uint64_t minFreeBytes)
{
    m_recorder.configure(dir, minFreeBytes, NULL);
    m_recorder.setListener(&m_cloudControl);
    m_cloudControl.setRecorder(&m_recorder);
    m_cloudControl.setStartFn(std::bind(&ScreenShareRoomRelay::startCloudRecord, &m_roomRelay,
                                        std::placeholders::_1, std::placeholders::_2));
    m_cloudControl.setClockFn(&ScreenShareRoomRelay::recordNowMs);
    m_roomRelay.setCloudRecorder(&m_recorder);
    m_cloudRecordOn = true;
    LOGI("cloud record enabled, dir: %s, min free: %llu MB", dir.c_str(),
         static_cast<unsigned long long>(minFreeBytes / (1024 * 1024)));
}

void RelayServer::notifyCloudRecordUploaded(uint64_t recordId, const std::string& fileName, uint64_t bytes)
{
    if (!m_cloudRecordOn)
        return;
    m_cloudControl.notifyUploaded(recordId, fileName, bytes);
}

void RelayServer::uninit()
{
    if (m_server)
        m_server->stop();
    //进行中的录制收尾：写完队列、关文件；回报暂存在控制里（控制连接此时多半已断开）
    m_recorder.shutdown();
}

void RelayServer::onConnected(std::shared_ptr<TcpConnection> conn)
{
    if (conn->connected())
    {
        //关闭 Nagle 算法：中转转发的键鼠帧、指针帧都是几十字节的小包，开着 Nagle 时，上一个包被对端确认之前
        //后续小包会被攒着不发，再遇上接收端的延迟确认（Windows 默认最长 200 毫秒），键鼠操作就会成批晚到。
        //客户端一侧建连时已关闭（Source/net/TcpConnectWait.h 的 ConnectBlockingSocket），这里补上服务端到客户端的方向
        conn->setTcpNoDelay(true);
        LOGI("relay client connected: %s, nodelay=%d", conn->peerAddress().toIpPort().c_str(),
             conn->isTcpNoDelay() ? 1 : 0);
        std::shared_ptr<RelaySession> spSession(new RelaySession(conn, m_tokenKey, &m_hub, &m_roomRelay,
                                                                 m_cloudRecordOn ? &m_cloudControl : NULL));
        conn->setMessageCallback(std::bind(&RelaySession::onRead, spSession.get(),
                                           std::placeholders::_1, std::placeholders::_2,
                                           std::placeholders::_3));
        //输出缓冲写空时通知会话，屏幕共享的丢帧策略据此知道这名观看者的积压已经消退
        conn->setWriteCompleteCallback(std::bind(&RelaySession::onWriteComplete, spSession.get(),
                                                 std::placeholders::_1));

        std::lock_guard<std::mutex> guard(m_sessionMutex);
        m_sessions.push_back(spSession);
    }
    else
    {
        onDisconnected(conn);
    }
}

void RelayServer::onDisconnected(const std::shared_ptr<TcpConnection>& conn)
{
    //屏幕共享房间成员：只摘除自己，房间里的其他人照常收发（发起人掉线由 chatserver 的宽限期处理）
    std::string roomId;
    int seat = -1;
    if (m_roomRelay.leave(conn, &roomId, &seat))
    {
        LOGI("screenshare relay member left, room: %s, seat: %d, client: %s",
             roomId.c_str(), seat, conn->peerAddress().toIpPort().c_str());
    }
    else
    {
        //从配对表摘除本连接；若对端仍在，连带关闭它，使两端一起收尾。
        //配对表保存的是会话，故要再从会话取出它所属的连接
        std::shared_ptr<RelaySession> peerSession = m_hub.leaveByConn(conn.get());
        if (peerSession)
        {
            std::shared_ptr<TcpConnection> peerConn = peerSession->getConnectionPtr();
            if (peerConn)
                peerConn->forceClose();
        }
    }

    //移除本连接对应的会话对象
    std::lock_guard<std::mutex> guard(m_sessionMutex);
    for (std::list<std::shared_ptr<RelaySession>>::iterator iter = m_sessions.begin();
         iter != m_sessions.end(); ++iter)
    {
        if ((*iter)->getConnectionPtr() == conn)
        {
            //云端录制的控制连接断开：之后的事件暂存，等 chatserver 重连
            if ((*iter)->isCloudRecordControl())
            {
                m_cloudControl.detach(iter->get());
                LOGI("cloud record control connection closed: %s", conn->peerAddress().toIpPort().c_str());
            }
            m_sessions.erase(iter);
            LOGI("relay client disconnected: %s", conn->peerAddress().toIpPort().c_str());
            break;
        }
    }
}
