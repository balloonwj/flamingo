/**
 *  给 MP4 文件追加一条 tx3g（3GPP Timed Text）字幕轨（docs/语音字幕方案.md 第 8 节「字幕轨版」、14.5）。
 *
 *  Media Foundation 的 MP4 写入不支持字幕流（0d 实测），所以字幕轨版的做法是：先把录像原样转封装成普通 MP4，
 *  再由本文件在文件末尾追加一块 mdat（全部文字样本）并写一个带新文字轨的 moov：
 *    - 原 moov 在文件末尾：截掉它，依次写文字样本的 mdat 与新 moov；
 *    - 原 moov 不在末尾：把原 moov 的盒子类型改成 free（大小不变，相当于作废），再在末尾写 mdat 与新 moov。
 *  两种情况下原有的音视频数据都不移动，原轨道的块偏移不用修改。新 moov = 原 moov 的全部子盒子 + 新文字轨，
 *  mvhd 的 next_track_ID 随之更新。
 *
 *  文字轨的结构与 ffmpeg 写 mov_text 时相同：handler 为 sbtl、媒体头为 nmhd、样本描述为 tx3g（居中靠下、白字、
 *  字体表一项），每个样本为「2 字节大端长度 + UTF-8 文字」，长度为 0 的样本表示这段时间不显示字幕；样本首尾相接
 *  覆盖整段时长（见 CaptionTimedText.h 的 BuildTextSamples），一块一个样本。
 *
 *  只依赖 Win32 文件接口，不依赖 Media Foundation。单测见 Tests/CaptionExportTests.cpp（CAPEX4～CAPEX6）。
 *
 *  典型用法：
 *      std::vector<caption::Mp4TextSample> samples = ...;
 *      std::string error;
 *      if (!caption::AppendTx3gTrack(path, samples, 1920, 1080, "chi", &error)) ...
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

namespace caption {

//一个文字样本
struct Mp4TextSample
{
    int64_t     m_durationMs;   //时长（毫秒），须大于 0
    std::string m_utf8;         //文字（UTF-8，不含 BOM）；为空表示这段时间不显示字幕

    Mp4TextSample()
        : m_durationMs(0)
    {
    }
};

//单个文字样本的最大字节数（2 字节长度字段能表示的上限）
const size_t kMp4TextSampleMaxBytes = 0xFFFF;

/**
 *  给 MP4 文件追加一条 tx3g 字幕轨（就地修改文件）。
 *  @param path      MP4 文件路径（须可读写）
 *  @param samples   文字样本，首尾相接；不能为空
 *  @param widthPx   字幕轨的显示宽（像素，一般取视频宽）
 *  @param heightPx  字幕轨的显示高（像素，一般取视频高）
 *  @param language  语言代码（ISO 639-2，三个小写字母，如 "chi"）；为 NULL 或不合法时写 "und"
 *  @param error     [出参，可为 NULL] 失败原因（英文，写日志用）
 *  @return 成功返回 true；文件不是能识别的 MP4（找不到 moov / mvhd、盒子大小不对）、样本为空或过长、读写失败时返回 false
 *          （读写中途失败时文件可能已损坏，调用方应在副本上操作）
 */
bool AppendTx3gTrack(const std::wstring& path, const std::vector<Mp4TextSample>& samples, uint32_t widthPx,
                     uint32_t heightPx, const char* language, std::string* error);

/**
 *  读出 MP4 文件里第一条 tx3g 字幕轨的全部样本（单测与导出后的自检用）。
 *  @param path    MP4 文件路径
 *  @param samples [出参] 样本，时长按毫秒换算（媒体时间刻度不是 1000 时四舍五入）
 *  @param error   [出参，可为 NULL] 失败原因
 *  @return 找到字幕轨并读出全部样本时返回 true
 */
bool ReadTx3gTrack(const std::wstring& path, std::vector<Mp4TextSample>& samples, std::string* error);

/**
 *  MP4 文件的顶层盒子类型序列（单测核对文件结构用），如 {"ftyp", "free", "mdat", "mdat", "moov"}。
 *  @param path MP4 文件路径
 *  @return 顶层盒子类型；读不出时为空
 */
std::vector<std::string> ListMp4TopLevelBoxes(const std::wstring& path);

} // namespace caption
