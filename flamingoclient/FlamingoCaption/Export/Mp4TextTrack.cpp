/**
 *  给 MP4 文件追加 tx3g 字幕轨，见 Mp4TextTrack.h。
 *  盒子结构参照 ISO/IEC 14496-12（ISO 基本媒体文件格式）与 3GPP TS 26.245（Timed Text）。
 *  balloonwj@qq.com   2026-10-06
 */
#include "Mp4TextTrack.h"

#include <windows.h>
#include <string.h>

namespace caption {

namespace {

//普通盒子头（4 字节大小 + 4 字节类型）与带 64 位大小的盒子头的字节数
const uint64_t kBoxHeaderBytes = 8;
const uint64_t kLargeBoxHeaderBytes = 16;
//盒子大小字段的两个特殊值：1 表示后面跟 64 位大小，0 表示一直到文件末尾
const uint32_t kBoxSizeLarge = 1;
const uint32_t kBoxSizeToEnd = 0;
//读进内存的 moov 的大小上限：正常录像的 moov 只有几百 KB，超过它视为文件异常
const uint64_t kMaxMoovBytes = 256ULL * 1024 * 1024;
//32 位无符号数的上限：超过它的偏移要用 co64，时长要用 64 位版本的盒子
const uint64_t kMaxU32 = 0xFFFFFFFFULL;
//文字轨的媒体时间刻度：1 毫秒一个单位
const uint32_t kTextTimescale = 1000;
//tkhd 的标志：轨道启用（0x1）且参与播放（0x2）
const uint32_t kTkhdFlags = 0x000003;
//16.16 定点数的 1.0 与 2.30 定点数的 1.0（tkhd 的变换矩阵）
const uint32_t kFixed16One = 0x00010000;
const uint32_t kFixed30One = 0x40000000;
//16.16 定点数的整数部分左移位数
const int kFixed16Shift = 16;
//dref 里 url 盒子的标志：媒体数据就在本文件里
const uint32_t kUrlSelfContained = 0x000001;
//tx3g 样本描述的取值（与 ffmpeg 写 mov_text 时的默认值相同）：水平居中、垂直靠下、字体编号 1、字号 18、白字
const uint8_t kTx3gHorizontalCenter = 1;
const uint8_t kTx3gVerticalBottom = 0xFF;
const uint16_t kTx3gFontId = 1;
const uint8_t kTx3gFontSize = 18;
const uint32_t kTx3gTextColorRgba = 0xFFFFFFFF;
const char* const kTx3gFontName = "Serif";
//sample entry 的 data_reference_index：指向 dref 的第 1 项
const uint16_t kDataReferenceIndex = 1;
//sample entry 开头 6 字节保留字段
const int kSampleEntryReservedBytes = 6;
//hdlr 的保留字段（3 个 32 位）与名字
const int kHdlrReservedWords = 3;
const char* const kHdlrName = "SubtitleHandler";
//语言代码：ISO 639-2 三个小写字母，打包时每个字母减去 0x60 后占 5 位
const int kLangChars = 3;
const int kLangBits = 5;
const char kLangBase = 0x60;
const char* const kLangUndetermined = "und";
//tkhd 变换矩阵的 9 个数
const int kMatrixWords = 9;
//mvhd 与 tkhd 第 1 版（64 位时间）的版本号
const uint8_t kBoxVersion1 = 1;

uint32_t FourCC(const char* s)
{
    return (static_cast<uint32_t>(static_cast<uint8_t>(s[0])) << 24) |
           (static_cast<uint32_t>(static_cast<uint8_t>(s[1])) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(s[2])) << 8) | static_cast<uint32_t>(static_cast<uint8_t>(s[3]));
}

std::string FourCCText(uint32_t v)
{
    std::string s(4, ' ');
    s[0] = static_cast<char>((v >> 24) & 0xFF);
    s[1] = static_cast<char>((v >> 16) & 0xFF);
    s[2] = static_cast<char>((v >> 8) & 0xFF);
    s[3] = static_cast<char>(v & 0xFF);
    return s;
}

uint16_t ReadU16(const uint8_t* p)
{
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

uint32_t ReadU32(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

uint64_t ReadU64(const uint8_t* p)
{
    return (static_cast<uint64_t>(ReadU32(p)) << 32) | ReadU32(p + 4);
}

void PutU32(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>((v >> 24) & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[3] = static_cast<uint8_t>(v & 0xFF);
}

void PutU64(uint8_t* p, uint64_t v)
{
    PutU32(p, static_cast<uint32_t>(v >> 32));
    PutU32(p + 4, static_cast<uint32_t>(v & kMaxU32));
}

void SetError(std::string* error, const std::string& text)
{
    if (error != NULL)
        *error = text;
}

//文件句柄的简单封装：析构时关闭
class Mp4File
{
public:
    Mp4File(const std::wstring& path, bool write)
        : m_handle(INVALID_HANDLE_VALUE)
    {
        const DWORD access = write ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ;
        m_handle = ::CreateFileW(path.c_str(), access, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    }

    ~Mp4File()
    {
        if (m_handle != INVALID_HANDLE_VALUE)
            ::CloseHandle(m_handle);
    }

    bool Ok() const
    {
        return m_handle != INVALID_HANDLE_VALUE;
    }

    bool Size(uint64_t& out)
    {
        LARGE_INTEGER size;
        if (!::GetFileSizeEx(m_handle, &size))
            return false;
        out = static_cast<uint64_t>(size.QuadPart);
        return true;
    }

    bool ReadAt(uint64_t offset, void* buf, size_t len)
    {
        if (!seek(offset))
            return false;
        uint8_t* p = static_cast<uint8_t*>(buf);
        while (len > 0)
        {
            const DWORD chunk = len > 0x10000000 ? 0x10000000 : static_cast<DWORD>(len);
            DWORD got = 0;
            if (!::ReadFile(m_handle, p, chunk, &got, NULL) || got != chunk)
                return false;
            p += got;
            len -= got;
        }
        return true;
    }

    bool WriteAt(uint64_t offset, const void* buf, size_t len)
    {
        if (!seek(offset))
            return false;
        const uint8_t* p = static_cast<const uint8_t*>(buf);
        while (len > 0)
        {
            const DWORD chunk = len > 0x10000000 ? 0x10000000 : static_cast<DWORD>(len);
            DWORD put = 0;
            if (!::WriteFile(m_handle, p, chunk, &put, NULL) || put != chunk)
                return false;
            p += put;
            len -= put;
        }
        return true;
    }

    bool Truncate(uint64_t size)
    {
        return seek(size) && ::SetEndOfFile(m_handle) != FALSE;
    }

    bool Flush()
    {
        return ::FlushFileBuffers(m_handle) != FALSE;
    }

private:
    bool seek(uint64_t offset)
    {
        LARGE_INTEGER li;
        li.QuadPart = static_cast<LONGLONG>(offset);
        return ::SetFilePointerEx(m_handle, li, NULL, FILE_BEGIN) != FALSE;
    }

    HANDLE m_handle;   //文件句柄；打开失败时为 INVALID_HANDLE_VALUE
};

//文件里的一个顶层盒子
struct FileBox
{
    uint32_t m_type;     //盒子类型
    uint64_t m_offset;   //盒子起点（含盒子头）在文件里的偏移
    uint64_t m_size;     //盒子总字节数（含盒子头）
    uint64_t m_header;   //盒子头字节数（8 或 16）
};

bool ListFileBoxes(Mp4File& f, uint64_t fileSize, std::vector<FileBox>& out, std::string* error)
{
    out.clear();
    uint64_t off = 0;
    while (off < fileSize)
    {
        if (fileSize - off < kBoxHeaderBytes)
        {
            SetError(error, "trailing bytes shorter than a box header");
            return false;
        }
        uint8_t head[kLargeBoxHeaderBytes];
        if (!f.ReadAt(off, head, static_cast<size_t>(kBoxHeaderBytes)))
        {
            SetError(error, "read box header failed");
            return false;
        }
        FileBox b;
        b.m_type = ReadU32(head + 4);
        b.m_offset = off;
        b.m_header = kBoxHeaderBytes;
        const uint32_t size32 = ReadU32(head);
        if (size32 == kBoxSizeLarge)
        {
            if (fileSize - off < kLargeBoxHeaderBytes ||
                !f.ReadAt(off + kBoxHeaderBytes, head + kBoxHeaderBytes, static_cast<size_t>(kBoxHeaderBytes)))
            {
                SetError(error, "read large box header failed");
                return false;
            }
            b.m_size = ReadU64(head + kBoxHeaderBytes);
            b.m_header = kLargeBoxHeaderBytes;
        }
        else if (size32 == kBoxSizeToEnd)
        {
            b.m_size = fileSize - off;
        }
        else
        {
            b.m_size = size32;
        }
        if (b.m_size < b.m_header || b.m_size > fileSize - off)
        {
            SetError(error, "box " + FourCCText(b.m_type) + " has an invalid size");
            return false;
        }
        out.push_back(b);
        off += b.m_size;
    }
    return true;
}

//内存里的一个盒子（moov 的子孙）
struct MemBox
{
    uint32_t m_type;     //盒子类型
    size_t   m_offset;   //盒子起点在缓冲区里的偏移
    size_t   m_size;     //盒子总字节数
    size_t   m_header;   //盒子头字节数
};

//解析缓冲区 [begin, end) 里依次排列的盒子
bool ParseMemBoxes(const std::vector<uint8_t>& buf, size_t begin, size_t end, std::vector<MemBox>& out)
{
    out.clear();
    size_t off = begin;
    while (off < end)
    {
        if (end - off < kBoxHeaderBytes)
            return false;
        MemBox b;
        b.m_type = ReadU32(&buf[off + 4]);
        b.m_offset = off;
        b.m_header = static_cast<size_t>(kBoxHeaderBytes);
        const uint32_t size32 = ReadU32(&buf[off]);
        uint64_t size = size32;
        if (size32 == kBoxSizeLarge)
        {
            if (end - off < kLargeBoxHeaderBytes)
                return false;
            size = ReadU64(&buf[off + kBoxHeaderBytes]);
            b.m_header = static_cast<size_t>(kLargeBoxHeaderBytes);
        }
        else if (size32 == kBoxSizeToEnd)
        {
            size = end - off;
        }
        if (size < b.m_header || size > end - off)
            return false;
        b.m_size = static_cast<size_t>(size);
        out.push_back(b);
        off += b.m_size;
    }
    return true;
}

//在子盒子里找第一个指定类型的；找不到返回 NULL
const MemBox* FindBox(const std::vector<MemBox>& boxes, const char* type)
{
    const uint32_t t = FourCC(type);
    for (size_t i = 0; i < boxes.size(); ++i)
    {
        if (boxes[i].m_type == t)
            return &boxes[i];
    }
    return NULL;
}

//解析某个盒子的子盒子（容器盒子，内容紧跟在盒子头之后）
bool ChildrenOf(const std::vector<uint8_t>& buf, const MemBox& box, std::vector<MemBox>& out)
{
    return ParseMemBoxes(buf, box.m_offset + box.m_header, box.m_offset + box.m_size, out);
}

//按路径找后代盒子，如 {"mdia", "minf", "stbl"}；找不到返回 false
bool FindPath(const std::vector<uint8_t>& buf, const MemBox& root, const char* const* path, size_t depth, MemBox& out)
{
    MemBox cur = root;
    for (size_t i = 0; i < depth; ++i)
    {
        std::vector<MemBox> kids;
        if (!ChildrenOf(buf, cur, kids))
            return false;
        const MemBox* next = FindBox(kids, path[i]);
        if (next == NULL)
            return false;
        cur = *next;
    }
    out = cur;
    return true;
}

//盒子正文（盒子头之后）的起点与长度
const uint8_t* BodyOf(const std::vector<uint8_t>& buf, const MemBox& box, size_t& len)
{
    len = box.m_size - box.m_header;
    return &buf[box.m_offset + box.m_header];
}

//按大端顺序拼接字节，支持先写盒子头、写完内容后回填大小
class BoxWriter
{
public:
    void U8(uint8_t v)
    {
        m_buf.push_back(v);
    }

    void U16(uint16_t v)
    {
        m_buf.push_back(static_cast<uint8_t>(v >> 8));
        m_buf.push_back(static_cast<uint8_t>(v & 0xFF));
    }

    void U24(uint32_t v)
    {
        m_buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        m_buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        m_buf.push_back(static_cast<uint8_t>(v & 0xFF));
    }

    void U32(uint32_t v)
    {
        uint8_t b[4];
        PutU32(b, v);
        m_buf.insert(m_buf.end(), b, b + 4);
    }

    void U64(uint64_t v)
    {
        uint8_t b[8];
        PutU64(b, v);
        m_buf.insert(m_buf.end(), b, b + 8);
    }

    void Type(const char* t)
    {
        m_buf.insert(m_buf.end(), t, t + 4);
    }

    void Bytes(const void* p, size_t len)
    {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        m_buf.insert(m_buf.end(), b, b + len);
    }

    void Zeros(size_t n)
    {
        m_buf.insert(m_buf.end(), n, 0);
    }

    //开始一个盒子：返回它的起点，写完内容后交给 End 回填大小
    size_t Begin(const char* type)
    {
        const size_t pos = m_buf.size();
        U32(0);
        Type(type);
        return pos;
    }

    //开始一个带版本与标志的盒子（full box）
    size_t BeginFull(const char* type, uint8_t version, uint32_t flags)
    {
        const size_t pos = Begin(type);
        U8(version);
        U24(flags);
        return pos;
    }

    void End(size_t pos)
    {
        PutU32(&m_buf[pos], static_cast<uint32_t>(m_buf.size() - pos));
    }

    std::vector<uint8_t>& Buffer()
    {
        return m_buf;
    }

private:
    std::vector<uint8_t> m_buf;   //已写的字节
};

//语言代码打包成 mdhd 的 16 位字段
uint16_t PackLanguage(const char* language)
{
    const char* lang = language;
    bool ok = (lang != NULL && strlen(lang) == static_cast<size_t>(kLangChars));
    for (int i = 0; ok && i < kLangChars; ++i)
        ok = (lang[i] >= 'a' && lang[i] <= 'z');
    if (!ok)
        lang = kLangUndetermined;
    uint16_t v = 0;
    for (int i = 0; i < kLangChars; ++i)
        v = static_cast<uint16_t>((v << kLangBits) | static_cast<uint16_t>(lang[i] - kLangBase));
    return v;
}

//写 tx3g 样本描述（3GPP TS 26.245 的 TextSampleEntry）
void WriteTx3gEntry(BoxWriter& w)
{
    const size_t entry = w.Begin("tx3g");
    w.Zeros(kSampleEntryReservedBytes);
    w.U16(kDataReferenceIndex);
    w.U32(0);                          //displayFlags
    w.U8(kTx3gHorizontalCenter);       //水平居中
    w.U8(kTx3gVerticalBottom);         //垂直靠下
    w.U32(0);                          //背景色 RGBA：全透明
    w.U16(0);                          //BoxRecord：top
    w.U16(0);                          //left
    w.U16(0);                          //bottom
    w.U16(0);                          //right
    w.U16(0);                          //StyleRecord：startChar
    w.U16(0);                          //endChar
    w.U16(kTx3gFontId);
    w.U8(0);                           //face-style-flags：常规
    w.U8(kTx3gFontSize);
    w.U32(kTx3gTextColorRgba);
    const size_t ftab = w.Begin("ftab");
    w.U16(1);                          //字体表一项
    w.U16(kTx3gFontId);
    const size_t nameLen = strlen(kTx3gFontName);
    w.U8(static_cast<uint8_t>(nameLen));
    w.Bytes(kTx3gFontName, nameLen);
    w.End(ftab);
    w.End(entry);
}

//写文字轨（trak）
void WriteTextTrak(BoxWriter& w, uint32_t trackId, uint64_t movieDuration, uint64_t durationMs, uint32_t widthPx,
                   uint32_t heightPx, const char* language, const std::vector<Mp4TextSample>& samples,
                   const std::vector<uint64_t>& offsets, bool useCo64)
{
    const size_t trak = w.Begin("trak");

    const bool tkhdV1 = movieDuration > kMaxU32;
    const size_t tkhd = w.BeginFull("tkhd", tkhdV1 ? kBoxVersion1 : 0, kTkhdFlags);
    if (tkhdV1)
    {
        w.U64(0);
        w.U64(0);
        w.U32(trackId);
        w.U32(0);
        w.U64(movieDuration);
    }
    else
    {
        w.U32(0);
        w.U32(0);
        w.U32(trackId);
        w.U32(0);
        w.U32(static_cast<uint32_t>(movieDuration));
    }
    w.U32(0);                          //reserved[2]
    w.U32(0);
    w.U16(0);                          //layer
    w.U16(0);                          //alternate_group
    w.U16(0);                          //volume：文字轨为 0
    w.U16(0);                          //reserved
    const uint32_t matrix[kMatrixWords] = { kFixed16One, 0, 0, 0, kFixed16One, 0, 0, 0, kFixed30One };
    for (int i = 0; i < kMatrixWords; ++i)
        w.U32(matrix[i]);
    w.U32(widthPx << kFixed16Shift);
    w.U32(heightPx << kFixed16Shift);
    w.End(tkhd);

    const size_t mdia = w.Begin("mdia");
    const bool mdhdV1 = durationMs > kMaxU32;
    const size_t mdhd = w.BeginFull("mdhd", mdhdV1 ? kBoxVersion1 : 0, 0);
    if (mdhdV1)
    {
        w.U64(0);
        w.U64(0);
        w.U32(kTextTimescale);
        w.U64(durationMs);
    }
    else
    {
        w.U32(0);
        w.U32(0);
        w.U32(kTextTimescale);
        w.U32(static_cast<uint32_t>(durationMs));
    }
    w.U16(PackLanguage(language));
    w.U16(0);
    w.End(mdhd);

    const size_t hdlr = w.BeginFull("hdlr", 0, 0);
    w.U32(0);                          //pre_defined
    w.Type("sbtl");
    for (int i = 0; i < kHdlrReservedWords; ++i)
        w.U32(0);
    w.Bytes(kHdlrName, strlen(kHdlrName) + 1);
    w.End(hdlr);

    const size_t minf = w.Begin("minf");
    const size_t nmhd = w.BeginFull("nmhd", 0, 0);
    w.End(nmhd);
    const size_t dinf = w.Begin("dinf");
    const size_t dref = w.BeginFull("dref", 0, 0);
    w.U32(1);
    const size_t url = w.BeginFull("url ", 0, kUrlSelfContained);
    w.End(url);
    w.End(dref);
    w.End(dinf);

    const size_t stbl = w.Begin("stbl");
    const size_t stsd = w.BeginFull("stsd", 0, 0);
    w.U32(1);
    WriteTx3gEntry(w);
    w.End(stsd);

    //stts：相邻时长相同的样本合成一项
    const size_t stts = w.BeginFull("stts", 0, 0);
    const size_t countPos = w.Buffer().size();
    w.U32(0);
    uint32_t entries = 0;
    size_t i = 0;
    while (i < samples.size())
    {
        size_t k = i + 1;
        while (k < samples.size() && samples[k].m_durationMs == samples[i].m_durationMs)
            ++k;
        w.U32(static_cast<uint32_t>(k - i));
        w.U32(static_cast<uint32_t>(samples[i].m_durationMs));
        ++entries;
        i = k;
    }
    PutU32(&w.Buffer()[countPos], entries);
    w.End(stts);

    const size_t stsz = w.BeginFull("stsz", 0, 0);
    w.U32(0);                          //各样本大小不同
    w.U32(static_cast<uint32_t>(samples.size()));
    for (size_t s = 0; s < samples.size(); ++s)
        w.U32(static_cast<uint32_t>(sizeof(uint16_t) + samples[s].m_utf8.size()));
    w.End(stsz);

    //一块一个样本
    const size_t stsc = w.BeginFull("stsc", 0, 0);
    w.U32(1);
    w.U32(1);                          //first_chunk
    w.U32(1);                          //samples_per_chunk
    w.U32(1);                          //sample_description_index
    w.End(stsc);

    const size_t stco = w.BeginFull(useCo64 ? "co64" : "stco", 0, 0);
    w.U32(static_cast<uint32_t>(offsets.size()));
    for (size_t s = 0; s < offsets.size(); ++s)
    {
        if (useCo64)
            w.U64(offsets[s]);
        else
            w.U32(static_cast<uint32_t>(offsets[s]));
    }
    w.End(stco);
    w.End(stbl);
    w.End(minf);
    w.End(mdia);
    w.End(trak);
}

//mvhd 里的时间刻度、时长与 next_track_ID 的位置
struct MvhdInfo
{
    uint32_t m_timescale;      //时间刻度
    uint64_t m_duration;       //时长（按时间刻度）
    bool     m_v1;             //是否为第 1 版（64 位时间）
    size_t   m_durationPos;    //时长字段在 moov 缓冲区里的偏移
    size_t   m_nextIdPos;      //next_track_ID 字段在 moov 缓冲区里的偏移
};

bool ParseMvhd(const std::vector<uint8_t>& buf, const MemBox& mvhd, MvhdInfo& out)
{
    size_t len = 0;
    const uint8_t* p = BodyOf(buf, mvhd, len);
    const size_t bodyStart = mvhd.m_offset + mvhd.m_header;
    //版本与标志 4 字节；第 0 版之后是 4+4+4+4（创建、修改、刻度、时长），第 1 版是 8+8+4+8
    const size_t kV0Need = 4 + 16;
    const size_t kV1Need = 4 + 28;
    if (len < kV0Need + sizeof(uint32_t))
        return false;
    out.m_v1 = (p[0] == kBoxVersion1);
    if (out.m_v1)
    {
        if (len < kV1Need + sizeof(uint32_t))
            return false;
        out.m_timescale = ReadU32(p + 4 + 16);
        out.m_duration = ReadU64(p + 4 + 20);
        out.m_durationPos = bodyStart + 4 + 20;
    }
    else
    {
        out.m_timescale = ReadU32(p + 4 + 8);
        out.m_duration = ReadU32(p + 4 + 12);
        out.m_durationPos = bodyStart + 4 + 12;
    }
    //next_track_ID 是 mvhd 的最后 4 个字节
    out.m_nextIdPos = mvhd.m_offset + mvhd.m_size - sizeof(uint32_t);
    return out.m_timescale != 0;
}

//tkhd 里的 track_ID；读不出时返回 0
uint32_t TrackIdOf(const std::vector<uint8_t>& buf, const MemBox& trak)
{
    std::vector<MemBox> kids;
    if (!ChildrenOf(buf, trak, kids))
        return 0;
    const MemBox* tkhd = FindBox(kids, "tkhd");
    if (tkhd == NULL)
        return 0;
    size_t len = 0;
    const uint8_t* p = BodyOf(buf, *tkhd, len);
    const size_t v0Pos = 4 + 8;
    const size_t v1Pos = 4 + 16;
    const size_t pos = (len > 0 && p[0] == kBoxVersion1) ? v1Pos : v0Pos;
    if (len < pos + sizeof(uint32_t))
        return 0;
    return ReadU32(p + pos);
}

} // namespace

bool AppendTx3gTrack(const std::wstring& path, const std::vector<Mp4TextSample>& samples, uint32_t widthPx,
                     uint32_t heightPx, const char* language, std::string* error)
{
    if (samples.empty())
    {
        SetError(error, "no text samples");
        return false;
    }
    uint64_t durationMs = 0;
    for (size_t i = 0; i < samples.size(); ++i)
    {
        if (samples[i].m_durationMs <= 0 || static_cast<uint64_t>(samples[i].m_durationMs) > kMaxU32 ||
            samples[i].m_utf8.size() > kMp4TextSampleMaxBytes)
        {
            SetError(error, "invalid text sample");
            return false;
        }
        durationMs += static_cast<uint64_t>(samples[i].m_durationMs);
    }

    Mp4File f(path, true);
    uint64_t fileSize = 0;
    if (!f.Ok() || !f.Size(fileSize))
    {
        SetError(error, "open file failed");
        return false;
    }
    std::vector<FileBox> boxes;
    if (!ListFileBoxes(f, fileSize, boxes, error))
        return false;
    int moovIndex = -1;
    for (size_t i = 0; i < boxes.size(); ++i)
    {
        if (boxes[i].m_type == FourCC("moov"))
        {
            if (moovIndex >= 0)
            {
                SetError(error, "more than one moov");
                return false;
            }
            moovIndex = static_cast<int>(i);
        }
    }
    if (moovIndex < 0)
    {
        SetError(error, "moov not found");
        return false;
    }
    const FileBox moov = boxes[moovIndex];
    if (moov.m_size > kMaxMoovBytes)
    {
        SetError(error, "moov too large");
        return false;
    }
    std::vector<uint8_t> moovBuf(static_cast<size_t>(moov.m_size));
    if (!f.ReadAt(moov.m_offset, &moovBuf[0], moovBuf.size()))
    {
        SetError(error, "read moov failed");
        return false;
    }
    std::vector<MemBox> moovKids;
    if (!ParseMemBoxes(moovBuf, static_cast<size_t>(moov.m_header), moovBuf.size(), moovKids))
    {
        SetError(error, "parse moov failed");
        return false;
    }
    const MemBox* mvhdBox = FindBox(moovKids, "mvhd");
    MvhdInfo mvhd;
    if (mvhdBox == NULL || !ParseMvhd(moovBuf, *mvhdBox, mvhd))
    {
        SetError(error, "mvhd not found or invalid");
        return false;
    }
    uint32_t maxTrackId = 0;
    for (size_t i = 0; i < moovKids.size(); ++i)
    {
        if (moovKids[i].m_type == FourCC("trak"))
        {
            const uint32_t id = TrackIdOf(moovBuf, moovKids[i]);
            if (id > maxTrackId)
                maxTrackId = id;
        }
    }
    const uint32_t nextId = ReadU32(&moovBuf[mvhd.m_nextIdPos]);
    uint32_t trackId = maxTrackId + 1;
    if (nextId > trackId && nextId != static_cast<uint32_t>(kMaxU32))
        trackId = nextId;

    //moov 在文件末尾：从它的位置起重写；否则在文件末尾追加，原 moov 改成 free
    const bool moovIsLast = (moovIndex == static_cast<int>(boxes.size()) - 1);
    const uint64_t appendPos = moovIsLast ? moov.m_offset : fileSize;

    //文字样本的 mdat：每个样本 2 字节大端长度 + UTF-8 文字
    BoxWriter mdat;
    const size_t mdatPos = mdat.Begin("mdat");
    std::vector<uint64_t> offsets;
    offsets.reserve(samples.size());
    for (size_t i = 0; i < samples.size(); ++i)
    {
        offsets.push_back(appendPos + mdat.Buffer().size());
        mdat.U16(static_cast<uint16_t>(samples[i].m_utf8.size()));
        mdat.Bytes(samples[i].m_utf8.data(), samples[i].m_utf8.size());
    }
    mdat.End(mdatPos);
    const bool useCo64 = offsets.back() > kMaxU32;

    //新 moov：原有子盒子原样拷贝，再加文字轨；mvhd 的 next_track_ID 与时长按需更新
    const uint64_t textMovieDuration = durationMs * mvhd.m_timescale / kTextTimescale;
    if (textMovieDuration > mvhd.m_duration)
    {
        if (mvhd.m_v1)
            PutU64(&moovBuf[mvhd.m_durationPos], textMovieDuration);
        else if (textMovieDuration <= kMaxU32)
            PutU32(&moovBuf[mvhd.m_durationPos], static_cast<uint32_t>(textMovieDuration));
    }
    PutU32(&moovBuf[mvhd.m_nextIdPos], trackId + 1);
    BoxWriter newMoov;
    const size_t newMoovPos = newMoov.Begin("moov");
    for (size_t i = 0; i < moovKids.size(); ++i)
        newMoov.Bytes(&moovBuf[moovKids[i].m_offset], moovKids[i].m_size);
    WriteTextTrak(newMoov, trackId, textMovieDuration, durationMs, widthPx, heightPx, language, samples, offsets,
                  useCo64);
    newMoov.End(newMoovPos);

    if (moovIsLast)
    {
        if (!f.Truncate(appendPos))
        {
            SetError(error, "truncate old moov failed");
            return false;
        }
    }
    if (!f.WriteAt(appendPos, &mdat.Buffer()[0], mdat.Buffer().size()) ||
        !f.WriteAt(appendPos + mdat.Buffer().size(), &newMoov.Buffer()[0], newMoov.Buffer().size()))
    {
        SetError(error, "write text track failed");
        return false;
    }
    if (!moovIsLast)
    {
        //原 moov 作废：只改盒子类型，大小不变，其后的数据位置不受影响
        const char freeType[4] = { 'f', 'r', 'e', 'e' };
        if (!f.WriteAt(moov.m_offset + 4, freeType, sizeof(freeType)))
        {
            SetError(error, "mark old moov free failed");
            return false;
        }
    }
    f.Flush();
    return true;
}

bool ReadTx3gTrack(const std::wstring& path, std::vector<Mp4TextSample>& samples, std::string* error)
{
    samples.clear();
    Mp4File f(path, false);
    uint64_t fileSize = 0;
    if (!f.Ok() || !f.Size(fileSize))
    {
        SetError(error, "open file failed");
        return false;
    }
    std::vector<FileBox> boxes;
    if (!ListFileBoxes(f, fileSize, boxes, error))
        return false;
    const FileBox* moov = NULL;
    for (size_t i = 0; i < boxes.size(); ++i)
    {
        if (boxes[i].m_type == FourCC("moov"))
            moov = &boxes[i];
    }
    if (moov == NULL || moov->m_size > kMaxMoovBytes)
    {
        SetError(error, "moov not found");
        return false;
    }
    std::vector<uint8_t> buf(static_cast<size_t>(moov->m_size));
    if (!f.ReadAt(moov->m_offset, &buf[0], buf.size()))
    {
        SetError(error, "read moov failed");
        return false;
    }
    std::vector<MemBox> kids;
    if (!ParseMemBoxes(buf, static_cast<size_t>(moov->m_header), buf.size(), kids))
    {
        SetError(error, "parse moov failed");
        return false;
    }
    const char* const kStblPath[] = { "mdia", "minf", "stbl" };
    const char* const kMdhdPath[] = { "mdia", "mdhd" };
    const size_t kStblDepth = 3;
    const size_t kMdhdDepth = 2;
    for (size_t t = 0; t < kids.size(); ++t)
    {
        if (kids[t].m_type != FourCC("trak"))
            continue;
        MemBox stbl;
        MemBox mdhd;
        if (!FindPath(buf, kids[t], kStblPath, kStblDepth, stbl) || !FindPath(buf, kids[t], kMdhdPath, kMdhdDepth, mdhd))
            continue;
        std::vector<MemBox> st;
        if (!ChildrenOf(buf, stbl, st))
            continue;
        const MemBox* stsd = FindBox(st, "stsd");
        const MemBox* stts = FindBox(st, "stts");
        const MemBox* stsz = FindBox(st, "stsz");
        const MemBox* stsc = FindBox(st, "stsc");
        const MemBox* stco = FindBox(st, "stco");
        const MemBox* co64 = FindBox(st, "co64");
        if (stsd == NULL || stts == NULL || stsz == NULL || stsc == NULL || (stco == NULL && co64 == NULL))
            continue;
        size_t len = 0;
        const uint8_t* p = BodyOf(buf, *stsd, len);
        //stsd 正文：版本与标志 4、项数 4，第一项的类型在其后 4 字节处
        const size_t kStsdEntryTypePos = 12;
        if (len < kStsdEntryTypePos + 4 || ReadU32(p + kStsdEntryTypePos) != FourCC("tx3g"))
            continue;
        //媒体时间刻度
        const uint8_t* mp = BodyOf(buf, mdhd, len);
        const size_t tsPos = (len > 0 && mp[0] == kBoxVersion1) ? (4 + 16) : (4 + 8);
        if (len < tsPos + 4)
            continue;
        const uint32_t timescale = ReadU32(mp + tsPos);
        if (timescale == 0)
            continue;
        //样本时长
        std::vector<int64_t> durations;
        p = BodyOf(buf, *stts, len);
        if (len < 8)
            continue;
        const uint32_t sttsCount = ReadU32(p + 4);
        if (len < 8 + static_cast<size_t>(sttsCount) * 8)
            continue;
        for (uint32_t e = 0; e < sttsCount; ++e)
        {
            const uint32_t n = ReadU32(p + 8 + e * 8);
            const uint32_t delta = ReadU32(p + 12 + e * 8);
            const int64_t ms = (static_cast<int64_t>(delta) * kTextTimescale + timescale / 2) / timescale;
            durations.insert(durations.end(), n, ms);
        }
        //样本大小
        std::vector<uint32_t> sizes;
        p = BodyOf(buf, *stsz, len);
        if (len < 12)
            continue;
        const uint32_t fixedSize = ReadU32(p + 4);
        const uint32_t sampleCount = ReadU32(p + 8);
        if (fixedSize == 0 && len < 12 + static_cast<size_t>(sampleCount) * 4)
            continue;
        for (uint32_t s = 0; s < sampleCount; ++s)
            sizes.push_back(fixedSize != 0 ? fixedSize : ReadU32(p + 12 + s * 4));
        //块偏移
        std::vector<uint64_t> chunkOffsets;
        const bool is64 = (co64 != NULL);
        p = BodyOf(buf, is64 ? *co64 : *stco, len);
        if (len < 8)
            continue;
        const uint32_t chunkCount = ReadU32(p + 4);
        const size_t entryBytes = is64 ? 8 : 4;
        if (len < 8 + static_cast<size_t>(chunkCount) * entryBytes)
            continue;
        for (uint32_t c = 0; c < chunkCount; ++c)
            chunkOffsets.push_back(is64 ? ReadU64(p + 8 + c * 8) : ReadU32(p + 8 + c * 4));
        //样本到块的对应：按 stsc 展开每块的样本数
        p = BodyOf(buf, *stsc, len);
        if (len < 8)
            continue;
        const uint32_t stscCount = ReadU32(p + 4);
        const size_t kStscEntryBytes = 12;
        if (len < 8 + static_cast<size_t>(stscCount) * kStscEntryBytes)
            continue;
        std::vector<uint32_t> perChunk(chunkOffsets.size(), 0);
        for (uint32_t e = 0; e < stscCount; ++e)
        {
            const uint32_t first = ReadU32(p + 8 + e * kStscEntryBytes);
            const uint32_t per = ReadU32(p + 12 + e * kStscEntryBytes);
            const uint32_t last = (e + 1 < stscCount) ? ReadU32(p + 8 + (e + 1) * kStscEntryBytes) - 1
                                                      : static_cast<uint32_t>(chunkOffsets.size());
            for (uint32_t c = first; c <= last && c >= 1 && c <= chunkOffsets.size(); ++c)
                perChunk[c - 1] = per;
        }
        if (durations.size() != sizes.size())
        {
            SetError(error, "stts and stsz disagree");
            return false;
        }
        size_t sampleIndex = 0;
        for (size_t c = 0; c < chunkOffsets.size() && sampleIndex < sizes.size(); ++c)
        {
            uint64_t off = chunkOffsets[c];
            for (uint32_t k = 0; k < perChunk[c] && sampleIndex < sizes.size(); ++k)
            {
                const uint32_t size = sizes[sampleIndex];
                if (size < sizeof(uint16_t) || off + size > fileSize)
                {
                    SetError(error, "text sample out of range");
                    return false;
                }
                std::vector<uint8_t> data(size);
                if (!f.ReadAt(off, &data[0], size))
                {
                    SetError(error, "read text sample failed");
                    return false;
                }
                const uint16_t textLen = ReadU16(&data[0]);
                if (static_cast<size_t>(textLen) + sizeof(uint16_t) > size)
                {
                    SetError(error, "text sample length field too large");
                    return false;
                }
                Mp4TextSample s;
                s.m_durationMs = durations[sampleIndex];
                //空样本只有长度字段，不能写 &data[2]（越界）；用指针运算取文字起点
                s.m_utf8.assign(reinterpret_cast<const char*>(data.data()) + sizeof(uint16_t), textLen);
                samples.push_back(s);
                off += size;
                ++sampleIndex;
            }
        }
        if (sampleIndex != sizes.size())
        {
            SetError(error, "chunk table does not cover all samples");
            return false;
        }
        return true;
    }
    SetError(error, "no tx3g track");
    return false;
}

std::vector<std::string> ListMp4TopLevelBoxes(const std::wstring& path)
{
    std::vector<std::string> out;
    Mp4File f(path, false);
    uint64_t fileSize = 0;
    if (!f.Ok() || !f.Size(fileSize))
        return out;
    std::vector<FileBox> boxes;
    if (!ListFileBoxes(f, fileSize, boxes, NULL))
        return out;
    for (size_t i = 0; i < boxes.size(); ++i)
        out.push_back(FourCCText(boxes[i].m_type));
    return out;
}

} // namespace caption
