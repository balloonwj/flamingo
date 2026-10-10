/**
 *  FlamingoMedia 里的云端录像中心，职责见 CloudRecordCenter.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "CloudRecordCenter.h"

#include <stdio.h>
#include <shellapi.h>
#include <shlobj.h>
#include "CloudRecordComposer.h"
#include "../Share/ShareCornerToast.h"
#include "../../Source/Common/I18n/I18n.h"   // TR / TRF：界面文字的多语言查找
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/net/Protocol.h"

namespace screenrecord {

namespace {

//消息窗口的类名
const wchar_t* const kWindowClass = L"FlamingoMediaCloudRecordCenter";
//进度的上限（百分比）
const int kProgressFull = 100;
//本机目录（%LOCALAPPDATA% 下）
const wchar_t* const kLocalRoot = L"\\Flamingo\\CloudRecords";
const wchar_t* const kWorkSub = L"\\work";
//没传完的上传清单的文件名前缀与扩展名（后接本人 userid）
const wchar_t* const kPendingPrefix = L"\\pending_";
const wchar_t* const kPendingExt = L".json";
//读清单时一次读入的字节数
const size_t kReadChunk = 4096;
const wchar_t* const kCacheSub = L"\\cache";
//暂停清单的文件名前缀（后接本人 userid 与 kPendingExt，2026-10-07）
const wchar_t* const kPausedPrefix = L"\\paused_";

std::wstring utf8ToWide(const std::string& s)
{
    if (s.empty())
        return std::wstring();
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), NULL, 0);
    std::wstring out(static_cast<size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0)
        ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], n);
    return out;
}

std::string wideToUtf8(const std::wstring& s)
{
    if (s.empty())
        return std::string();
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), NULL, 0, NULL, NULL);
    std::string out(static_cast<size_t>(n > 0 ? n : 0), '\0');
    if (n > 0)
        ::WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], n, NULL, NULL);
    return out;
}

bool fileSize(const std::wstring& path, int64_t& bytes)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        return false;
    ULARGE_INTEGER size;
    size.LowPart = data.nFileSizeLow;
    size.HighPart = data.nFileSizeHigh;
    bytes = static_cast<int64_t>(size.QuadPart);
    return true;
}

std::wstring baseNameNoExt(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    std::wstring name = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
    const size_t dot = name.rfind(L'.');
    if (dot != std::wstring::npos)
        name = name.substr(0, dot);
    return name;
}

} // namespace

CCloudRecordCenter::ComposeJob::ComposeJob()
    : m_recordId(0)
    , m_stage(kComposeWaitFrecToken)
    , m_transferId(0)
    , m_mp4Bytes(0)
{
}

CCloudRecordCenter::UploadJob::UploadJob()
    : m_recordId(0)
    , m_audioOnly(false)
    , m_durationMs(0)
    , m_startTime(0)
    , m_bytes(0)
    , m_transferId(0)
    , m_waitingToken(true)
{
}

CCloudRecordCenter::OpenJob::OpenJob()
    : m_recordId(0)
    , m_transferId(0)
{
}

CCloudRecordCenter::DownloadJob::DownloadJob()
    : m_recordId(0)
    , m_transferId(0)
{
}

CCloudRecordCenter::CCloudRecordCenter()
    : m_host(NULL)
    , m_hWnd(NULL)
    , m_listedOnce(false)
    , m_composing(0)
    , m_nextPickToken(1)
    , m_toastRecordId(0)
    , m_observer(NULL)
    , m_listLoaded(false)
    , m_listHasMore(false)
    , m_listEnabled(false)
    , m_listUsedBytes(0)
    , m_listQuotaBytes(0)
    , m_listRetainDays(0)
    , m_progressTimerOn(false)
    , m_pausedLoaded(false)
    , m_toastPauseRecordId(0)
{
}

CCloudRecordCenter::~CCloudRecordCenter()
{
    Shutdown();
}

void CCloudRecordCenter::Init(flamingomedia::IMediaHost* host)
{
    m_host = host;
    ensureWindow();
}

bool CCloudRecordCenter::ensureWindow()
{
    if (m_hWnd != NULL)
        return true;
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &CCloudRecordCenter::WndProc;
    wc.hInstance = ::GetModuleHandleW(NULL);
    wc.lpszClassName = kWindowClass;
    ::RegisterClassExW(&wc);
    m_hWnd = ::CreateWindowExW(0, kWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, this);
    if (m_hWnd == NULL)
    {
        LOG_ERROR("cloud record center: create message window failed, error %lu.", ::GetLastError());
        return false;
    }
    ::SetWindowLongPtrW(m_hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    return true;
}

LRESULT CALLBACK CCloudRecordCenter::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    CCloudRecordCenter* self = reinterpret_cast<CCloudRecordCenter*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (self != NULL)
    {
        if (msg == kMsgComposeDone)
        {
            CCloudRecordComposeTask* task = reinterpret_cast<CCloudRecordComposeTask*>(wParam);
            if (task != NULL)
                self->onComposeDone(task->RecordId(), static_cast<int>(lParam));
            return 0;
        }
        if (msg == kMsgComposeNames)
        {
            self->ResolveNames(reinterpret_cast<CCloudRecordComposeTask*>(lParam));
            return 0;
        }
        if (msg == kMsgToastCommand)
        {
            //右下角提示的主按钮：「播放」（「已可观看」的提示）、「打开所在文件夹」（「已下载」的提示）
            //或「暂停」（「正在生成云端录像」的提示，2026-10-07）
            if (static_cast<int>(wParam) == screenshare::CShareCornerToast::kCmdJoin)
            {
                if (self->m_toastPauseRecordId > 0)
                {
                    self->Pause(self->m_toastPauseRecordId);
                }
                else if (self->m_toastRecordId > 0)
                {
                    self->OpenRecord(self->m_toastRecordId, std::wstring());
                }
                else if (!self->m_toastFolderPath.empty())
                {
                    const std::wstring param = L"/select,\"" + self->m_toastFolderPath + L"\"";
                    ::ShellExecuteW(NULL, L"open", L"explorer.exe", param.c_str(), NULL, SW_SHOWNORMAL);
                }
            }
            if (self->m_toast)
                self->m_toast->Hide();
            self->m_toastRecordId = 0;
            self->m_toastPauseRecordId = 0;
            self->m_toastFolderPath.clear();
            return 0;
        }
        if (msg == WM_TIMER && wParam == kProgressTimerId)
        {
            self->onProgressTimer();
            return 0;
        }
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

void CCloudRecordCenter::Shutdown()
{
    m_observer = NULL;
    OnLogout();
    if (m_toast)
    {
        m_toast->Hide();
        m_toast.reset();
    }
    if (m_hWnd != NULL)
    {
        ::DestroyWindow(m_hWnd);
        m_hWnd = NULL;
    }
    m_host = NULL;
}

void CCloudRecordCenter::OnLogout()
{
    //合成线程：取消并等它退出（它在分段边界检查取消标志）
    for (std::map<int64_t, ComposeJob>::iterator it = m_composeJobs.begin(); it != m_composeJobs.end(); ++it)
    {
        if (it->second.m_task)
        {
            it->second.m_task->Cancel();
            it->second.m_task->Join();
        }
        if (it->second.m_transferId != 0 && m_host != NULL)
            m_host->CancelCloudTransfer(it->second.m_transferId);
    }
    for (size_t i = 0; i < m_uploadJobs.size(); ++i)
    {
        if (m_uploadJobs[i].m_transferId != 0 && m_host != NULL)
            m_host->CancelCloudTransfer(m_uploadJobs[i].m_transferId);
    }
    for (std::map<int64_t, OpenJob>::iterator it = m_openJobs.begin(); it != m_openJobs.end(); ++it)
    {
        if (it->second.m_transferId != 0 && m_host != NULL)
            m_host->CancelCloudTransfer(it->second.m_transferId);
    }
    for (std::map<int64_t, DownloadJob>::iterator it = m_downloadJobs.begin(); it != m_downloadJobs.end(); ++it)
    {
        if (it->second.m_transferId != 0 && m_host != NULL)
            m_host->CancelCloudTransfer(it->second.m_transferId);
    }
    if (!m_composeJobs.empty() || !m_uploadJobs.empty())
        LOG_INFO("cloud record center: logout, %u compose jobs and %u uploads dropped, redone after next login.",
                 static_cast<unsigned>(m_composeJobs.size()), static_cast<unsigned>(m_uploadJobs.size()));
    m_composeJobs.clear();
    m_composeQueue.clear();
    m_composing = 0;
    m_uploadJobs.clear();
    m_openJobs.clear();
    m_sendPicks.clear();
    m_listedOnce = false;
    m_downloadJobs.clear();
    m_transferProgress.clear();
    m_composeFailed.clear();
    m_list.clear();
    m_listRequests.clear();
    m_listLoaded = false;
    m_listHasMore = false;
    m_listEnabled = false;
    m_listUsedBytes = 0;
    m_listQuotaBytes = 0;
    m_listRetainDays = 0;
    //暂停清单按账号存在本机：换账号后重新读
    m_paused.clear();
    m_pausedLoaded = false;
    m_toastPauseRecordId = 0;
    notifyChanged();
}

void CCloudRecordCenter::OnConnectionStateChanged(bool online)
{
    if (m_host == NULL)
        return;
    if (!online)
    {
        //断线时还在等应答的请求不会再有应答。「打开」撤掉并提示；「保存到云端」还没拿到录像 id 的撤掉并提示，
        //不在重连后重发：第一次请求可能已经在服务端建了记录，重发会多出一条
        bool openDropped = false;
        for (std::map<int64_t, OpenJob>::iterator it = m_openJobs.begin(); it != m_openJobs.end();)
        {
            if (it->second.m_transferId == 0)
            {
                it = m_openJobs.erase(it);
                openDropped = true;
            }
            else
            {
                ++it;
            }
        }
        for (std::map<int64_t, DownloadJob>::iterator it = m_downloadJobs.begin(); it != m_downloadJobs.end();)
        {
            if (it->second.m_transferId == 0)
            {
                it = m_downloadJobs.erase(it);
                openDropped = true;
            }
            else
            {
                ++it;
            }
        }
        m_listRequests.clear();
        bool uploadDropped = false;
        for (size_t i = m_uploadJobs.size(); i > 0; --i)
        {
            if (m_uploadJobs[i - 1].m_waitingToken)
            {
                //续传的（已有录像 id）还在本机清单里，下次读到列表时再续，不提示
                if (m_uploadJobs[i - 1].m_recordId == 0)
                    uploadDropped = true;
                m_uploadJobs.erase(m_uploadJobs.begin() + static_cast<std::ptrdiff_t>(i - 1));
            }
        }
        if (uploadDropped)
            showError(TR(L"上传到云端失败，录像仍保存在本机，可以在录制管理里重新上传。"));
        else if (openDropped)
            showError(TR(L"现在不能播放，请在连接恢复后再试。"));
        //合成保留：令牌请求可以重发，重连后接着做。重连后再列一次录像
        m_listedOnce = false;
        LOG_INFO("cloud record center: offline, open dropped %d, upload dropped %d, compose kept %u.", openDropped ? 1 : 0,
                 uploadDropped ? 1 : 0, static_cast<unsigned>(m_composeJobs.size()));
        notifyChanged();
        return;
    }
    //暂停清单（2026-10-07）：登录后先读入，下面补做合成、续传时跳过暂停了的
    if (!m_pausedLoaded)
        loadPaused();
    if (m_listedOnce)
        return;
    //断线前（或发送失败时）在等令牌的合成：重发令牌请求
    if (m_composing != 0)
    {
        std::map<int64_t, ComposeJob>::iterator it = m_composeJobs.find(m_composing);
        if (it != m_composeJobs.end()
            && (it->second.m_stage == kComposeWaitFrecToken || it->second.m_stage == kComposeWaitUpToken))
        {
            requestComposeToken(it->second);
        }
    }
    //登录或重连后列一次本人的录像：没合成完的（等待合成）接着做；录制管理窗口开着时也用这一页刷新列表
    m_listedOnce = RequestList(false);
}

bool CCloudRecordCenter::IsCloudRecordSignal(uint16_t cmd)
{
    return cmd >= cmd_cloudrecord_list && cmd <= cmd_cloudrecord_notify;
}

bool CCloudRecordCenter::send(uint16_t cmd, const std::string& json)
{
    if (m_host == NULL)
        return false;
    if (!m_host->SendSignal(cmd, json.c_str(), static_cast<uint32_t>(json.size())))
    {
        LOG_WARNING("cloud record center: send cmd %u failed.", static_cast<unsigned>(cmd));
        return false;
    }
    return true;
}

bool CCloudRecordCenter::OnSignal(uint16_t cmd, const std::string& json)
{
    switch (cmd)
    {
    //列表的应答：补做没合成的
    case cmd_cloudrecord_list:
        onList(json);
        return true;

    //操作的应答：令牌、发到会话
    case cmd_cloudrecord_op:
        onOp(json);
        return true;

    //状态变化、即将到期、已删除
    case cmd_cloudrecord_notify:
        onNotify(json);
        return true;

    //其余命令不归本类
    default:
        return false;
    }
}

void CCloudRecordCenter::onList(const std::string& json)
{
    //应答与请求按发出的顺序一一对应（同一条主连接、服务端按序处理）
    bool more = false;
    if (!m_listRequests.empty())
    {
        more = m_listRequests.front();
        m_listRequests.pop_front();
    }
    CloudRecordListResult r;
    if (!ParseCloudRecordList(json, r) || r.m_code != 0)
    {
        LOG_WARNING("cloud record center: list failed, code %d.", r.m_code);
        return;
    }
    //第一页替换整个列表，更早的一页接在后面（与已有的同 id 去重）
    if (!more)
        m_list.clear();
    for (size_t i = 0; i < r.m_records.size(); ++i)
        applyRecord(r.m_records[i]);
    m_listLoaded = true;
    m_listHasMore = r.m_hasMore;
    m_listEnabled = r.m_enabled;
    m_listUsedBytes = r.m_usedBytes;
    m_listQuotaBytes = r.m_quotaBytes;
    m_listRetainDays = r.m_retainDays;
    const int64_t self = m_host != NULL ? m_host->GetSelfUserId() : 0;
    int pending = 0;
    for (size_t i = 0; i < r.m_records.size(); ++i)
    {
        const CloudRecordInfo& info = r.m_records[i];
        if (info.m_owner == self && info.m_source == cloudrecord_source_share &&
            info.m_status == cloudrecord_status_pending)
        {
            enqueueCompose(info);
            ++pending;
        }
    }
    LOG_INFO("cloud record center: listed %u records (more %d), %d waiting for compose.",
             static_cast<unsigned>(r.m_records.size()), more ? 1 : 0, pending);
    //第一页：本机清单里没传完的上传接着传（27d-4）
    if (!more)
        resumePendingUploads();
    notifyChanged();
}

void CCloudRecordCenter::onNotify(const std::string& json)
{
    CloudRecordNotify n;
    if (!ParseCloudRecordNotify(json, n))
        return;
    LOG_INFO("cloud record center: notify type %d, record %lld, status %d.", n.m_type,
             static_cast<long long>(n.m_recordId), n.m_hasRecord ? n.m_record.m_status : 0);
    //列表先跟着变：有整条录像的替换，已删除的去掉（另一台电脑上删除、到期清理）
    if (n.m_hasRecord)
        applyRecord(n.m_record);
    if (n.m_type == cloudrecord_notify_delete)
    {
        cancelLocalWork(n.m_recordId);
        removeRecord(n.m_recordId);
        notifyChanged();
        return;
    }
    if (n.m_type == cloudrecord_notify_status && n.m_hasRecord)
    {
        notifyChanged();
        //录制结束、等待合成：开始合成
        if (n.m_record.m_status == cloudrecord_status_pending && n.m_record.m_source == cloudrecord_source_share)
        {
            enqueueCompose(n.m_record);
            return;
        }
        //上传完、可观看：合成的收尾，提示「已可观看」
        if (n.m_record.m_status == cloudrecord_status_ready)
        {
            std::map<int64_t, ComposeJob>::iterator it = m_composeJobs.find(n.m_recordId);
            const bool composed = (it != m_composeJobs.end());
            if (composed)
            {
                ::DeleteFileW(it->second.m_frecPath.c_str());
                //合成出的 MP4 挪进播放缓存：开启者随后点「播放」时不必再下载
                const std::wstring cached = cacheDir() + L"\\rec_" +
                                            std::to_wstring(static_cast<long long>(n.m_recordId)) + L".mp4";
                ::MoveFileExW(it->second.m_mp4Path.c_str(), cached.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
                m_composeJobs.erase(it);
            }
            for (size_t i = 0; i < m_uploadJobs.size(); ++i)
            {
                if (m_uploadJobs[i].m_recordId == n.m_recordId)
                {
                    m_uploadJobs.erase(m_uploadJobs.begin() + static_cast<std::ptrdiff_t>(i));
                    break;
                }
            }
            removePending(n.m_recordId);
            showToast(TRF(L"云端录像「{0}」已可观看。", utf8ToWide(n.m_record.m_title)), n.m_recordId, true);
            return;
        }
        //录制出错（一帧都没录到）
        if (n.m_record.m_status == cloudrecord_status_failed)
            showToast(TRF(L"云端录像「{0}」没有录到任何内容。", utf8ToWide(n.m_record.m_title)), 0, false);
        return;
    }
    if (n.m_type == cloudrecord_notify_expire && n.m_hasRecord)
    {
        notifyChanged();
        showToast(TRF(L"云端录像「{0}」将在 3 天内到期删除，需要保留请及时下载。", utf8ToWide(n.m_record.m_title)),
                  n.m_recordId, true);
        return;
    }
}

void CCloudRecordCenter::enqueueCompose(const CloudRecordInfo& info)
{
    if (m_composeJobs.find(info.m_recordId) != m_composeJobs.end())
        return;
    //用户暂停了的不自动做（2026-10-07）：点「继续」时先从暂停清单去掉再加入
    if (m_paused.find(info.m_recordId) != m_paused.end())
    {
        LOG_INFO("cloud record center: record %lld is paused, compose not queued.", static_cast<long long>(info.m_recordId));
        return;
    }
    m_composeFailed.erase(info.m_recordId);
    ComposeJob job;
    job.m_recordId = info.m_recordId;
    job.m_title = utf8ToWide(info.m_title);
    m_composeJobs[info.m_recordId] = job;
    m_composeQueue.push_back(info.m_recordId);
    LOG_INFO("cloud record center: compose queued, record %lld, queue %u.", static_cast<long long>(info.m_recordId),
             static_cast<unsigned>(m_composeQueue.size()));
    startNextCompose();
    notifyChanged();
}

void CCloudRecordCenter::startNextCompose()
{
    if (m_composing != 0 || m_composeQueue.empty())
        return;
    m_composing = m_composeQueue.front();
    m_composeQueue.pop_front();
    ComposeJob& job = m_composeJobs[m_composing];
    job.m_stage = kComposeWaitFrecToken;
    //发不出去时（主连接断开）合成留在这一阶段，重连后由 OnConnectionStateChanged 重发
    if (!requestComposeToken(job))
        return;
    //第 56 条：开始合成时提示，完成上传前不要退出；2026-10-07 起提示上带「暂停」
    showPauseToast(TR(L"正在生成云端录像，完成上传前请不要退出 Flamingo。"), m_composing);
}

bool CCloudRecordCenter::requestComposeToken(const ComposeJob& job)
{
    if (job.m_stage == kComposeWaitFrecToken)
        return send(cmd_cloudrecord_op, BuildCloudRecordDownloadToken(job.m_recordId, kCloudFileFrec));
    if (job.m_stage == kComposeWaitUpToken)
        return send(cmd_cloudrecord_op, BuildCloudRecordUploadToken(job.m_recordId, job.m_mp4Bytes, false, std::string(), 0, 0));
    return false;
}

void CCloudRecordCenter::onOp(const std::string& json)
{
    CloudRecordOpResult r;
    if (!ParseCloudRecordOpResult(json, r))
        return;
    switch (r.m_op)
    {
    //下载令牌：合成取原始帧，或打开取最终文件
    case cloudrecord_op_download_token:
        onDownloadToken(r);
        break;

    //上传令牌：合成后的上传，或保存到云端
    case cloudrecord_op_upload_token:
        onUploadToken(r);
        break;

    //发到会话
    case cloudrecord_op_send:
        onSendResult(r);
        break;

    //重命名：更新列表里的标题
    case cloudrecord_op_rename:
        onRenameResult(r);
        break;

    //删除：从列表里去掉
    case cloudrecord_op_delete:
        onDeleteResult(r);
        break;

    //其余操作不归本类，只记日志
    default:
        LOG_INFO("cloud record center: op %d result %d, record %lld.", r.m_op, r.m_code,
                 static_cast<long long>(r.m_recordId));
        break;
    }
}

uint32_t CCloudRecordCenter::startTransfer(int direction, const CloudRecordOpResult& r, const std::wstring& localPath,
                                           int64_t bytes)
{
    if (m_host == NULL)
        return 0;
    flamingomedia::MediaCloudTransfer req;
    req.m_direction = direction;
    req.m_token = r.m_token.c_str();
    req.m_host = r.m_host.c_str();
    req.m_port = r.m_port;
    req.m_localPath = localPath.c_str();
    req.m_bytes = bytes;
    return m_host->StartCloudTransfer(&req);
}

void CCloudRecordCenter::onDownloadToken(const CloudRecordOpResult& r)
{
    std::map<int64_t, ComposeJob>::iterator cj = m_composeJobs.find(r.m_recordId);
    if (cj != m_composeJobs.end() && cj->second.m_stage == kComposeWaitFrecToken)
    {
        if (r.m_code != 0)
        {
            failCompose(r.m_recordId, "frec download token refused", r.m_code);
            return;
        }
        ComposeJob& job = cj->second;
        job.m_frecPath = workDir() + L"\\rec_" + std::to_wstring(static_cast<long long>(job.m_recordId)) + L".frec";
        job.m_transferId = startTransfer(flamingomedia::media_cloud_download, r, job.m_frecPath, r.m_bytes);
        if (job.m_transferId == 0)
        {
            failCompose(job.m_recordId, "start frec download failed", 0);
            return;
        }
        job.m_stage = kComposeDownloading;
        return;
    }
    std::map<int64_t, OpenJob>::iterator oj = m_openJobs.find(r.m_recordId);
    if (oj != m_openJobs.end() && oj->second.m_transferId == 0)
    {
        if (r.m_code != 0)
        {
            m_openJobs.erase(oj);
            showError(r.m_code == error_code_cloudrecord_not_found ? TR(L"这条云端录像已被删除，或你没有权限观看。")
                                                                    : TRF(L"打开云端录像失败（错误码 {0}）。", r.m_code));
            return;
        }
        oj->second.m_transferId = startTransfer(flamingomedia::media_cloud_download, r, oj->second.m_cachePath, r.m_bytes);
        if (oj->second.m_transferId == 0)
        {
            m_openJobs.erase(oj);
            showError(TR(L"下载云端录像失败，请稍后再试。"));
        }
        return;
    }
    std::map<int64_t, DownloadJob>::iterator dj = m_downloadJobs.find(r.m_recordId);
    if (dj != m_downloadJobs.end() && dj->second.m_transferId == 0)
    {
        if (r.m_code != 0)
        {
            m_downloadJobs.erase(dj);
            showError(r.m_code == error_code_cloudrecord_not_found ? TR(L"这条云端录像已被删除。")
                                                                    : TRF(L"下载云端录像失败（错误码 {0}）。", r.m_code));
            return;
        }
        dj->second.m_transferId = startTransfer(flamingomedia::media_cloud_download, r, dj->second.m_destPath, r.m_bytes);
        if (dj->second.m_transferId == 0)
        {
            m_downloadJobs.erase(dj);
            showError(TR(L"下载云端录像失败，请稍后再试。"));
        }
    }
}

void CCloudRecordCenter::onUploadToken(const CloudRecordOpResult& r)
{
    //合成后的上传
    std::map<int64_t, ComposeJob>::iterator cj = m_composeJobs.find(r.m_recordId);
    if (cj != m_composeJobs.end() && cj->second.m_stage == kComposeWaitUpToken)
    {
        if (r.m_code != 0)
        {
            failCompose(r.m_recordId, "upload token refused", r.m_code);
            return;
        }
        ComposeJob& job = cj->second;
        job.m_transferId = startTransfer(flamingomedia::media_cloud_upload, r, job.m_mp4Path, job.m_mp4Bytes);
        if (job.m_transferId == 0)
        {
            failCompose(job.m_recordId, "start mp4 upload failed", 0);
            return;
        }
        job.m_stage = kComposeUploading;
        return;
    }
    //续传（27d-4）：已有录像 id、还在等令牌的上传
    for (size_t i = 0; i < m_uploadJobs.size() && r.m_recordId > 0; ++i)
    {
        UploadJob& job = m_uploadJobs[i];
        if (job.m_recordId != r.m_recordId || !job.m_waitingToken)
            continue;
        if (r.m_code != 0)
        {
            //服务端不再允许续传（已经传完、被删除、大小对不上）：清单里去掉，不再重试
            LOG_WARNING("cloud record center: resume upload of record %lld refused, code %d.",
                        static_cast<long long>(r.m_recordId), r.m_code);
            removePending(job.m_recordId);
            m_uploadJobs.erase(m_uploadJobs.begin() + static_cast<std::ptrdiff_t>(i));
            notifyChanged();
            return;
        }
        job.m_transferId = startTransfer(flamingomedia::media_cloud_upload, r, job.m_path, job.m_bytes);
        if (job.m_transferId == 0)
        {
            m_uploadJobs.erase(m_uploadJobs.begin() + static_cast<std::ptrdiff_t>(i));
            notifyChanged();
            return;
        }
        job.m_waitingToken = false;
        LOG_INFO("cloud record center: resume upload of record %lld started.", static_cast<long long>(r.m_recordId));
        notifyChanged();
        return;
    }
    //保存到云端：新建的应答按请求顺序对应排队里第一个还没有 id 的上传
    for (size_t i = 0; i < m_uploadJobs.size(); ++i)
    {
        UploadJob& job = m_uploadJobs[i];
        if (job.m_recordId != 0 || job.m_transferId != 0)
            continue;
        if (r.m_code != 0)
        {
            showError(r.m_code == error_code_storage_quota
                          ? TR(L"本企业的云端录像空间已满，没有保存到云端。")
                          : TRF(L"保存到云端失败（错误码 {0}），录像仍保存在本机。", r.m_code));
            m_uploadJobs.erase(m_uploadJobs.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
        job.m_recordId = r.m_recordId;
        if (r.m_hasRecord)
            applyRecord(r.m_record);
        job.m_transferId = startTransfer(flamingomedia::media_cloud_upload, r, job.m_path, job.m_bytes);
        if (job.m_transferId == 0)
        {
            showError(TR(L"上传到云端失败，请稍后再试。"));
            m_uploadJobs.erase(m_uploadJobs.begin() + static_cast<std::ptrdiff_t>(i));
            notifyChanged();
            return;
        }
        job.m_waitingToken = false;
        //记进本机清单：上传途中退出、断网时下次登录续传
        addPending(job);
        notifyChanged();
        return;
    }
}

void CCloudRecordCenter::onSendResult(const CloudRecordOpResult& r)
{
    if (r.m_code == 0)
        return;
    showError(r.m_code == error_code_cloudrecord_busy ? TR(L"这条云端录像还没有生成完成，暂时不能发送。")
                                                      : TRF(L"发送云端录像失败（错误码 {0}）。", r.m_code));
}

void CCloudRecordCenter::OnTransferProgress(uint32_t id, int64_t doneBytes, int64_t totalBytes)
{
    //只记下来，录制管理「云端」页由进度定时器每秒取一次
    if (totalBytes <= 0 || doneBytes < 0)
        return;
    int percent = static_cast<int>(doneBytes * kProgressFull / totalBytes);
    if (percent > kProgressFull)
        percent = kProgressFull;
    m_transferProgress[id] = percent;
}

void CCloudRecordCenter::OnTransferFinished(uint32_t id, int result)
{
    m_transferProgress.erase(id);
    for (std::map<int64_t, DownloadJob>::iterator it = m_downloadJobs.begin(); it != m_downloadJobs.end(); ++it)
    {
        if (it->second.m_transferId != id)
            continue;
        const std::wstring path = it->second.m_destPath;
        const CloudRecordInfo* info = findRecord(it->first);
        const std::wstring title = (info != NULL) ? utf8ToWide(info->m_title) : std::wstring();
        m_downloadJobs.erase(it);
        if (result == 0)
        {
            //完成提示带「打开所在文件夹」
            if (!m_toast)
                m_toast.reset(new screenshare::CShareCornerToast());
            screenshare::ShareToastTexts texts;
            texts.m_title = TR(L"云端录像");
            texts.m_accept = TR(L"打开所在文件夹");
            texts.m_ignore = TR(L"关闭");
            texts.m_message = kMsgToastCommand;
            m_toastRecordId = 0;
            m_toastFolderPath = path;
            m_toast->Show(TRF(L"「{0}」已下载。", title), static_cast<LPARAM>(1), m_hWnd, 2, texts);
        }
        else if (result != -1)
        {
            showError(TR(L"下载云端录像失败，请稍后再试。"));
        }
        LOG_INFO("cloud record center: download finished, result %d.", result);
        notifyChanged();
        return;
    }
    for (std::map<int64_t, ComposeJob>::iterator it = m_composeJobs.begin(); it != m_composeJobs.end(); ++it)
    {
        ComposeJob& job = it->second;
        if (job.m_transferId != id)
            continue;
        job.m_transferId = 0;
        if (result != 0)
        {
            failCompose(job.m_recordId, "transfer failed", result);
            return;
        }
        if (job.m_stage == kComposeDownloading)
        {
            //下载完：后台线程合成
            job.m_mp4Path = workDir() + L"\\rec_" + std::to_wstring(static_cast<long long>(job.m_recordId)) + L".mp4";
            job.m_task.reset(new CCloudRecordComposeTask(job.m_recordId, job.m_frecPath, job.m_mp4Path, m_hWnd,
                                                          kMsgComposeNames, kMsgComposeDone, i18n::UiFontName()));
            job.m_stage = kComposeRunning;
            job.m_task->Start();
            notifyChanged();
            return;
        }
        if (job.m_stage == kComposeUploading)
        {
            //上传完：等服务端推「可观看」，同时可以开始下一个
            job.m_stage = kComposeWaitReady;
            m_composing = 0;
            startNextCompose();
            notifyChanged();
        }
        return;
    }
    for (size_t i = 0; i < m_uploadJobs.size(); ++i)
    {
        if (m_uploadJobs[i].m_transferId != id)
            continue;
        m_uploadJobs[i].m_transferId = 0;
        if (result != 0 && result != -1)
            showError(TR(L"上传到云端中断了，下次登录后会自动接着上传。"));
        if (result != 0)
            m_uploadJobs.erase(m_uploadJobs.begin() + static_cast<std::ptrdiff_t>(i));
        //成功时等服务端推「可观看」再从列表里去掉
        notifyChanged();
        return;
    }
    for (std::map<int64_t, OpenJob>::iterator it = m_openJobs.begin(); it != m_openJobs.end(); ++it)
    {
        if (it->second.m_transferId != id)
            continue;
        const std::wstring path = it->second.m_cachePath;
        m_openJobs.erase(it);
        if (result == 0)
            ::ShellExecuteW(NULL, L"open", path.c_str(), NULL, NULL, SW_SHOWNORMAL);
        else if (result != -1)
            showError(TR(L"下载云端录像失败，请稍后再试。"));
        return;
    }
}

void CCloudRecordCenter::onComposeDone(int64_t recordId, int result)
{
    std::map<int64_t, ComposeJob>::iterator it = m_composeJobs.find(recordId);
    if (it == m_composeJobs.end())
        return;
    ComposeJob& job = it->second;
    if (job.m_task)
    {
        job.m_task->Join();
        job.m_task.reset();
    }
    if (result != 0)
    {
        failCompose(recordId, "compose failed", result);
        return;
    }
    if (!fileSize(job.m_mp4Path, job.m_mp4Bytes) || job.m_mp4Bytes <= 0)
    {
        failCompose(recordId, "composed file missing", 0);
        return;
    }
    job.m_stage = kComposeWaitUpToken;
    //发不出去时（主连接断开）留在这一阶段，重连后由 OnConnectionStateChanged 重发
    requestComposeToken(job);
    notifyChanged();
    LOG_INFO("cloud record center: composed record %lld, %lld bytes, uploading.", static_cast<long long>(recordId),
             static_cast<long long>(job.m_mp4Bytes));
}

void CCloudRecordCenter::failCompose(int64_t recordId, const char* reason, int code)
{
    LOG_ERROR("cloud record center: compose of record %lld failed: %s (%d).", static_cast<long long>(recordId), reason,
              code);
    std::map<int64_t, ComposeJob>::iterator it = m_composeJobs.find(recordId);
    if (it != m_composeJobs.end())
    {
        ::DeleteFileW(it->second.m_frecPath.c_str());
        ::DeleteFileW(it->second.m_mp4Path.c_str());
        m_composeJobs.erase(it);
    }
    if (m_composing == recordId)
        m_composing = 0;
    //取消（退出、登出）不提示；其余失败提示一次，下次登录会重新做，也可以在录制管理「云端」页重新合成
    if (code != -1)
    {
        m_composeFailed.insert(recordId);
        showError(TR(L"生成云端录像失败，可以在录制管理的「云端」页重新合成。"));
    }
    startNextCompose();
    notifyChanged();
}

bool CCloudRecordCenter::HasWork() const
{
    for (std::map<int64_t, ComposeJob>::const_iterator it = m_composeJobs.begin(); it != m_composeJobs.end(); ++it)
    {
        if (it->second.m_stage != kComposeWaitReady)
            return true;
    }
    for (size_t i = 0; i < m_uploadJobs.size(); ++i)
    {
        if (m_uploadJobs[i].m_transferId != 0 || m_uploadJobs[i].m_recordId == 0)
            return true;
    }
    //暂停了的同样算未完成（2026-10-07）：退出前的对话框可以选择放弃它们
    return !m_paused.empty();
}

void CCloudRecordCenter::OpenRecord(int64_t recordId, const std::wstring& title)
{
    if (recordId <= 0 || m_host == NULL)
        return;
    if (m_openJobs.find(recordId) != m_openJobs.end())
        return;
    const std::wstring cached = cacheDir() + L"\\rec_" + std::to_wstring(static_cast<long long>(recordId)) + L".mp4";
    const std::wstring cachedAudio = cacheDir() + L"\\rec_" + std::to_wstring(static_cast<long long>(recordId)) + L".m4a";
    int64_t bytes = 0;
    if (fileSize(cached, bytes) && bytes > 0)
    {
        ::ShellExecuteW(NULL, L"open", cached.c_str(), NULL, NULL, SW_SHOWNORMAL);
        return;
    }
    if (fileSize(cachedAudio, bytes) && bytes > 0)
    {
        ::ShellExecuteW(NULL, L"open", cachedAudio.c_str(), NULL, NULL, SW_SHOWNORMAL);
        return;
    }
    OpenJob job;
    job.m_recordId = recordId;
    job.m_title = title;
    //缓存文件名用录像 id：下载令牌的应答才知道是录像还是录音，这里先按 MP4 命名，录音的播放器同样能打开
    job.m_cachePath = cached;
    //主连接不可用时请求发不出去，不留下这次「打开」，否则之后再点会被当成正在下载而忽略
    if (!send(cmd_cloudrecord_op, BuildCloudRecordDownloadToken(recordId, kCloudFileFinal)))
    {
        showError(TR(L"现在不能播放，请在连接恢复后再试。"));
        return;
    }
    m_openJobs[recordId] = job;
    LOG_INFO("cloud record center: open record %lld, downloading.", static_cast<long long>(recordId));
}

void CCloudRecordCenter::SendRecord(int64_t recordId, const std::wstring& title)
{
    if (recordId <= 0 || m_host == NULL)
        return;
    const uint32_t token = m_nextPickToken++;
    const std::wstring preview = TRF(L"[云端录像] {0}", title);
    if (!m_host->PickConversations(preview.c_str(), token))
    {
        showError(TR(L"现在不能发送，请在连接恢复后再试。"));
        return;
    }
    m_sendPicks[token] = recordId;
}

void CCloudRecordCenter::OnConversationsPicked(uint32_t token, const flamingomedia::MediaConversationRef* refs,
                                               uint32_t count)
{
    std::map<uint32_t, int64_t>::iterator it = m_sendPicks.find(token);
    if (it == m_sendPicks.end() || refs == NULL)
        return;
    const int64_t recordId = it->second;
    m_sendPicks.erase(it);
    for (uint32_t i = 0; i < count; ++i)
        send(cmd_cloudrecord_op, BuildCloudRecordSend(recordId, refs[i].m_ctype, refs[i].m_cid));
    LOG_INFO("cloud record center: record %lld sent to %u conversations.", static_cast<long long>(recordId), count);
}

bool CCloudRecordCenter::UploadLocalRecording(const std::wstring& path, bool audioOnly, int64_t durationMs,
                                              int64_t startTime, const std::wstring& title)
{
    int64_t bytes = 0;
    if (m_host == NULL || !m_host->IsCloudRecordEnabled() || !fileSize(path, bytes) || bytes <= 0)
        return false;
    UploadJob job;
    job.m_path = path;
    job.m_audioOnly = audioOnly;
    job.m_durationMs = durationMs;
    job.m_startTime = startTime;
    job.m_bytes = bytes;
    job.m_title = title.empty() ? baseNameNoExt(path) : title;
    if (!send(cmd_cloudrecord_op,
              BuildCloudRecordUploadToken(0, bytes, audioOnly, wideToUtf8(job.m_title), durationMs, startTime)))
    {
        return false;
    }
    m_uploadJobs.push_back(job);
    LOG_INFO("cloud record center: save to cloud requested, %lld bytes, audio only %d.", static_cast<long long>(bytes),
             audioOnly ? 1 : 0);
    return true;
}

void CCloudRecordCenter::ResolveNames(CCloudRecordComposeTask* task)
{
    if (task == NULL)
        return;
    std::map<int64_t, std::wstring> names;
    const std::vector<int64_t>& users = task->UserIds();
    for (size_t i = 0; i < users.size(); ++i)
    {
        wchar_t buf[MAX_PATH] = { 0 };
        if (m_host != NULL && m_host->GetUserDisplayName(users[i], buf, MAX_PATH) && buf[0] != L'\0')
            names[users[i]] = buf;
    }
    task->SetNames(names);
}

void CCloudRecordCenter::showPauseToast(const std::wstring& text, int64_t recordId)
{
    if (!m_toast)
        m_toast.reset(new screenshare::CShareCornerToast());
    screenshare::ShareToastTexts texts;
    texts.m_title = TR(L"云端录像");
    texts.m_accept = TR(L"暂停");
    texts.m_ignore = TR(L"关闭");
    texts.m_message = kMsgToastCommand;
    m_toastRecordId = 0;
    m_toastPauseRecordId = recordId;
    m_toastFolderPath.clear();
    m_toast->Show(text, static_cast<LPARAM>(1), m_hWnd, 2, texts);
    LOG_INFO("cloud record center: toast with pause shown, record %lld.", static_cast<long long>(recordId));
}

void CCloudRecordCenter::showToast(const std::wstring& text, int64_t recordId, bool playButton)
{
    if (!m_toast)
        m_toast.reset(new screenshare::CShareCornerToast());
    screenshare::ShareToastTexts texts;
    texts.m_title = TR(L"云端录像");
    texts.m_accept = playButton ? TR(L"播放") : TR(L"知道了");
    texts.m_ignore = TR(L"关闭");
    texts.m_message = kMsgToastCommand;
    m_toastRecordId = playButton ? recordId : 0;
    m_toastPauseRecordId = 0;
    m_toastFolderPath.clear();
    m_toast->Show(text, static_cast<LPARAM>(1), m_hWnd, 2, texts);
    LOG_INFO("cloud record center: toast shown, record %lld, play button %d.", static_cast<long long>(recordId),
             playButton ? 1 : 0);
}

void CCloudRecordCenter::showError(const std::wstring& text)
{
    showToast(text, 0, false);
}

std::wstring CCloudRecordCenter::workDir()
{
    wchar_t base[MAX_PATH] = { 0 };
    if (FAILED(::SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, base)))
        ::GetTempPathW(MAX_PATH, base);
    std::wstring dir = std::wstring(base) + kLocalRoot + kWorkSub;
    ::SHCreateDirectoryExW(NULL, dir.c_str(), NULL);
    return dir;
}

std::wstring CCloudRecordCenter::cacheDir()
{
    wchar_t base[MAX_PATH] = { 0 };
    if (FAILED(::SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, base)))
        ::GetTempPathW(MAX_PATH, base);
    std::wstring dir = std::wstring(base) + kLocalRoot + kCacheSub;
    ::SHCreateDirectoryExW(NULL, dir.c_str(), NULL);
    return dir;
}

//======== 录制管理「云端」页（27d） ========

void CCloudRecordCenter::SetObserver(ICloudRecordObserver* observer)
{
    m_observer = observer;
    updateProgressTimer();
}

bool CCloudRecordCenter::RequestList(bool more)
{
    //更早的一页同一时刻只要一个：前一个还没回来时再点「加载更多」不重复请求
    if (more)
    {
        for (size_t i = 0; i < m_listRequests.size(); ++i)
        {
            if (m_listRequests[i])
                return false;
        }
    }
    const int64_t beforeId = (more && !m_list.empty()) ? m_list.back().m_recordId : 0;
    if (!send(cmd_cloudrecord_list, BuildCloudRecordList(beforeId, kListPage)))
        return false;
    m_listRequests.push_back(more && beforeId > 0);
    return true;
}

const std::vector<CloudRecordInfo>& CCloudRecordCenter::Records() const
{
    return m_list;
}

bool CCloudRecordCenter::ListLoaded() const
{
    return m_listLoaded;
}

bool CCloudRecordCenter::ListHasMore() const
{
    return m_listHasMore;
}

bool CCloudRecordCenter::CloudEnabled() const
{
    return m_listEnabled;
}

int64_t CCloudRecordCenter::UsedBytes() const
{
    return m_listUsedBytes;
}

int64_t CCloudRecordCenter::QuotaBytes() const
{
    return m_listQuotaBytes;
}

int64_t CCloudRecordCenter::RetainDays() const
{
    return m_listRetainDays;
}

CloudRowLocal CCloudRecordCenter::LocalState(int64_t recordId) const
{
    CloudRowLocal local;
    local.m_composeFailed = (m_composeFailed.find(recordId) != m_composeFailed.end());
    local.m_paused = (m_paused.find(recordId) != m_paused.end());
    std::map<int64_t, ComposeJob>::const_iterator cj = m_composeJobs.find(recordId);
    if (cj != m_composeJobs.end())
    {
        const ComposeJob& job = cj->second;
        switch (job.m_stage)
        {
        //合成线程在做：进度取合成任务按读过的字节估算的值
        case kComposeRunning:
            local.m_composing = true;
            local.m_progress = job.m_task ? job.m_task->Progress() : -1;
            break;
        //合成后的上传：进度取传输的进度；还在等令牌时为 0
        case kComposeWaitUpToken:
        case kComposeUploading:
        {
            local.m_uploading = true;
            std::map<uint32_t, int>::const_iterator tp = m_transferProgress.find(job.m_transferId);
            local.m_progress = (tp != m_transferProgress.end()) ? tp->second : 0;
            break;
        }
        //上传完了、等服务端推「可观看」
        case kComposeWaitReady:
            local.m_uploading = true;
            local.m_progress = kProgressFull;
            break;
        //排队、等原始帧的令牌、下载原始帧：合成中，进度未知
        default:
            local.m_composing = true;
            local.m_progress = -1;
            break;
        }
        return local;
    }
    for (size_t i = 0; i < m_uploadJobs.size(); ++i)
    {
        if (m_uploadJobs[i].m_recordId != recordId)
            continue;
        local.m_uploading = true;
        std::map<uint32_t, int>::const_iterator tp = m_transferProgress.find(m_uploadJobs[i].m_transferId);
        //还在等令牌时为 0，传输已结束（等服务端推「可观看」）时为 100
        if (m_uploadJobs[i].m_waitingToken)
            local.m_progress = 0;
        else if (m_uploadJobs[i].m_transferId == 0)
            local.m_progress = kProgressFull;
        else
            local.m_progress = (tp != m_transferProgress.end()) ? tp->second : 0;
        break;
    }
    return local;
}

bool CCloudRecordCenter::Rename(int64_t recordId, const std::wstring& title)
{
    if (recordId <= 0 || title.empty())
        return false;
    const bool ok = send(cmd_cloudrecord_op, BuildCloudRecordRename(recordId, wideToUtf8(title)));
    if (!ok)
        showError(TR(L"现在不能重命名，请在连接恢复后再试。"));
    LOG_INFO("cloud record center: rename record %lld requested, sent %d.", static_cast<long long>(recordId), ok ? 1 : 0);
    return ok;
}

bool CCloudRecordCenter::Delete(int64_t recordId)
{
    if (recordId <= 0)
        return false;
    const bool ok = send(cmd_cloudrecord_op, BuildCloudRecordDelete(recordId));
    if (!ok)
    {
        showError(TR(L"现在不能删除，请在连接恢复后再试。"));
        return false;
    }
    //本机对它的合成与上传不再有意义：先取消，删除失败时下次登录还会补做
    cancelLocalWork(recordId);
    notifyChanged();
    LOG_INFO("cloud record center: delete record %lld requested.", static_cast<long long>(recordId));
    return true;
}

bool CCloudRecordCenter::Download(int64_t recordId, const std::wstring& destPath)
{
    if (recordId <= 0 || destPath.empty() || m_downloadJobs.find(recordId) != m_downloadJobs.end())
        return false;
    if (!send(cmd_cloudrecord_op, BuildCloudRecordDownloadToken(recordId, kCloudFileFinal)))
    {
        showError(TR(L"现在不能下载，请在连接恢复后再试。"));
        return false;
    }
    DownloadJob job;
    job.m_recordId = recordId;
    job.m_destPath = destPath;
    m_downloadJobs[recordId] = job;
    LOG_INFO("cloud record center: download record %lld requested.", static_cast<long long>(recordId));
    return true;
}

bool CCloudRecordCenter::Recompose(int64_t recordId)
{
    const CloudRecordInfo* info = findRecord(recordId);
    if (info == NULL || info->m_source != cloudrecord_source_share || info->m_status != cloudrecord_status_pending ||
        m_composeFailed.find(recordId) == m_composeFailed.end())
    {
        return false;
    }
    const CloudRecordInfo copy = *info;
    enqueueCompose(copy);
    LOG_INFO("cloud record center: recompose record %lld.", static_cast<long long>(recordId));
    return true;
}

void CCloudRecordCenter::onRenameResult(const CloudRecordOpResult& r)
{
    if (r.m_code == 0)
    {
        if (r.m_hasRecord)
            applyRecord(r.m_record);
        notifyChanged();
        return;
    }
    LOG_WARNING("cloud record center: rename record %lld failed, code %d.", static_cast<long long>(r.m_recordId), r.m_code);
    if (r.m_code == error_code_sensitive_word)
        showError(TR(L"标题里有不允许使用的内容，请换一个标题。"));
    else if (r.m_code == error_code_cloudrecord_not_found)
        showError(TR(L"这条云端录像已被删除。"));
    else
        showError(TRF(L"重命名失败（错误码 {0}）。", r.m_code));
}

void CCloudRecordCenter::onDeleteResult(const CloudRecordOpResult& r)
{
    if (r.m_code == 0 || r.m_code == error_code_cloudrecord_not_found)
    {
        //已经不在了（另一台电脑上删过）同样从列表里去掉
        removeRecord(r.m_recordId);
        notifyChanged();
        return;
    }
    LOG_WARNING("cloud record center: delete record %lld failed, code %d.", static_cast<long long>(r.m_recordId), r.m_code);
    showError(r.m_code == error_code_cloudrecord_busy ? TR(L"这条录像正在录制，停止录制之后才能删除。")
                                                      : TRF(L"删除失败（错误码 {0}）。", r.m_code));
}

void CCloudRecordCenter::applyRecord(const CloudRecordInfo& info)
{
    if (info.m_recordId <= 0)
        return;
    //已不需要本机再做的（可观看、录制出错、录制中）：不再算暂停（2026-10-07）
    if (info.m_status != cloudrecord_status_pending && info.m_status != cloudrecord_status_uploading)
        forgetPaused(info.m_recordId);
    for (size_t i = 0; i < m_list.size(); ++i)
    {
        if (m_list[i].m_recordId == info.m_recordId)
        {
            m_list[i] = info;
            return;
        }
    }
    //按 id 从大到小插入：新录像在最前
    std::vector<CloudRecordInfo>::iterator pos = m_list.begin();
    while (pos != m_list.end() && pos->m_recordId > info.m_recordId)
        ++pos;
    m_list.insert(pos, info);
}

void CCloudRecordCenter::removeRecord(int64_t recordId)
{
    //已删除的录像不再算暂停（2026-10-07）
    forgetPaused(recordId);
    for (std::vector<CloudRecordInfo>::iterator it = m_list.begin(); it != m_list.end(); ++it)
    {
        if (it->m_recordId == recordId)
        {
            m_list.erase(it);
            return;
        }
    }
}

const CloudRecordInfo* CCloudRecordCenter::findRecord(int64_t recordId) const
{
    for (size_t i = 0; i < m_list.size(); ++i)
    {
        if (m_list[i].m_recordId == recordId)
            return &m_list[i];
    }
    return NULL;
}

void CCloudRecordCenter::cancelLocalWork(int64_t recordId)
{
    std::map<int64_t, ComposeJob>::iterator cj = m_composeJobs.find(recordId);
    if (cj != m_composeJobs.end())
    {
        if (cj->second.m_task)
        {
            cj->second.m_task->Cancel();
            cj->second.m_task->Join();
        }
        if (cj->second.m_transferId != 0 && m_host != NULL)
            m_host->CancelCloudTransfer(cj->second.m_transferId);
        ::DeleteFileW(cj->second.m_frecPath.c_str());
        ::DeleteFileW(cj->second.m_mp4Path.c_str());
        m_composeJobs.erase(cj);
    }
    for (std::deque<int64_t>::iterator it = m_composeQueue.begin(); it != m_composeQueue.end(); ++it)
    {
        if (*it == recordId)
        {
            m_composeQueue.erase(it);
            break;
        }
    }
    m_composeFailed.erase(recordId);
    for (size_t i = 0; i < m_uploadJobs.size(); ++i)
    {
        if (m_uploadJobs[i].m_recordId != recordId)
            continue;
        if (m_uploadJobs[i].m_transferId != 0 && m_host != NULL)
            m_host->CancelCloudTransfer(m_uploadJobs[i].m_transferId);
        m_uploadJobs.erase(m_uploadJobs.begin() + static_cast<std::ptrdiff_t>(i));
        break;
    }
    std::map<int64_t, DownloadJob>::iterator dj = m_downloadJobs.find(recordId);
    if (dj != m_downloadJobs.end())
    {
        if (dj->second.m_transferId != 0 && m_host != NULL)
            m_host->CancelCloudTransfer(dj->second.m_transferId);
        m_downloadJobs.erase(dj);
    }
    removePending(recordId);
    //正在做的那个被取消了：接着做队列里的下一个
    if (m_composing == recordId)
    {
        m_composing = 0;
        startNextCompose();
    }
}

void CCloudRecordCenter::notifyChanged()
{
    updateProgressTimer();
    if (m_observer != NULL)
        m_observer->OnCloudRecordsChanged();
}

void CCloudRecordCenter::updateProgressTimer()
{
    if (m_hWnd == NULL)
        return;
    //窗口开着、本机有正在合成或传输的工作时每秒刷新一次进度
    bool busy = !m_composeJobs.empty() || !m_downloadJobs.empty();
    for (size_t i = 0; i < m_uploadJobs.size() && !busy; ++i)
        busy = (m_uploadJobs[i].m_transferId != 0);
    const bool want = (m_observer != NULL) && busy;
    if (want == m_progressTimerOn)
        return;
    if (want)
        ::SetTimer(m_hWnd, kProgressTimerId, kProgressTimerMs, NULL);
    else
        ::KillTimer(m_hWnd, kProgressTimerId);
    m_progressTimerOn = want;
}

void CCloudRecordCenter::onProgressTimer()
{
    if (m_observer != NULL)
        m_observer->OnCloudProgressChanged();
}

std::wstring CCloudRecordCenter::pendingPath() const
{
    const int64_t self = (m_host != NULL) ? m_host->GetSelfUserId() : 0;
    if (self <= 0)
        return std::wstring();
    wchar_t base[MAX_PATH] = { 0 };
    if (FAILED(::SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, base)))
        ::GetTempPathW(MAX_PATH, base);
    const std::wstring dir = std::wstring(base) + kLocalRoot;
    ::SHCreateDirectoryExW(NULL, dir.c_str(), NULL);
    return dir + kPendingPrefix + std::to_wstring(static_cast<long long>(self)) + kPendingExt;
}

bool CCloudRecordCenter::loadPending(std::vector<PendingCloudUpload>& out) const
{
    out.clear();
    const std::wstring path = pendingPath();
    if (path.empty())
        return false;
    FILE* fp = NULL;
    //文件不存在就是清单为空
    if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || fp == NULL)
        return true;
    std::string text;
    char buf[kReadChunk];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        text.append(buf, n);
    fclose(fp);
    if (!ParsePendingUploadsJson(text, out))
    {
        LOG_WARNING("cloud record center: pending upload list unreadable, ignored.");
        out.clear();
    }
    return true;
}

void CCloudRecordCenter::savePending(const std::vector<PendingCloudUpload>& list) const
{
    const std::wstring path = pendingPath();
    if (path.empty())
        return;
    if (list.empty())
    {
        ::DeleteFileW(path.c_str());
        return;
    }
    const std::string text = BuildPendingUploadsJson(list);
    FILE* fp = NULL;
    if (_wfopen_s(&fp, path.c_str(), L"wb") != 0 || fp == NULL)
    {
        LOG_WARNING("cloud record center: write pending upload list failed.");
        return;
    }
    fwrite(text.data(), 1, text.size(), fp);
    fclose(fp);
}

void CCloudRecordCenter::addPending(const UploadJob& job)
{
    if (job.m_recordId <= 0)
        return;
    std::vector<PendingCloudUpload> list;
    if (!loadPending(list))
        return;
    std::vector<PendingCloudUpload> kept;
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (list[i].m_recordId != job.m_recordId)
            kept.push_back(list[i]);
    }
    PendingCloudUpload p;
    p.m_recordId = job.m_recordId;
    p.m_pathUtf8 = wideToUtf8(job.m_path);
    p.m_bytes = job.m_bytes;
    p.m_audioOnly = job.m_audioOnly;
    p.m_durationMs = job.m_durationMs;
    p.m_startTime = job.m_startTime;
    p.m_title = wideToUtf8(job.m_title);
    kept.push_back(p);
    savePending(kept);
}

void CCloudRecordCenter::removePending(int64_t recordId)
{
    std::vector<PendingCloudUpload> list;
    if (!loadPending(list) || list.empty())
        return;
    std::vector<PendingCloudUpload> kept;
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (list[i].m_recordId != recordId)
            kept.push_back(list[i]);
    }
    if (kept.size() != list.size())
        savePending(kept);
}

void CCloudRecordCenter::resumePendingUploads()
{
    std::vector<PendingCloudUpload> list;
    if (!loadPending(list) || list.empty())
        return;
    std::vector<PendingCloudUpload> kept;
    int resumed = 0;
    for (size_t i = 0; i < list.size(); ++i)
    {
        const PendingCloudUpload& p = list[i];
        //用户暂停了的（2026-10-07）：留在清单里，点「继续」时再续传
        if (m_paused.find(p.m_recordId) != m_paused.end())
        {
            kept.push_back(p);
            continue;
        }
        //本次登录里已经在传：不重复
        bool running = false;
        for (size_t k = 0; k < m_uploadJobs.size(); ++k)
            running = running || (m_uploadJobs[k].m_recordId == p.m_recordId);
        if (running)
        {
            kept.push_back(p);
            continue;
        }
        const CloudRecordInfo* info = findRecord(p.m_recordId);
        const std::wstring path = utf8ToWide(p.m_pathUtf8);
        int64_t size = 0;
        const bool fileOk = fileSize(path, size) && size == p.m_bytes;
        const int decision = DecidePendingResume(info != NULL, info != NULL ? info->m_status : 0, !m_listHasMore, fileOk);
        if (decision == pending_keep)
        {
            kept.push_back(p);
            continue;
        }
        if (decision == pending_drop)
        {
            LOG_INFO("cloud record center: pending upload of record %lld dropped (found %d, file ok %d).",
                     static_cast<long long>(p.m_recordId), info != NULL ? 1 : 0, fileOk ? 1 : 0);
            continue;
        }
        UploadJob job;
        job.m_recordId = p.m_recordId;
        job.m_path = path;
        job.m_audioOnly = p.m_audioOnly;
        job.m_durationMs = p.m_durationMs;
        job.m_startTime = p.m_startTime;
        job.m_bytes = p.m_bytes;
        job.m_title = utf8ToWide(p.m_title);
        kept.push_back(p);
        if (!send(cmd_cloudrecord_op, BuildCloudRecordUploadToken(p.m_recordId, p.m_bytes, p.m_audioOnly, p.m_title,
                                                                  p.m_durationMs, p.m_startTime)))
        {
            continue;
        }
        m_uploadJobs.push_back(job);
        ++resumed;
    }
    savePending(kept);
    if (resumed > 0)
    {
        LOG_INFO("cloud record center: %d pending uploads resumed.", resumed);
        showToast(TR(L"正在接着上传上次没有传完的云端录像。"), 0, false);
    }
}

//==================== 暂停 / 继续与退出前的处理（2026-10-07） ====================

bool CCloudRecordCenter::Pause(int64_t recordId)
{
    const CloudRowLocal local = LocalState(recordId);
    if (recordId <= 0 || (!local.m_composing && !local.m_uploading))
        return false;
    //「保存到云端」的上传：cancelLocalWork 会把它从续传清单里去掉，先记下，取消后写回，继续时据此续传
    UploadJob saved;
    bool hasUpload = false;
    for (size_t i = 0; i < m_uploadJobs.size(); ++i)
    {
        if (m_uploadJobs[i].m_recordId == recordId)
        {
            saved = m_uploadJobs[i];
            hasUpload = true;
            break;
        }
    }
    cancelLocalWork(recordId);
    if (hasUpload)
        addPending(saved);
    m_paused.insert(recordId);
    savePaused();
    if (m_toastPauseRecordId == recordId && m_toast)
    {
        m_toast->Hide();
        m_toastPauseRecordId = 0;
    }
    notifyChanged();
    LOG_INFO("cloud record center: record %lld paused (upload %d).", static_cast<long long>(recordId), hasUpload ? 1 : 0);
    return true;
}

bool CCloudRecordCenter::Resume(int64_t recordId)
{
    if (m_paused.erase(recordId) == 0)
        return false;
    savePaused();
    const CloudRecordInfo* info = findRecord(recordId);
    if (info != NULL && info->m_source == cloudrecord_source_share && info->m_status == cloudrecord_status_pending)
    {
        //等待合成的共享录制：重新加入合成队列，从下载原始帧开始
        const CloudRecordInfo copy = *info;
        enqueueCompose(copy);
    }
    else
    {
        //「保存到云端」的上传：按续传清单接着传（清单里其余暂停了的仍跳过）
        resumePendingUploads();
    }
    notifyChanged();
    LOG_INFO("cloud record center: record %lld resumed.", static_cast<long long>(recordId));
    return true;
}

bool CCloudRecordCenter::IsPaused(int64_t recordId) const
{
    return m_paused.find(recordId) != m_paused.end();
}

std::vector<std::pair<int64_t, std::wstring> > CCloudRecordCenter::UnfinishedRecords() const
{
    std::vector<std::pair<int64_t, std::wstring> > out;
    std::set<int64_t> seen;
    for (std::map<int64_t, ComposeJob>::const_iterator it = m_composeJobs.begin(); it != m_composeJobs.end(); ++it)
    {
        //上传完了、只等服务端推「可观看」的不算
        if (it->second.m_stage == kComposeWaitReady)
            continue;
        out.push_back(std::make_pair(it->first, it->second.m_title));
        seen.insert(it->first);
    }
    for (size_t i = 0; i < m_uploadJobs.size(); ++i)
    {
        const UploadJob& job = m_uploadJobs[i];
        if (job.m_recordId > 0 && seen.find(job.m_recordId) != seen.end())
            continue;
        out.push_back(std::make_pair(job.m_recordId, job.m_title));
        if (job.m_recordId > 0)
            seen.insert(job.m_recordId);
    }
    for (std::set<int64_t>::const_iterator it = m_paused.begin(); it != m_paused.end(); ++it)
    {
        if (seen.find(*it) != seen.end())
            continue;
        const CloudRecordInfo* info = findRecord(*it);
        out.push_back(std::make_pair(*it, info != NULL ? utf8ToWide(info->m_title) : std::wstring()));
    }
    return out;
}

int CCloudRecordCenter::AbandonUnfinished()
{
    const std::vector<std::pair<int64_t, std::wstring> > list = UnfinishedRecords();
    int deleted = 0;
    for (size_t i = 0; i < list.size(); ++i)
    {
        const int64_t id = list[i].first;
        if (id <= 0)
            continue;
        //Delete 先取消本机的工作再发删除请求；发不出去时本机的工作照样取消，下次登录服务端仍为未完成，会再次询问
        if (Delete(id))
            ++deleted;
        else
            cancelLocalWork(id);
        forgetPaused(id);
    }
    //「保存到云端」还没拿到录像 id 的：服务端可能已建了记录，这里只撤掉本机的请求
    for (size_t i = m_uploadJobs.size(); i > 0; --i)
    {
        if (m_uploadJobs[i - 1].m_recordId == 0)
            m_uploadJobs.erase(m_uploadJobs.begin() + static_cast<std::ptrdiff_t>(i - 1));
    }
    notifyChanged();
    LOG_INFO("cloud record center: abandoned %u unfinished records, %d delete requests sent.",
             static_cast<unsigned>(list.size()), deleted);
    return deleted;
}

std::wstring CCloudRecordCenter::pausedPath() const
{
    const int64_t self = (m_host != NULL) ? m_host->GetSelfUserId() : 0;
    if (self <= 0)
        return std::wstring();
    wchar_t base[MAX_PATH] = { 0 };
    if (FAILED(::SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, base)))
        ::GetTempPathW(MAX_PATH, base);
    const std::wstring dir = std::wstring(base) + kLocalRoot;
    ::SHCreateDirectoryExW(NULL, dir.c_str(), NULL);
    return dir + kPausedPrefix + std::to_wstring(static_cast<long long>(self)) + kPendingExt;
}

void CCloudRecordCenter::loadPaused()
{
    m_paused.clear();
    const std::wstring path = pausedPath();
    if (path.empty())
        return;
    m_pausedLoaded = true;
    FILE* fp = NULL;
    //文件不存在就是没有暂停的录像
    if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || fp == NULL)
        return;
    std::string text;
    char buf[kReadChunk];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        text.append(buf, n);
    fclose(fp);
    if (!DecodePausedCloudRecords(text, m_paused))
        LOG_WARNING("cloud record center: paused list unreadable, ignored.");
    LOG_INFO("cloud record center: %u paused records loaded.", static_cast<unsigned>(m_paused.size()));
}

void CCloudRecordCenter::savePaused() const
{
    const std::wstring path = pausedPath();
    if (path.empty())
        return;
    if (m_paused.empty())
    {
        ::DeleteFileW(path.c_str());
        return;
    }
    const std::string text = EncodePausedCloudRecords(m_paused);
    FILE* fp = NULL;
    if (_wfopen_s(&fp, path.c_str(), L"wb") != 0 || fp == NULL)
    {
        LOG_WARNING("cloud record center: write paused list failed.");
        return;
    }
    fwrite(text.data(), 1, text.size(), fp);
    fclose(fp);
}

void CCloudRecordCenter::forgetPaused(int64_t recordId)
{
    if (m_paused.erase(recordId) == 0)
        return;
    savePaused();
}

} // namespace screenrecord
