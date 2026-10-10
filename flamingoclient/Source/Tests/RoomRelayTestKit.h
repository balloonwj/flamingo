/**
 *  屏幕共享房间中转联调用的测试工具，RoomRelayTestKit.h
 *
 *  直接连 tmp/calltest 测试服务端的 imgserver 中转端口，自己签中转令牌（与 chatserver 用同一把密钥），完成握手、
 *  1859 报到，按 11 字节帧头收发 1860 数据帧。SSRELAY（ScreenShareRelayLiveTests.cpp）与压测用例
 *  SSSTRESS（ScreenShareStressLiveTests.cpp）共用。不经过 chatserver —— imgserver 只认令牌。
 *
 *  典型用法：
 *      roomrelaytest::RoomPeer host;
 *      std::string reason;
 *      int code = -1;
 *      if (roomrelaytest::joinRoom(host, room, RelayToken::role_screenshare_host, 0, roomrelaytest::kRoomTokenKey,
 *                                  code, reason) && code == 0)
 *          roomrelaytest::sendRoomFrame(host, screenshare_track_screen, 0, roomrelaytest::kWholeKey, ts, payload);
 *
 *  balloonwj@qq.com   2026-10-03（从 ScreenShareRelayLiveTests.cpp 抽出）
 */
#pragma once

#include <stdint.h>
#include <string>

#include "LiveTestConn.h"
#include "../net/Protocol.h"

namespace roomrelaytest {

//tmp/calltest 测试服务端的 imgserver 中转端口
const unsigned short kRoomRelayPort = 26003;

//中转令牌的 HMAC 密钥（tmp/calltest/server/etc 下 chatserver.conf 与 imgserver.conf 的 relaytokenkey）
const char* const kRoomTokenKey = "flamingo-relay-token-secret";
//一把错误的密钥，用来签出验签不过的令牌
const char* const kWrongTokenKey = "not-the-relay-token-secret";

//常用的标志位组合：不分片的整帧、不分片的关键帧
const int kWhole = screenshare_flag_first_frag | screenshare_flag_last_frag;
const int kWholeKey = kWhole | screenshare_flag_keyframe;

//一个房间成员的连接
struct RoomPeer
{
    livetest::TestConn   m_conn;   //到 imgserver 中转端口的连接
    net::ClientHandshake m_hs;     //握手器，握手成功后其 cipher() 用于加解密
    uint32_t             m_seq;    //发帧序列号，每发一帧自增

    RoomPeer()
        : m_seq(0)
    {
    }
};

//一帧收到的房间中转数据
struct RoomFrame
{
    int         m_track;       //轨道类型
    int         m_seat;        //发送者座位号
    int         m_flags;       //标志位
    uint64_t    m_timestamp;   //时间戳
    std::string m_payload;     //载荷

    RoomFrame()
        : m_track(0)
        , m_seat(0)
        , m_flags(0)
        , m_timestamp(0)
    {
    }
};

//生成本进程内唯一的房间号（不含 '.' 与 '|'，可作令牌的 sessionid）
std::string makeRoomId();

/**
 *  签一个房间中转令牌。
 *  @param role 角色：RelayToken::role_screenshare_host / role_screenshare_member
 *  @param seat 座位号（写进令牌的 peeruid）
 *  @param key  HMAC 密钥
 */
std::string makeToken(const std::string& roomId, int role, int seat, const char* key);

/**
 *  连上中转端口、握手、发 1859 报到并收应答。
 *  @param code   [出参] 报到应答里的错误码
 *  @param reason [出参] 失败原因
 *  @return 收到并解开了报到应答返回 true
 */
bool joinRoom(RoomPeer& p, const std::string& roomId, int role, int seat, const char* key, int& code,
              std::string& reason);

/**
 *  发一帧 1860。
 *  @param seat           帧头里填的发送者座位号（服务端会改写为本连接登记的座位）
 *  @param markCompressed 为 true 时在帧头置上压缩标志（包体并未真的压缩），用来检查服务端拒绝转发压缩帧
 */
bool sendRoomFrame(RoomPeer& p, int track, int seat, int flags, uint64_t timestamp, const std::string& payload,
                   bool markCompressed = false);

//收一帧 1860 并解出帧头与载荷；超时（3 秒）、断开、解密失败或命令号不对返回 false
bool recvRoomFrame(RoomPeer& p, RoomFrame& out);

//收到的是否为服务端发出的、要求某视频轨的关键帧请求
bool isKeyFrameRequest(const RoomFrame& f, int videoTrack);

//生成一段长度为 len、内容随 salt 变化的载荷
std::string makePayload(size_t len, int salt);

} // namespace roomrelaytest
