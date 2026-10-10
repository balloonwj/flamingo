/**
 *  上传分片文件的命名与收尾策略实现，UploadPartPolicy.cpp
 *  接口说明见 UploadPartPolicy.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "UploadPartPolicy.h"
#include <stdio.h>
#include <atomic>

namespace fileserver {

//登记表的键：md5 统一转成小写，使大小写不同的同一 md5 落到同一项
static std::string normalizeMd5(const std::string& md5)
{
    std::string key(md5);
    for (size_t i = 0; i < key.size(); ++i)
    {
        if (key[i] >= 'A' && key[i] <= 'Z')
            key[i] = static_cast<char>(key[i] - 'A' + 'a');
    }
    return key;
}

//文件是否存在：能以只读方式打开即视为存在
static bool fileExists(const std::string& path)
{
    FILE* fp = fopen(path.c_str(), "rb");
    if (fp == NULL)
        return false;
    fclose(fp);
    return true;
}

bool UploadPartRegistry::claim(const std::string& md5)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    //set::insert 的返回值 second 为 false 表示该 md5 已在表中，即已被别的会话占用
    return m_claimed.insert(normalizeMd5(md5)).second;
}

void UploadPartRegistry::release(const std::string& md5)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_claimed.erase(normalizeMd5(md5));
}

bool UploadPartRegistry::isClaimed(const std::string& md5) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_claimed.find(normalizeMd5(md5)) != m_claimed.end();
}

uint64_t NextUploadPartTag()
{
    //函数内静态原子量：C++11 保证其初始化线程安全，fetch_add 保证并发取号互不重复
    static std::atomic<uint64_t> s_nextTag(1);
    return s_nextTag.fetch_add(1);
}

std::string MakeUploadPartPath(const std::string& baseDir, const std::string& md5,
                               bool shared, uint64_t tag)
{
    if (shared)
        return baseDir + md5 + ".part";

    //私有分片：<md5>.<序号>.part。序号在本进程内唯一，两个会话不会写到同一个文件
    char tagText[32];
    snprintf(tagText, sizeof(tagText), "%llu", static_cast<unsigned long long>(tag));
    return baseDir + md5 + "." + tagText + ".part";
}

upload_finalize_result FinalizeUploadPart(const std::string& partPath,
                                          const std::string& finalPath,
                                          bool finalKnownExists)
{
    //最终文件已知存在：别的会话先行完成了同一文件，内容由 MD5 保证一致，丢弃本会话的分片
    if (finalKnownExists)
    {
        remove(partPath.c_str());
        return upload_finalize_existed;
    }

    if (rename(partPath.c_str(), finalPath.c_str()) == 0)
        return upload_finalize_renamed;

    //改名失败：两个会话几乎同时收齐时，调用方查询时最终文件尚不存在，改名前已被对方生成。
    //Windows 上目标已存在时 rename 必然失败，这种情况按成功处理（Linux 上 rename 会直接覆盖，
    //不会走到这里）
    if (fileExists(finalPath))
    {
        remove(partPath.c_str());
        return upload_finalize_existed;
    }
    return upload_finalize_failed;
}

} // namespace fileserver
