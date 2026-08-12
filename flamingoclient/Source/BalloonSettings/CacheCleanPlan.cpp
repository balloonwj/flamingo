/**
 *  「清理本地缓存」清理计划的实现，CacheCleanPlan.cpp
 *  可清与不可清的分界见 CacheCleanPlan.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-08-12
 */

#include "CacheCleanPlan.h"
#include "../net/UserCachePath.h"   // UserCacheDir / SanitizeAccountForPath（目录口径只此一处）

#include <stdio.h>                  // swprintf_s（占用量文本的格式化）

namespace balloonsettings {

namespace {

//三个类别的显示名。与对话框里的行标题一一对应，写在一处免得两边走样。
const wchar_t* const kTitleChatFile  = L"聊天文件";
const wchar_t* const kTitleChatImage = L"聊天图片";
const wchar_t* const kTitleAvatar    = L"头像缓存";

//各类别对应的子目录名。头像有两个目录，对用户是一件事，合成一项。
const wchar_t* const kDirChatFile   = L"ChatFile";
const wchar_t* const kDirChatImage  = L"ChatImage";
const wchar_t* const kDirAvatar     = L"Avatar";
const wchar_t* const kDirUserThumb  = L"UserThumb";

//路径里是否存在"整段就是 .."的片段（即真正的上跳）。分隔符按反斜杠与正斜杠都认。
//之所以不直接 find(L"..")：目录名里本来就可能含两个点（账号净化后可能得到
//"_.._Windows" 这类名字），那是合法目录，不是上跳。
bool HasDotDotSegment(const std::wstring& path)
{
    size_t begin = 0;
    while (begin <= path.size())
    {
        size_t end = path.find_first_of(L"\\/", begin);
        if (end == std::wstring::npos)
        {
            end = path.size();
        }
        if (end - begin == 2 && path.compare(begin, 2, L"..") == 0)
        {
            return true;
        }
        if (end == path.size())
        {
            break;
        }
        begin = end + 1;
    }
    return false;
}

//往计划里追加一项。任一目录拼不出来（账号非法等）时整项跳过，不产出半截计划。
void AppendEntry(std::vector<CacheCleanEntry>& plan, int category, const wchar_t* title,
                 const std::wstring& dataRoot, const std::wstring& account,
                 const wchar_t* const* subDirs, size_t subDirCount)
{
    CacheCleanEntry entry;
    entry.m_category = category;
    entry.m_title    = title;

    for (size_t i = 0; i < subDirCount; ++i)
    {
        const std::wstring dir = net::UserCacheDir(dataRoot, account, subDirs[i]);
        if (dir.empty())
        {
            return;
        }
        entry.m_dirs.push_back(dir);
    }

    plan.push_back(entry);
}

} // anonymous namespace

std::vector<CacheCleanEntry> BuildCacheCleanPlan(const std::wstring& dataRoot,
                                                 const std::wstring& account)
{
    std::vector<CacheCleanEntry> plan;

    //还没登录（或账号非法）时没有可清的东西。UserCacheDir 对这两种情形都返回空串，
    //此处先挡一道，免得产出一个各项都空的计划让调用方去判。
    if (dataRoot.empty() || net::SanitizeAccountForPath(account).empty())
    {
        return plan;
    }

    const wchar_t* const chatFileDirs[]  = { kDirChatFile };
    const wchar_t* const chatImageDirs[] = { kDirChatImage };
    const wchar_t* const avatarDirs[]    = { kDirAvatar, kDirUserThumb };

    AppendEntry(plan, kCacheChatFile,  kTitleChatFile,  dataRoot, account,
                chatFileDirs,  sizeof(chatFileDirs) / sizeof(chatFileDirs[0]));
    AppendEntry(plan, kCacheChatImage, kTitleChatImage, dataRoot, account,
                chatImageDirs, sizeof(chatImageDirs) / sizeof(chatImageDirs[0]));
    AppendEntry(plan, kCacheAvatar,    kTitleAvatar,    dataRoot, account,
                avatarDirs,    sizeof(avatarDirs) / sizeof(avatarDirs[0]));

    return plan;
}

bool IsSafeToDeleteCacheDir(const std::wstring& dir, const std::wstring& dataRoot)
{
    if (dir.empty() || dataRoot.empty())
    {
        return false;
    }

    //含上跳段的路径一律拒绝：字符串前缀比对挡不住 "<数据根>Users\..\..\Windows" 这种。
    //注意判的是"某一段<u>整段</u>等于 .."，而不是"路径里出现过两个点" —— 账号净化后
    //可能留下 "_.._Windows" 这类目录名，它在 Windows 上完全合法，不该被误拒。
    if (HasDotDotSegment(dir))
    {
        return false;
    }

    //边界是 <数据根>Users\。dataRoot 按约定以反斜杠结尾，这里再兜一次底。
    std::wstring usersRoot = dataRoot;
    if (usersRoot[usersRoot.size() - 1] != L'\\')
    {
        usersRoot += L'\\';
    }
    usersRoot += L"Users\\";

    //必须<u>严格更深</u>：正好等于 Users\ 本身不算，那会把所有账号的数据连同聊天记录
    //一起删掉。故要求长度大于边界且前缀匹配。
    if (dir.size() <= usersRoot.size())
    {
        return false;
    }
    return dir.compare(0, usersRoot.size(), usersRoot) == 0;
}

std::wstring FormatByteSize(unsigned long long bytes)
{
    //进制取 1024。文件管理器显示的也是这个口径，两边对得上用户才不会觉得数字对不上。
    const unsigned long long kKB = 1024ULL;
    const unsigned long long kMB = kKB * 1024ULL;
    const unsigned long long kGB = kMB * 1024ULL;

    //缓冲区：最长形如 "1234.5 GB"，32 个宽字符绰绰有余。
    const size_t kBufChars = 32;
    wchar_t buf[kBufChars] = { 0 };

    if (bytes < kKB)
    {
        //不足 1 KB 时给具体字节数："0 字节"要能与"很小但不为空"区分开 ——
        //显示成 "0.0 KB" 的话用户分不清这一项到底有没有东西。
        swprintf_s(buf, kBufChars, L"%llu 字节", bytes);
    }
    else if (bytes < kMB)
    {
        swprintf_s(buf, kBufChars, L"%.1f KB", (double)bytes / (double)kKB);
    }
    else if (bytes < kGB)
    {
        swprintf_s(buf, kBufChars, L"%.1f MB", (double)bytes / (double)kMB);
    }
    else
    {
        swprintf_s(buf, kBufChars, L"%.1f GB", (double)bytes / (double)kGB);
    }
    return std::wstring(buf);
}

} // namespace balloonsettings
