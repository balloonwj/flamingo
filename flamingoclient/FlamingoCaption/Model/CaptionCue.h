/**
 *  语音字幕的一条字幕（docs/语音字幕方案.md 6.2），CaptionCue.h
 *
 *  一条字幕由起止时刻、说话人、文字和来源组成。时刻以毫秒计，起点是录像的第 0 毫秒（录制时间轴，暂停的时段
 *  不计入）。文字用 UTF-16 的 std::wstring 保存，与编辑器界面直接对接；写文件时再转成 UTF-8。
 *
 *  典型用法：
 *      caption::CaptionCue cue;
 *      cue.m_startMs = 1200;
 *      cue.m_endMs = 3400;
 *      cue.m_text = L"...";
 *      track.Add(cue);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <string>

namespace caption {

//字幕的来源
enum CaptionSource
{
    caption_source_mic        = 0,   //本人麦克风：录制或共享时在本机识别
    caption_source_others     = 1,   //房间里其他人的发言：共享时由对方识别、经字幕控制帧收到
    caption_source_system     = 2,   //电脑声音：录制时勾了「生成字幕」且在录电脑声音（2026-10-06 起与麦克风一起识别）
    caption_source_postrec    = 3,   //事后识别：在编辑器里对整段录像重新识别得到
    caption_source_manual     = 4,   //手工添加：在编辑器里插入的空白字幕
    caption_source_count      = 5,   //来源的个数（不是一种来源），用于校验取值范围
};

//一条字幕
struct CaptionCue
{
    CaptionCue();

    int64_t      m_id;        //字幕编号：在所属字幕轨里唯一，由 CaptionTrack 分配；0 表示尚未分配
    int64_t      m_startMs;   //开始时刻（毫秒，录像时间轴）
    int64_t      m_endMs;     //结束时刻（毫秒，录像时间轴），大于 m_startMs
    std::wstring m_speaker;   //说话人名字；为空表示不显示说话人前缀
    std::wstring m_text;      //字幕文字，可含一个换行（两行字幕）
    int          m_source;    //来源，取值见 CaptionSource
    bool         m_edited;    //是否被人工修改过（文字、时间或说话人），编辑器据此标记
};

} // namespace caption
