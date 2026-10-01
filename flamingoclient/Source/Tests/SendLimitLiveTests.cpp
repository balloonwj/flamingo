/**
 *  单条消息体积上限的端到端联调用例（Suite MSG-L），SendLimitLiveTests.cpp
 *
 *  背景见 bugs.md 的 BUG-32：发消息（cmd_send_message）原先不限制单条内容的大小，接近或
 *  超过 1 MiB 的消息会让发送方（请求帧超过服务端的单帧上限被断开）或接收方（推送 / 同步
 *  整帧超过客户端的单帧上限被断开）掉线。修复后服务端对 content 与 quote 合计超过
 *  MESSAGE_CONTENT_MAX_BYTES（256 KiB）的消息回 error_code_msg_too_large，不落库、不推送。
 *  服务端的判定函数有 doctest（MSG-S1～S5），客户端的发送前检查有 Catch2（CSL1～CSL6）；
 *  本文件验证的是这两者之间缺的一环：<u>服务端处理函数确实接上了这项检查</u>，而且拒收
 *  之后连接仍然正常。客户端界面会在发送前拦下超大消息，因此这里绕过界面，直接组帧发送。
 *
 *    MSG-L1 登录后发一条 300 KiB 的文字：收到 505 且回执带回 clientmsgid；随后心跳照常
 *           有应答（连接没有被断开）；再发一条 100 KiB 的文字照常成功。
 *
 *  <u>默认不参与常规测试</u>：用例带 [.live] 标签，Catch2 会跳过它。要跑就显式指定标签：
 *
 *      FlamingoUnit.exe "[sendlimit]"
 *
 *  跑之前需要一个监听 LiveTestConn.h 的 kTestServerPort（20100）、带测试账号的 chatserver：
 *  运行 tmp/livetest/setup_livetest.py（复制最新编译的 chatserver.exe、改端口、在线备份
 *  tmp/cleantest 的库），再在 tmp/livetest/server/bin 下起 chatserver.exe。库里要有账号
 *  kAccount（密码 kPassword）与对端 kPeerUserId，换库时下面几个常量要跟着改。
 *
 *  balloonwj@qq.com   2026-10-01
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <stdint.h>
#include <string>

//连接、按帧收发与三帧握手的脚手架，以及联调实例的端口与身份公钥常量
#include "LiveTestConn.h"

#include "../ClientVersion.h"              // FLAMINGO_CLIENT_VERSION
#include "../net/FrameCodec.h"
#include "../net/FrameCrypto.h"
#include "../net/FramePayload.h"
#include "../net/HandshakeRunner.h"
#include "../net/LoginProtocolClient.h"
#include "../net/MessageProtocolClient.h"
#include "../net/Protocol.h"
#include "../net/Sha256.h"

using namespace net;
using namespace livetest;

namespace {

// 登录用的测试账号与口令（tmp/cleantest 的库里，张勇）
const char* const kAccount  = "MY0127";
const char* const kPassword = "perf1234";

// 发消息的对端 userid（同一个库里的 MY0308，韩晓燕888）
const int64_t kPeerUserId = 5308;

// 超限的那条文字的正文字节数：300 KiB，明显超过 256 KiB 的单条上限
const size_t kOversizedTextBytes = 300 * 1024;

// 不超限的那条文字的正文字节数：100 KiB，远低于上限，但比日常消息长得多
const size_t kNormalTextBytes = 100 * 1024;

// 各帧的序列号。服务端会原样回填到应答里，据此认出是哪一帧的应答
const uint32_t kLoginSeq     = 1;
const uint32_t kBigSeq       = 2;
const uint32_t kHeartbeatSeq = 3;
const uint32_t kNormalSeq    = 4;

// 等某一帧的应答时最多跳过多少条无关的推送帧（登录后服务端会推送在线状态、会话变化等）
const int kMaxFramesToScan = 64;

// 把明文包体加密、组帧后发出。
bool SendEncrypted(TestConn& conn, ClientHandshake& hs, uint16_t cmd, uint32_t seq,
                   const std::string& body)
{
    std::string wire;
    uint8_t     flags = 0;
    uint32_t    originlen = 0;
    if (!encodeFramePayload(cmd, seq, body, &hs.cipher(), 0, payload_compress_auto,
                            wire, flags, originlen))
    {
        return false;
    }
    std::string frame;
    if (!encodeFrame(cmd, seq, flags, wire.data(), (uint32_t)wire.size(), originlen, frame))
    {
        return false;
    }
    return conn.SendAll(frame);
}

// 一直收到 cmd 与 seq 都匹配的那一帧为止，返回其明文包体。
// 中途收到的推送帧也要逐帧解密：加密是流式的，同一方向的帧必须按顺序解，跳过一帧
// 不解，后面的就全都解不开了。解开之后再丢弃。
bool RecvReply(TestConn& conn, ClientHandshake& hs, uint16_t cmd, uint32_t seq,
               std::string& plainOut)
{
    for (int i = 0; i < kMaxFramesToScan; ++i)
    {
        frame_header header;
        std::string  wire;
        if (!conn.RecvFrame(header, wire))
        {
            return false;   // 超时或对端关闭了连接
        }
        std::string plain;
        std::string err;
        if (!decodeFramePayload(header.cmd, header.seq, header.flags, header.originlen,
                                wire, &hs.cipher(), plain, err))
        {
            return false;
        }
        if (header.cmd == cmd && header.seq == seq)
        {
            plainOut = plain;
            return true;
        }
    }
    return false;
}

// 带上时刻后缀的 clientmsgid：服务端按 clientmsgid 去重，重复运行本用例时不能撞上上一次的
std::string UniqueClientMsgId(const char* prefix)
{
    return std::string(prefix) + "-" + std::to_string((unsigned long long)::GetTickCount64());
}

} // namespace

TEST_CASE("MSG-L1 超过单条上限的消息被服务端拒收且连接不断", "[.live][sendlimit]")
{
    REQUIRE(cryptoInit());

    TestConn conn;
    INFO("cannot connect to the live-test chatserver on port " << kTestServerPort
         << "; see the setup steps at the top of this file");
    REQUIRE(conn.Connect(kTestServerPort));

    ClientHandshake hs;
    std::string reason;
    INFO("handshake failure reason: " << reason);
    REQUIRE(DoHandshake(conn, hs, reason));

    //登录
    LoginRequestParams login;
    login.clientversion = FLAMINGO_CLIENT_VERSION;
    login.account       = kAccount;
    login.password      = Sha256::hexDigest(std::string(kPassword));
    login.clienttype    = platform_pc;
    login.presence      = presence_online;
    login.deviceid      = "livetest-sendlimit";
    login.devicename    = "livetest";
    REQUIRE(SendEncrypted(conn, hs, cmd_login, kLoginSeq, LoginProtocolClient::buildLoginRequest(login)));
    std::string loginPlain;
    REQUIRE(RecvReply(conn, hs, cmd_login, kLoginSeq, loginPlain));
    LoginResponse loginResp;
    REQUIRE(LoginProtocolClient::parseLoginResponse(loginPlain, loginResp));
    INFO("login response: " << loginPlain);
    REQUIRE(loginResp.code == 0);

    //超限的文字：服务端回 505，回执带回 clientmsgid
    const std::string bigId = UniqueClientMsgId("livetest-sizelimit-big");
    REQUIRE(SendEncrypted(conn, hs, cmd_send_message, kBigSeq,
                          MessageProtocolClient::buildSendTextRequest(kPeerUserId, bigId,
                                                                      std::string(kOversizedTextBytes, 'a'))));
    std::string bigPlain;
    REQUIRE(RecvReply(conn, hs, cmd_send_message, kBigSeq, bigPlain));
    SendMessageResponse bigResp;
    REQUIRE(MessageProtocolClient::parseSendResponse(bigPlain, bigResp));
    CHECK(bigResp.m_code == error_code_msg_too_large);
    CHECK(bigResp.m_clientmsgid == bigId);
    CHECK(bigResp.m_msgid == 0);

    //拒收之后连接仍然正常：心跳照常有应答
    REQUIRE(SendEncrypted(conn, hs, cmd_heartbeat, kHeartbeatSeq, std::string()));
    std::string heartbeatPlain;
    CHECK(RecvReply(conn, hs, cmd_heartbeat, kHeartbeatSeq, heartbeatPlain));

    //不超限的长文字照常发出
    const std::string normalId = UniqueClientMsgId("livetest-sizelimit-ok");
    REQUIRE(SendEncrypted(conn, hs, cmd_send_message, kNormalSeq,
                          MessageProtocolClient::buildSendTextRequest(kPeerUserId, normalId,
                                                                      std::string(kNormalTextBytes, 'b'))));
    std::string normalPlain;
    REQUIRE(RecvReply(conn, hs, cmd_send_message, kNormalSeq, normalPlain));
    SendMessageResponse normalResp;
    REQUIRE(MessageProtocolClient::parseSendResponse(normalPlain, normalResp));
    CHECK(normalResp.m_code == 0);
    CHECK(normalResp.m_clientmsgid == normalId);
    CHECK(normalResp.m_msgid > 0);
}
