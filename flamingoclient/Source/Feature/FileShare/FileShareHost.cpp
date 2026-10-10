/**
 *  主程序一侧的文件共享接入 CFileShareHost 的实现，见 FileShareHost.h。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "FileShareHost.h"
#include "FileShareGate.h"

#include "../../Common/Log/IULog.h"
#include "../../Session/UserSessionData.h"   // g_szHomePath（程序目录，DLL 与 exe 同目录）
#include "../../net/IUSocket.h"              // CIUSocket::GetServerPublicKey / GetOrCreateDeviceId
#include "../../Common/Util/EncodeUtil.h"    // 账号、昵称转 UTF-8
#include "../../net/GroupProtocolClient.h"   // net::GroupMembersResult
#include "../../net/Protocol.h"              // conversation_single / conversation_group
#include "../../Common/I18n/I18n.h"   // i18n::Translate / GetLanguage / GetUiFontFace：宿主接口的多语言部分
#include "../P2P/P2PHost.h"            // CP2PHost::GetService：交给文件共享 DLL 的 P2P 服务
#include "../Lock/LockHost.h"          // IsScreenLocked：锁屏期间文件共享的弹窗通知推迟
#include "../Lock/LockSuppressPolicy.h"   // DecideDuringLock
#include "../../UI/BalloonMain/NotificationSound.h"   // PlayNotifySound：文件共享弹窗通知的提示音
#include "ShareNoticePolicy.h"         // 勿扰与锁屏的判定

#include <string.h>   // memcpy / wcsncpy_s

namespace balloonfileshare {

namespace {

//DLL 写来的日志在日志行「函数签名」一栏显示的名字，与 FlamingoMedia 的做法一致，便于按模块筛选。
//DLL 已在正文前加了 "[share] [调用处:行号] "，故这里不再重复调用处
const char* const kFileShareLogSource = "FlamingoFileShare";

} // namespace

CFileShareHost& CFileShareHost::GetInstance()
{
    static CFileShareHost s_instance;
    return s_instance;
}

CFileShareHost::CFileShareHost()
    : m_loadAttempted(false)
    , m_ready(false)
    , m_loggedIn(false)
    , m_featureEnabled(false)
    , m_moduleActive(false)
    , m_directory(NULL)
{
}

CFileShareHost::~CFileShareHost()
{
}

void CFileShareHost::OnLoginSucceeded(const FileShareLoginInfo& login)
{
    //先记下账号快照与设备标识：模块在 OnLoggedIn 里就会来取
    m_login = login;
    m_deviceId = CIUSocket::GetInstance().GetOrCreateDeviceId();
    m_loggedIn = true;

    //本企业的文件共享被关闭：不加载、不激活，三处入口不出现；开关打开时（1013）再激活
    if (!m_featureEnabled)
    {
        LOG_INFO("file share is disabled for this enterprise, module not activated.");
        return;
    }
    if (!EnsureModuleReady())
        return;

    //每次登录成功（含断线重连）都通知：模块据此重新拉取「我的共享」并上报可提供状态。
    //断线重连时模块自己重新拉取登记过的会话；首次进入已登录状态时模块还不知道它们，由这里转过去
    const bool wasActive = m_moduleActive;
    m_loader.GetModule()->OnLoggedIn();
    m_moduleActive = true;
    if (!wasActive)
        ForwardWatchedConversations();
}

void CFileShareHost::OnLoggingOut()
{
    m_loggedIn = false;
    m_pendingGroupMembers.clear();
    if (!m_ready || !m_moduleActive)
        return;

    m_loader.GetModule()->OnLoggingOut();
    m_moduleActive = false;
    ClearWatchedBadges();
}

void CFileShareHost::SetFeatureEnabled(bool enabled)
{
    m_featureEnabled = enabled;
}

bool CFileShareHost::OnFeaturesChanged(bool enabled)
{
    const bool before = IsModuleReady();
    m_featureEnabled = enabled;
    switch (FileShareGateAction(m_loggedIn, m_featureEnabled, m_moduleActive))
    {
    //开关由关变开（或模块尚未激活）：必要时先加载 DLL，再让模块进入已登录状态
    case file_share_gate_activate:
        if (EnsureModuleReady())
        {
            m_loader.GetModule()->OnLoggedIn();
            m_moduleActive = true;
            ForwardWatchedConversations();
        }
        break;

    //开关由开变关：模块按退出登录收尾（关闭全部窗口、结束会话），DLL 保持加载
    case file_share_gate_deactivate:
        if (m_ready)
            m_loader.GetModule()->OnLoggingOut();
        m_moduleActive = false;
        ClearWatchedBadges();
        break;

    //没有需要做的：开关没变，或尚未登录
    default:
        break;
    }
    const bool after = IsModuleReady();
    LOG_INFO("file share feature switch changed: enabled=%d, module ready %d -> %d.", enabled ? 1 : 0, before ? 1 : 0,
             after ? 1 : 0);
    return before != after;
}

bool CFileShareHost::IsFeatureDisabled() const
{
    return m_loggedIn && !m_featureEnabled;
}

void CFileShareHost::Shutdown()
{
    if (!m_loader.IsLoaded())
        return;

    //先解除帧路由的挂接：返回之后网络线程不会再调用模块，随后才能关闭模块、卸载 DLL
    m_router.Detach();
    if (m_ready)
        m_loader.GetModule()->Shutdown();
    m_ready = false;
    m_loader.Unload();
    LOG_INFO("file share module unloaded.");
}

bool CFileShareHost::IsModuleReady() const
{
    return m_ready && m_featureEnabled;
}

bool CFileShareHost::EnsureModuleReady()
{
    if (m_loadAttempted)
        return m_ready;
    m_loadAttempted = true;

    //g_szHomePath 是程序目录，以反斜杠结尾；DLL 与 exe 放在同一目录。传完整路径，
    //不依赖系统的 DLL 搜索顺序，避免加载到别处的同名 DLL
    CString dllPath(g_szHomePath);
    dllPath += FLAMINGO_FILESHARE_DLL_NAME;
    balloonapp::PluginLoadResult result = m_loader.Load(dllPath, FLAMINGO_FILESHARE_FACTORY_NAME,
                                                        fileshare::kApiVersion);
    if (result != balloonapp::plugin_load_ok)
    {
        LOG_WARNING("file share module unavailable: %s, api version %u.",
                    balloonapp::CPluginLibrary::DescribeResult(result),
                    static_cast<unsigned>(fileshare::kApiVersion));
        return false;
    }

    //初始化参数里的程序目录不带结尾反斜杠
    CString appDir(g_szHomePath);
    appDir.TrimRight(_T('\\'));
    fileshare::FileShareInitParams params;
    params.m_cbSize = sizeof(params);
    params.m_appDir = appDir;
    if (!m_loader.GetModule()->Init(this, &params))
    {
        LOG_ERROR("file share module loaded but Init failed, unloading it.");
        m_loader.Unload();
        return false;
    }

    m_ready = true;
    m_router.Attach(m_loader.GetModule());
    LOG_INFO("file share module loaded, api version %u.",
             static_cast<unsigned>(m_loader.GetModule()->GetApiVersion()));
    return true;
}

void CFileShareHost::Log(int level, const char* text)
{
    if (text == NULL)
        return;

    long logLevel = LOG_LEVEL_INFO;
    switch (level)
    {
    //DLL 报告的警告
    case fileshare::fileshare_log_warning:
        logLevel = LOG_LEVEL_WARNING;
        break;
    //DLL 报告的错误
    case fileshare::fileshare_log_error:
        logLevel = LOG_LEVEL_ERROR;
        break;
    //普通信息及不认识的级别
    default:
        logLevel = LOG_LEVEL_INFO;
        break;
    }
    //行号一栏没有意义，固定写 0；正文作为参数传入，避免其中的 % 被当成格式符
    CIULog::Log(logLevel, kFileShareLogSource, 0, "%s", text);
}

uint32_t CFileShareHost::SendShareCommand(uint16_t cmd, const char* json, uint32_t len)
{
    if (json == NULL && len != 0)
    {
        LOG_WARNING("file share command %u rejected: null body with length %u.", static_cast<unsigned>(cmd),
                    static_cast<unsigned>(len));
        return 0;
    }
    //放行规则（号段、包体非空、已连接）由 CIUSocket::SendShareCommand 统一判定
    return CIUSocket::GetInstance().SendShareCommand(cmd, std::string(json != NULL ? json : "", len));
}

void CFileShareHost::OnServerFrame(uint16_t cmd, uint32_t seq, const std::string& body)
{
    if (m_router.Route(cmd, seq, body.data(), static_cast<uint32_t>(body.size())))
        return;
    LOG_WARNING("file share frame dropped, cmd=%u, seq=%u, len=%u: module not loaded or frame rejected.",
                static_cast<unsigned>(cmd), seq, static_cast<unsigned>(body.size()));
}

const unsigned char* CFileShareHost::GetServerPublicKey()
{
    return CIUSocket::GetInstance().GetServerPublicKey();
}

bool CFileShareHost::GetSessionInfo(fileshare::FileShareSessionInfo* info)
{
    if (info == NULL || info->m_cbSize != sizeof(fileshare::FileShareSessionInfo))
    {
        LOG_WARNING("file share GetSessionInfo rejected: null or size mismatch.");
        return false;
    }
    if (!m_loggedIn)
        return false;

    const std::string account = EncodeUtil::UnicodeToUtf8(m_login.m_account);
    const std::string nickName = EncodeUtil::UnicodeToUtf8(m_login.m_nickName);
    //各字段连同结尾的 '\0' 都要写得下，写不下时判失败，不截断（截断的个人目录会让 DLL 把库建到别处）
    if (account.size() >= fileshare::kFileShareTextBytes || nickName.size() >= fileshare::kFileShareTextBytes
        || m_deviceId.size() >= fileshare::kFileShareTextBytes
        || m_login.m_personalDir.size() >= fileshare::kFileSharePathChars)
    {
        LOG_WARNING("file share GetSessionInfo failed: a field exceeds its capacity.");
        return false;
    }

    info->m_userId = m_login.m_userId;
    memcpy(info->m_account, account.c_str(), account.size() + 1);
    memcpy(info->m_nickName, nickName.c_str(), nickName.size() + 1);
    memcpy(info->m_deviceId, m_deviceId.c_str(), m_deviceId.size() + 1);
    wcsncpy_s(info->m_personalDir, fileshare::kFileSharePathChars, m_login.m_personalDir.c_str(), _TRUNCATE);
    return true;
}

void CFileShareHost::SetDirectory(IFileShareDirectory* directory)
{
    m_directory = directory;
}

bool CFileShareHost::RequestConversationMembers(int ctype, int64_t cid)
{
    if (!m_loggedIn || !m_ready || cid <= 0)
        return false;

    //单聊：成员就是对方一人，不用向服务端要，直接回送
    if (ctype == conversation_single)
    {
        const fileshare::FileShareMemberInfo peer = BuildPeerMemberInfo(cid, m_directory);
        LOG_INFO("file share members: single chat peer returned directly, peer=%lld.", static_cast<long long>(cid));
        m_loader.GetModule()->OnConversationMembers(ctype, cid, 0, &peer, 1);
        return true;
    }
    if (ctype != conversation_group)
        return false;

    //群聊：发 1304，应答到达主面板后经 OnGroupMembersArrived 转回来
    m_pendingGroupMembers.insert(cid);
    CIUSocket::GetInstance().RequestGroupMembers(cid);
    LOG_INFO("file share members: group members requested, groupid=%lld.", static_cast<long long>(cid));
    return true;
}

void CFileShareHost::OnGroupMembersArrived(const net::GroupMembersResult& result)
{
    std::set<int64_t>::iterator it = m_pendingGroupMembers.find(result.m_groupid);
    if (it == m_pendingGroupMembers.end())
        return;
    m_pendingGroupMembers.erase(it);
    if (!m_ready)
        return;

    const std::vector<fileshare::FileShareMemberInfo> infos =
        (result.m_code == 0) ? BuildGroupMemberInfos(result, m_directory) : std::vector<fileshare::FileShareMemberInfo>();
    LOG_INFO("file share members: group members arrived, groupid=%lld, code=%d, count=%u.",
             static_cast<long long>(result.m_groupid), result.m_code, static_cast<unsigned>(infos.size()));
    m_loader.GetModule()->OnConversationMembers(conversation_group, result.m_groupid, result.m_code,
                                                infos.empty() ? NULL : &infos[0], static_cast<uint32_t>(infos.size()));
}

bool CFileShareHost::GetUserDisplayName(int64_t userId, char* buf, uint32_t bufBytes)
{
    if (buf == NULL || bufBytes == 0 || userId <= 0)
        return false;
    const fileshare::FileShareMemberInfo info = BuildPeerMemberInfo(userId, m_directory);
    CopyUtf8Truncated(info.m_name, buf, bufBytes);
    return true;
}

void CFileShareHost::NotifyShareState(int64_t shareId, int state, int reason, int myPerms)
{
    LOG_INFO("file share card state changed, shareid=%lld, state=%d, reason=%d.", static_cast<long long>(shareId), state,
             reason);
    if (m_directory != NULL)
        m_directory->OnShareCardStateChanged(shareId, state, reason, myPerms);
}

bool CFileShareHost::OpenCreateShare(HWND owner, int ctype, int64_t cid)
{
    if (!m_ready || !m_loggedIn || !m_moduleActive)
        return false;
    const bool ok = m_loader.GetModule()->OpenCreateShare(owner, ctype, cid);
    LOG_INFO("file share create dialog requested from menu, ctype=%d, cid=%lld, ok=%d.", ctype,
             static_cast<long long>(cid), ok ? 1 : 0);
    return ok;
}

bool CFileShareHost::StartDirectShareTo(int64_t userId)
{
    if (!m_ready || !m_loggedIn || !m_moduleActive || userId <= 0)
        return false;
    LOG_INFO("file share direct share to one contact from menu, userid=%lld.", static_cast<long long>(userId));
    OnMembersPicked(std::vector<int64_t>(1, userId));
    return true;
}

bool CFileShareHost::PickShareTargets(HWND owner)
{
    if (m_directory == NULL || !m_loggedIn)
        return false;
    const bool ok = m_directory->OpenShareTargetPicker(owner);
    LOG_INFO("file share target picker requested, ok=%d.", ok ? 1 : 0);
    return ok;
}

bool CFileShareHost::CreateShareGroup(const char* groupName, const int64_t* memberIds, uint32_t count)
{
    //多人共享先建群：至少两名成员（加上本人三人）
    const uint32_t kMinGroupMembers = 2;
    if (m_directory == NULL || !m_loggedIn || groupName == NULL || memberIds == NULL || count < kMinGroupMembers)
        return false;
    const std::vector<int64_t> ids(memberIds, memberIds + count);
    const bool ok = m_directory->CreateShareGroup(groupName, ids);
    LOG_INFO("file share group requested, members=%u, ok=%d.", count, ok ? 1 : 0);
    return ok;
}

void CFileShareHost::OnShareGroupCreated(int code, int64_t groupId)
{
    LOG_INFO("file share group created, code=%d, groupid=%lld.", code, static_cast<long long>(groupId));
    if (!m_ready || !m_moduleActive)
        return;
    m_loader.GetModule()->OnShareGroupCreated(code, groupId);
}

void CFileShareHost::OnAvatarsUpdated()
{
    if (!m_ready || !m_moduleActive)
        return;
    m_loader.GetModule()->OnAvatarsUpdated();
}

bool CFileShareHost::GetUserAvatar(int64_t userId, fileshare::FileShareAvatarInfo* out)
{
    if (userId <= 0 || out == NULL)
        return false;
    ShareUserAvatar avatar;
    if (m_directory != NULL)
    {
        avatar = m_directory->GetShareUserAvatar(userId);
    }
    else
    {
        const fileshare::FileShareMemberInfo info = BuildPeerMemberInfo(userId, NULL);
        avatar = DefaultShareUserAvatar(userId, EncodeUtil::Utf8ToUnicode(std::string(info.m_name)));
    }
    return FillAvatarInfo(avatar, out);
}

void CFileShareHost::OnMembersPicked(const std::vector<int64_t>& userIds)
{
    if (!m_ready || !m_moduleActive || userIds.empty())
        return;
    //带上显示名：发起共享对话框的成员表按它显示
    std::vector<fileshare::FileShareMemberInfo> targets;
    for (size_t i = 0; i < userIds.size(); ++i)
    {
        fileshare::FileShareMemberInfo info;
        memset(&info, 0, sizeof(info));
        info.m_userId = userIds[i];
        char name[fileshare::kFileShareTextBytes] = { 0 };
        if (GetUserDisplayName(userIds[i], name, fileshare::kFileShareTextBytes))
            strcpy_s(info.m_name, fileshare::kFileShareTextBytes, name);
        targets.push_back(info);
    }
    LOG_INFO("file share targets picked, count=%u.", (unsigned)targets.size());
    m_loader.GetModule()->OnShareTargetsPicked(&targets[0], static_cast<uint32_t>(targets.size()));
}

void CFileShareHost::NotifyConversationShareCount(int ctype, int64_t cid, int count)
{
    LOG_INFO("file share available count changed, ctype=%d, cid=%lld, count=%d.", ctype, static_cast<long long>(cid), count);
    if (m_directory != NULL)
        m_directory->OnConversationShareCount(ctype, cid, count);
}

bool CFileShareHost::OpenShareCenter(HWND owner)
{
    //模块没有处于已登录状态（开关关着、或尚未登录）时入口不可用
    if (!m_ready || !m_moduleActive)
        return false;
    return m_loader.GetModule()->OpenShareCenter(owner);
}

bool CFileShareHost::OpenConversationShares(HWND owner, int ctype, int64_t cid, const RECT* anchor)
{
    if (!m_ready || !m_loggedIn || !m_moduleActive)
        return false;
    return m_loader.GetModule()->OpenConversationShares(owner, ctype, cid, anchor);
}

bool CFileShareHost::OpenShareFromCard(HWND owner, int64_t shareId)
{
    if (!m_ready || !m_loggedIn || !m_moduleActive)
        return false;
    return m_loader.GetModule()->OpenShareFromCard(owner, shareId);
}

void CFileShareHost::WatchConversationShares(int ctype, int64_t cid)
{
    if (cid <= 0)
        return;
    m_watchedConvs.insert(std::make_pair(ctype, cid));
    //模块没有处于已登录状态时只记下，进入已登录状态后由 ForwardWatchedConversations 转过去
    if (!m_ready || !m_loggedIn || !m_moduleActive)
        return;
    m_loader.GetModule()->WatchConversationShares(ctype, cid);
}

void CFileShareHost::UnwatchConversationShares(int ctype, int64_t cid)
{
    m_watchedConvs.erase(std::make_pair(ctype, cid));
    if (!m_ready)
        return;
    m_loader.GetModule()->UnwatchConversationShares(ctype, cid);
}

bool CFileShareHost::OpenOwnShareLocally(HWND owner, int64_t shareId)
{
    if (!m_ready || !m_loggedIn || !m_moduleActive)
        return false;
    return m_loader.GetModule()->OpenOwnShareLocally(owner, shareId);
}

void CFileShareHost::ForwardWatchedConversations()
{
    if (!m_ready || !m_moduleActive)
        return;
    for (std::set<std::pair<int, int64_t> >::const_iterator it = m_watchedConvs.begin(); it != m_watchedConvs.end(); ++it)
        m_loader.GetModule()->WatchConversationShares(it->first, it->second);
    LOG_INFO("file share watched conversations forwarded to module, count=%u.", (unsigned)m_watchedConvs.size());
}

void CFileShareHost::ClearWatchedBadges()
{
    if (m_directory == NULL)
        return;
    for (std::set<std::pair<int, int64_t> >::const_iterator it = m_watchedConvs.begin(); it != m_watchedConvs.end(); ++it)
        m_directory->OnConversationShareCount(it->first, it->second, 0);
}

bool CFileShareHost::GetShareState(int64_t shareId, int& state, int& reason)
{
    state = 0;
    reason = 0;
    if (!m_ready)
        return false;
    fileshare::FileShareCardState out;
    memset(&out, 0, sizeof(out));
    out.m_cbSize = sizeof(out);
    if (!m_loader.GetModule()->GetShareState(shareId, &out))
        return false;
    state = out.m_state;
    reason = out.m_reason;
    return true;
}

bool CFileShareHost::GetConversationName(int ctype, int64_t cid, char* buf, uint32_t bufBytes)
{
    if (buf == NULL || bufBytes == 0 || cid <= 0 || m_directory == NULL)
        return false;
    const std::wstring name = m_directory->GetShareConversationName(ctype, cid);
    if (name.empty())
        return false;
    CopyUtf8Truncated(EncodeUtil::UnicodeToUtf8(name), buf, bufBytes);
    return true;
}

bool CFileShareHost::OpenImageViewer(HWND owner, const wchar_t* imagePath, bool navigable)
{
    if (imagePath == NULL || imagePath[0] == L'\0')
        return false;
    HWND parent = (owner != NULL && ::IsWindow(owner)) ? owner : NULL;
    CBalloonImageViewerWindow::OpenSharedWithNavigator(parent, imagePath, navigable ? this : NULL);
    return true;
}

void CFileShareHost::OnImageViewerNavigate(int delta)
{
    fileshare::IFileShareModule* module = m_loader.GetModule();
    if (module != NULL && m_ready)
        module->NavigateImagePreview(delta);
}

bool CFileShareHost::OpenConversation(int ctype, int64_t cid)
{
    if (!m_loggedIn || cid <= 0 || m_directory == NULL)
        return false;
    LOG_INFO("file share asks to open conversation, ctype=%d, cid=%lld.", ctype, static_cast<long long>(cid));
    m_directory->OpenShareConversation(ctype, cid);
    return true;
}

const wchar_t* CFileShareHost::Translate(const wchar_t* context, const wchar_t* source)
{
    return i18n::Translate(context, source);
}

bool CFileShareHost::GetUiLanguage(wchar_t* langBuf, uint32_t langChars, wchar_t* fontBuf, uint32_t fontChars)
{
    if (langBuf == NULL || langChars == 0 || fontBuf == NULL || fontChars == 0)
        return false;
    //写不下时截断，结果始终以 '\0' 结尾
    wcsncpy_s(langBuf, langChars, i18n::GetLanguage().c_str(), _TRUNCATE);
    wcsncpy_s(fontBuf, fontChars, i18n::GetUiFontFace().c_str(), _TRUNCATE);
    return true;
}

p2p::IP2PService* CFileShareHost::GetP2PService()
{
    //P2P 模块在程序启动时加载、在文件共享模块关闭之后才关闭，交出去的服务对象在 DLL 的 Shutdown 之前一直有效
    return balloonp2p::CP2PHost::GetInstance().GetService();
}

int CFileShareHost::QueryNoticePolicy()
{
    //勿扰开关取主面板的用户配置；没有登记主面板（单元测试、登录前）时按没开
    const bool dnd = (m_directory != NULL) && m_directory->IsDoNotDisturbEnabled();
    SYSTEMTIME now;
    ::GetLocalTime(&now);
    const int lockAction = balloonlock::DecideDuringLock(balloonlock::kLockEvtFileShareNotice, balloonlock::IsScreenLocked());
    const int policy = DecideShareNoticePolicy(dnd, static_cast<int>(now.wHour), lockAction);
    if (policy != fileshare::fileshare_notice_show)
        LOG_INFO("file share notice policy: %d (dnd %d, hour %d, lock action %d).", policy, dnd ? 1 : 0,
                 static_cast<int>(now.wHour), lockAction);
    return policy;
}

void CFileShareHost::PlayNoticeSound()
{
    //与新消息提示音同一个声音与开关；锁屏期间由提示音的全局拦截判定压下
    const bool muted = (m_directory != NULL) && m_directory->IsShareNoticeMuted();
    balloonmain::PlayNotifySound(balloonmain::kNotifySoundNewMessage, muted);
}

uint32_t CFileShareHost::GetActiveTransferCount()
{
    if (!m_ready || !m_moduleActive || m_loader.GetModule() == NULL)
        return 0;
    return m_loader.GetModule()->GetActiveTransferCount();
}

} // namespace balloonfileshare
