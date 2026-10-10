/**
 *  录像对应的字幕文件：状态查询、导出文件命名、改名与删除（docs/语音字幕方案.md 第 2 节第 9 条、第 8 节），CaptionFiles.h
 *
 *  录制管理窗口要显示每个录像的字幕状态，并在用户改名、删除录像时同步处理草稿。字幕文件包括：
 *    - 草稿「原名.mp4.caption.json」；
 *    - 导出的文件（硬字幕版、字幕轨版、SRT），与录像在同一目录。导出文件名里的「字幕」等字样随界面语言翻译，
 *      所以状态不靠文件名后缀判断，而是读草稿里的导出记录（CaptionDraft::m_exports）并检查文件是否还在。
 *
 *  全部为纯函数，只做文件系统操作，任意线程可调用。
 *
 *  典型用法：
 *      int status = caption::QueryCaptionFileStatus(videoPath);
 *      if (status & caption::kCaptionFileDraft) { ... }
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <string>

namespace caption {

//字幕文件状态的各个位，取值与 CaptionApi.h 的 CaptionFileStatus 相同
const int kCaptionFileNone  = 0x00;   //没有任何字幕文件
const int kCaptionFileDraft = 0x01;   //有草稿
const int kCaptionFileHard  = 0x02;   //导出过硬字幕版，且文件还在
const int kCaptionFileTrack = 0x04;   //导出过字幕轨版，且文件还在
const int kCaptionFileSrt   = 0x08;   //导出过 SRT，且文件还在

/**
 *  录像的字幕文件状态。草稿读不出来时只报 kCaptionFileDraft。
 *  @param videoPath 录像的完整路径
 *  @return kCaptionFile* 的组合
 */
int QueryCaptionFileStatus(const std::wstring& videoPath);

/**
 *  导出文件的路径：与录像同一目录，文件名为「录像主文件名 + suffix + ext」。
 *  @param videoPath 录像完整路径
 *  @param suffix    加在主文件名后的字样（如翻译后的「-字幕」），可为空
 *  @param ext       扩展名，含点号（如 L".mp4"）
 */
std::wstring MakeExportPath(const std::wstring& videoPath, const std::wstring& suffix, const std::wstring& ext);

/**
 *  是不是只录声音的录音（14.13，屏幕共享方案第 24 期）：按扩展名 .m4a 判断，不区分大小写。导出窗口据此只提供
 *  SRT 与纯文本（硬字幕版与字幕轨版都要有画面）。
 *  @param mediaPath 录像或录音的路径
 */
bool IsAudioOnlyMediaPath(const std::wstring& mediaPath);

/**
 *  录像改名后把草稿一并改名，并把草稿里记录的录像文件名改成新名字。没有草稿时什么都不做。
 *  @return 有草稿且改名成功返回 true
 */
bool RenameCaptionDraft(const std::wstring& oldVideoPath, const std::wstring& newVideoPath);

/**
 *  录像删除后删除它的草稿（导出的文件是独立的录像，不删）。
 *  @return 草稿存在且已删除返回 true
 */
bool DeleteCaptionDraft(const std::wstring& videoPath);

//取路径里的文件名部分（最后一个反斜杠或斜杠之后）
std::wstring FileNameOf(const std::wstring& path);

//取路径里的目录部分（含结尾的分隔符）；没有目录时返回空串
std::wstring DirectoryOf(const std::wstring& path);

} // namespace caption
