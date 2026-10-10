/**
 *  云端录像原始帧文件（.frec）的读取，格式见 CloudRecordReader.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "CloudRecordReader.h"

#include <string.h>

namespace screenrecord {

namespace {

//一个字节的位数
const int kBitsPerByte = 8;
//记录头里相对毫秒与负载长度两个字段的偏移
const size_t kOffRecordRelMs = 1;
const size_t kOffRecordLen = 5;

//读 bytes 个字节的小端无符号整数（调用方已保证字节够）
uint64_t readLittle(const std::string& d, size_t p, size_t bytes)
{
    uint64_t v = 0;
    for (size_t i = bytes; i > 0; --i)
        v = (v << kBitsPerByte) | static_cast<unsigned char>(d[p + i - 1]);
    return v;
}

uint16_t readU16(const std::string& d, size_t p)
{
    return static_cast<uint16_t>(readLittle(d, p, sizeof(uint16_t)));
}

uint32_t readU32(const std::string& d, size_t p)
{
    return static_cast<uint32_t>(readLittle(d, p, sizeof(uint32_t)));
}

uint64_t readU64(const std::string& d, size_t p)
{
    return readLittle(d, p, sizeof(uint64_t));
}

//文件头里固定部分的字节数：标识 4 + 版本 2 + 标志 2 + 录像 id 8 + 开始时刻 8 + 房间号长度 2
const size_t kFixedHeaderBytes = 26;
//固定部分里各字段的偏移
const size_t kOffVersion = 4;
const size_t kOffFlags = 6;
const size_t kOffRecordId = 8;
const size_t kOffStartTime = 16;
const size_t kOffRoomLen = 24;

} // namespace

FrecFileHeader::FrecFileHeader()
    : m_version(0)
    , m_flags(0)
    , m_recordId(0)
    , m_startTimeMs(0)
{
}

FrecRecord::FrecRecord()
    : m_type(0)
    , m_relMs(0)
{
}

bool DecodeFrecHeader(const std::string& data, size_t& pos, FrecFileHeader& out)
{
    if (pos > data.size() || data.size() - pos < kFixedHeaderBytes)
        return false;
    if (memcmp(data.data() + pos, kFrecMagic, kFrecMagicBytes) != 0)
        return false;
    const uint16_t version = readU16(data, pos + kOffVersion);
    if (version != kFrecVersion)
        return false;
    const uint16_t roomLen = readU16(data, pos + kOffRoomLen);
    if (data.size() - pos - kFixedHeaderBytes < roomLen)
        return false;
    out.m_version = version;
    out.m_flags = readU16(data, pos + kOffFlags);
    out.m_recordId = readU64(data, pos + kOffRecordId);
    out.m_startTimeMs = static_cast<int64_t>(readU64(data, pos + kOffStartTime));
    out.m_roomId.assign(data, pos + kFixedHeaderBytes, roomLen);
    pos += kFixedHeaderBytes + roomLen;
    return true;
}

bool DecodeFrecRecord(const std::string& data, size_t& pos, FrecRecord& out)
{
    if (pos > data.size() || data.size() - pos < kFrecRecordHeadBytes)
        return false;
    const uint32_t len = readU32(data, pos + kOffRecordLen);
    if (len > kFrecMaxPayloadBytes || data.size() - pos - kFrecRecordHeadBytes < len)
        return false;
    out.m_type = static_cast<unsigned char>(data[pos]);
    out.m_relMs = readU32(data, pos + kOffRecordRelMs);
    out.m_payload.assign(data, pos + kFrecRecordHeadBytes, len);
    pos += kFrecRecordHeadBytes + len;
    return true;
}

bool DecodeFrecSeat(const std::string& payload, int& seat, int64_t& userId)
{
    if (payload.size() != kFrecSeatPayloadBytes)
        return false;
    //座位号 1 字节，其后为 userid
    seat = static_cast<unsigned char>(payload[0]);
    userId = static_cast<int64_t>(readU64(payload, sizeof(uint8_t)));
    return true;
}

CFrecReader::CFrecReader()
    : m_file(NULL)
    , m_pos(0)
    , m_consumed(0)
    , m_fileBytes(0)
    , m_eof(false)
    , m_truncated(false)
{
}

CFrecReader::~CFrecReader()
{
    Close();
}

void CFrecReader::Close()
{
    if (m_file != NULL)
    {
        fclose(m_file);
        m_file = NULL;
    }
    m_buf.clear();
    m_pos = 0;
}

bool CFrecReader::fill()
{
    if (m_file == NULL || m_eof)
        return false;
    //把已交出的部分丢掉，缓冲只留还没解出的字节
    if (m_pos > 0)
    {
        m_buf.erase(0, m_pos);
        m_pos = 0;
    }
    std::string chunk(kReadChunkBytes, '\0');
    const size_t n = fread(&chunk[0], 1, chunk.size(), m_file);
    if (n == 0)
    {
        m_eof = true;
        return false;
    }
    m_buf.append(chunk, 0, n);
    return true;
}

bool CFrecReader::Open(const std::wstring& path)
{
    Close();
    m_header = FrecFileHeader();
    m_consumed = 0;
    m_fileBytes = 0;
    m_eof = false;
    m_truncated = false;
    if (_wfopen_s(&m_file, path.c_str(), L"rb") != 0 || m_file == NULL)
    {
        m_file = NULL;
        return false;
    }
    if (_fseeki64(m_file, 0, SEEK_END) == 0)
    {
        const int64_t size = _ftelli64(m_file);
        m_fileBytes = size > 0 ? static_cast<uint64_t>(size) : 0;
    }
    _fseeki64(m_file, 0, SEEK_SET);
    //文件头不超过固定部分加 64 KiB 的房间号，第一块必然读得下
    fill();
    size_t pos = 0;
    if (!DecodeFrecHeader(m_buf, pos, m_header))
    {
        Close();
        return false;
    }
    m_pos = pos;
    m_consumed = pos;
    return true;
}

const FrecFileHeader& CFrecReader::Header() const
{
    return m_header;
}

bool CFrecReader::Next(FrecRecord& out)
{
    if (m_file == NULL)
        return false;
    for (;;)
    {
        size_t pos = m_pos;
        if (DecodeFrecRecord(m_buf, pos, out))
        {
            m_consumed += pos - m_pos;
            m_pos = pos;
            return true;
        }
        //负载长度超限：文件损坏，到此为止
        if (m_buf.size() - m_pos >= kFrecRecordHeadBytes && readU32(m_buf, m_pos + kOffRecordLen) > kFrecMaxPayloadBytes)
        {
            m_truncated = true;
            return false;
        }
        if (!fill())
        {
            m_truncated = (m_buf.size() > m_pos);
            return false;
        }
    }
}

bool CFrecReader::IsTruncated() const
{
    return m_truncated;
}

uint64_t CFrecReader::BytesConsumed() const
{
    return m_consumed;
}

uint64_t CFrecReader::FileBytes() const
{
    return m_fileBytes;
}

} // namespace screenrecord
