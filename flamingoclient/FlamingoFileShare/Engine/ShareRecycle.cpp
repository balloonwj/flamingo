/**
 *  回收站判定与执行 ShareRecyclePolicy、CShareSystemRecycler 的实现，见 ShareRecycle.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "ShareRecycle.h"

#include "ShareRecycleShell.h"
#include "ShareWinPath.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/net/Protocol.h"   //error_code

namespace fileshare {

namespace {

//注册表里各卷回收站设置所在的键（HKEY_CURRENT_USER 下），其后接卷 GUID（形如 {xxxxxxxx-...}）
const wchar_t* const kBitBucketVolumeKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\BitBucket\\Volume\\";

//同一键下的两个值：是否不移到回收站直接删除、回收站容量（MB）
const wchar_t* const kNukeOnDeleteValue = L"NukeOnDelete";
const wchar_t* const kMaxCapacityValue = L"MaxCapacity";

//盘符根目录 "X:\" 的字符数
const size_t kDriveRootChars = 3;

//把创建外壳条目或执行操作失败的 HRESULT 归为错误码
int MapShellError(HRESULT hr)
{
    if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND))
        return error_code_share_path_not_found;
    return error_code_share_io_fail;
}

//已初始化 COM 的线程上执行一次「放进回收站」，结果见 IShareRecycler::Recycle
int RecycleWithShell(const std::wstring& absPath)
{
    ShareShellRecycleResult r;
    ShareShellRecycleItem(absPath.c_str(), r);
    if (r.m_stage == share_shell_stage_parse_path)
    {
        LOG_WARNING("share recycle: cannot parse path, hr: 0x%08x.", static_cast<unsigned>(r.m_hr));
        return MapShellError(r.m_hr);
    }
    if (r.m_stage == share_shell_stage_create_op)
    {
        LOG_ERROR("share recycle: IFileOperation unavailable, hr: 0x%08x.", static_cast<unsigned>(r.m_hr));
        return error_code_share_io_fail;
    }
    if (!r.m_advised)
    {
        //挂不上进度回调就无从做第二道检查：此时操作已经执行，只能如实记录
        LOG_WARNING("share recycle: progress sink not advised, hr: 0x%08x.", static_cast<unsigned>(r.m_hr));
    }
    if (r.m_vetoed)
    {
        LOG_WARNING("share recycle: item cannot go to the recycle bin, delete canceled.");
        return error_code_share_recycle_unavailable;
    }
    if (FAILED(r.m_hr) || r.m_aborted)
    {
        LOG_WARNING("share recycle: delete failed, hr: 0x%08x, aborted: %d.", static_cast<unsigned>(r.m_hr),
                    r.m_aborted ? 1 : 0);
        return FAILED(r.m_hr) ? MapShellError(r.m_hr) : error_code_share_io_fail;
    }
    if (::GetFileAttributesW(ShareToLongPath(absPath).c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        LOG_WARNING("share recycle: item still exists after delete.");
        return error_code_share_io_fail;
    }
    if (r.m_deleteReported && !r.m_recycledItem)
    {
        //执行之前的判定没能拦住、系统直接彻底删除了：已无法挽回，记一条错误日志以便追查
        LOG_ERROR("share recycle: item was deleted permanently instead of being recycled.");
    }
    return error_code_ok;
}

} // namespace

int64_t ShareRecyclePolicy::DefaultCapacityBytes(int64_t volumeBytes)
{
    if (volumeBytes <= 0)
        return 0;
    if (volumeBytes <= kShareRecycleFirstTierBytes)
        return volumeBytes / kShareRecycleFirstTierDivisor;
    return kShareRecycleFirstTierBytes / kShareRecycleFirstTierDivisor
           + (volumeBytes - kShareRecycleFirstTierBytes) / kShareRecycleRestDivisor;
}

int ShareRecyclePolicy::Check(const ShareVolumeRecycleInfo& info, int64_t totalBytes)
{
    //本机固定盘之外（网络路径、映射的网络盘、可移动盘、光驱等）一律不放：实测经网络路径时会被直接彻底删除
    if (info.m_driveType != DRIVE_FIXED)
        return error_code_share_recycle_unavailable;
    if (info.m_nukeOnDelete)
        return error_code_share_recycle_unavailable;
    //容量取不到、或放不下：实测超过容量的文件会被直接彻底删除
    if (info.m_capacityBytes <= 0 || totalBytes < 0 || totalBytes > info.m_capacityBytes)
        return error_code_share_recycle_unavailable;
    return error_code_ok;
}

bool CShareSystemRecycler::QueryVolume(const std::wstring& absPath, ShareVolumeRecycleInfo& out)
{
    out = ShareVolumeRecycleInfo();

    //卷的根目录：GetVolumePathNameW 能识别挂载到文件夹上的卷；它对超长路径失败时退回盘符根目录
    std::wstring root;
    wchar_t buf[MAX_PATH] = { 0 };
    if (absPath.size() < MAX_PATH && ::GetVolumePathNameW(absPath.c_str(), buf, MAX_PATH))
        root = buf;
    else if (absPath.size() >= kDriveRootChars && absPath[1] == L':' && absPath[2] == L'\\')
        root = absPath.substr(0, kDriveRootChars);
    if (root.empty())
        return false;

    out.m_driveType = ::GetDriveTypeW(root.c_str());
    if (out.m_driveType != DRIVE_FIXED)
        return true;

    //卷 GUID：GetVolumeNameForVolumeMountPointW 给出 "\\?\Volume{GUID}\"，取其中的 {GUID}
    std::wstring guid;
    wchar_t volumeName[MAX_PATH] = { 0 };
    if (::GetVolumeNameForVolumeMountPointW(root.c_str(), volumeName, MAX_PATH))
    {
        const std::wstring name(volumeName);
        const size_t left = name.find(L'{');
        const size_t right = name.find(L'}');
        if (left != std::wstring::npos && right != std::wstring::npos && right > left)
            guid = name.substr(left, right - left + 1);
    }

    bool hasCapacity = false;
    if (!guid.empty())
    {
        const std::wstring key = std::wstring(kBitBucketVolumeKey) + guid;
        DWORD value = 0;
        DWORD size = sizeof(value);
        if (::RegGetValueW(HKEY_CURRENT_USER, key.c_str(), kNukeOnDeleteValue, RRF_RT_REG_DWORD, NULL, &value, &size)
            == ERROR_SUCCESS)
            out.m_nukeOnDelete = (value != 0);
        size = sizeof(value);
        if (::RegGetValueW(HKEY_CURRENT_USER, key.c_str(), kMaxCapacityValue, RRF_RT_REG_DWORD, NULL, &value, &size)
            == ERROR_SUCCESS)
        {
            out.m_capacityBytes = static_cast<int64_t>(value) * kShareRecycleCapacityUnitBytes;
            hasCapacity = true;
        }
    }
    //注册表里还没有这个卷的设置（从没在这个卷上用过回收站）：按系统默认值估算
    if (!hasCapacity)
    {
        ULARGE_INTEGER total;
        total.QuadPart = 0;
        if (::GetDiskFreeSpaceExW(root.c_str(), NULL, &total, NULL))
            out.m_capacityBytes = ShareRecyclePolicy::DefaultCapacityBytes(static_cast<int64_t>(total.QuadPart));
    }
    return true;
}

int CShareSystemRecycler::CheckRecyclable(const std::wstring& anyPath, int64_t totalBytes)
{
    ShareVolumeRecycleInfo info;
    QueryVolume(anyPath, info);
    const int code = ShareRecyclePolicy::Check(info, totalBytes);
    if (code != error_code_ok)
        LOG_INFO("share recycle check failed, drive type: %u, nuke: %d, capacity: %lld, total: %lld.", info.m_driveType,
                 info.m_nukeOnDelete ? 1 : 0, static_cast<long long>(info.m_capacityBytes),
                 static_cast<long long>(totalBytes));
    return code;
}

int CShareSystemRecycler::Recycle(const std::wstring& absPath)
{
    if (::GetFileAttributesW(ShareToLongPath(absPath).c_str()) == INVALID_FILE_ATTRIBUTES)
        return error_code_share_path_not_found;

    //IFileOperation 要求调用线程已初始化 COM（单线程套间）；调用线程已是多线程套间时照常使用
    const HRESULT hrInit = ::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    const int code = RecycleWithShell(absPath);
    if (SUCCEEDED(hrInit))
        ::CoUninitialize();
    return code;
}

} // namespace fileshare
