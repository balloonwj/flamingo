/**
 *  分享者一端访问会话请求处理器 CShareProvider 的实现，见 ShareProvider.h。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "ShareProvider.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>

#include "ShareFileOps.h"
#include "SharePathGuard.h"
#include "ShareWinPath.h"
#include "ShareThumbnail.h"
#include "ShareWriteOps.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/net/Protocol.h"   //error_code / share_kind / share_audit_action

namespace fileshare {

namespace {

/**
 *  同一个共享上的写操作串行执行（方案 7.5）：两名访问者的会话各在自己的接收线程上，可能同时改名与删除同一个条目。
 *  进程内每个共享一把锁，取出后一直保留（个数等于有过写操作的共享数，很小）。
 */
std::shared_ptr<std::mutex> ShareWriteLock(int64_t shareId)
{
    static std::mutex s_mapMutex;
    static std::map<int64_t, std::shared_ptr<std::mutex> > s_locks;
    std::lock_guard<std::mutex> guard(s_mapMutex);
    std::shared_ptr<std::mutex>& lock = s_locks[shareId];
    if (!lock)
        lock.reset(new std::mutex());
    return lock;
}

//写操作帧类型对应的权限检查项与审计动作
void WriteOpKinds(int frameType, share_operation& op, int& action)
{
    switch (frameType)
    {
    //新建文件夹
    case sf_mkdir:
        op = share_op_mkdir;
        action = share_audit_mkdir;
        break;
    //重命名
    case sf_rename:
        op = share_op_rename;
        action = share_audit_rename;
        break;
    //删除（放进回收站）
    case sf_delete:
        op = share_op_delete;
        action = share_audit_delete;
        break;
    //移动，以及调用方保证不会出现的其余类型
    default:
        op = share_op_move;
        action = share_audit_move;
        break;
    }
}

//本机当前时间（Unix 毫秒），审计记录的 clienttime
int64_t WallNowMs()
{
    FILETIME ft;
    ::GetSystemTimeAsFileTime(&ft);
    return ShareFileOps::FileTimeToUnixMs(ft);
}

} // namespace

/**
 *  CShareProviderCore —— 请求处理器与它的在途任务共享的部分。请求处理器销毁时 Detach，之后任务的发帧与回报都被丢弃。
 *  全部方法线程安全。
 */
class CShareProviderCore
{
public:
    CShareProviderCore(IShareChannel* channel, IShareProviderListener* listener)
        : m_channel(channel)
        , m_listener(listener)
        , m_recycler(NULL)
    {
    }

    //经通道发一帧；已断开时返回 false
    bool Send(const std::string& frame)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_channel != NULL && m_channel->SendFrame(frame);
    }

    //把写操作的审计交给监听者；已断开或没有监听者时丢弃
    void ReportWrite(const std::string& accessId, const std::vector<ShareAuditEntry>& audits)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (m_listener != NULL && !audits.empty())
            m_listener->OnWriteOpDone(accessId, audits);
    }

    //登记一个正在进行的搜索，返回它的取消标志；已断开时返回一个已置位的标志
    std::shared_ptr<std::atomic<bool> > AddSearch(int64_t reqId)
    {
        std::shared_ptr<std::atomic<bool> > flag(new std::atomic<bool>(false));
        std::lock_guard<std::mutex> guard(m_mutex);
        if (m_channel == NULL)
            flag->store(true);
        else
            m_searches[reqId] = flag;
        return flag;
    }

    //搜索结束（无论怎样结束）时去掉登记
    void EndSearch(int64_t reqId)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_searches.erase(reqId);
    }

    //置位一个搜索的取消标志；没有这个请求号时返回 false
    bool CancelSearch(int64_t reqId)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        std::map<int64_t, std::shared_ptr<std::atomic<bool> > >::iterator it = m_searches.find(reqId);
        if (it == m_searches.end())
            return false;
        it->second->store(true);
        return true;
    }

    //正在进行的搜索个数
    size_t GetSearchCount() const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_searches.size();
    }

    //注入或取出删除用的回收站（NULL 表示用系统回收站）
    void SetRecycler(IShareRecycler* recycler)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_recycler = recycler;
    }

    IShareRecycler* GetRecycler() const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_recycler;
    }

    //请求处理器销毁：断开通道与监听者，置位全部搜索的取消标志
    void Detach()
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_channel = NULL;
        m_listener = NULL;
        for (std::map<int64_t, std::shared_ptr<std::atomic<bool> > >::iterator it = m_searches.begin();
             it != m_searches.end(); ++it)
            it->second->store(true);
        m_searches.clear();
    }

private:
    mutable std::mutex       m_mutex;     //保护以下各项
    IShareChannel*           m_channel;   //控制通道（不拥有）；断开后为 NULL
    IShareProviderListener*  m_listener;  //监听者（不拥有）；断开后为 NULL
    IShareRecycler*          m_recycler;  //注入的回收站（不拥有）；NULL 表示用系统回收站
    std::map<int64_t, std::shared_ptr<std::atomic<bool> > > m_searches;   //正在进行的搜索：请求号 → 取消标志
};

namespace {

//编码并经核心发出一帧
void SendViaCore(CShareProviderCore& core, const ShareControlMessage& msg)
{
    std::string frame;
    if (ShareControlFraming::BuildFrame(msg, frame))
        core.Send(frame);
}

/**
 *  一次写操作的任务：在「同一个共享串行」的锁里交 ShareWriteOps 执行，回 sf_op_result，把每个条目的结果作为审计回报。
 *  执行所需的一切都在构造时拷贝，不引用请求处理器本身。
 */
class WriteTask : public IShareTask
{
public:
    WriteTask(const std::shared_ptr<CShareProviderCore>& core, const ShareControlMessage& req, const ShareWriteContext& ctx,
              int64_t shareId, const std::string& accessId, int64_t visitorId, int action)
        : m_core(core)
        , m_req(req)
        , m_ctx(ctx)
        , m_shareId(shareId)
        , m_accessId(accessId)
        , m_visitorId(visitorId)
        , m_action(action)
    {
    }

    virtual void Run() override
    {
        //注入的回收站优先（测试用），否则用系统回收站（无状态，每个任务各用一份）
        CShareSystemRecycler systemRecycler;
        IShareRecycler* injected = m_core->GetRecycler();
        m_ctx.m_recycler = (injected != NULL) ? injected : &systemRecycler;

        ShareControlMessage resp;
        resp.m_type = sf_op_result;
        resp.m_reqId = m_req.m_reqId;
        std::string toPath;   //审计里的目标：重命名为新名字，移动为目标目录
        {
            std::shared_ptr<std::mutex> lock = ShareWriteLock(m_shareId);
            std::lock_guard<std::mutex> guard(*lock);
            switch (m_req.m_type)
            {
            //新建文件夹：一条结果
            case sf_mkdir:
                {
                    ShareOpItem item;
                    item.m_path = m_req.m_path;
                    item.m_code = ShareWriteOps::MakeDirectory(m_ctx, m_req.m_path);
                    resp.m_items.push_back(item);
                    resp.m_code = item.m_code;
                }
                break;
            //重命名：一条结果
            case sf_rename:
                {
                    std::string newRel;
                    ShareOpItem item;
                    item.m_path = m_req.m_path;
                    item.m_code = ShareWriteOps::Rename(m_ctx, m_req.m_path, m_req.m_newName, newRel);
                    resp.m_items.push_back(item);
                    resp.m_code = item.m_code;
                    toPath = m_req.m_newName;
                }
                break;
            //删除：逐条结果
            case sf_delete:
                resp.m_code = ShareWriteOps::Delete(m_ctx, m_req.m_paths, resp.m_items);
                break;
            //移动：逐条结果
            default:
                resp.m_code = ShareWriteOps::Move(m_ctx, m_req.m_paths, m_req.m_toDir, resp.m_items);
                toPath = m_req.m_toDir;
                break;
            }
        }
        SendViaCore(*m_core, resp);
        LOG_INFO("share provider write op done, accessid: %s, type: %d, items: %u, code: %d.", m_accessId.c_str(),
                 m_req.m_type, static_cast<unsigned>(resp.m_items.size()), resp.m_code);

        const int64_t now = WallNowMs();
        std::vector<ShareAuditEntry> audits;
        for (size_t i = 0; i < resp.m_items.size(); ++i)
        {
            ShareAuditEntry audit;
            audit.m_shareId = m_shareId;
            audit.m_accessId = m_accessId;
            audit.m_visitorId = m_visitorId;
            audit.m_action = m_action;
            audit.m_path = resp.m_items[i].m_path;
            audit.m_toPath = toPath;
            audit.m_result = resp.m_items[i].m_code;
            audit.m_clientTime = now;
            audits.push_back(audit);
        }
        m_core->ReportWrite(m_accessId, audits);
    }

private:
    std::shared_ptr<CShareProviderCore> m_core;       //与请求处理器共享的核心
    ShareControlMessage                 m_req;        //请求
    ShareWriteContext                   m_ctx;        //共享根与选项（回收站在执行时填）
    int64_t                             m_shareId;    //共享 id（串行用的锁按它取）
    std::string                         m_accessId;   //访问会话 id
    int64_t                             m_visitorId;  //访问者 userid
    int                                 m_action;     //审计动作
};

/**
 *  一次缩略图的任务：交 ShareThumbnail 生成，回 sf_thumb_result。开始前已被取消的不生成、不回结果。
 */
class ThumbTask : public IShareTask
{
public:
    ThumbTask(const std::shared_ptr<CShareProviderCore>& core, int64_t reqId, const std::wstring& absPath, int maxEdge,
              const std::shared_ptr<std::atomic<bool> >& cancel, const std::string& accessId)
        : m_core(core)
        , m_reqId(reqId)
        , m_absPath(absPath)
        , m_maxEdge(maxEdge)
        , m_cancel(cancel)
        , m_accessId(accessId)
    {
    }

    virtual void Run() override
    {
        if (m_cancel->load())
        {
            m_core->EndSearch(m_reqId);
            return;
        }
        ShareControlMessage resp;
        resp.m_type = sf_thumb_result;
        resp.m_reqId = m_reqId;
        resp.m_code = ShareThumbnail::Make(m_absPath, m_maxEdge, resp.m_binary, resp.m_width, resp.m_height);
        m_core->EndSearch(m_reqId);
        SendViaCore(*m_core, resp);
        LOG_INFO("share provider thumbnail done, accessid: %s, reqid: %lld, code: %d, %dx%d, bytes: %u.",
                 m_accessId.c_str(), static_cast<long long>(m_reqId), resp.m_code, resp.m_width, resp.m_height,
                 static_cast<unsigned>(resp.m_binary.size()));
    }

private:
    std::shared_ptr<CShareProviderCore> m_core;      //与请求处理器共享的核心
    int64_t                             m_reqId;     //请求号
    std::wstring                        m_absPath;   //图片的本机绝对路径（已校验）
    int                                 m_maxEdge;   //长边像素数
    std::shared_ptr<std::atomic<bool> > m_cancel;    //取消标志
    std::string                         m_accessId;  //访问会话 id（日志用）
};

/**
 *  一次搜索的任务：交 ShareSearch 执行，命中按批编成 sf_search_result 经核心发出。
 */
class SearchTask : public IShareTask, public IShareSearchSink
{
public:
    SearchTask(const std::shared_ptr<CShareProviderCore>& core, int64_t reqId, const ShareSearchParams& params,
               const std::shared_ptr<std::atomic<bool> >& cancel, const std::string& accessId)
        : m_core(core)
        , m_reqId(reqId)
        , m_params(params)
        , m_cancel(cancel)
        , m_accessId(accessId)
        , m_hits(0)
    {
    }

    virtual void Run() override
    {
        const int outcome = ShareSearch::Run(m_params, m_cancel.get(), this);
        m_core->EndSearch(m_reqId);
        LOG_INFO("share provider search done, accessid: %s, reqid: %lld, hits: %u, outcome: %d.", m_accessId.c_str(),
                 static_cast<long long>(m_reqId), static_cast<unsigned>(m_hits), outcome);
    }

    virtual bool OnSearchHits(const std::vector<ShareEntry>& hits, bool done, bool truncated) override
    {
        m_hits += hits.size();
        std::vector<std::string> frames;
        if (!ShareControlFraming::BuildSearchResultFrames(m_reqId, hits, done, truncated, frames))
            return false;
        for (size_t i = 0; i < frames.size(); ++i)
        {
            if (!m_core->Send(frames[i]))
                return false;
        }
        return true;
    }

private:
    std::shared_ptr<CShareProviderCore>   m_core;      //与请求处理器共享的核心
    int64_t                               m_reqId;     //请求号
    ShareSearchParams                     m_params;    //搜索参数
    std::shared_ptr<std::atomic<bool> >   m_cancel;    //取消标志
    std::string                           m_accessId;  //访问会话 id（日志用）
    size_t                                m_hits;      //已交出的命中条数（日志用）
};

} // namespace

CShareProvider::CShareProvider(const ShareProviderContext& ctx, IShareChannel* channel,
                               IShareProviderListener* listener)
    : m_ctx(ctx)
    , m_channel(channel)
    , m_listener(listener)
    , m_permission(ctx.m_kind, ctx.m_perms)
    , m_heardVisitor(false)
    , m_listValid(false)
    , m_core(new CShareProviderCore(channel, listener))
{
}

CShareProvider::~CShareProvider()
{
    m_core->Detach();
}

bool CShareProvider::Start()
{
    const bool sent = SendHello();
    LOG_INFO("share provider started, accessid: %s, shareid: %lld, visitor: %lld, kind: %d, perms: %d, hello sent: %d.",
             m_ctx.m_accessId.c_str(), static_cast<long long>(m_ctx.m_shareId),
             static_cast<long long>(m_ctx.m_visitorId), m_ctx.m_kind, m_permission.GetPerms(), sent ? 1 : 0);
    return sent;
}

bool CShareProvider::ResendHello()
{
    return SendHello();
}

bool CShareProvider::HasHeardFromVisitor() const
{
    return m_heardVisitor.load();
}

bool CShareProvider::SendHello()
{
    ShareControlMessage hello;
    hello.m_type = sf_hello;
    hello.m_version = kShareControlVersion;
    hello.m_kind = m_ctx.m_kind;
    //根名只在单文件共享时给（就是那个文件的名字，访问者下载时要用）；磁盘与文件夹共享的根名取自真实路径
    //（文件夹名、盘符），不发给访问者，访问者的浏览窗口改用服务端在 2220 应答里给的共享名称（2026-10-04 起）
    if (m_ctx.m_kind == share_kind_file)
        hello.m_rootName = RootDisplayName(m_ctx.m_rootPath);
    hello.m_includeHidden = m_ctx.m_includeHidden;
    hello.m_perms = m_permission.GetPerms();
    return SendControlMessage(hello);
}

void CShareProvider::OnFrame(const char* data, size_t len)
{
    //收到任何一帧都说明访问者已经报到，sf_hello 不必再重发
    m_heardVisitor.store(true);

    ShareControlMessage req;
    if (!ShareControlFraming::DecodeFrame(data, len, req))
    {
        LOG_WARNING("share provider got an undecodable frame, accessid: %s, bytes: %u.",
                    m_ctx.m_accessId.c_str(), static_cast<unsigned>(len));
        return;
    }

    switch (req.m_type)
    {
    //列目录（含按游标取后续批次）
    case sf_list:
        HandleList(req);
        break;

    //查一个条目
    case sf_stat:
        HandleStat(req);
        break;

    //访问者主动结束访问会话：转告监听者关闭通道
    case sf_bye:
        LOG_INFO("share provider: visitor said bye, accessid: %s, errcode: %d.",
                 m_ctx.m_accessId.c_str(), req.m_errCode);
        if (m_listener != NULL)
            m_listener->OnVisitorBye(m_ctx.m_accessId, req.m_errCode);
        break;

    //四种写操作：新建文件夹、重命名、删除、移动
    case sf_mkdir:
    case sf_rename:
    case sf_delete:
    case sf_move:
        HandleWriteOp(req);
        break;

    //按文件名搜索
    case sf_search:
        HandleSearch(req);
        break;

    //缩略图或预览图
    case sf_thumb:
        HandleThumb(req);
        break;

    //取消一个尚未完成的搜索
    case sf_cancel:
        HandleCancel(req);
        break;

    //其余是分享者发给访问者的帧或传输子会话的首帧，访问者不应在控制通道上发来
    default:
        LOG_WARNING("share provider ignored a frame in the wrong direction, accessid: %s, type: %d.",
                    m_ctx.m_accessId.c_str(), req.m_type);
        break;
    }
}

bool CShareProvider::SetPerms(int perms)
{
    m_permission.SetPerms(perms);
    ShareControlMessage msg;
    msg.m_type = sf_perms;
    msg.m_perms = perms;
    const bool sent = SendControlMessage(msg);
    LOG_INFO("share provider perms changed, accessid: %s, perms: %d, notified: %d.",
             m_ctx.m_accessId.c_str(), perms, sent ? 1 : 0);
    return sent;
}

bool CShareProvider::SendBye(int errCode)
{
    ShareControlMessage msg;
    msg.m_type = sf_bye;
    msg.m_errCode = errCode;
    return SendControlMessage(msg);
}

const std::string& CShareProvider::GetAccessId() const
{
    return m_ctx.m_accessId;
}

int64_t CShareProvider::GetShareId() const
{
    return m_ctx.m_shareId;
}

int64_t CShareProvider::GetVisitorId() const
{
    return m_ctx.m_visitorId;
}

int CShareProvider::GetPerms() const
{
    return m_permission.GetPerms();
}

void CShareProvider::SetRecycler(IShareRecycler* recycler)
{
    m_core->SetRecycler(recycler);
}

size_t CShareProvider::GetRunningSearchCount() const
{
    return m_core->GetSearchCount();
}

std::string CShareProvider::RootDisplayName(const std::wstring& rootPath)
{
    std::wstring path = rootPath;
    while (path.size() > 3 && path[path.size() - 1] == L'\\')
        path.erase(path.size() - 1);
    //盘符根目录："D:\" 取 "D:"
    if (path.size() <= 3 && path.size() >= 2 && path[1] == L':')
        return ShareWideToUtf8(path.substr(0, 2));
    const size_t pos = path.find_last_of(L'\\');
    return ShareWideToUtf8((pos == std::wstring::npos) ? path : path.substr(pos + 1));
}

void CShareProvider::HandleList(const ShareControlMessage& req)
{
    //单文件共享没有目录可列
    if (m_ctx.m_kind == share_kind_file)
    {
        SendListError(req.m_reqId, error_code_share_kind_mismatch);
        return;
    }
    const int permCode = m_permission.Check(share_op_list);
    if (permCode != error_code_ok)
    {
        SendListError(req.m_reqId, permCode);
        return;
    }

    size_t offset = 0;
    if (!ShareControlFraming::ParseListCursor(req.m_cursor, offset))
    {
        SendListError(req.m_reqId, error_code_bad_request);
        return;
    }

    std::wstring absDir;
    const int pathCode = ResolveRequestPath(req.m_path, absDir);
    if (pathCode != error_code_ok)
    {
        SendListError(req.m_reqId, pathCode);
        return;
    }

    //首批（不带游标）、或缓存的不是这个目录时重新列目录；后续批次沿用缓存，各批来自同一份快照
    const bool useCache = !req.m_cursor.empty() && m_listValid && m_listPath == req.m_path;
    if (!useCache)
    {
        m_listValid = false;
        m_listEntries.clear();
        const int listCode = ShareFileOps::ListDirectory(absDir, m_ctx.m_includeHidden, m_listEntries);
        if (listCode != error_code_ok)
        {
            SendListError(req.m_reqId, listCode);
            LOG_INFO("share provider list failed, accessid: %s, code: %d.", m_ctx.m_accessId.c_str(), listCode);
            return;
        }
        m_listPath = req.m_path;
        m_listValid = true;
    }

    //游标越过了条目数（目录在两批之间被重新列过、条目变少了）：从末尾产出一个空的末批
    if (offset > m_listEntries.size())
        offset = m_listEntries.size();

    std::string frame;
    size_t next = 0;
    if (!ShareControlFraming::BuildListResultBatch(req.m_reqId, m_listEntries, offset, frame, next))
    {
        m_listValid = false;
        SendListError(req.m_reqId, error_code_share_io_fail);
        LOG_WARNING("share provider list batch too large, accessid: %s.", m_ctx.m_accessId.c_str());
        return;
    }
    if (!m_channel->SendFrame(frame))
        LOG_WARNING("share provider list result not sent, accessid: %s.", m_ctx.m_accessId.c_str());

    //最后一批已发出：释放缓存
    if (next >= m_listEntries.size())
    {
        LOG_INFO("share provider listed a directory, accessid: %s, entries: %u.",
                 m_ctx.m_accessId.c_str(), static_cast<unsigned>(m_listEntries.size()));
        m_listValid = false;
        m_listEntries.clear();
        m_listPath.clear();
    }
}

void CShareProvider::HandleStat(const ShareControlMessage& req)
{
    ShareControlMessage resp;
    resp.m_type = sf_stat_result;
    resp.m_reqId = req.m_reqId;
    resp.m_code = m_permission.Check(share_op_stat);

    std::wstring absPath;
    if (resp.m_code == error_code_ok)
        resp.m_code = ResolveRequestPath(req.m_path, absPath);
    if (resp.m_code == error_code_ok)
    {
        ShareEntry entry;
        resp.m_code = ShareFileOps::StatEntry(absPath, entry);
        if (resp.m_code == error_code_ok)
        {
            //共享根本身用显示名，其余用条目名
            if (req.m_path.empty())
                entry.m_name = RootDisplayName(m_ctx.m_rootPath);
            resp.m_entries.push_back(entry);
        }
    }
    SendControlMessage(resp);
}

void CShareProvider::HandleWriteOp(const ShareControlMessage& req)
{
    share_operation op = share_op_move;
    int action = 0;
    WriteOpKinds(req.m_type, op, action);

    //本次涉及的条目：新建与重命名是 path 一条，删除与移动是 paths
    std::vector<std::string> paths;
    if (req.m_type == sf_mkdir || req.m_type == sf_rename)
        paths.push_back(req.m_path);
    else
        paths = req.m_paths;

    int code = m_permission.Check(op);
    //多条目操作至少要有一条
    if (code == error_code_ok && paths.empty())
        code = error_code_bad_request;
    if (code != error_code_ok)
    {
        //权限不足或请求不完整：不进线程池，直接回结果并回报审计
        ShareControlMessage resp;
        resp.m_type = sf_op_result;
        resp.m_reqId = req.m_reqId;
        resp.m_code = code;
        std::vector<ShareAuditEntry> audits;
        const int64_t now = WallNowMs();
        for (size_t i = 0; i < paths.size(); ++i)
        {
            ShareOpItem item;
            item.m_path = paths[i];
            item.m_code = code;
            resp.m_items.push_back(item);
            ShareAuditEntry audit;
            audit.m_shareId = m_ctx.m_shareId;
            audit.m_accessId = m_ctx.m_accessId;
            audit.m_visitorId = m_ctx.m_visitorId;
            audit.m_action = action;
            audit.m_path = paths[i];
            audit.m_toPath = (req.m_type == sf_rename) ? req.m_newName : req.m_toDir;
            audit.m_result = code;
            audit.m_clientTime = now;
            audits.push_back(audit);
        }
        SendControlMessage(resp);
        LOG_INFO("share provider write op refused, accessid: %s, type: %d, code: %d.", m_ctx.m_accessId.c_str(),
                 req.m_type, code);
        m_core->ReportWrite(m_ctx.m_accessId, audits);
        return;
    }

    ShareWriteContext ctx;
    ctx.m_rootPath = m_ctx.m_rootPath;
    ctx.m_kind = m_ctx.m_kind;
    ctx.m_includeHidden = m_ctx.m_includeHidden;
    PostOrRun(std::unique_ptr<IShareTask>(
        new WriteTask(m_core, req, ctx, m_ctx.m_shareId, m_ctx.m_accessId, m_ctx.m_visitorId, action)));
}

void CShareProvider::HandleSearch(const ShareControlMessage& req)
{
    ShareSearchParams params;
    int code = m_permission.Check(share_op_search);
    //列整棵目录树时不用关键字
    params.m_matchAll = req.m_matchAll;
    if (code == error_code_ok && !req.m_matchAll && !ShareSearch::CheckKeyword(req.m_keyword, params.m_keyword))
        code = error_code_bad_request;
    std::wstring startAbs;
    if (code == error_code_ok)
        code = ResolveRequestPath(req.m_path, startAbs);
    if (code == error_code_ok)
    {
        const DWORD attrs = ::GetFileAttributesW(ShareToLongPath(startAbs).c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
            code = error_code_share_path_not_found;
    }
    if (code != error_code_ok)
    {
        ShareControlMessage resp;
        resp.m_type = sf_search_result;
        resp.m_reqId = req.m_reqId;
        resp.m_done = true;
        resp.m_code = code;
        SendControlMessage(resp);
        LOG_INFO("share provider search refused, accessid: %s, reqid: %lld, code: %d.", m_ctx.m_accessId.c_str(),
                 static_cast<long long>(req.m_reqId), code);
        return;
    }

    params.m_startAbs = startAbs;
    params.m_startRel = req.m_path;
    params.m_includeHidden = m_ctx.m_includeHidden;
    params.m_maxResults = (m_ctx.m_searchMaxResults > 0) ? m_ctx.m_searchMaxResults : kShareSearchDefaultMaxResults;
    //列整棵目录树：上限取访问者要的与 kShareTreeMaxItems + 1 中较小的（多一条用来判断是否超过上限）
    if (req.m_matchAll)
        params.m_maxResults = (req.m_maxItems > 0 && req.m_maxItems <= kShareTreeMaxItems + 1) ? req.m_maxItems
                                                                                              : kShareTreeMaxItems + 1;
    params.m_timeoutMs = (m_ctx.m_searchTimeoutMs > 0) ? m_ctx.m_searchTimeoutMs : kShareSearchDefaultTimeoutMs;
    LOG_INFO("share provider search started, accessid: %s, reqid: %lld, max: %d, timeout: %d ms.", m_ctx.m_accessId.c_str(),
             static_cast<long long>(req.m_reqId), params.m_maxResults, params.m_timeoutMs);
    std::shared_ptr<std::atomic<bool> > cancel = m_core->AddSearch(req.m_reqId);
    PostOrRun(std::unique_ptr<IShareTask>(new SearchTask(m_core, req.m_reqId, params, cancel, m_ctx.m_accessId)));
}

void CShareProvider::HandleCancel(const ShareControlMessage& req)
{
    const bool found = m_core->CancelSearch(req.m_reqId);
    LOG_INFO("share provider cancel, accessid: %s, reqid: %lld, found: %d.", m_ctx.m_accessId.c_str(),
             static_cast<long long>(req.m_reqId), found ? 1 : 0);
}

void CShareProvider::HandleThumb(const ShareControlMessage& req)
{
    int code = m_permission.Check(share_op_thumb);
    std::wstring absPath;
    if (code == error_code_ok)
        code = ResolveRequestPath(req.m_path, absPath);
    if (code == error_code_ok)
    {
        const DWORD attrs = ::GetFileAttributesW(ShareToLongPath(absPath).c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0)
            code = error_code_share_path_not_found;
    }
    if (code == error_code_ok && !ShareThumbnail::IsImageName(absPath))
        code = error_code_bad_request;
    if (code != error_code_ok)
    {
        ShareControlMessage resp;
        resp.m_type = sf_thumb_result;
        resp.m_reqId = req.m_reqId;
        resp.m_code = code;
        SendControlMessage(resp);
        return;
    }
    std::shared_ptr<std::atomic<bool> > cancel = m_core->AddSearch(req.m_reqId);
    PostOrRun(std::unique_ptr<IShareTask>(new ThumbTask(m_core, req.m_reqId, absPath, req.m_maxEdge, cancel,
                                                        m_ctx.m_accessId)));
}

void CShareProvider::PostOrRun(std::unique_ptr<IShareTask> task)
{
    //线程池接受之后任务归它；没有线程池、或线程池已停止（Post 失败时任务仍在本函数手里）时在本线程上执行
    if (m_ctx.m_pool != NULL && m_ctx.m_pool->Post(task))
        return;
    task->Run();
}

int CShareProvider::ResolveRequestPath(const std::string& relPath, std::wstring& absPath) const
{
    //与写操作共用一套校验（写法与拼接 → 共享根是否存在 → 重解析点 → 隐藏属性 → 正在上传的半成品）
    ShareWriteContext ctx;
    ctx.m_rootPath = m_ctx.m_rootPath;
    ctx.m_kind = m_ctx.m_kind;
    ctx.m_includeHidden = m_ctx.m_includeHidden;
    return ShareWriteOps::ResolvePath(ctx, relPath, absPath);
}

bool CShareProvider::SendControlMessage(const ShareControlMessage& msg)
{
    std::string frame;
    if (!ShareControlFraming::BuildFrame(msg, frame))
    {
        LOG_WARNING("share provider failed to build frame, accessid: %s, type: %d.", m_ctx.m_accessId.c_str(), msg.m_type);
        return false;
    }
    if (!m_channel->SendFrame(frame))
    {
        LOG_WARNING("share provider failed to send frame, accessid: %s, type: %d.", m_ctx.m_accessId.c_str(), msg.m_type);
        return false;
    }
    return true;
}

void CShareProvider::SendListError(int64_t reqId, int code)
{
    ShareControlMessage resp;
    resp.m_type = sf_list_result;
    resp.m_reqId = reqId;
    resp.m_code = code;
    SendControlMessage(resp);
}

} // namespace fileshare
