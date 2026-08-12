/**
 *  客户端升级包目录扫描器实现，UpdatePackageScanner.cpp
 *  balloonwj@qq.com   2026-08-11
 */
#include "UpdatePackageScanner.h"

#include <fstream>

#include "../base/AsyncLog.h"
#include "../utils/FileEnum.h"

//zip 文件头魔数：本地文件头以 "PK\x03\x04" 开头。这是最便宜的一道防呆，
//挡的是"把 exe 或说明文档误放进升级包目录"这类低级错误；真正"解压得开"的校验
//要等客户端更新器写完 zip 解析后才能做。
static const char ZIP_MAGIC[4] = { 'P', 'K', '\x03', '\x04' };
//zip 魔数的字节数
static const size_t ZIP_MAGIC_LEN = 4;

//读文件头判断是不是 zip。文件不足 4 字节（含空文件）一律判否。
static bool looksLikeZip(const std::string& path)
{
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in)
        return false;

    char head[ZIP_MAGIC_LEN] = { 0 };
    in.read(head, static_cast<std::streamsize>(ZIP_MAGIC_LEN));
    if (static_cast<size_t>(in.gcount()) != ZIP_MAGIC_LEN)
        return false;

    for (size_t i = 0; i < ZIP_MAGIC_LEN; ++i)
    {
        if (head[i] != ZIP_MAGIC[i])
            return false;
    }
    return true;
}

void ScanUpdatePackages(const std::string& dir, std::vector<UpdatePackageCandidate>& out)
{
    out.clear();
    if (dir.empty())
    {
        LOGW("ScanUpdatePackages: update package dir not configured");
        return;
    }

    std::vector<std::string> names;
    fileenum::ListFileNames(dir, names);

    const std::string base = fileenum::WithTrailingSeparator(dir);
    for (size_t i = 0; i < names.size(); ++i)
    {
        const std::string fullPath = base + names[i];

        int64_t size = 0;
        int64_t mtime = 0;
        if (!fileenum::StatFile64(fullPath, size, mtime))
            continue;

        UpdatePackageCandidate item;
        item.filename = names[i];
        item.size     = size;
        item.iszip    = looksLikeZip(fullPath);
        item.md5      = fileenum::ComputeFileMd5(fullPath);
        if (item.md5.empty())
        {
            //算不出 MD5 的文件（读权限不足等）不列出来：列了也没法建版本记录
            LOGW("ScanUpdatePackages: compute md5 failed, path: %s", fullPath.c_str());
            continue;
        }

        out.push_back(item);
    }

    LOGI("ScanUpdatePackages ok, dir: %s, candidates: %d",
         dir.c_str(), static_cast<int>(out.size()));
}

UpdatePackageScanner::UpdatePackageScanner()
{
}

UpdatePackageScanner::~UpdatePackageScanner()
{
}

void UpdatePackageScanner::init(const std::string& dir)
{
    m_dir = dir;
    if (m_dir.empty())
        LOGW("UpdatePackageScanner: update package dir not configured, console cannot list packages");
    else
        LOGI("UpdatePackageScanner init ok, dir: %s", m_dir.c_str());
}

const std::string& UpdatePackageScanner::dir() const
{
    return m_dir;
}

void UpdatePackageScanner::scan(std::vector<UpdatePackageCandidate>& out) const
{
    ScanUpdatePackages(m_dir, out);
}
