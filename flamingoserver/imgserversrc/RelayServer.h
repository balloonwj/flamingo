/**
 *  远程桌面中转服务器，RelayServer.h
 *
 *  仅 imgserver 进程启用：在一个<u>独立端口</u>侦听远程桌面中转连接，为每条
 *  连接建一个 RelaySession，并持有所有会话共享的 RelayHub 配对表。与图片传输
 *  用的 FileServer 各自监听不同端口、互不影响（FileServer / FileSession 为
 *  imgserver 与 fileserver 共用代码，故中转逻辑独立成本类，不污染共用代码）。
 *
 *  balloonwj@qq.com   2026-06-08
 */
#pragma once

#include <memory>
#include <list>
#include <mutex>
#include <string>
#include "../net/TcpServer.h"
#include "../net/EventLoop.h"
#include "RelaySession.h"
#include "RelayHub.h"

using namespace net;

class RelayServer final
{
public:
    RelayServer() = default;
    ~RelayServer() = default;

    RelayServer(const RelayServer& rhs) = delete;
    RelayServer& operator=(const RelayServer& rhs) = delete;

    /**
     *  启动侦听。
     *  @param ip       监听地址
     *  @param port     监听端口（远程桌面中转专用，独立于图片传输端口）
     *  @param loop     主事件循环
     *  @param tokenKey relay 令牌的 HMAC 校验密钥，与 chatserver 共享（relaytokenkey）
     *  @param ioThreadCount 网络 I/O 工作线程数，单位：条线程，取值范围见
     *                       serverconfig::MIN_IO_THREAD_COUNT / MAX_IO_THREAD_COUNT。
     *                       调用方须先用 serverconfig::ReadIoThreadCountConfig 把配置里
     *                       的非法取值挡掉，本函数不再校验
     *  @return 恒返回 true
     */
    bool init(const char* ip, short port, EventLoop* loop, const char* tokenKey,
              int ioThreadCount);
    void uninit();

    //中转端口所用网络 I/O 工作线程数的默认值，单位：条线程。取 4 是为了与本项改造前
    //写死在 RelayServer.cpp 里的 RELAY_SERVER_THREAD_NUM 完全一致：中转连接数远少于
    //文件传输，故比文件端口取得小。配置项 relayiothreadnum 缺失或取值非法时回落到这里
    static const int kDefaultIoThreadCount = 4;

private:
    //新连接到来或断开（通过 conn->connected() 区分）
    void onConnected(std::shared_ptr<TcpConnection> conn);
    //连接断开：从配对表摘除并连带关闭对端、移除会话
    void onDisconnected(const std::shared_ptr<TcpConnection>& conn);

private:
    std::unique_ptr<TcpServer>                  m_server;
    std::list<std::shared_ptr<RelaySession>>    m_sessions;       //所有在线中转会话
    std::mutex                                  m_sessionMutex;   //保护 m_sessions
    std::string                                 m_tokenKey;       //relay 令牌 HMAC 密钥
    RelayHub                                    m_hub;            //会话配对表
};
