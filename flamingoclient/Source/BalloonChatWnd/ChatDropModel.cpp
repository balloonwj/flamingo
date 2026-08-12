/**
 *  ChatDropModel.cpp
 *
 *  「把文件拖进聊天窗」的落点归类实现。接口说明与设计取舍见 ChatDropModel.h。
 *
 *  balloonwj@qq.com   2026-08-11
 */

//本文件被 FlamingoUnit 测试工程直接编译，故<u>不</u>走主工程的预编译头
//（两处工程文件里都登记为 NotUsing），需要的系统头在这里自己引。
#include "ChatDropModel.h"

#include <windows.h>
#include <string>

namespace balloonchatwnd {

namespace {

//可当图片消息发的扩展名（<u>不含点号</u>、全小写）。口径与聊天窗「图片」按钮的
//选择对话框过滤器一致，见 CBalloonChatWindow::PickAndSendImage 里的 lpstrFilter。
const wchar_t* const kImageExtensions[] =
{
    L"png",
    L"jpg",
    L"jpeg",
    L"bmp",
    L"gif"
};

//上面那张表的项数。
const int kImageExtensionCount = (int)(sizeof(kImageExtensions) / sizeof(kImageExtensions[0]));

/**
 *  取一条路径的扩展名（不含点号），并转成小写。没有扩展名时返回空串。
 *
 *  只从最后一个路径分隔符之后找点号 —— 否则 "D:\my.folder\readme" 会被当成
 *  扩展名是 "folder\readme"。
 */
std::wstring LowerExtensionOf(const std::wstring& path)
{
    const std::wstring::size_type sep = path.find_last_of(L"\\/");
    const std::wstring::size_type dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos)
    {
        return std::wstring();
    }
    if (sep != std::wstring::npos && dot < sep)
    {
        return std::wstring();   // 点号出现在目录名里，文件名本身没有扩展名
    }
    if (dot + 1 >= path.size())
    {
        return std::wstring();   // 以点号结尾，没有扩展名
    }

    std::wstring ext = path.substr(dot + 1);
    for (std::wstring::size_type i = 0; i < ext.size(); ++i)
    {
        //只处理 ASCII 大写字母：扩展名里出现非 ASCII 字符时按原样比较即可，
        //反正也不会命中上面那张全小写的表。
        if (ext[i] >= L'A' && ext[i] <= L'Z')
        {
            ext[i] = (wchar_t)(ext[i] - L'A' + L'a');
        }
    }
    return ext;
}

} // namespace

bool IsImageDropPath(const std::wstring& path)
{
    const std::wstring ext = LowerExtensionOf(path);
    if (ext.empty())
    {
        return false;
    }
    for (int i = 0; i < kImageExtensionCount; ++i)
    {
        if (ext == kImageExtensions[i])
        {
            return true;
        }
    }
    return false;
}

DropPathInfo ProbeDropPath(const std::wstring& path)
{
    DropPathInfo info;
    info.m_path = path;
    if (path.empty())
    {
        return info;   // 空路径当作不存在，调用方会把它归到跳过项
    }

    //只取属性、不打开文件 —— 拖进来的可能是只读介质上的文件，也可能正被别的
    //程序独占，此处不该因为打不开就判定它不存在。
    WIN32_FILE_ATTRIBUTE_DATA fad;
    ::ZeroMemory(&fad, sizeof(fad));
    if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad))
    {
        return info;
    }

    info.m_exists      = true;
    info.m_isDirectory = (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (!info.m_isDirectory)
    {
        info.m_sizeBytes = ((long long)fad.nFileSizeHigh << 32) | (long long)fad.nFileSizeLow;
    }
    return info;
}

DropPlan BuildDropPlan(const std::vector<DropPathInfo>& infos, bool forceAsFile)
{
    DropPlan plan;
    for (std::vector<DropPathInfo>::size_type i = 0; i < infos.size(); ++i)
    {
        const DropPathInfo& info = infos[i];

        if (!info.m_exists)
        {
            DropSkippedItem skipped;
            skipped.m_path   = info.m_path;
            skipped.m_reason = kDropSkipNotFound;
            plan.m_skipped.push_back(skipped);
            continue;
        }

        if (info.m_isDirectory)
        {
            //本期不支持发文件夹（已与用户对齐），一律跳过并给出原因。
            DropSkippedItem skipped;
            skipped.m_path   = info.m_path;
            skipped.m_reason = kDropSkipDirectory;
            plan.m_skipped.push_back(skipped);
            continue;
        }

        if (info.m_sizeBytes <= 0)
        {
            //空文件在发送链路那一层本来就会被拒（SendFileData 直接返回），
            //在这里先拦下来才能告诉用户为什么没发出去。
            DropSkippedItem skipped;
            skipped.m_path   = info.m_path;
            skipped.m_reason = kDropSkipEmpty;
            plan.m_skipped.push_back(skipped);
            continue;
        }

        DropSendItem item;
        item.m_path = info.m_path;
        item.m_kind = (!forceAsFile && IsImageDropPath(info.m_path))
                          ? kDropSendImage
                          : kDropSendFile;
        plan.m_items.push_back(item);
    }

    //确认阈值只看真正要发出去的条数：拖进来一堆文件夹却只有两个文件时，不该
    //因为"拖了很多东西"就拦一道。
    plan.m_needConfirm = ((int)plan.m_items.size() > kDropConfirmThreshold);
    return plan;
}

std::wstring DescribeDropSkipped(const std::vector<DropSkippedItem>& skipped)
{
    int dirCount     = 0;   // 文件夹
    int missingCount = 0;   // 路径已不存在
    int emptyCount   = 0;   // 0 字节文件
    for (std::vector<DropSkippedItem>::size_type i = 0; i < skipped.size(); ++i)
    {
        switch (skipped[i].m_reason)
        {
        case kDropSkipDirectory:
            ++dirCount;
            break;
        case kDropSkipEmpty:
            ++emptyCount;
            break;
        case kDropSkipNotFound:
        default:
            ++missingCount;
            break;
        }
    }

    std::wstring text;
    if (dirCount > 0)
    {
        text += std::to_wstring((long long)dirCount);
        text += L" 个文件夹暂不支持发送";
    }
    if (missingCount > 0)
    {
        if (!text.empty())
        {
            text += L"、";
        }
        text += std::to_wstring((long long)missingCount);
        text += L" 个文件已不存在";
    }
    if (emptyCount > 0)
    {
        if (!text.empty())
        {
            text += L"、";
        }
        text += std::to_wstring((long long)emptyCount);
        text += L" 个空文件未发送";
    }
    return text;
}

DropPlan BuildDropPlanFromPaths(const std::vector<std::wstring>& paths, bool forceAsFile)
{
    std::vector<DropPathInfo> infos;
    infos.reserve(paths.size());
    for (std::vector<std::wstring>::size_type i = 0; i < paths.size(); ++i)
    {
        infos.push_back(ProbeDropPath(paths[i]));
    }
    return BuildDropPlan(infos, forceAsFile);
}

} // namespace balloonchatwnd
