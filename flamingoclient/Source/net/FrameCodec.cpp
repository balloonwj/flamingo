/**
 *  v2 协议帧编解码实现，FrameCodec.cpp
 *
 *  由服务端 flamingoserver/net/FrameCodec.cpp 移植，与服务端保持一致。
 *
 *  balloonwj@qq.com   2026-05-22
 */
#include "FrameCodec.h"

namespace net {

//帧头各字段在 24 字节帧头中的字节偏移
constexpr size_t FOFF_MAGIC     = 0;    //魔数，2 字节
constexpr size_t FOFF_VERSION   = 2;    //协议版本，1 字节
constexpr size_t FOFF_FLAGS     = 3;    //标志位，1 字节
constexpr size_t FOFF_CMD       = 4;    //消息类型，2 字节
constexpr size_t FOFF_RESERVED  = 6;    //保留字段，2 字节
constexpr size_t FOFF_SEQ       = 8;    //序列号，4 字节
constexpr size_t FOFF_BODYLEN   = 12;   //线上包体长度，4 字节
constexpr size_t FOFF_ORIGINLEN = 16;   //原始包体长度，4 字节
constexpr size_t FOFF_CRC       = 20;   //帧头 CRC-32，4 字节

//CRC-32 覆盖的帧头字节数：自帧头起始到 crc 字段之前（即前 20 字节）
constexpr size_t FRAME_CRC_COVER_LEN = 20;

//CRC-32（IEEE）反转多项式
constexpr uint32_t CRC32_POLY = 0xEDB88320u;
//CRC-32 计算初值
constexpr uint32_t CRC32_INIT = 0xFFFFFFFFu;

uint32_t crc32(const void* data, size_t len)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t crc = CRC32_INIT;
    for (size_t i = 0; i < len; ++i)
    {
        crc ^= p[i];
        //逐位处理一个字节的 8 个比特
        for (int bit = 0; bit < 8; ++bit)
        {
            if (crc & 1u)
                crc = (crc >> 1) ^ CRC32_POLY;
            else
                crc = crc >> 1;
        }
    }
    return ~crc;
}

//把 16 位无符号整型按大端序写入 p 指向的 2 字节
static void putU16(char* p, uint16_t v)
{
    p[0] = static_cast<char>((v >> 8) & 0xFF);
    p[1] = static_cast<char>(v & 0xFF);
}

//把 32 位无符号整型按大端序写入 p 指向的 4 字节
static void putU32(char* p, uint32_t v)
{
    p[0] = static_cast<char>((v >> 24) & 0xFF);
    p[1] = static_cast<char>((v >> 16) & 0xFF);
    p[2] = static_cast<char>((v >> 8) & 0xFF);
    p[3] = static_cast<char>(v & 0xFF);
}

//从 p 指向的 2 字节按大端序读出 16 位无符号整型
static uint16_t getU16(const char* p)
{
    return static_cast<uint16_t>((static_cast<uint8_t>(p[0]) << 8)
                               |  static_cast<uint8_t>(p[1]));
}

//从 p 指向的 4 字节按大端序读出 32 位无符号整型
static uint32_t getU32(const char* p)
{
    return (static_cast<uint32_t>(static_cast<uint8_t>(p[0])) << 24)
         | (static_cast<uint32_t>(static_cast<uint8_t>(p[1])) << 16)
         | (static_cast<uint32_t>(static_cast<uint8_t>(p[2])) << 8)
         |  static_cast<uint32_t>(static_cast<uint8_t>(p[3]));
}

bool encodeFrame(uint16_t cmd, uint32_t seq, uint8_t flags,
                 const char* body, uint32_t bodylen, uint32_t originlen,
                 std::string& out)
{
    //编码侧用最宽松上限（fileserver 上限）做兜底，防止异常超大包
    if (bodylen > MAX_BODY_FILESERVER)
        return false;

    char hdr[FRAME_HEADER_SIZE];
    putU16(hdr + FOFF_MAGIC, FRAME_MAGIC);
    hdr[FOFF_VERSION] = static_cast<char>(PROTOCOL_VERSION_V2);
    hdr[FOFF_FLAGS]   = static_cast<char>(flags);
    putU16(hdr + FOFF_CMD, cmd);
    putU16(hdr + FOFF_RESERVED, 0);
    putU32(hdr + FOFF_SEQ, seq);
    putU32(hdr + FOFF_BODYLEN, bodylen);
    putU32(hdr + FOFF_ORIGINLEN, originlen);
    //对帧头前 20 字节求 CRC-32，写入末 4 字节
    putU32(hdr + FOFF_CRC, crc32(hdr, FRAME_CRC_COVER_LEN));

    out.append(hdr, FRAME_HEADER_SIZE);
    if (body != nullptr && bodylen > 0)
        out.append(body, bodylen);
    return true;
}

bool encodeFrame(uint16_t cmd, uint32_t seq, const std::string& body, std::string& out)
{
    uint32_t len = static_cast<uint32_t>(body.size());
    return encodeFrame(cmd, seq, 0, body.data(), len, len, out);
}

bool encodeFrame(uint16_t cmd, uint32_t seq, std::string& out)
{
    return encodeFrame(cmd, seq, 0, nullptr, 0, 0, out);
}

frame_decode_result decodeFrame(const char* buffer, size_t buflen,
                                frame_header& header, std::string& body,
                                size_t& consumed, uint32_t maxbody)
{
    //不足一个帧头，等待更多数据
    if (buffer == nullptr || buflen < FRAME_HEADER_SIZE)
        return frame_decode_need_more;

    header.magic     = getU16(buffer + FOFF_MAGIC);
    header.version   = static_cast<uint8_t>(buffer[FOFF_VERSION]);
    header.flags     = static_cast<uint8_t>(buffer[FOFF_FLAGS]);
    header.cmd       = getU16(buffer + FOFF_CMD);
    header.reserved  = getU16(buffer + FOFF_RESERVED);
    header.seq       = getU32(buffer + FOFF_SEQ);
    header.bodylen   = getU32(buffer + FOFF_BODYLEN);
    header.originlen = getU32(buffer + FOFF_ORIGINLEN);
    header.headercrc = getU32(buffer + FOFF_CRC);

    //魔数不符：判定为脏数据/协议错乱
    if (header.magic != FRAME_MAGIC)
        return frame_decode_error;
    //协议版本不符：拒绝
    if (header.version != PROTOCOL_VERSION_V2)
        return frame_decode_error;
    //包体长度越界：防御恶意大包
    if (header.bodylen > maxbody || header.originlen > maxbody)
        return frame_decode_error;
    //headercrc 为 0 表示发送方未提供校验，跳过；非 0 则必须校验通过
    if (header.headercrc != 0
        && crc32(buffer, FRAME_CRC_COVER_LEN) != header.headercrc)
        return frame_decode_error;

    //帧头合法但包体尚未收全，等待更多数据
    if (buflen < FRAME_HEADER_SIZE + header.bodylen)
        return frame_decode_need_more;

    body.assign(buffer + FRAME_HEADER_SIZE, header.bodylen);
    consumed = FRAME_HEADER_SIZE + header.bodylen;
    return frame_decode_ok;
}

//附件包体中描述符长度字段（desclen）所占字节数
constexpr size_t ATTACH_DESCLEN_SIZE = 4;

bool encodeAttachmentBody(const std::string& descriptor,
                          const char* binary, uint32_t binlen,
                          std::string& out)
{
    uint32_t desclen = static_cast<uint32_t>(descriptor.size());
    //4 字节长度域 + 描述符 + 附件 三者之和不得超过 fileserver 包体上限
    uint64_t total = static_cast<uint64_t>(ATTACH_DESCLEN_SIZE) + desclen + binlen;
    if (total > MAX_BODY_FILESERVER)
        return false;

    //写入 4 字节大端 desclen
    char lenbuf[ATTACH_DESCLEN_SIZE];
    putU32(lenbuf, desclen);
    out.append(lenbuf, ATTACH_DESCLEN_SIZE);
    //写入描述符
    out.append(descriptor);
    //写入附件二进制
    if (binary != nullptr && binlen > 0)
        out.append(binary, binlen);
    return true;
}

bool decodeAttachmentBody(const char* body, size_t bodylen,
                          std::string& descriptor,
                          size_t& binaryoff, size_t& binarylen)
{
    //至少要容纳 4 字节 desclen 字段
    if (body == nullptr || bodylen < ATTACH_DESCLEN_SIZE)
        return false;

    uint32_t desclen = getU32(body);
    //描述符不得越出包体范围
    if (static_cast<uint64_t>(ATTACH_DESCLEN_SIZE) + desclen > bodylen)
        return false;

    descriptor.assign(body + ATTACH_DESCLEN_SIZE, desclen);
    binaryoff = ATTACH_DESCLEN_SIZE + desclen;
    binarylen = bodylen - binaryoff;
    return true;
}

} // namespace net
