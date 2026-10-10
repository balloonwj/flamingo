/**
 *  语音消息的 Ogg Opus 文件读写（语音消息第 2a 步，方案见 docs/语音消息方案.md 第 4 节 V2、第 7.1 节）。
 *
 *  语音消息的文件是 Ogg 封装的 Opus（RFC 7845）：单声道、16 kHz 录音、20 毫秒一帧。仓库里没有 libogg，
 *  本文件自带一份只覆盖本需求的 Ogg 页读写：
 *    - VoiceOggWriter：先写 OpusHead、OpusTags 两个头页，再把 Opus 包按页写入（每页最多 kOggPacketsPerPage
 *      个包），最后一页带结束标志；granule 按 48 kHz 计并计入 pre-skip，校验和是 Ogg 规定的 CRC32。
 *    - VoiceOggReader：逐页检查页头与校验和，把包拼回来，检查前两个包是 OpusHead 与 OpusTags，
 *      取出 pre-skip 与最后一页的 granule 算出时长。格式不对时返回失败，不越界读。
 *  两个类都只处理内存里的字节，另带读写文件的便捷函数；不依赖 Opus 库，可以直接做单元测试。
 *
 *  典型用法：
 *      voicemsg::VoiceOggWriter writer;
 *      writer.Begin(serial, voicemsg::kVoiceSampleRate, preSkip48k);
 *      writer.AddPacket(packet, packetLen, voicemsg::kOpusSamplesPerFrame48k);   //每编出一帧调用一次
 *      writer.Finish();
 *      voicemsg::SaveBytesToFile(path, writer.Bytes());
 *
 *      voicemsg::VoiceOggReader reader;
 *      if (reader.Parse(bytes))
 *          for (size_t i = 0; i < reader.Packets().size(); ++i) ...   //逐个交给 Opus 解码
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

namespace voicemsg {

//录音与编码的采样率（Hz），见方案 V2
const int kVoiceSampleRate = 16000;
//Ogg Opus 的 granule 与 pre-skip 一律按 48 kHz 计（RFC 7845 第 4 节）
const int kOpusGranuleRate = 48000;
//一帧的时长（毫秒）
const int kVoiceFrameMs = 20;
//每秒的毫秒数
const int kMillisPerSecond = 1000;
//一帧在 16 kHz 下的采样数（320）
const int kVoiceSamplesPerFrame = kVoiceSampleRate * kVoiceFrameMs / kMillisPerSecond;
//一帧在 48 kHz granule 下的采样数（960）
const int kOpusSamplesPerFrame48k = kOpusGranuleRate * kVoiceFrameMs / kMillisPerSecond;
//每个 Ogg 页最多放的 Opus 包数：50 个即 1 秒音频，页头开销小，读到一半损坏时丢得也不多
const int kOggPacketsPerPage = 50;

/**
 *  写语音消息的 Ogg Opus 文件（只在内存里拼字节，写盘见 SaveBytesToFile）。
 *  使用顺序：Begin → 若干次 AddPacket → Finish；Finish 之后不能再 AddPacket，可以再次 Begin 开始一份新文件。
 *  不是线程安全的，由录音线程独占使用。
 */
class VoiceOggWriter
{
public:
    VoiceOggWriter();

    /**
     *  开始一份新文件：清空之前的内容，写入 OpusHead 页与 OpusTags 页。
     *  @param serial          Ogg 逻辑流序列号，同一文件内各页相同；取任意值，一般用随机数
     *  @param inputSampleRate 原始录音的采样率（Hz），写进 OpusHead，只作说明，不影响解码
     *  @param preSkip48k      解码器开头要丢弃的采样数（48 kHz 计），即 Opus 编码器的 lookahead × 3（16 kHz 时）
     */
    void Begin(uint32_t serial, int inputSampleRate, int preSkip48k);

    /**
     *  追加一个 Opus 包；待写的包达到 kOggPacketsPerPage 个后，再来新包时先把它们写成一页。
     *  @param data       Opus 包字节，只在本次调用内读取；为 NULL 或 len 为 0 时忽略
     *  @param len        包长度（字节）
     *  @param samples48k 这个包解码后的采样数（48 kHz 计），20 毫秒一帧时为 kOpusSamplesPerFrame48k
     */
    void AddPacket(const unsigned char* data, size_t len, int samples48k);

    /**
     *  结束文件：把还没写出的包写成最后一页并带上结束标志（没有剩余包时写一个空的结束页）。
     *  @param trimTo48k 实际有效的采样数（48 kHz 计，不含 pre-skip）；为正且小于已写入的采样数时，
     *                   最后一页的 granule 按它计算，解码时据此裁掉最后一帧里补的静音；传 0 表示不裁
     */
    void Finish(int64_t trimTo48k);

    //目前拼好的文件字节（Finish 之后即完整文件）
    const std::vector<uint8_t>& Bytes() const
    {
        return m_bytes;
    }

    //已写入的音频包个数（不含两个头包）
    int PacketCount() const
    {
        return m_packetCount;
    }

private:
    //把 m_pendingPackets 写成一页；granule 为该页结束时的位置，eos 表示是否是最后一页
    void flushPage(int64_t granule, bool eos);

    //把一个完整的包（如头包）单独写成一页
    void writeSinglePacketPage(const std::vector<uint8_t>& packet, int64_t granule, uint8_t headerType);

    //按给定的包列表拼一页并追加到 m_bytes
    void appendPage(const std::vector<std::vector<uint8_t> >& packets, int64_t granule, uint8_t headerType);

private:
    std::vector<uint8_t>               m_bytes;          //拼好的文件字节，Begin 时清空
    std::vector<std::vector<uint8_t> > m_pendingPackets; //尚未写成页的音频包，写页后清空
    uint32_t                           m_serial;         //Ogg 逻辑流序列号，Begin 时设定
    uint32_t                           m_pageSeq;        //下一页的页序号，从 0 开始，每写一页加一
    int                                m_preSkip;        //pre-skip（48 kHz 计），Begin 时设定
    int64_t                            m_granule;        //已写入音频的结束位置（48 kHz 计，含 pre-skip）
    int                                m_packetCount;    //已写入的音频包个数
    bool                               m_finished;       //是否已 Finish，Finish 后忽略 AddPacket
};

/**
 *  读语音消息的 Ogg Opus 文件：检查格式，取出 Opus 包、pre-skip 与时长。
 *  Parse 失败后各取值函数返回空列表或 0。
 */
class VoiceOggReader
{
public:
    VoiceOggReader();

    /**
     *  解析整个文件的字节。
     *  @param bytes 文件内容
     *  @return 格式合法时返回 true：每一页以 "OggS" 开头、版本为 0、页头与页体完整、校验和正确，
     *          同一逻辑流，第一个包是声道数为 1 的 OpusHead，第二个包是 OpusTags，最后一页带结束标志。
     *          否则返回 false（截断、校验和不对、不是 Opus 等）
     */
    bool Parse(const std::vector<uint8_t>& bytes);

    //音频包列表（不含两个头包），按顺序交给 Opus 解码器
    const std::vector<std::vector<uint8_t> >& Packets() const
    {
        return m_packets;
    }

    //pre-skip（48 kHz 计）：解码出的前这么多个采样（换算到解码采样率后）要丢弃
    int PreSkip48k() const
    {
        return m_preSkip;
    }

    //OpusHead 里记录的原始录音采样率（Hz）
    uint32_t InputSampleRate() const
    {
        return m_inputSampleRate;
    }

    //有效采样数（48 kHz 计）：最后一页的 granule 减去 pre-skip，不会小于 0
    int64_t TotalSamples48k() const;

    //时长（毫秒），由 TotalSamples48k 换算
    int64_t DurationMs() const;

private:
    //清空解析结果
    void reset();

private:
    std::vector<std::vector<uint8_t> > m_packets;          //音频包，Parse 成功时有效
    int                                m_preSkip;          //pre-skip（48 kHz 计）
    uint32_t                           m_inputSampleRate;  //OpusHead 里的原始采样率（Hz）
    int64_t                            m_finalGranule;     //最后一页的 granule（48 kHz 计，含 pre-skip）
};

/**
 *  计算 Ogg 页的校验和（多项式 0x04C11DB7、初值 0、不反转，RFC 3533 第 6 节）。
 *  @param data 页的全部字节，其中校验和字段（第 22~25 字节）须先置 0
 *  @param len  字节数
 *  @return 32 位校验和
 */
uint32_t OggCrc32(const uint8_t* data, size_t len);

/**
 *  把字节写进文件（先写到同目录的临时文件再改名，避免留下写了一半的文件）。
 *  @param path  目标文件的完整路径
 *  @param bytes 要写的内容
 *  @return 成功返回 true
 */
bool SaveBytesToFile(const std::wstring& path, const std::vector<uint8_t>& bytes);

/**
 *  读出整个文件。
 *  @param path  文件的完整路径
 *  @param bytes [出参] 文件内容
 *  @param maxBytes 允许的最大字节数，超过时返回 false（防止误读大文件）
 *  @return 成功返回 true；文件不存在、读失败或超过上限返回 false
 */
bool LoadBytesFromFile(const std::wstring& path, std::vector<uint8_t>& bytes, size_t maxBytes);

} // namespace voicemsg
