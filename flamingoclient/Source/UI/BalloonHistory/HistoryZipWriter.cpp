/**
 *  导出聊天记录时打包 zip 的写入器实现，说明见 HistoryZipWriter.h。
 *
 *  zip 的几种记录（小端序）：
 *    本地文件头   签名 0x04034b50，30 字节定长部分 + 名字
 *    中央目录项   签名 0x02014b50，46 字节定长部分 + 名字
 *    目录结束记录 签名 0x06054b50，22 字节
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "HistoryZipWriter.h"

#include <windows.h>
#include <time.h>
#include "../../third-party/zlib1.2.11/zlib.h"

namespace balloonhistory {

namespace {

//几种记录的签名
const uint32_t kLocalHeaderSig = 0x04034b50;
const uint32_t kCentralSig     = 0x02014b50;
const uint32_t kEndSig         = 0x06054b50;
//解压所需的版本：2.0（deflate）
const uint16_t kVersionNeeded  = 20;
//「由谁创建」：高字节 0 = MS-DOS 兼容（属性按 DOS 解释），低字节同版本
const uint16_t kVersionMadeBy  = 20;
//通用位标记 bit 11：条目名与注释按 UTF-8 编码
const uint16_t kFlagUtf8       = 0x0800;
//压缩方法：deflate
const uint16_t kMethodDeflate  = 8;
//本地文件头里 CRC-32 字段相对文件头起点的偏移（其后依次是压缩后、原始长度）
const long kLocalCrcOffset     = 14;
//不写 ZIP64 时的上限：偏移与长度都是 32 位，条目数是 16 位
const uint64_t kMax32 = 0xFFFFFFFFULL;
const size_t kMaxEntries = 0xFFFF;
//读源文件、喂给 deflate 的块大小（字节）
const size_t kChunkBytes = 256 * 1024;
//deflate 参数：默认压缩级别、不带 zlib 头的原始 deflate（窗口位数取负）、默认内存级别
const int kDeflateLevel = Z_DEFAULT_COMPRESSION;
const int kRawWindowBits = -15;
const int kMemLevel = 8;
//DOS 日期时间的各段
const int kDosYearBase = 1980;
const int kTmYearBase = 1900;
const int kDosYearShift = 9;
const int kDosMonthShift = 5;
const int kDosHourShift = 11;
const int kDosMinuteShift = 5;
const int kDosSecondDiv = 2;

//一个字节的位数与掩码（小端序拆字节用）
const int kBitsPerByte = 8;
const uint32_t kByteMask = 0xFF;
const int kBytesU16 = 2;
const int kBytesU32 = 4;

//小端序写 16 / 32 位整数到缓冲区
void PutU16(std::string& buf, uint16_t v)
{
    for (int i = 0; i < kBytesU16; ++i)
    {
        buf.push_back((char)((v >> (i * kBitsPerByte)) & kByteMask));
    }
}

void PutU32(std::string& buf, uint32_t v)
{
    for (int i = 0; i < kBytesU32; ++i)
    {
        buf.push_back((char)((v >> (i * kBitsPerByte)) & kByteMask));
    }
}

//当前本地时间换成 DOS 格式的日期与时间
void CurrentDosTime(uint16_t& dosTime, uint16_t& dosDate)
{
    const time_t now = ::time(NULL);
    struct tm t;
    if (::localtime_s(&t, &now) != 0 || t.tm_year + kTmYearBase < kDosYearBase)
    {
        dosTime = 0;
        dosDate = (uint16_t)((1 << kDosMonthShift) | 1);   //1980-01-01
        return;
    }
    dosDate = (uint16_t)(((t.tm_year + kTmYearBase - kDosYearBase) << kDosYearShift)
                         | ((t.tm_mon + 1) << kDosMonthShift) | t.tm_mday);
    dosTime = (uint16_t)((t.tm_hour << kDosHourShift) | (t.tm_min << kDosMinuteShift) | (t.tm_sec / kDosSecondDiv));
}

} // namespace

CHistoryZipWriter::CHistoryZipWriter()
    : m_file(NULL)
    , m_written(0)
    , m_failed(false)
    , m_tooLarge(false)
{
}

CHistoryZipWriter::~CHistoryZipWriter()
{
    if (m_file != NULL)
    {
        ::fclose(m_file);
        m_file = NULL;
    }
}

bool CHistoryZipWriter::Open(const std::wstring& zipPath)
{
    if (m_file != NULL || zipPath.empty())
    {
        return false;
    }
    if (::_wfopen_s(&m_file, zipPath.c_str(), L"wb") != 0 || m_file == NULL)
    {
        m_file = NULL;
        m_failed = true;
        return false;
    }
    m_entries.clear();
    m_written = 0;
    m_failed = false;
    m_tooLarge = false;
    return true;
}

bool CHistoryZipWriter::AddData(const std::string& nameUtf8, const std::string& data)
{
    return WriteEntry(nameUtf8, NULL, &data);
}

bool CHistoryZipWriter::AddFile(const std::string& nameUtf8, const std::wstring& sourcePath)
{
    if (m_failed || m_file == NULL)
    {
        return false;
    }
    FILE* src = NULL;
    if (::_wfopen_s(&src, sourcePath.c_str(), L"rb") != 0 || src == NULL)
    {
        //源文件打不开不算整个 zip 失败：调用方可以跳过这一个继续写别的
        return false;
    }
    const bool ok = WriteEntry(nameUtf8, src, NULL);
    ::fclose(src);
    return ok;
}

bool CHistoryZipWriter::CheckLimit(uint64_t value)
{
    if (value > kMax32)
    {
        m_failed = true;
        m_tooLarge = true;
        return false;
    }
    return true;
}

bool CHistoryZipWriter::WriteRaw(const void* data, size_t len)
{
    if (m_failed || m_file == NULL)
    {
        return false;
    }
    if (len > 0 && ::fwrite(data, 1, len, m_file) != len)
    {
        m_failed = true;
        return false;
    }
    m_written += len;
    return true;
}

bool CHistoryZipWriter::WriteEntry(const std::string& nameUtf8, FILE* source, const std::string* memory)
{
    if (m_failed || m_file == NULL || nameUtf8.empty())
    {
        return false;
    }
    if (m_entries.size() >= kMaxEntries)
    {
        m_failed = true;
        m_tooLarge = true;
        return false;
    }
    if (!CheckLimit(m_written))
    {
        return false;
    }
    Entry e;
    e.m_name = nameUtf8;
    e.m_crc = 0;
    e.m_compressed = 0;
    e.m_uncompressed = 0;
    e.m_offset = (uint32_t)m_written;
    CurrentDosTime(e.m_dosTime, e.m_dosDate);

    //1. 本地文件头：CRC 与长短先写 0，数据写完再回填
    std::string header;
    PutU32(header, kLocalHeaderSig);
    PutU16(header, kVersionNeeded);
    PutU16(header, kFlagUtf8);
    PutU16(header, kMethodDeflate);
    PutU16(header, e.m_dosTime);
    PutU16(header, e.m_dosDate);
    PutU32(header, 0);
    PutU32(header, 0);
    PutU32(header, 0);
    PutU16(header, (uint16_t)nameUtf8.size());
    PutU16(header, 0);
    header += nameUtf8;
    if (!WriteRaw(header.data(), header.size()))
    {
        return false;
    }

    //2. 数据：原始 deflate，边读边压缩
    z_stream zs;
    ::memset(&zs, 0, sizeof(zs));
    if (::deflateInit2(&zs, kDeflateLevel, Z_DEFLATED, kRawWindowBits, kMemLevel, Z_DEFAULT_STRATEGY) != Z_OK)
    {
        m_failed = true;
        return false;
    }
    std::vector<unsigned char> inBuf(kChunkBytes);
    std::vector<unsigned char> outBuf(kChunkBytes);
    uLong crc = ::crc32(0L, Z_NULL, 0);
    uint64_t rawTotal = 0;
    uint64_t packedTotal = 0;
    size_t memoryPos = 0;
    bool ok = true;
    bool finished = false;
    while (ok && !finished)
    {
        //取下一块原始数据
        size_t got = 0;
        if (memory != NULL)
        {
            got = (memory->size() - memoryPos < kChunkBytes) ? (memory->size() - memoryPos) : kChunkBytes;
            if (got > 0)
            {
                ::memcpy(&inBuf[0], memory->data() + memoryPos, got);
            }
            memoryPos += got;
        }
        else
        {
            got = ::fread(&inBuf[0], 1, kChunkBytes, source);
            if (::ferror(source))
            {
                ok = false;
                break;
            }
        }
        const bool last = (memory != NULL) ? (memoryPos >= memory->size()) : (::feof(source) != 0);
        crc = ::crc32(crc, &inBuf[0], (uInt)got);
        rawTotal += got;
        zs.next_in = &inBuf[0];
        zs.avail_in = (uInt)got;
        const int flush = last ? Z_FINISH : Z_NO_FLUSH;
        //把这一块全部压完（输出缓冲区满了就先写出去）
        do
        {
            zs.next_out = &outBuf[0];
            zs.avail_out = (uInt)outBuf.size();
            const int rc = ::deflate(&zs, flush);
            if (rc == Z_STREAM_ERROR)
            {
                ok = false;
                break;
            }
            const size_t produced = outBuf.size() - zs.avail_out;
            if (!WriteRaw(&outBuf[0], produced))
            {
                ok = false;
                break;
            }
            packedTotal += produced;
            if (rc == Z_STREAM_END)
            {
                finished = true;
            }
        } while (zs.avail_out == 0);
        if (!CheckLimit(rawTotal) || !CheckLimit(packedTotal) || !CheckLimit(m_written))
        {
            ok = false;
        }
    }
    ::deflateEnd(&zs);
    if (!ok)
    {
        m_failed = true;
        return false;
    }
    e.m_crc = (uint32_t)crc;
    e.m_compressed = (uint32_t)packedTotal;
    e.m_uncompressed = (uint32_t)rawTotal;

    //3. 回到本地文件头补 CRC 与长短，再回到末尾
    std::string sizes;
    PutU32(sizes, e.m_crc);
    PutU32(sizes, e.m_compressed);
    PutU32(sizes, e.m_uncompressed);
    if (::_fseeki64(m_file, (__int64)e.m_offset + kLocalCrcOffset, SEEK_SET) != 0
        || ::fwrite(sizes.data(), 1, sizes.size(), m_file) != sizes.size()
        || ::_fseeki64(m_file, 0, SEEK_END) != 0)
    {
        m_failed = true;
        return false;
    }
    m_entries.push_back(e);
    return true;
}

bool CHistoryZipWriter::Close()
{
    if (m_file == NULL)
    {
        return false;
    }
    bool ok = !m_failed;
    if (ok)
    {
        const uint64_t centralStart = m_written;
        for (size_t i = 0; ok && i < m_entries.size(); ++i)
        {
            const Entry& e = m_entries[i];
            std::string rec;
            PutU32(rec, kCentralSig);
            PutU16(rec, kVersionMadeBy);
            PutU16(rec, kVersionNeeded);
            PutU16(rec, kFlagUtf8);
            PutU16(rec, kMethodDeflate);
            PutU16(rec, e.m_dosTime);
            PutU16(rec, e.m_dosDate);
            PutU32(rec, e.m_crc);
            PutU32(rec, e.m_compressed);
            PutU32(rec, e.m_uncompressed);
            PutU16(rec, (uint16_t)e.m_name.size());
            PutU16(rec, 0);   //扩展字段长度
            PutU16(rec, 0);   //注释长度
            PutU16(rec, 0);   //起始磁盘号
            PutU16(rec, 0);   //内部属性
            PutU32(rec, 0);   //外部属性
            PutU32(rec, e.m_offset);
            rec += e.m_name;
            ok = WriteRaw(rec.data(), rec.size());
        }
        const uint64_t centralSize = m_written - centralStart;
        ok = ok && CheckLimit(centralStart) && CheckLimit(centralSize) && CheckLimit(m_written);
        if (ok)
        {
            std::string end;
            PutU32(end, kEndSig);
            PutU16(end, 0);   //本磁盘号
            PutU16(end, 0);   //中央目录起始磁盘号
            PutU16(end, (uint16_t)m_entries.size());
            PutU16(end, (uint16_t)m_entries.size());
            PutU32(end, (uint32_t)centralSize);
            PutU32(end, (uint32_t)centralStart);
            PutU16(end, 0);   //注释长度
            ok = WriteRaw(end.data(), end.size());
        }
    }
    if (::fclose(m_file) != 0)
    {
        ok = false;
    }
    m_file = NULL;
    if (!ok)
    {
        m_failed = true;
    }
    return ok;
}

bool CHistoryZipWriter::Failed() const
{
    return m_failed;
}

bool CHistoryZipWriter::TooLarge() const
{
    return m_tooLarge;
}

int CHistoryZipWriter::EntryCount() const
{
    return (int)m_entries.size();
}

uint64_t CHistoryZipWriter::BytesWritten() const
{
    return m_written;
}

} // namespace balloonhistory
