/**
 *  传输子会话四种角色的扩展点 ShareTransferHooks 的实现，见 ShareTransferHooks.h。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "ShareTransferHooks.h"

#include <windows.h>
#include <chrono>

#include "ShareBrowseModel.h"
#include "ShareControlFraming.h"
#include "ShareFileOps.h"
#include "SharePathGuard.h"
#include "ShareProtocolCodec.h"   //share_transfer_direction
#include "ShareTransferPlan.h"
#include "ShareWinPath.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/Common/Util/MD5Core.h"
#include "../../Source/net/Protocol.h"

namespace fileshare {

namespace {

//半成品文件的后缀（与文件夹传输接收端一致）
const wchar_t* const kPartSuffix = L".part";

//续传时重算前缀 MD5 每次读取的字节数
const DWORD kMd5ReadChunkBytes = 256 * 1024;

//64 位文件大小拆成两个 32 位字段时，高位字段的位移
const int kHighDwordShift = 32;

//当前时间（毫秒，单调递增）
int64_t NowMs()
{
    return static_cast<int64_t>(::GetTickCount64());
}

//相对路径（'/' 分隔）转成本机路径片段（'\' 分隔）
std::wstring ToLocalRel(const std::string& rel)
{
    std::wstring w;
    ShareUtf8ToWide(rel, w);
    for (size_t i = 0; i < w.size(); ++i)
    {
        if (w[i] == L'/')
            w[i] = L'\\';
    }
    return w;
}

//目录与片段拼成完整路径
std::wstring JoinLocal(const std::wstring& dir, const std::wstring& rel)
{
    if (dir.empty())
        return rel;
    return (dir[dir.size() - 1] == L'\\') ? dir + rel : dir + L"\\" + rel;
}

//本机文件的字节数；不存在返回 -1
int64_t LocalFileSize(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!::GetFileAttributesExW(ShareToLongPath(path).c_str(), GetFileExInfoStandard, &data))
        return -1;
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        return -1;
    return static_cast<int64_t>((static_cast<uint64_t>(data.nFileSizeHigh) << kHighDwordShift) | data.nFileSizeLow);
}

//读一个文件的前 bytes 字节算增量 MD5；读不满时返回 false
bool HashPrefix(const std::wstring& path, int64_t bytes, MD5_CTX& ctx)
{
    MD5Init(&ctx);
    HANDLE h = ::CreateFileW(ShareToLongPath(path).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                             FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    std::vector<unsigned char> buf(kMd5ReadChunkBytes);
    int64_t left = bytes;
    bool ok = true;
    while (left > 0)
    {
        const DWORD want = static_cast<DWORD>(left < static_cast<int64_t>(buf.size()) ? left : static_cast<int64_t>(buf.size()));
        DWORD got = 0;
        if (!::ReadFile(h, &buf[0], want, &got, NULL) || got != want)
        {
            ok = false;
            break;
        }
        MD5Update(&ctx, &buf[0], got);
        left -= got;
    }
    ::CloseHandle(h);
    return ok;
}

//解出一个 st_job 或 st_job_ack；不是这两种帧时返回 false
bool DecodeJobFrame(const char* data, size_t len, ShareControlMessage& msg)
{
    return ShareControlFraming::DecodeFrame(data, len, msg) && (msg.m_type == st_job || msg.m_type == st_job_ack);
}

//本机目录的完整路径是否以 prefix 开头（按目录边界，不区分大小写）
bool LocalUnder(const std::wstring& prefix, const std::wstring& path)
{
    return SharePathGuard::IsUnderRoot(prefix, path);
}

} // namespace

bool BuildJobFrame(int direction, const std::vector<std::string>& items, const std::string& toDir, std::string& frame)
{
    ShareControlMessage job;
    job.m_type = st_job;
    job.m_direction = direction;
    job.m_paths = items;
    job.m_toDir = toDir;
    return ShareControlFraming::BuildFrame(job, frame);
}

bool BuildJobAckFrame(int code, std::string& frame)
{
    ShareControlMessage ack;
    ack.m_type = st_job_ack;
    ack.m_code = code;
    return ShareControlFraming::BuildFrame(ack, frame);
}

//==================== CVisitorDownloadHooks ====================

CVisitorDownloadHooks::CVisitorDownloadHooks(int64_t shareId, const std::vector<std::string>& items,
                                             const std::wstring& targetDir, CShareLocalStore* store,
                                             const std::shared_ptr<foldertransfer::FolderReceiveState>& state)
    : m_shareId(shareId)
    , m_items(items)
    , m_targetDir(targetDir)
    , m_store(store)
    , m_state(state)
    , m_startMs(0)
    , m_lastSendMs(0)
    , m_acked(false)
{
    m_itemParent = items.empty() ? std::string() : ShareBrowseModel::ParentPath(items[0]);
}

int CVisitorDownloadHooks::OnLanesReady(foldertransfer::IFolderControlSender& sender)
{
    m_startMs = NowMs();
    SendJob(sender);
    return 0;
}

int CVisitorDownloadHooks::OnControlIdle(foldertransfer::IFolderControlSender& sender)
{
    if (m_acked)
        return 0;
    const int64_t now = NowMs();
    if (now - m_startMs >= kShareJobWaitMs)
        return error_code_share_owner_timeout;
    if (now - m_lastSendMs >= kShareJobResendMs)
        SendJob(sender);
    return 0;
}

int CVisitorDownloadHooks::OnControlFrame(foldertransfer::IFolderControlSender& sender, const std::string& frame)
{
    (void)sender;
    ShareControlMessage msg;
    if (!DecodeJobFrame(frame.data(), frame.size(), msg) || msg.m_type != st_job_ack)
        return 0;
    //重复的应答忽略
    if (m_acked)
        return 0;
    m_acked = true;
    if (msg.m_code != error_code_ok)
        LOG_WARNING("share download refused by owner, shareid: %lld, code: %d.", static_cast<long long>(m_shareId), msg.m_code);
    return msg.m_code;
}

int CVisitorDownloadHooks::PrepareRoot(const std::string& folderName, std::vector<std::string>& dirs,
                                       std::vector<foldertransfer::FolderRelayFileEntry>& files, std::wstring& rootPath)
{
    (void)folderName;
    const DWORD attrs = ::GetFileAttributesW(ShareToLongPath(m_targetDir).c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
        return error_code_share_io_fail;

    //文件在共享内的相对路径（续传记录的键）按改名之前的路径计算
    m_shareRelPaths.clear();
    for (size_t i = 0; i < files.size(); ++i)
        m_shareRelPaths.push_back(ShareBrowseModel::JoinPath(m_itemParent, files[i].m_relpath));

    //上次没下完的记录
    std::vector<ShareDownloadRecord> records;
    if (m_store != NULL)
        m_store->ListDownloads(m_shareId, records);

    std::set<std::string> dirSet(dirs.begin(), dirs.end());
    const std::vector<std::string> topNames = ShareTransferPlan::TopLevelNames(dirs, files);
    CShareDiskProbe probe;
    m_localTopNames.clear();
    for (size_t t = 0; t < topNames.size(); ++t)
    {
        const std::string& top = topNames[t];
        std::wstring topW;
        ShareUtf8ToWide(top, topW);
        const std::wstring topLocal = JoinLocal(m_targetDir, topW);

        //有续传记录、且记录的落地位置就在这个顶层名字下：沿用原名接着下载
        const std::string shareTop = ShareBrowseModel::JoinPath(m_itemParent, top);
        bool resumable = false;
        for (size_t r = 0; r < records.size() && !resumable; ++r)
        {
            const bool sameItem = records[r].m_relPath == shareTop
                                  || records[r].m_relPath.compare(0, shareTop.size() + 1, shareTop + "/") == 0;
            resumable = sameItem && LocalUnder(topLocal, records[r].m_localPath);
        }

        std::wstring chosen = topW;
        if (!resumable && probe.Exists(topLocal))
        {
            const bool isDir = dirSet.find(top) != dirSet.end();
            if (!ShareTransferPlan::ChooseFreeName(m_targetDir, topW, isDir, probe, chosen))
                return error_code_share_name_conflict;
        }
        m_localTopNames.push_back(chosen);
        if (chosen == topW)
            continue;

        //改名：只改本端的落地位置，协议按文件序号对应
        const std::string chosenUtf8 = ShareWideToUtf8(chosen);
        for (size_t i = 0; i < dirs.size(); ++i)
            dirs[i] = ShareTransferPlan::ReplaceTopLevel(dirs[i], top, chosenUtf8);
        for (size_t i = 0; i < files.size(); ++i)
            files[i].m_relpath = ShareTransferPlan::ReplaceTopLevel(files[i].m_relpath, top, chosenUtf8);
        LOG_INFO(L"share download renames a top-level item to avoid a conflict: %s.", chosen.c_str());
    }

    m_localPaths.clear();
    m_sizes.clear();
    m_mtimes.clear();
    for (size_t i = 0; i < files.size(); ++i)
    {
        m_localPaths.push_back(JoinLocal(m_targetDir, ToLocalRel(files[i].m_relpath)));
        m_sizes.push_back(files[i].m_size);
        m_mtimes.push_back(files[i].m_mtime);
    }
    for (size_t i = 0; i < files.size(); ++i)
        SeedResume(i);

    rootPath = m_targetDir;
    return 0;
}

void CVisitorDownloadHooks::OnPartFileOpened(const std::wstring& partPath)
{
    (void)partPath;
}

bool CVisitorDownloadHooks::ReplaceExistingOnFinish() const
{
    //落地名字已在 PrepareRoot 里避开了已有文件；万一期间用户自己建了同名文件，也不覆盖它
    return false;
}

size_t CVisitorDownloadHooks::GetFileCount() const
{
    return m_localPaths.size();
}

const std::string& CVisitorDownloadHooks::GetShareRelPath(size_t index) const
{
    return m_shareRelPaths[index];
}

const std::wstring& CVisitorDownloadHooks::GetLocalPath(size_t index) const
{
    return m_localPaths[index];
}

int64_t CVisitorDownloadHooks::GetFileSize(size_t index) const
{
    return m_sizes[index];
}

int64_t CVisitorDownloadHooks::GetFileMtime(size_t index) const
{
    return m_mtimes[index];
}

const std::vector<std::wstring>& CVisitorDownloadHooks::GetLocalTopNames() const
{
    return m_localTopNames;
}

bool CVisitorDownloadHooks::SendJob(foldertransfer::IFolderControlSender& sender)
{
    m_lastSendMs = NowMs();
    std::string frame;
    return BuildJobFrame(share_transfer_download, m_items, std::string(), frame) && sender.SendControl(frame);
}

void CVisitorDownloadHooks::SeedResume(size_t index)
{
    if (m_store == NULL || !m_state)
        return;
    std::wstring savedPath;
    int64_t offset = 0;
    //源文件的大小或修改时间变了：记录作废（GetResumePoint 内部删除），从头下载
    if (!m_store->GetResumePoint(m_shareId, m_shareRelPaths[index], m_sizes[index], m_mtimes[index], savedPath, offset))
        return;
    if (::CompareStringOrdinal(savedPath.c_str(), -1, m_localPaths[index].c_str(), -1, TRUE) != CSTR_EQUAL || offset <= 0)
        return;

    //上次已经下完的文件：正式文件还在、大小相符就跳过
    if (offset >= m_sizes[index])
    {
        if (LocalFileSize(m_localPaths[index]) == m_sizes[index])
            m_state->MarkFileCompleted(static_cast<int64_t>(index));
        return;
    }

    //半截文件：按记录的位点重算前缀 MD5，状态机随后按 .part 的实际长度核对
    const std::wstring part = m_localPaths[index] + kPartSuffix;
    if (LocalFileSize(part) < offset)
        return;
    MD5_CTX ctx;
    if (!HashPrefix(part, offset, ctx))
        return;
    m_state->StoreFileProgress(static_cast<int64_t>(index), offset, ctx);
    LOG_INFO("share download resumes a file, shareid: %lld, path: %s, offset: %lld.", static_cast<long long>(m_shareId),
             m_shareRelPaths[index].c_str(), static_cast<long long>(offset));
}

//==================== CVisitorUploadHooks ====================

CVisitorUploadHooks::CVisitorUploadHooks(const std::vector<std::string>& topNames, const std::string& toDir, int waitMs)
    : m_topNames(topNames)
    , m_toDir(toDir)
    , m_waitMs(waitMs)
    , m_acked(false)
    , m_ackCode(0)
    , m_stopping(false)
{
}

int CVisitorUploadHooks::RunPreamble(foldertransfer::IFolderControlSender& sender, foldertransfer::FolderManifest& manifest,
                                     std::wstring& rootPath)
{
    (void)manifest;
    (void)rootPath;
    std::string frame;
    if (!BuildJobFrame(share_transfer_upload, m_topNames, m_toDir, frame))
        return error_code_bad_request;

    const int64_t deadline = NowMs() + m_waitMs;
    std::unique_lock<std::mutex> lock(m_mutex);
    while (true)
    {
        if (m_acked)
            return m_ackCode;
        if (m_stopping)
            return error_code_share_transfer_canceled;
        if (NowMs() >= deadline)
            return error_code_share_owner_timeout;
        //发送不持锁
        lock.unlock();
        sender.SendControl(frame);
        lock.lock();
        m_cv.wait_for(lock, std::chrono::milliseconds(kShareJobResendMs));
    }
}

void CVisitorUploadHooks::OnControlFrame(const unsigned char* data, size_t len)
{
    ShareControlMessage msg;
    if (!DecodeJobFrame(reinterpret_cast<const char*>(data), len, msg) || msg.m_type != st_job_ack)
        return;
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_acked)
        return;
    m_acked = true;
    m_ackCode = msg.m_code;
    m_cv.notify_all();
}

void CVisitorUploadHooks::OnStopping()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_stopping = true;
    m_cv.notify_all();
}

//==================== COwnerDownloadHooks ====================

COwnerDownloadHooks::COwnerDownloadHooks(const ShareLocalRecord& share, int waitMs)
    : m_share(share)
    , m_waitMs(waitMs)
    , m_jobReceived(false)
    , m_jobDirection(0)
    , m_answered(false)
    , m_answerCode(0)
    , m_sender(NULL)
    , m_stopping(false)
{
}

int COwnerDownloadHooks::RunPreamble(foldertransfer::IFolderControlSender& sender, foldertransfer::FolderManifest& manifest,
                                     std::wstring& rootPath)
{
    std::vector<std::string> items;
    int direction = 0;
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_sender = &sender;
        const int64_t deadline = NowMs() + m_waitMs;
        while (!m_jobReceived && !m_stopping)
        {
            const int64_t left = deadline - NowMs();
            if (left <= 0)
                break;
            m_cv.wait_for(lock, std::chrono::milliseconds(left));
        }
        if (m_stopping)
            return error_code_share_transfer_canceled;
        if (!m_jobReceived)
            return error_code_share_owner_timeout;
        items = m_items;
        direction = m_jobDirection;
    }

    int code = error_code_ok;
    foldertransfer::FolderManifest built;
    std::wstring sourceRoot;
    if (direction != share_transfer_download)
        code = error_code_bad_request;
    else
        code = ShareTransferPlan::BuildDownloadManifest(m_share.m_rootPath, m_share.m_kind, m_share.m_includeHidden, items,
                                                        built, sourceRoot);

    std::string ack;
    if (BuildJobAckFrame(code, ack))
        sender.SendControl(ack);
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_answered = true;
        m_answerCode = code;
    }
    LOG_INFO("share download job answered, shareid: %lld, items: %u, files: %u, code: %d.",
             static_cast<long long>(m_share.m_shareId), static_cast<unsigned>(items.size()),
             static_cast<unsigned>(built.m_files.size()), code);
    if (code != error_code_ok)
        return code;
    {
        //记下各文件在共享内的路径，供传输结束后审计上报
        std::lock_guard<std::mutex> guard(m_mutex);
        const std::string parent = items.empty() ? std::string() : ShareBrowseModel::ParentPath(items[0]);
        m_filePaths.clear();
        m_fileSizes.clear();
        for (size_t i = 0; i < built.m_files.size(); ++i)
        {
            m_filePaths.push_back(ShareBrowseModel::JoinPath(parent, built.m_files[i].m_relativePath));
            m_fileSizes.push_back(built.m_files[i].m_sizeBytes);
        }
    }
    manifest = built;
    rootPath = sourceRoot;
    return 0;
}

void COwnerDownloadHooks::OnControlFrame(const unsigned char* data, size_t len)
{
    ShareControlMessage msg;
    if (!DecodeJobFrame(reinterpret_cast<const char*>(data), len, msg) || msg.m_type != st_job)
        return;
    foldertransfer::IFolderControlSender* sender = NULL;
    int code = 0;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (!m_jobReceived)
        {
            m_jobReceived = true;
            m_jobDirection = msg.m_direction;
            m_items = msg.m_paths;
            m_cv.notify_all();
            return;
        }
        //重复的 st_job：已经应答过就按原结果再应答一次（访问者可能还没收到）
        if (!m_answered || m_sender == NULL)
            return;
        sender = m_sender;
        code = m_answerCode;
    }
    std::string ack;
    if (BuildJobAckFrame(code, ack))
        sender->SendControl(ack);
}

void COwnerDownloadHooks::OnStopping()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_stopping = true;
    m_cv.notify_all();
}

std::vector<std::string> COwnerDownloadHooks::GetItems() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_items;
}

void COwnerDownloadHooks::GetFiles(std::vector<std::string>& paths, std::vector<int64_t>& sizes) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    paths = m_filePaths;
    sizes = m_fileSizes;
}

std::string COwnerDownloadHooks::GetItemParent() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_items.empty() ? std::string() : ShareBrowseModel::ParentPath(m_items[0]);
}

//==================== COwnerUploadHooks ====================

COwnerUploadHooks::COwnerUploadHooks(const ShareLocalRecord& share, int perms, int waitMs)
    : m_share(share)
    , m_permission(share.m_kind, perms)
    , m_waitMs(waitMs)
    , m_startMs(0)
    , m_answered(false)
    , m_answerCode(0)
{
}

int COwnerUploadHooks::OnLanesReady(foldertransfer::IFolderControlSender& sender)
{
    (void)sender;
    m_startMs = NowMs();
    return 0;
}

int COwnerUploadHooks::OnControlIdle(foldertransfer::IFolderControlSender& sender)
{
    (void)sender;
    std::lock_guard<std::mutex> guard(m_mutex);
    if (!m_answered && NowMs() - m_startMs >= m_waitMs)
        return error_code_share_owner_timeout;
    return 0;
}

int COwnerUploadHooks::OnControlFrame(foldertransfer::IFolderControlSender& sender, const std::string& frame)
{
    ShareControlMessage msg;
    if (!DecodeJobFrame(frame.data(), frame.size(), msg) || msg.m_type != st_job)
        return 0;

    int code = error_code_ok;
    bool repeat = false;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        repeat = m_answered;
        code = m_answerCode;
    }
    if (!repeat)
    {
        //依次校验：方向、上传权限（以分享者手里的最新权限为准）、目标目录
        std::wstring target;
        if (msg.m_direction != share_transfer_upload)
            code = error_code_bad_request;
        if (code == error_code_ok)
            code = m_permission.Check(share_op_upload);
        if (code == error_code_ok && !SharePathGuard::ResolveUnderRoot(m_share.m_rootPath, msg.m_toDir, false, target))
            code = error_code_share_path_invalid;
        if (code == error_code_ok && !ShareFileOps::IsRootAvailable(m_share.m_rootPath, m_share.m_kind))
            code = error_code_share_root_missing;
        if (code == error_code_ok && !SharePathGuard::IsReparseSafe(m_share.m_rootPath, target))
            code = error_code_share_path_invalid;
        if (code == error_code_ok && !m_share.m_includeHidden && ShareFileOps::HasHiddenComponent(m_share.m_rootPath, target))
            code = error_code_share_path_not_found;
        if (code == error_code_ok)
        {
            ShareEntry entry;
            const int stat = ShareFileOps::StatEntry(target, entry);
            if (stat != error_code_ok)
                code = stat;
            else if (!entry.m_isDir)
                code = error_code_share_path_not_found;
        }
        std::lock_guard<std::mutex> guard(m_mutex);
        m_answered = true;
        m_answerCode = code;
        m_toDir = msg.m_toDir;
        m_targetDir = target;
        LOG_INFO("share upload job answered, shareid: %lld, todir: %s, code: %d.", static_cast<long long>(m_share.m_shareId),
                 msg.m_toDir.c_str(), code);
    }

    std::string ack;
    if (BuildJobAckFrame(code, ack))
        sender.SendControl(ack);
    return code;
}

int COwnerUploadHooks::PrepareRoot(const std::string& folderName, std::vector<std::string>& dirs,
                                   std::vector<foldertransfer::FolderRelayFileEntry>& files, std::wstring& rootPath)
{
    (void)folderName;
    std::wstring target;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (!m_answered || m_answerCode != error_code_ok)
            return error_code_bad_request;
        target = m_targetDir;
    }
    //传输中途权限被收回：不再写入
    const int permCode = m_permission.Check(share_op_upload);
    if (permCode != error_code_ok)
        return permCode;

    //顶层条目与目标目录里已有的条目同名：整个上传拒绝，一个字节都不写（方案 8.4）
    const std::vector<std::string> topNames = ShareTransferPlan::TopLevelNames(dirs, files);
    CShareDiskProbe probe;
    const std::string conflict = ShareTransferPlan::FindTopLevelConflict(target, topNames, probe);
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_topNames = topNames;
        m_filePaths.clear();
        m_fileSizes.clear();
        for (size_t i = 0; i < files.size(); ++i)
        {
            m_filePaths.push_back(ShareBrowseModel::JoinPath(m_toDir, files[i].m_relpath));
            m_fileSizes.push_back(files[i].m_size);
        }
    }
    if (!conflict.empty())
    {
        LOG_INFO("share upload refused, name conflict: %s, shareid: %lld.", conflict.c_str(),
                 static_cast<long long>(m_share.m_shareId));
        return error_code_share_name_conflict;
    }
    rootPath = target;
    return 0;
}

void COwnerUploadHooks::OnPartFileOpened(const std::wstring& partPath)
{
    //半成品设隐藏属性，并登记到正在上传的表里：列目录时据此滤掉，其他访问者看不到
    const std::wstring longPath = ShareToLongPath(partPath);
    const DWORD attrs = ::GetFileAttributesW(longPath.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES)
        ::SetFileAttributesW(longPath.c_str(), attrs | FILE_ATTRIBUTE_HIDDEN);
    CShareUploadRegistry::Instance().Add(partPath);
    std::lock_guard<std::mutex> guard(m_mutex);
    m_partFiles.push_back(partPath);
}

void COwnerUploadHooks::OnFileFinalized(const std::wstring& partPath, const std::wstring& finalPath)
{
    //改名保留了半成品的隐藏属性：不去掉的话正式文件在列目录时一直被当成隐藏文件滤掉
    const std::wstring longPath = ShareToLongPath(finalPath);
    const DWORD attrs = ::GetFileAttributesW(longPath.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_HIDDEN) != 0)
        ::SetFileAttributesW(longPath.c_str(), attrs & ~static_cast<DWORD>(FILE_ATTRIBUTE_HIDDEN));
    //半成品已不存在：登记表里的这一项不再需要，会话结束时 CleanupUploadParts 再删一次也无妨
    CShareUploadRegistry::Instance().Remove(partPath);
}

bool COwnerUploadHooks::ReplaceExistingOnFinish() const
{
    //两名访问者同时上传同名文件时，后改名的那个失败，不覆盖先到的（方案 8.4）
    return false;
}

void COwnerUploadHooks::SetPerms(int perms)
{
    m_permission.SetPerms(perms);
}

std::vector<std::wstring> COwnerUploadHooks::GetPartFiles() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_partFiles;
}

std::string COwnerUploadHooks::GetToDir() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_toDir;
}

std::wstring COwnerUploadHooks::GetTargetDir() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_targetDir;
}

std::vector<std::string> COwnerUploadHooks::GetTopNames() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_topNames;
}

void COwnerUploadHooks::GetFiles(std::vector<std::string>& paths, std::vector<int64_t>& sizes) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    paths = m_filePaths;
    sizes = m_fileSizes;
}

} // namespace fileshare
