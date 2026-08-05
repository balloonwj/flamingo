/**
 *  文件服务器 / 图片服务器主服务类，FileServer.h
 *
 *  fileserver 与 imgserver 两个可执行程序共用本类（见 CMakeLists.txt），
 *  仅监听端口、缓存目录与服务器种类（file_server_kind）不同。本类负责
 *  侦听连接、为每条连接创建 FileSession。
 *
 *  balloonwj@qq.com   2026-05-22
 */
#pragma once
#include <memory>
#include <list>
#include <mutex>
#include <string>
#include "../net/TcpServer.h"
#include "../net/EventLoop.h"
#include "FileSession.h"

using namespace net;

class FileServer final
{
public:
    FileServer() = default;
    ~FileServer() = default;

    FileServer(const FileServer& rhs) = delete;
    FileServer& operator =(const FileServer& rhs) = delete;

    /**
     *  启动侦听。
     *  @param ip          监听地址
     *  @param port        监听端口
     *  @param loop        主事件循环
     *  @param fileBaseDir 文件存储根目录
     *  @param tokenKey    上传 / 下载令牌的 HMAC 校验密钥，与 chatserver 共享
     *  @param serverKind  服务器种类，取值见 enum file_server_kind
     *  @return 恒返回 true
     */
    bool init(const char* ip, short port, EventLoop* loop,
              const char* fileBaseDir, const char* tokenKey, int serverKind);
    void uninit();

private:
    //新连接到来或连接断开时回调，通过 conn->connected() 区分
    void onConnected(std::shared_ptr<TcpConnection> conn);
    //连接断开处理
    void onDisconnected(const std::shared_ptr<TcpConnection>& conn);

private:
    std::unique_ptr<TcpServer>                     m_server;
    std::list<std::shared_ptr<FileSession>>        m_sessions;
    std::mutex                                     m_sessionMutex;      //保护 m_sessions
    std::string                                    m_strFileBaseDir;    //文件存储根目录
    std::string                                    m_tokenKey;          //令牌校验密钥
    int                                            m_serverKind = 0;    //服务器种类，见 enum file_server_kind
};
