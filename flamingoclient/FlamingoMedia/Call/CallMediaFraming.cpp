/**
 *  通话中转媒体复用 / 分片实现，CallMediaFraming.cpp
 *  balloonwj@qq.com   2026-06-16
 */
#include "CallMediaFraming.h"

#include <string.h>

namespace call {

//body 头部固定 10 字节：[kind(1)][chunk(1)][ts(8,毫秒,小端)]。
static const size_t kHeaderBytes = 10;

//把 64 位时间戳按小端追加到缓冲。
static void putU64LE(std::vector<uint8_t>& v, uint64_t x)
{
    int i;
    for (i = 0; i < 8; ++i)
        v.push_back(static_cast<uint8_t>((x >> (i * 8)) & 0xFF));
}

//从小端字节读出 64 位时间戳（调用方保证 p 起至少 8 字节可读）。
static uint64_t getU64LE(const uint8_t* p)
{
    uint64_t x = 0;
    int i;
    for (i = 0; i < 8; ++i)
        x |= static_cast<uint64_t>(p[i]) << (i * 8);
    return x;
}

//往 body 写头部：[kind][chunk][ts]。
static void putHeader(std::vector<uint8_t>& body, CallMediaKind kind, CallMediaChunk chunk, uint64_t tsMs)
{
    body.push_back(static_cast<uint8_t>(kind));
    body.push_back(static_cast<uint8_t>(chunk));
    putU64LE(body, tsMs);
}

void BuildAudioBody(const uint8_t* data, size_t len, uint64_t tsMs, std::vector<uint8_t>& outBody)
{
    outBody.clear();
    outBody.reserve(kHeaderBytes + len);
    putHeader(outBody, call_media_kind_audio, call_media_chunk_whole, tsMs);
    if (data != NULL && len > 0)
        outBody.insert(outBody.end(), data, data + len);
}

void BuildStatsBody(const uint8_t* data, size_t len, uint64_t tsMs, std::vector<uint8_t>& outBody)
{
    outBody.clear();
    outBody.reserve(kHeaderBytes + len);
    putHeader(outBody, call_media_kind_stats, call_media_chunk_whole, tsMs);
    if (data != NULL && len > 0)
        outBody.insert(outBody.end(), data, data + len);
}

void BuildSystemAudioBody(const uint8_t* data, size_t len, uint64_t tsMs, std::vector<uint8_t>& outBody)
{
    outBody.clear();
    outBody.reserve(kHeaderBytes + len);
    putHeader(outBody, call_media_kind_system_audio, call_media_chunk_whole, tsMs);
    if (data != NULL && len > 0)
        outBody.insert(outBody.end(), data, data + len);
}

void BuildVideoBodies(const uint8_t* data, size_t len, size_t chunkSize, uint64_t tsMs,
                      std::vector<std::vector<uint8_t> >& outBodies)
{
    outBodies.clear();
    if (chunkSize == 0)
        return;

    //整帧不超过分片大小：单个 whole
    if (len <= chunkSize)
    {
        std::vector<uint8_t> body;
        body.reserve(kHeaderBytes + len);
        putHeader(body, call_media_kind_video, call_media_chunk_whole, tsMs);
        if (data != NULL && len > 0)
            body.insert(body.end(), data, data + len);
        outBodies.push_back(body);
        return;
    }

    //切片：前面若干 more，最后一片 last（各片打同一时间戳）
    size_t off = 0;
    while (off < len)
    {
        size_t take = len - off;
        if (take > chunkSize)
            take = chunkSize;
        bool isLast = (off + take >= len);

        std::vector<uint8_t> body;
        body.reserve(kHeaderBytes + take);
        putHeader(body, call_media_kind_video,
                  isLast ? call_media_chunk_last : call_media_chunk_more, tsMs);
        body.insert(body.end(), data + off, data + off + take);
        outBodies.push_back(body);

        off += take;
    }
}

CallMediaDemux::CallMediaDemux()
    : m_videoInProgress(false)
{
}

void CallMediaDemux::Reset()
{
    m_videoBuf.clear();
    m_videoInProgress = false;
}

CallMediaDemux::Result CallMediaDemux::Feed(const uint8_t* body, size_t len,
                                            std::vector<uint8_t>& out, uint64_t& outTsMs)
{
    if (body == NULL || len < kHeaderBytes)
        return call_demux_error;

    uint8_t kind = body[0];
    uint8_t chunk = body[1];
    uint64_t tsMs = getU64LE(body + 2);
    const uint8_t* payload = body + kHeaderBytes;
    size_t plen = len - kHeaderBytes;

    if (kind == call_media_kind_audio)
    {
        //音频恒为完整负载，直接交付（不动视频重组状态）
        out.assign(payload, payload + plen);
        outTsMs = tsMs;
        return call_demux_audio;
    }

    if (kind == call_media_kind_stats)
    {
        //质量报告同样恒为完整负载，直接交付（不动视频重组状态）
        out.assign(payload, payload + plen);
        outTsMs = tsMs;
        return call_demux_stats;
    }

    if (kind == call_media_kind_system_audio)
    {
        //电脑声音同样恒为完整负载，直接交付（不动视频重组状态）
        out.assign(payload, payload + plen);
        outTsMs = tsMs;
        return call_demux_system_audio;
    }

    if (kind == call_media_kind_video)
    {
        if (chunk == call_media_chunk_whole)
        {
            //完整视频帧：丢弃任何未完成的重组缓冲，直接交付本帧
            m_videoBuf.clear();
            m_videoInProgress = false;
            out.assign(payload, payload + plen);
            outTsMs = tsMs;
            return call_demux_video;
        }
        if (chunk == call_media_chunk_more)
        {
            m_videoBuf.insert(m_videoBuf.end(), payload, payload + plen);
            m_videoInProgress = true;
            return call_demux_none;
        }
        if (chunk == call_media_chunk_last)
        {
            //同帧各片时间戳相同，故完成片(last)的戳即整帧时间戳
            m_videoBuf.insert(m_videoBuf.end(), payload, payload + plen);
            out.swap(m_videoBuf);
            m_videoBuf.clear();
            m_videoInProgress = false;
            outTsMs = tsMs;
            return call_demux_video;
        }
        //非法分片标志：重置
        Reset();
        return call_demux_error;
    }

    //未知媒体类型
    return call_demux_error;
}

} // namespace call
