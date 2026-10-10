/**
 *  IMediaHost 录像记录方法的实现，说明见 MediaRecordingBridge.h。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"

#include "MediaRecordingBridge.h"
#include "../UI/BalloonHistory/RecordLocalStore.h"

#include <cwchar>
#include <vector>

namespace balloonapp {

namespace {

//这条记录的路径能否放进 MediaRecordingInfo（要留出结尾的 L'\0'）
bool FitsRecordingInfo(const balloonhistory::RecordLocalEntry& e)
{
    return e.m_path.size() < flamingomedia::kMediaRecordingPathChars;
}

} // namespace

bool AddRecordingToStore(const flamingomedia::MediaRecordingInfo* info)
{
    if (info == NULL || info->m_cbSize != sizeof(flamingomedia::MediaRecordingInfo))
    {
        return false;
    }
    balloonhistory::RecordLocalEntry e;
    //DLL 传来的路径以 L'\0' 结尾；为防结构体被写满而没有结尾，按容量截取
    e.m_path.assign(info->m_path, ::wcsnlen(info->m_path, flamingomedia::kMediaRecordingPathChars));
    e.m_durationMs = info->m_durationMs;
    e.m_sizeBytes = info->m_sizeBytes;
    e.m_endTimeMs = info->m_endTimeMs;
    return balloonhistory::CRecordLocalStore::GetInstance().Add(e, NULL);
}

uint32_t ListRecordingsFromStore(flamingomedia::MediaRecordingInfo* out, uint32_t capacity)
{
    std::vector<balloonhistory::RecordLocalEntry> all;
    if (!balloonhistory::CRecordLocalStore::GetInstance().List(all))
    {
        return 0;
    }
    uint32_t total = 0;
    for (size_t i = 0; i < all.size(); ++i)
    {
        const balloonhistory::RecordLocalEntry& e = all[i];
        //路径过长的记录放不进结构体，不计入
        if (!FitsRecordingInfo(e))
        {
            continue;
        }
        if (out != NULL && total < capacity && out[total].m_cbSize == sizeof(flamingomedia::MediaRecordingInfo))
        {
            flamingomedia::MediaRecordingInfo& dst = out[total];
            dst.m_id = e.m_id;
            dst.m_durationMs = e.m_durationMs;
            dst.m_sizeBytes = e.m_sizeBytes;
            dst.m_endTimeMs = e.m_endTimeMs;
            ::wcsncpy_s(dst.m_path, flamingomedia::kMediaRecordingPathChars, e.m_path.c_str(), _TRUNCATE);
        }
        ++total;
    }
    return total;
}

bool UpdateRecordingPathInStore(int64_t id, const wchar_t* newPath)
{
    if (newPath == NULL || newPath[0] == L'\0')
    {
        return false;
    }
    return balloonhistory::CRecordLocalStore::GetInstance().UpdatePath(id, newPath);
}

bool RemoveRecordingFromStore(int64_t id)
{
    return balloonhistory::CRecordLocalStore::GetInstance().Remove(id);
}

} // namespace balloonapp
