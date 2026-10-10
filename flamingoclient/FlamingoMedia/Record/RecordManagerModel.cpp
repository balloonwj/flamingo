/**
 *  录制管理窗口的纯逻辑，见 RecordManagerModel.h。
 *  balloonwj@qq.com   2026-10-05
 */
#include "RecordManagerModel.h"

#include <stdio.h>
#include <cwctype>
#include <string.h>
#include "RecordPolicy.h"   // FormatRecordDuration、RecordFileNameOf
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace screenrecord {

namespace {

//1 KB、1 MB、1 GB 的字节数
const uint64_t kBytesPerKb = 1024ULL;
const uint64_t kBytesPerMb = 1024ULL * 1024ULL;
const uint64_t kBytesPerGb = 1024ULL * 1024ULL * 1024ULL;
//格式化缓冲区的容量（字符）
const int kTextChars = 64;
//FILETIME 以 1601-01-01 起的 100 纳秒为单位；Unix 纪元（1970-01-01）与它相差的 100 纳秒数
const int64_t kUnixEpochIn100ns = 116444736000000000LL;
//1 毫秒等于多少个 100 纳秒
const int64_t k100nsPerMs = 10000;
//把 64 位的 FILETIME 拆成高低两个 32 位时用的掩码与移位
const int64_t kLow32Mask = 0xFFFFFFFFLL;
const int kHigh32Shift = 32;
//副文字里各段之间的分隔符
const wchar_t* const kSubTextSep = L" · ";

//Windows 的系统保留文件名（不区分大小写，带不带扩展名都不能用）
const wchar_t* const kReservedNames[] = {
    L"CON", L"PRN", L"AUX", L"NUL",
    L"COM1", L"COM2", L"COM3", L"COM4", L"COM5", L"COM6", L"COM7", L"COM8", L"COM9",
    L"LPT1", L"LPT2", L"LPT3", L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8", L"LPT9"
};

//两段文字是否相同（不区分大小写，按序号比较，与 NTFS 文件名的比较方式一致）
bool EqualsNoCase(const std::wstring& a, const std::wstring& b)
{
    if (a.size() != b.size())
        return false;
    if (a.empty())
        return true;
    return ::CompareStringOrdinal(a.c_str(), (int)a.size(), b.c_str(), (int)b.size(), TRUE) == CSTR_EQUAL;
}

//去掉首尾空白
std::wstring TrimSpaces(const std::wstring& s)
{
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && ::iswspace(s[begin]))
        ++begin;
    while (end > begin && ::iswspace(s[end - 1]))
        --end;
    return s.substr(begin, end - begin);
}

//文件名的扩展名（含点，如 ".mp4"）；没有扩展名或点在第一个字符（如 ".profile"）时为空
std::wstring ExtensionOf(const std::wstring& fileName)
{
    const std::wstring::size_type dot = fileName.find_last_of(L'.');
    if (dot == std::wstring::npos || dot == 0)
        return std::wstring();
    return fileName.substr(dot);
}

//文件名里有没有 Windows 不允许的字符（\ / : * ? " < > | 与控制字符）
bool HasInvalidNameChar(const std::wstring& name)
{
    //控制字符的上界：码值小于它的都不允许出现在文件名里
    const wchar_t kFirstPrintable = 0x20;
    for (size_t i = 0; i < name.size(); ++i)
    {
        const wchar_t c = name[i];
        if (c < kFirstPrintable || ::wcschr(L"\\/:*?\"<>|", c) != NULL)
            return true;
    }
    return false;
}

//文件名（不含扩展名部分，取第一个点之前）是不是系统保留名
bool IsReservedName(const std::wstring& name)
{
    const std::wstring base = name.substr(0, name.find(L'.'));
    for (size_t i = 0; i < sizeof(kReservedNames) / sizeof(kReservedNames[0]); ++i)
    {
        if (EqualsNoCase(base, kReservedNames[i]))
            return true;
    }
    return false;
}

} // namespace

int RecordRowActions(bool fileExists, bool captionAvailable)
{
    if (!fileExists)
        return record_action_remove;
    int actions = record_action_play | record_action_open_folder | record_action_send | record_action_rename |
                  record_action_delete;
    if (captionAvailable)
        actions |= record_action_caption;
    return actions;
}

std::wstring RecordCaptionStatusText(int status)
{
    //导出过的几种，按「硬字幕版、字幕轨版、SRT」的顺序列出
    std::wstring exported;
    const int kinds[] = { caption::caption_file_hard, caption::caption_file_track, caption::caption_file_srt };
    const wchar_t* const names[] = { TR_NOOP(L"硬字幕版"), TR_NOOP(L"字幕轨版"), TR_NOOP(L"SRT") };
    for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i)
    {
        if ((status & kinds[i]) == 0)
            continue;
        if (!exported.empty())
            exported += TR(L"、");
        exported += TR(names[i]);
    }
    if (!exported.empty())
        return TRF(L"已导出字幕：{0}", exported);
    if ((status & caption::caption_file_draft) != 0)
        return TR(L"有字幕");
    return std::wstring();
}

std::wstring RecordCaptionDraftPath(const std::wstring& videoPath)
{
    if (videoPath.empty())
        return std::wstring();
    return videoPath + L".caption.json";
}

std::wstring FormatRecordSize(uint64_t bytes)
{
    wchar_t buf[kTextChars] = { 0 };
    if (bytes < kBytesPerKb)
        _snwprintf_s(buf, kTextChars, _TRUNCATE, L"%llu B", static_cast<unsigned long long>(bytes));
    else if (bytes < kBytesPerMb)
        _snwprintf_s(buf, kTextChars, _TRUNCATE, L"%.1f KB", static_cast<double>(bytes) / kBytesPerKb);
    else if (bytes < kBytesPerGb)
        _snwprintf_s(buf, kTextChars, _TRUNCATE, L"%.1f MB", static_cast<double>(bytes) / kBytesPerMb);
    else
        _snwprintf_s(buf, kTextChars, _TRUNCATE, L"%.1f GB", static_cast<double>(bytes) / kBytesPerGb);
    return std::wstring(buf);
}

std::wstring RecordRowSubText(bool fileExists, const SYSTEMTIME& localEnd, int64_t durationMs, uint64_t bytes)
{
    if (!fileExists)
        return TR(L"文件已不存在");
    //日期写成与语言无关的「年-月-日 时:分」，分隔符同样与语言无关，所以整行不经翻译
    wchar_t date[kTextChars] = { 0 };
    _snwprintf_s(date, kTextChars, _TRUNCATE, L"%04u-%02u-%02u %02u:%02u", localEnd.wYear, localEnd.wMonth,
                 localEnd.wDay, localEnd.wHour, localEnd.wMinute);
    return std::wstring(date) + kSubTextSep + FormatRecordDuration(durationMs) + kSubTextSep + FormatRecordSize(bytes);
}

bool RecordUtcMsToLocal(int64_t utcMs, SYSTEMTIME& out)
{
    if (utcMs < 0)
        return false;
    const int64_t ticks = utcMs * k100nsPerMs + kUnixEpochIn100ns;
    FILETIME ft;
    ft.dwLowDateTime = static_cast<DWORD>(ticks & kLow32Mask);
    ft.dwHighDateTime = static_cast<DWORD>(ticks >> kHigh32Shift);
    SYSTEMTIME utc;
    if (!::FileTimeToSystemTime(&ft, &utc))
        return false;
    return ::SystemTimeToTzSpecificLocalTime(NULL, &utc, &out) != FALSE;
}

int64_t RecordFileTimeToUnixMs(const FILETIME& ft)
{
    const int64_t ticks = (static_cast<int64_t>(ft.dwHighDateTime) << kHigh32Shift) | ft.dwLowDateTime;
    if (ticks < kUnixEpochIn100ns)
        return 0;
    return (ticks - kUnixEpochIn100ns) / k100nsPerMs;
}

RecordRenameResult ValidateRecordRename(const std::wstring& oldPath, const std::wstring& input, RecordFileExistsFn exists,
                                        std::wstring& newPath)
{
    const std::wstring::size_type sep = oldPath.find_last_of(L"\\/");
    const std::wstring dir = (sep == std::wstring::npos) ? std::wstring() : oldPath.substr(0, sep + 1);
    const std::wstring ext = ExtensionOf(RecordFileNameOf(oldPath));

    //去掉首尾空白；用户自己写上了原扩展名时先去掉，最后统一按原扩展名的写法补上
    std::wstring name = TrimSpaces(input);
    if (!ext.empty() && name.size() >= ext.size() && EqualsNoCase(name.substr(name.size() - ext.size()), ext))
        name = TrimSpaces(name.substr(0, name.size() - ext.size()));
    if (name.empty())
        return record_rename_empty;
    if (HasInvalidNameChar(name) || IsReservedName(name))
        return record_rename_invalid;

    const std::wstring candidate = dir + name + ext;
    if (candidate == oldPath)
        return record_rename_unchanged;
    if (candidate.size() >= flamingomedia::kMediaRecordingPathChars)
        return record_rename_too_long;
    //只改大小写：目标就是原文件自己，不查是否存在
    if (!EqualsNoCase(candidate, oldPath) && exists != NULL && exists(candidate))
        return record_rename_exists;
    newPath = candidate;
    return record_rename_ok;
}

std::wstring RecordRenameEditText(const std::wstring& path)
{
    const std::wstring fileName = RecordFileNameOf(path);
    const std::wstring ext = ExtensionOf(fileName);
    return fileName.substr(0, fileName.size() - ext.size());
}

bool MakeRecordingInfo(const std::wstring& path, int64_t durationMs, uint64_t bytes, int64_t endTimeMs,
                       flamingomedia::MediaRecordingInfo& out)
{
    if (path.empty() || path.size() >= flamingomedia::kMediaRecordingPathChars)
        return false;
    ::memset(&out, 0, sizeof(out));
    out.m_cbSize = sizeof(flamingomedia::MediaRecordingInfo);
    out.m_id = 0;
    out.m_durationMs = durationMs;
    out.m_sizeBytes = static_cast<int64_t>(bytes);
    out.m_endTimeMs = endTimeMs;
    ::wcsncpy_s(out.m_path, flamingomedia::kMediaRecordingPathChars, path.c_str(), _TRUNCATE);
    return true;
}

bool RecordInlineEditRect(const RECT& listRect, int rowIndex, int rowHeight, int scrollPos, int textLeft, int rightInset,
                          RECT& out)
{
    if (rowHeight <= 0 || rowIndex < 0)
        return false;
    const int top = listRect.top + rowIndex * rowHeight - scrollPos;
    const int bottom = top + rowHeight;
    //只露出一部分的行不显示输入框：输入框会盖到列表外面
    if (top < listRect.top || bottom > listRect.bottom)
        return false;
    const int left = listRect.left + textLeft;
    const int right = listRect.right - rightInset;
    if (right <= left)
        return false;
    out.left = left;
    out.top = top;
    out.right = right;
    out.bottom = bottom;
    return true;
}

} // namespace screenrecord
