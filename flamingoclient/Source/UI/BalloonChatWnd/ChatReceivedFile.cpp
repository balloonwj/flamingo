/**
 *  ChatReceivedFile.cpp
 *
 *  单聊窗与群聊窗共用的文件消息处理，见 ChatReceivedFile.h。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"
#include "ChatReceivedFile.h"

#include <vector>
#include <shellapi.h>                          // ShellExecuteW（打开文件 / 打开所在文件夹）
#include "ChatMessageList.h"                   // MarkFileDownloadStartedByMsgid
#include "ChatFileLocation.h"                  // ResolveLocalFilePath（本地落点优先级与回退）
#include "ChatDirectFileModel.h"               // DirectRecordBubble：历史里的在线直传文件画成什么状态（2026-10-04 起）
#include "../../net/MessageProtocolClient.h"   // net::MessageNotify
#include "../../net/FileTransfer.h"            // net::FileTransfer（下载队列与缓存路径）
#include "../../net/CurrentUserAccount.h"      // net::CurrentUserAccount（缓存目录按登录账号区分）
#include "../BalloonHistory/FileLocalStore.h"  // 文件本地落点登记库（按 md5 + 文件名查路径）
#include "../../Common/Util/EncodeUtil.h"      // UTF-8 与 UTF-16 互转
#include "../../Common/Log/IULog.h"            // LOG_INFO / LOG_ERROR
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../../Common/Util/FileDialogFilter.h"   // filedlg::MakeFilter：文件对话框过滤串的说明文字逐段翻译

namespace balloonchatwnd {

namespace {

// UTF-8 std::string 转 CString（UTF-16）。空串安全。
CString Utf8ToCString(const std::string& utf8)
{
    if (utf8.empty())
    {
        return CString();
    }
    // UTF-16 码元个数不超过 UTF-8 字节数，按字节数 + 1 预留缓冲
    std::vector<wchar_t> buf(utf8.size() + 1, 0);
    EncodeUtil::Utf8ToUnicode(utf8.c_str(), buf.data(), (int)buf.size());
    return CString(buf.data());
}

// CString（UTF-16）转 UTF-8 std::string。空串安全。
std::string CStringToUtf8(const CString& text)
{
    const int wlen = text.GetLength();
    if (wlen <= 0)
    {
        return std::string();
    }
    // 一个 UTF-16 码元转成 UTF-8 最多 4 字节，按 4 倍 + 1 预留缓冲
    const int kUtf8BytesPerUnit = 4;
    std::vector<char> buf((size_t)wlen * kUtf8BytesPerUnit + 1, 0);
    EncodeUtil::UnicodeToUtf8(text.GetString(), buf.data(), (int)buf.size());
    return std::string(buf.data());
}

} // namespace

bool IsLocalFileUsable(const std::wstring& path, long long expectSize)
{
    if (path.empty())
    {
        return false;
    }
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad))
    {
        return false;
    }
    //同名目录不能当文件用
    if ((fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        return false;
    }
    //期望大小缺失（<=0）时退回只判存在；否则大小必须相符——下载到一半留下的残余文件、
    //或被改写过的同名文件都不能当作完整文件
    if (expectSize <= 0)
    {
        return true;
    }
    const ULONGLONG actualBytes =
        (static_cast<ULONGLONG>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
    return actualBytes == static_cast<ULONGLONG>(expectSize);
}

ChatMessage BuildReceivedFileMessage(bool fromMe, const net::MessageNotify& notify,
                                     bool directDownload, bool* cached)
{
    const CString fileName = Utf8ToCString(notify.m_fileName);

    //本地落点有两处可能：一是登记表里记下的路径（本端发出的源文件、或此前下载的落地
    //位置），二是按内容 MD5 推导的下载缓存路径。两处都可能失效（文件被移走、删除、
    //清了缓存），故各自查一次是否真实存在，再由 ResolveLocalFilePath 定优先级。
    std::wstring registeredPath;
    balloonhistory::CFileLocalStore::GetInstance().Query(
        notify.m_fileMd5, notify.m_fileName, registeredPath);

    const std::wstring cachePath = net::FileTransfer::CacheFilePathFor(
        net::CurrentUserAccount(), notify.m_fileMd5, notify.m_fileName);

    const bool registeredExists = IsLocalFileUsable(registeredPath, notify.m_fileSize);
    const bool cacheExists      = IsLocalFileUsable(cachePath, notify.m_fileSize);

    const std::wstring localPath = ResolveLocalFilePath(
        registeredPath, registeredExists, cachePath, cacheExists);
    const bool hit = !localPath.empty();

    //在线直传留下的文件消息：服务器上没有这个文件、不能下载。本机有文件（传完时按 MD5 登记过）就是完成态，
    //没有就显示「在线直传，服务器未保存」；被拒绝的显示作废原因
    if (notify.m_fileDirect)
    {
        int state = kFileDeleted;
        int directEnd = 0;
        DirectRecordBubble(notify.m_fileDirectResult, hit, state, directEnd);
        ChatMessage direct;
        if (state == kFileNormal)
        {
            direct = MakeFileMessage(fromMe, fileName, notify.m_fileSize, CString(localPath.c_str()),
                                     std::string(), kFileNormal);
        }
        else
        {
            direct = MakeIncomingFileMessage(fromMe, fileName, notify.m_fileSize, (long long)notify.m_msgid,
                                             std::string(), notify.m_fileMd5, state);
        }
        direct.fileMsgid           = (long long)notify.m_msgid;
        direct.fileMd5             = notify.m_fileMd5;
        direct.fileDirect          = true;
        direct.fileDirectSessionId = notify.m_fileDirectSessionId;
        direct.fileLinkPath        = notify.m_fileLinkPath;
        direct.fileDirectEnd       = directEnd;
        direct.fileDirectDownload  = directDownload;
        //发送方：直传成功即对方已收到，状态行与实时传完时一样写「对方已接收」
        direct.filePeerState       = DirectRecordPeerState(notify.m_fileDirectResult, fromMe, hit);
        if (cached != NULL)
        {
            *cached = (state == kFileNormal);
        }
        return direct;
    }

    ChatMessage msg;
    if (hit)
    {
        //本地已有这份文件：完成态气泡，路径填好（可直接打开、打开所在文件夹、另存为）
        msg = MakeFileMessage(fromMe, fileName, notify.m_fileSize, CString(localPath.c_str()),
                              std::string(), kFileNormal);
        msg.fileMsgid = (long long)notify.m_msgid;
        //完成态气泡也要带上 md5 与 fileid：文件日后被删掉、用户点「重新下载」时还要用
        msg.fileFileId = notify.m_fileFileId;
        msg.fileMd5    = notify.m_fileMd5;
    }
    else
    {
        //本地没有：待下载态气泡，不自动下载。收不收该由用户决定（大文件尤其如此）；而且
        //自己发出的文件从未进过下载缓存，翻历史时必然查不到，自动下载会把刚上传的文件
        //整个再取回来一遍
        msg = MakeIncomingFileMessage(fromMe, fileName, notify.m_fileSize,
                                      (long long)notify.m_msgid, notify.m_fileFileId,
                                      notify.m_fileMd5, kFilePendingAccept);
    }
    msg.fileDirectDownload = directDownload;
    //本端发出的文件：带上对方的收取状态。服务端只对拉取者自己发出的文件消息回填该字段，
    //离线期间对方的接收 / 拒绝正是靠它补看到的（实时的状态变化另经 cmd_file_transfer_ack 推送）
    msg.filePeerState = notify.m_fileAckState;

    if (cached != NULL)
    {
        *cached = hit;
    }
    return msg;
}

bool HandleLocalFileAction(HWND owner, const ChatFileActionData& d)
{
    switch (d.action)
    {
    //用关联程序打开本地文件
    case kChatFileOpen:
        if (!d.localPath.IsEmpty())
        {
            ::ShellExecuteW(owner, L"open", (LPCWSTR)d.localPath, NULL, NULL, SW_SHOWNORMAL);
        }
        return true;

    //在资源管理器里打开本地文件所在文件夹，并选中该文件（explorer /select）
    case kChatFileOpenFolder:
        if (!d.localPath.IsEmpty())
        {
            CString param;
            param.Format(_T("/select,\"%s\""), (LPCTSTR)d.localPath);
            ::ShellExecuteW(owner, L"open", L"explorer.exe", (LPCWSTR)param, NULL, SW_SHOWNORMAL);
        }
        return true;

    //把本地文件另存到用户选定位置（默认填原文件名）
    case kChatFileSaveAs:
        if (!d.localPath.IsEmpty())
        {
            wchar_t szSave[MAX_PATH] = { 0 };
            ::lstrcpynW(szSave, (LPCWSTR)d.fileName, MAX_PATH);
            OPENFILENAMEW ofn;
            ZeroMemory(&ofn, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = owner;
            //文件类型过滤串：说明文字按界面语言翻译后再拼成带 \0 的过滤串，须活到对话框关闭
            const std::wstring fileFilter = filedlg::MakeFilter(TR(L"所有文件"), L"*.*");
            ofn.lpstrFilter = fileFilter.c_str();
            ofn.lpstrFile   = szSave;
            ofn.nMaxFile    = MAX_PATH;
            ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
            if (::GetSaveFileNameW(&ofn))
            {
                ::CopyFileW((LPCWSTR)d.localPath, szSave, FALSE);
            }
        }
        return true;

    //其余动作（取消、接收、拒绝、重试）与会话有关，交还调用方
    default:
        return false;
    }
}

bool StartChatFileDownload(const ChatFileActionData& d, ChatMessageList* list)
{
    //下载靠 msgid 定位气泡、靠 fileid 向服务端取文件，缺一不可
    if (d.msgid <= 0 || d.fileId.empty())
    {
        LOG_ERROR("chat file download not started (missing locating info): msgid=%lld, fileid=%s.",
                  (long long)d.msgid, d.fileId.c_str());
        return false;
    }

    LOG_INFO("chat file download started: msgid=%lld, fileid=%s, size=%lld.",
             (long long)d.msgid, d.fileId.c_str(), (long long)d.sizeBytes);
    //落地按原始文件名命名，传输层要 UTF-8；气泡里存的是显示用的宽字符
    net::FileTransfer::GetInstance().EnqueueDownload(
        (int64_t)d.msgid, d.fileId, CStringToUtf8(d.fileName), d.md5, d.sizeBytes,
        net::CurrentUserAccount());

    //先把气泡切到「下载中」，随后的进度与结果按 msgid 回投更新本行
    if (list != NULL)
    {
        list->MarkFileDownloadStartedByMsgid(d.msgid);
    }
    return true;
}

} // namespace balloonchatwnd
