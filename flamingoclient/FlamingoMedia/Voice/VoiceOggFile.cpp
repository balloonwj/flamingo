/**
 *  语音消息的 Ogg Opus 文件读写，说明见 VoiceOggFile.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "VoiceOggFile.h"

#include <windows.h>
#include <string.h>

namespace voicemsg {

namespace {

//Ogg 页校验和的生成多项式（RFC 3533 第 6 节），按最高位在前计算、初值 0、不反转
const uint32_t kOggCrcPolynomial = 0x04C11DB7u;
//一个字节的取值个数与位数
const int kByteValues = 256;
const int kBitsPerByte = 8;
//把一个字节移到 32 位校验和最高字节的位移，以及最高位
const int kCrcTopByteShift = 24;
const uint32_t kCrcTopBit = 0x80000000u;

//Ogg 页头（RFC 3533 第 6 节）：固定部分 27 字节，之后是分段表与页体
const char kOggCapture[] = "OggS";
const size_t kOggCaptureLen = 4;
const uint8_t kOggVersion = 0;
const size_t kOggHeaderFixedLen = 27;
const size_t kOggVersionOffset = 4;
const size_t kOggHeaderTypeOffset = 5;
const size_t kOggGranuleOffset = 6;
const size_t kOggSerialOffset = 14;
const size_t kOggSeqOffset = 18;
const size_t kOggCrcOffset = 22;
const size_t kOggSegmentCountOffset = 26;
//页头类型的三个标志位：本页第一个包接续上一页、逻辑流的第一页、逻辑流的最后一页
const uint8_t kOggContinued = 0x01;
const uint8_t kOggBos = 0x02;
const uint8_t kOggEos = 0x04;
//一页最多 255 个分段；一个分段最多 255 字节，长度小于 255 的分段表示一个包到此结束
const size_t kOggMaxSegments = 255;
const size_t kOggLacingMax = 255;
//没有包在本页结束时 granule 写 -1
const int64_t kOggNoGranule = -1;

//OpusHead（RFC 7845 第 5.1 节）：magic 8 字节、版本、声道数、pre-skip、原始采样率、输出增益、声道映射
const char kOpusHeadMagic[] = "OpusHead";
const char kOpusTagsMagic[] = "OpusTags";
const size_t kOpusMagicLen = 8;
const size_t kOpusHeadLen = 19;
const uint8_t kOpusHeadVersion = 1;
//版本号高 4 位是主版本，主版本不为 0 的文件不兼容
const uint8_t kOpusHeadMajorVersionMask = 0xF0;
const uint8_t kOpusMono = 1;
const uint8_t kOpusMappingFamilyMono = 0;
const size_t kOpusHeadVersionOffset = 8;
const size_t kOpusHeadChannelsOffset = 9;
const size_t kOpusHeadPreSkipOffset = 10;
const size_t kOpusHeadRateOffset = 12;
const size_t kOpusHeadMappingOffset = 18;
//OpusTags 里的编码器名称
const char kOpusVendor[] = "FlamingoIM";

//小端读写：每字节的位移与掩码，以及 16、32、64 位整数的字节数
const int kBitsPerByteShift = 8;
const uint8_t kByteMask = 0xFF;
const int kLe16Bytes = 2;
const int kLe32Bytes = 4;
const int kLe64Bytes = 8;

//临时文件的后缀：先写临时文件再改名
const wchar_t kTempSuffix[] = L".part";

//Ogg 校验和表，首次使用时生成（C++11 保证局部静态对象初始化线程安全）
struct OggCrcTable
{
    uint32_t m_values[kByteValues];   //每个字节值对应的校验和

    OggCrcTable()
    {
        for (int i = 0; i < kByteValues; ++i)
        {
            uint32_t r = static_cast<uint32_t>(i) << kCrcTopByteShift;
            for (int b = 0; b < kBitsPerByte; ++b)
                r = (r & kCrcTopBit) ? ((r << 1) ^ kOggCrcPolynomial) : (r << 1);
            m_values[i] = r;
        }
    }
};

const OggCrcTable& crcTable()
{
    static const OggCrcTable table;
    return table;
}

//按小端字节序把 bytes 个字节的整数追加到 out
void appendLe(std::vector<uint8_t>& out, uint64_t value, int bytes)
{
    for (int i = 0; i < bytes; ++i)
        out.push_back(static_cast<uint8_t>((value >> (kBitsPerByteShift * i)) & kByteMask));
}

//按小端字节序从 p 读出 bytes 个字节的整数
uint64_t readLe(const uint8_t* p, int bytes)
{
    uint64_t v = 0;
    for (int i = 0; i < bytes; ++i)
        v |= static_cast<uint64_t>(p[i]) << (kBitsPerByteShift * i);
    return v;
}

//构造 OpusHead 包
std::vector<uint8_t> buildOpusHead(int inputSampleRate, int preSkip48k)
{
    std::vector<uint8_t> head(kOpusHeadMagic, kOpusHeadMagic + kOpusMagicLen);
    head.push_back(kOpusHeadVersion);
    head.push_back(kOpusMono);
    appendLe(head, static_cast<uint64_t>(preSkip48k), kLe16Bytes);
    appendLe(head, static_cast<uint64_t>(inputSampleRate), kLe32Bytes);
    appendLe(head, 0, kLe16Bytes);   //输出增益 0 dB
    head.push_back(kOpusMappingFamilyMono);
    return head;
}

//构造 OpusTags 包：编码器名称，没有用户注释
std::vector<uint8_t> buildOpusTags()
{
    std::vector<uint8_t> tags(kOpusTagsMagic, kOpusTagsMagic + kOpusMagicLen);
    const size_t vendorLen = sizeof(kOpusVendor) - 1;
    appendLe(tags, vendorLen, kLe32Bytes);
    tags.insert(tags.end(), kOpusVendor, kOpusVendor + vendorLen);
    appendLe(tags, 0, kLe32Bytes);   //用户注释条数
    return tags;
}

//一个包占用的分段数：每 255 字节一段，最后一段不足 255（正好整除时补一个 0 长分段）
size_t segmentsOfPacket(size_t len)
{
    return len / kOggLacingMax + 1;
}

} // namespace

VoiceOggWriter::VoiceOggWriter()
    : m_serial(0)
    , m_pageSeq(0)
    , m_preSkip(0)
    , m_granule(0)
    , m_packetCount(0)
    , m_finished(false)
{
}

void VoiceOggWriter::Begin(uint32_t serial, int inputSampleRate, int preSkip48k)
{
    m_bytes.clear();
    m_pendingPackets.clear();
    m_serial = serial;
    m_pageSeq = 0;
    m_preSkip = preSkip48k;
    m_granule = 0;
    m_packetCount = 0;
    m_finished = false;
    //两个头包各占一页，granule 为 0；第一页带开始标志（RFC 7845 第 3 节）
    writeSinglePacketPage(buildOpusHead(inputSampleRate, preSkip48k), 0, kOggBos);
    writeSinglePacketPage(buildOpusTags(), 0, 0);
}

void VoiceOggWriter::AddPacket(const unsigned char* data, size_t len, int samples48k)
{
    if (m_finished || data == NULL || len == 0)
        return;
    //待写的包已到上限（包数达到 kOggPacketsPerPage，或再放这个包分段数会超过 255）：先把它们写成一页，
    //granule 为写入这个包之前的位置。到上限时不立即写出，留到下一个包或 Finish 再写，保证最后一页一定带包，
    //结束标志与裁剪后的 granule 都落在这一页上
    size_t segments = 0;
    for (size_t i = 0; i < m_pendingPackets.size(); ++i)
        segments += segmentsOfPacket(m_pendingPackets[i].size());
    if (!m_pendingPackets.empty() && (m_pendingPackets.size() >= static_cast<size_t>(kOggPacketsPerPage)
                                      || segments + segmentsOfPacket(len) > kOggMaxSegments))
        flushPage(m_granule, false);
    m_pendingPackets.push_back(std::vector<uint8_t>(data, data + len));
    m_granule += samples48k;
    ++m_packetCount;
}

void VoiceOggWriter::Finish(int64_t trimTo48k)
{
    if (m_finished)
        return;
    //granule 是解码出的总采样数（含开头要丢弃的 pre-skip）；有效采样数比它少时按有效采样数写，
    //解码端据此丢掉最后一帧里补的静音（RFC 7845 第 4.5 节）
    int64_t finalGranule = m_granule;
    if (trimTo48k > 0 && trimTo48k + m_preSkip < finalGranule)
        finalGranule = trimTo48k + m_preSkip;
    //没有任何音频包时写一个空的结束页
    flushPage(finalGranule, true);
    m_finished = true;
}

void VoiceOggWriter::flushPage(int64_t granule, bool eos)
{
    appendPage(m_pendingPackets, granule, eos ? kOggEos : 0);
    m_pendingPackets.clear();
}

void VoiceOggWriter::writeSinglePacketPage(const std::vector<uint8_t>& packet, int64_t granule, uint8_t headerType)
{
    std::vector<std::vector<uint8_t> > packets(1, packet);
    appendPage(packets, granule, headerType);
}

void VoiceOggWriter::appendPage(const std::vector<std::vector<uint8_t> >& packets, int64_t granule,
                                uint8_t headerType)
{
    std::vector<uint8_t> page(kOggCapture, kOggCapture + kOggCaptureLen);
    page.push_back(kOggVersion);
    page.push_back(headerType);
    appendLe(page, static_cast<uint64_t>(granule), kLe64Bytes);
    appendLe(page, m_serial, kLe32Bytes);
    appendLe(page, m_pageSeq, kLe32Bytes);
    appendLe(page, 0, kLe32Bytes);   //校验和，算好之后回填
    //分段表：每个包按 255 字节一段切开，最后一段小于 255 表示包到此结束
    std::vector<uint8_t> lacing;
    std::vector<uint8_t> body;
    for (size_t i = 0; i < packets.size(); ++i)
    {
        size_t rest = packets[i].size();
        while (rest >= kOggLacingMax)
        {
            lacing.push_back(static_cast<uint8_t>(kOggLacingMax));
            rest -= kOggLacingMax;
        }
        lacing.push_back(static_cast<uint8_t>(rest));
        body.insert(body.end(), packets[i].begin(), packets[i].end());
    }
    page.push_back(static_cast<uint8_t>(lacing.size()));
    page.insert(page.end(), lacing.begin(), lacing.end());
    page.insert(page.end(), body.begin(), body.end());
    const uint32_t crc = OggCrc32(&page[0], page.size());
    for (int b = 0; b < kLe32Bytes; ++b)
        page[kOggCrcOffset + b] = static_cast<uint8_t>((crc >> (kBitsPerByteShift * b)) & kByteMask);
    m_bytes.insert(m_bytes.end(), page.begin(), page.end());
    ++m_pageSeq;
}

VoiceOggReader::VoiceOggReader()
    : m_preSkip(0)
    , m_inputSampleRate(0)
    , m_finalGranule(0)
{
}

bool VoiceOggReader::Parse(const std::vector<uint8_t>& bytes)
{
    reset();
    std::vector<uint8_t> partial;   //跨分段拼接中的包
    size_t packetIndex = 0;         //已拼出的包的序号（0 为 OpusHead，1 为 OpusTags）
    uint32_t serial = 0;
    uint32_t expectSeq = 0;
    bool sawEos = false;
    int64_t lastGranule = kOggNoGranule;
    size_t pos = 0;
    while (pos < bytes.size())
    {
        //结束页之后不应再有数据；页头不完整、不是 OggS 或版本不对都按损坏处理
        if (sawEos || bytes.size() - pos < kOggHeaderFixedLen)
            break;
        const uint8_t* p = &bytes[pos];
        if (memcmp(p, kOggCapture, kOggCaptureLen) != 0 || p[kOggVersionOffset] != kOggVersion)
            break;
        const uint8_t headerType = p[kOggHeaderTypeOffset];
        const int64_t granule = static_cast<int64_t>(readLe(p + kOggGranuleOffset, kLe64Bytes));
        const uint32_t pageSerial = static_cast<uint32_t>(readLe(p + kOggSerialOffset, kLe32Bytes));
        const uint32_t seq = static_cast<uint32_t>(readLe(p + kOggSeqOffset, kLe32Bytes));
        const uint32_t storedCrc = static_cast<uint32_t>(readLe(p + kOggCrcOffset, kLe32Bytes));
        const size_t segments = p[kOggSegmentCountOffset];
        if (bytes.size() - pos < kOggHeaderFixedLen + segments)
            break;
        size_t bodyLen = 0;
        for (size_t s = 0; s < segments; ++s)
            bodyLen += p[kOggHeaderFixedLen + s];
        const size_t pageLen = kOggHeaderFixedLen + segments + bodyLen;
        if (bytes.size() - pos < pageLen)
            break;
        //校验和：把校验和字段置 0 后对整页计算
        std::vector<uint8_t> copy(p, p + pageLen);
        memset(&copy[kOggCrcOffset], 0, kLe32Bytes);
        if (OggCrc32(&copy[0], copy.size()) != storedCrc)
            break;
        //第一页带开始标志并确定序列号，之后的页序列号相同、页序号连续、不再带开始标志
        const bool first = (pos == 0);
        if (first != ((headerType & kOggBos) != 0))
            break;
        if (first)
            serial = pageSerial;
        else if (pageSerial != serial)
            break;
        if (seq != expectSeq)
            break;
        ++expectSeq;
        //本页第一个包是否接续上一页，须与上一页末尾是否留有未完的包一致
        if (((headerType & kOggContinued) != 0) != !partial.empty())
            break;

        const uint8_t* body = p + kOggHeaderFixedLen + segments;
        bool bad = false;
        for (size_t s = 0; s < segments && !bad; ++s)
        {
            const size_t segLen = p[kOggHeaderFixedLen + s];
            partial.insert(partial.end(), body, body + segLen);
            body += segLen;
            if (segLen == kOggLacingMax)
                continue;
            //一个包到此结束
            if (packetIndex == 0)
            {
                //OpusHead：长度、magic、主版本为 0、单声道、声道映射为 0
                if (partial.size() < kOpusHeadLen || memcmp(&partial[0], kOpusHeadMagic, kOpusMagicLen) != 0
                    || (partial[kOpusHeadVersionOffset] & kOpusHeadMajorVersionMask) != 0
                    || partial[kOpusHeadChannelsOffset] != kOpusMono
                    || partial[kOpusHeadMappingOffset] != kOpusMappingFamilyMono)
                {
                    bad = true;
                    break;
                }
                m_preSkip = static_cast<int>(readLe(&partial[kOpusHeadPreSkipOffset], kLe16Bytes));
                m_inputSampleRate = static_cast<uint32_t>(readLe(&partial[kOpusHeadRateOffset], kLe32Bytes));
            }
            else if (packetIndex == 1)
            {
                if (partial.size() < kOpusMagicLen || memcmp(&partial[0], kOpusTagsMagic, kOpusMagicLen) != 0)
                {
                    bad = true;
                    break;
                }
            }
            else
            {
                m_packets.push_back(partial);
            }
            ++packetIndex;
            partial.clear();
        }
        if (bad)
            break;
        if (granule != kOggNoGranule)
            lastGranule = granule;
        if ((headerType & kOggEos) != 0)
            sawEos = true;
        pos += pageLen;
    }
    //必须完整读到结束页、没有残留的半个包、两个头包都在
    if (pos != bytes.size() || !sawEos || !partial.empty() || packetIndex < 2)
    {
        reset();
        return false;
    }
    m_finalGranule = (lastGranule > 0) ? lastGranule : 0;
    return true;
}

int64_t VoiceOggReader::TotalSamples48k() const
{
    const int64_t total = m_finalGranule - m_preSkip;
    return (total > 0) ? total : 0;
}

int64_t VoiceOggReader::DurationMs() const
{
    return TotalSamples48k() * kMillisPerSecond / kOpusGranuleRate;
}

void VoiceOggReader::reset()
{
    m_packets.clear();
    m_preSkip = 0;
    m_inputSampleRate = 0;
    m_finalGranule = 0;
}

uint32_t OggCrc32(const uint8_t* data, size_t len)
{
    const OggCrcTable& table = crcTable();
    uint32_t crc = 0;
    for (size_t i = 0; i < len; ++i)
        crc = (crc << kBitsPerByte) ^ table.m_values[((crc >> kCrcTopByteShift) ^ data[i]) & kByteMask];
    return crc;
}

bool SaveBytesToFile(const std::wstring& path, const std::vector<uint8_t>& bytes)
{
    if (path.empty())
        return false;
    const std::wstring temp = path + kTempSuffix;
    HANDLE file = ::CreateFileW(temp.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    const BOOL ok = bytes.empty() ? TRUE
        : ::WriteFile(file, &bytes[0], static_cast<DWORD>(bytes.size()), &written, NULL);
    ::CloseHandle(file);
    if (!ok || written != bytes.size())
    {
        ::DeleteFileW(temp.c_str());
        return false;
    }
    if (!::MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        ::DeleteFileW(temp.c_str());
        return false;
    }
    return true;
}

bool LoadBytesFromFile(const std::wstring& path, std::vector<uint8_t>& bytes, size_t maxBytes)
{
    bytes.clear();
    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER size;
    if (!::GetFileSizeEx(file, &size) || size.QuadPart < 0 || static_cast<uint64_t>(size.QuadPart) > maxBytes)
    {
        ::CloseHandle(file);
        return false;
    }
    bytes.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    const BOOL ok = bytes.empty() ? TRUE
        : ::ReadFile(file, &bytes[0], static_cast<DWORD>(bytes.size()), &read, NULL);
    ::CloseHandle(file);
    if (!ok || read != bytes.size())
    {
        bytes.clear();
        return false;
    }
    return true;
}

} // namespace voicemsg
