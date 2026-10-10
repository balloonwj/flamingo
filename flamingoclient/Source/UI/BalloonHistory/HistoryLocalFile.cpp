/**
 *  按一条本地库消息记录找出它在本机的可用文件或文件夹路径。整体说明见 HistoryLocalFile.h。
 *  查找规则原先写在 CBalloonHistoryWindow::ResolveRecordLocalFile 里，主面板搜索下拉也要用，
 *  于是挪到这里，两处调用同一份。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "HistoryLocalFile.h"

#include "HistoryView.h"       // ParseContentBrief：从内容 JSON 取 MD5、文件名、文件夹会话 id
#include "HistoryRowMenu.h"    // IsHistoryRowImageType / IsHistoryRowFileType / IsHistoryRowFolderType
#include "FileLocalStore.h"    // 文件本地落点登记库
#include "FolderLocalStore.h"  // 文件夹本地落点登记库（按会话 id 记根目录）
#include "../BalloonChatWnd/ChatFileLocation.h"   // ResolveLocalFilePath（本地落点优先级）
#include "../../net/ImageTransfer.h"              // CacheFilePathForMd5（图片缓存路径）
#include "../../net/FileTransfer.h"               // CacheFilePathFor（文件下载缓存路径）
#include "../../net/CurrentUserAccount.h"         // net::CurrentUserAccount（缓存目录按登录账号分）

namespace balloonhistory {

namespace {

// 某个路径在磁盘上是否真的存在（且不是目录）。
bool FileExistsOnDisk(const std::wstring& path)
{
    if (path.empty())
    {
        return false;
    }
    const DWORD attr = ::GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES)
    {
        return false;
    }
    return (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// 某个路径在磁盘上是否是一个真的存在的目录。
bool FolderExistsOnDisk(const std::wstring& path)
{
    if (path.empty())
    {
        return false;
    }
    const DWORD attr = ::GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES)
    {
        return false;
    }
    return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

} // namespace

bool ResolveRecordLocalPath(const HistoryRecord& rec, std::wstring& outPath)
{
    outPath.clear();

    const HistoryContentBrief brief = ParseContentBrief(rec.m_msgtype, rec.m_content);

    if (IsHistoryRowImageType(rec.m_msgtype))
    {
        //图片只有缓存这一处落点：本地缓存按内容 MD5 命名。
        if (brief.m_imageMd5.empty())
        {
            return false;
        }
        const std::wstring cache =
            net::ImageTransfer::CacheFilePathForMd5(net::CurrentUserAccount(), brief.m_imageMd5);
        if (!FileExistsOnDisk(cache))
        {
            return false;
        }
        outPath = cache;
        return true;
    }

    if (IsHistoryRowFileType(rec.m_msgtype))
    {
        //文件有两处可能的落点，优先级与回退交给纯逻辑 ResolveLocalFilePath 决定
        //（登记路径优先、失效再回退 MD5 缓存路径）。这里只负责把"路径"与"文件在不在"
        //两样事实交给它。
        std::wstring registered;
        CFileLocalStore::GetInstance().Query(brief.m_fileMd5, brief.m_fileName, registered);

        std::wstring cache;
        if (!brief.m_fileMd5.empty() && !brief.m_fileName.empty())
        {
            cache = net::FileTransfer::CacheFilePathFor(net::CurrentUserAccount(),
                                                        brief.m_fileMd5, brief.m_fileName);
        }

        const std::wstring picked = balloonchatwnd::ResolveLocalFilePath(
            registered, FileExistsOnDisk(registered), cache, FileExistsOnDisk(cache));
        if (picked.empty())
        {
            return false;
        }
        outPath = picked;
        return true;
    }

    if (IsHistoryRowFolderType(rec.m_msgtype))
    {
        //文件夹传输记录只有一处落点：本机按会话 id 登记的那个根目录。发送端记的是源
        //目录、接收端记的是落地目录，各记各的。换一台机器一律查不到 —— 那台机器上从
        //来没有过这个目录，只显示记录、不给「打开文件夹」，这是预期行为。
        if (brief.m_folderSessionId.empty())
        {
            return false;
        }
        std::wstring registered;
        if (!CFolderLocalStore::GetInstance().Query(brief.m_folderSessionId, registered))
        {
            return false;
        }
        //登记在、目录却已被改名或删除时同样不给按钮，免得点下去打开一个空白窗口
        if (!FolderExistsOnDisk(registered))
        {
            return false;
        }
        outPath = registered;
        return true;
    }

    return false;   //其余类型没有"本地文件"这一说
}

} // namespace balloonhistory
