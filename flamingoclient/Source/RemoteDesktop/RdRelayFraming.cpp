/**
 *  远程桌面中转帧封装 / 拆解 / 分片重组（纯逻辑）实现，RdRelayFraming.cpp
 *  balloonwj@qq.com   2026-06-09
 */
#include "RdRelayFraming.h"

#include "../net/FrameCodec.h"                    // encodeFrame / decodeFrame / frame_header / frame_decode_*
#include "../net/Protocol.h"                      // cmd_rd_relay_* / MAX_BODY_FILESERVER
#include "../net/RemoteDesktopProtocolClient.h"   // buildRelayHello

namespace remotedesktop {
namespace relayframing {

//握手帧使用的序列号：中转通道不做 seq 关联，取固定值 1 表示一个请求。
static const uint32_t kHelloSeq = 1;

bool BuildHelloFrame(const std::string& token, std::string& out)
{
    //握手包体为 {"token":"..."}；用 3 参版 encodeFrame 套 24 字节帧头
    std::string body = net::RemoteDesktopProtocolClient::buildRelayHello(token);
    return net::encodeFrame(cmd_rd_relay_hello, kHelloSeq, body, out);
}

bool BuildChunkFrame(unsigned char chunkFlag, const unsigned char* data, size_t len, std::string& out)
{
    //包体 = [1字节 chunkFlag] + 分片数据；本端不压缩（flags=0，originlen=bodylen）；
    //中转包体上限取 fileserver 级（2 MiB），与服务端 RelaySession 解帧上限一致
    std::string body;
    body.reserve(1 + len);
    body.push_back(static_cast<char>(chunkFlag));
    if (len > 0)
        body.append(reinterpret_cast<const char*>(data), len);

    return net::encodeFrame(cmd_rd_relay_data, 0, 0,
                            body.data(),
                            static_cast<uint32_t>(body.size()),
                            static_cast<uint32_t>(body.size()), out);
}

//把一个分片拼成包体：[1字节 chunkFlag] + 分片数据。
//超出中转包体上限返回 false —— 上限与服务端 RelaySession 的解帧上限一致。
static bool buildChunkBody(unsigned char chunkFlag, const unsigned char* data, size_t len,
                           std::string& out)
{
    if (len + 1 > MAX_BODY_FILESERVER)
        return false;

    out.clear();
    out.reserve(1 + len);
    out.push_back(static_cast<char>(chunkFlag));
    if (len > 0)
        out.append(reinterpret_cast<const char*>(data), len);
    return true;
}

bool BuildEngineBodies(const unsigned char* data, size_t len, size_t chunkSize,
                       std::vector<std::string>& outBodies)
{
    if (chunkSize == 0)
        return false;

    //不超过一个分片：单个 whole 包体（len==0 也产出一个空 whole 包体，语义上仍是一帧）
    if (len <= chunkSize)
    {
        std::string body;
        if (!buildChunkBody(rd_relay_chunk_whole, data, len, body))
            return false;
        outBodies.push_back(body);
        return true;
    }

    //需分片：除末片标 last 外，其余标 more
    size_t off = 0;
    while (off < len)
    {
        size_t take = len - off;
        if (take > chunkSize)
            take = chunkSize;

        bool isLast = (off + take >= len);
        unsigned char flag = isLast ? static_cast<unsigned char>(rd_relay_chunk_last)
                                    : static_cast<unsigned char>(rd_relay_chunk_more);
        std::string body;
        if (!buildChunkBody(flag, data + off, take, body))
            return false;
        outBodies.push_back(body);
        off += take;
    }
    return true;
}

bool BuildEngineFrames(const unsigned char* data, size_t len, size_t chunkSize,
                       std::vector<std::string>& outFrames)
{
    //切分规则只写在 BuildEngineBodies 一处，这里只负责给每个包体套上明文帧头。
    //注意本函数产出的是<u>明文</u>帧，发送侧接入加密后已改用 BuildEngineBodies；
    //保留它是因为单元测试用它构造可直接断言的帧。
    std::vector<std::string> bodies;
    if (!BuildEngineBodies(data, len, chunkSize, bodies))
        return false;

    for (size_t i = 0; i < bodies.size(); ++i)
    {
        std::string frame;
        if (!net::encodeFrame(cmd_rd_relay_data, 0, 0,
                              bodies[i].data(),
                              static_cast<uint32_t>(bodies[i].size()),
                              static_cast<uint32_t>(bodies[i].size()), frame))
        {
            return false;
        }
        outFrames.push_back(frame);
    }
    return true;
}

size_t DrainFrames(const unsigned char* buf, size_t len,
                   std::vector<RdRelayInboundFrame>& outFrames, bool& outError)
{
    outError = false;
    size_t consumed = 0;
    while (consumed < len)
    {
        //frame_header 是全局类型（非 net 命名空间），decodeFrame / frame_decode_* 才在 net 下
        frame_header header;
        std::string body;
        size_t one = 0;
        net::frame_decode_result r = net::decodeFrame(
            reinterpret_cast<const char*>(buf + consumed), len - consumed,
            header, body, one, MAX_BODY_FILESERVER);

        //尾部不足一个完整帧：停止，剩余字节留待补齐
        if (r == net::frame_decode_need_more)
            break;

        //协议错误（魔数 / 版本 / 长度 / CRC 非法）
        if (r == net::frame_decode_error)
        {
            outError = true;
            break;
        }

        //中转通道只承载握手与数据两种 cmd；其它一律视为异常
        if (header.cmd != cmd_rd_relay_hello && header.cmd != cmd_rd_relay_data)
        {
            outError = true;
            break;
        }

        RdRelayInboundFrame f;
        f.m_cmd = header.cmd;
        //帧头这三个字段要一并交出去：接入协议加密之后，调用方靠它们重建
        //附加认证数据才解得开包体
        f.m_seq       = header.seq;
        f.m_flags     = header.flags;
        f.m_originlen = header.originlen;
        f.m_body.assign(body.begin(), body.end());
        outFrames.push_back(f);
        consumed += one;
    }
    return consumed;
}

} // namespace relayframing

RdRelayReassembler::RdRelayReassembler()
    : m_inProgress(false)
{
}

void RdRelayReassembler::Reset()
{
    m_buf.clear();
    m_inProgress = false;
}

bool RdRelayReassembler::Feed(const unsigned char* body, size_t bodylen, std::vector<unsigned char>& outFrame)
{
    //包体至少含 1 字节分片标志
    if (body == NULL || bodylen < 1)
    {
        Reset();
        return false;
    }

    unsigned char flag = body[0];
    const unsigned char* payload = body + 1;
    size_t payloadLen = bodylen - 1;

    if (flag == rd_relay_chunk_whole)
    {
        //完整帧：若之前有未完成的重组，视为异常，丢弃残片后取当前完整帧
        if (m_inProgress)
            Reset();
        outFrame.assign(payload, payload + payloadLen);
        return true;
    }

    if (flag == rd_relay_chunk_more)
    {
        m_inProgress = true;
        m_buf.insert(m_buf.end(), payload, payload + payloadLen);
        return false;
    }

    if (flag == rd_relay_chunk_last)
    {
        //末片：拼上后交出整帧并清空（单独一个 last 不带前置 more 时即等价于 whole）
        m_buf.insert(m_buf.end(), payload, payload + payloadLen);
        outFrame.swap(m_buf);
        m_buf.clear();
        m_inProgress = false;
        return true;
    }

    //非法分片标志
    Reset();
    return false;
}

} // namespace remotedesktop
