/**
 *  本地图片缓存的落地写入实现，ImageCacheStore.cpp
 *
 *  只依赖 Windows 文件接口与 STL，与 FlamingoUnit 测试工程共用，因此不 include
 *  stdafx.h，在 Flamingo.vcxproj 里 PCH 须设 NotUsing。
 *
 *  balloonwj@qq.com   2026-09-30
 */

#include "ImageCacheStore.h"

#include <windows.h>

namespace net {

namespace {

// 取文件大小（字节）；文件不存在、是目录或取不到属性时返回 false。
bool GetFileSizeOnDisk(const std::wstring& path, ULONGLONG& outSize)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad))
    {
        return false;
    }
    if ((fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        return false;
    }
    outSize = (static_cast<ULONGLONG>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
    return true;
}

// 把整段字节写成新文件（已存在则覆盖）；全部写完才返回 true。
bool WriteWholeFile(const std::wstring& path, const std::string& bytes)
{
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    //图片上限 20 MiB（见 ImageTransfer.cpp 的 kMaxImageBytes），一次 WriteFile 足够。
    DWORD written = 0;
    const BOOL ok = ::WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()),
                                &written, NULL);
    ::CloseHandle(h);
    return ok != FALSE && written == static_cast<DWORD>(bytes.size());
}

} // namespace

bool StoreImageToCache(const std::wstring& cachePath, const std::string& bytes)
{
    if (cachePath.empty() || bytes.empty())
    {
        return false;
    }

    //缓存按内容 MD5 命名，同名且大小一致即为同一内容，不必重写。读取侧（聊天窗的
    //缓存命中检查）用的也是"存在且大小一致"这一判据，两边口径相同。
    ULONGLONG existing = 0;
    if (GetFileSizeOnDisk(cachePath, existing)
        && existing == static_cast<ULONGLONG>(bytes.size()))
    {
        return true;
    }

    //先写临时文件、整份写完再改名为目标名，保证目标名下只会出现完整文件：中途失败
    //只会留下临时文件，而临时文件在下面每条失败路径上都会被删除。
    const std::wstring tmpPath = cachePath + kImageCacheStoringSuffix;
    if (!WriteWholeFile(tmpPath, bytes))
    {
        ::DeleteFileW(tmpPath.c_str());
        return false;
    }

    //MOVEFILE_REPLACE_EXISTING：覆盖大小不符的残留；MOVEFILE_WRITE_THROUGH：改名落盘
    //之后才返回，与下载侧的写法一致。
    if (!::MoveFileExW(tmpPath.c_str(), cachePath.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        ::DeleteFileW(tmpPath.c_str());
        return false;
    }
    return true;
}

} // namespace net
