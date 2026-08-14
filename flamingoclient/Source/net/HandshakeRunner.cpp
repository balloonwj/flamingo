/**
 *  客户端侧协议加密握手执行器的实现。设计说明见 HandshakeRunner.h。
 *  balloonwj@qq.com   2026-08-13
 */
#include "HandshakeRunner.h"

#include "FrameCodec.h"   //encodeFrame / decodeFrame / frame_header（内含 net/Protocol.h）

namespace net {

namespace {

//握手三帧统一使用的序列号。服务端会把它原样回填到应答里，本身不参与任何判定，
//取 1 只是为了让抓包时一眼认得出这是握手帧。
const uint32_t HANDSHAKE_FRAME_SEQ = 1;

/**
 *  编码并发出一个握手帧（明文）。
 *  @param channel 收发适配器
 *  @param body    握手包体 JSON
 *  @param what    本帧的名字，仅用于拼错误信息，如 "ClientHello"
 *  @param errOut  [出参] 失败原因
 */
bool sendHandshakeFrame(IHandshakeChannel& channel, const std::string& body,
                        const char* what, std::string& errOut)
{
    //每帧都用一个<u>新的</u>局部缓冲：net::encodeFrame 是追加语义（为的是支持把多帧
    //攒在一起批量发出），复用同一个缓冲会把后一帧拼在前一帧后面一起发出去。
    //这个坑真踩过：第 3 帧被拼在 ClientHello 之后，服务端连着收到两个 ClientHello，
    //报的却是"字段 chdr 缺失"，与真正的原因隔得很远。
    std::string frame;
    if (!encodeFrame(cmd_handshake, HANDSHAKE_FRAME_SEQ, body, frame))
    {
        errOut = std::string("cannot encode ") + what + " frame";
        return false;
    }

    if (!channel.SendAll(frame.data(), frame.size()))
    {
        errOut = std::string("cannot send ") + what;
        return false;
    }

    return true;
}

/**
 *  收取一个握手帧并交出它的包体。
 *  @param channel 收发适配器
 *  @param bodyOut [出参] 握手包体
 *  @param errOut  [出参] 失败原因
 */
bool recvHandshakeFrame(IHandshakeChannel& channel, std::string& bodyOut, std::string& errOut)
{
    bodyOut.clear();

    char headerBuf[FRAME_HEADER_SIZE] = { 0 };
    if (!channel.RecvAll(headerBuf, FRAME_HEADER_SIZE))
    {
        errOut = "no handshake response from server (timed out or connection closed)";
        return false;
    }

    //只凭帧头解析：包体还没收，合法时 decodeFrame 返回 need_more，
    //此时 header 里的字段已经填好并校验过魔数 / 版本 / CRC
    frame_header header;
    std::string ignored;
    size_t consumed = 0;
    const frame_decode_result r =
        decodeFrame(headerBuf, FRAME_HEADER_SIZE, header, ignored, consumed);
    if (r == frame_decode_error)
    {
        errOut = "server sent an illegal frame header during handshake";
        return false;
    }

    if (header.cmd != cmd_handshake)
    {
        errOut = "unexpected cmd during handshake, expected cmd_handshake";
        return false;
    }

    //握手帧必须是明文。收到带加密位的握手帧说明对端实现不对，或者有人在改包——
    //此刻双方都还没有密钥，本端根本无从解起。
    if ((header.flags & frame_flag_encrypted) != 0)
    {
        errOut = "handshake frame must not be encrypted";
        return false;
    }

    //空包体帧：帧头本身已是完整一帧
    if (r == frame_decode_ok || header.bodylen == 0)
    {
        return true;
    }

    if (header.bodylen > MAX_BODY_CHATSERVER)
    {
        errOut = "handshake frame body is too large";
        return false;
    }

    bodyOut.resize(header.bodylen);
    if (!channel.RecvAll(&bodyOut[0], header.bodylen))
    {
        errOut = "incomplete handshake frame body";
        bodyOut.clear();
        return false;
    }

    return true;
}

} // anonymous namespace

bool performClientHandshake(IHandshakeChannel& channel, const unsigned char* serverPubKey,
                            ClientHandshake& hs, std::string& errOut)
{
    errOut.clear();

    if (serverPubKey == NULL)
    {
        errOut = "server public key is not configured";
        return false;
    }

    //每条新连接都要重新协商一套会话密钥，先把可能残留的旧状态清掉
    hs.reset();

    //第 1 帧：本端一次性公钥与随机数
    const std::string helloBody = hs.buildClientHello();
    if (helloBody.empty())
    {
        errOut = "cannot build ClientHello";
        return false;
    }
    if (!sendHandshakeFrame(channel, helloBody, "ClientHello", errOut))
    {
        return false;
    }

    //第 2 帧：服务端的一次性公钥、随机数、签名与它的发送流流头
    std::string serverHelloBody;
    if (!recvHandshakeFrame(channel, serverHelloBody, errOut))
    {
        return false;
    }
    if (!hs.onServerHello(serverHelloBody, serverPubKey, errOut))
    {
        //验签不过意味着对端不是我们认识的那台服务器，可能有人在中间。这里<u>只能失败</u>，
        //绝不允许提示使用者"是否继续"——那等于把安全判断交给看不懂它的人。
        return false;
    }

    //第 3 帧：本端发送流的流头。服务端要靠它才能初始化接收流，所以这一帧仍是明文；
    //发出之后本端即视为握手完成，后续帧才开始加密。本帧没有应答。
    const std::string streamHeaderBody = hs.buildClientStreamHeader();
    if (streamHeaderBody.empty())
    {
        errOut = "cannot build client stream header";
        return false;
    }
    if (!sendHandshakeFrame(channel, streamHeaderBody, "client stream header", errOut))
    {
        return false;
    }

    return true;
}

} // namespace net
