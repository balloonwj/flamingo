/**
 *  文件会话里云端录像场景（scene 6）的运行参数与路径解析，见 CloudRecordScene.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "CloudRecordScene.h"

#include <sys/stat.h>
#include <sys/types.h>
#include "../utils/CloudRecordCtlMsg.h"

namespace fileserver {

namespace {

//录像目录；为空表示不支持 scene 6。只在启动时写一次，之后只读
std::string g_cloudRecordDir;
//上传完成回调；只在启动时写一次
CloudRecordUploadedFn g_onUploaded = NULL;

} // namespace

void SetCloudRecordScene(const std::string& dir, CloudRecordUploadedFn onUploaded)
{
    g_cloudRecordDir = dir;
    g_onUploaded = onUploaded;
}

bool CloudRecordSceneEnabled()
{
    return !g_cloudRecordDir.empty();
}

bool ResolveCloudRecordTarget(const std::string& tokenKey, std::string& dir, std::string& fileName)
{
    dir.clear();
    fileName.clear();
    if (g_cloudRecordDir.empty())
        return false;
    if (!ParseCloudRecordTokenKey(tokenKey, fileName))
        return false;
    dir = g_cloudRecordDir;
    return true;
}

bool CloudRecordFileComplete(const std::string& path, int64_t expectedBytes)
{
#if defined(_WIN32)
    struct _stat64 st;
    if (_stat64(path.c_str(), &st) != 0)
        return false;
#else
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        return false;
#endif
    return static_cast<int64_t>(st.st_size) == expectedBytes;
}

void NotifyCloudRecordUploaded(const std::string& fileName, uint64_t bytes)
{
    if (g_onUploaded != NULL)
        g_onUploaded(fileName, bytes);
}

} // namespace fileserver
