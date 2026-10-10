/**
 *  帖子群模块对象 CThreadGroupModule 的实现，见 ThreadGroupModule.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "ThreadGroupModule.h"

#include <string.h>
#include <wchar.h>

#include "ThreadLog.h"
#include "../Core/ThreadCodec.h"
#include "../../Source/Common/Log/IULog.h"

//本模块所在映像的基址（链接器提供），用来注册消息窗口类
extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace threadgroup {

namespace {

//消息窗口类名
const wchar_t* const kMsgWndClass = L"FlamingoThreadGroupMsgWnd";

//群号拆成两个 32 位数放进消息参数
const int kHalfBits = 32;
const uint64_t kLowMask = 0xFFFFFFFFULL;

//表情名缓冲与路径缓冲在宿主接口里的容量
const uint32_t kFaceNameChars = kThreadNameChars;

} // namespace

CThreadGroupModule& CThreadGroupModule::Instance()
{
    static CThreadGroupModule s_module;
    return s_module;
}

CThreadGroupModule::CThreadGroupModule()
    : m_host(NULL)
    , m_factory(NULL)
    , m_initialized(false)
    , m_headless(false)
    , m_loggedIn(false)
    , m_hwnd(NULL)
    , m_postTarget(NULL)
    , m_nextImageId(0)
    , m_facesLoaded(false)
{
}

CThreadGroupModule::~CThreadGroupModule()
{
}

void CThreadGroupModule::SetWindowFactory(IThreadWindowFactory* factory)
{
    m_factory = factory;
}

void CThreadGroupModule::SetHeadlessForTest(bool headless)
{
    m_headless = headless;
}

//======================== IThreadGroupModule ========================

bool CThreadGroupModule::Init(IThreadGroupHost* host, const ThreadInitParams* params)
{
    if (m_initialized)
        return true;
    if (host == NULL || params == NULL || params->m_cbSize != sizeof(ThreadInitParams))
        return false;
    m_host = host;
    ThreadLogSetHost(host);
    if (m_factory != NULL && !m_factory->InitUi(host))
    {
        LOG_ERROR("thread group ui init failed.");
        ThreadLogSetHost(NULL);
        m_host = NULL;
        return false;
    }
    if (!m_headless && !CreateMessageWindow())
    {
        LOG_ERROR("thread group message window creation failed, error=%u.", static_cast<unsigned>(::GetLastError()));
        if (m_factory != NULL)
            m_factory->ShutdownUi();
        ThreadLogSetHost(NULL);
        m_host = NULL;
        return false;
    }
    m_initialized = true;
    LOG_INFO("FlamingoThreadGroup initialized, api version %u.", static_cast<unsigned>(kApiVersion));
    return true;
}

void CThreadGroupModule::Shutdown()
{
    if (!m_initialized)
        return;
    CloseAll();
    DestroyMessageWindow();
    if (m_factory != NULL)
        m_factory->ShutdownUi();
    m_seqGroup.clear();
    m_imageGroup.clear();
    m_loggedIn = false;
    m_initialized = false;
    LOG_INFO("FlamingoThreadGroup shut down.");
    ThreadLogSetHost(NULL);
    m_host = NULL;
}

uint32_t CThreadGroupModule::GetApiVersion() const
{
    return kApiVersion;
}

void CThreadGroupModule::OnLoggedIn()
{
    m_loggedIn = true;
    //断线之前发出的请求不会再有应答，重连后服务端的 seq 也可能从头计数：清掉登记，避免把新应答交错了群
    m_seqGroup.clear();
    GroupMap groups = m_groups;
    for (GroupMap::iterator it = groups.begin(); it != groups.end(); ++it)
    {
        if (it->second->m_closed)
            continue;
        it->second->m_session->OnDisconnected();
        it->second->m_session->Start();
    }
    LOG_INFO("thread group logged in, %u open window(s) restarted.", static_cast<unsigned>(groups.size()));
}

void CThreadGroupModule::OnLoggingOut()
{
    CloseAll();
    m_seqGroup.clear();
    m_imageGroup.clear();
    m_loggedIn = false;
    LOG_INFO("thread group logging out, all windows closed.");
}

void CThreadGroupModule::OnServerFrame(uint16_t cmd, uint32_t seq, const char* body, uint32_t len)
{
    FramePacket* packet = new FramePacket();
    packet->m_cmd = cmd;
    packet->m_seq = seq;
    if (body != NULL && len > 0)
        packet->m_body.assign(body, len);
    HWND target = NULL;
    {
        std::lock_guard<std::mutex> guard(m_wndMutex);
        target = m_postTarget;
    }
    if (target == NULL || !::PostMessageW(target, kMsgThreadFrameArrived, 0, reinterpret_cast<LPARAM>(packet)))
    {
        LOG_WARNING("thread frame dropped, cmd=%u, module not ready.", static_cast<unsigned>(cmd));
        delete packet;
    }
}

bool CThreadGroupModule::OpenThreadWindow(int64_t groupId, int64_t postId)
{
    if (!m_initialized || groupId <= 0)
        return false;
    GroupMap::iterator it = m_groups.find(groupId);
    //上一次关闭的窗口还没来得及释放：现在释放（此时不在那个窗口的消息处理里）
    if (it != m_groups.end() && it->second->m_closed)
    {
        ReleaseGroup(groupId);
        it = m_groups.end();
    }
    if (it == m_groups.end())
    {
        std::shared_ptr<GroupEntry> entry(new GroupEntry());
        entry->m_session.reset(new CThreadSession(groupId, this));
        if (m_factory != NULL)
        {
            entry->m_window.reset(m_factory->CreateThreadWindow(groupId, entry->m_session.get(), this));
            if (!entry->m_window)
            {
                LOG_ERROR("thread window creation failed, groupid=%lld.", static_cast<long long>(groupId));
                return false;
            }
            entry->m_session->SetView(entry->m_window->View());
        }
        m_groups[groupId] = entry;
        entry->m_session->Start();
        LOG_INFO("thread group opened, groupid=%lld, postid=%lld.", static_cast<long long>(groupId),
                 static_cast<long long>(postId));
    }
    std::shared_ptr<GroupEntry> entry = m_groups[groupId];
    if (entry->m_window)
    {
        if (!entry->m_window->Open(postId))
        {
            LOG_ERROR("thread window open failed, groupid=%lld.", static_cast<long long>(groupId));
            entry->m_session->Stop();
            m_groups.erase(groupId);
            return false;
        }
        return true;
    }
    //无窗口模式：只选中帖子
    if (postId > 0)
        entry->m_session->SelectPost(postId);
    return true;
}

void CThreadGroupModule::CloseThreadWindow(int64_t groupId)
{
    GroupMap::iterator it = m_groups.find(groupId);
    if (it == m_groups.end())
        return;
    std::shared_ptr<GroupEntry> entry = it->second;
    m_groups.erase(it);
    entry->m_session->SetView(NULL);
    entry->m_session->Stop();
    if (entry->m_watching && m_host != NULL)
        m_host->OnWatchChanged(groupId, false);
    if (entry->m_window)
        entry->m_window->Close();
    LOG_INFO("thread group closed by host, groupid=%lld.", static_cast<long long>(groupId));
}

bool CThreadGroupModule::IsWatching(int64_t groupId) const
{
    GroupMap::const_iterator it = m_groups.find(groupId);
    if (it == m_groups.end() || it->second->m_closed || !it->second->m_window)
        return false;
    return it->second->m_window->IsWatching();
}

void CThreadGroupModule::OnGroupChanged(int64_t groupId)
{
    GroupMap::iterator it = m_groups.find(groupId);
    if (it != m_groups.end() && !it->second->m_closed && it->second->m_window)
        it->second->m_window->OnGroupChanged();
}

void CThreadGroupModule::OnMemberPanelClosed(int64_t groupId)
{
    GroupMap::iterator it = m_groups.find(groupId);
    if (it != m_groups.end() && !it->second->m_closed && it->second->m_window)
        it->second->m_window->OnMemberPanelClosed();
}

void CThreadGroupModule::OnGroupMembers(int64_t groupId, const ThreadMemberInfo* members, uint32_t count)
{
    GroupMap::iterator it = m_groups.find(groupId);
    if (it == m_groups.end() || it->second->m_closed || !it->second->m_window)
        return;
    //结构体大小对不上说明宿主与本 DLL 的布局不一致（版本号本应已拦下），整份名单按空处理，不按错位的内存读名字
    if (members != NULL && count > 0 && members[0].m_cbSize != sizeof(ThreadMemberInfo))
    {
        LOG_ERROR("thread group members ignored, bad struct size %u, groupid=%lld.",
                  static_cast<unsigned>(members[0].m_cbSize), static_cast<long long>(groupId));
        count = 0;
    }
    std::vector<ThreadMember> list;
    for (uint32_t i = 0; members != NULL && i < count; ++i)
    {
        ThreadMember m;
        m.m_userId = members[i].m_userId;
        m.m_role = members[i].m_role;
        //名字按定长数组读：宿主没写结尾 0 时也不越界
        m.m_name.assign(members[i].m_name, wcsnlen(members[i].m_name, kThreadNameChars));
        list.push_back(m);
    }
    it->second->m_window->OnGroupMembers(list);
}

void CThreadGroupModule::OnImageUploaded(uint32_t requestId, bool ok, const char* fileId, const char* md5,
                                         uint32_t width, uint32_t height, int64_t size, const wchar_t* localPath)
{
    std::map<uint32_t, int64_t>::iterator req = m_imageGroup.find(requestId);
    if (req == m_imageGroup.end())
        return;
    const int64_t groupId = req->second;
    m_imageGroup.erase(req);
    GroupMap::iterator it = m_groups.find(groupId);
    if (it == m_groups.end() || it->second->m_closed || !it->second->m_window)
        return;
    ThreadImage image;
    if (ok)
    {
        image.m_fileId = (fileId != NULL) ? fileId : "";
        image.m_md5 = (md5 != NULL) ? md5 : "";
        image.m_width = static_cast<int>(width);
        image.m_height = static_cast<int>(height);
        image.m_size = size;
    }
    it->second->m_window->OnImageUploaded(requestId, ok && !image.m_fileId.empty(), image,
                                          localPath != NULL ? std::wstring(localPath) : std::wstring());
}

void CThreadGroupModule::OnImageReady(uint32_t requestId, bool ok, const wchar_t* localPath)
{
    std::map<uint32_t, int64_t>::iterator req = m_imageGroup.find(requestId);
    if (req == m_imageGroup.end())
        return;
    const int64_t groupId = req->second;
    m_imageGroup.erase(req);
    GroupMap::iterator it = m_groups.find(groupId);
    if (it == m_groups.end() || it->second->m_closed || !it->second->m_window)
        return;
    it->second->m_window->OnImageReady(requestId, ok, localPath != NULL ? std::wstring(localPath) : std::wstring());
}

void CThreadGroupModule::OnAvatarsUpdated()
{
    for (GroupMap::iterator it = m_groups.begin(); it != m_groups.end(); ++it)
    {
        if (!it->second->m_closed && it->second->m_window)
            it->second->m_window->OnAvatarsUpdated();
    }
}

//======================== 收帧 ========================

void CThreadGroupModule::ProcessFrame(uint16_t cmd, uint32_t seq, const std::string& body)
{
    if (cmd == thread_cmd_changed_notify)
    {
        ThreadChange change;
        if (!ThreadCodec::ParseChangedNotify(body, change))
        {
            LOG_WARNING("thread changed notify unparsable, %u bytes.", static_cast<unsigned>(body.size()));
            return;
        }
        GroupMap::iterator it = m_groups.find(change.m_groupId);
        if (it == m_groups.end() || it->second->m_closed)
            return;
        std::shared_ptr<GroupEntry> entry = it->second;
        entry->m_session->OnChange(change);
        //正在看的群有了会产生卡片消息的变化：请宿主按最新的消息标为已读
        const bool makesCard = change.m_changeType == thread_change_post_new
                               || change.m_changeType == thread_change_comment_new
                               || change.m_changeType == thread_change_addendum_new;
        if (makesCard && m_host != NULL && entry->m_window && entry->m_window->IsWatching())
            m_host->MarkGroupRead(change.m_groupId);
        return;
    }
    std::map<uint32_t, int64_t>::iterator req = m_seqGroup.find(seq);
    if (req == m_seqGroup.end())
    {
        LOG_INFO("thread response without pending request dropped, cmd=%u, seq=%u.", static_cast<unsigned>(cmd),
                 static_cast<unsigned>(seq));
        return;
    }
    const int64_t groupId = req->second;
    m_seqGroup.erase(req);
    GroupMap::iterator it = m_groups.find(groupId);
    if (it == m_groups.end() || it->second->m_closed)
        return;
    std::shared_ptr<GroupEntry> entry = it->second;
    if (!entry->m_session->OnResponse(cmd, seq, body))
        LOG_INFO("thread response not claimed by session, cmd=%u, seq=%u.", static_cast<unsigned>(cmd),
                 static_cast<unsigned>(seq));
}

CThreadSession* CThreadGroupModule::SessionOf(int64_t groupId)
{
    GroupMap::iterator it = m_groups.find(groupId);
    return (it != m_groups.end()) ? it->second->m_session.get() : NULL;
}

size_t CThreadGroupModule::PendingSeqCount() const
{
    return m_seqGroup.size();
}

//======================== IThreadSessionSender ========================

uint32_t CThreadGroupModule::SendThreadCommand(int64_t groupId, uint16_t cmd, const std::string& body)
{
    if (m_host == NULL || body.empty())
        return 0;
    const uint32_t seq = m_host->SendThreadCommand(cmd, body.data(), static_cast<uint32_t>(body.size()));
    if (seq == 0)
    {
        LOG_WARNING("thread command not sent, cmd=%u, groupid=%lld.", static_cast<unsigned>(cmd),
                    static_cast<long long>(groupId));
        return 0;
    }
    m_seqGroup[seq] = groupId;
    return seq;
}

int64_t CThreadGroupModule::MyUserId()
{
    return (m_host != NULL) ? m_host->GetMyUserId() : 0;
}

//======================== IThreadWindowOwner ========================

IThreadGroupHost* CThreadGroupModule::Host()
{
    return m_host;
}

const FaceTable& CThreadGroupModule::Faces()
{
    EnsureFaces();
    return m_faces;
}

const std::vector<ThreadFaceEntry>& CThreadGroupModule::FaceEntries()
{
    EnsureFaces();
    return m_faceEntries;
}

uint32_t CThreadGroupModule::BeginImageRequest(int64_t groupId)
{
    ++m_nextImageId;
    if (m_nextImageId == 0)
        ++m_nextImageId;
    m_imageGroup[m_nextImageId] = groupId;
    return m_nextImageId;
}

bool CThreadGroupModule::StartUpload(uint32_t requestId, const std::wstring& localPath)
{
    if (m_host == NULL || m_imageGroup.find(requestId) == m_imageGroup.end())
        return false;
    if (m_host->UploadImage(requestId, localPath.c_str()))
        return true;
    m_imageGroup.erase(requestId);
    LOG_WARNING("thread image upload refused by host, request=%u.", static_cast<unsigned>(requestId));
    return false;
}

bool CThreadGroupModule::StartDownload(uint32_t requestId, const std::string& fileId, const std::string& md5)
{
    if (m_host == NULL || m_imageGroup.find(requestId) == m_imageGroup.end())
        return false;
    if (m_host->DownloadImage(requestId, fileId.c_str(), md5.c_str()))
        return true;
    //宿主可能已经同步回报过（并删掉了登记），这里再删一次无妨
    m_imageGroup.erase(requestId);
    LOG_WARNING("thread image download refused by host, request=%u.", static_cast<unsigned>(requestId));
    return false;
}

void CThreadGroupModule::OnWindowWatchChanged(int64_t groupId, bool watching)
{
    GroupMap::iterator it = m_groups.find(groupId);
    if (it == m_groups.end() || it->second->m_watching == watching)
        return;
    it->second->m_watching = watching;
    if (m_host == NULL)
        return;
    m_host->OnWatchChanged(groupId, watching);
    if (watching)
        m_host->MarkGroupRead(groupId);
}

void CThreadGroupModule::OnWindowClosed(int64_t groupId)
{
    GroupMap::iterator it = m_groups.find(groupId);
    if (it == m_groups.end() || it->second->m_closed)
        return;
    std::shared_ptr<GroupEntry> entry = it->second;
    entry->m_closed = true;
    entry->m_session->SetView(NULL);
    entry->m_session->Stop();
    if (entry->m_watching && m_host != NULL)
    {
        entry->m_watching = false;
        m_host->OnWatchChanged(groupId, false);
    }
    LOG_INFO("thread window closed by user, groupid=%lld.", static_cast<long long>(groupId));
    //窗口对象此刻还在自己的消息处理里，投递一条消息，等它返回后再释放
    const uint64_t id = static_cast<uint64_t>(groupId);
    if (m_hwnd != NULL)
    {
        ::PostMessageW(m_hwnd, kMsgThreadReleaseWindow, static_cast<WPARAM>(id & kLowMask),
                       static_cast<LPARAM>(id >> kHalfBits));
    }
}

//======================== 内部 ========================

void CThreadGroupModule::ReleaseGroup(int64_t groupId)
{
    GroupMap::iterator it = m_groups.find(groupId);
    if (it == m_groups.end() || !it->second->m_closed)
        return;
    std::shared_ptr<GroupEntry> entry = it->second;
    m_groups.erase(it);
    //窗口已经销毁，Close 只释放对象里剩下的资源
    if (entry->m_window)
        entry->m_window->Close();
    entry->m_window.reset();
    entry->m_session.reset();
}

void CThreadGroupModule::CloseAll()
{
    GroupMap groups;
    groups.swap(m_groups);
    for (GroupMap::iterator it = groups.begin(); it != groups.end(); ++it)
    {
        std::shared_ptr<GroupEntry> entry = it->second;
        entry->m_session->SetView(NULL);
        if (!entry->m_closed)
            entry->m_session->Stop();
        if (entry->m_watching && m_host != NULL)
            m_host->OnWatchChanged(it->first, false);
        if (entry->m_window)
            entry->m_window->Close();
        entry->m_window.reset();
        entry->m_session.reset();
    }
}

void CThreadGroupModule::EnsureFaces()
{
    if (m_facesLoaded || m_host == NULL)
        return;
    m_facesLoaded = true;
    const uint32_t count = m_host->GetFaceCount();
    for (uint32_t i = 0; i < count; ++i)
    {
        ThreadFaceInfo info;
        memset(&info, 0, sizeof(info));
        info.m_cbSize = sizeof(info);
        if (!m_host->GetFaceInfo(i, &info))
            continue;
        info.m_name[kFaceNameChars - 1] = L'\0';
        info.m_path[kThreadPathChars - 1] = L'\0';
        ThreadFaceEntry entry;
        entry.m_faceId = info.m_faceId;
        entry.m_name = info.m_name;
        entry.m_path = info.m_path;
        m_faceEntries.push_back(entry);
        m_faces.Add(entry.m_faceId, entry.m_name);
    }
    LOG_INFO("thread group face library loaded, %u face(s).", static_cast<unsigned>(m_faceEntries.size()));
}

bool CThreadGroupModule::CreateMessageWindow()
{
    HINSTANCE inst = reinterpret_cast<HINSTANCE>(&__ImageBase);
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &CThreadGroupModule::MessageWndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kMsgWndClass;
    //重复注册（DLL 卸载后又加载）返回失败但类仍可用，故不检查返回值
    ::RegisterClassExW(&wc);
    m_hwnd = ::CreateWindowExW(0, kMsgWndClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, inst, NULL);
    if (m_hwnd == NULL)
        return false;
    ::SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    std::lock_guard<std::mutex> guard(m_wndMutex);
    m_postTarget = m_hwnd;
    return true;
}

void CThreadGroupModule::DestroyMessageWindow()
{
    {
        std::lock_guard<std::mutex> guard(m_wndMutex);
        m_postTarget = NULL;
    }
    if (m_hwnd == NULL)
        return;
    //丢弃还在队列里的帧，释放它们的包体
    MSG msg;
    while (::PeekMessageW(&msg, m_hwnd, kMsgThreadFrameArrived, kMsgThreadFrameArrived, PM_REMOVE))
        delete reinterpret_cast<FramePacket*>(msg.lParam);
    ::DestroyWindow(m_hwnd);
    m_hwnd = NULL;
}

LRESULT CALLBACK CThreadGroupModule::MessageWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    CThreadGroupModule* self = reinterpret_cast<CThreadGroupModule*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != NULL && msg == kMsgThreadFrameArrived)
    {
        FramePacket* packet = reinterpret_cast<FramePacket*>(lParam);
        if (packet != NULL)
        {
            self->ProcessFrame(packet->m_cmd, packet->m_seq, packet->m_body);
            delete packet;
        }
        return 0;
    }
    if (self != NULL && msg == kMsgThreadReleaseWindow)
    {
        const uint64_t id = (static_cast<uint64_t>(static_cast<uint32_t>(lParam)) << kHalfBits)
                            | (static_cast<uint64_t>(wParam) & kLowMask);
        self->ReleaseGroup(static_cast<int64_t>(id));
        return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace threadgroup
