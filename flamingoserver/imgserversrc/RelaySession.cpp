/**
 *  远程桌面中转会话实现，RelaySession.cpp
 *  balloonwj@qq.com   2026-06-08
 */
#include "RelaySession.h"
#include <time.h>
#include "../net/TcpConnection.h"
#include "../net/FrameCodec.h"
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

        //整帧原始字节（含帧头与压缩标志）：data 帧据此原样透传，故须在 retrieve 前取
        const char* frameptr = pBuffer->peek();
        bool ok = true;
        if (header.cmd == cmd_rd_relay_hello || header.cmd == cmd_av_relay_hello)
        {
            ok = onHello(conn, header.cmd, framebody);
        }
        else if (header.cmd == cmd_rd_relay_data || header.cmd == cmd_av_relay_data)
        {
            onData(conn, frameptr, consumed);
        }
        else
        {
            LOGE("relay unsupported cmd: %u, client: %s",
                 static_cast<unsigned>(header.cmd), conn->peerAddress().toIpPort().c_str());
            ok = false;
        }

        //前移读指针；失败则在前移后关连接
        pBuffer->retrieve(consumed);
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

    //登记到配对表；同角色重复 / 会话满员则失败
    if (!m_hub->join(m_sessionId, m_role, conn))
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

void RelaySession::onData(const std::shared_ptr<TcpConnection>& conn, const char* frameptr, size_t framelen)
{
    //未握手就发数据：丢弃（不关连接，等其先握手）
    if (!m_paired)
    {
        LOGE("relay data before hello, client: %s", conn->peerAddress().toIpPort().c_str());
        return;
    }

    //定位对端：对端尚未连上 / 已断开时丢弃本帧（客户端靠 chatserver 信令收尾）
    std::shared_ptr<TcpConnection> peer = m_hub->getPeer(m_sessionId, conn.get());
    if (!peer)
        return;

    //原样转发整帧（含帧头与压缩标志）：imgserver 不解压、不解析包体
    peer->send(frameptr, static_cast<int>(framelen));
}

void RelaySession::sendHelloAck(uint16_t helloCmd, int code, const std::string& msg)
{
    Json::Value root;
    root["code"] = code;
    root["msg"]  = msg;
    std::string body = writeCompact(root);

    std::string out;
    //应答 cmd 与请求一致：远程桌面回 cmd_rd_relay_hello、音视频回 cmd_av_relay_hello
    encodeFrame(helloCmd, 0, body, out);
    sendData(out.data(), out.size());
}
