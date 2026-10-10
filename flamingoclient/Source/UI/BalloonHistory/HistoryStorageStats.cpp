/**
 *  聊天记录管理器「本机存储占用」页的统计与清理逻辑实现，说明见 HistoryStorageStats.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "HistoryStorageStats.h"

#include <windows.h>
#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include "../../net/Protocol.h"                         // ::message_type_*
#include "../../third-party/jsoncpp-1.9.0/json.h"
#include "../BalloonSettings/CacheCleanPlan.h"          // IsSafeToDeleteCacheDir
#include "../BalloonSettings/CacheCleanExecutor.h"      // DeleteDirTree

namespace balloonhistory {

namespace {

//MD5 的十六进制字符数
const size_t kMd5HexLen = 32;
//语音缓存文件的扩展名（与 balloonvoice::VoiceCachePath 一致）
const wchar_t kVoiceExt[] = L".ogg";
//本地库的两个附属文件（WAL 模式）
const wchar_t kWalSuffix[] = L"-wal";
const wchar_t kShmSuffix[] = L"-shm";
//FILETIME 风格的 64 位大小：高 32 位的倍数
const int kHighPartShift = 32;

//一个会话的键
typedef std::pair<int, int64_t> ConvKey;

//ASCII 十六进制字符
bool IsHexChar(char ch)
{
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

//目录路径补上结尾的反斜杠
std::wstring WithSlash(const std::wstring& dir)
{
    if (dir.empty() || dir[dir.size() - 1] == L'\\' || dir[dir.size() - 1] == L'/')
    {
        return dir;
    }
    return dir + L"\\";
}

//路径去掉结尾的反斜杠
std::wstring WithoutSlash(const std::wstring& path)
{
    std::wstring p = path;
    while (!p.empty() && (p[p.size() - 1] == L'\\' || p[p.size() - 1] == L'/'))
    {
        p.erase(p.size() - 1);
    }
    return p;
}

//ASCII 字母转小写（比较路径前缀用；路径里的其它字符原样）
std::wstring LowerPath(const std::wstring& s)
{
    std::wstring out = s;
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i] >= L'A' && out[i] <= L'Z')
        {
            out[i] = (wchar_t)(out[i] - L'A' + L'a');
        }
    }
    return out;
}

//path 是否严格在 dir 之下（dir 以反斜杠结尾，不分大小写）
bool IsUnderDir(const std::wstring& path, const std::wstring& dir)
{
    if (dir.empty())
    {
        return false;
    }
    const std::wstring d = LowerPath(WithSlash(dir));
    const std::wstring p = LowerPath(path);
    return p.size() > d.size() && p.compare(0, d.size(), d) == 0;
}

//单个文件的大小；不存在或是目录时为 0
int64_t FileBytes(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)
        || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        return 0;
    }
    return ((int64_t)info.nFileSizeHigh << kHighPartShift) | (int64_t)info.nFileSizeLow;
}

//会话排序：合计字节数多的在前
bool MoreBytesFirst(const HistoryStorageConv& a, const HistoryStorageConv& b)
{
    return a.TotalBytes() > b.TotalBytes();
}

//取 JSON 对象里的字符串字段；不是字符串时为空串
std::string JsonString(const Json::Value& obj, const char* key)
{
    const Json::Value& v = obj[key];
    return v.isString() ? v.asString() : std::string();
}

} // namespace

bool IsValidCacheMd5(const std::string& md5)
{
    if (md5.size() != kMd5HexLen)
    {
        return false;
    }
    for (size_t i = 0; i < md5.size(); ++i)
    {
        if (!IsHexChar(md5[i]))
        {
            return false;
        }
    }
    return true;
}

bool CacheRefFromContent(int msgtype, const std::string& contentJson, HistoryCacheRef& out)
{
    const char* objKey = NULL;
    int kind = kStorageImage;
    if (msgtype == ::message_type_image || msgtype == ::message_type_screenshot)
    {
        objKey = "image";
        kind = kStorageImage;
    }
    else if (msgtype == ::message_type_file)
    {
        objKey = "file";
        kind = kStorageFile;
    }
    else if (msgtype == ::message_type_voice)
    {
        objKey = "voice";
        kind = kStorageVoice;
    }
    else
    {
        return false;
    }
    if (contentJson.empty())
    {
        return false;
    }
    Json::Value root;
    Json::CharReaderBuilder builder;
    JSONCPP_STRING errs;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    const char* begin = contentJson.c_str();
    if (!reader->parse(begin, begin + contentJson.size(), &root, &errs) || !root.isObject()
        || !root[objKey].isObject())
    {
        return false;
    }
    const std::string md5 = JsonString(root[objKey], "md5");
    if (!IsValidCacheMd5(md5))
    {
        return false;
    }
    out.m_kind = kind;
    out.m_md5 = md5;
    return true;
}

std::wstring StorageCachePath(const HistoryStorageDirs& dirs, int kind, const std::string& md5)
{
    if (!IsValidCacheMd5(md5))
    {
        return std::wstring();
    }
    const std::wstring md5W(md5.begin(), md5.end());
    switch (kind)
    {
    //图片：ChatImage\<md5>
    case kStorageImage:
        return dirs.m_imageDir.empty() ? std::wstring() : WithSlash(dirs.m_imageDir) + md5W;
    //文件：ChatFile\<md5>\ 整个子目录（里面是按原文件名命名的那一份）
    case kStorageFile:
        return dirs.m_fileDir.empty() ? std::wstring() : WithSlash(dirs.m_fileDir) + md5W + L"\\";
    //语音：ChatVoice\<md5>.ogg
    case kStorageVoice:
        return dirs.m_voiceDir.empty() ? std::wstring() : WithSlash(dirs.m_voiceDir) + md5W + kVoiceExt;
    //类别越界
    default:
        return std::wstring();
    }
}

int64_t StoragePathBytes(const std::wstring& path)
{
    if (path.empty())
    {
        return 0;
    }
    const std::wstring p = WithoutSlash(path);
    const DWORD attrs = ::GetFileAttributesW(p.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES)
    {
        return 0;
    }
    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
        return FileBytes(p);
    }
    //目录：递归累加（缓存目录不会有符号链接，遇到重解析点也不跟进，免得统计到目录之外）
    int64_t sum = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = ::FindFirstFileW((p + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
    {
        return 0;
    }
    do
    {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..")
        {
            continue;
        }
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        {
            continue;
        }
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            sum += StoragePathBytes(p + L"\\" + name);
        }
        else
        {
            sum += ((int64_t)fd.nFileSizeHigh << kHighPartShift) | (int64_t)fd.nFileSizeLow;
        }
    } while (::FindNextFileW(h, &fd));
    ::FindClose(h);
    return sum;
}

HistoryStorageStats ComputeStorageStats(const std::vector<HistoryCacheRef>& refs, const HistoryStorageDirs& dirs)
{
    HistoryStorageStats stats;
    //每份缓存只量一次：同一个 md5 在多个会话、多条消息里出现时复用第一次的结果
    std::map<std::pair<int, std::string>, int64_t> sizeOf;
    //每个会话里同一份缓存只计一次（同一张图在一个会话里发了两次，占用的仍是一份）
    std::set<std::pair<ConvKey, std::pair<int, std::string> > > counted;
    std::map<ConvKey, HistoryStorageConv> convs;
    for (size_t i = 0; i < refs.size(); ++i)
    {
        const HistoryCacheRef& r = refs[i];
        if (r.m_kind < 0 || r.m_kind >= kStorageKindCount)
        {
            continue;
        }
        const std::pair<int, std::string> key(r.m_kind, r.m_md5);
        std::map<std::pair<int, std::string>, int64_t>::iterator it = sizeOf.find(key);
        int64_t bytes = 0;
        if (it == sizeOf.end())
        {
            const std::wstring path = StorageCachePath(dirs, r.m_kind, r.m_md5);
            const bool exists = !path.empty()
                             && ::GetFileAttributesW(WithoutSlash(path).c_str()) != INVALID_FILE_ATTRIBUTES;
            bytes = exists ? StoragePathBytes(path) : -1;
            sizeOf[key] = bytes;
            //页首总数按 md5 去重：第一次遇到时计入
            if (bytes >= 0)
            {
                stats.m_totalBytes[r.m_kind] += bytes;
                ++stats.m_totalFiles[r.m_kind];
            }
        }
        else
        {
            bytes = it->second;
        }
        //本机缓存里没有的不计入会话
        if (bytes < 0)
        {
            continue;
        }
        const ConvKey conv(r.m_ctype, r.m_cid);
        if (!counted.insert(std::make_pair(conv, key)).second)
        {
            continue;
        }
        HistoryStorageConv& c = convs[conv];
        c.m_ctype = r.m_ctype;
        c.m_cid = r.m_cid;
        c.m_bytes[r.m_kind] += bytes;
        ++c.m_files[r.m_kind];
    }
    for (std::map<ConvKey, HistoryStorageConv>::const_iterator it = convs.begin(); it != convs.end(); ++it)
    {
        stats.m_convs.push_back(it->second);
    }
    //按合计倒序；合计相同时保持 map 的顺序（ctype、cid 升序）
    std::stable_sort(stats.m_convs.begin(), stats.m_convs.end(), MoreBytesFirst);
    if (!dirs.m_dbPath.empty())
    {
        stats.m_dbBytes = FileBytes(dirs.m_dbPath) + FileBytes(dirs.m_dbPath + kWalSuffix)
                        + FileBytes(dirs.m_dbPath + kShmSuffix);
    }
    return stats;
}

std::vector<std::wstring> StorageCleanTargets(const std::vector<HistoryCacheRef>& refs, int ctype, int64_t cid,
                                              const HistoryStorageDirs& dirs)
{
    std::vector<std::wstring> paths;
    std::set<std::wstring> seen;
    for (size_t i = 0; i < refs.size(); ++i)
    {
        const HistoryCacheRef& r = refs[i];
        if (cid != 0 && (r.m_ctype != ctype || r.m_cid != cid))
        {
            continue;
        }
        const std::wstring path = StorageCachePath(dirs, r.m_kind, r.m_md5);
        if (!path.empty() && seen.insert(path).second)
        {
            paths.push_back(path);
        }
    }
    return paths;
}

bool DeleteStoragePath(const std::wstring& path, const HistoryStorageDirs& dirs)
{
    if (path.empty())
    {
        return false;
    }
    //边界检查：在 <数据根>Users\ 之下，并且就在三个缓存目录之一里面（不允许删缓存目录本身或别处的文件）
    if (!balloonsettings::IsSafeToDeleteCacheDir(path, dirs.m_dataRoot))
    {
        return false;
    }
    if (!IsUnderDir(path, dirs.m_imageDir) && !IsUnderDir(path, dirs.m_fileDir) && !IsUnderDir(path, dirs.m_voiceDir))
    {
        return false;
    }
    const std::wstring p = WithoutSlash(path);
    const DWORD attrs = ::GetFileAttributesW(p.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES)
    {
        return true;
    }
    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        return balloonsettings::DeleteDirTree(p, dirs.m_dataRoot);
    }
    return ::DeleteFileW(p.c_str()) != FALSE;
}

} // namespace balloonhistory
