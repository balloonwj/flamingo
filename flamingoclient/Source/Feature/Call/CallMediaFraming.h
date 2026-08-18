/**
 *  通话中转媒体复用 / 分片（纯逻辑），CallMediaFraming.h
 *
 *  第三期：音频与视频要在同一条 imgserver 中转连接上并行收发，故给每个中转数据帧
 *  （cmd_av_relay_data 的包体）加一层小封装来区分音 / 视频，并把超过中转单帧上限
 *  （MAX_BODY_FILESERVER=2MiB）的大视频帧（H.264 关键帧可能较大）切片、接收端重组。
 *
 *  中转 body 格式（即 cmd_av_relay_data 帧包体；imgserver 不解析、原样转发）：
 *    [1 字节 媒体类型 CallMediaKind][1 字节 分片标志 CallMediaChunk][8 字节 时间戳(毫秒,小端)][负载...]
 *  时间戳是「会话相对毫秒」，发端为音 / 视频帧打同一时间轴的戳，接收端据此做 A/V 同步呈现。
 *  音频包恒为「whole」单帧（Opus 帧小）；视频帧 ≤ 分片大小时为单个「whole」，否则切成
 *  「more...more,last」多帧（同一帧的各分片打同一时间戳）。接收端按媒体类型分流：音频直接
 *  交付，视频按 whole/more/last 重组成整帧再交付，并带出该帧的时间戳（取完成片的戳，因同帧
 *  各片戳相同）。音 / 视频来自不同线程、可在连接上交错，但分流后各用各的状态，互不影响；
 *  视频帧之间不交错（视频单线程顺序发），故重组依赖 TCP 有序即可正确。
 *
 *  纯逻辑，不碰 socket / 设备，便于单元测试（CallMediaFramingTests.cpp）。
 *
 *  balloonwj@qq.com   2026-06-16
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <vector>

namespace call {

//中转 body 首字节：媒体类型。
enum CallMediaKind
{
    call_media_kind_audio = 1,   //音频负载（Opus 包）
    call_media_kind_video = 2,   //视频负载（H.264 帧或其分片）
};

//中转 body 次字节：分片标志（语义同远程桌面的 chunk 标志）。
enum CallMediaChunk
{
    call_media_chunk_whole = 0,   //完整负载：标志后即整个负载
    call_media_chunk_more  = 1,   //分片且后续还有
    call_media_chunk_last  = 2,   //分片且为末片
};

/**
 *  把一个音频负载（Opus 包）封成一个中转 body：[audio][whole][ts][opus]。
 *  @param data    Opus 包字节起始
 *  @param len     字节数
 *  @param tsMs    会话相对时间戳（毫秒），供接收端 A/V 同步
 *  @param outBody [出参] 写入封装结果（会先清空）
 */
void BuildAudioBody(const uint8_t* data, size_t len, uint64_t tsMs, std::vector<uint8_t>& outBody);

/**
 *  把一个视频帧（H.264）按 chunkSize 切成若干中转 body，各片打同一时间戳。
 *  len ≤ chunkSize 产出单个 [video][whole][ts]；否则产出 [video][more][ts]... [video][last][ts] 多个。
 *  @param data      视频帧字节起始（len 为 0 时产出一个空 whole）
 *  @param len       视频帧字节数
 *  @param chunkSize 单个 body 的负载字节上限（须 > 0；典型 512 KiB，须使 body 不超 2MiB）
 *  @param tsMs      会话相对时间戳（毫秒），同一帧各分片相同
 *  @param outBodies [出参] 每项是一个中转 body，调用方须按序逐个发出（会先清空）
 */
void BuildVideoBodies(const uint8_t* data, size_t len, size_t chunkSize, uint64_t tsMs,
                      std::vector<std::vector<uint8_t> >& outBodies);

//接收端解复用 + 视频重组：逐个喂入中转 body，分流出完整的音频 / 视频负载。
class CallMediaDemux
{
public:
    CallMediaDemux();

    //Feed 的返回码。
    enum Result
    {
        call_demux_none  = 0,   //已消费但未构成完整负载（视频分片累积中）
        call_demux_audio = 1,   //得到一个完整音频负载（out 有效）
        call_demux_video = 2,   //得到一个完整视频帧（out 有效）
        call_demux_error = -1,  //body 非法（过短 / 标志非法），已重置视频重组状态
    };

    /**
     *  喂入一个中转 body（cmd_av_relay_data 包体）。
     *  @param body    包体起始（[kind][chunk][ts][payload]）
     *  @param len     包体字节数
     *  @param out     [出参] 构成完整音频/视频负载时填入（音频=Opus 包；视频=完整 H.264 帧）
     *  @param outTsMs [出参] 返回 audio/video 时填入该负载的会话相对时间戳（毫秒）；none/error 时不保证有效
     *  @return 见 enum Result
     */
    Result Feed(const uint8_t* body, size_t len, std::vector<uint8_t>& out, uint64_t& outTsMs);

    //清空视频重组状态（连接建立 / 异常时调用）。
    void Reset();

private:
    std::vector<uint8_t> m_videoBuf;        //视频分片重组缓冲
    bool                 m_videoInProgress; //是否正在重组一个被分片的视频帧
};

} // namespace call
