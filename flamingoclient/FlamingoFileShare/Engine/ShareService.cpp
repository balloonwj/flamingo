/**
 *  文件共享 DLL 的业务总控 CShareService 的实现，见 ShareService.h。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "ShareService.h"

#include <objidl.h>
#include <gdiplus.h>

#include <shlobj.h>   //SHCreateDirectoryExW
#include <string.h>   //memset

#include "ShareBrowseModel.h"
#include "ShareCreateModel.h"
#include "ShareFileOps.h"
#include "SharePathGuard.h"
#include "ShareReconcile.h"
#include "ShareTransferPlan.h"
#include "ShareWinPath.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/net/Protocol.h"
#include "../../Source/Feature/P2P/FolderP2PTransport.h"   //FolderP2PLinkBroker：文件共享传输的 P2P 业务方（第 6 期）

namespace fileshare {

namespace {

//本机库在个人目录下的相对位置（与 msg.db 等同在 ChatLog 目录，方案 7.6）
const wchar_t* const kStoreSubDir = L"ChatLog";

//图片预览图的缓存目录名，放在本机库所在的 ChatLog 目录下
const wchar_t* const kPreviewSubDir = L"FileSharePreview";

//拖出的临时下载目录的上一级目录名，同样放在 ChatLog 目录下
const wchar_t* const kDragSubDir = L"FileShareDrag";

//本机库的文件名
const wchar_t* const kStoreFileName = L"fileshare.db";

//拖出临时目录名的缓冲区字符数
const size_t kDragNameBufChars = 64;

//2225 的 result 取值：1 表示失败
const int kTransferResultFail = 1;

//当前的本机时间（Unix 毫秒），通知里显示「时:分」用
int64_t WallNowMs()
{
    FILETIME ft;
    ::GetSystemTimeAsFileTime(&ft);
    return ShareFileOps::FileTimeToUnixMs(ft);
}

//本机路径的最后一段（文件或文件夹名）；盘符根目录等没有最后一段时为整个路径去掉结尾反斜杠
std::wstring LocalLastSegment(const std::wstring& path)
{
    std::wstring trimmed = path;
    while (!trimmed.empty() && trimmed[trimmed.size() - 1] == L'\\')
        trimmed.erase(trimmed.size() - 1);
    const size_t pos = trimmed.find_last_of(L'\\');
    return (pos == std::wstring::npos) ? trimmed : trimmed.substr(pos + 1);
}

} // namespace

//一个访问状态机与它的传输层、通道。状态机经本类（IShareVisitorTransport）发信令、连中转、发应用帧、关中转。
//第 4 期起状态机的观察者也是本类：各项回调转给挂着的浏览窗口（窗口关闭后为空，传输在后台继续），状态变化另外报告业务总控，
//由它发出排队的传输或以失败结束它们
class CShareService::VisitorLink : public IShareVisitorTransport, public IShareVisitorObserver
{
public:
    VisitorLink(CShareService* service, int64_t shareId, IShareVisitorObserver* window)
        : m_service(service)
        , m_window(window)
        , m_attached(window != NULL)
        , m_visitor(shareId, this, this)
    {
    }

    //状态变化：先转告窗口（界面先换成新状态），再报告业务总控
    virtual void OnVisitorStateChanged(int state, int errCode) override
    {
        if (m_window != NULL)
            m_window->OnVisitorStateChanged(state, errCode);
        m_service->OnVisitorLinkState(m_visitor.GetShareId(), state, errCode);
    }

    //以下各项只转告窗口；窗口已关闭时丢弃（后台传输不需要这些结果）
    virtual void OnVisitorListed(const std::string& path, int code, const std::vector<ShareEntry>& entries) override
    {
        if (m_window != NULL)
            m_window->OnVisitorListed(path, code, entries);
    }

    virtual void OnVisitorPermsChanged(int perms) override
    {
        if (m_window != NULL)
            m_window->OnVisitorPermsChanged(perms);
    }

    virtual void OnVisitorShareState(int state, int reason) override
    {
        if (m_window != NULL)
            m_window->OnVisitorShareState(state, reason);
    }

    virtual void OnVisitorOpResult(int64_t reqId, int opType, int code, const std::vector<ShareOpItem>& items) override
    {
        if (m_window != NULL)
            m_window->OnVisitorOpResult(reqId, opType, code, items);
    }

    virtual void OnVisitorThumbResult(int64_t reqId, const std::string& path, int code, int width, int height,
                                      const std::string& jpeg) override
    {
        if (m_window != NULL)
            m_window->OnVisitorThumbResult(reqId, path, code, width, height, jpeg);
    }

    virtual void OnVisitorTreeResult(int64_t reqId, const std::string& path, const std::vector<ShareEntry>& entries,
                                     bool truncated, int code) override
    {
        if (m_window != NULL)
            m_window->OnVisitorTreeResult(reqId, path, entries, truncated, code);
    }

    virtual void OnVisitorSearchResult(int64_t reqId, const std::vector<ShareEntry>& hits, bool done, bool truncated,
                                       int code) override
    {
        if (m_window != NULL)
            m_window->OnVisitorSearchResult(reqId, hits, done, truncated, code);
    }

    virtual uint32_t SendCommand(uint16_t cmd, const std::string& body) override
    {
        return m_service->SendCommand(cmd, body);
    }

    virtual bool StartRelay(const ShareRelayParams& relay) override
    {
        return m_service->StartVisitorChannel(*this, relay);
    }

    virtual bool SendFrame(const std::string& frame) override
    {
        return m_channel && m_channel->SendFrame(frame);
    }

    virtual void CloseRelay() override
    {
        m_service->CloseVisitorChannel(*this);
    }

    CShareService*                        m_service;   //所属的业务总控（不拥有）
    IShareVisitorObserver*                m_window;    //挂着的浏览窗口（不拥有）；窗口关闭后、没有窗口时发起的传输为 NULL
    bool                                  m_attached;  //是否挂着浏览窗口：为假且没有传输时，定时检查结束访问会话并销毁本对象
    CShareVisitor                         m_visitor;   //访问状态机；传输层与观察者都是本对象（须在上面三项之后构造）
    std::unique_ptr<CShareVisitorChannel> m_channel;   //当前的访问通道；没有连中转时为空
};

struct CShareService::TransferEntry
{
    uint32_t                               m_localId;     //本端编号
    int64_t                                m_shareId;     //共享 id
    int                                    m_direction;   //传输方向，share_transfer_direction
    bool                                   m_isOwner;     //本端是不是分享者
    std::string                            m_accessId;    //所属访问会话 id
    std::string                            m_transferId;  //传输子会话 id；访问者在 2224 应答之前为空
    uint32_t                               m_requestSeq;  //访问者 2224 请求的 seq；分享者为 0
    int64_t                                m_visitorId;   //访问者 userid（分享者一端审计用）
    std::vector<std::string>               m_items;       //下载：条目的共享内路径
    std::wstring                           m_targetDir;   //下载：本机目标目录
    foldertransfer::FolderManifest         m_manifest;    //上传：本机生成的清单
    std::wstring                           m_sourceRoot;  //上传：清单条目所在的本机目录
    std::string                            m_toDir;       //上传：共享内的目标目录
    std::unique_ptr<CShareTransferSession> m_session;     //传输子会话；2222 到达之前为空
    //本轮的 P2P 通道工厂（与交给传输子会话的是同一个对象）：取链路实际路径供「P2P」徽章；没有 P2P 时为空
    std::shared_ptr<balloonp2p::FolderP2PTransportFactory> m_p2pFactory;
    IShareTransferObserver*                m_observer;    //观察者（访问者一端的浏览窗口，不拥有，可为 NULL）
    int64_t                                m_lastSaveMs;  //访问者下载最近一次写续传记录的时间
    std::vector<int64_t>                   m_savedOffsets;//访问者下载：各文件最近一次写进续传记录的位点（下标为清单序号），
                                                          //只有位点变了的文件才重写，上万个文件时每次只写正在传的那几个
    bool                                   m_queued;      //访问者：在排队等同一共享上前面的传输结束，2224 还没有发
    std::vector<ShareTransferEntry>        m_requestItems;//访问者：2224 的顶层条目（排队的传输轮到时用它发请求）
    int64_t                                m_requestBytes;//访问者：2224 的总字节数
    int64_t                                m_requestFiles;//访问者：2224 的总文件数
    int64_t                                m_bytesDone;   //最近一次进度事件里已完成的字节数（「正在访问的人」显示进度用）
    int64_t                                m_bytesTotal;  //最近一次进度事件里的总字节数；还没有进度时为 0
    std::wstring                           m_label;       //访问者：传输列表与完成弹窗里显示的名称（第 4 期起）
    bool                                   m_dragOut;     //访问者：是不是拖出到资源管理器的下载（结束时不弹完成窗）
    bool                                   m_userCanceled;//访问者：是否由用户点「取消」结束（结束时不弹完成窗）
    std::shared_ptr<IShareTransferCompletion> m_completion;//访问者：结束回调（拖出任务；与发起方共同持有，可为空）
    std::vector<std::wstring>              m_localItems;  //访问者上传：本机条目的完整路径（「重试」时重新上传）

    TransferEntry()
        : m_localId(0)
        , m_shareId(0)
        , m_direction(0)
        , m_isOwner(false)
        , m_requestSeq(0)
        , m_visitorId(0)
        , m_observer(NULL)
        , m_lastSaveMs(0)
        , m_bytesDone(0)
        , m_bytesTotal(0)
        , m_queued(false)
        , m_requestBytes(0)
        , m_requestFiles(0)
        , m_dragOut(false)
        , m_userCanceled(false)
    {
    }
};

struct CShareService::ManageRequest
{
    uint32_t              m_requestId;      //请求编号
    int                   m_kind;           //请求种类，share_manage_kind
    uint32_t              m_seq;            //最近一次发送的帧 seq
    int64_t               m_deadlineMs;     //等待应答的截止时间（NowMs 的时间轴）
    int                   m_attempts;       //已发送的次数
    std::string           m_body;           //请求包体（发起共享超时重发时原样再发）
    std::string           m_clientShareId;  //发起共享的 clientshareid
    std::string           m_shareName;      //发起共享的名称（UTF-8）：成功后记进 m_ownShareNames，分享者操作通知里显示
    int64_t               m_shareId;        //取消与调整的共享 id
    IShareManageObserver* m_observer;       //观察者（不拥有，可为 NULL）

    ManageRequest()
        : m_requestId(0)
        , m_kind(0)
        , m_seq(0)
        , m_deadlineMs(0)
        , m_attempts(0)
        , m_shareId(0)
        , m_observer(NULL)
    {
    }
};

CShareService::CShareService()
    : m_host(NULL)
    , m_p2pBroker(new balloonp2p::FolderP2PLinkBroker(balloonp2p::kFileShareP2PPurpose))
    , m_notifyWnd(NULL)
    , m_notifyMsg(0)
    , m_userId(0)
    , m_listMineSeq(0)
    , m_lastRootCheckMs(0)
    , m_nextTransferId(1)
    , m_holdTransferQueue(false)
    , m_nextManageId(1)
    , m_manageTimeoutMs(kShareManageTimeoutMs)
    , m_gdiplusToken(0)
    , m_dragSeq(0)
    , m_cardStateListener(NULL)
    , m_finishListener(NULL)
    , m_activityListener(NULL)
{
}

CShareService::~CShareService()
{
    Detach();
    delete m_p2pBroker;
    m_p2pBroker = NULL;
}

void CShareService::Attach(IFileShareHost* host, HWND notifyWnd, UINT notifyMsg)
{
    m_host = host;
    //文件共享的传输优先走 P2P：exe 加载了 P2P 模块时以用途 fileshare 登记业务方。本 DLL 在首次登录之后才加载，那时
    //P2P 模块已经报到过一次；登记表一变它就会重新报到，服务端此后才会为文件共享的传输建 P2P 会话
    p2p::IP2PService* p2pService = host->GetP2PService();
    if (p2pService != NULL)
        m_p2pBroker->Register(p2pService);
    else
        LOG_INFO("share p2p: host has no p2p service, share transfers use the fileserver relay.");
    //缩略图的生成（分享者）与解码（访问者）都用 GDI+；不在 DllMain 里初始化，在这里（UI 线程上）初始化
    if (m_gdiplusToken == 0)
    {
        Gdiplus::GdiplusStartupInput input;
        if (Gdiplus::GdiplusStartup(&m_gdiplusToken, &input, NULL) != Gdiplus::Ok)
            m_gdiplusToken = 0;
    }
    //分享者一端的搜索与写操作在工作线程上执行（方案 7.5）
    m_workers.Start(kShareWorkerThreads);
    std::lock_guard<std::mutex> guard(m_eventMutex);
    m_notifyWnd = notifyWnd;
    m_notifyMsg = notifyMsg;
}

void CShareService::Detach()
{
    if (m_host == NULL)
        return;

    //进程退出：等待尚在连接中的线程结束，保证 DLL 卸载时没有线程还在执行本模块的代码
    FailAllManage();
    m_listRequests.clear();
    m_cardStates.clear();
    StopAllTransfers();
    //全部传输已停止，没有通道还在用 P2P 链路：注销业务方（还没被认领的链路随之关闭）
    m_p2pBroker->Unregister();
    CloseAllProviderSessions(error_code_ok, true);
    //提供会话都已销毁（在途任务的核心已断开、搜索已置位取消），等正在执行的任务结束；DLL 卸载前必须在这里停下
    m_workers.Stop();
    ReleaseAllVisitors(true);
    //工作线程都已结束：没有人再用 GDI+
    if (m_gdiplusToken != 0)
    {
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
        m_gdiplusToken = 0;
    }
    m_store.Close();
    m_storePath.clear();
    m_localStatus.clear();
    m_rootAvailable.clear();
    m_ownShareNames.clear();
    m_listMineSeq = 0;
    m_userId = 0;
    {
        std::lock_guard<std::mutex> guard(m_eventMutex);
        m_events.clear();
        m_notifyWnd = NULL;
    }
    m_host = NULL;
}

void CShareService::OnLoggedIn()
{
    if (m_host == NULL)
        return;

    //断线重连：服务端在连接断开时已结束了涉及本连接的访问会话，本地遗留的提供会话一并结束，不发 sf_bye
    CloseAllProviderSessions(CShareProviderSession::kShareNoBye, false);
    m_localStatus.clear();
    m_rootAvailable.clear();

    FileShareSessionInfo info;
    memset(&info, 0, sizeof(info));
    info.m_cbSize = sizeof(info);
    if (!m_host->GetSessionInfo(&info))
    {
        LOG_ERROR("file share: host did not provide session info, provider side disabled for this login.");
        return;
    }
    m_userId = info.m_userId;
    if (!OpenStore(info))
        return;
    RemoveStalePendingShares();

    const std::string body = ShareProtocolCodec::BuildListRequest(kShareListScopeMine, 0, 0);
    m_listMineSeq = SendCommand(cmd_share_list, body);
    if (m_listMineSeq != 0)
        LOG_INFO("share list request sent, seq: %u, userid: %lld, device: %s.", static_cast<unsigned>(m_listMineSeq),
                 static_cast<long long>(m_userId), info.m_deviceId);
}

void CShareService::OnLoggingOut()
{
    if (m_host == NULL)
        return;

    LOG_INFO("file share service: logging out, %u provider sessions to close.",
             static_cast<unsigned>(m_sessions.size()));
    FailAllManage();
    {
        //尚未有结果的列表请求以「没有应答」结束
        std::vector<uint32_t> seqs;
        for (std::map<uint32_t, ListRequest>::iterator it = m_listRequests.begin(); it != m_listRequests.end(); ++it)
            seqs.push_back(it->first);
        for (size_t i = 0; i < seqs.size(); ++i)
            FinishList(seqs[i], kShareManageNoResponse, std::vector<ShareListEntry>());
    }
    m_cardStates.clear();
    StopAllTransfers();
    CloseAllProviderSessions(error_code_ok, false);
    ReleaseAllVisitors(false);
    m_store.Close();
    m_storePath.clear();
    m_localStatus.clear();
    m_rootAvailable.clear();
    m_ownShareNames.clear();
    m_listMineSeq = 0;
    m_userId = 0;
}

void CShareService::HandleServerFrame(uint16_t cmd, uint32_t seq, const std::string& body)
{
    if (m_host == NULL)
        return;

    switch (cmd)
    {
    //拉取共享列表的应答
    case cmd_share_list:
        HandleListResponse(seq, body);
        break;

    //打开共享的应答：交给发出它的访问状态机
    case cmd_share_open:
        HandleOpenResponse(seq, body);
        break;

    //申请传输的应答：交给发出它的那次传输
    case cmd_share_request_transfer:
        HandleTransferResponse(seq, body);
        break;

    //共享对本人的状态变化
    case cmd_share_state_notify:
        HandleStateNotify(body);
        break;

    //成员权限变化（推给成员本人与分享者）
    case cmd_share_perms_notify:
        HandlePermsNotify(body);
        break;

    //中转参数：分享者一端据此建提供会话
    case cmd_share_relay_info:
        HandleRelayInfo(body);
        break;

    //访问会话或传输子会话已结束
    case cmd_share_access_state_notify:
        HandleAccessEnded(body);
        break;

    //发起、取消、调整成员的应答：交给发出它的那次管理请求
    case cmd_share_create:
    case cmd_share_cancel:
    case cmd_share_update_members:
        HandleManageResponse(cmd, seq, body);
        break;

    //只有结果码的几种应答：记日志
    case cmd_share_serve_report:
    case cmd_share_close_access:
    case cmd_share_transfer_close:
    case cmd_share_audit_report:
        HandleSimpleResponse(cmd, seq, body);
        break;

    //其余命令（拉取访问者一端的共享列表等）本类不处理
    default:
        break;
    }
}

void CShareService::DrainEvents()
{
    std::deque<ShareSessionEvent> events;
    {
        std::lock_guard<std::mutex> guard(m_eventMutex);
        events.swap(m_events);
    }
    for (size_t i = 0; i < events.size(); ++i)
        HandleEvent(events[i]);
}

void CShareService::PostSessionEvent(const ShareSessionEvent& ev)
{
    std::lock_guard<std::mutex> guard(m_eventMutex);
    if (m_notifyWnd == NULL)
        return;
    //队列由空变非空时才通知一次；UI 线程每次把队列整个取空，故不会漏处理
    const bool wasEmpty = m_events.empty();
    m_events.push_back(ev);
    if (wasEmpty)
        ::PostMessageW(m_notifyWnd, m_notifyMsg, 0, 0);
}

int CShareService::GetLocalStatus(int64_t shareId) const
{
    std::map<int64_t, int>::const_iterator it = m_localStatus.find(shareId);
    return (it == m_localStatus.end()) ? 0 : it->second;
}

size_t CShareService::GetProviderSessionCount() const
{
    return m_sessions.size() + m_closingSessions.size();
}

void CShareService::HandleListResponse(uint32_t seq, const std::string& body)
{
    ShareListResponse resp;
    if (!ShareProtocolCodec::ParseListResponse(body, resp))
    {
        LOG_WARNING("share list response not parsable, seq: %u.", static_cast<unsigned>(seq));
        return;
    }
    LOG_INFO("share list response, seq: %u, code: %d, shares: %u.", static_cast<unsigned>(seq), resp.m_code,
             static_cast<unsigned>(resp.m_shares.size()));

    //分享者的真实路径只给分享者本人看（2026-10-04 起）：服务端只在「我的共享」里带路径，这里再挡一道，
    //别人的共享即使带着路径也清空，「文件共享」窗口的列表、详情、搜索与会话内的共享浮层就都碰不到它
    for (size_t i = 0; i < resp.m_shares.size(); ++i)
    {
        if (resp.m_shares[i].m_ownerId != m_userId)
            resp.m_shares[i].m_displayPath.clear();
    }

    //每次应答都带着各共享对本人的最新状态，顺带更新卡片状态（这里不通知宿主：卡片状态以推送为准，列表只补缺）
    if (resp.m_code == error_code_ok)
    {
        for (size_t i = 0; i < resp.m_shares.size(); ++i)
        {
            const ShareListEntry& e = resp.m_shares[i];
            const bool mine = (e.m_ownerId == m_userId);
            UpdateCardState(e.m_shareId, e.m_state, e.m_reason, mine ? -1 : e.m_myPerms, false);
            //本人发起的共享的名称：分享者操作通知里显示（第 4 期起）
            if (mine && !e.m_name.empty())
                m_ownShareNames[e.m_shareId] = e.m_name;
        }
    }

    //界面发起的列表请求：交给它的观察者
    if (seq != 0 && m_listRequests.find(seq) != m_listRequests.end())
    {
        FinishList(seq, resp.m_code, resp.m_shares);
        return;
    }

    //只处理本类发出的「我的共享」请求；其余 2205 应答属于界面（第 13 步起）
    if (seq == 0 || seq != m_listMineSeq)
        return;
    m_listMineSeq = 0;
    if (resp.m_code != error_code_ok || !m_store.IsOpen())
        return;

    std::vector<ShareLocalRecord> local;
    if (!m_store.ListShares(local))
    {
        LOG_ERROR("share reconcile skipped: local store list failed.");
        return;
    }

    const ShareReconcilePlan plan = ShareReconcile::Plan(resp.m_shares, local);
    for (size_t i = 0; i < plan.m_removeLocal.size(); ++i)
        m_store.RemoveShare(plan.m_removeLocal[i]);
    for (size_t i = 0; i < plan.m_missingLocal.size(); ++i)
        m_localStatus[plan.m_missingLocal[i]] = share_local_status_missing_local;
    for (size_t i = 0; i < plan.m_otherDevice.size(); ++i)
        m_localStatus[plan.m_otherDevice[i]] = share_local_status_other_device;

    std::vector<ShareServeItem> items;
    for (size_t i = 0; i < plan.m_report.size(); ++i)
    {
        const ShareLocalRecord& rec = plan.m_report[i];
        const bool available = ShareFileOps::IsRootAvailable(rec.m_rootPath, rec.m_kind);
        items.push_back(ShareReconcile::MakeServeItem(rec.m_shareId, available));
        m_localStatus[rec.m_shareId] = share_local_status_reported;
        m_rootAvailable[rec.m_shareId] = available;
    }
    LOG_INFO("share reconcile done, report: %u, missing local: %u, other device: %u, removed local: %u.",
             static_cast<unsigned>(plan.m_report.size()), static_cast<unsigned>(plan.m_missingLocal.size()),
             static_cast<unsigned>(plan.m_otherDevice.size()), static_cast<unsigned>(plan.m_removeLocal.size()));
    if (!items.empty())
        SendServeReport(items);
}

void CShareService::HandleStateNotify(const std::string& body)
{
    int64_t shareId = 0;
    int state = 0;
    int reason = 0;
    if (!ShareProtocolCodec::ParseStateNotify(body, shareId, state, reason))
    {
        LOG_WARNING("share state notify not parsable.");
        return;
    }

    //卡片状态：主程序据此刷新已打开窗口里的卡片
    UpdateCardState(shareId, state, reason, -1, true);

    //本人是成员、正在浏览这个共享：转告浏览窗口
    std::map<int64_t, std::unique_ptr<VisitorLink> >::iterator visitor = m_visitors.find(shareId);
    if (visitor != m_visitors.end())
        visitor->second->m_visitor.OnShareStateNotify(state, reason);

    if (state != share_state_invalid)
        return;
    DropOwnShare(shareId, reason);
}

void CShareService::DropOwnShare(int64_t shareId, int reason)
{
    //本机若是它的分享者，删除本机记录并结束该共享上的全部提供会话
    ShareLocalRecord rec;
    if (!m_store.IsOpen() || !m_store.GetByShareId(shareId, rec))
        return;
    m_store.RemoveShare(shareId);
    m_localStatus.erase(shareId);
    m_rootAvailable.erase(shareId);
    CancelTransfersOfShare(shareId, true, 0);

    std::vector<std::string> accessIds;
    for (SessionMap::iterator it = m_sessions.begin(); it != m_sessions.end(); ++it)
    {
        if (it->second->GetShareId() == shareId)
            accessIds.push_back(it->first);
    }
    for (size_t i = 0; i < accessIds.size(); ++i)
        CloseProviderSession(accessIds[i], error_code_share_canceled);
    LOG_INFO("share invalidated, local record removed, shareid: %lld, reason: %d, sessions closed: %u.",
             static_cast<long long>(shareId), reason, static_cast<unsigned>(accessIds.size()));
}

void CShareService::HandlePermsNotify(const std::string& body)
{
    int64_t shareId = 0;
    int64_t userId = 0;
    int perms = 0;
    if (!ShareProtocolCodec::ParsePermsNotify(body, shareId, userId, perms))
    {
        LOG_WARNING("share perms notify not parsable.");
        return;
    }

    //访问者一端：推给本人的是本人在这个共享上的新权限
    if (userId == m_userId)
    {
        std::map<int64_t, ShareCardState>::const_iterator known = m_cardStates.find(shareId);
        if (known != m_cardStates.end())
            UpdateCardState(shareId, known->second.m_state, known->second.m_reason, perms, true);
        else
            UpdateCardState(shareId, 0, 0, perms, true);
        std::map<int64_t, std::unique_ptr<VisitorLink> >::iterator visitor = m_visitors.find(shareId);
        if (visitor != m_visitors.end())
            visitor->second->m_visitor.OnPermsNotify(perms);
    }

    //分享者一端：正在进行的上传按新权限检查，上传权限被收回时立即中止（方案 8.7）
    for (std::map<uint32_t, std::unique_ptr<TransferEntry> >::iterator it = m_transfers.begin(); it != m_transfers.end(); ++it)
    {
        TransferEntry& entry = *it->second;
        if (!entry.m_isOwner || entry.m_shareId != shareId || entry.m_visitorId != userId || !entry.m_session)
            continue;
        COwnerUploadHooks* upload = entry.m_session->GetOwnerUploadHooks();
        if (upload == NULL)
            continue;
        upload->SetPerms(perms);
        if ((perms & share_perm_upload) == 0)
            entry.m_session->Cancel();
    }

    //分享者一端：更新该访问者在这个共享上全部会话的权限
    size_t updated = 0;
    for (SessionMap::iterator it = m_sessions.begin(); it != m_sessions.end(); ++it)
    {
        if (it->second->GetShareId() == shareId && it->second->GetVisitorId() == userId)
        {
            it->second->SetPerms(perms);
            ++updated;
        }
    }
    if (updated > 0)
        LOG_INFO("share perms updated, shareid: %lld, visitor: %lld, perms: %d, sessions: %u.",
                 static_cast<long long>(shareId), static_cast<long long>(userId), perms,
                 static_cast<unsigned>(updated));
}

void CShareService::HandleRelayInfo(const std::string& body)
{
    ShareRelayParams relay;
    if (!ShareProtocolCodec::ParseRelayInfo(body, relay))
    {
        LOG_WARNING("share relay info not parsable.");
        return;
    }
    //推给访问者的传输子会话：按 transferid 建会话
    if (!relay.m_forOwner && !relay.m_transferId.empty())
    {
        StartVisitorTransferSession(relay);
        return;
    }
    //推给访问者的那一帧：交给 accessid 对得上的访问状态机
    if (!relay.m_forOwner)
    {
        const int64_t now = NowMs();
        for (std::map<int64_t, std::unique_ptr<VisitorLink> >::iterator it = m_visitors.begin();
             it != m_visitors.end(); ++it)
        {
            if (it->second->m_visitor.OnRelayInfo(relay, now))
                return;
        }
        LOG_INFO("share relay info for visitor not matched, accessid: %s, transferid: %s.", relay.m_accessId.c_str(),
                 relay.m_transferId.c_str());
        return;
    }
    //推给分享者的传输子会话：按方向建发送端或接收端
    if (!relay.m_transferId.empty())
    {
        StartOwnerTransferSession(relay);
        return;
    }
    if (m_sessions.find(relay.m_accessId) != m_sessions.end())
    {
        LOG_WARNING("share relay info duplicated, accessid: %s.", relay.m_accessId.c_str());
        return;
    }

    ShareLocalRecord rec;
    if (!m_store.IsOpen() || !m_store.GetByShareId(relay.m_shareId, rec))
    {
        LOG_WARNING("share access refused: no local record, shareid: %lld, accessid: %s.",
                    static_cast<long long>(relay.m_shareId), relay.m_accessId.c_str());
        SendCloseAccess(relay.m_accessId, error_code_share_unavailable);
        return;
    }
    if (!ShareFileOps::IsRootAvailable(rec.m_rootPath, rec.m_kind))
    {
        LOG_WARNING("share access refused: root missing, shareid: %lld, accessid: %s.",
                    static_cast<long long>(relay.m_shareId), relay.m_accessId.c_str());
        SendCloseAccess(relay.m_accessId, error_code_share_root_missing);
        //顺带补报一次：服务端此后不再把这个共享当作可用
        SendServeReport(std::vector<ShareServeItem>(1, ShareReconcile::MakeServeItem(relay.m_shareId, false)));
        m_rootAvailable[relay.m_shareId] = false;
        return;
    }

    ShareProviderContext ctx;
    ctx.m_accessId = relay.m_accessId;
    ctx.m_shareId = relay.m_shareId;
    ctx.m_visitorId = relay.m_visitorId;
    ctx.m_visitorName = relay.m_visitorName;
    ctx.m_rootPath = rec.m_rootPath;
    ctx.m_kind = rec.m_kind;
    ctx.m_includeHidden = rec.m_includeHidden;
    ctx.m_perms = relay.m_perms;
    ctx.m_searchMaxResults = relay.m_searchMaxResults;
    ctx.m_searchTimeoutMs = relay.m_searchTimeoutMs;
    ctx.m_pool = &m_workers;

    std::unique_ptr<CShareProviderSession> session(
        new CShareProviderSession(ctx, relay, m_host->GetServerPublicKey(), this));
    session->StartConnect();
    m_sessions[relay.m_accessId] = std::move(session);
    LOG_INFO("share provider session started, accessid: %s, shareid: %lld, visitor: %lld (%s), perms: %d.",
             relay.m_accessId.c_str(), static_cast<long long>(relay.m_shareId),
             static_cast<long long>(relay.m_visitorId), relay.m_visitorName.c_str(), relay.m_perms);
}

void CShareService::HandleAccessEnded(const std::string& body)
{
    std::string accessId;
    std::string transferId;
    int errCode = 0;
    if (!ShareProtocolCodec::ParseAccessEnded(body, accessId, transferId, errCode))
    {
        LOG_WARNING("share access ended notify not parsable.");
        return;
    }
    //传输子会话已结束（对端关闭、名额收回、成员被移除等）：取消本端的会话，结果经失败事件收尾
    if (!transferId.empty())
    {
        const uint32_t localId = FindTransfer(transferId);
        if (localId == 0)
            return;
        TransferEntry& entry = *m_transfers[localId];
        LOG_INFO("share transfer ended by server, transferid: %s, errcode: %d.", transferId.c_str(), errCode);
        if (entry.m_session)
            entry.m_session->Cancel();
        else
            FinishTransfer(localId, errCode != error_code_ok ? errCode : error_code_share_transfer_canceled);
        return;
    }
    //访问会话结束时它下面的传输子会话也随之结束（两种角色都是），中转上的数据通道不会因访问会话结束而断开，须本端取消
    const size_t ended = EndTransfersOfAccess(accessId, errCode);
    if (ended > 0)
        LOG_INFO("share transfers ended with their access session, accessid: %s, transfers: %u, errcode: %d.",
                 accessId.c_str(), static_cast<unsigned>(ended), errCode);
    //访问会话已在服务端结束：不再发 sf_bye
    if (CloseProviderSession(accessId, CShareProviderSession::kShareNoBye))
    {
        LOG_INFO("share provider session ended by server, accessid: %s, errcode: %d.", accessId.c_str(), errCode);
        return;
    }
    //访问者一端
    for (std::map<int64_t, std::unique_ptr<VisitorLink> >::iterator it = m_visitors.begin(); it != m_visitors.end(); ++it)
    {
        if (it->second->m_visitor.OnAccessEnded(accessId, errCode))
        {
            LOG_INFO("share visitor access ended by server, accessid: %s, errcode: %d.", accessId.c_str(), errCode);
            return;
        }
    }
}

void CShareService::HandleSimpleResponse(uint16_t cmd, uint32_t seq, const std::string& body)
{
    int code = 0;
    std::string msg;
    if (!ShareProtocolCodec::ParseSimpleResponse(body, code, msg))
    {
        LOG_WARNING("share response not parsable, cmd: %u, seq: %u.", static_cast<unsigned>(cmd),
                    static_cast<unsigned>(seq));
        return;
    }
    if (code == error_code_ok)
        LOG_INFO("share response ok, cmd: %u, seq: %u.", static_cast<unsigned>(cmd), static_cast<unsigned>(seq));
    else
        LOG_WARNING("share response failed, cmd: %u, seq: %u, code: %d, msg: %s.", static_cast<unsigned>(cmd),
                    static_cast<unsigned>(seq), code, msg.c_str());
}

std::wstring CShareService::GetPreviewCacheDir()
{
    if (m_storePath.empty())
        return std::wstring();
    const size_t pos = m_storePath.find_last_of(L'\\');
    if (pos == std::wstring::npos)
        return std::wstring();
    const std::wstring dir = m_storePath.substr(0, pos) + L"\\" + kPreviewSubDir;
    const int mk = ::SHCreateDirectoryExW(NULL, dir.c_str(), NULL);
    if (mk != ERROR_SUCCESS && mk != ERROR_ALREADY_EXISTS && mk != ERROR_FILE_EXISTS)
        return std::wstring();
    return dir;
}

std::wstring CShareService::NewDragTempDir()
{
    if (m_storePath.empty())
        return std::wstring();
    const size_t pos = m_storePath.find_last_of(L'\\');
    if (pos == std::wstring::npos)
        return std::wstring();
    const std::wstring root = m_storePath.substr(0, pos) + L"\\" + kDragSubDir;
    const int mk = ::SHCreateDirectoryExW(NULL, root.c_str(), NULL);
    if (mk != ERROR_SUCCESS && mk != ERROR_ALREADY_EXISTS && mk != ERROR_FILE_EXISTS)
    {
        LOG_ERROR(L"file share: cannot create drag temp root %s, error %d.", root.c_str(), mk);
        return std::wstring();
    }
    wchar_t name[kDragNameBufChars] = { 0 };
    _snwprintf_s(name, kDragNameBufChars, _TRUNCATE, L"%llu_%u", static_cast<unsigned long long>(::GetTickCount64()),
                 static_cast<unsigned>(++m_dragSeq));
    return root + L"\\" + name;
}

bool CShareService::OpenStore(const FileShareSessionInfo& info)
{
    std::wstring dir(info.m_personalDir);
    if (dir.empty())
    {
        LOG_ERROR("file share: personal dir is empty, local store not opened.");
        return false;
    }
    if (dir[dir.size() - 1] != L'\\')
        dir += L'\\';
    dir += kStoreSubDir;
    const std::wstring path = dir + L"\\" + kStoreFileName;

    //断线重连且账号没变：库已经开着，不重开
    if (m_store.IsOpen() && path == m_storePath)
        return true;

    m_store.Close();
    m_storePath.clear();
    const int mk = ::SHCreateDirectoryExW(NULL, dir.c_str(), NULL);
    if (mk != ERROR_SUCCESS && mk != ERROR_ALREADY_EXISTS && mk != ERROR_FILE_EXISTS)
    {
        LOG_ERROR(L"file share: cannot create dir %s, error %d.", dir.c_str(), mk);
        return false;
    }
    if (!m_store.Open(path.c_str()))
    {
        LOG_ERROR(L"file share: cannot open local store %s.", path.c_str());
        return false;
    }
    m_storePath = path;
    LOG_INFO(L"file share local store opened: %s.", path.c_str());
    //上次进程异常退出时没来得及删除的拖出临时目录：本账号刚登录，还没有进行中的拖出，整个删掉
    ShareDeleteTree(dir + L"\\" + kDragSubDir);
    return true;
}

uint32_t CShareService::SendCommand(uint16_t cmd, const std::string& body)
{
    if (m_host == NULL)
        return 0;
    const uint32_t seq = m_host->SendShareCommand(cmd, body.data(), static_cast<uint32_t>(body.size()));
    if (seq == 0)
        LOG_WARNING("share command not sent, cmd: %u (not connected?).", static_cast<unsigned>(cmd));
    return seq;
}

void CShareService::SendServeReport(const std::vector<ShareServeItem>& items)
{
    const uint32_t seq = SendCommand(cmd_share_serve_report, ShareProtocolCodec::BuildServeReport(items));
    for (size_t i = 0; i < items.size(); ++i)
        LOG_INFO("share serve report item, shareid: %lld, available: %d, reason: %d, seq: %u.",
                 static_cast<long long>(items[i].m_shareId), items[i].m_available ? 1 : 0, items[i].m_reason,
                 static_cast<unsigned>(seq));
}

void CShareService::SendCloseAccess(const std::string& accessId, int reason)
{
    SendCommand(cmd_share_close_access, ShareProtocolCodec::BuildCloseAccessRequest(accessId, reason));
}

bool CShareService::CloseProviderSession(const std::string& accessId, int byeErrCode)
{
    SessionMap::iterator it = m_sessions.find(accessId);
    if (it == m_sessions.end())
        return false;
    std::unique_ptr<CShareProviderSession> session(std::move(it->second));
    m_sessions.erase(it);
    //连接线程仍在进行：等它的连接结果事件到达后再关
    if (!session->Close(byeErrCode))
        m_closingSessions[accessId] = std::move(session);
    return true;
}

void CShareService::CloseAllProviderSessions(int byeErrCode, bool waitConnecting)
{
    std::vector<std::string> accessIds;
    for (SessionMap::iterator it = m_sessions.begin(); it != m_sessions.end(); ++it)
        accessIds.push_back(it->first);
    for (size_t i = 0; i < accessIds.size(); ++i)
        CloseProviderSession(accessIds[i], byeErrCode);

    if (!waitConnecting)
        return;
    for (SessionMap::iterator it = m_closingSessions.begin(); it != m_closingSessions.end(); ++it)
    {
        it->second->WaitConnectThread();
        it->second->Close(CShareProviderSession::kShareNoBye);
    }
    m_closingSessions.clear();
}

void CShareService::HandleEvent(const ShareSessionEvent& ev)
{
    //传输子会话的事件
    if (ev.m_type >= share_transfer_event_progress)
    {
        HandleTransferEvent(ev);
        return;
    }
    //访问通道的事件另行处理
    if (ev.m_type >= share_visitor_event_connected)
    {
        HandleVisitorEvent(ev);
        return;
    }

    //写操作已在分享者电脑上执行：无论会话此刻是否已在关闭，审计都要上报；成功的部分另外通知分享者（第 4 期起）
    if (ev.m_type == share_provider_event_write_done)
    {
        ReportWriteAudit(ev.m_audits);
        NotifyOwnerWrite(ev.m_audits);
        return;
    }

    //已要求关闭的会话：连接线程结束（无论成败）后完成关闭；其余事件忽略
    SessionMap::iterator closing = m_closingSessions.find(ev.m_accessId);
    if (closing != m_closingSessions.end())
    {
        if (closing->second->Close(CShareProviderSession::kShareNoBye))
            m_closingSessions.erase(closing);
        return;
    }
    if (m_sessions.find(ev.m_accessId) == m_sessions.end())
        return;

    switch (ev.m_type)
    {
    //连接成功：sf_hello 已在连接线程上发出，这里只记日志
    case share_provider_event_connected:
        LOG_INFO("share provider session ready, accessid: %s.", ev.m_accessId.c_str());
        break;

    //连接失败：结束会话，并告诉服务端（访问者随即收到 2223）
    case share_provider_event_connect_failed:
        CloseProviderSession(ev.m_accessId, CShareProviderSession::kShareNoBye);
        SendCloseAccess(ev.m_accessId, error_code_share_relay_broken);
        LOG_WARNING("share provider session failed to connect relay, accessid: %s.", ev.m_accessId.c_str());
        break;

    //访问者发来 sf_bye：访问者自己会发 2221，这里只关闭本端
    case share_provider_event_visitor_bye:
        CloseProviderSession(ev.m_accessId, CShareProviderSession::kShareNoBye);
        LOG_INFO("share provider session closed by visitor, accessid: %s, errcode: %d.", ev.m_accessId.c_str(),
                 ev.m_errCode);
        break;

    //通道异常断开：结束会话并告诉服务端（服务端的结束是幂等的，访问者若也报了不会重复处理）
    case share_provider_event_channel_closed:
        CloseProviderSession(ev.m_accessId, CShareProviderSession::kShareNoBye);
        SendCloseAccess(ev.m_accessId, error_code_share_relay_broken);
        LOG_WARNING("share provider session channel broken, accessid: %s.", ev.m_accessId.c_str());
        break;

    //未知事件
    default:
        break;
    }
}

CShareVisitor* CShareService::AcquireVisitor(int64_t shareId, IShareVisitorObserver* observer)
{
    if (m_host == NULL)
        return NULL;
    std::map<int64_t, std::unique_ptr<VisitorLink> >::iterator it = m_visitors.find(shareId);
    if (it != m_visitors.end())
    {
        //窗口关闭后仍在后台传输的状态机（第 4 期起）：挂回新窗口，不再被定时检查回收
        const bool wasDetached = !it->second->m_attached;
        it->second->m_window = observer;
        it->second->m_attached = (observer != NULL);
        if (wasDetached)
            LOG_INFO("share visitor reattached to a browser, shareid: %lld, state: %d.", static_cast<long long>(shareId),
                     it->second->m_visitor.GetState());
        return &it->second->m_visitor;
    }
    return &CreateVisitorLink(shareId, observer)->m_visitor;
}

CShareService::VisitorLink* CShareService::CreateVisitorLink(int64_t shareId, IShareVisitorObserver* window)
{
    std::unique_ptr<VisitorLink> link(new VisitorLink(this, shareId, window));
    VisitorLink* raw = link.get();
    m_visitors[shareId] = std::move(link);
    LOG_INFO("share visitor created, shareid: %lld, with browser: %d.", static_cast<long long>(shareId), window != NULL ? 1 : 0);
    return raw;
}

void CShareService::ReleaseVisitor(int64_t shareId)
{
    std::map<int64_t, std::unique_ptr<VisitorLink> >::iterator it = m_visitors.find(shareId);
    if (it == m_visitors.end())
        return;
    //浏览窗口关闭：此后不再回调它，它发起的传输也不再回调它
    it->second->m_window = NULL;
    it->second->m_attached = false;
    for (std::map<uint32_t, std::unique_ptr<TransferEntry> >::iterator t = m_transfers.begin(); t != m_transfers.end(); ++t)
    {
        if (!t->second->m_isOwner && t->second->m_shareId == shareId)
            t->second->m_observer = NULL;
    }
    //还有传输：访问会话保留，传输在后台继续（第 4 期起；服务端结束访问会话时会连带结束它下面的传输），全部结束后由定时检查回收
    if (HasVisitorTransfers(shareId))
    {
        LOG_INFO("share visitor detached from its browser, transfers continue in the background, shareid: %lld.",
                 static_cast<long long>(shareId));
        return;
    }
    DestroyVisitorLink(shareId);
}

void CShareService::DestroyVisitorLink(int64_t shareId)
{
    std::map<int64_t, std::unique_ptr<VisitorLink> >::iterator it = m_visitors.find(shareId);
    if (it == m_visitors.end())
        return;
    std::unique_ptr<VisitorLink> link(std::move(it->second));
    m_visitors.erase(it);
    //残留的传输（退出登录、模块关闭时已先行全部结束，正常不会有）随访问会话一起结束，不再回调窗口
    for (std::map<uint32_t, std::unique_ptr<TransferEntry> >::iterator t = m_transfers.begin(); t != m_transfers.end(); ++t)
    {
        if (!t->second->m_isOwner && t->second->m_shareId == shareId)
            t->second->m_observer = NULL;
    }
    CancelTransfersOfShare(shareId, false, 0);
    //先解除窗口：不再回调它；再结束访问会话（会话已建立时发 sf_bye 与 2221）并关闭通道，通道仍在连接时由
    //CloseVisitorChannel 挂起。状态机此时已不在 m_visitors 里，它报告的「空闲」不会再触发任何处理
    link->m_window = NULL;
    link->m_attached = false;
    link->m_visitor.Close();
    CloseVisitorChannel(*link);
    LOG_INFO("share visitor released, shareid: %lld.", static_cast<long long>(shareId));
}

void CShareService::ReapDetachedVisitors()
{
    std::vector<int64_t> idle;
    for (std::map<int64_t, std::unique_ptr<VisitorLink> >::iterator it = m_visitors.begin(); it != m_visitors.end(); ++it)
    {
        if (!it->second->m_attached && !HasVisitorTransfers(it->first))
            idle.push_back(it->first);
    }
    for (size_t i = 0; i < idle.size(); ++i)
    {
        LOG_INFO("share visitor without browser has no more transfers, closing its access, shareid: %lld.",
                 static_cast<long long>(idle[i]));
        DestroyVisitorLink(idle[i]);
    }
}

bool CShareService::HasVisitorTransfers(int64_t shareId) const
{
    for (std::map<uint32_t, std::unique_ptr<TransferEntry> >::const_iterator it = m_transfers.begin(); it != m_transfers.end();
         ++it)
    {
        if (!it->second->m_isOwner && it->second->m_shareId == shareId)
            return true;
    }
    return false;
}

std::vector<ShareTransferSnapshot> CShareService::AdoptVisitorTransfers(int64_t shareId, IShareTransferObserver* observer)
{
    std::vector<ShareTransferSnapshot> out;
    for (std::map<uint32_t, std::unique_ptr<TransferEntry> >::iterator it = m_transfers.begin(); it != m_transfers.end(); ++it)
    {
        TransferEntry& entry = *it->second;
        if (entry.m_isOwner || entry.m_shareId != shareId)
            continue;
        entry.m_observer = observer;
        ShareTransferSnapshot snap;
        snap.m_localId = entry.m_localId;
        snap.m_direction = entry.m_direction;
        snap.m_label = entry.m_label;
        snap.m_bytesDone = entry.m_bytesDone;
        snap.m_bytesTotal = entry.m_bytesTotal;
        snap.m_queued = entry.m_queued;
        out.push_back(snap);
    }
    if (!out.empty())
        LOG_INFO("share browser adopted background transfers, shareid: %lld, transfers: %u.", static_cast<long long>(shareId),
                 static_cast<unsigned>(out.size()));
    return out;
}

void CShareService::SetNoticeListeners(IShareTransferFinishListener* finishListener, IShareOwnerActivityListener* activityListener)
{
    m_finishListener = finishListener;
    m_activityListener = activityListener;
}

bool CShareService::HasVisitor(int64_t shareId) const
{
    return m_visitors.find(shareId) != m_visitors.end();
}

bool CShareService::IsVisitorAttached(int64_t shareId) const
{
    std::map<int64_t, std::unique_ptr<VisitorLink> >::const_iterator it = m_visitors.find(shareId);
    return it != m_visitors.end() && it->second->m_attached;
}

void CShareService::OnVisitorLinkState(int64_t shareId, int state, int errCode)
{
    //状态机已被销毁（DestroyVisitorLink 先移出表再关闭）：它报告的「空闲」不做任何处理
    if (m_visitors.find(shareId) == m_visitors.end())
        return;
    //就绪：发出排在最前面的那一笔（前面没有进行中的传输时）
    if (state == share_visitor_ready)
    {
        StartNextQueuedTransfer(shareId);
        return;
    }
    //失败：排队的传输等不到会话了，以同一原因结束
    if (state == share_visitor_failed)
        FailQueuedTransfers(shareId, errCode != error_code_ok ? errCode : error_code_share_relay_broken);
}

void CShareService::FailQueuedTransfers(int64_t shareId, int errCode)
{
    std::vector<uint32_t> ids;
    for (std::map<uint32_t, std::unique_ptr<TransferEntry> >::iterator it = m_transfers.begin(); it != m_transfers.end(); ++it)
    {
        const TransferEntry& e = *it->second;
        if (!e.m_isOwner && e.m_shareId == shareId && e.m_queued)
            ids.push_back(it->first);
    }
    if (ids.empty())
        return;
    LOG_INFO("share queued transfers failed with their access session, shareid: %lld, transfers: %u, errcode: %d.",
             static_cast<long long>(shareId), static_cast<unsigned>(ids.size()), errCode);
    //逐个结束的过程中不发起排队的下一笔：它们都在这次要结束的范围内
    const bool hold = m_holdTransferQueue;
    m_holdTransferQueue = true;
    for (size_t i = 0; i < ids.size(); ++i)
        FinishTransfer(ids[i], errCode);
    m_holdTransferQueue = hold;
}

void CShareService::OnTimer()
{
    const int64_t now = NowMs();
    CheckManageTimeouts(now);
    {
        std::vector<uint32_t> expired;
        for (std::map<uint32_t, ListRequest>::iterator it = m_listRequests.begin(); it != m_listRequests.end(); ++it)
        {
            if (now >= it->second.m_deadlineMs)
                expired.push_back(it->first);
        }
        for (size_t i = 0; i < expired.size(); ++i)
            FinishList(expired[i], kShareManageNoResponse, std::vector<ShareListEntry>());
    }
    if (now - m_lastRootCheckMs >= kShareRootCheckIntervalMs)
    {
        m_lastRootCheckMs = now;
        CheckRootAvailability();
    }
    //先收集共享 id：OnTick 里可能回调界面，界面又可能释放状态机
    std::vector<int64_t> shareIds;
    for (std::map<int64_t, std::unique_ptr<VisitorLink> >::iterator it = m_visitors.begin(); it != m_visitors.end(); ++it)
        shareIds.push_back(it->first);
    for (size_t i = 0; i < shareIds.size(); ++i)
    {
        std::map<int64_t, std::unique_ptr<VisitorLink> >::iterator it = m_visitors.find(shareIds[i]);
        if (it != m_visitors.end())
            it->second->m_visitor.OnTick(now);
    }
    //浏览窗口已关闭、后台传输也都结束了的访问状态机：结束访问会话（第 4 期起）
    ReapDetachedVisitors();
}

void CShareService::HandleVisitorEvent(const ShareSessionEvent& ev)
{
    //已挂起关闭的通道：连接线程结束（无论成败）后完成关闭；其余事件忽略
    std::map<std::string, std::unique_ptr<CShareVisitorChannel> >::iterator closing = m_closingChannels.find(ev.m_accessId);
    if (closing != m_closingChannels.end())
    {
        if ((ev.m_type == share_visitor_event_connected || ev.m_type == share_visitor_event_connect_failed)
            && closing->second->Close())
            m_closingChannels.erase(closing);
        return;
    }

    VisitorLink* link = NULL;
    for (std::map<int64_t, std::unique_ptr<VisitorLink> >::iterator it = m_visitors.begin(); it != m_visitors.end(); ++it)
    {
        if (it->second->m_channel && it->second->m_channel->GetAccessId() == ev.m_accessId)
        {
            link = it->second.get();
            break;
        }
    }
    if (link == NULL)
        return;

    const int64_t now = NowMs();
    switch (ev.m_type)
    {
    //连上中转：状态机开始等 sf_hello
    case share_visitor_event_connected:
        link->m_visitor.OnRelayConnected(true, now);
        break;

    //连接失败：状态机回 2221 并进入失败
    case share_visitor_event_connect_failed:
        link->m_visitor.OnRelayConnected(false, now);
        break;

    //收到一个应用帧：解码后交给状态机
    case share_visitor_event_frame:
    {
        ShareControlMessage msg;
        if (!ShareControlFraming::DecodeFrame(ev.m_data.data(), ev.m_data.size(), msg))
        {
            LOG_WARNING("share visitor got an undecodable frame, accessid: %s, bytes: %u.", ev.m_accessId.c_str(),
                        static_cast<unsigned>(ev.m_data.size()));
            break;
        }
        link->m_visitor.OnFrame(msg, now);
        break;
    }

    //通道异常断开
    case share_visitor_event_channel_closed:
        link->m_visitor.OnRelayClosed(now);
        break;

    //未知事件
    default:
        break;
    }
}

void CShareService::HandleOpenResponse(uint32_t seq, const std::string& body)
{
    int code = 0;
    std::string msg;
    std::string accessId;
    std::string shareName;
    if (!ShareProtocolCodec::ParseOpenResponse(body, code, msg, accessId, shareName))
    {
        LOG_WARNING("share open response not parsable, seq: %u.", static_cast<unsigned>(seq));
        return;
    }
    LOG_INFO("share open response, seq: %u, code: %d, accessid: %s.", static_cast<unsigned>(seq), code, accessId.c_str());
    const int64_t now = NowMs();
    for (std::map<int64_t, std::unique_ptr<VisitorLink> >::iterator it = m_visitors.begin(); it != m_visitors.end(); ++it)
    {
        if (it->second->m_visitor.OnOpenResponse(seq, code, accessId, now))
        {
            //共享名称（2026-10-04 起服务端在成功时给出）：浏览窗口作标题用；没给时保留上次的
            if (!shareName.empty())
                it->second->m_visitor.SetShareName(shareName);
            return;
        }
    }
    //对应的浏览窗口已经关闭：服务端若建成了访问会话，立即结束它
    if (code == error_code_ok && !accessId.empty())
        SendCloseAccess(accessId, 0);
}

void CShareService::ReleaseAllVisitors(bool waitConnecting)
{
    std::vector<int64_t> shareIds;
    for (std::map<int64_t, std::unique_ptr<VisitorLink> >::iterator it = m_visitors.begin(); it != m_visitors.end(); ++it)
        shareIds.push_back(it->first);
    //退出登录与模块关闭：无论有没有窗口、有没有传输，一律结束（调用方已先结束全部传输）
    for (size_t i = 0; i < shareIds.size(); ++i)
        DestroyVisitorLink(shareIds[i]);

    if (!waitConnecting)
        return;
    for (std::map<std::string, std::unique_ptr<CShareVisitorChannel> >::iterator it = m_closingChannels.begin();
         it != m_closingChannels.end(); ++it)
    {
        it->second->WaitConnectThread();
        it->second->Close();
    }
    m_closingChannels.clear();
}

bool CShareService::StartVisitorChannel(VisitorLink& link, const ShareRelayParams& relay)
{
    if (m_host == NULL)
        return false;
    //上一条通道还在（不应发生）：先关闭
    CloseVisitorChannel(link);
    link.m_channel.reset(new CShareVisitorChannel(relay, m_host->GetServerPublicKey(), this));
    link.m_channel->StartConnect();
    LOG_INFO("share visitor channel connecting, accessid: %s, relay: %s:%d.", relay.m_accessId.c_str(),
             relay.m_relayIp.c_str(), relay.m_relayPort);
    return true;
}

void CShareService::CloseVisitorChannel(VisitorLink& link)
{
    if (!link.m_channel)
        return;
    std::unique_ptr<CShareVisitorChannel> channel(std::move(link.m_channel));
    if (!channel->Close())
        m_closingChannels[channel->GetAccessId()] = std::move(channel);
}

void CShareService::CheckRootAvailability()
{
    if (!m_store.IsOpen() || m_rootAvailable.empty())
        return;
    std::vector<ShareServeItem> changed;
    for (std::map<int64_t, bool>::iterator it = m_rootAvailable.begin(); it != m_rootAvailable.end(); ++it)
    {
        ShareLocalRecord rec;
        if (!m_store.GetByShareId(it->first, rec))
            continue;
        const bool available = ShareFileOps::IsRootAvailable(rec.m_rootPath, rec.m_kind);
        if (available == it->second)
            continue;
        it->second = available;
        changed.push_back(ShareReconcile::MakeServeItem(it->first, available));
        LOG_INFO("share root availability changed, shareid: %lld, available: %d.", static_cast<long long>(it->first),
                 available ? 1 : 0);
    }
    if (!changed.empty())
        SendServeReport(changed);
}

int64_t CShareService::NowMs()
{
    return static_cast<int64_t>(::GetTickCount64());
}

//==================== 传输子会话 ====================

uint32_t CShareService::StartDownload(int64_t shareId, const std::vector<ShareTransferEntry>& items,
                                      const std::wstring& targetDir, IShareTransferObserver* observer,
                                      const ShareTransferOptions& options)
{
    if (m_host == NULL || items.empty())
        return 0;
    //还没有访问状态机（浏览窗口没开时重试）：新建一个不挂窗口的，传输结束后由定时检查回收（第 4 期起）
    std::map<int64_t, std::unique_ptr<VisitorLink> >::iterator v = m_visitors.find(shareId);
    VisitorLink* link = (v != m_visitors.end()) ? v->second.get() : CreateVisitorLink(shareId, NULL);

    int64_t totalBytes = 0;
    int64_t totalFiles = 0;
    std::unique_ptr<TransferEntry> entry(new TransferEntry());
    for (size_t i = 0; i < items.size(); ++i)
    {
        totalBytes += items[i].m_size;
        if (!items[i].m_isDir)
            ++totalFiles;
        entry->m_items.push_back(items[i].m_path);
    }
    entry->m_shareId = shareId;
    entry->m_direction = share_transfer_download;
    entry->m_accessId = link->m_visitor.GetAccessId();
    entry->m_targetDir = targetDir;
    entry->m_observer = observer;
    entry->m_label = options.m_label;
    entry->m_dragOut = options.m_dragOut;
    entry->m_completion = options.m_completion;
    const uint32_t localId = SubmitVisitorTransfer(std::move(entry), items, totalBytes, totalFiles);
    LOG_INFO("share download requested, shareid: %lld, items: %u, local id: %u, queued: %d.", static_cast<long long>(shareId),
             static_cast<unsigned>(items.size()), localId, IsTransferQueued(localId) ? 1 : 0);
    return localId;
}

uint32_t CShareService::StartUpload(int64_t shareId, const std::vector<std::wstring>& localItems, const std::string& toDir,
                                    IShareTransferObserver* observer, int& errCode, const ShareTransferOptions& options)
{
    errCode = error_code_ok;
    if (m_host == NULL)
    {
        errCode = error_code_share_relay_broken;
        return 0;
    }

    std::unique_ptr<TransferEntry> entry(new TransferEntry());
    errCode = ShareTransferPlan::BuildUploadManifest(localItems, entry->m_manifest, entry->m_sourceRoot);
    if (errCode != error_code_ok)
        return 0;

    //2224 的条目是顶层条目：名字、是否目录、字节数（目录为其下文件之和）
    std::vector<ShareTransferEntry> items;
    int64_t totalBytes = 0;
    for (size_t i = 0; i < entry->m_manifest.m_files.size(); ++i)
        totalBytes += entry->m_manifest.m_files[i].m_sizeBytes;
    std::vector<foldertransfer::FolderRelayFileEntry> files;
    for (size_t i = 0; i < entry->m_manifest.m_files.size(); ++i)
    {
        foldertransfer::FolderRelayFileEntry f;
        f.m_relpath = entry->m_manifest.m_files[i].m_relativePath;
        f.m_size = entry->m_manifest.m_files[i].m_sizeBytes;
        files.push_back(f);
    }
    const std::vector<std::string> topNames = ShareTransferPlan::TopLevelNames(entry->m_manifest.m_dirs, files);
    for (size_t t = 0; t < topNames.size(); ++t)
    {
        ShareTransferEntry item;
        item.m_path = topNames[t];
        for (size_t d = 0; d < entry->m_manifest.m_dirs.size() && !item.m_isDir; ++d)
            item.m_isDir = (entry->m_manifest.m_dirs[d] == topNames[t]);
        for (size_t i = 0; i < files.size(); ++i)
        {
            if (ShareTransferPlan::ReplaceTopLevel(files[i].m_relpath, topNames[t], std::string()) != files[i].m_relpath)
                item.m_size += files[i].m_size;
        }
        items.push_back(item);
    }

    //清单生成成功之后才建访问状态机：清单失败时不必为它打开访问会话（浏览窗口没开时重试，第 4 期起）
    std::map<int64_t, std::unique_ptr<VisitorLink> >::iterator v = m_visitors.find(shareId);
    VisitorLink* link = (v != m_visitors.end()) ? v->second.get() : CreateVisitorLink(shareId, NULL);
    entry->m_shareId = shareId;
    entry->m_direction = share_transfer_upload;
    entry->m_accessId = link->m_visitor.GetAccessId();
    entry->m_toDir = toDir;
    entry->m_observer = observer;
    entry->m_label = options.m_label;
    entry->m_completion = options.m_completion;
    entry->m_localItems = localItems;
    const uint32_t localId = SubmitVisitorTransfer(std::move(entry), items, totalBytes, static_cast<int64_t>(files.size()));
    if (localId == 0)
    {
        errCode = error_code_share_relay_broken;
        return 0;
    }
    LOG_INFO("share upload requested, shareid: %lld, files: %u, bytes: %lld, local id: %u, queued: %d.",
             static_cast<long long>(shareId), static_cast<unsigned>(files.size()), static_cast<long long>(totalBytes), localId,
             IsTransferQueued(localId) ? 1 : 0);
    return localId;
}

uint32_t CShareService::SubmitVisitorTransfer(std::unique_ptr<TransferEntry> entry, const std::vector<ShareTransferEntry>& items,
                                              int64_t totalBytes, int64_t totalFiles)
{
    entry->m_requestItems = items;
    entry->m_requestBytes = totalBytes;
    entry->m_requestFiles = totalFiles;
    std::map<int64_t, std::unique_ptr<VisitorLink> >::iterator v = m_visitors.find(entry->m_shareId);
    if (v == m_visitors.end())
        return 0;
    CShareVisitor& visitor = v->second->m_visitor;
    //访问会话已空闲回收或失败（第 4 期起由这里重新打开，不再靠浏览窗口排队）：先发 2220；主连接断开时状态机同步失败，
    //本笔不入表、直接报告失败
    if (visitor.GetState() == share_visitor_idle || visitor.GetState() == share_visitor_failed)
    {
        visitor.EnsureOpen(NowMs());
        if (visitor.GetState() == share_visitor_failed)
            return 0;
    }
    //这个共享上已有本端发起的传输（进行中或在排队），或访问会话还没就绪：排在后面。服务端同一访问会话同时只接受一笔传输（813）
    if (HasVisitorTransfers(entry->m_shareId) || visitor.GetState() != share_visitor_ready)
    {
        entry->m_queued = true;
    }
    else
    {
        entry->m_accessId = visitor.GetAccessId();
        const uint32_t seq = SendCommand(cmd_share_request_transfer,
                                         ShareProtocolCodec::BuildTransferRequest(entry->m_accessId, entry->m_direction, items,
                                                                                  totalBytes, totalFiles));
        if (seq == 0)
            return 0;
        entry->m_requestSeq = seq;
    }
    entry->m_localId = m_nextTransferId++;
    const uint32_t localId = entry->m_localId;
    const int64_t shareId = entry->m_shareId;
    m_transfers[localId] = std::move(entry);
    UpdateVisitorTransferCount(shareId);
    return localId;
}

void CShareService::StartNextQueuedTransfer(int64_t shareId)
{
    if (m_holdTransferQueue)
        return;
    for (;;)
    {
        //有进行中的就不发；否则取编号最小（最早排队）的那一笔
        uint32_t next = 0;
        bool running = false;
        for (std::map<uint32_t, std::unique_ptr<TransferEntry> >::const_iterator it = m_transfers.begin();
             it != m_transfers.end() && !running; ++it)
        {
            const TransferEntry& e = *it->second;
            if (e.m_isOwner || e.m_shareId != shareId)
                continue;
            if (!e.m_queued)
                running = true;
            else if (next == 0)
                next = it->first;
        }
        if (running || next == 0)
            return;
        TransferEntry& entry = *m_transfers[next];
        //访问状态机已不在（不应发生：有传输时不会销毁它）：这一笔按中转断开结束，接着看下一笔
        std::map<int64_t, std::unique_ptr<VisitorLink> >::iterator v = m_visitors.find(shareId);
        if (v == m_visitors.end())
        {
            FinishTransfer(next, error_code_share_relay_broken);
            continue;
        }
        //访问会话还没就绪（第 4 期起）：已回收或失败的先重新打开，然后等就绪（OnVisitorLinkState 再调本函数）；
        //重新打开时主连接断开，状态机同步失败，排队的传输已由 FailQueuedTransfers 以失败结束
        CShareVisitor& visitor = v->second->m_visitor;
        if (visitor.GetState() != share_visitor_ready)
        {
            if (visitor.GetState() == share_visitor_idle || visitor.GetState() == share_visitor_failed)
                visitor.EnsureOpen(NowMs());
            return;
        }
        entry.m_accessId = visitor.GetAccessId();
        const uint32_t seq = SendCommand(cmd_share_request_transfer,
                                         ShareProtocolCodec::BuildTransferRequest(entry.m_accessId, entry.m_direction,
                                                                                  entry.m_requestItems, entry.m_requestBytes,
                                                                                  entry.m_requestFiles));
        if (seq == 0)
        {
            FinishTransfer(next, error_code_share_relay_broken);
            continue;
        }
        entry.m_queued = false;
        entry.m_requestSeq = seq;
        LOG_INFO("share queued transfer requested, shareid: %lld, local id: %u, direction: %d, seq: %u.",
                 static_cast<long long>(shareId), next, entry.m_direction, seq);
        return;
    }
}

bool CShareService::IsTransferQueued(uint32_t localId) const
{
    std::map<uint32_t, std::unique_ptr<TransferEntry> >::const_iterator it = m_transfers.find(localId);
    return it != m_transfers.end() && it->second->m_queued;
}

int CShareService::TransferLinkPath(uint32_t localId) const
{
    std::map<uint32_t, std::unique_ptr<TransferEntry> >::const_iterator it = m_transfers.find(localId);
    if (it == m_transfers.end() || !it->second->m_p2pFactory)
        return 0;
    return it->second->m_p2pFactory->LinkPath();
}

void CShareService::CancelTransfer(uint32_t localId)
{
    std::map<uint32_t, std::unique_ptr<TransferEntry> >::iterator it = m_transfers.find(localId);
    if (it == m_transfers.end())
        return;
    //用户自己点的取消：结束时不弹完成窗（第 4 期起）
    it->second->m_userCanceled = true;
    LOG_INFO("share transfer canceled by user, local id: %u, transferid: %s.", localId, it->second->m_transferId.c_str());
    CancelTransferInternal(localId);
}

void CShareService::CancelTransferInternal(uint32_t localId)
{
    std::map<uint32_t, std::unique_ptr<TransferEntry> >::iterator it = m_transfers.find(localId);
    if (it == m_transfers.end())
        return;
    if (it->second->m_session)
        it->second->m_session->Cancel();
    else
        FinishTransfer(localId, error_code_share_transfer_canceled);
}

size_t CShareService::GetTransferCount(bool isOwner) const
{
    size_t n = 0;
    for (std::map<uint32_t, std::unique_ptr<TransferEntry> >::const_iterator it = m_transfers.begin(); it != m_transfers.end();
         ++it)
    {
        if (it->second->m_isOwner == isOwner)
            ++n;
    }
    return n;
}

void CShareService::HandleTransferResponse(uint32_t seq, const std::string& body)
{
    int code = 0;
    std::string msg;
    std::string transferId;
    if (!ShareProtocolCodec::ParseTransferResponse(body, code, msg, transferId))
    {
        LOG_WARNING("share transfer response not parsable, seq: %u.", static_cast<unsigned>(seq));
        return;
    }
    for (std::map<uint32_t, std::unique_ptr<TransferEntry> >::iterator it = m_transfers.begin(); it != m_transfers.end(); ++it)
    {
        TransferEntry& entry = *it->second;
        if (entry.m_isOwner || entry.m_requestSeq != seq || !entry.m_transferId.empty())
            continue;
        LOG_INFO("share transfer response, seq: %u, code: %d, transferid: %s.", static_cast<unsigned>(seq), code,
                 transferId.c_str());
        if (code != error_code_ok || transferId.empty())
        {
            FinishTransfer(entry.m_localId, code != error_code_ok ? code : error_code_server_internal);
            return;
        }
        entry.m_transferId = transferId;
        return;
    }
}

void CShareService::StartVisitorTransferSession(const ShareRelayParams& relay)
{
    const uint32_t localId = FindTransfer(relay.m_transferId);
    if (localId == 0)
    {
        LOG_WARNING("share transfer relay info for an unknown transfer, transferid: %s.", relay.m_transferId.c_str());
        SendCommand(cmd_share_transfer_close, ShareProtocolCodec::BuildTransferClose(relay.m_transferId, kTransferResultFail,
                                                                                     error_code_share_transfer_canceled));
        return;
    }
    TransferEntry& entry = *m_transfers[localId];
    if (entry.m_session)
        return;

    const std::shared_ptr<foldertransfer::IFolderLaneTransportFactory> factory = MakeTransportFactory(relay);
    entry.m_p2pFactory = std::dynamic_pointer_cast<balloonp2p::FolderP2PTransportFactory>(factory);
    entry.m_session.reset(new CShareTransferSession(relay, m_host->GetServerPublicKey(), this, factory));
    bool started = false;
    if (entry.m_direction == share_transfer_download)
        started = entry.m_session->StartVisitorDownload(entry.m_shareId, entry.m_items, entry.m_targetDir, &m_store);
    else
        started = entry.m_session->StartVisitorUpload(entry.m_manifest, entry.m_sourceRoot, entry.m_toDir);
    LOG_INFO("share visitor transfer session %s, transferid: %s, direction: %d, lanes: %d.", started ? "started" : "failed",
             relay.m_transferId.c_str(), entry.m_direction, relay.m_lanes);
    if (!started)
        FinishTransfer(localId, error_code_share_relay_broken);
}

std::shared_ptr<foldertransfer::IFolderLaneTransportFactory> CShareService::MakeTransportFactory(const ShareRelayParams& relay)
{
    if (relay.m_p2pSessionId.empty())
        return std::shared_ptr<foldertransfer::IFolderLaneTransportFactory>();
    if (!m_p2pBroker->IsRegistered())
    {
        LOG_INFO("share transfer %s: p2p session %s offered but p2p is not available here, use the relay.",
                 relay.m_transferId.c_str(), relay.m_p2pSessionId.c_str());
        return std::shared_ptr<foldertransfer::IFolderLaneTransportFactory>();
    }
    LOG_INFO("share transfer %s: p2p session %s offered, lanes prefer the p2p link.", relay.m_transferId.c_str(),
             relay.m_p2pSessionId.c_str());
    return m_p2pBroker->CreateFactory(relay.m_transferId, relay.m_relayIp, relay.m_relayPort, relay.m_token,
                                      m_host->GetServerPublicKey());
}

void CShareService::StartOwnerTransferSession(const ShareRelayParams& relay)
{
    ShareLocalRecord rec;
    int refuse = error_code_ok;
    if (!m_store.IsOpen() || !m_store.GetByShareId(relay.m_shareId, rec))
        refuse = error_code_share_unavailable;
    else if (!ShareFileOps::IsRootAvailable(rec.m_rootPath, rec.m_kind))
        refuse = error_code_share_root_missing;
    else if (relay.m_direction != share_transfer_download && relay.m_direction != share_transfer_upload)
        refuse = error_code_bad_request;
    if (refuse != error_code_ok)
    {
        LOG_WARNING("share owner transfer refused, transferid: %s, shareid: %lld, code: %d.", relay.m_transferId.c_str(),
                    static_cast<long long>(relay.m_shareId), refuse);
        SendCommand(cmd_share_transfer_close, ShareProtocolCodec::BuildTransferClose(relay.m_transferId, kTransferResultFail,
                                                                                     refuse));
        return;
    }

    std::unique_ptr<TransferEntry> entry(new TransferEntry());
    entry->m_localId = m_nextTransferId++;
    entry->m_shareId = relay.m_shareId;
    entry->m_direction = relay.m_direction;
    entry->m_isOwner = true;
    entry->m_accessId = relay.m_accessId;
    entry->m_transferId = relay.m_transferId;
    entry->m_visitorId = relay.m_visitorId;
    const std::shared_ptr<foldertransfer::IFolderLaneTransportFactory> factory = MakeTransportFactory(relay);
    entry->m_p2pFactory = std::dynamic_pointer_cast<balloonp2p::FolderP2PTransportFactory>(factory);
    entry->m_session.reset(new CShareTransferSession(relay, m_host->GetServerPublicKey(), this, factory));
    const bool started = (relay.m_direction == share_transfer_download)
                         ? entry->m_session->StartOwnerDownload(rec)
                         : entry->m_session->StartOwnerUpload(rec, relay.m_perms);
    const uint32_t localId = entry->m_localId;
    m_transfers[localId] = std::move(entry);
    LOG_INFO("share owner transfer session %s, transferid: %s, direction: %d, visitor: %lld, lanes: %d.",
             started ? "started" : "failed", relay.m_transferId.c_str(), relay.m_direction,
             static_cast<long long>(relay.m_visitorId), relay.m_lanes);
    if (!started)
        FinishTransfer(localId, error_code_share_relay_broken);
}

void CShareService::HandleTransferEvent(const ShareSessionEvent& ev)
{
    const uint32_t localId = FindTransfer(ev.m_accessId);
    if (localId == 0)
        return;
    TransferEntry& entry = *m_transfers[localId];

    switch (ev.m_type)
    {
    //进度：记下来（「正在访问的人」用）并转告界面；访问者下载每隔一段时间写一次续传记录
    case share_transfer_event_progress:
        entry.m_bytesDone = ev.m_bytesDone;
        entry.m_bytesTotal = ev.m_bytesTotal;
        if (entry.m_observer != NULL)
            entry.m_observer->OnTransferProgress(localId, ev.m_bytesDone, ev.m_bytesTotal, ev.m_filesDone, ev.m_filesTotal);
        if (!entry.m_isOwner && entry.m_direction == share_transfer_download
            && NowMs() - entry.m_lastSaveMs >= kShareResumeSaveIntervalMs)
            SaveDownloadResume(entry);
        break;

    //整体完成
    case share_transfer_event_completed:
        LOG_INFO("share transfer completed, transferid: %s, files: %lld, bytes: %lld.", entry.m_transferId.c_str(),
                 static_cast<long long>(ev.m_filesTotal), static_cast<long long>(ev.m_bytesTotal));
        FinishTransfer(localId, error_code_ok);
        break;

    //整体失败或被取消
    case share_transfer_event_failed:
        FinishTransfer(localId, ev.m_errCode != error_code_ok ? ev.m_errCode : error_code_share_relay_broken);
        break;

    //未知事件
    default:
        break;
    }
}

void CShareService::FinishTransfer(uint32_t localId, int code)
{
    std::map<uint32_t, std::unique_ptr<TransferEntry> >::iterator it = m_transfers.find(localId);
    if (it == m_transfers.end())
        return;
    std::unique_ptr<TransferEntry> entry(std::move(it->second));
    m_transfers.erase(it);

    //先回收线程：此时状态机已回报结果、正在退出
    if (entry->m_session)
        entry->m_session->Stop();

    if (!entry->m_transferId.empty())
        SendCommand(cmd_share_transfer_close, ShareProtocolCodec::BuildTransferClose(entry->m_transferId,
                                                                                     code == error_code_ok ? 0 : kTransferResultFail,
                                                                                     code));
    if (entry->m_session)
    {
        if (!entry->m_isOwner && entry->m_direction == share_transfer_download)
        {
            if (code == error_code_ok)
                ClearDownloadResume(*entry);
            else
                SaveDownloadResume(*entry);
        }
        if (entry->m_isOwner)
        {
            if (entry->m_direction == share_transfer_upload)
                CleanupUploadParts(*entry, code != error_code_ok);
            ReportTransferAudit(*entry, code);
            //访问者在本人的共享上下载或上传成功：通知分享者（第 4 期起）
            if (code == error_code_ok)
                NotifyOwnerTransfer(*entry);
        }
    }
    LOG_INFO("share transfer finished, local id: %u, transferid: %s, owner: %d, direction: %d, code: %d, user canceled: %d.",
             localId, entry->m_transferId.c_str(), entry->m_isOwner ? 1 : 0, entry->m_direction, code,
             entry->m_userCanceled ? 1 : 0);
    //结束回调（拖出任务）：与浏览窗口是否还开着无关
    if (entry->m_completion)
        entry->m_completion->OnTransferDone(code);
    if (entry->m_observer != NULL)
        entry->m_observer->OnTransferFinished(localId, code);
    //访问者一端：交给结束监听，由模块决定要不要弹窗（第 4 期起）
    if (!entry->m_isOwner)
        NotifyVisitorFinished(*entry, code);
    //访问者一端进行中的一笔结束了：轮到同一共享上排队的下一笔
    if (!entry->m_isOwner && !entry->m_queued)
        StartNextQueuedTransfer(entry->m_shareId);
    if (!entry->m_isOwner)
        UpdateVisitorTransferCount(entry->m_shareId);
}

uint32_t CShareService::FindTransfer(const std::string& transferId) const
{
    if (transferId.empty())
        return 0;
    for (std::map<uint32_t, std::unique_ptr<TransferEntry> >::const_iterator it = m_transfers.begin(); it != m_transfers.end();
         ++it)
    {
        if (it->second->m_transferId == transferId)
            return it->first;
    }
    return 0;
}

void CShareService::CancelTransfersOfShare(int64_t shareId, bool owner, int64_t visitorId)
{
    std::vector<uint32_t> ids;
    for (std::map<uint32_t, std::unique_ptr<TransferEntry> >::iterator it = m_transfers.begin(); it != m_transfers.end(); ++it)
    {
        const TransferEntry& e = *it->second;
        if (e.m_shareId == shareId && e.m_isOwner == owner && (visitorId == 0 || e.m_visitorId == visitorId))
            ids.push_back(it->first);
    }
    //逐个取消的过程中不发起排队的下一笔：它们也在这次要取消的范围内
    const bool hold = m_holdTransferQueue;
    m_holdTransferQueue = true;
    for (size_t i = 0; i < ids.size(); ++i)
        CancelTransferInternal(ids[i]);
    m_holdTransferQueue = hold;
}

size_t CShareService::EndTransfersOfAccess(const std::string& accessId, int errCode)
{
    if (accessId.empty())
        return 0;
    std::vector<uint32_t> ids;
    for (std::map<uint32_t, std::unique_ptr<TransferEntry> >::iterator it = m_transfers.begin(); it != m_transfers.end(); ++it)
    {
        if (it->second->m_accessId == accessId)
            ids.push_back(it->first);
    }
    //逐个结束的过程中不发起排队的下一笔：排队的也属于这个访问会话，同样要结束
    const bool hold = m_holdTransferQueue;
    m_holdTransferQueue = true;
    for (size_t i = 0; i < ids.size(); ++i)
    {
        std::map<uint32_t, std::unique_ptr<TransferEntry> >::iterator it = m_transfers.find(ids[i]);
        if (it == m_transfers.end())
            continue;
        //已建会话的经取消收尾（状态机回报失败后再走 FinishTransfer）；还在等 2222 或在排队的直接按服务端的原因收尾
        if (it->second->m_session)
            it->second->m_session->Cancel();
        else
            FinishTransfer(ids[i], errCode != error_code_ok ? errCode : error_code_share_transfer_canceled);
    }
    m_holdTransferQueue = hold;
    return ids.size();
}

void CShareService::StopAllTransfers()
{
    std::vector<uint32_t> ids;
    for (std::map<uint32_t, std::unique_ptr<TransferEntry> >::iterator it = m_transfers.begin(); it != m_transfers.end(); ++it)
    {
        ids.push_back(it->first);
        if (it->second->m_session)
            it->second->m_session->Cancel();
    }
    //Stop 会等线程退出；已取消的会话很快结束。全部结束的过程中不发起排队的下一笔
    const bool hold = m_holdTransferQueue;
    m_holdTransferQueue = true;
    for (size_t i = 0; i < ids.size(); ++i)
        FinishTransfer(ids[i], error_code_share_transfer_canceled);
    m_holdTransferQueue = hold;
}

void CShareService::SaveDownloadResume(TransferEntry& entry)
{
    if (!entry.m_session || !m_store.IsOpen())
    {
        entry.m_lastSaveMs = NowMs();
        return;
    }
    CVisitorDownloadHooks* hooks = entry.m_session->GetVisitorDownloadHooks();
    std::shared_ptr<foldertransfer::FolderReceiveState> state = entry.m_session->GetReceiveState();
    if (hooks == NULL || !state)
        return;
    int64_t wallMs = 0;
    {
        FILETIME ft;
        ::GetSystemTimeAsFileTime(&ft);
        wallMs = ShareFileOps::FileTimeToUnixMs(ft);
    }
    const size_t fileCount = hooks->GetFileCount();
    if (entry.m_savedOffsets.size() != fileCount)
        entry.m_savedOffsets.assign(fileCount, 0);
    std::vector<ShareDownloadRecord> batch;
    for (size_t i = 0; i < fileCount; ++i)
    {
        foldertransfer::FolderRecvFileState rec;
        if (!state->GetFileRecord(static_cast<int64_t>(i), rec))
            continue;
        const int64_t offset = rec.m_completed ? rec.m_size : rec.m_bytesWritten;
        //没有进度、或与上次写的一样：不必重写
        if (offset <= 0 || offset == entry.m_savedOffsets[i])
            continue;
        entry.m_savedOffsets[i] = offset;
        ShareDownloadRecord dl;
        dl.m_shareId = entry.m_shareId;
        dl.m_relPath = hooks->GetShareRelPath(i);
        dl.m_srcSize = hooks->GetFileSize(i);
        dl.m_srcMtime = hooks->GetFileMtime(i);
        dl.m_localPath = hooks->GetLocalPath(i);
        dl.m_offset = offset;
        dl.m_updateTime = wallMs;
        batch.push_back(dl);
    }
    //一批在一个事务里写完；写完之后再记时间，即使写得慢也不会紧接着又写下一次
    const bool ok = m_store.SaveDownloadProgressBatch(batch);
    entry.m_lastSaveMs = NowMs();
    if (!batch.empty())
        LOG_INFO("share download resume points saved, transferid: %s, files: %u, ok: %d.", entry.m_transferId.c_str(),
                 static_cast<unsigned>(batch.size()), ok ? 1 : 0);
}

void CShareService::ClearDownloadResume(TransferEntry& entry)
{
    if (!entry.m_session || !m_store.IsOpen())
        return;
    CVisitorDownloadHooks* hooks = entry.m_session->GetVisitorDownloadHooks();
    if (hooks == NULL)
        return;
    std::vector<std::string> relPaths;
    for (size_t i = 0; i < hooks->GetFileCount(); ++i)
        relPaths.push_back(hooks->GetShareRelPath(i));
    //一次删完：上万个文件逐条删同样要几分钟
    m_store.RemoveDownloads(entry.m_shareId, relPaths);
}

void CShareService::ReportTransferAudit(TransferEntry& entry, int code)
{
    std::vector<std::string> paths;
    std::vector<int64_t> sizes;
    std::vector<bool> done;
    int action = share_audit_download_file;
    std::string summary;
    if (entry.m_direction == share_transfer_download)
    {
        COwnerDownloadHooks* hooks = entry.m_session->GetOwnerDownloadHooks();
        if (hooks != NULL)
        {
            hooks->GetFiles(paths, sizes);
            const std::vector<std::string> items = hooks->GetItems();
            summary = items.empty() ? std::string() : items[0];
        }
        //发送端不逐个回报文件结果：整体成功即全部成功，失败时只记一条整体失败
        done.assign(paths.size(), code == error_code_ok);
    }
    else
    {
        action = share_audit_upload_file;
        COwnerUploadHooks* hooks = entry.m_session->GetOwnerUploadHooks();
        std::shared_ptr<foldertransfer::FolderReceiveState> state = entry.m_session->GetReceiveState();
        if (hooks != NULL)
        {
            hooks->GetFiles(paths, sizes);
            const std::vector<std::string> tops = hooks->GetTopNames();
            summary = ShareBrowseModel::JoinPath(hooks->GetToDir(), tops.empty() ? std::string() : tops[0]);
        }
        for (size_t i = 0; i < paths.size(); ++i)
        {
            foldertransfer::FolderRecvFileState rec;
            done.push_back(state && state->GetFileRecord(static_cast<int64_t>(i), rec) && rec.m_completed);
        }
    }

    FILETIME ft;
    ::GetSystemTimeAsFileTime(&ft);
    const int64_t wallMs = ShareFileOps::FileTimeToUnixMs(ft);
    std::vector<ShareAuditEntry> batch;
    for (size_t i = 0; i <= paths.size(); ++i)
    {
        const bool tail = (i == paths.size());
        if (!tail && done[i])
        {
            ShareAuditEntry item;
            item.m_shareId = entry.m_shareId;
            item.m_accessId = entry.m_accessId;
            item.m_visitorId = entry.m_visitorId;
            item.m_action = action;
            item.m_path = paths[i];
            item.m_size = sizes[i];
            item.m_result = error_code_ok;
            item.m_clientTime = wallMs;
            batch.push_back(item);
        }
        //失败时在最后补一条整体失败的记录
        if (tail && code != error_code_ok)
        {
            ShareAuditEntry item;
            item.m_shareId = entry.m_shareId;
            item.m_accessId = entry.m_accessId;
            item.m_visitorId = entry.m_visitorId;
            item.m_action = action;
            item.m_path = summary;
            item.m_result = code;
            item.m_clientTime = wallMs;
            batch.push_back(item);
        }
        if (!batch.empty() && (batch.size() >= kShareAuditBatch || tail))
        {
            SendCommand(cmd_share_audit_report, ShareProtocolCodec::BuildAuditReport(batch));
            batch.clear();
        }
    }

    //最近操作记录：每次传输一条
    ShareActivityRecord activity;
    activity.m_shareId = entry.m_shareId;
    activity.m_visitorId = entry.m_visitorId;
    activity.m_action = action;
    activity.m_path = summary;
    activity.m_result = code;
    activity.m_time = wallMs;
    m_store.AddActivity(activity);
}

void CShareService::ReportWriteAudit(const std::vector<ShareAuditEntry>& audits)
{
    for (size_t start = 0; start < audits.size(); start += kShareAuditBatch)
    {
        const size_t end = (start + kShareAuditBatch < audits.size()) ? start + kShareAuditBatch : audits.size();
        const std::vector<ShareAuditEntry> batch(audits.begin() + start, audits.begin() + end);
        SendCommand(cmd_share_audit_report, ShareProtocolCodec::BuildAuditReport(batch));
    }
    //最近操作：每个条目一条（重命名与移动的目标不入本机库，界面只显示原路径，完整信息在服务端审计里）
    for (size_t i = 0; i < audits.size(); ++i)
    {
        ShareActivityRecord activity;
        activity.m_shareId = audits[i].m_shareId;
        activity.m_visitorId = audits[i].m_visitorId;
        activity.m_action = audits[i].m_action;
        activity.m_path = audits[i].m_path;
        activity.m_result = audits[i].m_result;
        activity.m_time = audits[i].m_clientTime;
        m_store.AddActivity(activity);
    }
    if (!audits.empty())
        LOG_INFO("share write op audit reported, shareid: %lld, action: %d, items: %u.",
                 static_cast<long long>(audits[0].m_shareId), audits[0].m_action, static_cast<unsigned>(audits.size()));
}

void CShareService::NotifyVisitorFinished(const TransferEntry& entry, int code)
{
    if (m_finishListener == NULL)
        return;
    const bool download = (entry.m_direction == share_transfer_download);
    ShareVisitorTransferResult result;
    result.m_localId = entry.m_localId;
    result.m_shareId = entry.m_shareId;
    result.m_direction = entry.m_direction;
    result.m_code = code;
    result.m_userCanceled = entry.m_userCanceled;
    result.m_dragOut = entry.m_dragOut;
    result.m_label = entry.m_label;
    result.m_targetDir = entry.m_targetDir;
    result.m_topCount = entry.m_requestItems.size();
    result.m_toDir = entry.m_toDir;
    result.m_timeMs = WallNowMs();
    std::map<int64_t, std::unique_ptr<VisitorLink> >::const_iterator v = m_visitors.find(entry.m_shareId);
    if (v != m_visitors.end())
        result.m_shareName = v->second->m_visitor.GetShareName();
    if (download)
        result.m_downloadItems = entry.m_requestItems;
    else
        result.m_uploadItems = entry.m_localItems;
    //发起方没给名称（单元测试、旧调用点）：退回第一项的名字
    if (result.m_label.empty())
    {
        if (download && !entry.m_requestItems.empty())
            ShareUtf8ToWide(ShareNoticeModel::LastSegment(entry.m_requestItems[0].m_path), result.m_label);
        else if (!download && !entry.m_localItems.empty())
            result.m_label = LocalLastSegment(entry.m_localItems[0]);
    }
    //下载成功：第一个顶层条目落地后的路径（顶层同名时接收端已改名，以它给出的名字为准），弹窗的「打开」用它
    if (download && code == error_code_ok && entry.m_session)
    {
        CVisitorDownloadHooks* hooks = entry.m_session->GetVisitorDownloadHooks();
        if (hooks != NULL && !hooks->GetLocalTopNames().empty() && !entry.m_targetDir.empty())
        {
            std::wstring dir = entry.m_targetDir;
            if (dir[dir.size() - 1] != L'\\')
                dir += L'\\';
            result.m_localTopPath = dir + hooks->GetLocalTopNames()[0];
        }
    }
    m_finishListener->OnVisitorTransferFinished(result);
}

void CShareService::NotifyOwnerTransfer(const TransferEntry& entry)
{
    if (m_activityListener == NULL || !entry.m_session)
        return;
    std::vector<std::string> paths;
    std::string toDir;
    if (entry.m_direction == share_transfer_download)
    {
        COwnerDownloadHooks* hooks = entry.m_session->GetOwnerDownloadHooks();
        if (hooks != NULL)
            paths = hooks->GetItems();
    }
    else
    {
        COwnerUploadHooks* hooks = entry.m_session->GetOwnerUploadHooks();
        if (hooks != NULL)
        {
            paths = hooks->GetTopNames();
            toDir = hooks->GetToDir();
        }
    }
    ShareOwnerActivity activity;
    if (!ShareNoticeModel::MakeTransferActivity(entry.m_direction, paths, toDir, activity))
        return;
    activity.m_shareId = entry.m_shareId;
    activity.m_visitorId = entry.m_visitorId;
    activity.m_timeMs = WallNowMs();
    CompleteOwnerActivity(activity, entry.m_accessId);
    LOG_INFO("share owner activity from a transfer, shareid: %lld, visitor: %lld, action: %d, items: %u.",
             static_cast<long long>(activity.m_shareId), static_cast<long long>(activity.m_visitorId), activity.m_action,
             static_cast<unsigned>(activity.m_count));
    m_activityListener->OnOwnerActivity(activity);
}

void CShareService::NotifyOwnerWrite(const std::vector<ShareAuditEntry>& audits)
{
    if (m_activityListener == NULL || audits.empty())
        return;
    ShareOwnerActivity activity;
    if (!ShareNoticeModel::MakeWriteActivity(audits, activity))
        return;
    if (activity.m_timeMs <= 0)
        activity.m_timeMs = WallNowMs();
    CompleteOwnerActivity(activity, audits[0].m_accessId);
    LOG_INFO("share owner activity from a write op, shareid: %lld, visitor: %lld, action: %d, items: %u.",
             static_cast<long long>(activity.m_shareId), static_cast<long long>(activity.m_visitorId), activity.m_action,
             static_cast<unsigned>(activity.m_count));
    m_activityListener->OnOwnerActivity(activity);
}

void CShareService::CompleteOwnerActivity(ShareOwnerActivity& activity, const std::string& accessId)
{
    //访问者名字的后备：服务端在 2222 里给的显示名（宿主取得到通讯录里的名字时用通讯录的）
    SessionMap::const_iterator session = m_sessions.find(accessId);
    if (session != m_sessions.end())
        activity.m_visitorName = session->second->GetVisitorName();
    std::map<int64_t, std::string>::const_iterator name = m_ownShareNames.find(activity.m_shareId);
    if (name != m_ownShareNames.end())
        activity.m_shareName = name->second;
    ShareLocalRecord rec;
    if (!m_store.IsOpen() || !m_store.GetByShareId(activity.m_shareId, rec))
        return;
    //单文件共享的下载条目是空串：名字取共享的那个文件本身
    if (activity.m_firstName.empty())
        activity.m_firstName = ShareWideToUtf8(LocalLastSegment(rec.m_rootPath));
    //要在资源管理器里选中的那一项：拼到共享根下，越出共享根的（不应发生）不给
    if (!activity.m_revealRelPath.empty())
    {
        std::wstring abs;
        if (SharePathGuard::ResolveUnderRoot(rec.m_rootPath, activity.m_revealRelPath, rec.m_kind == share_kind_file, abs))
            activity.m_revealPath = abs;
    }
}

void CShareService::CleanupUploadParts(TransferEntry& entry, bool deleteFiles)
{
    COwnerUploadHooks* hooks = entry.m_session->GetOwnerUploadHooks();
    if (hooks == NULL)
        return;
    const std::vector<std::wstring> parts = hooks->GetPartFiles();
    for (size_t i = 0; i < parts.size(); ++i)
    {
        CShareUploadRegistry::Instance().Remove(parts[i]);
        //上传不续传：失败或取消时删掉本次写的半成品（已改成正式名的文件不在其中，保留）
        if (deleteFiles)
            ::DeleteFileW(ShareToLongPath(parts[i]).c_str());
    }
}

void CShareService::UpdateVisitorTransferCount(int64_t shareId)
{
    std::map<int64_t, std::unique_ptr<VisitorLink> >::iterator v = m_visitors.find(shareId);
    if (v == m_visitors.end())
        return;
    int count = 0;
    for (std::map<uint32_t, std::unique_ptr<TransferEntry> >::iterator it = m_transfers.begin(); it != m_transfers.end(); ++it)
    {
        if (!it->second->m_isOwner && it->second->m_shareId == shareId)
            ++count;
    }
    v->second->m_visitor.SetTransferCount(count);
}

//==================== 管理请求（发起、取消、调整成员）====================

uint32_t CShareService::CreateShare(const ShareCreateParams& params, const std::wstring& rootPath,
                                    IShareManageObserver* observer)
{
    if (m_host == NULL || !m_store.IsOpen() || rootPath.empty())
    {
        LOG_WARNING("share create refused: not logged in or local store not open.");
        return 0;
    }
    ShareCreateParams req = params;
    if (req.m_clientShareId.empty())
        req.m_clientShareId = ShareCreateModel::NewClientShareId();
    if (req.m_clientShareId.empty())
    {
        LOG_ERROR("share create refused: client share id not generated.");
        return 0;
    }

    //先写本机记录再发请求：应答回来之前进程意外退出，下次登录时按「没有 shareid」清理（方案 8.1）
    FILETIME ft;
    ::GetSystemTimeAsFileTime(&ft);
    if (!m_store.AddPendingShare(req.m_clientShareId, rootPath, req.m_kind, req.m_includeHidden,
                                 ShareFileOps::FileTimeToUnixMs(ft)))
    {
        LOG_ERROR("share create refused: local record not written, clientshareid: %s.", req.m_clientShareId.c_str());
        return 0;
    }

    std::unique_ptr<ManageRequest> mr(new ManageRequest());
    mr->m_kind = share_manage_create;
    mr->m_body = ShareProtocolCodec::BuildCreateRequest(req);
    mr->m_clientShareId = req.m_clientShareId;
    mr->m_shareName = req.m_name;
    mr->m_observer = observer;
    mr->m_seq = SendCommand(cmd_share_create, mr->m_body);
    if (mr->m_seq == 0)
    {
        m_store.RemovePendingShare(req.m_clientShareId);
        return 0;
    }
    mr->m_attempts = 1;
    mr->m_deadlineMs = NowMs() + m_manageTimeoutMs;
    mr->m_requestId = m_nextManageId++;
    const uint32_t requestId = mr->m_requestId;
    LOG_INFO("share create sent, request: %u, seq: %u, clientshareid: %s, kind: %d, ctype: %d, cid: %lld, members: %u.",
             static_cast<unsigned>(requestId), static_cast<unsigned>(mr->m_seq), req.m_clientShareId.c_str(), req.m_kind,
             req.m_ctype, static_cast<long long>(req.m_cid), static_cast<unsigned>(req.m_members.size()));
    m_manage[requestId] = std::move(mr);
    return requestId;
}

uint32_t CShareService::CancelShare(int64_t shareId, IShareManageObserver* observer)
{
    if (m_host == NULL || shareId <= 0)
        return 0;
    std::unique_ptr<ManageRequest> mr(new ManageRequest());
    mr->m_kind = share_manage_cancel;
    mr->m_shareId = shareId;
    mr->m_observer = observer;
    mr->m_body = ShareProtocolCodec::BuildShareIdRequest(shareId);
    mr->m_seq = SendCommand(cmd_share_cancel, mr->m_body);
    if (mr->m_seq == 0)
        return 0;
    mr->m_attempts = 1;
    mr->m_deadlineMs = NowMs() + m_manageTimeoutMs;
    mr->m_requestId = m_nextManageId++;
    const uint32_t requestId = mr->m_requestId;
    LOG_INFO("share cancel sent, request: %u, seq: %u, shareid: %lld.", static_cast<unsigned>(requestId),
             static_cast<unsigned>(mr->m_seq), static_cast<long long>(shareId));
    m_manage[requestId] = std::move(mr);
    return requestId;
}

uint32_t CShareService::UpdateMembers(int64_t shareId, const std::vector<ShareMemberPerm>& add,
                                      const std::vector<int64_t>& remove, const std::vector<ShareMemberPerm>& update,
                                      IShareManageObserver* observer)
{
    if (m_host == NULL || shareId <= 0 || (add.empty() && remove.empty() && update.empty()))
        return 0;
    std::unique_ptr<ManageRequest> mr(new ManageRequest());
    mr->m_kind = share_manage_update;
    mr->m_shareId = shareId;
    mr->m_observer = observer;
    mr->m_body = ShareProtocolCodec::BuildUpdateMembersRequest(shareId, add, remove, update);
    mr->m_seq = SendCommand(cmd_share_update_members, mr->m_body);
    if (mr->m_seq == 0)
        return 0;
    mr->m_attempts = 1;
    mr->m_deadlineMs = NowMs() + m_manageTimeoutMs;
    mr->m_requestId = m_nextManageId++;
    const uint32_t requestId = mr->m_requestId;
    LOG_INFO("share update members sent, request: %u, seq: %u, shareid: %lld, add: %u, remove: %u, update: %u.",
             static_cast<unsigned>(requestId), static_cast<unsigned>(mr->m_seq), static_cast<long long>(shareId),
             static_cast<unsigned>(add.size()), static_cast<unsigned>(remove.size()), static_cast<unsigned>(update.size()));
    m_manage[requestId] = std::move(mr);
    return requestId;
}

void CShareService::DetachManageObserver(IShareManageObserver* observer)
{
    for (std::map<uint32_t, std::unique_ptr<ManageRequest> >::iterator it = m_manage.begin(); it != m_manage.end(); ++it)
    {
        if (it->second->m_observer == observer)
            it->second->m_observer = NULL;
    }
}

size_t CShareService::GetPendingManageCount() const
{
    return m_manage.size();
}

int64_t CShareService::GetUserId() const
{
    return m_userId;
}

void CShareService::SetManageTimeoutMs(int64_t timeoutMs)
{
    if (timeoutMs > 0)
        m_manageTimeoutMs = timeoutMs;
}

void CShareService::HandleManageResponse(uint16_t cmd, uint32_t seq, const std::string& body)
{
    ShareManageResult result;
    std::string msg;
    std::string clientShareId;
    bool parsed = false;
    //各命令的应答格式
    switch (cmd)
    {
    //发起共享：带 shareid、clientshareid，被拒时可带 invalidmembers
    case cmd_share_create:
    {
        ShareCreateResponse resp;
        parsed = ShareProtocolCodec::ParseCreateResponse(body, resp);
        result.m_code = resp.m_code;
        result.m_shareId = resp.m_shareId;
        result.m_invalidMembers = resp.m_invalidMembers;
        clientShareId = resp.m_clientShareId;
        msg = resp.m_msg;
        break;
    }
    //调整成员：被拒时可带 invalidmembers
    case cmd_share_update_members:
        parsed = ShareProtocolCodec::ParseUpdateMembersResponse(body, result.m_code, msg, result.m_invalidMembers);
        break;
    //取消共享：只有结果码
    default:
        parsed = ShareProtocolCodec::ParseSimpleResponse(body, result.m_code, msg);
        break;
    }
    if (!parsed)
    {
        LOG_WARNING("share manage response not parsable, cmd: %u, seq: %u.", static_cast<unsigned>(cmd),
                    static_cast<unsigned>(seq));
        return;
    }

    //按 seq 找；发起共享再按 clientshareid 找（重发之后，前一次发送的应答才到）
    uint32_t requestId = 0;
    for (std::map<uint32_t, std::unique_ptr<ManageRequest> >::iterator it = m_manage.begin(); it != m_manage.end(); ++it)
    {
        const ManageRequest& mr = *it->second;
        const bool sameCmd = (cmd == cmd_share_create && mr.m_kind == share_manage_create)
            || (cmd == cmd_share_cancel && mr.m_kind == share_manage_cancel)
            || (cmd == cmd_share_update_members && mr.m_kind == share_manage_update);
        if (!sameCmd)
            continue;
        if ((seq != 0 && mr.m_seq == seq)
            || (mr.m_kind == share_manage_create && !clientShareId.empty() && mr.m_clientShareId == clientShareId))
        {
            requestId = it->first;
            break;
        }
    }
    if (requestId == 0)
    {
        LOG_INFO("share manage response without pending request, cmd: %u, seq: %u, code: %d.", static_cast<unsigned>(cmd),
                 static_cast<unsigned>(seq), result.m_code);
        return;
    }

    ManageRequest& mr = *m_manage[requestId];
    result.m_requestId = requestId;
    result.m_kind = mr.m_kind;
    if (mr.m_kind != share_manage_create)
        result.m_shareId = mr.m_shareId;
    LOG_INFO("share manage response, cmd: %u, request: %u, seq: %u, code: %d, shareid: %lld, invalid members: %u, msg: %s.",
             static_cast<unsigned>(cmd), static_cast<unsigned>(requestId), static_cast<unsigned>(seq), result.m_code,
             static_cast<long long>(result.m_shareId), static_cast<unsigned>(result.m_invalidMembers.size()), msg.c_str());

    if (result.m_code == error_code_ok && mr.m_kind == share_manage_create)
    {
        //补写 shareid，并立即报告能否提供，卡片对成员才会显示「可用」（方案 8.1 第 5 步）
        if (result.m_shareId <= 0 || !m_store.IsOpen() || !m_store.BindShareId(mr.m_clientShareId, result.m_shareId))
        {
            LOG_ERROR("share create: local record not bound, clientshareid: %s, shareid: %lld.", mr.m_clientShareId.c_str(),
                      static_cast<long long>(result.m_shareId));
        }
        else
        {
            ShareLocalRecord rec;
            const bool available = m_store.GetByShareId(result.m_shareId, rec)
                && ShareFileOps::IsRootAvailable(rec.m_rootPath, rec.m_kind);
            m_localStatus[result.m_shareId] = share_local_status_reported;
            m_rootAvailable[result.m_shareId] = available;
            if (!mr.m_shareName.empty())
                m_ownShareNames[result.m_shareId] = mr.m_shareName;
            SendServeReport(std::vector<ShareServeItem>(1, ShareReconcile::MakeServeItem(result.m_shareId, available)));
        }
    }
    else if (result.m_code == error_code_ok && mr.m_kind == share_manage_cancel)
    {
        DropOwnShare(result.m_shareId, share_invalid_owner_canceled);
    }
    FinishManage(requestId, result);
}

void CShareService::CheckManageTimeouts(int64_t now)
{
    std::vector<uint32_t> expired;
    for (std::map<uint32_t, std::unique_ptr<ManageRequest> >::iterator it = m_manage.begin(); it != m_manage.end(); ++it)
    {
        ManageRequest& mr = *it->second;
        if (now < mr.m_deadlineMs)
            continue;
        //发起共享：用同一个 clientshareid 重发，服务端按它去重
        if (mr.m_kind == share_manage_create && mr.m_attempts < kShareCreateMaxAttempts)
        {
            const uint32_t seq = SendCommand(cmd_share_create, mr.m_body);
            ++mr.m_attempts;
            mr.m_deadlineMs = now + m_manageTimeoutMs;
            if (seq != 0)
                mr.m_seq = seq;
            LOG_WARNING("share create timed out, resent, request: %u, attempt: %d, seq: %u, clientshareid: %s.",
                        static_cast<unsigned>(it->first), mr.m_attempts, static_cast<unsigned>(seq),
                        mr.m_clientShareId.c_str());
            continue;
        }
        expired.push_back(it->first);
    }
    for (size_t i = 0; i < expired.size(); ++i)
    {
        std::map<uint32_t, std::unique_ptr<ManageRequest> >::iterator it = m_manage.find(expired[i]);
        if (it == m_manage.end())
            continue;
        ShareManageResult result;
        result.m_requestId = expired[i];
        result.m_kind = it->second->m_kind;
        result.m_code = kShareManageNoResponse;
        result.m_shareId = it->second->m_shareId;
        LOG_WARNING("share manage request timed out, request: %u, kind: %d, attempts: %d.",
                    static_cast<unsigned>(expired[i]), result.m_kind, it->second->m_attempts);
        FinishManage(expired[i], result);
    }
}

void CShareService::FinishManage(uint32_t requestId, ShareManageResult& result)
{
    std::map<uint32_t, std::unique_ptr<ManageRequest> >::iterator it = m_manage.find(requestId);
    if (it == m_manage.end())
        return;
    std::unique_ptr<ManageRequest> mr(std::move(it->second));
    m_manage.erase(it);
    //发起没有成功（被拒或没有应答）：删除本机记录。没有应答时服务端可能其实已经建成，下次登录比对时它会显示为
    //「本机缺少记录」，分享者可以取消
    if (mr->m_kind == share_manage_create && result.m_code != error_code_ok && m_store.IsOpen())
        m_store.RemovePendingShare(mr->m_clientShareId);
    //先从表里去掉再回调：观察者可能在回调里发起新的请求
    if (mr->m_observer != NULL)
        mr->m_observer->OnShareManageResult(result);
}

void CShareService::FailAllManage()
{
    std::vector<uint32_t> ids;
    for (std::map<uint32_t, std::unique_ptr<ManageRequest> >::iterator it = m_manage.begin(); it != m_manage.end(); ++it)
        ids.push_back(it->first);
    for (size_t i = 0; i < ids.size(); ++i)
    {
        std::map<uint32_t, std::unique_ptr<ManageRequest> >::iterator it = m_manage.find(ids[i]);
        if (it == m_manage.end())
            continue;
        ShareManageResult result;
        result.m_requestId = ids[i];
        result.m_kind = it->second->m_kind;
        result.m_code = kShareManageNoResponse;
        result.m_shareId = it->second->m_shareId;
        FinishManage(ids[i], result);
    }
}

void CShareService::RemoveStalePendingShares()
{
    std::vector<ShareLocalRecord> local;
    if (!m_store.ListShares(local))
        return;
    for (size_t i = 0; i < local.size(); ++i)
    {
        if (local[i].m_shareId != 0)
            continue;
        //断线重连时发起请求可能仍在进行（等待超时重发），不能删
        bool inFlight = false;
        for (std::map<uint32_t, std::unique_ptr<ManageRequest> >::iterator it = m_manage.begin(); it != m_manage.end(); ++it)
        {
            if (it->second->m_kind == share_manage_create && it->second->m_clientShareId == local[i].m_clientShareId)
                inFlight = true;
        }
        if (inFlight)
            continue;
        m_store.RemovePendingShare(local[i].m_clientShareId);
        LOG_INFO("stale pending share record removed, clientshareid: %s.", local[i].m_clientShareId.c_str());
    }
}

//==================== 列表请求与卡片状态 ====================

uint32_t CShareService::RequestList(int scope, int ctype, int64_t cid, IShareListObserver* observer)
{
    if (m_host == NULL)
        return 0;
    const uint32_t seq = SendCommand(cmd_share_list, ShareProtocolCodec::BuildListRequest(scope, ctype, cid));
    if (seq == 0)
        return 0;
    ListRequest req;
    req.m_scope = scope;
    req.m_deadlineMs = NowMs() + m_manageTimeoutMs;
    req.m_observer = observer;
    m_listRequests[seq] = req;
    LOG_INFO("share list requested by ui, seq: %u, scope: %d, ctype: %d, cid: %lld.", static_cast<unsigned>(seq), scope,
             ctype, static_cast<long long>(cid));
    return seq;
}

void CShareService::DetachListObserver(IShareListObserver* observer)
{
    for (std::map<uint32_t, ListRequest>::iterator it = m_listRequests.begin(); it != m_listRequests.end(); ++it)
    {
        if (it->second.m_observer == observer)
            it->second.m_observer = NULL;
    }
}

void CShareService::SetCardStateListener(IShareCardStateListener* listener)
{
    m_cardStateListener = listener;
}

void CShareService::FinishList(uint32_t seq, int code, const std::vector<ShareListEntry>& shares)
{
    std::map<uint32_t, ListRequest>::iterator it = m_listRequests.find(seq);
    if (it == m_listRequests.end())
        return;
    const ListRequest req = it->second;
    m_listRequests.erase(it);
    //先从表里去掉再回调：观察者可能在回调里发起新的请求
    if (req.m_observer != NULL)
        req.m_observer->OnShareListResult(seq, req.m_scope, code, shares);
}

bool CShareService::GetCardState(int64_t shareId, ShareCardState& out) const
{
    std::map<int64_t, ShareCardState>::const_iterator it = m_cardStates.find(shareId);
    if (it == m_cardStates.end() || it->second.m_state == 0)
        return false;
    out = it->second;
    return true;
}

void CShareService::UpdateCardState(int64_t shareId, int state, int reason, int myPerms, bool notifyHost)
{
    if (shareId <= 0)
        return;
    ShareCardState& cur = m_cardStates[shareId];
    const ShareCardState before = cur;
    //共享被取消（分享者、群主或管理员取消、群解散、分享者退群）是终态：共享 id 不复用，之后迟到的「可用 / 暂不可用」
    //不能把卡片改回去（服务端处理 2210 与处理取消在不同线程上，偶尔会在「已失效」之后再推一次「可用」）。
    //本人被移出成员的「已失效」不是终态：重新被加入时应当恢复
    const bool canceled = cur.m_state == share_state_invalid && cur.m_reason >= share_invalid_owner_canceled
                          && cur.m_reason <= share_invalid_owner_left_group;
    if (canceled && state != 0 && state != share_state_invalid)
    {
        LOG_INFO("share card state kept as canceled, shareid: %lld, late state: %d, reason: %d.", static_cast<long long>(shareId),
                 state, reason);
        state = 0;
    }
    if (state != 0)
    {
        cur.m_state = state;
        cur.m_reason = reason;
    }
    if (myPerms >= 0)
        cur.m_myPerms = myPerms;
    const bool changed = before.m_state != cur.m_state || before.m_reason != cur.m_reason || before.m_myPerms != cur.m_myPerms;
    //监听者（按会话统计可用共享个数）不区分来源：列表应答带来的变化同样要计入
    if (changed && cur.m_state != 0 && m_cardStateListener != NULL)
        m_cardStateListener->OnCardStateChanged(shareId);
    if (!changed || !notifyHost || m_host == NULL || cur.m_state == 0)
        return;
    LOG_INFO("share card state changed, shareid: %lld, state: %d, reason: %d, perms: %d.", static_cast<long long>(shareId),
             cur.m_state, cur.m_reason, cur.m_myPerms);
    m_host->NotifyShareState(shareId, cur.m_state, cur.m_reason, cur.m_myPerms);
}

//==================== 「文件共享」窗口的查询 ====================

std::vector<ShareActiveVisitor> CShareService::GetActiveVisitors(int64_t shareId) const
{
    std::map<int64_t, ShareActiveVisitor> byVisitor;
    for (SessionMap::const_iterator it = m_sessions.begin(); it != m_sessions.end(); ++it)
    {
        if (it->second->GetShareId() != shareId)
            continue;
        ShareActiveVisitor& v = byVisitor[it->second->GetVisitorId()];
        v.m_visitorId = it->second->GetVisitorId();
        if (v.m_visitorName.empty())
            v.m_visitorName = it->second->GetVisitorName();
    }
    for (std::map<uint32_t, std::unique_ptr<TransferEntry> >::const_iterator it = m_transfers.begin();
         it != m_transfers.end(); ++it)
    {
        const TransferEntry& entry = *it->second;
        if (!entry.m_isOwner || entry.m_shareId != shareId)
            continue;
        ShareActiveVisitor& v = byVisitor[entry.m_visitorId];
        v.m_visitorId = entry.m_visitorId;
        if (entry.m_direction == share_transfer_upload)
            ++v.m_uploads;
        else
            ++v.m_downloads;
        v.m_bytesDone += entry.m_bytesDone;
        v.m_bytesTotal += entry.m_bytesTotal;
    }
    std::vector<ShareActiveVisitor> out;
    for (std::map<int64_t, ShareActiveVisitor>::const_iterator it = byVisitor.begin(); it != byVisitor.end(); ++it)
        out.push_back(it->second);
    return out;
}

bool CShareService::GetLocalRecord(int64_t shareId, ShareLocalRecord& out)
{
    return m_store.IsOpen() && m_store.GetByShareId(shareId, out);
}

bool CShareService::ListActivities(int64_t shareId, size_t limit, std::vector<ShareActivityRecord>& out)
{
    out.clear();
    if (!m_store.IsOpen())
        return false;
    //库里最多保留 kShareActivityKeep 条（全部共享合计），取全部再按共享筛选
    std::vector<ShareActivityRecord> all;
    if (!m_store.ListActivities(kShareActivityKeep, all))
        return false;
    for (size_t i = 0; i < all.size() && out.size() < limit; ++i)
    {
        if (all[i].m_shareId == shareId)
            out.push_back(all[i]);
    }
    return true;
}

} // namespace fileshare
