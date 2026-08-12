/**
 *  客户端升级包目录索引实现，UpdatePackageIndex.cpp
 *  balloonwj@qq.com   2026-08-11
 */
#include "UpdatePackageIndex.h"

#include "../base/AsyncLog.h"
#include "../utils/FileEnum.h"
#include "../chatserversrc/Protocol.h"   //enum file_scene

//把字符串里的大写字母转成小写（只处理 ASCII，MD5 十六进制串足够用）
static std::string toLower(const std::string& s)
{
    std::string r = s;
    for (size_t i = 0; i < r.size(); ++i)
    {
        if (r[i] >= 'A' && r[i] <= 'Z')
            r[i] = static_cast<char>(r[i] - 'A' + 'a');
    }
    return r;
}

bool ResolveDownloadPath(int scene, const std::string& md5, int64_t nowMs,
                         const std::string& fileBaseDir,
                         UpdatePackageIndex& updateIndex,
                         std::string& outPath)
{
    outPath.clear();
    if (md5.empty())
        return false;

    //升级包由运维手工放入，文件名是人起的，只能经索引按内容MD5 反查
    if (scene == file_scene_update)
        return updateIndex.find(md5, nowMs, outPath);

    //其余场景的文件由客户端上传，落盘时即以内容MD5 命名，直接拼接。
    //这条路径每天要走成千上万次，不查索引以免引入额外开销。
    outPath = fileBaseDir + md5;
    return true;
}

UpdatePackageIndex::UpdatePackageIndex()
    : m_lastScanMs(0)
{
}

UpdatePackageIndex::~UpdatePackageIndex()
{
}

void UpdatePackageIndex::init(const std::string& dir, int64_t nowMs)
{
    m_dir = dir;
    m_byPath.clear();
    m_byMd5.clear();
    m_lastScanMs = 0;

    if (m_dir.empty())
    {
        LOGI("UpdatePackageIndex disabled: update package dir not configured");
        return;
    }

    rescan(nowMs);
    LOGI("UpdatePackageIndex init ok, dir: %s, packages: %d",
         m_dir.c_str(), static_cast<int>(m_byMd5.size()));
}

void UpdatePackageIndex::rescan(int64_t nowMs)
{
    m_lastScanMs = nowMs;
    if (m_dir.empty())
        return;

    std::vector<std::string> names;
    fileenum::ListFileNames(m_dir, names);

    //扫描结果重建两张表。m_byPath 里没被本轮扫到的条目自然被丢弃（文件已删除）。
    std::map<std::string, Entry> freshByPath;
    std::map<std::string, std::string> freshByMd5;

    const std::string base = fileenum::WithTrailingSeparator(m_dir);
    for (size_t i = 0; i < names.size(); ++i)
    {
        const std::string fullPath = base + names[i];

        int64_t size = 0;
        int64_t mtime = 0;
        if (!fileenum::StatFile64(fullPath, size, mtime))
            continue;

        //大小与修改时间都没变就沿用上次算好的 MD5，避免每次重扫都把整个目录哈希一遍
        Entry entry;
        std::map<std::string, Entry>::const_iterator it = m_byPath.find(fullPath);
        if (it != m_byPath.end() && it->second.m_size == size && it->second.m_mtime == mtime
            && !it->second.m_md5.empty())
        {
            entry = it->second;
        }
        else
        {
            entry.m_size  = size;
            entry.m_mtime = mtime;
            entry.m_md5   = fileenum::ComputeFileMd5(fullPath);
            if (entry.m_md5.empty())
            {
                LOGW("UpdatePackageIndex compute md5 failed, path: %s", fullPath.c_str());
                continue;
            }
        }

        freshByPath[fullPath] = entry;

        //同一份内容出现在多个文件名下时保留先遇到的那个。ListFileNames 已按文件名排序，
        //故"先遇到的"是确定的（字典序最小的那个名字），换台机器结果也一样。
        if (freshByMd5.find(entry.m_md5) == freshByMd5.end())
            freshByMd5[entry.m_md5] = fullPath;
    }

    m_byPath.swap(freshByPath);
    m_byMd5.swap(freshByMd5);
}

bool UpdatePackageIndex::find(const std::string& md5, int64_t nowMs, std::string& outPath)
{
    outPath.clear();
    if (m_dir.empty() || md5.empty())
        return false;

    const std::string key = toLower(md5);

    std::map<std::string, std::string>::const_iterator it = m_byMd5.find(key);
    if (it != m_byMd5.end())
    {
        outPath = it->second;
        return true;
    }

    //没命中：可能是运维刚放进来的新包。重扫一次再判，但要受节流约束——
    //否则一串伪造 MD5 的下载请求就能让服务器反复把整个目录哈希一遍。
    if (nowMs - m_lastScanMs < kUpdateIndexRescanMinIntervalMs)
        return false;

    LOGI("UpdatePackageIndex miss, rescanning dir: %s", m_dir.c_str());
    rescan(nowMs);

    it = m_byMd5.find(key);
    if (it == m_byMd5.end())
        return false;

    outPath = it->second;
    return true;
}

size_t UpdatePackageIndex::size() const
{
    return m_byMd5.size();
}
