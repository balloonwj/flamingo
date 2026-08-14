/**
 *  单聊「发送 / 接收文件夹」客户端会话管理器的实现，FolderTransferManager.cpp
 *
 *  设计与职责边界见 FolderTransferManager.h 的文件头注释，本文件只补充实现层面的四点取舍：
 *
 *    1. <u>头文件顺序不能调</u>。FolderRecvSession.h 经 FolderRelayClient.h 引入
 *       <winsock2.h>，而 <windows.h> 内嵌的是老的 winsock.h，两者撞定义。故本文件把两个
 *       状态机的头文件排在最前，其后才允许出现任何 windows.h 系头文件（包括 ATL 的
 *       atlbase.h 与随之而来的 UserSessionData.h）。同理，本编译单元<u>不能</u>使用
 *       Flamingo 的预编译头（stdafx.h 第一句就是 atlbase.h），工程文件里须标
 *       PrecompiledHeader=NotUsing。
 *    2. <u>阻塞方法一律在锁外调</u>。FolderSendSession::Stop / FolderRecvSession::Stop 会等
 *       工作线程退出，而工作线程的进度回调要进本单例的互斥量；在锁内调 Stop 必然死锁。
 *       故收尾一律分两步：先在锁内把运行期对象从记录里换出来（DetachRuntimeLocked），
 *       出了临界区再 StopAndDestroyRuntime。扫描线程的 join 同理。
 *    3. <u>回调适配器比状态机活得久</u>。销毁顺序固定为「先销毁状态机、再销毁适配器」——
 *       状态机析构时才保证其全部线程已退出，反过来会让尚在运行的线程调到已析构的适配器上。
 *       这条只在 StopAndDestroyRuntime 一处实现，别处不得零散 reset。
 *    4. <u>多笔并存靠会话 id 路由</u>。每个回调适配器在构造时就记死自己那一笔的会话 id，
 *       故进度与结果绝不会串到别人的气泡上；信令一律先按会话 id 在会话表里找记录，找不到
 *       就只记日志、不动任何状态。
 *
 *  balloonwj@qq.com   2026-08-14
 */

//—— 中转通道相关头文件必须排在任何 windows.h 系头文件之前，理由见文件头第 1 点 ——
#include "FolderSendSession.h"
#include "FolderRecvSession.h"
#include "FolderTransferManager.h"

//—— 以下才允许引入 windows.h 系头文件 ——
#include <atlbase.h>              // CStringA 等 ATL 类型（UserSessionData.h 需要）
#include <atlstr.h>
#include <stdio.h>                // sprintf_s（拼本地占位会话 id）

#include "../UserSessionData.h"   // FMG_MSG_V2_FOLDER_* 与 g_szHomePath
#include "../IniFile.h"           // CIniFile（读 flamingo.ini 里的并发笔数上限）
#include "../net/Protocol.h"      // cmd_folder_* / error_code_folder_* / folder_transfer_state
#include "../net/IUSocket.h"      // 信令发送（SendFolder*）
#include "../IULog.h"             // LOG_INFO / LOG_WARNING / LOG_ERROR

namespace foldertransfer {

const char* const kFolderLocalIdPrefix = "local-";

namespace {

//并发笔数上限所在的配置文件段与键名（flamingo.ini）。该文件经 Win32 的配置文件接口
//按 ANSI 读取，故段名与键名只能是 ASCII。
const TCHAR* const kFolderIniSection = _T("app");
const TCHAR* const kFolderIniKeyMaxConcurrent = _T("foldermaxconcurrent");

//读配置时传给 GetPrivateProfileInt 的默认值。取 0 是有意的：0 本身就落在合法区间
//[1, kFolderMaxConcurrentHardLimit] 之外，于是「键不存在」与「取值非法」两种情形都会
//回到同一条越界分支上，不必为了区分它们再读一次字符串。
const int kFolderIniMissingValue = 0;

//本地占位会话 id 的自增序号。同一进程内单调递增，配合毫秒时间戳保证不重复。
//只在 UI 线程的 StartSend 里自增，故无须原子化。
unsigned int g_localIdSeed = 0;

//拼一个本地占位会话 id，形如 "local-123456789-3"。
//两段的作用：毫秒时间戳让不同进程 / 不同登录周期之间不重复，自增序号让同一毫秒内
//连点两次也不重复。
std::string MakeLocalSessionId()
{
    ++g_localIdSeed;

    char buf[64] = { 0 };
    ::sprintf_s(buf, sizeof(buf), "%s%llu-%u", kFolderLocalIdPrefix,
                (unsigned long long)::GetTickCount64(), g_localIdSeed);
    return std::string(buf);
}

} // anonymous namespace

// ======================== 扫描线程的入参 ========================

/**
 *  交给一条扫描线程的全部入参。多笔发送可能同时在扫描，故入参随线程走，不再放在管理器的
 *  成员上（那样后起的一笔会覆盖先起的那一笔）。由 StartSend 在堆上建好，ScanProc 接管并
 *  在返回前 delete。
 */
struct FolderScanTask
{
    std::wstring m_rootPath;     //要扫描的本地根目录完整路径
    std::string  m_localId;      //本笔的本地占位会话 id，扫完据它回表里找记录
    int64_t      m_peerUserId;   //接收方 userid
    //本线程是否已跑完的标志，与管理器 m_scanThreads 里那一项共用同一个对象。
    //由 ScanProc 在返回前持锁置 true，UI 线程据它决定哪些线程可以 join 回收。
    std::shared_ptr<bool> m_finished;

    FolderScanTask()
        : m_peerUserId(0)
    {
    }
};

// ======================== 回调适配器 ========================

/**
 *  发送端状态机的回调适配器。
 *  做成独立的类而不是让 FolderTransferManager 直接继承 IFolderSendSessionHandler，是为了
 *  不把 FolderSendSession.h 塞进 FolderTransferManager.h —— 那会把 <winsock2.h> 的包含顺序
 *  约束传染给每一个使用本单例的编译单元。
 *  <u>一笔传输一个实例</u>，构造时记死自己那一笔的会话 id，故多笔并存时进度不会串。
 *  生命周期：由对应的会话记录持有，且<u>晚于</u>该记录里的状态机销毁。
 */
class FolderSendCallback : public IFolderSendSessionHandler
{
public:
    /**
     *  @param owner     所属管理器（不接管所有权；单例，生命周期覆盖本对象）
     *  @param sessionId 本适配器对应的那一笔传输的会话 id，构造后不再变
     */
    FolderSendCallback(FolderTransferManager* owner, const std::string& sessionId)
        : m_owner(owner)
        , m_sessionId(sessionId)
    {
    }

    //进度更新（在状态机的工作线程 / 通道接收线程上触发）。
    virtual void OnFolderSendProgress(const FolderSendProgress& progress)
    {
        if (m_owner != NULL)
        {
            m_owner->OnSendProgressFromWorker(m_sessionId,
                                              progress.m_sentBytes, progress.m_totalBytes,
                                              progress.m_finishedFiles, progress.m_totalFiles,
                                              progress.m_currentFile);
        }
    }

    //整体完成（在状态机的驱动线程上触发）。
    virtual void OnFolderSendCompleted(int64_t totalFiles, int64_t totalBytes)
    {
        if (m_owner != NULL)
            m_owner->OnSendCompletedFromWorker(m_sessionId, totalFiles, totalBytes);
    }

    //整体失败（在状态机的驱动线程上触发）。
    virtual void OnFolderSendFailed(int errcode, const std::string& errmsg)
    {
        if (m_owner != NULL)
            m_owner->OnSendFailedFromWorker(m_sessionId, errcode, errmsg);
    }

private:
    FolderTransferManager* m_owner;      //所属管理器，引用不持有
    std::string            m_sessionId;  //本适配器对应的那一笔的会话 id，构造后只读
};

/**
 *  接收端状态机的回调适配器。与 FolderSendCallback 同因、同生命周期约定。
 */
class FolderRecvCallback : public IFolderRecvSessionHandler
{
public:
    /**
     *  @param owner     所属管理器（不接管所有权）
     *  @param sessionId 本适配器对应的那一笔传输的会话 id，构造后不再变
     */
    FolderRecvCallback(FolderTransferManager* owner, const std::string& sessionId)
        : m_owner(owner)
        , m_sessionId(sessionId)
    {
    }

    //进度更新（在各数据通道的工作线程上触发，已由会话内部串行化）。
    virtual void OnFolderRecvProgress(const FolderRecvProgress& progress)
    {
        if (m_owner != NULL)
        {
            m_owner->OnRecvProgressFromWorker(m_sessionId,
                                              progress.m_receivedBytes, progress.m_totalBytes,
                                              progress.m_doneFiles, progress.m_totalFiles,
                                              progress.m_currentFile);
        }
    }

    //整体完成（在会话线程上触发）。
    virtual void OnFolderRecvCompleted(const std::wstring& rootPath,
                                       int64_t totalFiles, int64_t totalBytes)
    {
        if (m_owner != NULL)
            m_owner->OnRecvCompletedFromWorker(m_sessionId, rootPath, totalFiles, totalBytes);
    }

    //整体失败（在会话线程上触发）。
    virtual void OnFolderRecvFailed(int errcode, const std::string& errmsg)
    {
        if (m_owner != NULL)
            m_owner->OnRecvFailedFromWorker(m_sessionId, errcode, errmsg);
    }

private:
    FolderTransferManager* m_owner;      //所属管理器，引用不持有
    std::string            m_sessionId;  //本适配器对应的那一笔的会话 id，构造后只读
};

// ======================== 构造 / 析构 ========================

FolderTransferManager& FolderTransferManager::GetInstance()
{
    static FolderTransferManager s_instance;
    return s_instance;
}

FolderTransferManager::FolderTransferManager()
    : m_hNotifyWnd(NULL)
{
}

FolderTransferManager::~FolderTransferManager()
{
    //进程退出时兜底回收线程。正常路径上 CMainDlg 在登出 / 掉线时已经调过 Reset。
    JoinAllScanThreads();
    StopAllSessions();
}

// ======================== 配置与基础设施 ========================

void FolderTransferManager::LoadConfig()
{
    CIniFile iniFile;
    CString  strIniFilePath(g_szHomePath);
    strIniFilePath += _T("config\\flamingo.ini");

    const int rawValue = (int)iniFile.ReadInt(kFolderIniSection, kFolderIniKeyMaxConcurrent,
                                              kFolderIniMissingValue, strIniFilePath);
    const int maxConcurrent = NormalizeFolderMaxConcurrent(rawValue);
    if (maxConcurrent != rawValue)
    {
        //缺失、非数字、负数、越界四种情形都落到这里（理由见 kFolderIniMissingValue 的注释）。
        LOG_WARNING("Folder transfer: foldermaxconcurrent=%d is missing or out of range [1, %d], "
                    "fall back to %d.",
                    rawValue, kFolderMaxConcurrentHardLimit, maxConcurrent);
    }

    {
        std::lock_guard<std::mutex> guard(m_mtx);
        m_registry.SetMaxConcurrent(maxConcurrent);
    }
    LOG_INFO("Folder transfer: max concurrent transfers on this client is %d.", maxConcurrent);
}

int FolderTransferManager::GetMaxConcurrent() const
{
    std::lock_guard<std::mutex> guard(m_mtx);
    return m_registry.GetMaxConcurrent();
}

int FolderTransferManager::GetActiveCount() const
{
    std::lock_guard<std::mutex> guard(m_mtx);
    return m_registry.ActiveCount();
}

void FolderTransferManager::SetNotifyTarget(HWND hNotifyWnd)
{
    std::lock_guard<std::mutex> guard(m_mtx);
    m_hNotifyWnd = hNotifyWnd;
}

void FolderTransferManager::PostToUi(UINT message, void* payload)
{
    HWND hWnd = NULL;
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        hWnd = m_hNotifyWnd;
    }

    //窗口尚未就绪或已销毁：就地释放堆负载，绝不让它悬着。各负载类型互不相同，
    //故按消息号分别按真实类型删除 —— 用 void* 直接 delete 是未定义行为。
    if (hWnd != NULL && ::IsWindow(hWnd) && ::PostMessage(hWnd, message, 0, (LPARAM)payload))
        return;

    LOG_WARNING("Folder transfer: post ui message %u failed, drop payload.", message);
    switch (message)
    {
    //目录扫描结果
    case FMG_MSG_V2_FOLDER_SCANNED:
        delete (FolderScanNotice*)payload;
        break;

    //发送 / 接收进度（两条消息共用同一个负载类型）
    case FMG_MSG_V2_FOLDER_SEND_PROGRESS:
    case FMG_MSG_V2_FOLDER_RECV_PROGRESS:
        delete (FolderProgressNotice*)payload;
        break;

    //发送 / 接收最终结果（同上）
    case FMG_MSG_V2_FOLDER_SEND_RESULT:
    case FMG_MSG_V2_FOLDER_RECV_RESULT:
        delete (FolderResultNotice*)payload;
        break;

    //未登记的消息号：只可能是本文件漏改，记一条日志好定位，不猜类型硬删
    default:
        LOG_ERROR("Folder transfer: unknown ui message %u, payload leaked.", message);
        break;
    }
}

void FolderTransferManager::JoinFinishedScanThreads()
{
    std::list<std::shared_ptr<std::thread> > finished;
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        std::list<FolderScanThreadEntry>::iterator it = m_scanThreads.begin();
        while (it != m_scanThreads.end())
        {
            if (it->m_finished && *(it->m_finished))
            {
                finished.push_back(it->m_thread);
                it = m_scanThreads.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    //join 必须在锁外做：扫描线程自己要进这把锁。这里只 join 已经置过完成标志的，
    //故实际等待时间极短。
    for (std::list<std::shared_ptr<std::thread> >::iterator it = finished.begin();
         it != finished.end(); ++it)
    {
        if (*it && (*it)->joinable())
            (*it)->join();
    }
}

void FolderTransferManager::JoinAllScanThreads()
{
    std::list<FolderScanThreadEntry> all;
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        all.swap(m_scanThreads);
    }

    //同样在锁外 join。这里可能要等一次大目录扫描跑完，故只在 Reset 与析构时调。
    for (std::list<FolderScanThreadEntry>::iterator it = all.begin(); it != all.end(); ++it)
    {
        if (it->m_thread && it->m_thread->joinable())
            it->m_thread->join();
    }
}

bool FolderTransferManager::DetachRuntimeLocked(FolderSessionRecord& record,
                                                FolderRuntimeHandles& out)
{
    out.m_sendSession = record.m_sendSession;
    out.m_recvSession = record.m_recvSession;
    out.m_sendCallback = record.m_sendCallback;
    out.m_recvCallback = record.m_recvCallback;

    record.m_sendSession.reset();
    record.m_recvSession.reset();
    record.m_sendCallback.reset();
    record.m_recvCallback.reset();

    return !out.Empty();
}

void FolderTransferManager::StopAndDestroyRuntime(FolderRuntimeHandles& handles)
{
    //Stop 会等工作线程退出，而那些线程的回调要进 m_mtx，故本函数必须在锁外调。
    if (handles.m_sendSession)
        handles.m_sendSession->Stop();
    if (handles.m_recvSession)
        handles.m_recvSession->Stop();

    //销毁顺序固定：先状态机、后回调适配器。反过来会让尚未退出的线程调到已析构的适配器上。
    handles.m_sendSession.reset();
    handles.m_recvSession.reset();
    handles.m_sendCallback.reset();
    handles.m_recvCallback.reset();
}

void FolderTransferManager::StopSessionById(const std::string& sessionId)
{
    FolderRuntimeHandles handles;
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        FolderSessionRecord* record = m_registry.FindByAnyId(sessionId);
        if (record == NULL)
            return;
        DetachRuntimeLocked(*record, handles);
    }
    StopAndDestroyRuntime(handles);
}

void FolderTransferManager::StopAllSessions()
{
    std::list<FolderRuntimeHandles> all;
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        FolderSessionRegistry::RecordList& records = m_registry.Records();
        for (FolderSessionRegistry::RecordList::iterator it = records.begin();
             it != records.end(); ++it)
        {
            FolderRuntimeHandles handles;
            if (DetachRuntimeLocked(*it, handles))
                all.push_back(handles);
        }
    }

    for (std::list<FolderRuntimeHandles>::iterator it = all.begin(); it != all.end(); ++it)
        StopAndDestroyRuntime(*it);
}

void FolderTransferManager::SendClose(const std::string& sessionId, bool success,
                                      int errCode, const std::string& errMsg)
{
    if (sessionId.empty())
    {
        //会话 id 还没拿到就失败了（例如扫描失败、或邀请应答本身就是错误）：服务端那边
        //根本没有会话可关，直接跳过即可。
        return;
    }
    CIUSocket::GetInstance().SendFolderClose(sessionId, success, errCode, errMsg);
}

void FolderTransferManager::Reset()
{
    //先回收线程（阻塞、须在锁外），再清状态，顺序不能反：清了状态之后线程仍在跑，
    //它拿到的会是半清空的数据。
    JoinAllScanThreads();
    StopAllSessions();

    std::lock_guard<std::mutex> guard(m_mtx);
    m_registry.Clear();
    LOG_INFO("Folder transfer: manager reset, all local session state cleared.");
}

bool FolderTransferManager::IsBusy() const
{
    std::lock_guard<std::mutex> guard(m_mtx);
    return m_registry.IsFull();
}

size_t FolderTransferManager::GetPendingInvites(int64_t peerUserId,
                                                std::vector<FolderPendingInvite>& out) const
{
    std::lock_guard<std::mutex> guard(m_mtx);
    return m_registry.CollectPendingInvites(peerUserId, out);
}

bool FolderTransferManager::IsWithinRetryWindow(const std::string& sessionId) const
{
    std::lock_guard<std::mutex> guard(m_mtx);
    return m_registry.IsWithinRetryWindow(sessionId, (int64_t)::GetTickCount64());
}

// ======================== 发送侧 ========================

int FolderTransferManager::StartSend(int64_t peerUserId, const std::wstring& rootPath,
                                     std::string& outLocalId)
{
    outLocalId.clear();

    if (peerUserId <= 0 || rootPath.empty())
    {
        LOG_ERROR("Folder send: bad argument, peer=%lld, path empty=%d.",
                  (long long)peerUserId, rootPath.empty() ? 1 : 0);
        return kFolderStartBadArgument;
    }

    //先回收已经跑完的扫描线程（阻塞，须在锁外），免得线程登记表越积越长。
    JoinFinishedScanThreads();

    std::string localId;
    {
        std::lock_guard<std::mutex> guard(m_mtx);

        //已过保留期的失败记录留着也没用（服务端会话早已销毁），顺手清掉。
        m_registry.PurgeExpired((int64_t)::GetTickCount64());

        if (m_registry.IsFull())
        {
            LOG_WARNING("Folder send: rejected, %d concurrent folder transfers already running, "
                        "limit is %d.",
                        m_registry.ActiveCount(), m_registry.GetMaxConcurrent());
            return kFolderStartBusy;
        }

        localId = MakeLocalSessionId();

        FolderSessionRecord record;
        record.m_role = kFolderRoleSender;
        record.m_peerUserId = peerUserId;
        record.m_localId = localId;
        record.m_active = true;
        record.m_rootPath = rootPath;
        record.m_folderName = ExtractFolderName(rootPath);
        m_registry.Insert(record);

        //扫描可能耗时数秒（上万个文件），必须放到工作线程，不能卡住 UI 线程。
        //入参随线程走：多笔发送可能同时在扫描。
        FolderScanTask* task = new FolderScanTask();
        task->m_rootPath = rootPath;
        task->m_localId = localId;
        task->m_peerUserId = peerUserId;

        FolderScanThreadEntry entry;
        entry.m_finished.reset(new bool(false));
        task->m_finished = entry.m_finished;
        entry.m_thread.reset(new std::thread(&FolderTransferManager::ScanProc, this, task));
        m_scanThreads.push_back(entry);
    }

    outLocalId = localId;
    LOG_INFO("Folder send: start scanning, peer=%lld, localid=%s.",
             (long long)peerUserId, localId.c_str());
    return kFolderStartOk;
}

void FolderTransferManager::ScanProc(FolderScanTask* task)
{
    if (task == NULL)
        return;

    ScanProcBody(*task);

    //置完成标志与真正退出之间只隔一个 return，故 UI 线程随后 join 到的是一条马上就结束的
    //线程。标志与 m_scanThreads 同受 m_mtx 保护，UI 线程不会读到写了一半的值。
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        if (task->m_finished)
            *(task->m_finished) = true;
    }
    delete task;
}

void FolderTransferManager::ScanProcBody(const FolderScanTask& task)
{
    FolderScanResult result;
    const bool ok = ScanFolderTree(task.m_rootPath, result);

    FolderScanNotice* notice = new FolderScanNotice();
    notice->m_localId = task.m_localId;
    notice->m_peerUserId = task.m_peerUserId;
    notice->m_rootPath = task.m_rootPath;
    notice->m_scanError = result.m_error;
    notice->m_errorPath = result.m_errorPath;

    if (!ok)
    {
        //扫描失败即整个文件夹失败（需求：任一错误即整体失败）。这一笔的名额就地放开，
        //让用户改完权限 / 换个目录之后能立刻再来一次。
        LOG_ERROR("Folder send: scan failed, localid=%s, error=%d, win32=%lu, path=%s.",
                  task.m_localId.c_str(), result.m_error, result.m_win32Error,
                  result.m_errorPath.c_str());

        {
            std::lock_guard<std::mutex> guard(m_mtx);
            m_registry.Erase(task.m_localId);
        }
        PostToUi(FMG_MSG_V2_FOLDER_SCANNED, notice);
        return;
    }

    notice->m_folderName = ExtractFolderName(task.m_rootPath);
    notice->m_dirCount = result.m_stats.m_dirCount;
    notice->m_fileCount = result.m_stats.m_fileCount;
    notice->m_totalBytes = result.m_stats.m_totalBytes;

    std::string folderNameUtf8;
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        FolderSessionRecord* record = m_registry.FindByLocalId(task.m_localId);
        //扫描期间用户可能已经取消 / 登出，此时本次结果作废，不再发邀请。
        if (record == NULL || !record->m_active)
        {
            LOG_INFO("Folder send: scan result discarded, session already gone, localid=%s.",
                     task.m_localId.c_str());
            delete notice;
            return;
        }

        record->m_manifest = result.m_manifest;
        record->m_manifestReady = true;
        record->m_totalFiles = result.m_stats.m_fileCount;
        record->m_totalBytes = result.m_stats.m_totalBytes;
        folderNameUtf8 = record->m_manifest.m_folderName;
    }

    LOG_INFO("Folder send: scan done, localid=%s, dirs=%lld, files=%lld, bytes=%lld.",
             task.m_localId.c_str(), (long long)result.m_stats.m_dirCount,
             (long long)result.m_stats.m_fileCount, (long long)result.m_stats.m_totalBytes);

    //先把规模统计交给界面补进气泡，再发邀请：应答必然晚于本次投递到达 UI 线程，
    //故界面拿到会话 id 时那条气泡一定已经存在。
    PostToUi(FMG_MSG_V2_FOLDER_SCANNED, notice);

    //「发出邀请」与「按帧 seq 登记进等待应答表」必须是同一个临界区内的一步：应答回到 UI
    //线程时要按 seq 查这张表，登记若晚于应答到达，那一笔就会被当成对不上而被忽略。
    //本模块唯一一处在临界区内做网络调用，理由是 CIUSocket::EnqueueFrame 只是组帧后往发送
    //缓冲追加一段并唤醒发送线程（net/IUSocket.cpp:1490），既不等网络、也不会回调进本单例，
    //耗时有界。多条扫描线程同时跑完不需要任何额外的顺序约束 —— seq 由 CIUSocket 逐帧发号、
    //与请求一一对应，配对不依赖先后次序。
    uint32_t inviteSeq = 0;
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        if (m_registry.FindByLocalId(task.m_localId) == NULL)
        {
            LOG_INFO("Folder send: invite skipped, session already gone, localid=%s.",
                     task.m_localId.c_str());
            return;
        }

        inviteSeq = CIUSocket::GetInstance().SendFolderInvite(
            task.m_peerUserId, folderNameUtf8,
            result.m_stats.m_dirCount, result.m_stats.m_fileCount, result.m_stats.m_totalBytes);
        if (inviteSeq != 0)
        {
            m_registry.RegisterInviteSeq(inviteSeq, task.m_localId);
        }
        else
        {
            //包体构造不出来（规模统计或目录名不合契约）：请求根本没发出去，不会有应答回来，
            //这一笔就地结束、名额放开，免得它一直占着名额等一个永远不来的应答。
            m_registry.Erase(task.m_localId);
        }
    }

    if (inviteSeq == 0)
        LOG_ERROR("Folder send: invite was not sent, localid=%s.", task.m_localId.c_str());
}

void FolderTransferManager::OnInviteAck(uint32_t seq, const net::FolderInviteResponse& resp,
                                        FolderBindNotice& out)
{
    out.m_code = resp.m_code;
    out.m_maxFiles = resp.m_maxFiles;
    out.m_maxSize = resp.m_maxSize;

    //服务端已建好会话、本端却已经没有对应记录时，要替它把那笔会话撤掉（在锁外发）。
    std::string orphanSessionId;
    {
        std::lock_guard<std::mutex> guard(m_mtx);

        //按帧 seq 精确命中是哪一笔发起的：应答包体里没有任何可对回的字段，而本机可能有
        //多笔在等应答。命中不了就只记日志、不猜 —— 猜错的后果是把会话 id 绑到别人的气泡上。
        std::string localId;
        FolderSessionRecord* record = NULL;
        if (m_registry.TakeInviteSeq(seq, localId))
            record = m_registry.FindByLocalId(localId);

        if (record == NULL)
        {
            //两种情形：seq 对不上任何一笔（本端已 Reset、或同一个应答到达了两次），
            //以及那一笔在应答到达前已被用户取消（登记项还在、记录已删）。
            LOG_WARNING("Folder send: invite ack seq=%u matches no pending record, code=%d, "
                        "session=%s.",
                        seq, resp.m_code, resp.m_sessionId.c_str());
            if (resp.m_code == error_code_ok)
                orphanSessionId = resp.m_sessionId;
        }
        else if (resp.m_code == error_code_ok)
        {
            record->m_sessionId = resp.m_sessionId;
            out.m_localId = record->m_localId;
            out.m_sessionId = resp.m_sessionId;
            LOG_INFO("Folder send: invite accepted by server, seq=%u, localid=%s, session=%s.",
                     seq, localId.c_str(), resp.m_sessionId.c_str());
        }
        else
        {
            //被服务端拒绝（规模超限 / 对方不在线 / 服务器繁忙 / 对方满额）：这一笔到此为止，
            //名额立刻放开。界面按 code 给出提示，规模超限时用应答回带的上限值，不硬编码。
            out.m_localId = record->m_localId;
            LOG_ERROR("Folder send: invite rejected, seq=%u, localid=%s, code=%d, msg=%s, "
                      "maxfiles=%lld, maxsize=%lld.",
                      seq, localId.c_str(), resp.m_code, resp.m_msg.c_str(),
                      (long long)resp.m_maxFiles, (long long)resp.m_maxSize);
            m_registry.Erase(localId);
        }
    }

    if (!orphanSessionId.empty())
    {
        LOG_WARNING("Folder send: cancel orphan server session %s.", orphanSessionId.c_str());
        CIUSocket::GetInstance().SendFolderCancel(orphanSessionId);
    }
}

// ======================== 接收侧 ========================

bool FolderTransferManager::OnInviteNotify(const net::FolderInviteNotify& notify)
{
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        m_registry.PurgeExpired((int64_t)::GetTickCount64());

        if (m_registry.FindBySessionId(notify.m_sessionId) != NULL)
        {
            //同一个会话 id 的邀请推来了两遍：账面上已经有这一笔，再建一条会让界面出两个气泡。
            LOG_WARNING("Folder recv: duplicated invite notify for session %s, ignored.",
                        notify.m_sessionId.c_str());
            return false;
        }

        if (!m_registry.IsFull())
        {
            FolderSessionRecord record;
            record.m_role = kFolderRoleRecv;
            record.m_peerUserId = notify.m_fromId;
            record.m_sessionId = notify.m_sessionId;
            record.m_active = true;
            record.m_totalFiles = notify.m_totalFiles;
            record.m_totalBytes = notify.m_totalSize;

            std::wstring wideName;
            Utf8ToUtf16(notify.m_folderName, wideName);
            record.m_folderName = wideName;

            //记下这条待应答的邀请：收到邀请不再弹窗，聊天窗可能是稍后才被点开的，
            //那一刻它要能按对端 userid 把邀请查回来补画气泡（见 GetPendingInvites）。
            record.m_invite.m_sessionId = notify.m_sessionId;
            record.m_invite.m_fromUserId = notify.m_fromId;
            record.m_invite.m_folderNameUtf8 = notify.m_folderName;
            record.m_invite.m_dirCount = notify.m_totalDirs;
            record.m_invite.m_fileCount = notify.m_totalFiles;
            record.m_invite.m_totalBytes = notify.m_totalSize;
            record.m_invite.m_receivedAtMs = (int64_t)::GetTickCount64();

            m_registry.Insert(record);

            LOG_INFO("Folder recv: invite received, session=%s, from=%lld, dirs=%lld, files=%lld, "
                     "bytes=%lld, active=%d/%d.",
                     notify.m_sessionId.c_str(), (long long)notify.m_fromId,
                     (long long)notify.m_totalDirs, (long long)notify.m_totalFiles,
                     (long long)notify.m_totalSize,
                     m_registry.ActiveCount(), m_registry.GetMaxConcurrent());
            return true;
        }

        //名额已满：立刻回一个拒绝，好过把邀请挂在那里等服务端超时（默认 30 分钟）。
        LOG_WARNING("Folder recv: invite %s rejected locally, %d concurrent transfers already "
                    "running, limit is %d.",
                    notify.m_sessionId.c_str(), m_registry.ActiveCount(),
                    m_registry.GetMaxConcurrent());
    }

    //忙时的拒绝要在锁外发，免得在临界区里做网络调用。
    CIUSocket::GetInstance().SendFolderResponse(notify.m_sessionId, false);
    return false;
}

int FolderTransferManager::AcceptInvite(const std::string& sessionId,
                                        const std::wstring& saveParentDir)
{
    if (sessionId.empty() || saveParentDir.empty())
    {
        LOG_ERROR("Folder recv: accept with bad argument, session empty=%d, dir empty=%d.",
                  sessionId.empty() ? 1 : 0, saveParentDir.empty() ? 1 : 0);
        return kFolderStartBadArgument;
    }

    {
        std::lock_guard<std::mutex> guard(m_mtx);
        FolderSessionRecord* record = m_registry.FindBySessionId(sessionId);
        if (record == NULL || record->m_role != kFolderRoleRecv
            || record->m_invite.m_sessionId.empty())
        {
            LOG_WARNING("Folder recv: accept ignored, session %s is no longer pending.",
                        sessionId.c_str());
            return kFolderStartNoSession;
        }

        record->m_saveParentDir = saveParentDir;
        //用户已经应答，这条邀请不再"待应答"：此后再点开聊天窗看到的应当是传输中 / 结果
        //气泡（由状态推送与进度回投维护），而不是又一个「接收 / 拒绝」。
        record->m_invite = FolderPendingInvite();
        //接收进度对象在这里新建，之后<u>整轮传输（含重试）共用同一个</u>：断点续传要靠
        //它记住每个文件已落盘多少字节与增量 MD5 上下文。
        record->m_recvState.reset(new FolderReceiveState());
    }

    LOG_INFO("Folder recv: accept invite, session=%s.", sessionId.c_str());
    CIUSocket::GetInstance().SendFolderResponse(sessionId, true);
    return kFolderStartOk;
}

void FolderTransferManager::RejectInvite(const std::string& sessionId)
{
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        FolderSessionRecord* record = m_registry.FindBySessionId(sessionId);
        if (record == NULL || record->m_role != kFolderRoleRecv
            || record->m_invite.m_sessionId.empty())
        {
            LOG_WARNING("Folder recv: reject ignored, session %s is no longer pending.",
                        sessionId.c_str());
            return;
        }
        //拒绝之后这一笔就没了，名额随记录一并放开（此时还没有任何运行期对象）。
        m_registry.Erase(sessionId);
    }

    LOG_INFO("Folder recv: reject invite, session=%s.", sessionId.c_str());
    CIUSocket::GetInstance().SendFolderResponse(sessionId, false);
}

// ======================== 取消与重试 ========================

void FolderTransferManager::CancelTransfer(const std::string& sessionId)
{
    if (sessionId.empty())
    {
        LOG_WARNING("Folder transfer: cancel ignored, empty session id.");
        return;
    }

    int         role = kFolderRoleNone;
    std::string serverSessionId;   //服务端分配的会话 id；空=1810 的应答还没回来
    std::shared_ptr<FolderSendSession> sendSession;
    std::shared_ptr<FolderRecvSession> recvSession;
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        FolderSessionRecord* record = m_registry.FindByAnyId(sessionId);
        if (record == NULL)
        {
            LOG_WARNING("Folder transfer: cancel ignored, session %s is not in the table.",
                        sessionId.c_str());
            return;
        }

        role = record->m_role;
        serverSessionId = record->m_sessionId;
        sendSession = record->m_sendSession;
        recvSession = record->m_recvSession;

        if (!sendSession && !recvSession)
        {
            //还没开始传字节：这一笔到此为止，账面就地删掉（此时并没有运行期对象要收尾）。
            m_registry.Erase(sessionId);
        }
    }

    LOG_INFO("Folder transfer: user canceled, session=%s, role=%d, transferring=%d.",
             sessionId.c_str(), role,
             (sendSession || recvSession) ? 1 : 0);

    if (sendSession || recvSession)
    {
        //传输中取消：只置标志，真正的收尾走状态机的失败回调（错误码
        //error_code_folder_canceled），再由界面在 UI 线程回调 FinishSend / FinishRecv
        //停线程并发 cmd_folder_close。Cancel 本身不阻塞，仍统一放到锁外调。
        if (sendSession)
            sendSession->Cancel();
        if (recvSession)
            recvSession->Cancel();
        return;
    }

    if (serverSessionId.empty())
    {
        //服务端还没分配会话 id（1810 的应答未到）：本端账面已删，等应答到达时 OnInviteAck
        //会因按 seq 命中的记录已不存在而替这一笔撤掉服务端的会话。
        LOG_INFO("Folder send: canceled before invite ack, localid=%s.", sessionId.c_str());
        return;
    }

    if (role == kFolderRoleSender)
    {
        //还在等对方应答：撤回邀请。
        CIUSocket::GetInstance().SendFolderCancel(serverSessionId);
        return;
    }

    //接收端已同意、通道还没建起来时点了取消：按失败结束，让对端也立刻知道结果。
    SendClose(serverSessionId, false, error_code_folder_canceled, "canceled by receiver");
}

int FolderTransferManager::RetryTransfer(const std::string& sessionId)
{
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        FolderSessionRecord* record = m_registry.FindBySessionId(sessionId);
        if (record == NULL || record->m_failedAtMs == 0)
        {
            LOG_WARNING("Folder transfer: retry rejected, session %s is not a retained failed one.",
                        sessionId.c_str());
            return kFolderStartNoSession;
        }
        if (record->m_active)
        {
            //上一次重试还在跑（用户连点了两下）。
            LOG_WARNING("Folder transfer: retry ignored, session %s is already running.",
                        sessionId.c_str());
            return kFolderStartOk;
        }

        const int64_t now = (int64_t)::GetTickCount64();
        if ((now - record->m_failedAtMs) >= kFolderRetryWindowMs)
        {
            //已超出保留期：服务端那边会话早已销毁，续传位点一并作废，只能重新发送。
            LOG_WARNING("Folder transfer: retry rejected, session %s expired (%lld ms elapsed).",
                        sessionId.c_str(), (long long)(now - record->m_failedAtMs));
            m_registry.Erase(sessionId);
            return kFolderStartNoSession;
        }

        //重试要重新占一个名额；期间用户可能已经另发了几笔把名额占满。
        if (m_registry.IsFull())
        {
            LOG_WARNING("Folder transfer: retry rejected, %d concurrent folder transfers already "
                        "running, limit is %d.",
                        m_registry.ActiveCount(), m_registry.GetMaxConcurrent());
            return kFolderStartBusy;
        }

        //清单（发送端）与接收进度（接收端）都照旧留着，等新的中转参数下来复用。
        record->m_active = true;
    }

    LOG_INFO("Folder transfer: retry requested, session=%s.", sessionId.c_str());
    CIUSocket::GetInstance().SendFolderRetry(sessionId);
    return kFolderStartOk;
}

// ======================== 信令处理 ========================

void FolderTransferManager::OnSessionAck(int cmd, const net::FolderSessionResponse& resp)
{
    if (resp.m_code == error_code_ok)
    {
        LOG_INFO("Folder transfer: cmd %d ack ok, session=%s.", cmd, resp.m_sessionId.c_str());
        return;
    }

    LOG_ERROR("Folder transfer: cmd %d ack failed, session=%s, code=%d, msg=%s.",
              cmd, resp.m_sessionId.c_str(), resp.m_code, resp.m_msg.c_str());

    //同意接收与重试这两条被拒绝，意味着那一笔根本起不来，名额要立刻放开；
    //撤回与结束被拒绝无所谓（会话本来就要没了），照旧走状态推送收尾。
    if (cmd != cmd_folder_response && cmd != cmd_folder_retry)
        return;

    if (resp.m_sessionId.empty())
    {
        //请求格式非法时服务端带不出会话 id，多笔并存时无从判断是哪一笔。留着不动即可：
        //那一笔的名额会在服务端推来状态变更（拒绝 / 超时 / 失败）或退出登录 Reset 时放开。
        LOG_WARNING("Folder transfer: cmd %d ack has no session id, cannot release any slot.", cmd);
        return;
    }

    std::lock_guard<std::mutex> guard(m_mtx);
    if (m_registry.Erase(resp.m_sessionId))
    {
        LOG_INFO("Folder transfer: session %s dropped after a failed cmd %d ack.",
                 resp.m_sessionId.c_str(), cmd);
    }
}

void FolderTransferManager::OnStateNotify(const net::FolderStateNotify& notify)
{
    LOG_INFO("Folder transfer: state notify, session=%s, state=%d, errcode=%d, errmsg=%s.",
             notify.m_sessionId.c_str(), notify.m_state, notify.m_errCode, notify.m_errMsg.c_str());

    {
        std::lock_guard<std::mutex> guard(m_mtx);
        if (m_registry.FindBySessionId(notify.m_sessionId) == NULL)
        {
            //不是本机记着的哪一笔（服务端串号、或本端刚清完状态）：只记日志，不动任何东西。
            LOG_WARNING("Folder transfer: state notify for unknown session %s, ignored.",
                        notify.m_sessionId.c_str());
            return;
        }
    }

    switch (notify.m_state)
    {
    //响铃中 / 传输中：这一笔仍在推进，本单例无须动作，界面照状态更新气泡即可。
    case folder_state_ringing:
    case folder_state_transferring:
        break;

    //对方拒绝 / 发送方撤回邀请 / 对方离线 / 邀请超时：这一笔作废，且没有任何进度可续，
    //整条记录删掉，名额立刻放开。
    //其中"邀请超时"（服务端 folderinvitetimeout）在<u>聊天窗没打开</u>时同样要处理干净：
    //记录一删，待应答的邀请随之消失，用户此后点开会话不会再看到那条已经失效的邀请气泡。
    //会话列表里那条 "[文件夹] xxx" 的未读摘要按定稿保持原样。
    case folder_state_rejected:
    case folder_state_canceled:
    case folder_state_peer_offline:
    case folder_state_timeout:
        StopSessionById(notify.m_sessionId);
        {
            std::lock_guard<std::mutex> guard(m_mtx);
            m_registry.Erase(notify.m_sessionId);
        }
        break;

    //传输失败：停掉这一笔的状态机并放开它的名额，但<u>保留</u>清单与接收进度、记下失败时刻，
    //使保留期内点「重试」能断点续传。
    case folder_state_failed:
        StopSessionById(notify.m_sessionId);
        {
            std::lock_guard<std::mutex> guard(m_mtx);
            FolderSessionRecord* record = m_registry.FindBySessionId(notify.m_sessionId);
            if (record != NULL)
                m_registry.MarkFailed(*record, (int64_t)::GetTickCount64());
        }
        break;

    //全部完成：状态机那边已经自行收尾，这里把这一笔的记录整条删掉。
    case folder_state_completed:
        StopSessionById(notify.m_sessionId);
        {
            std::lock_guard<std::mutex> guard(m_mtx);
            m_registry.Erase(notify.m_sessionId);
        }
        break;

    //越界状态：解析层已按 1~8 校验过，走到这里说明协议实现不一致，记日志后忽略。
    default:
        LOG_ERROR("Folder transfer: unexpected state %d for session %s.",
                  notify.m_state, notify.m_sessionId.c_str());
        break;
    }
}

void FolderTransferManager::OnRelayInfo(const net::FolderRelayInfo& info)
{
    LOG_INFO("Folder transfer: relay info, session=%s, role=%d, %s:%d, lanes=%d.",
             info.m_sessionId.c_str(), info.m_role, info.m_relayIp.c_str(),
             info.m_relayPort, info.m_lanes);

    //重试时这一笔上一轮的状态机可能还挂着（失败路径已停过，这里是兜底），先收干净再建新的。
    StopSessionById(info.m_sessionId);

    if (info.m_role == net::folder_role_sender)
    {
        FolderSendSessionParams params;
        std::shared_ptr<FolderSendSession>  session;
        std::shared_ptr<FolderSendCallback> callback;
        {
            std::lock_guard<std::mutex> guard(m_mtx);
            FolderSessionRecord* record = m_registry.FindBySessionId(info.m_sessionId);
            if (record == NULL || record->m_role != kFolderRoleSender)
            {
                LOG_ERROR("Folder send: relay info for unknown session %s, ignored.",
                          info.m_sessionId.c_str());
                return;
            }
            if (!record->m_manifestReady)
            {
                LOG_ERROR("Folder send: relay info arrived before manifest is ready, session=%s.",
                          info.m_sessionId.c_str());
                return;
            }

            params.m_relayIp = info.m_relayIp;
            params.m_relayPort = info.m_relayPort;
            params.m_relayToken = info.m_token;
            params.m_dataLanes = info.m_lanes;
            params.m_rootPath = record->m_rootPath;
            params.m_manifest = record->m_manifest;   //重试时复用的正是同一份清单
            params.m_connectTimeoutSec = kFolderConnectTimeoutSec;

            record->m_active = true;
            callback.reset(new FolderSendCallback(this, info.m_sessionId));
            session.reset(new FolderSendSession());
            record->m_sendCallback = callback;
            record->m_sendSession = session;
        }

        //Start 只起线程即返回，但仍放到锁外调 —— 它一返回工作线程就可能回调进本单例。
        if (session && !session->Start(params, callback.get()))
        {
            LOG_ERROR("Folder send: start session failed, session=%s.", info.m_sessionId.c_str());
            OnSendFailedFromWorker(info.m_sessionId, error_code_folder_relay_broken,
                                   "start send session failed");
        }
        return;
    }

    if (info.m_role == net::folder_role_receiver)
    {
        FolderRecvSessionParams params;
        std::shared_ptr<FolderRecvSession>  session;
        std::shared_ptr<FolderRecvCallback> callback;
        std::shared_ptr<FolderReceiveState> state;
        {
            std::lock_guard<std::mutex> guard(m_mtx);
            FolderSessionRecord* record = m_registry.FindBySessionId(info.m_sessionId);
            if (record == NULL || record->m_role != kFolderRoleRecv)
            {
                LOG_ERROR("Folder recv: relay info for unknown session %s, ignored.",
                          info.m_sessionId.c_str());
                return;
            }
            if (!record->m_recvState)
            {
                //正常路径上 AcceptInvite 已经建好；走到这里说明重试时状态被清过，
                //新建一份即可，只是这一轮会从零开始收。
                record->m_recvState.reset(new FolderReceiveState());
                LOG_WARNING("Folder recv: receive state missing, resume points are lost, session=%s.",
                            info.m_sessionId.c_str());
            }

            params.m_sessionId = info.m_sessionId;
            params.m_relayIp = info.m_relayIp;
            params.m_relayPort = info.m_relayPort;
            params.m_relayToken = info.m_token;
            params.m_dataLanes = info.m_lanes;
            params.m_saveParentDir = record->m_saveParentDir;

            state = record->m_recvState;
            record->m_active = true;
            callback.reset(new FolderRecvCallback(this, info.m_sessionId));
            session.reset(new FolderRecvSession());
            record->m_recvCallback = callback;
            record->m_recvSession = session;
        }

        if (session && !session->Start(params, state, callback.get()))
        {
            LOG_ERROR("Folder recv: start session failed, session=%s.", info.m_sessionId.c_str());
            OnRecvFailedFromWorker(info.m_sessionId, error_code_folder_relay_broken,
                                   "start recv session failed");
        }
        return;
    }

    LOG_ERROR("Folder transfer: relay info with unknown role %d, session=%s.",
              info.m_role, info.m_sessionId.c_str());
}

// ======================== 状态机回调（工作线程） ========================

void FolderTransferManager::OnSendProgressFromWorker(const std::string& sessionId,
                                                     int64_t doneBytes, int64_t totalBytes,
                                                     int64_t doneFiles, int64_t totalFiles,
                                                     const std::string& currentFile)
{
    FolderProgressNotice* notice = new FolderProgressNotice();
    notice->m_sessionId = sessionId;
    notice->m_doneBytes = doneBytes;
    notice->m_totalBytes = totalBytes;
    notice->m_doneFiles = doneFiles;
    notice->m_totalFiles = totalFiles;
    notice->m_currentFile = currentFile;
    PostToUi(FMG_MSG_V2_FOLDER_SEND_PROGRESS, notice);
}

void FolderTransferManager::OnSendCompletedFromWorker(const std::string& sessionId,
                                                      int64_t totalFiles, int64_t totalBytes)
{
    FolderResultNotice* notice = new FolderResultNotice();
    notice->m_sessionId = sessionId;
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        const FolderSessionRecord* record = m_registry.FindBySessionId(sessionId);
        if (record != NULL)
            notice->m_localRoot = record->m_rootPath;
    }
    notice->m_success = true;
    notice->m_totalFiles = totalFiles;
    notice->m_totalBytes = totalBytes;

    LOG_INFO("Folder send: completed, session=%s, files=%lld, bytes=%lld.",
             sessionId.c_str(), (long long)totalFiles, (long long)totalBytes);
    PostToUi(FMG_MSG_V2_FOLDER_SEND_RESULT, notice);
}

void FolderTransferManager::OnSendFailedFromWorker(const std::string& sessionId,
                                                   int errCode, const std::string& errMsg)
{
    FolderResultNotice* notice = new FolderResultNotice();
    notice->m_sessionId = sessionId;
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        const FolderSessionRecord* record = m_registry.FindBySessionId(sessionId);
        if (record != NULL)
        {
            notice->m_localRoot = record->m_rootPath;
            notice->m_totalFiles = record->m_totalFiles;
            notice->m_totalBytes = record->m_totalBytes;
        }
    }
    notice->m_success = false;
    notice->m_errCode = errCode;
    notice->m_errMsg = errMsg;

    LOG_ERROR("Folder send: failed, session=%s, errcode=%d, errmsg=%s.",
              sessionId.c_str(), errCode, errMsg.c_str());
    PostToUi(FMG_MSG_V2_FOLDER_SEND_RESULT, notice);
}

void FolderTransferManager::OnRecvProgressFromWorker(const std::string& sessionId,
                                                     int64_t doneBytes, int64_t totalBytes,
                                                     int64_t doneFiles, int64_t totalFiles,
                                                     const std::string& currentFile)
{
    FolderProgressNotice* notice = new FolderProgressNotice();
    notice->m_sessionId = sessionId;
    notice->m_doneBytes = doneBytes;
    notice->m_totalBytes = totalBytes;
    notice->m_doneFiles = doneFiles;
    notice->m_totalFiles = totalFiles;
    notice->m_currentFile = currentFile;
    PostToUi(FMG_MSG_V2_FOLDER_RECV_PROGRESS, notice);
}

void FolderTransferManager::OnRecvCompletedFromWorker(const std::string& sessionId,
                                                      const std::wstring& rootPath,
                                                      int64_t totalFiles, int64_t totalBytes)
{
    FolderResultNotice* notice = new FolderResultNotice();
    notice->m_sessionId = sessionId;
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        FolderSessionRecord* record = m_registry.FindBySessionId(sessionId);
        if (record != NULL)
            record->m_recvRootPath = rootPath;
    }
    notice->m_success = true;
    notice->m_localRoot = rootPath;
    notice->m_totalFiles = totalFiles;
    notice->m_totalBytes = totalBytes;

    LOG_INFO("Folder recv: completed, session=%s, files=%lld, bytes=%lld.",
             sessionId.c_str(), (long long)totalFiles, (long long)totalBytes);
    PostToUi(FMG_MSG_V2_FOLDER_RECV_RESULT, notice);
}

void FolderTransferManager::OnRecvFailedFromWorker(const std::string& sessionId,
                                                   int errCode, const std::string& errMsg)
{
    FolderResultNotice* notice = new FolderResultNotice();
    notice->m_sessionId = sessionId;
    {
        std::lock_guard<std::mutex> guard(m_mtx);
        const FolderSessionRecord* record = m_registry.FindBySessionId(sessionId);
        if (record != NULL)
        {
            //落地根目录取自接收进度对象：它才知道自动改名之后真正建出来的是哪个目录。
            if (record->m_recvState)
                notice->m_localRoot = record->m_recvState->GetRootPath();
            notice->m_totalFiles = record->m_totalFiles;
            notice->m_totalBytes = record->m_totalBytes;
        }
    }
    notice->m_success = false;
    notice->m_errCode = errCode;
    notice->m_errMsg = errMsg;

    LOG_ERROR("Folder recv: failed, session=%s, errcode=%d, errmsg=%s.",
              sessionId.c_str(), errCode, errMsg.c_str());
    PostToUi(FMG_MSG_V2_FOLDER_RECV_RESULT, notice);
}

// ======================== UI 线程收尾 ========================

void FolderTransferManager::FinishSend(const FolderResultNotice& result)
{
    //Stop 会等驱动线程退出，故只能在 UI 线程做（状态机的回调里调等于让它 join 自己）。
    StopSessionById(result.m_sessionId);

    {
        std::lock_guard<std::mutex> guard(m_mtx);
        FolderSessionRecord* record = m_registry.FindBySessionId(result.m_sessionId);
        if (record != NULL)
        {
            if (result.m_success)
            {
                m_registry.Erase(result.m_sessionId);
            }
            else
            {
                //保留清单与会话 id：保留期内点「重试」要靠它们复用同一份清单断点续传。
                //名额则立刻放开 —— 用户不必等这一笔的保留期过完才能另发一笔。
                m_registry.MarkFailed(*record, (int64_t)::GetTickCount64());
            }
        }
    }

    SendClose(result.m_sessionId, result.m_success, result.m_errCode, result.m_errMsg);
}

void FolderTransferManager::FinishRecv(const FolderResultNotice& result)
{
    StopSessionById(result.m_sessionId);

    {
        std::lock_guard<std::mutex> guard(m_mtx);
        FolderSessionRecord* record = m_registry.FindBySessionId(result.m_sessionId);
        if (record != NULL)
        {
            if (result.m_success)
            {
                m_registry.Erase(result.m_sessionId);
            }
            else
            {
                //接收进度（m_recvState）照旧留着 —— 已落盘的部分与增量 MD5 上下文都在里面，
                //重试时原样交给新会话才能接着写，而不是从零开始。
                m_registry.MarkFailed(*record, (int64_t)::GetTickCount64());
            }
        }
    }

    SendClose(result.m_sessionId, result.m_success, result.m_errCode, result.m_errMsg);
}

} // namespace foldertransfer
