/**
 *  录制文件的写入，RecordMp4Writer.h
 *
 *  用 Media Foundation 的 Sink Writer 写分段式 MP4（docs/屏幕共享与录制方案.md 6.1）：视频输入为自上而下的 BGRA，
 *  编成 H.264（优先硬件编码器）；声音输入为 48 kHz 双声道 int16，编成 AAC。分段式的好处是客户端中途崩溃时已经写下的
 *  部分仍能播放（第 0 期 0d 实测：写到 30 秒时强制结束进程，仍能读到 29.67 秒，普通 MP4 整个文件打不开）。
 *
 *  只录声音（第 24 期，docs/屏幕共享与录制方案.md 第 12.5 节）：OpenAudioOnly 只建 AAC 音频流，容器同样是分段式 MP4，
 *  文件扩展名由调用方定为 .m4a（24a 实测：只含音频的分段式 MP4 正常收尾时时长正确，强制结束进程后已写部分仍可读出）。
 *
 *  线程：全部方法都在同一条线程上调用，该线程须已初始化 COM 与 Media Foundation。
 *
 *  典型用法：
 *      writer.Open(path, 1920, 1080, 30, bitrate);     //或 writer.OpenAudioOnly(path);
 *      writer.WriteVideo(bgra, stride, pts, duration);  //只录声音时不调用
 *      writer.WriteAudio(stereo, frames, pts);
 *      writer.Close();
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <string>

struct IMFSinkWriter;

namespace screenrecord {

//MP4 写入
class RecordMp4Writer
{
public:
    RecordMp4Writer();
    ~RecordMp4Writer();

    RecordMp4Writer(const RecordMp4Writer&) = delete;
    RecordMp4Writer& operator=(const RecordMp4Writer&) = delete;

    /**
     *  创建文件并开始写入。
     *  @param path    文件路径（已存在则覆盖）
     *  @param width、height 视频尺寸（偶数）
     *  @param fps     帧率
     *  @param bitrate 视频码率（bit/s）
     *  @return 成功返回 true；失败时记日志并返回 false（文件可能已创建，调用方自行删除）
     */
    bool Open(const std::wstring& path, int width, int height, int fps, uint32_t bitrate);

    /**
     *  只录声音（第 24 期）：创建文件并开始写入，只有 48 kHz 双声道的 AAC 音频流，没有视频流。
     *  @param path 文件路径（已存在则覆盖），调用方以 .m4a 结尾
     *  @return 成功返回 true；失败时记日志并返回 false（文件可能已创建，调用方自行删除）
     */
    bool OpenAudioOnly(const std::wstring& path);

    //文件里有没有视频流（Open 打开的有，OpenAudioOnly 打开的没有；没有打开时为 false）
    bool HasVideo() const;

    /**
     *  写一帧画面（只录声音时返回 false）。
     *  @param bgra     自上而下的 BGRA，尺寸为 Open 时的宽高
     *  @param stride   行跨度（字节）
     *  @param ptsHns   显示时刻（100 纳秒单位，从 0 起）
     *  @param durHns   时长（100 纳秒单位）
     */
    bool WriteVideo(const uint8_t* bgra, int stride, int64_t ptsHns, int64_t durHns);

    /**
     *  写一段声音。
     *  @param stereo 交错双声道 int16，48 kHz
     *  @param frames 采样帧数（每帧左右各一个采样）
     *  @param ptsHns 开始时刻（100 纳秒单位）
     */
    bool WriteAudio(const int16_t* stereo, int frames, int64_t ptsHns);

    //结束写入并关闭文件（写完 MP4 的收尾信息）；可重复调用
    bool Close();

    bool IsOpen() const;

private:
    /**
     *  建 Sink Writer 并加上 AAC 音频流（两种打开方式共用）。
     *  @param path   文件路径
     *  @param withVideo 是否先加 H.264 视频流（随后的四个参数只在为 true 时使用）
     *  @return 成功返回 true，失败时记日志
     */
    bool openInternal(const std::wstring& path, bool withVideo, int width, int height, int fps, uint32_t bitrate);

    IMFSinkWriter* m_writer;       //Sink Writer；打开时非空
    unsigned long  m_videoStream;  //视频流序号；没有视频流时无意义
    unsigned long  m_audioStream;  //声音流序号
    int            m_width;        //视频宽；没有视频流时为 0
    int            m_height;       //视频高；没有视频流时为 0
    bool           m_hasVideo;     //是否建了视频流（只录声音时为 false）
};

} // namespace screenrecord
