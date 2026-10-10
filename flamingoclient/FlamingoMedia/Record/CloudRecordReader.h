/**
 *  云端录像原始帧文件（.frec）的读取，CloudRecordReader.h
 *
 *  第 27a 期（docs/屏幕共享与录制方案.md 12.6.3）：共享中的云端录制由 imgserver 把房间里的帧原样写进 rec_<录像 id>.frec，开启者的客户端
 *  下载后在本机离线合成 MP4。本模块读这个文件，格式与服务端 flamingoserver/imgserversrc/CloudRecordFile.h 逐字节一致：
 *    文件头：「FREC」| 版本 u16 | 标志 u16 | 录像 id u64 | 开始时刻 i64（Unix 毫秒）| 房间号长度 u16 | 房间号
 *    记录：类型 u8 | 相对开始时刻的毫秒 u32 | 负载长度 u32 | 负载（全部整数为小端）
 *      - 帧（1）：负载为房间中转帧的明文包体（11 字节帧头 + 数据），可直接交给 ShareFrameReassembler；
 *      - 座位（2）：负载为座位号 u8 + userid u64；
 *      - 暂停（3）/ 继续（4）：没有负载。
 *  文件只追加，imgserver 中途退出时末尾可能残缺：读到最后一条完整的记录为止。
 *  纯逻辑部分（解码函数）不依赖文件，单元测试见 Source/Tests/CloudRecordReaderTests.cpp（标签 [cloudrec]）。
 *
 *  典型用法：
 *      screenrecord::CFrecReader reader;
 *      if (reader.Open(path))
 *      {
 *          screenrecord::FrecRecord rec;
 *          while (reader.Next(rec)) ...按 rec.m_type 分别处理...
 *      }
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string>

namespace screenrecord {

//文件开头的标识
const char kFrecMagic[] = "FREC";
//标识的字节数
const size_t kFrecMagicBytes = 4;
//读得懂的格式版本
const uint16_t kFrecVersion = 1;
//单条记录负载的上限（字节）：超过视为文件损坏，与服务端一致
const uint32_t kFrecMaxPayloadBytes = 8U * 1024 * 1024;
//记录头的字节数：类型 1 + 相对毫秒 4 + 负载长度 4
const size_t kFrecRecordHeadBytes = 9;
//座位记录负载的字节数：座位号 1 + userid 8
const size_t kFrecSeatPayloadBytes = 9;

//记录的类型（与服务端 frec_record_type 一致）
enum FrecRecordType
{
    kFrecRecordFrame  = 1,   //一帧房间中转帧（明文包体）
    kFrecRecordSeat   = 2,   //座位归属
    kFrecRecordPause  = 3,   //开始暂停
    kFrecRecordResume = 4,   //继续录制
};

//文件头
struct FrecFileHeader
{
    uint16_t    m_version;      //格式版本
    uint16_t    m_flags;        //标志位（第 0 位为只录声音，目前不会置上）
    uint64_t    m_recordId;     //录像 id
    int64_t     m_startTimeMs;  //开始录制的时刻，Unix 毫秒
    std::string m_roomId;       //屏幕共享房间号

    FrecFileHeader();
};

//一条记录
struct FrecRecord
{
    int         m_type;      //类型，取值见 FrecRecordType；不认识的类型原样返回，调用方跳过
    uint32_t    m_relMs;     //相对开始时刻的毫秒数
    std::string m_payload;   //负载

    FrecRecord();
};

/**
 *  从 data 的 pos 处解出文件头；成功时 pos 移到文件头之后。
 *  @return 标识与版本正确、文件头完整时返回 true
 */
bool DecodeFrecHeader(const std::string& data, size_t& pos, FrecFileHeader& out);

/**
 *  从 data 的 pos 处解出一条记录；成功时 pos 移到这条记录之后。
 *  @return 记录完整且负载不超过上限返回 true；数据不够（末尾残缺）或负载超限返回 false，pos 不变
 */
bool DecodeFrecRecord(const std::string& data, size_t& pos, FrecRecord& out);

//解出座位记录的负载；长度不对返回 false
bool DecodeFrecSeat(const std::string& payload, int& seat, int64_t& userId);

//原始帧文件的顺序读取器：按块读入，逐条交出记录，长录像也不必整文件读进内存
class CFrecReader
{
public:
    //每次从文件读入的块大小（字节）
    static const size_t kReadChunkBytes = 1024 * 1024;

    CFrecReader();
    ~CFrecReader();

    CFrecReader(const CFrecReader&) = delete;
    CFrecReader& operator=(const CFrecReader&) = delete;

    /**
     *  打开文件并读出文件头。
     *  @param path 文件路径
     *  @return 文件能打开且文件头合法返回 true
     */
    bool Open(const std::wstring& path);

    //关闭文件；析构时自动调用
    void Close();

    //文件头（Open 成功后有效）
    const FrecFileHeader& Header() const;

    /**
     *  读下一条记录。
     *  @param out [出参] 记录
     *  @return 读到返回 true；到文件尾、末尾残缺或文件损坏返回 false（IsTruncated 区分是否有残缺）
     */
    bool Next(FrecRecord& out);

    //文件尾是否有残缺的数据（imgserver 中途退出留下的）；Next 返回 false 之后有意义
    bool IsTruncated() const;

    //已读过的字节数（含文件头），用于显示合成进度
    uint64_t BytesConsumed() const;

    //文件总字节数（Open 时取得）
    uint64_t FileBytes() const;

private:
    //从文件再读一块追加到缓冲；读到文件尾返回 false
    bool fill();

    FILE*          m_file;       //文件；没打开时为 NULL
    FrecFileHeader m_header;     //文件头
    std::string    m_buf;        //已读入、还没交出的字节
    size_t         m_pos;        //m_buf 里下一条记录的起点
    uint64_t       m_consumed;   //已交出的字节数（含文件头）
    uint64_t       m_fileBytes;  //文件总字节数
    bool           m_eof;        //文件已读完
    bool           m_truncated;  //末尾有残缺
};

} // namespace screenrecord
