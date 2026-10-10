/**
 *  远程桌面中转服务器，RelayServer.h
 *
 *  仅 imgserver 进程启用：在一个<u>独立端口</u>侦听远程桌面中转连接，为每条
 *  连接建一个 RelaySession，并持有所有会话共享的 RelayHub 配对表。与图片传输
 *  用的 FileServer 各自监听不同端口、互不影响（FileServer / FileSession 为
 *  imgserver 与 fileserver 共用代码，故中转逻辑独立成本类，不污染共用代码）。
 *
 *  云端录制（第 26 期，屏幕共享与录制方案 12.6.2）：本类同时持有录制器（CloudRecorder）与控制（CloudRecordControl），
 *  配置了录像目录（configureCloudRecord）后，屏幕共享房间的帧另写一份到录像文件，chatserver 的控制连接也在本端口报到。
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
#include "ScreenShareRoomRelay.h"
#include "CloudRecordControl.h"
#include "CloudRecorder.h"

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

    /**
     *  设置屏幕共享每个观看者的积压上限（imgserver 配置项 screenshareviewerbufferbytes）。须在 init 之前调用。
     *  @param bytes 上限，单位字节；为 0 时按默认值 1 MiB 处理
     */
    void setScreenShareViewerBufferBytes(size_t bytes);

    /**
     *  启用云端录制（imgserver 配置项 cloudrecorddir / cloudrecordminfreemb）。须在 init 之前调用；不调用时不录制，
     *  chatserver 的控制连接报到会被拒绝。
     *  @param dir          录像目录，已确保存在、末尾带分隔符
     *  @param minFreeBytes 磁盘剩余空间下限（字节）：开始录制时低于它拒绝，录制中低于它停止
     */
    void configureCloudRecord(const std::string& dir, uint64_t minFreeBytes);

    /**
     *  合成后的录像已上传完（scene 6 上传完成时调用，任意线程）：经控制连接通知 chatserver。没有启用云端录制时什么也不做。
     *  @param recordId 录像 id
     *  @param fileName 录像目录里的文件名
     *  @param bytes    文件字节数
     */
    void notifyCloudRecordUploaded(uint64_t recordId, const std::string& fileName, uint64_t bytes);

    //停止侦听；进行中的云端录制以 shutdown 原因收尾（已录部分保留）
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
    //以下三项的声明顺序决定析构顺序：录制器析构时可能回报到控制，房间中转持有录制器的指针，故控制最先声明、房间中转最后
    CloudRecordControl                          m_cloudControl;   //云端录制控制：处理 chatserver 的命令、回报录制事件
    CloudRecorder                               m_recorder;       //云端录制的录制器
    ScreenShareRoomRelay                        m_roomRelay;      //屏幕共享房间中转（成员表与丢帧策略）
    bool                                        m_cloudRecordOn = false;  //是否已启用云端录制（configureCloudRecord 之后为 true）
};
