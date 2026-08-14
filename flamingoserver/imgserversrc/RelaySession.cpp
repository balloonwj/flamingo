/**
 *  远程桌面中转会话实现，RelaySession.cpp
 *  balloonwj@qq.com   2026-06-08
 */
#include "RelaySession.h"
#include <time.h>
#include "../net/TcpConnection.h"
#include "../net/FrameCodec.h"
#include "../net/FrameCrypto.h"
#include "../net/FramePayload.h"
#include "../base/AsyncLog.h"
#include "../utils/RelayToken.h"
#include "../jsoncpp1.9.0/json.h"
#include "../chatserversrc/Protocol.h"   // cmd_rd_relay_* / error_code_*

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
                           const std::string& tokenkey, RelayHub* hub)
    : TcpSession(conn),
      m_tokenKey(tokenkey),
      m_hub(hub),
      m_paired(false),
      m_role(0)
{
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
        if (header.cmd == cmd_rd_relay_hello || header.cmd == cmd_av_relay_hello)
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
        else if (header.cmd == cmd_rd_relay_data || header.cmd == cmd_av_relay_data)
        {
            ok = onData(conn, header.cmd, header.seq, header.flags, header.originlen, framebody);
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
    //重复握手视为非法
    if (m_paired)
    {
        LOGE("relay duplicate hello, client: %s", conn->peerAddress().toIpPort().c_str());
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

    m_sessionId = payload.sessionid;
    m_role      = payload.role;

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
    //加密与投递必须整体串行，理由见 m_sendMutex 的注释
    std::lock_guard<std::mutex> guard(m_sendMutex);

    //只加密、不压缩：压缩是两端客户端之间的语义，服务端既不解压也不重压，
    //原帧的压缩标志与 originlen 都原样透传，对端客户端据此自行解压
    std::string wire;
    uint8_t     finalFlags = 0;
    if (!net::encryptFramePayload(cmd, seq, srcFlags, originlen, plainBody,
                                  &m_handshake.cipher(), wire, finalFlags))
    {
        LOGE("relay forward: encrypt payload failed, cmd: %u, bodylen: %u",
             static_cast<unsigned>(cmd), static_cast<unsigned>(plainBody.size()));
        return;
    }

    std::string out;
    if (!encodeFrame(cmd, seq, finalFlags, wire.data(),
                     static_cast<uint32_t>(wire.size()), originlen, out))
    {
        LOGE("relay forward: encodeFrame failed, cmd: %u, bodylen: %u",
             static_cast<unsigned>(cmd), static_cast<unsigned>(wire.size()));
        return;
    }

    sendData(out.data(), out.size());
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
    //加密与投递必须整体串行，理由见 m_sendMutex 的注释
    std::lock_guard<std::mutex> guard(m_sendMutex);

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
