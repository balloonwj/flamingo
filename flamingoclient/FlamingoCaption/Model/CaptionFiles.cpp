/**
 *  录像对应的字幕文件，实现见 CaptionFiles.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "CaptionFiles.h"

#include <windows.h>

#include "CaptionDraft.h"

namespace caption {

namespace {

bool FileExists(const std::wstring& path)
{
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

int ExportKindToStatus(int kind)
{
    switch (kind)
    {
    //硬字幕版
    case caption_export_hard:
        return kCaptionFileHard;
    //字幕轨版
    case caption_export_track:
        return kCaptionFileTrack;
    //SRT
    case caption_export_srt:
        return kCaptionFileSrt;
    //未知种类：草稿是新版本写的，忽略
    default:
        return kCaptionFileNone;
    }
}

} // namespace

std::wstring FileNameOf(const std::wstring& path)
{
    const size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

std::wstring DirectoryOf(const std::wstring& path)
{
    const size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? std::wstring() : path.substr(0, pos + 1);
}

int QueryCaptionFileStatus(const std::wstring& videoPath)
{
    const std::wstring draftPath = CaptionDraftIo::DraftPathFor(videoPath);
    if (!FileExists(draftPath))
        return kCaptionFileNone;
    int status = kCaptionFileDraft;
    CaptionDraft d;
    if (!CaptionDraftIo::LoadFile(draftPath, d, NULL))
        return status;
    const std::wstring dir = DirectoryOf(videoPath);
    for (size_t i = 0; i < d.m_exports.size(); ++i)
    {
        if (FileExists(dir + d.m_exports[i].m_name))
            status |= ExportKindToStatus(d.m_exports[i].m_kind);
    }
    return status;
}

bool IsAudioOnlyMediaPath(const std::wstring& mediaPath)
{
    //只录声音的录音扩展名（与 FlamingoMedia/Record/RecordPolicy.h 的 kRecordAudioExt 一致）
    const wchar_t kAudioExt[] = L".m4a";
    const size_t extLen = sizeof(kAudioExt) / sizeof(kAudioExt[0]) - 1;
    if (mediaPath.size() <= extLen)
        return false;
    return _wcsicmp(mediaPath.c_str() + mediaPath.size() - extLen, kAudioExt) == 0;
}

std::wstring MakeExportPath(const std::wstring& videoPath, const std::wstring& suffix, const std::wstring& ext)
{
    const std::wstring dir = DirectoryOf(videoPath);
    std::wstring name = FileNameOf(videoPath);
    const size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos)
        name = name.substr(0, dot);
    return dir + name + suffix + ext;
}

bool RenameCaptionDraft(const std::wstring& oldVideoPath, const std::wstring& newVideoPath)
{
    const std::wstring oldDraft = CaptionDraftIo::DraftPathFor(oldVideoPath);
    const std::wstring newDraft = CaptionDraftIo::DraftPathFor(newVideoPath);
    if (!FileExists(oldDraft))
        return false;
    if (!MoveFileExW(oldDraft.c_str(), newDraft.c_str(), MOVEFILE_REPLACE_EXISTING))
        return false;
    CaptionDraft d;
    if (CaptionDraftIo::LoadFile(newDraft, d, NULL))
    {
        d.m_videoName = FileNameOf(newVideoPath);
        CaptionDraftIo::SaveFile(newDraft, d);
    }
    return true;
}

bool DeleteCaptionDraft(const std::wstring& videoPath)
{
    const std::wstring draft = CaptionDraftIo::DraftPathFor(videoPath);
    if (!FileExists(draft))
        return false;
    return DeleteFileW(draft.c_str()) != FALSE;
}

} // namespace caption
