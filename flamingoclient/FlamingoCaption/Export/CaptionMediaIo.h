/**
 *  导出与预览用到的 Media Foundation 处理（docs/语音字幕方案.md 第 8 节、14.4、14.7），CaptionMediaIo.h
 *
 *    - ProbeVideo：读录像的时长、画面尺寸、帧率与码率；
 *    - RemuxMp4：原样转封装（不重新编码）。Source Reader 以原始压缩格式读出 H.264 与 AAC 样本，Sink Writer 原样写成
 *      普通 MP4。用途有二：字幕编辑器预览分段式录像前先转一份（分段式 MP4 上 IMFMediaEngine 的跳转有问题，14.7），
 *      字幕轨版导出时先转出普通 MP4 再追加字幕轨（14.5）；
 *    - BurnHardSub：生成硬字幕版。Source Reader 解码为 RGB32，字幕变化时才把字幕画进缓存图层、每帧只做叠加（14.4 实测
 *      这样比每帧重画快约一半），Sink Writer 重新编码 H.264（码率为原码率的 1.2 倍），声音的 AAC 样本原样拷贝；
 *      所有流都读到结尾才收尾（14.4 中试验程序在视频结束时就退出，丢了最后两个声音包）。
 *  三个函数都是同步的，在调用线程上运行（导出线程或预览准备线程），调用方须已初始化 COM（多线程套间）与
 *  Media Foundation（MFStartup）。耗时操作经 ICaptionMediaProgress 报进度、查询是否取消。
 *
 *  单测见 Tests/CaptionExportTests.cpp（CAPEX7～CAPEX9），用测试里现场生成的小段 H.264 + AAC 录像。
 *
 *  典型用法：
 *      caption::CaptionVideoInfo info;
 *      if (caption::ProbeVideo(path, info, &error)) ...
 *      caption::RemuxMp4(src, dst, &progress, &error);
 *      caption::BurnHardSub(src, dst, spans, style, pip, &progress, &error);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "CaptionTimedText.h"
#include "../Model/CaptionDraft.h"

namespace caption {

//录像的基本信息
struct CaptionVideoInfo
{
    int64_t  m_durationMs;   //时长（毫秒）
    uint32_t m_width;        //画面宽（像素）
    uint32_t m_height;       //画面高（像素）
    uint32_t m_fpsNum;       //帧率分子
    uint32_t m_fpsDen;       //帧率分母
    uint32_t m_bitrate;      //视频码率（比特/秒）；文件里没有记录时为 0
    bool     m_hasAudio;     //是否有声音流
    bool     m_hasVideo;     //是否有视频流（只录声音的录音没有，见 docs/语音字幕方案.md 14.13）

    CaptionVideoInfo()
        : m_durationMs(0)
        , m_width(0)
        , m_height(0)
        , m_fpsNum(0)
        , m_fpsDen(0)
        , m_bitrate(0)
        , m_hasAudio(false)
        , m_hasVideo(false)
    {
    }
};

/**
 *  耗时处理的进度与取消。实现方须线程安全（在处理线程上被调用）。
 */
class ICaptionMediaProgress
{
public:
    /**
     *  进度（处理线程上调用，约每处理 1% 调用一次）。
     *  @param permille 已完成的千分比（0～1000）
     */
    virtual void OnMediaProgress(int permille) = 0;

    /**
     *  是否已要求取消（处理线程上每处理一个样本查询一次）。
     *  @return 已要求取消时返回 true，处理随即停止并返回失败（不完整的输出文件由调用方删除）
     */
    virtual bool IsMediaCancelled() = 0;

protected:
    ~ICaptionMediaProgress()
    {
    }
};

/**
 *  读录像的基本信息。
 *  @param path  录像路径
 *  @param info  [出参] 基本信息
 *  @param error [出参，可为 NULL] 失败原因（英文）
 *  @return 能打开、且有视频流时返回 true
 */
bool ProbeVideo(const std::wstring& path, CaptionVideoInfo& info, std::string* error);

/**
 *  原样转封装成普通 MP4（不重新编码）。目标文件已存在时覆盖。只有音频流的录音（.m4a，屏幕共享方案第 24 期）同样可以转封装，
 *  结果里只有音频流（字幕编辑器据此把分段式的录音转成可以随意跳转的普通 MP4 来预览）。
 *  @param src      源录像路径
 *  @param dst      目标路径
 *  @param progress 进度与取消，可为 NULL
 *  @param error    [出参，可为 NULL] 失败原因
 *  @return 成功返回 true；取消或失败时返回 false（目标文件可能不完整，由调用方删除）
 */
bool RemuxMp4(const std::wstring& src, const std::wstring& dst, ICaptionMediaProgress* progress, std::string* error);

/**
 *  生成硬字幕版。目标文件已存在时覆盖。
 *  @param src      源录像路径
 *  @param dst      目标路径
 *  @param spans    要画的字幕（BuildDisplaySpans 的结果）
 *  @param style    字幕外观
 *  @param pip      画中画位置（排版时避开它）
 *  @param progress 进度与取消，可为 NULL
 *  @param error    [出参，可为 NULL] 失败原因
 *  @return 成功返回 true；取消或失败时返回 false（目标文件可能不完整，由调用方删除）
 */
bool BurnHardSub(const std::wstring& src, const std::wstring& dst, const std::vector<CaptionSpan>& spans,
                 const CaptionStyle& style, const CaptionPip& pip, ICaptionMediaProgress* progress, std::string* error);

} // namespace caption
