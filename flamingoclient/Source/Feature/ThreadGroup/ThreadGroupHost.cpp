/**
 *  主程序一侧的帖子群接入 CThreadGroupHost 的实现，见 ThreadGroupHost.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"

#include "ThreadGroupHost.h"
#include "ThreadGroupFilter.h"   // ThreadMentionName：成员名单里每个人的显示名
#include "ThreadCardSync.h"      // ParseThreadCards
#include "../../net/Protocol.h"  // cmd_thread_cards_notify
#include "ThreadGroupGate.h"

#include <string.h>   // memset / wcsncpy_s

#include "../../Common/Log/IULog.h"
#include "../../Common/I18n/I18n.h"          // i18n::Translate / GetLanguage / GetUiFontFace：宿主接口的多语言部分
#include "../../Common/Util/Path.h"          // Hootina::CPath::GetAppPath：表情库配置文件的位置
#include "../../Common/Graphics/FaceList.h"  // CFaceList：表情库
#include "../../Session/UserSessionData.h"   // g_szHomePath（程序目录，DLL 与 exe 同目录）
#include "../../net/IUSocket.h"              // CIUSocket::SendThreadCommand / RequestGroupMembers
#include "../../net/GroupProtocolClient.h"   // net::GroupMembersResult
#include "../../net/ImageTransfer.h"         // net::ImageTransfer::EnqueuePlainUpload / EnqueuePlainDownload

namespace balloonthread {

namespace {

//接收图片结果的隐藏消息窗口的窗口类名
const wchar_t* const kMsgWndClass = L"FlamingoThreadHostMsgWnd";

//DLL 写来的日志在日志行「函数签名」一栏显示的名字，便于按模块筛选；DLL 已在正文前加了 "[thread] [调用处:行号] "
const char* const kThreadLogSource = "FlamingoThreadGroup";

//通讯录里查不到用户时的显示名模板（与主面板 ContactDisplayName 的兜底一致），{0} 为用户 id；
//只做标记（TR_NOOP），使用处用 TRF 取译文
const wchar_t* const kFallbackNameTemplate = TR_NOOP(L"用户{0}");

//表情库配置文件（相对程序目录），与聊天窗的表情选择浮层是同一份
const wchar_t* const kFaceConfigRelativePath = L"Face\\FaceConfig.xml";

//ThreadGroupBrief::m_myRole 表示「本人不是这个群的成员」的取值（见 ThreadGroupApi.h）
const int kRoleNotMember = -1;

//表情库里表情名（tip）的前缀：FaceConfig.xml 写的是 "/呲牙"，交给 DLL 时去掉（与表情选择浮层的提示文字一致）
const wchar_t kFaceTipPrefix = L'/';

//把宽字符串写进定长缓冲区：写不下时截断，结果始终以 '\0' 结尾
void CopyWide(const std::wstring& text, wchar_t* buf, uint32_t chars)
{
    wcsncpy_s(buf, chars, text.c_str(), _TRUNCATE);
}

} // namespace

CThreadGroupHost& CThreadGroupHost::GetInstance()
{
    static CThreadGroupHost s_instance;
    return s_instance;
}

CThreadGroupHost::CThreadGroupHost()
    : m_loadAttempted(false)
    , m_ready(false)
    , m_featureEnabled(false)
    , m_loggedIn(false)
    , m_moduleActive(false)
    , m_myUserId(0)
    , m_directory(NULL)
    , m_msgWnd(NULL)
    , m_viewerOwner(NULL)
    , m_viewerIndex(0)
    , m_routeTarget(NULL)
{
}

CThreadGroupHost::~CThreadGroupHost()
{
}

void CThreadGroupHost::SetDirectory(IThreadGroupDirectory* directory)
{
    m_directory = directory;
}

void CThreadGroupHost::SetFeatureEnabled(bool enabled)
{
    m_featureEnabled = enabled;
}

void CThreadGroupHost::OnLoginSucceeded(int64_t myUserId)
{
    m_myUserId = myUserId;
    m_loggedIn = true;
    const int action = DecideThreadLogin(m_featureEnabled, m_moduleActive);
    //断线期间开关被关掉：与运行期收到 1013 由开变关相同，关闭全部帖子群窗口，DLL 保持加载
    if (action == thread_login_deactivate)
    {
        DeactivateModule();
        LOG_INFO("thread group was disabled while disconnected, module deactivated and windows closed.");
        return;
    }
    if (action == thread_login_none)
    {
        LOG_INFO("thread group is disabled for this enterprise, module not activated.");
        return;
    }
    if (!EnsureModuleReady())
        return;
    //每次登录成功（含断线重连）都通知：模块据此让已打开的窗口重新登记「正在查看」并刷新
    ActivateModule();
}

bool CThreadGroupHost::OnFeaturesChanged(bool enabled)
{
    const bool before = IsAvailable();
    m_featureEnabled = enabled;
    if (m_loggedIn && enabled && !m_moduleActive)
    {
        //由关变开：必要时先加载 DLL，再让模块进入已登录状态
        if (EnsureModuleReady())
            ActivateModule();
    }
    else if (!enabled && m_moduleActive)
    {
        //由开变关：模块关闭全部帖子群窗口，DLL 保持加载；帖子群随后从各处隐藏
        DeactivateModule();
    }
    const bool after = IsAvailable();
    LOG_INFO("thread group feature switch changed: enabled=%d, available %d -> %d.", enabled ? 1 : 0, before ? 1 : 0,
             after ? 1 : 0);
    return before != after;
}

void CThreadGroupHost::OnLoggingOut()
{
    DeactivateModule();
    m_loggedIn = false;
    m_myUserId = 0;
    m_pendingGroupMembers.clear();
    m_viewerPaths.clear();
    m_viewerOwner = NULL;
}

void CThreadGroupHost::Shutdown()
{
    if (!m_loader.IsLoaded())
        return;
    //先解除帧路由：返回之后网络线程不会再拿着旧模块指针调用，随后才能关闭模块、卸载 DLL
    {
        std::lock_guard<std::mutex> guard(m_routeMutex);
        m_routeTarget = NULL;
    }
    //还没送达的图片结果不再交给模块：先销毁消息窗口（顺带删除队列里的结果对象），再关闭模块
    DestroyMessageWindow();
    if (m_ready)
        m_loader.GetModule()->Shutdown();
    m_ready = false;
    m_moduleActive = false;
    m_loader.Unload();
    m_directory = NULL;
    LOG_INFO("thread group module unloaded.");
}

bool CThreadGroupHost::IsAvailable() const
{
    return m_loggedIn && m_moduleActive && IsThreadGroupAvailable(m_ready, m_featureEnabled);
}

bool CThreadGroupHost::OpenThreadWindow(int64_t groupId, int64_t postId)
{
    if (!IsAvailable() || groupId <= 0)
    {
        LOG_WARNING("thread window not opened, groupid=%lld: thread group unavailable.", static_cast<long long>(groupId));
        return false;
    }
    const bool ok = m_loader.GetModule()->OpenThreadWindow(groupId, postId);
    LOG_INFO("thread window open requested, groupid=%lld, postid=%lld, ok=%d.", static_cast<long long>(groupId),
             static_cast<long long>(postId), ok ? 1 : 0);
    return ok;
}

void CThreadGroupHost::CloseThreadWindow(int64_t groupId)
{
    if (!m_ready || !m_moduleActive)
        return;
    m_loader.GetModule()->CloseThreadWindow(groupId);
}

bool CThreadGroupHost::IsWatching(int64_t groupId) const
{
    if (!m_ready || !m_moduleActive)
        return false;
    return m_loader.GetModule()->IsWatching(groupId);
}

void CThreadGroupHost::OnGroupChanged(int64_t groupId)
{
    if (!m_ready || !m_moduleActive)
        return;
    m_loader.GetModule()->OnGroupChanged(groupId);
}

void CThreadGroupHost::OnGroupMembersArrived(const net::GroupMembersResult& result)
{
    std::set<int64_t>::iterator it = m_pendingGroupMembers.find(result.m_groupid);
    if (it == m_pendingGroupMembers.end())
        return;
    m_pendingGroupMembers.erase(it);
    if (!m_ready || !m_moduleActive)
        return;
    //每个人带群角色与显示名：群名片，没有时为昵称，与普通群 @ 的口径相同，不带本人给对方起的备注（ThreadMentionName）
    std::vector<threadgroup::ThreadMemberInfo> members;
    if (result.m_code == 0)
    {
        for (size_t i = 0; i < result.m_members.size(); ++i)
        {
            const net::GroupMemberItem& src = result.m_members[i];
            threadgroup::ThreadMemberInfo info;
            ::ZeroMemory(&info, sizeof(info));
            info.m_cbSize = sizeof(info);
            info.m_userId = src.m_userid;
            info.m_role = src.m_role;
            std::wstring name = ThreadMentionName(src.m_card, src.m_nickname);
            if (name.empty())
                name = TRF(kFallbackNameTemplate, static_cast<long long>(src.m_userid));
            CopyWide(name, info.m_name, threadgroup::kThreadNameChars);
            members.push_back(info);
        }
    }
    LOG_INFO("thread group members arrived, groupid=%lld, code=%d, count=%u.", static_cast<long long>(result.m_groupid),
             result.m_code, static_cast<unsigned>(members.size()));
    m_loader.GetModule()->OnGroupMembers(result.m_groupid, members.empty() ? NULL : &members[0],
                                         static_cast<uint32_t>(members.size()));
}

void CThreadGroupHost::OnAvatarsUpdated()
{
    if (!m_ready || !m_moduleActive)
        return;
    m_loader.GetModule()->OnAvatarsUpdated();
}

void CThreadGroupHost::OnServerFrame(uint16_t cmd, uint32_t seq, const std::string& body)
{
    //持锁转交：Shutdown 解除路由之后，网络线程不会再拿着旧模块指针调用
    std::lock_guard<std::mutex> guard(m_routeMutex);
    if (m_routeTarget == NULL)
    {
        LOG_WARNING("thread frame dropped, module not loaded, cmd=%u, seq=%u.", static_cast<unsigned>(cmd), seq);
        return;
    }
    //卡片改写的推送与核对应答只给 exe 用（改写本地消息记录），不转给模块
    if (InterceptCardFrame(cmd, body))
        return;
    m_routeTarget->OnServerFrame(cmd, seq, body.data(), static_cast<uint32_t>(body.size()));
}

namespace {

//网络线程投给界面线程的一批被改写的卡片（kMsgThreadHostCards 的 lParam）
struct ThreadCardBatch
{
    int64_t                       m_groupId;   //帖子群
    std::vector<ThreadCardUpdate> m_cards;     //改写后的卡片

    ThreadCardBatch()
        : m_groupId(0)
    {
    }
};

} // namespace

bool CThreadGroupHost::InterceptCardFrame(uint16_t cmd, const std::string& body)
{
    if (cmd != cmd_thread_cards_notify)
        return false;
    std::unique_ptr<ThreadCardBatch> batch(new ThreadCardBatch());
    if (!ParseThreadCards(body, batch->m_groupId, batch->m_cards))
    {
        LOG_WARNING("thread cards frame ignored, cmd=%u, bad body or error code.", static_cast<unsigned>(cmd));
        return true;
    }
    LOG_INFO("thread cards frame, cmd=%u, groupid=%lld, cards=%u.", static_cast<unsigned>(cmd),
             static_cast<long long>(batch->m_groupId), static_cast<unsigned>(batch->m_cards.size()));
    if (batch->m_cards.empty() || m_msgWnd == NULL)
        return true;
    if (::PostMessageW(m_msgWnd, kMsgThreadHostCards, 0, reinterpret_cast<LPARAM>(batch.get())))
        batch.release();
    return true;
}

void CThreadGroupHost::HandleCardBatch(LPARAM lParam)
{
    std::unique_ptr<ThreadCardBatch> batch(reinterpret_cast<ThreadCardBatch*>(lParam));
    if (!batch || m_directory == NULL)
        return;
    m_directory->OnThreadCardsChanged(batch->m_groupId, batch->m_cards);
}

bool CThreadGroupHost::EnsureModuleReady()
{
    if (m_loadAttempted)
        return m_ready;
    m_loadAttempted = true;
    //g_szHomePath 是程序目录，以反斜杠结尾；传完整路径，不依赖系统的 DLL 搜索顺序，避免加载到别处的同名 DLL
    CString dllPath(g_szHomePath);
    dllPath += FLAMINGO_THREADGROUP_DLL_NAME;
    balloonapp::PluginLoadResult result =
        m_loader.Load(dllPath, FLAMINGO_THREADGROUP_FACTORY_NAME, threadgroup::kApiVersion);
    if (result != balloonapp::plugin_load_ok)
    {
        LOG_WARNING("thread group module unavailable: %s, api version %u.",
                    balloonapp::CPluginLibrary::DescribeResult(result), static_cast<unsigned>(threadgroup::kApiVersion));
        return false;
    }
    //初始化参数里的程序目录不带结尾反斜杠
    CString appDir(g_szHomePath);
    appDir.TrimRight(_T('\\'));
    threadgroup::ThreadInitParams params;
    params.m_cbSize = sizeof(params);
    params.m_appDir = appDir;
    if (!m_loader.GetModule()->Init(this, &params))
    {
        LOG_ERROR("thread group module loaded but Init failed, unloading it.");
        m_loader.Unload();
        return false;
    }
    m_ready = true;
    {
        std::lock_guard<std::mutex> guard(m_routeMutex);
        m_routeTarget = m_loader.GetModule();
    }
    //图片上传、下载的结果投到这个窗口；建不出来时只是没有图片，帖子群其余功能照常
    if (!CreateMessageWindow())
        LOG_WARNING("thread host message window creation failed, error=%lu; images will not load.", ::GetLastError());
    LOG_INFO("thread group module loaded, api version %u.",
             static_cast<unsigned>(m_loader.GetModule()->GetApiVersion()));
    return true;
}

void CThreadGroupHost::ActivateModule()
{
    if (!m_ready)
        return;
    m_loader.GetModule()->OnLoggedIn();
    m_moduleActive = true;
}

void CThreadGroupHost::DeactivateModule()
{
    if (m_ready && m_moduleActive)
        m_loader.GetModule()->OnLoggingOut();
    m_moduleActive = false;
}

//======================== threadgroup::IThreadGroupHost ========================

void CThreadGroupHost::Log(int level, const char* text)
{
    if (text == NULL)
        return;
    long logLevel = LOG_LEVEL_INFO;
    switch (level)
    {
    //DLL 报告的警告
    case threadgroup::thread_log_warning:
        logLevel = LOG_LEVEL_WARNING;
        break;

    //DLL 报告的错误
    case threadgroup::thread_log_error:
        logLevel = LOG_LEVEL_ERROR;
        break;

    //普通信息及不认识的级别
    default:
        logLevel = LOG_LEVEL_INFO;
        break;
    }
    //行号一栏没有意义，固定写 0；正文作为参数传入，避免其中的 % 被当成格式符
    CIULog::Log(logLevel, kThreadLogSource, 0, "%s", text);
}

const wchar_t* CThreadGroupHost::Translate(const wchar_t* context, const wchar_t* source)
{
    return i18n::Translate(context, source);
}

bool CThreadGroupHost::GetUiLanguage(wchar_t* langBuf, uint32_t langChars, wchar_t* fontBuf, uint32_t fontChars)
{
    if (langBuf == NULL || langChars == 0 || fontBuf == NULL || fontChars == 0)
        return false;
    CopyWide(i18n::GetLanguage(), langBuf, langChars);
    CopyWide(i18n::GetUiFontFace(), fontBuf, fontChars);
    return true;
}

uint32_t CThreadGroupHost::SendThreadCommand(uint16_t cmd, const char* json, uint32_t len)
{
    if (json == NULL || len == 0)
        return 0;
    //放行规则（号段、包体非空、已连接）由 CIUSocket::SendThreadCommand 统一判定
    return CIUSocket::GetInstance().SendThreadCommand(cmd, std::string(json, len));
}

int64_t CThreadGroupHost::GetMyUserId()
{
    return m_myUserId;
}

bool CThreadGroupHost::GetUserDisplayName(int64_t userId, int64_t groupId, wchar_t* buf, uint32_t chars)
{
    if (buf == NULL || chars == 0 || userId <= 0)
        return false;
    std::wstring name;
    if (m_directory != NULL)
        name = m_directory->GetThreadUserDisplayName(userId, groupId);
    if (name.empty())
        name = TRF(kFallbackNameTemplate, static_cast<long long>(userId));
    CopyWide(name, buf, chars);
    return true;
}

bool CThreadGroupHost::GetUserAvatar(int64_t userId, threadgroup::ThreadAvatarInfo* out)
{
    if (userId <= 0 || out == NULL || out->m_cbSize != sizeof(threadgroup::ThreadAvatarInfo))
        return false;
    ThreadUserAvatar avatar;
    if (m_directory != NULL)
        avatar = m_directory->GetThreadUserAvatar(userId);
    if (avatar.m_glyph.empty())
        avatar.m_glyph = L"?";
    CopyWide(avatar.m_glyph, out->m_glyph, threadgroup::kThreadGlyphChars);
    out->m_color = avatar.m_color;
    //照片路径放不下时整体留空、改画字形，不截断（截断后的路径指向的是另一个文件）
    if (avatar.m_imagePath.size() < threadgroup::kThreadPathChars)
        CopyWide(avatar.m_imagePath, out->m_imagePath, threadgroup::kThreadPathChars);
    else
        out->m_imagePath[0] = L'\0';
    return true;
}

bool CThreadGroupHost::GetGroupBrief(int64_t groupId, threadgroup::ThreadGroupBrief* out)
{
    if (groupId <= 0 || out == NULL || out->m_cbSize != sizeof(threadgroup::ThreadGroupBrief))
        return false;
    ThreadGroupBriefInfo info;
    if (m_directory == NULL || !m_directory->GetThreadGroupBrief(groupId, info))
    {
        //本人不在这个群里（或还没登记主面板）：角色按「不是成员」回答
        out->m_name[0] = L'\0';
        out->m_memberCount = 0;
        out->m_myRole = kRoleNotMember;
        out->m_mutedForMe = true;
        out->m_atAllScope = 0;
        return false;
    }
    CopyWide(info.m_name, out->m_name, threadgroup::kThreadNameChars);
    out->m_memberCount = info.m_memberCount;
    out->m_myRole = info.m_myRole;
    out->m_mutedForMe = info.m_mutedForMe;
    out->m_atAllScope = info.m_atAllScope;
    return true;
}

void CThreadGroupHost::RequestGroupMembers(int64_t groupId)
{
    if (!m_loggedIn || groupId <= 0)
        return;
    //发 1304，应答到达主面板后经 OnGroupMembersArrived 转回来
    m_pendingGroupMembers.insert(groupId);
    CIUSocket::GetInstance().RequestGroupMembers(groupId);
    LOG_INFO("thread group members requested, groupid=%lld.", static_cast<long long>(groupId));
}

void CThreadGroupHost::OpenGroupInfo(HWND owner, int64_t groupId)
{
    if (m_directory != NULL && groupId > 0)
        m_directory->OpenThreadGroupInfo(owner, groupId);
}

void CThreadGroupHost::OpenGroupManage(HWND owner, int64_t groupId)
{
    if (m_directory != NULL && groupId > 0)
        m_directory->OpenThreadGroupManage(owner, groupId);
}

void CThreadGroupHost::ShowMemberMenu(HWND owner, int64_t groupId, int64_t userId, int targetRole, int screenX,
                                      int screenY)
{
    if (m_directory == NULL || groupId <= 0 || userId <= 0)
        return;
    POINT pt;
    pt.x = screenX;
    pt.y = screenY;
    m_directory->ShowThreadMemberMenu(owner, groupId, userId, targetRole, pt);
}

void CThreadGroupHost::OpenContactCard(HWND owner, int64_t userId)
{
    if (m_directory != NULL && userId > 0)
        m_directory->OpenThreadContactCard(owner, userId);
}

bool CThreadGroupHost::UploadImage(uint32_t requestId, const wchar_t* localPath)
{
    if (!m_loggedIn || m_msgWnd == NULL || localPath == NULL || localPath[0] == L'\0')
    {
        LOG_WARNING("thread image upload refused, request=%u, logged in %d.", static_cast<unsigned>(requestId),
                    m_loggedIn ? 1 : 0);
        return false;
    }
    //经聊天图片的上传通道（imgserver）上传，不发消息；结果投到本对象的消息窗口
    net::ImageTransfer::GetInstance().EnqueuePlainUpload(requestId, std::wstring(localPath), m_msgWnd,
                                                         kMsgThreadHostImageUploaded);
    return true;
}

bool CThreadGroupHost::DownloadImage(uint32_t requestId, const char* fileId, const char* md5)
{
    if (!m_loggedIn || m_msgWnd == NULL || fileId == NULL || fileId[0] == '\0')
    {
        LOG_WARNING("thread image download refused, request=%u, logged in %d.", static_cast<unsigned>(requestId),
                    m_loggedIn ? 1 : 0);
        return false;
    }
    //按 fileid 下载到聊天图片缓存（md5 已知且已缓存时不下载）；结果投到本对象的消息窗口
    net::ImageTransfer::GetInstance().EnqueuePlainDownload(requestId, std::string(fileId),
                                                           std::string(md5 != NULL ? md5 : ""), m_msgWnd,
                                                           kMsgThreadHostImageDownloaded);
    return true;
}

bool CThreadGroupHost::CreateMessageWindow()
{
    if (m_msgWnd != NULL)
        return true;
    HINSTANCE inst = ::GetModuleHandleW(NULL);
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &CThreadGroupHost::MessageWndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kMsgWndClass;
    //重复注册（退出登录后又登录）返回失败但类仍可用，故不检查返回值
    ::RegisterClassExW(&wc);
    m_msgWnd = ::CreateWindowExW(0, kMsgWndClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, inst, NULL);
    if (m_msgWnd == NULL)
        return false;
    ::SetWindowLongPtrW(m_msgWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    return true;
}

void CThreadGroupHost::DestroyMessageWindow()
{
    if (m_msgWnd == NULL)
        return;
    //删除还在队列里、没送达的结果对象
    MSG msg;
    while (::PeekMessageW(&msg, m_msgWnd, kMsgThreadHostImageDownloaded, kMsgThreadHostImageUploaded, PM_REMOVE))
        delete reinterpret_cast<net::PlainTransferResult*>(msg.lParam);
    while (::PeekMessageW(&msg, m_msgWnd, kMsgThreadHostCards, kMsgThreadHostCards, PM_REMOVE))
        delete reinterpret_cast<ThreadCardBatch*>(msg.lParam);
    ::DestroyWindow(m_msgWnd);
    m_msgWnd = NULL;
}

LRESULT CALLBACK CThreadGroupHost::MessageWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    CThreadGroupHost* self = reinterpret_cast<CThreadGroupHost*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == kMsgThreadHostImageDownloaded || msg == kMsgThreadHostImageUploaded)
    {
        if (self != NULL)
            self->HandleImageResult(msg, lParam);
        else
            delete reinterpret_cast<net::PlainTransferResult*>(lParam);
        return 0;
    }
    if (msg == kMsgThreadHostCards)
    {
        if (self != NULL)
            self->HandleCardBatch(lParam);
        else
            delete reinterpret_cast<ThreadCardBatch*>(lParam);
        return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

void CThreadGroupHost::HandleImageResult(UINT msg, LPARAM lParam)
{
    std::unique_ptr<net::PlainTransferResult> result(reinterpret_cast<net::PlainTransferResult*>(lParam));
    if (!result || !m_ready || m_loader.GetModule() == NULL)
        return;
    threadgroup::IThreadGroupModule* module = m_loader.GetModule();
    if (msg == kMsgThreadHostImageDownloaded)
    {
        if (!result->m_ok)
            LOG_WARNING("thread image download failed, request=%u, code=%d.", static_cast<unsigned>(result->m_requestId),
                        result->m_errcode);
        module->OnImageReady(result->m_requestId, result->m_ok, result->m_localpath.c_str());
        return;
    }
    if (!result->m_ok)
        LOG_WARNING("thread image upload failed, request=%u, code=%d.", static_cast<unsigned>(result->m_requestId),
                    result->m_errcode);
    module->OnImageUploaded(result->m_requestId, result->m_ok, result->m_fileid.c_str(), result->m_md5.c_str(),
                            static_cast<uint32_t>(result->m_width), static_cast<uint32_t>(result->m_height),
                            result->m_size, result->m_localpath.c_str());
}

void CThreadGroupHost::OpenImageViewer(HWND owner, const wchar_t* const* paths, uint32_t count, uint32_t index)
{
    if (paths == NULL || count == 0)
        return;
    m_viewerPaths.clear();
    for (uint32_t i = 0; i < count; ++i)
        m_viewerPaths.push_back((paths[i] != NULL) ? std::wstring(paths[i]) : std::wstring());
    m_viewerIndex = (index < count) ? index : 0;
    m_viewerOwner = (owner != NULL && ::IsWindow(owner)) ? owner : NULL;
    ShowViewerImage();
}

void CThreadGroupHost::OnImageViewerNavigate(int delta)
{
    if (m_viewerPaths.empty())
        return;
    //到头或到尾时停在原处，不循环
    if (delta < 0 && m_viewerIndex > 0)
        --m_viewerIndex;
    else if (delta > 0 && m_viewerIndex + 1 < m_viewerPaths.size())
        ++m_viewerIndex;
    else
        return;
    ShowViewerImage();
}

void CThreadGroupHost::ShowViewerImage()
{
    if (m_viewerIndex >= m_viewerPaths.size() || m_viewerPaths[m_viewerIndex].empty())
        return;
    //只有一张时不需要翻页，不登记翻页回调
    IImageViewerNavigator* navigator = (m_viewerPaths.size() > 1) ? this : NULL;
    CBalloonImageViewerWindow::OpenSharedWithNavigator(m_viewerOwner, m_viewerPaths[m_viewerIndex].c_str(), navigator);
}

uint32_t CThreadGroupHost::GetFaceCount()
{
    EnsureFaces();
    return static_cast<uint32_t>(m_faces->m_arrFaceInfo.size());
}

bool CThreadGroupHost::GetFaceInfo(uint32_t index, threadgroup::ThreadFaceInfo* out)
{
    if (out == NULL || out->m_cbSize != sizeof(threadgroup::ThreadFaceInfo))
        return false;
    EnsureFaces();
    if (index >= m_faces->m_arrFaceInfo.size() || m_faces->m_arrFaceInfo[index] == NULL)
        return false;
    const CFaceInfo* info = m_faces->m_arrFaceInfo[index];
    out->m_faceId = info->m_nId;
    std::wstring name = info->m_strTip;
    if (!name.empty() && name[0] == kFaceTipPrefix)
        name.erase(0, 1);
    CopyWide(name, out->m_name, threadgroup::kThreadNameChars);
    CopyWide(info->m_strFileName, out->m_path, threadgroup::kThreadPathChars);
    return true;
}

void CThreadGroupHost::EnsureFaces()
{
    if (m_faces)
        return;
    //只加载一次：配置文件读不到时留一个空表情库，不再重试
    m_faces.reset(new CFaceList());
    const tstring config = Hootina::CPath::GetAppPath() + kFaceConfigRelativePath;
    if (!m_faces->LoadConfigFile(config.c_str()))
        LOG_WARNING("thread group face library not loaded, faces will show as text.");
    else
        LOG_INFO("thread group face library loaded, %u face(s).", static_cast<unsigned>(m_faces->m_arrFaceInfo.size()));
}

void CThreadGroupHost::MarkGroupRead(int64_t groupId)
{
    if (m_directory != NULL && groupId > 0)
        m_directory->MarkThreadGroupRead(groupId);
}

void CThreadGroupHost::OnWatchChanged(int64_t groupId, bool watching)
{
    if (m_directory != NULL && groupId > 0)
        m_directory->OnThreadWatchChanged(groupId, watching);
}

bool CThreadGroupHost::ShowMemberPanel(HWND parent, int64_t groupId, const RECT* rc)
{
    if (m_directory == NULL || groupId <= 0 || rc == NULL || !::IsWindow(parent))
    {
        LOG_WARNING("thread member panel: show rejected, groupid=%lld.", static_cast<long long>(groupId));
        return false;
    }
    return m_directory->ShowThreadMemberPanel(parent, groupId, *rc);
}

void CThreadGroupHost::MoveMemberPanel(int64_t groupId, const RECT* rc)
{
    if (m_directory != NULL && groupId > 0 && rc != NULL)
        m_directory->MoveThreadMemberPanel(groupId, *rc);
}

void CThreadGroupHost::HideMemberPanel(int64_t groupId)
{
    if (m_directory != NULL && groupId > 0)
        m_directory->HideThreadMemberPanel(groupId);
}

void CThreadGroupHost::OnMemberPanelClosed(int64_t groupId)
{
    if (!m_ready || !m_moduleActive)
        return;
    m_loader.GetModule()->OnMemberPanelClosed(groupId);
}

} // namespace balloonthread
