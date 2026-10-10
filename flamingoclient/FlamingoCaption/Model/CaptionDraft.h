/**
 *  语音字幕的样式、草稿文件与 SRT 读写（docs/语音字幕方案.md 6.1、6.2、第 7、8 节），CaptionDraft.h
 *
 *  - CaptionStyle：字幕的外观，尺寸都按 1080 像素高的画面计，画在其他高度的画面上时按比例缩放；
 *  - CaptionDraft：录像旁的草稿「原名.mp4.caption.json」，保存字幕、样式与画中画的位置（排版时避开它）；
 *  - CaptionDraftIo：草稿的 JSON 序列化与存盘（先写临时文件再替换，写到一半时进程退出不会损坏原草稿）；
 *  - CaptionSrt：SRT 的写出与读入。
 *
 *  典型用法：
 *      caption::CaptionDraft d;
 *      d.m_cues = track.Cues();
 *      caption::CaptionDraftIo::SaveFile(caption::CaptionDraftIo::DraftPathFor(videoPath), d);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "CaptionCue.h"

namespace caption {

//草稿文件的格式版本；读到更高的版本时拒绝读取，避免老版本程序改坏新格式
const int kCaptionDraftVersion = 1;
//样式尺寸的基准画面高度（像素）
const int kCaptionStyleBaseHeight = 1080;

//字幕外观
struct CaptionStyle
{
    CaptionStyle();

    int      m_fontPx;          //字号（像素，按 1080 高的画面计）
    int      m_bottomMarginPx;  //字幕底边到画面底边的距离（像素，按 1080 高的画面计）
    int      m_outlinePx;       //描边宽度（像素，按 1080 高的画面计），0 表示不描边
    int      m_boxAlpha;        //半透明底框的不透明度（0～255），0 表示不画底框
    bool     m_showSpeaker;     //是否在第一行前面显示「说话人：」
    uint32_t m_textColor;       //文字颜色，0xRRGGBB
    uint32_t m_outlineColor;    //描边与底框颜色，0xRRGGBB
};

//画中画（摄像头小窗）在画面里的位置，归一化到 0～1；排版字幕时避开它
struct CaptionPip
{
    CaptionPip();

    bool   m_present;   //录像里是否有画中画
    double m_left;      //左边（0～1）
    double m_top;       //上边（0～1）
    double m_right;     //右边（0～1）
    double m_bottom;    //下边（0～1）
};

//导出文件的种类
enum CaptionExportKind
{
    caption_export_hard  = 1,   //硬字幕版：字幕画进画面、重新编码的 MP4
    caption_export_track = 2,   //字幕轨版：原样拷贝画面与声音、另加一条字幕轨的 MP4
    caption_export_srt   = 3,   //外挂字幕 SRT 文件
    caption_export_text  = 4,   //纯文本（14.13）：每条字幕一行，带开始时刻；录音与录像都可以导出
};

//一条导出记录：导出完成后写进草稿，状态查询据此判断（文件名随界面语言不同，不能靠后缀判断）
struct CaptionExport
{
    CaptionExport();

    std::wstring m_name;   //导出文件的文件名（不含目录，与录像在同一目录）
    int          m_kind;   //种类，取值见 CaptionExportKind
};

//一份字幕草稿
struct CaptionDraft
{
    CaptionDraft();

    int                     m_version;          //格式版本，见 kCaptionDraftVersion
    std::wstring            m_videoName;        //对应的录像文件名（不含目录），用于核对草稿是否属于该录像
    int64_t                 m_videoBytes;       //录像文件的字节数（录制中为 0）
    int64_t                 m_videoDurationMs;  //录像时长（毫秒，录制中为 0）
    CaptionStyle            m_style;            //字幕外观
    CaptionPip              m_pip;              //画中画位置
    std::vector<CaptionCue> m_cues;             //全部字幕，按开始时刻排序
    int64_t                 m_nextId;           //字幕轨的下一个编号（CaptionTrack::NextId）
    std::vector<CaptionExport> m_exports;       //导出记录，同一种类同一文件名只记一条
};

/**
 *  草稿的读写。全部为静态方法。
 */
class CaptionDraftIo
{
public:
    //序列化为 UTF-8 JSON（带缩进）
    static std::string ToJson(const CaptionDraft& draft);

    /**
     *  从 JSON 恢复。字段缺失时取默认值；单条字幕的起止时刻不合法时跳过该条。
     *  @param error [出参，可为 NULL] 失败原因（英文，写日志用）
     *  @return 不是 JSON 对象、或版本高于 kCaptionDraftVersion 时返回 false
     */
    static bool FromJson(const std::string& json, CaptionDraft& draft, std::string* error);

    //录像对应的草稿路径：录像完整路径后面加「.caption.json」
    static std::wstring DraftPathFor(const std::wstring& videoPath);

    /**
     *  写草稿：先写「路径.tmp」，再替换为正式文件。
     *  @return 写入或替换失败返回 false（原草稿不受影响）
     */
    static bool SaveFile(const std::wstring& path, const CaptionDraft& draft);

    /**
     *  读草稿。
     *  @return 文件不存在、读不出或内容不对时返回 false，原因写入 error（可为 NULL）
     */
    static bool LoadFile(const std::wstring& path, CaptionDraft& draft, std::string* error);
};

/**
 *  SRT 读写。全部为静态方法。
 */
class CaptionSrt
{
public:
    /**
     *  写成 SRT 文本：UTF-8 带 BOM，CRLF 换行，序号从 1 开始，按开始时刻排序。
     *  @param withSpeaker 为 true 时说话人非空的字幕写成「说话人：文字」
     */
    static std::string Write(const std::vector<CaptionCue>& cues, bool withSpeaker);

    /**
     *  读 SRT 文本（UTF-8，可带 BOM，CRLF 或 LF）。序号行可有可无；时间行之后的多行文字以换行连接；
     *  格式不对的段落跳过。读出的字幕来源为 caption_source_manual，编号为 0，说话人为空（前缀保留在文字里）。
     *  @param skipped [出参，可为 NULL] 跳过的段落数
     *  @return 至少读出一条字幕时返回 true
     */
    static bool Parse(const std::string& utf8, std::vector<CaptionCue>& out, int* skipped);
};

/**
 *  纯文本导出（docs/语音字幕方案.md 14.13：录音也能导出文字，便于整理成会议记录）。全部为静态方法。
 */
class CaptionPlainText
{
public:
    /**
     *  写成纯文本：UTF-8 带 BOM，CRLF 换行，按开始时刻排序，每条字幕一行「[时:分:秒] 文字」；
     *  withSpeaker 为 true 且说话人非空时写成「[时:分:秒] 说话人：文字」。字幕里的换行换成空格，文字为空的跳过。
     *  @param cues        字幕
     *  @param withSpeaker 是否带说话人（取字幕外观里的「显示说话人」）
     */
    static std::string Write(const std::vector<CaptionCue>& cues, bool withSpeaker);
};

} // namespace caption
