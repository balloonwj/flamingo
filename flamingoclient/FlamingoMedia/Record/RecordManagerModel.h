/**
 *  录制管理窗口的纯逻辑，RecordManagerModel.h
 *
 *  录制管理窗口（docs/屏幕共享与录制方案.md 第 11.6 节 F9）里不依赖窗口与系统对象的部分，全部是纯函数，便于单元测试：
 *    - 每一行的副文字：「2026-10-05 13:18 · 01:23 · 15.6 MB」，文件已不存在时为「文件已不存在」；
 *    - 每一行可做的操作：文件在时可播放、打开所在文件夹、发送、重命名、删除，文件已不存在时只能从列表移除；
 *    - 重命名的校验：保留原扩展名、去掉首尾空白、拒绝空名与 Windows 不允许的文件名、目标已存在时拒绝；
 *    - 录制完成时交给宿主的记录（MediaRecordingInfo）；
 *    - 列表里原地编辑时输入框的位置；
 *    - 语音字幕（docs/语音字幕方案.md 第 2 节第 9 条）：字幕可用时文件在的行多一项「编辑字幕」，副文字末尾写字幕状态，
 *      字幕 DLL 不可用时重命名、删除录像按草稿文件的命名规则自己处理草稿。
 *  单元测试见 Source/Tests/RecordManagerModelTests.cpp（标签 [recordmgr]）。
 *
 *  典型用法：
 *      int actions = screenrecord::RecordRowActions(exists);
 *      std::wstring sub = screenrecord::RecordRowSubText(exists, localEnd, durationMs, bytes);
 *      std::wstring newPath;
 *      if (screenrecord::ValidateRecordRename(oldPath, input, &FileExists, newPath) == screenrecord::record_rename_ok) ...
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <stdint.h>
#include <string>
#include <windows.h>
#include "../Api/FlamingoMediaApi.h"   // MediaRecordingInfo
#include "../../FlamingoCaption/CaptionApi.h"   // CaptionFileStatus：字幕状态的取值

namespace screenrecord {

//录制管理窗口里一行可做的操作（按位组合，RecordRowActions 的返回值）
enum RecordRowAction
{
    record_action_play        = 0x01,   //播放：用系统默认的播放器打开
    record_action_open_folder = 0x02,   //打开所在文件夹并选中文件
    record_action_send        = 0x04,   //发送到会话…：经宿主的选会话窗发送
    record_action_rename      = 0x08,   //重命名：在列表里原地编辑文件名
    record_action_delete      = 0x10,   //删除：确认后把文件移到回收站并删除记录
    record_action_remove      = 0x20,   //从列表移除：只删记录，文件已不存在时才有
    record_action_caption     = 0x40,   //编辑字幕：打开这个录像的字幕编辑器，文件在且字幕功能可用时才有（语音字幕）
};

/**
 *  一行可做的操作。
 *  @param fileExists       录像文件是否还在磁盘上
 *  @param captionAvailable 语音字幕当前是否可用（企业开关打开、字幕 DLL 没有加载失败）
 *  @return RecordRowAction 的按位组合：文件在时为播放、打开所在文件夹、发送、重命名、删除，字幕可用时再加编辑字幕；
 *          不在时只有从列表移除
 */
int RecordRowActions(bool fileExists, bool captionAvailable = false);

/**
 *  副文字末尾的字幕状态（语音字幕）。
 *  @param status 字幕文件状态，caption::CaptionFileStatus 的组合
 *  @return 没有任何字幕文件时为空串；只有草稿时为「有字幕」；导出过时为「已导出字幕：硬字幕版、字幕轨版、SRT」（只列导出过的）
 */
std::wstring RecordCaptionStatusText(int status);

/**
 *  录像的字幕草稿文件路径：录像完整路径后加「.caption.json」（与 FlamingoCaption 的 CaptionDraftIo::DraftPathFor 一致）。
 *  字幕 DLL 不可用时，录制管理窗口据此自己改名或删除草稿。
 *  @param videoPath 录像完整路径
 *  @return 草稿文件路径；videoPath 为空时返回空串
 */
std::wstring RecordCaptionDraftPath(const std::wstring& videoPath);

/**
 *  文件大小的文字：不满 1 KB 写「N B」，否则按 KB / MB / GB 保留一位小数（1 KB = 1024 字节），如「15.6 MB」。
 *  @param bytes 文件字节数
 */
std::wstring FormatRecordSize(uint64_t bytes);

/**
 *  一行的副文字。文件在时为「录制结束时间 · 时长 · 大小」，如「2026-10-05 13:18 · 01:23 · 15.6 MB」
 *  （时长见 FormatRecordDuration，大小见 FormatRecordSize）；文件已不存在时为「文件已不存在」。
 *  @param fileExists 录像文件是否还在磁盘上
 *  @param localEnd   录制结束的本地时间（只用年、月、日、时、分）
 *  @param durationMs 时长（毫秒）
 *  @param bytes      文件字节数
 */
std::wstring RecordRowSubText(bool fileExists, const SYSTEMTIME& localEnd, int64_t durationMs, uint64_t bytes);

/**
 *  把 Unix 毫秒（UTC）换成本地时间，供 RecordRowSubText 使用。
 *  @param utcMs Unix 毫秒
 *  @param out   [出参] 本地时间
 *  @return 换算成功返回 true；utcMs 小于 0 或系统换算失败时返回 false
 */
bool RecordUtcMsToLocal(int64_t utcMs, SYSTEMTIME& out);

/**
 *  把 FILETIME（UTC）换成 Unix 毫秒，录制完成时据此填写录制结束时间（与 RecordUtcMsToLocal 互为反向换算）。
 *  @param ft UTC 时间，通常来自 GetSystemTimeAsFileTime
 *  @return Unix 毫秒；早于 1970-01-01 时返回 0
 */
int64_t RecordFileTimeToUnixMs(const FILETIME& ft);

//ValidateRecordRename 的结果
enum RecordRenameResult
{
    record_rename_ok        = 0,   //可以改名，newPath 为新的完整路径
    record_rename_unchanged = 1,   //与原文件名完全相同，不需要改
    record_rename_empty     = 2,   //去掉首尾空白（以及用户自己写上的原扩展名）后为空
    record_rename_invalid   = 3,   //含 Windows 文件名不允许的字符（\ / : * ? " < > | 与控制字符），或是系统保留名（CON、NUL、COM1 等）
    record_rename_exists    = 4,   //目标文件已存在
    record_rename_too_long  = 5,   //新的完整路径太长，放不进 MediaRecordingInfo
};

//判断文件是否存在的函数（ValidateRecordRename 用它查目标文件；单元测试传入假的实现）
typedef bool (*RecordFileExistsFn)(const std::wstring& path);

/**
 *  校验列表里原地编辑输入的新文件名，并算出新的完整路径。原扩展名始终保留：输入里没写扩展名时补上；
 *  写了原扩展名（不区分大小写）时按原扩展名的写法替换；写了别的扩展名时它算作文件名的一部分，再补上原扩展名。
 *  只改大小写（如 a.mp4 → A.mp4）视为可以改名，不查目标文件是否存在。
 *  @param oldPath 原文件的完整路径
 *  @param input   用户输入的新文件名（可以不带扩展名）
 *  @param exists  判断目标文件是否存在的函数；为 NULL 时视为不存在
 *  @param newPath [出参] 结果为 record_rename_ok 时为新的完整路径，否则不变
 *  @return 校验结果
 */
RecordRenameResult ValidateRecordRename(const std::wstring& oldPath, const std::wstring& input, RecordFileExistsFn exists,
                                        std::wstring& newPath);

/**
 *  原地编辑时输入框里预填的文字：文件名去掉扩展名。
 *  @param path 录像文件的完整路径
 */
std::wstring RecordRenameEditText(const std::wstring& path);

/**
 *  录制完成时交给宿主 AddRecording 的记录。
 *  @param path       录像文件的完整路径
 *  @param durationMs 时长（毫秒）
 *  @param bytes      文件字节数
 *  @param endTimeMs  录制结束时间（Unix 毫秒，UTC）
 *  @param out        [出参] 填好 m_cbSize 等全部字段，m_id 为 0
 *  @return 填写成功返回 true；路径为空或长度达到 kMediaRecordingPathChars 时返回 false
 */
bool MakeRecordingInfo(const std::wstring& path, int64_t durationMs, uint64_t bytes, int64_t endTimeMs,
                       flamingomedia::MediaRecordingInfo& out);

/**
 *  列表里原地编辑时输入框的位置：盖在第 rowIndex 行的文字区域上。
 *  @param listRect   列表控件的矩形（宿主窗口客户区坐标）
 *  @param rowIndex   行号，从 0 起
 *  @param rowHeight  行高（像素），须大于 0
 *  @param scrollPos  列表当前的滚动位置（像素）
 *  @param textLeft   行内文字相对列表左边的起点（像素）
 *  @param rightInset 输入框右边与列表右边的距离（像素，给悬浮滚动条让位）
 *  @param out        [出参] 输入框的矩形
 *  @return 这一行完整落在列表可见范围内时返回 true；滚出（哪怕只露出一部分）或参数非法时返回 false，此时不显示输入框
 */
bool RecordInlineEditRect(const RECT& listRect, int rowIndex, int rowHeight, int scrollPos, int textLeft, int rightInset,
                          RECT& out);

} // namespace screenrecord
