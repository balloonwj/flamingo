/**
 *  远程桌面中转会话实现，RelaySession.cpp
 *  balloonwj@qq.com   2026-06-08
 */
#include "RelaySession.h"
#include <time.h>
#include <functional>
#include "../net/EventLoop.h"
#include "../net/TcpConnection.h"
#include "../net/FrameCodec.h"
#include "../net/FrameCrypto.h"
#include "../net/FramePayload.h"
#include "../base/AsyncLog.h"
#include "../utils/RelayToken.h"
#include "../jsoncpp1.9.0/json.h"
#include "../chatserversrc/Protocol.h"   // cmd_rd_relay_* / error_code_*
#include "RelayHelloPolicy.h"           // 报到命令与角色、数据命令与报到种类的判断
#include "ScreenShareRoomRelay.h"
#include "RoomRelayFrame.h"

using namespace net;

//把 Json::Value 序列化为紧凑 JSON 文本
static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//把 UTF-8 JSON 文本解析为 Json::Value；解析失败或非对象返回 false
static bool parseJsonObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

RelaySession::RelaySession(const std::shared_ptr<TcpConnection>& conn,
                           const std::string& tokenkey, RelayHub* hub, ScreenShareRoomRelay* roomRelay,
                           CloudRecordControl* cloudControl)
    : TcpSession(conn),
      m_tokenKey(tokenkey),
      m_hub(hub),
      m_paired(false),
      m_role(0),
      m_pairKind(relay_pair_none),
      m_roomRelay(roomRelay),
      m_inRoom(false),
      m_seat(-1),
      m_trackDropLogged(false),
      m_inputDropLogged(false),
      m_queuedBytes(0),
      m_outputBytes(0),
      m_cloudControl(cloudControl),
      m_control(false)
{
}

bool RelaySession::isCloudRecordControl() const
{
    return m_control.load();
}

void RelaySession::sendCloudRecordCtl(const std::string& body)
{
    sendFrame(cmd_cloudrec_ctl_data, 0, 0, body);
}

size_t RelaySession::pendingBytes() const
{
    return m_queuedBytes.load() + m_outputBytes.load();
}

void RelaySession::onWriteComplete(const std::shared_ptr<TcpConnection>& conn)
{
    (void)conn;
    m_outputBytes.store(0);
}

RelaySession::~RelaySession()
{
}

void RelaySession::onRead(const std::shared_ptr<TcpConnection>& conn, ByteBuffer* pBuffer, Timestamp receivTime)
{
    (void)receivTime;
    while (true)
    {
        frame_header header;
        std::string framebody;
        size_t consumed = 0;
        //relay 通道承载屏幕帧 / 文件级大包，用 fileserver 的包体上限
        frame_decode_result r = decodeFrame(pBuffer->peek(), pBuffer->readableBytes(),
                                            header, framebody, consumed, MAX_BODY_FILESERVER);
        //数据不足一个完整帧，等待后续字节
        if (r == frame_decode_need_more)
            return;
        //协议错误（魔数 / 版本 / 长度 / CRC 非法），关闭连接
        if (r == frame_decode_error)
        {
            LOGE("relay decodeFrame error, close connection, client: %s",
                 conn->peerAddress().toIpPort().c_str());
            conn->forceClose();
            return;
        }

        //成功解出一帧，前移读指针。转发不再依赖整帧原始字节（改为解密后重新加密），
        //故不必像以前那样把读指针留到处理完再前移
        pBuffer->retrieve(consumed);

        //协议加密的前置处理：握手分派与加密状态校验，必须排在业务处理之前。
        //这里只做校验、不还原包体——中转数据帧要走「只解密不解压」的路径，
        //与普通业务帧的还原步骤不同，故两者分开，判定条件仍与其它服务共用一份
        std::string response;
        std::string err;
        net::frame_security_result sr =
            net::checkInboundFrameSecurity(m_handshake, header.cmd, header.flags,
                                           framebody, response, err);
        if (sr == net::frame_security_error)
        {
            LOGE("relay frame security check failed: %s, cmd: %u, close connection, client: %s",
                 err.c_str(), static_cast<unsigned>(header.cmd),
                 conn->peerAddress().toIpPort().c_str());
            conn->forceClose();
            return;
        }

        //本帧是握手帧，已在上面处理完毕，回完应答就接着读下一帧，不进业务分发
        if (sr == net::frame_security_handled)
        {
            //三帧握手里只有第 2 帧有应答。此刻 m_handshake.ready() 仍为 false，
            //sendFrame 走的正是明文分支——客户端那时也还没初始化接收流
            if (!response.empty())
                sendFrame(cmd_handshake, header.seq, 0, response);

            if (m_handshake.ready())
            {
                LOGI("relay handshake done, frame encryption is now active, client: %s, key fingerprint: %s",
                     conn->peerAddress().toIpPort().c_str(),
                     net::cryptoServerIdentity().fingerprint().c_str());
            }
            continue;
        }

        bool ok = true;
        if (header.cmd == cmd_rd_relay_hello || header.cmd == cmd_av_relay_hello || header.cmd == cmd_p2p_relay_hello)
        {
            //报到帧的包体本会话自己要读，故按普通业务帧完整还原（解密 + 解压）
            std::string plainbody;
            if (!net::decodeFramePayload(header.cmd, header.seq, header.flags, header.originlen,
                                         framebody, &m_handshake.cipher(), plainbody, err))
            {
                LOGE("relay hello: decode payload failed: %s, client: %s",
                     err.c_str(), conn->peerAddress().toIpPort().c_str());
                ok = false;
            }
            else
            {
                ok = onHello(conn, header.cmd, plainbody);
            }
        }
        else if (header.cmd == cmd_rd_relay_data || header.cmd == cmd_av_relay_data || header.cmd == cmd_p2p_relay_data)
        {
            ok = onData(conn, header.cmd, header.seq, header.flags, header.originlen, framebody);
        }
        else if (header.cmd == cmd_screenshare_relay_hello)
        {
            //屏幕共享报到帧：与上面两种报到帧一样按普通业务帧完整还原
            std::string plainbody;
            if (!net::decodeFramePayload(header.cmd, header.seq, header.flags, header.originlen,
                                         framebody, &m_handshake.cipher(), plainbody, err))
            {
                LOGE("screenshare relay hello: decode payload failed: %s, client: %s",
                     err.c_str(), conn->peerAddress().toIpPort().c_str());
                ok = false;
            }
            else
            {
                ok = onRoomHello(conn, plainbody);
            }
        }
        else if (header.cmd == cmd_screenshare_relay_data)
        {
            ok = onRoomData(conn, header.cmd, header.seq, header.flags, header.originlen, framebody);
        }
        else if (header.cmd == cmd_cloudrec_ctl_hello)
        {
            //云端录制控制连接的报到帧：与其它报到帧一样按普通业务帧完整还原
            std::string plainbody;
            if (!net::decodeFramePayload(header.cmd, header.seq, header.flags, header.originlen,
                                         framebody, &m_handshake.cipher(), plainbody, err))
            {
                LOGE("cloud record control hello: decode payload failed: %s, client: %s",
                     err.c_str(), conn->peerAddress().toIpPort().c_str());
                ok = false;
            }
            else
            {
                ok = onControlHello(conn, plainbody);
            }
        }
        else if (header.cmd == cmd_cloudrec_ctl_data)
        {
            ok = onControlData(conn, header.cmd, header.seq, header.flags, header.originlen, framebody);
        }
        else
        {
            LOGE("relay unsupported cmd: %u, client: %s",
                 static_cast<unsigned>(header.cmd), conn->peerAddress().toIpPort().c_str());
            ok = false;
        }

        if (!ok)
        {
            conn->forceClose();
            return;
        }
    }
}

bool RelaySession::onHello(const std::shared_ptr<TcpConnection>& conn, uint16_t helloCmd, const std::string& body)
{
    //重复报到、或已在屏幕共享房间里的连接再做一对一报到，都视为非法。原先只判断了已配对：房间连接还能再加入
    //配对表，断开时 RelayServer::onDisconnected 只把它从房间摘除，配对表里留下残留表项，之后同一会话同一角色的
    //报到会因「角色重复」被拒（P2P 第 2f 步用联调用例 P2PR5 复现后修正）
    if (!RelayHelloPolicy::canAcceptPairHello(m_paired || m_control.load(), m_inRoom))
    {
        LOGE("relay hello rejected, already paired: %d, in room: %d, client: %s", m_paired ? 1 : 0, m_inRoom ? 1 : 0,
             conn->peerAddress().toIpPort().c_str());
        return false;
    }

    Json::Value root;
    if (!parseJsonObject(body, root))
    {
        sendHelloAck(helloCmd, error_code_bad_request, "bad request");
        return false;
    }
    std::string token = root["token"].isString() ? root["token"].asString() : std::string();

    //校验 relay 令牌：验签 + 验过期
    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    RelayTokenPayload payload;
    if (!RelayToken::verify(token, m_tokenKey, now, payload))
    {
        LOGE("relay hello: invalid or expired token, client: %s",
             conn->peerAddress().toIpPort().c_str());
        sendHelloAck(helloCmd, error_code_session_expired, "invalid or expired token");
        return false;
    }

    //报到命令与令牌角色须相符：P2P 报到只接受角色 11 / 12，远程桌面与音视频报到不接受 11 / 12
    const int pairKind = RelayHelloPolicy::pairKindOfHello(helloCmd);
    if (!RelayHelloPolicy::isAcceptedPairRole(pairKind, payload.role))
    {
        LOGE("relay hello: role not accepted, cmd: %u, role: %d, session: %s, client: %s",
             static_cast<unsigned>(helloCmd), payload.role, payload.sessionid.c_str(),
             conn->peerAddress().toIpPort().c_str());
        sendHelloAck(helloCmd, error_code_bad_request, "role not accepted");
        return false;
    }

    m_sessionId = payload.sessionid;
    m_role      = payload.role;
    m_pairKind  = pairKind;

    //登记到配对表；同角色重复 / 会话满员则失败。
    //配对表保存的是会话弱引用，故这里要把自己的 shared_ptr 取出来交进去
    if (!m_hub->join(m_sessionId, m_role, shared_from_this()))
    {
        LOGE("relay hello: join failed (dup role or full), session: %s, role: %d, client: %s",
             m_sessionId.c_str(), m_role, conn->peerAddress().toIpPort().c_str());
        sendHelloAck(helloCmd, error_code_server_internal, "join failed");
        return false;
    }

    m_paired = true;
    sendHelloAck(helloCmd, error_code_ok, "ok");
    LOGI("relay hello ok, session: %s, role: %d, client: %s",
         m_sessionId.c_str(), m_role, conn->peerAddress().toIpPort().c_str());
    return true;
}

bool RelaySession::onData(const std::shared_ptr<TcpConnection>& conn, uint16_t cmd, uint32_t seq,
                          uint8_t flags, uint32_t originlen, const std::string& wireBody)
{
    //先解密、再判断该不该转发。<u>解密这一步无论如何都要做</u>：流加密的内部状态
    //按帧推进，一旦漏解一帧，这个方向后面所有帧都会认证失败。所以「尚未报到」
    //「对端不在」这类要丢帧的情形也必须先把本帧解出来
    std::string plainbody;
    std::string err;
    if (!net::decryptFramePayload(cmd, seq, flags, originlen, wireBody,
                                  &m_handshake.cipher(), plainbody, err))
    {
        LOGE("relay data: decrypt failed: %s, cmd: %u, close connection, client: %s",
             err.c_str(), static_cast<unsigned>(cmd), conn->peerAddress().toIpPort().c_str());
        return false;
    }

    //未报到就发数据：丢弃（不关连接，等其先报到）
    if (!m_paired)
    {
        LOGE("relay data before hello, client: %s", conn->peerAddress().toIpPort().c_str());
        return true;
    }

    //数据命令须与报到种类相符：P2P 连接只转发 1862，远程桌面与音视频连接不转发 1862。不相符时丢弃本帧，不关连接
    if (!RelayHelloPolicy::isForwardableData(m_pairKind, cmd))
    {
        LOGE("relay data cmd does not match hello kind, drop, cmd: %u, kind: %d, session: %s, client: %s",
             static_cast<unsigned>(cmd), m_pairKind, m_sessionId.c_str(), conn->peerAddress().toIpPort().c_str());
        return true;
    }

    //定位对端会话：对端尚未连上 / 已断开时丢弃本帧（客户端靠 chatserver 信令收尾）
    std::shared_ptr<RelaySession> peer = m_hub->getPeer(m_sessionId, conn.get());
    if (!peer)
        return true;

    //交给对端会话重新加密后发出：包体仍保持发送端压缩后的形态，压缩标志随帧透传
    peer->forwardFrame(cmd, seq, flags, originlen, plainbody);
    return true;
}

void RelaySession::forwardFrame(uint16_t cmd, uint32_t seq, uint8_t srcFlags, uint32_t originlen,
                                const std::string& plainBody)
{
    net::EventLoop* loop = getOwnLoop();
    if (loop == NULL)
    {
        LOGE("relay forward: TcpConnection is destroyed, drop frame, cmd: %u",
             static_cast<unsigned>(cmd));
        return;
    }

    //计入积压：从这里起到 forwardFrameInLoop 把它写进连接为止，这些字节都算「还没发出去」
    m_queuedBytes.fetch_add(plainBody.size());

    //本函数运行在<u>配对端</u>的 I/O 线程上，故必须把整段动作投递回本连接自己的线程，
    //理由见 sendFrame 里的说明
    if (!loop->isInLoopThread())
    {
        loop->runInLoop(std::bind(&RelaySession::forwardFrameInLoop, shared_from_this(),
                                  cmd, seq, srcFlags, originlen, plainBody));
        return;
    }

    forwardFrameInLoop(cmd, seq, srcFlags, originlen, plainBody);
}

void RelaySession::forwardFrameInLoop(uint16_t cmd, uint32_t seq, uint8_t srcFlags,
                                      uint32_t originlen, const std::string& plainBody)
{
    //只加密、不压缩：压缩是两端客户端之间的语义，服务端既不解压也不重压，
    //原帧的压缩标志与 originlen 都原样透传，对端客户端据此自行解压
    std::string wire;
    uint8_t     finalFlags = 0;
    if (!net::encryptFramePayload(cmd, seq, srcFlags, originlen, plainBody,
                                  &m_handshake.cipher(), wire, finalFlags))
    {
        LOGE("relay forward: encrypt payload failed, cmd: %u, bodylen: %u",
             static_cast<unsigned>(cmd), static_cast<unsigned>(plainBody.size()));
        m_queuedBytes.fetch_sub(plainBody.size());
        return;
    }

    std::string out;
    if (!encodeFrame(cmd, seq, finalFlags, wire.data(),
                     static_cast<uint32_t>(wire.size()), originlen, out))
    {
        LOGE("relay forward: encodeFrame failed, cmd: %u, bodylen: %u",
             static_cast<unsigned>(cmd), static_cast<unsigned>(wire.size()));
        m_queuedBytes.fetch_sub(plainBody.size());
        return;
    }

    sendData(out.data(), out.size());

    //本函数在本连接的 EventLoop 线程里执行，此刻读输出缓冲是安全的：写不进 socket 的部分都留在那里
    std::shared_ptr<TcpConnection> conn = getConnectionPtr();
    m_outputBytes.store(conn ? conn->outputBuffer()->readableBytes() : 0);
    m_queuedBytes.fetch_sub(plainBody.size());
}

bool RelaySession::onRoomHello(const std::shared_ptr<TcpConnection>& conn, const std::string& body)
{
    //一条连接只报到一次，两端配对、房间与云端录制控制连接不能混用
    if (m_paired || m_inRoom || m_control.load())
    {
        LOGE("screenshare relay duplicate hello, client: %s", conn->peerAddress().toIpPort().c_str());
        return false;
    }

    Json::Value root;
    if (!parseJsonObject(body, root))
    {
        sendHelloAck(cmd_screenshare_relay_hello, error_code_bad_request, "bad request");
        return false;
    }
    std::string token = root["token"].isString() ? root["token"].asString() : std::string();

    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    RelayTokenPayload payload;
    if (!RelayToken::verify(token, m_tokenKey, now, payload))
    {
        LOGE("screenshare relay hello: invalid or expired token, client: %s",
             conn->peerAddress().toIpPort().c_str());
        sendHelloAck(cmd_screenshare_relay_hello, error_code_session_expired, "invalid or expired token");
        return false;
    }

    //只接受屏幕共享的三种角色（第 22 期加了只推流的共享者）与群通话成员的角色：远程桌面、通话、文件夹传输的令牌不能拿来进房间
    if (payload.role != RelayToken::role_screenshare_host && payload.role != RelayToken::role_screenshare_member &&
        payload.role != RelayToken::role_screenshare_publisher && payload.role != RelayToken::role_groupcall_member)
    {
        LOGE("screenshare relay hello: role %d is not a room role, session: %s, client: %s",
             payload.role, payload.sessionid.c_str(), conn->peerAddress().toIpPort().c_str());
        sendHelloAck(cmd_screenshare_relay_hello, error_code_bad_request, "role mismatch");
        return false;
    }

    //座位号由 chatserver 写在令牌的 peeruid 字段里；座位越界、发起人不坐 0 号、座位已被占用都在登记时拒绝
    const int seat = static_cast<int>(payload.peeruid);
    if (!m_roomRelay->join(payload.sessionid, seat, payload.role, shared_from_this(), payload.uid))
    {
        LOGE("screenshare relay hello: join failed (bad seat, seat taken or role mismatch), room: %s, seat: %d, "
             "role: %d, client: %s", payload.sessionid.c_str(), seat, payload.role,
             conn->peerAddress().toIpPort().c_str());
        sendHelloAck(cmd_screenshare_relay_hello, error_code_server_internal, "join failed");
        return false;
    }

    m_inRoom = true;
    m_roomId = payload.sessionid;
    m_seat = seat;
    m_role = payload.role;
    sendHelloAck(cmd_screenshare_relay_hello, error_code_ok, "ok");
    LOGI("screenshare relay hello ok, room: %s, seat: %d, role: %d, client: %s",
         m_roomId.c_str(), m_seat, m_role, conn->peerAddress().toIpPort().c_str());
    return true;
}

bool RelaySession::onRoomData(const std::shared_ptr<TcpConnection>& conn, uint16_t cmd, uint32_t seq,
                              uint8_t flags, uint32_t originlen, const std::string& wireBody)
{
    //与 onData 一样，无论这一帧最终转不转发都要先解密，否则这个方向的流加密状态会错位
    std::string plainbody;
    std::string err;
    if (!net::decryptFramePayload(cmd, seq, flags, originlen, wireBody,
                                  &m_handshake.cipher(), plainbody, err))
    {
        LOGE("screenshare relay data: decrypt failed: %s, close connection, client: %s",
             err.c_str(), conn->peerAddress().toIpPort().c_str());
        return false;
    }

    //未报到就发数据：丢弃（不关连接，等其先报到）
    if (!m_inRoom)
    {
        LOGE("screenshare relay data before hello, client: %s", conn->peerAddress().toIpPort().c_str());
        return true;
    }

    //约定 1860 一律不压缩：压缩过的包体读不出帧头，丢弃本帧，连接保持
    if ((flags & frame_flag_compressed) != 0)
    {
        LOGE("screenshare relay data: compressed frame dropped, room: %s, seat: %d, client: %s",
             m_roomId.c_str(), m_seat, conn->peerAddress().toIpPort().c_str());
        return true;
    }

    RoomRelayFrameHeader header;
    if (!RoomRelayFrame::parseHeader(plainbody.data(), plainbody.size(), header))
    {
        LOGE("screenshare relay data: bad frame header, dropped, room: %s, seat: %d, bodylen: %u",
             m_roomId.c_str(), m_seat, static_cast<unsigned>(plainbody.size()));
        return true;
    }

    //群通话房间只转发摄像头、麦克风与控制帧，屏幕、电脑声音、标注轨道直接丢弃（docs/协议设计-v2.md 21.8）；
    //每条连接只记第一次，避免出错的客户端把日志刷满
    if (!RoomRelayFrame::isTrackAllowedForRole(m_role, header.m_track))
    {
        if (!m_trackDropLogged)
        {
            LOGE("room relay data: track %d not allowed for role %d, dropped, room: %s, seat: %d",
                 (int)header.m_track, m_role, m_roomId.c_str(), m_seat);
            m_trackDropLogged = true;
        }
        return true;
    }

    //控制帧里的订阅（群通话第 5 期）：服务端自己消费，不转发
    if (header.m_track == screenshare_track_control)
    {
        std::vector<int> seats;
        if (RoomRelayFrame::parseSubscribe(plainbody, seats))
        {
            m_roomRelay->setSubscription(m_roomId, m_seat, seats);
            return true;
        }
        //群通话里没有共享内容状态等其它控制帧，成员发来的一律不转发
        if (m_role == RelayToken::role_groupcall_member)
            return true;
        //键鼠输入（屏幕共享第 18 期）：只转发给目标座位（画面主人），不广播，免得其他观看者收到键盘输入。
        //是否真是当前控制者由画面主人按发送者座位核对，服务端不知道控制关系
        int targetSeat = -1;
        if (RoomRelayFrame::parseInputTarget(plainbody, targetSeat))
        {
            RoomRelayFrame::rewriteSenderSeat(plainbody, static_cast<uint8_t>(m_seat));
            if (!m_roomRelay->forwardTo(m_roomId, m_seat, targetSeat, cmd, seq, flags, originlen, plainbody) &&
                !m_inputDropLogged)
            {
                LOGE("room relay data: input frame target seat %d not in room, dropped, room: %s, seat: %d",
                     targetSeat, m_roomId.c_str(), m_seat);
                m_inputDropLogged = true;
            }
            return true;
        }
    }

    //发送者座位号以本连接登记的为准，客户端填错或冒充都无效
    RoomRelayFrame::rewriteSenderSeat(plainbody, static_cast<uint8_t>(m_seat));
    header.m_senderSeat = static_cast<uint8_t>(m_seat);
    m_roomRelay->forward(m_roomId, conn, header, cmd, seq, flags, originlen, plainbody);
    return true;
}

bool RelaySession::onControlHello(const std::shared_ptr<TcpConnection>& conn, const std::string& body)
{
    //一条连接只报到一次，控制连接不能与配对、房间混用
    if (m_paired || m_inRoom || m_control.load())
    {
        LOGE("cloud record control hello rejected, already registered, client: %s",
             conn->peerAddress().toIpPort().c_str());
        return false;
    }

    Json::Value root;
    if (!parseJsonObject(body, root))
    {
        sendHelloAck(cmd_cloudrec_ctl_hello, error_code_bad_request, "bad request");
        return false;
    }
    std::string token = root["token"].isString() ? root["token"].asString() : std::string();

    int64_t now = static_cast<int64_t>(time(NULL)) * 1000;
    RelayTokenPayload payload;
    if (!RelayToken::verify(token, m_tokenKey, now, payload))
    {
        LOGE("cloud record control hello: invalid or expired token, client: %s",
             conn->peerAddress().toIpPort().c_str());
        sendHelloAck(cmd_cloudrec_ctl_hello, error_code_session_expired, "invalid or expired token");
        return false;
    }

    //只接受服务端控制连接的角色：客户端的令牌（远程桌面、通话、屏幕共享等）不能拿来冒充 chatserver
    if (payload.role != RelayToken::role_server_control)
    {
        LOGE("cloud record control hello: role %d is not the server control role, session: %s, client: %s",
             payload.role, payload.sessionid.c_str(), conn->peerAddress().toIpPort().c_str());
        sendHelloAck(cmd_cloudrec_ctl_hello, error_code_bad_request, "role mismatch");
        return false;
    }

    if (m_cloudControl == NULL)
    {
        LOGE("cloud record control hello: cloud record is not configured, client: %s",
             conn->peerAddress().toIpPort().c_str());
        sendHelloAck(cmd_cloudrec_ctl_hello, error_code_server_internal, "cloud record not configured");
        return false;
    }

    m_control.store(true);
    m_sessionId = payload.sessionid;
    m_role = payload.role;
    //先回报到应答，再登记为出口：登记时会补发暂存的事件，chatserver 要先收到应答
    sendHelloAck(cmd_cloudrec_ctl_hello, error_code_ok, "ok");
    m_cloudControl->attach(shared_from_this());
    LOGI("cloud record control hello ok, session: %s, client: %s", m_sessionId.c_str(),
         conn->peerAddress().toIpPort().c_str());
    return true;
}

bool RelaySession::onControlData(const std::shared_ptr<TcpConnection>& conn, uint16_t cmd, uint32_t seq,
                                 uint8_t flags, uint32_t originlen, const std::string& wireBody)
{
    //与其它数据帧一样，先还原包体再判断：漏解一帧会使这个方向后面的帧全部解密失败
    std::string plainbody;
    std::string err;
    if (!net::decodeFramePayload(cmd, seq, flags, originlen, wireBody, &m_handshake.cipher(), plainbody, err))
    {
        LOGE("cloud record control data: decode payload failed: %s, close connection, client: %s",
             err.c_str(), conn->peerAddress().toIpPort().c_str());
        return false;
    }
    if (!m_control.load() || m_cloudControl == NULL)
    {
        LOGE("cloud record control data before control hello, close connection, client: %s",
             conn->peerAddress().toIpPort().c_str());
        return false;
    }
    const std::string reply = m_cloudControl->onCommand(plainbody);
    if (!reply.empty())
        sendFrame(cmd_cloudrec_ctl_data, 0, 0, reply);
    return true;
}

void RelaySession::sendHelloAck(uint16_t helloCmd, int code, const std::string& msg)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;
    std::string body = writeCompact(root);

    //应答 cmd 与请求一致：远程桌面回 cmd_rd_relay_hello、音视频回 cmd_av_relay_hello
    sendFrame(helloCmd, 0, 0, body);
}

void RelaySession::sendFrame(uint16_t cmd, uint32_t seq, uint8_t extraFlags, const std::string& body)
{
    net::EventLoop* loop = getOwnLoop();
    if (loop == NULL)
    {
        LOGE("relay send: TcpConnection is destroyed, drop frame, cmd: %u",
             static_cast<unsigned>(cmd));
        return;
    }

    //不在本连接所属的 EventLoop 线程时，把整段「组帧 + 加密 + 写 socket」投递回去执行。
    //
    //加密器是每条连接一份的流式对象，每帧推进一次内部状态，接收端严格按序解密；本连接
    //的帧却有两个来源——本会话自己回的应答帧，以及配对端 I/O 线程经 forwardFrame
    //转发来的数据帧。若在调用方线程里就地加密、只把写 socket 投递回来，「加密的先后」
    //与「写到线上的先后」就可能相反：配对端线程加密好的那一帧要排队等本 loop 处理，
    //而本 loop 线程自己发的帧是直接写出去的，后加密的反而先到线上，接收端必然解不开。
    //把加密也一并放到连接自己的线程里，两个顺序就恒等（这也是这里不再需要发送锁的原因）。
    //
    //任务里捕获 shared_from_this() 而不是裸 this：任务真正执行时本会话可能已被
    //配对表与会话表移除，捕获 shared_ptr 才能保证那时对象还在。
    if (!loop->isInLoopThread())
    {
        loop->runInLoop(std::bind(&RelaySession::sendFrameInLoop, shared_from_this(),
                                  cmd, seq, extraFlags, body));
        return;
    }

    sendFrameInLoop(cmd, seq, extraFlags, body);
}

void RelaySession::sendFrameInLoop(uint16_t cmd, uint32_t seq, uint8_t extraFlags,
                                   const std::string& body)
{
    //包体的压缩与加密都由 FramePayload 这一条管线负责，顺序也在那里定死。
    //压缩策略取 never：本函数只用来发握手应答与报到应答，包体都是几十字节的 JSON，
    //压了反而更长。加密器未就绪（未启用加密 / 握手未完成）时它自然产出明文，
    //握手应答正是走这条
    std::string wire;
    uint8_t     finalFlags = 0;
    uint32_t    originlen = 0;
    if (!net::encodeFramePayload(cmd, seq, body, &m_handshake.cipher(), extraFlags,
                                 net::payload_compress_never, wire, finalFlags, originlen))
    {
        LOGE("relay send: encode v2 frame payload failed, cmd: %u, bodylen: %u",
             static_cast<unsigned>(cmd), static_cast<unsigned>(body.size()));
        return;
    }

    std::string out;
    if (!encodeFrame(cmd, seq, finalFlags, wire.data(),
                     static_cast<uint32_t>(wire.size()), originlen, out))
    {
        LOGE("relay send: encodeFrame failed, cmd: %u, bodylen: %u",
             static_cast<unsigned>(cmd), static_cast<unsigned>(body.size()));
        return;
    }

    sendData(out.data(), out.size());
}
