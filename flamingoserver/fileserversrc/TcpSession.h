/**
 *  fileserver / imgserver 会话基类，TcpSession.h
 *
 *  只承载连接生命周期相关的通用逻辑：持有 TcpConnection 的弱引用、提供
 *  取连接与发送字节的接口。具体的 v2 文件传输业务在子类 FileSession。
 *
 *  TcpSession 引用 TcpConnection 必须用弱指针——TcpConnection 可能因网络
 *  出错先行销毁，此时 TcpSession 也应随之失效。
 *
 *  balloonwj@qq.com   2026-05-22
 */
#pragma once

#include <memory>
#include <stddef.h>
#include "../net/TcpConnection.h"

using namespace net;

//fileserver / imgserver 会话基类
class TcpSession
{
public:
    TcpSession(const std::weak_ptr<TcpConnection>& tmpconn);
    ~TcpSession();

    TcpSession(const TcpSession& rhs) = delete;
    TcpSession& operator=(const TcpSession& rhs) = delete;

    //取当前连接的 shared_ptr；连接已销毁时返回空指针
    std::shared_ptr<TcpConnection> getConnectionPtr()
    {
        if (tmpConn_.expired())
            return NULL;

        return tmpConn_.lock();
    }

protected:
    /**
     *  把一段已编码好的字节（通常是一个完整 v2 帧）发送到对端连接。
     *  @param data 待发送字节起始地址
     *  @param len  待发送字节数
     */
    void sendData(const char* data, size_t len);

protected:
    //对 TcpConnection 的弱引用，见类注释
    std::weak_ptr<TcpConnection> tmpConn_;
};
