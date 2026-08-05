/**
 *  中转会话（远程桌面 + 音视频通话共用），RelaySession.h
 *
 *  imgserver relay 通道上的一条连接：首帧握手（远程桌面 cmd_rd_relay_hello /
 *  音视频 cmd_av_relay_hello）携带 relay 令牌，校验通过后按令牌里的 sessionid /
 *  role 登记到 RelayHub 与对端配对；之后每个数据帧（远程桌面 cmd_rd_relay_data
 *  的屏幕帧 / 控制命令、音视频 cmd_av_relay_data 的音视频帧）都按整帧原始字节
 *  原样转发给配对端。两类中转完全同构、共用同一套配对与转发逻辑，仅 cmd 取值
 *  不同（音视频会话与远程桌面会话各用独立 sessionid，天然不串话）。
 *
 *  本会话<u>不解压、不解析</u> data 帧的包体——压缩标志随帧透传、由对端客户端
 *  自行处理；imgserver 只做字节级双向转发。鉴权完全依赖 relay 令牌（RelayToken），
 *  不握手登录、不查库。
 *
 *  balloonwj@qq.com   2026-06-08
 */
#pragma once

#include <stdint.h>
#include <string>
#include "../net/ByteBuffer.h"
#include "../fileserversrc/TcpSession.h"
#include "RelayHub.h"

class RelaySession : public TcpSession
{
public:
    /**
     *  @param conn     本会话所属连接
     *  @param tokenkey relay 令牌的 HMAC 校验密钥（imgserver.conf 的 relaytokenkey）
     *  @param hub      配对表指针（由 RelayServer 持有，生命周期长于本会话；非空）
     */
    RelaySession(const std::shared_ptr<TcpConnection>& conn, const std::string& tokenkey, RelayHub* hub);
    ~RelaySession();

    RelaySession(const RelaySession& rhs) = delete;
    RelaySession& operator=(const RelaySession& rhs) = delete;

    //有数据可读，会被工作 loop 调用
    void onRead(const std::shared_ptr<TcpConnection>& conn, ByteBuffer* pBuffer, Timestamp receivTime);

private:
    //处理首帧握手（cmd_rd_relay_hello / cmd_av_relay_hello）：验令牌 + 配对；返回 false 表示应关连接
    //@param helloCmd 收到的握手 cmd（远程桌面 1853 或音视频 1855），用于回对应 cmd 的应答帧
    bool onHello(const std::shared_ptr<TcpConnection>& conn, uint16_t helloCmd, const std::string& body);
    //处理数据帧（cmd_rd_relay_data / cmd_av_relay_data）：把整帧原始字节原样转发给配对端
    void onData(const std::shared_ptr<TcpConnection>& conn, const char* frameptr, size_t framelen);
    //回握手应答帧（包体 {code,msg}）；应答 cmd 与收到的握手 cmd 一致（1853 / 1855）
    void sendHelloAck(uint16_t helloCmd, int code, const std::string& msg);

private:
    std::string m_tokenKey;    //relay 令牌 HMAC 密钥
    RelayHub*   m_hub;         //配对表（不拥有）
    bool        m_paired;      //是否已完成握手并登记到配对表
    std::string m_sessionId;   //本会话 id（握手后填）
    int         m_role;        //本端角色，见 RelayToken::role_*（握手后填）
};
