/**
 *  文件服务器 / 图片服务器主服务类实现，FileServer.cpp
 *  balloonwj@qq.com   2026-05-22
 */
#include "FileServer.h"
#include "../net/InetAddress.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "FileSession.h"
#include "FileSessionRemoval.h"
#include "FolderRelayEndpoint.h"

bool FileServer::init(const char* ip, short port, EventLoop* loop,
                      const char* fileBaseDir, const char* tokenKey, int serverKind,
                      const char* relayTokenKey, int ioThreadCount)
{
    m_strFileBaseDir = fileBaseDir;
    m_tokenKey       = (tokenKey != NULL) ? tokenKey : "";
    m_serverKind     = serverKind;
    m_relayTokenKey  = (relayTokenKey != NULL) ? relayTokenKey : "";

    //中转密钥缺失时本服务只做上传下载，不承载文件夹传输。这不是错误（imgserver 就不需要
    //中转，fileserver 若尚未在配置里补上 relaytokenkey 也照常提供上传下载），但必须留下
    //一条日志——否则客户端连上来做中转握手被拒时，服务端这边看不出是配置没配
    if (m_relayTokenKey.empty())
    {
        LOGW("folder relay is disabled on this server: relay token key is empty, "
             "cmd_folder_relay_hello will be rejected. "
             "set relaytokenkey in the config file to enable it.");
    }

    InetAddress addr(ip, port);
    m_server.reset(new TcpServer(loop, addr, "ZYL-MYImgAndFileServer", TcpServer::kReusePort));
    m_server->setConnectionCallback(std::bind(&FileServer::onConnected, this, std::placeholders::_1));
    //启动侦听。线程数由调用方从各自 .conf 的配置项 iothreadnum 取得（fileserver 与
    //imgserver 共用本类但各读各的），非法取值已在那里回落成 kDefaultIoThreadCount
    m_server->start(ioThreadCount);

    return true;
}

void FileServer::uninit()
{
    if (m_server)
        m_server->stop();
}

void FileServer::onConnected(std::shared_ptr<TcpConnection> conn)
{
    if (conn->connected())
    {
        LOGI("client connected: %s", conn->peerAddress().toIpPort().c_str());
        std::shared_ptr<FileSession> spSession(
            new FileSession(conn, m_strFileBaseDir.c_str(), m_tokenKey, m_serverKind,
                            m_relayTokenKey, &m_folderRelayHub));
        conn->setMessageCallback(std::bind(&FileSession::onRead, spSession.get(),
                                           std::placeholders::_1, std::placeholders::_2,
                                           std::placeholders::_3));

        std::lock_guard<std::mutex> guard(m_sessionMutex);
        m_sessions.push_back(spSession);
    }
    else
    {
        onDisconnected(conn);
    }
}

void FileServer::onDisconnected(const std::shared_ptr<TcpConnection>& conn)
{
    if (!conn)
    {
        LOGE("onDisconnected: connection is NULL");
        return;
    }

    //先处理文件夹传输中转：把本连接从配对表摘除。任一条通道断开即整个文件夹传输失败
    //（方案定死的语义，不做自动重连），故还要把本次传输当前登记着的其余通道一并关闭，
    //让两端客户端都从「通道断开」这条路径收尾。这一步必须放在下面那把会话表互斥量的
    //加锁范围之外：关闭对端连接会经由各自的 I/O 线程再次进入本函数，若此时已持有
    //m_sessionMutex，而它又是不可重入的 std::mutex，就会死锁
    std::string relaySessionId;
    std::vector<std::shared_ptr<FileSession>> relaySurvivors;
    if (FolderRelayEndpoint::detachByConnection(m_folderRelayHub, conn,
                                                relaySessionId, relaySurvivors))
    {
        LOGI("folder relay lane closed, session: %s, closing %u remaining lane(s) of this "
             "transfer, client: %s",
             relaySessionId.c_str(), static_cast<unsigned>(relaySurvivors.size()),
             conn->peerAddress().toIpPort().c_str());

        for (size_t i = 0; i < relaySurvivors.size(); ++i)
        {
            std::shared_ptr<TcpConnection> peerConn = relaySurvivors[i]->getConnectionPtr();

            //会话对象还在、其连接已销毁：该端没有可关闭的连接，跳过
            if (!peerConn)
                continue;

            peerConn->forceClose();
        }
    }

    std::lock_guard<std::mutex> guard(m_sessionMutex);
    //用户下线，按连接比对定位并移除其会话。查找与移除的纯逻辑抽在 FileSessionRemoval.h，
    //由 tests/test_filesessionremoval.cpp 覆盖；此处只负责加锁与记录日志
    if (fileserver::RemoveSessionByConnection(m_sessions, conn))
    {
        LOGI("client disconnected: %s", conn->peerAddress().toIpPort().c_str());
    }
    else
    {
        //连接断开却找不到对应会话：会话尚未登记或已被提前移除，属异常情况，记录下来便于排查
        LOGW("client disconnected but no matching session found: %s",
             conn->peerAddress().toIpPort().c_str());
    }
}
