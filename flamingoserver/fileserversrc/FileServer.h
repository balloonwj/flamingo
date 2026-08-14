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
     *  @param ip            监听地址
     *  @param port          监听端口
     *  @param loop          主事件循环
     *  @param fileBaseDir   文件存储根目录
     *  @param tokenKey      上传 / 下载令牌的 HMAC 校验密钥，与 chatserver 共享
     *  @param serverKind    服务器种类，取值见 enum file_server_kind
     *  @param relayTokenKey 单聊文件夹传输中转令牌的 HMAC 校验密钥（fileserver.conf 的
     *                       relaytokenkey，须与 chatserver.conf 的同名项一致）。传 NULL 或
     *                       空串表示本服务<u>不承载</u>文件夹中转，届时 cmd_folder_relay_hello
     *                       一律被拒并记日志——imgserver 复用本类，正是走这条
     *  @param ioThreadCount 网络 I/O 工作线程数，单位：条线程，取值范围见
     *                       serverconfig::MIN_IO_THREAD_COUNT / MAX_IO_THREAD_COUNT。
     *                       调用方须先用 serverconfig::ReadIoThreadCountConfig 把配置里
     *                       的非法取值挡掉，本函数不再校验。<u>两个服务各配各的</u>：
     *                       fileserver 与 imgserver 虽然共用本类，但读的是各自 .conf 里
     *                       的 iothreadnum，文件传输与图片传输的负载并不相同
     *  @return 恒返回 true
     */
    bool init(const char* ip, short port, EventLoop* loop,
              const char* fileBaseDir, const char* tokenKey, int serverKind,
              const char* relayTokenKey, int ioThreadCount);
    void uninit();

    //文件 / 图片端口所用网络 I/O 工作线程数的默认值，单位：条线程。取 6 是为了与本项
    //改造前写死在 FileServer.cpp 里的 FILE_SERVER_THREAD_NUM 完全一致。
    //fileserver 与 imgserver 共用本类，故两个服务的默认值也是这一个
    static const int kDefaultIoThreadCount = 6;

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

    //文件夹传输中转令牌的校验密钥；空串表示本服务不承载文件夹中转，中转握手一律被拒
    std::string                                    m_relayTokenKey;
    //文件夹传输的多通道配对表，按 (sessionid, lane) 把两端的同序号通道配成一对。
    //整个进程只有这一份，构造 FileSession 时把它的地址交给各会话的中转端点（做法同
    //RelayServer 持有 RelayHub）。它必须活得比全部会话都久，故直接按值持有
    FolderRelayHub                                 m_folderRelayHub;
};
