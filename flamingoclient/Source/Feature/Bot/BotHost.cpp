/**
 *  主程序一侧的机器人管理界面接入 CBotHost 的实现，见 BotHost.h。
 *  balloonwj@qq.com   2026-10-08
 */
#include "stdafx.h"

#include "BotHost.h"
#include "BotHostConvert.h"
#include "BotUiBridge.h"

#include <string.h>   // wcsncpy_s

#include "../../Common/Log/IULog.h"
#include "../../Common/I18n/I18n.h"          // i18n::Translate / GetLanguage / GetUiFontFace：宿主接口的多语言部分
#include "../../Session/UserSessionData.h"   // g_szHomePath（程序目录，DLL 与 exe 同目录）
#include "../../net/IUSocket.h"              // CIUSocket::SendBotCommand / SetBotFrameSink / SendGetGroupBots

namespace balloonbot {

namespace {

//DLL 写来的日志在日志行「函数签名」一栏显示的名字，便于按模块筛选；DLL 已在正文前加了 "[bot] [调用处:行号] "
const char* const kBotLogSource = "FlamingoBot";

//通讯录里查不到用户时的显示名模板（与主面板 ContactDisplayName 的兜底一致），{0} 为用户 id；
//只做标记（TR_NOOP），使用处用 TRF 取译文
const wchar_t* const kFallbackNameTemplate = TR_NOOP(L"用户{0}");

//把宽字符串写进定长缓冲区：写不下时截断，结果始终以 '\0' 结尾
void CopyWide(const std::wstring& text, wchar_t* buf, uint32_t chars)
{
    wcsncpy_s(buf, chars, text.c_str(), _TRUNCATE);
}

} // namespace

CBotHost& CBotHost::GetInstance()
{
    static CBotHost s_instance;
    return s_instance;
}

CBotHost::CBotHost()
    : m_loadAttempted(false)
    , m_ready(false)
    , m_loggedIn(false)
    , m_moduleActive(false)
    , m_botFeature(false)
    , m_groupBotFeature(false)
    , m_personalFeature(false)
    , m_externalFeature(false)
    , m_myUserId(0)
    , m_pendingPickId(0)
    , m_pendingDeptPickId(0)
    , m_routeTarget(NULL)
{
    //主面板在 1013 与 2820 之后经 botui 通知；宿主在加载插件之前就要收得到（开关打开时据此补加载）
    botui::SetNotifyHooks(&CBotHost::BridgeFeatures, &CBotHost::BridgeGroupBotsChanged);
}

CBotHost::~CBotHost()
{
}

void CBotHost::SetCallbacks(const BotHostCallbacks& callbacks)
{
    m_callbacks = callbacks;
}

bool CBotHost::SetFeatures(bool bot, bool groupBot, bool personal, bool groupExternal)
{
    const bool beforeAvailable = IsAvailable();
    const bool beforeMyBots = IsMyBotsAvailable();
    m_botFeature = bot;
    m_groupBotFeature = groupBot;
    m_personalFeature = personal;
    m_externalFeature = groupExternal;
    if (m_loggedIn)
    {
        if (bot && !m_moduleActive)
        {
            //总开关由关变开：必要时先加载 DLL，再让模块进入已登录状态
            if (EnsureModuleReady())
                ActivateModule();
        }
        else if (!bot && m_moduleActive)
        {
            //总开关由开变关：模块关闭全部窗口，DLL 保持加载；入口随后隐藏
            DeactivateModule();
        }
        else if (m_ready && m_moduleActive)
        {
            //其余开关变化：打开着的窗口刷新可用的操作
            m_loader.GetModule()->OnFeaturesChanged(m_botFeature, m_groupBotFeature, m_personalFeature, m_externalFeature);
        }
    }
    LOG_INFO("bot features set: bot=%d, groupbot=%d, personal=%d, groupexternal=%d, available %d -> %d.", bot ? 1 : 0,
             groupBot ? 1 : 0, personal ? 1 : 0, groupExternal ? 1 : 0, beforeAvailable ? 1 : 0, IsAvailable() ? 1 : 0);
    return beforeAvailable != IsAvailable() || beforeMyBots != IsMyBotsAvailable();
}

void CBotHost::OnLoginSucceeded(int64_t myUserId, const std::wstring& myName)
{
    m_myUserId = myUserId;
    m_myName = myName;
    m_loggedIn = true;
    if (!m_botFeature)
    {
        LOG_INFO("bot feature is disabled for this enterprise, bot module not activated.");
        return;
    }
    if (!EnsureModuleReady())
        return;
    //每次登录成功（含断线重连）都通知：模块据此作废断线前的请求，打开着的窗口重新加载
    ActivateModule();
}

void CBotHost::OnLogout()
{
    DeactivateModule();
    m_loggedIn = false;
    m_myUserId = 0;
    m_myName.clear();
    m_pendingPickId = 0;
    m_pendingDeptPickId = 0;
}

void CBotHost::Shutdown()
{
    //入口随之隐藏：撤销登记给 botui 的回调（模块没有加载过时也撤销开关通知）
    botui::SetHooks(NULL, NULL, NULL);
    botui::SetNotifyHooks(NULL, NULL);
    if (!m_loader.IsLoaded())
        return;
    //先撤销帧回调并解除路由：返回之后网络线程不会再拿着旧模块指针调用，随后才能关闭模块、卸载 DLL
    CIUSocket::SetBotFrameSink(NULL, NULL);
    {
        std::lock_guard<std::mutex> guard(m_routeMutex);
        m_routeTarget = NULL;
    }
    if (m_ready)
        m_loader.GetModule()->Shutdown();
    m_ready = false;
    m_moduleActive = false;
    m_loader.Unload();
    m_callbacks = BotHostCallbacks();
    LOG_INFO("bot module unloaded.");
}

bool CBotHost::IsAvailable() const
{
    return IsBotUiAvailable(m_loggedIn, m_ready, m_moduleActive, m_botFeature);
}

bool CBotHost::IsMyBotsAvailable() const
{
    return IsMyBotsEntryVisible(IsAvailable(), m_personalFeature);
}

bool CBotHost::OpenGroupBotManager(HWND owner, int64_t groupId, const std::wstring& groupName)
{
    if (!IsAvailable() || groupId <= 0)
    {
        LOG_WARNING("bot manager not opened, groupid=%lld: bot module unavailable.", static_cast<long long>(groupId));
        return false;
    }
    const bool ok = m_loader.GetModule()->OpenGroupBotManager(owner, groupId, groupName.c_str());
    LOG_INFO("bot manager open requested, groupid=%lld, ok=%d.", static_cast<long long>(groupId), ok ? 1 : 0);
    return ok;
}

void CBotHost::CloseGroupBotManager(int64_t groupId)
{
    if (!m_ready || !m_moduleActive)
        return;
    m_loader.GetModule()->CloseGroupBotManager(groupId);
}

bool CBotHost::OpenMyBots(HWND owner)
{
    if (!IsAvailable())
    {
        LOG_WARNING("my bots window not opened: bot module unavailable.");
        return false;
    }
    const bool ok = m_loader.GetModule()->OpenMyBots(owner);
    LOG_INFO("my bots window open requested, ok=%d.", ok ? 1 : 0);
    return ok;
}

void CBotHost::OnGroupBotsChanged(int64_t groupId)
{
    if (!m_ready || !m_moduleActive)
        return;
    m_loader.GetModule()->OnGroupBotsChanged(groupId);
}

void CBotHost::FrameSinkThunk(void* ctx, uint16_t cmd, uint32_t seq, const std::string& body)
{
    CBotHost* self = static_cast<CBotHost*>(ctx);
    if (self == NULL)
        return;
    //持锁转交：Shutdown 解除路由之后，网络线程不会再拿着旧模块指针调用
    std::lock_guard<std::mutex> guard(self->m_routeMutex);
    if (self->m_routeTarget == NULL)
    {
        LOG_WARNING("bot frame dropped, module not loaded, cmd=%u, seq=%u.", static_cast<unsigned>(cmd), seq);
        return;
    }
    self->m_routeTarget->OnServerFrame(cmd, seq, body.data(), static_cast<uint32_t>(body.size()));
}

bool CBotHost::BridgeIsAvailable()
{
    return GetInstance().IsAvailable();
}

void CBotHost::BridgeOpenGroupManager(HWND owner, int64_t groupId, const wchar_t* groupName)
{
    GetInstance().OpenGroupBotManager(owner, groupId, (groupName != NULL) ? std::wstring(groupName) : std::wstring());
}

void CBotHost::BridgeOpenMyBots(HWND owner)
{
    GetInstance().OpenMyBots(owner);
}

void CBotHost::BridgeFeatures(bool bot, bool groupBot, bool personal, bool groupExternal)
{
    GetInstance().SetFeatures(bot, groupBot, personal, groupExternal);
}

void CBotHost::BridgeGroupBotsChanged(int64_t groupId)
{
    GetInstance().OnGroupBotsChanged(groupId);
}

bool CBotHost::EnsureModuleReady()
{
    if (m_loadAttempted)
        return m_ready;
    m_loadAttempted = true;
    //g_szHomePath 是程序目录，以反斜杠结尾；传完整路径，不依赖系统的 DLL 搜索顺序，避免加载到别处的同名 DLL
    CString dllPath(g_szHomePath);
    dllPath += FLAMINGO_BOT_DLL_NAME;
    balloonapp::PluginLoadResult result = m_loader.Load(dllPath, FLAMINGO_BOT_FACTORY_NAME, flamingobot::kApiVersion);
    if (result != balloonapp::plugin_load_ok)
    {
        LOG_WARNING("bot module unavailable: %s, api version %u.", balloonapp::CPluginLibrary::DescribeResult(result),
                    static_cast<unsigned>(flamingobot::kApiVersion));
        return false;
    }
    //初始化参数里的程序目录不带结尾反斜杠
    CString appDir(g_szHomePath);
    appDir.TrimRight(_T('\\'));
    flamingobot::BotInitParams params;
    params.m_cbSize = sizeof(params);
    params.m_appDir = appDir;
    if (!m_loader.GetModule()->Init(this, &params))
    {
        LOG_ERROR("bot module loaded but Init failed, unloading it.");
        m_loader.Unload();
        return false;
    }
    m_ready = true;
    {
        std::lock_guard<std::mutex> guard(m_routeMutex);
        m_routeTarget = m_loader.GetModule();
    }
    //模块就绪之后才登记帧回调：之前收到的 2802～2809 帧由 CIUSocket 记一条警告后丢弃
    CIUSocket::SetBotFrameSink(&CBotHost::FrameSinkThunk, this);
    //群聊窗的「管理」、主面板与系统设置的「我的机器人」经 botui 调到这里
    botui::SetHooks(&CBotHost::BridgeIsAvailable, &CBotHost::BridgeOpenGroupManager, &CBotHost::BridgeOpenMyBots);
    LOG_INFO("bot module loaded, api version %u.", static_cast<unsigned>(m_loader.GetModule()->GetApiVersion()));
    return true;
}

void CBotHost::ActivateModule()
{
    if (!m_ready)
        return;
    flamingobot::IBotModule* module = m_loader.GetModule();
    module->OnFeaturesChanged(m_botFeature, m_groupBotFeature, m_personalFeature, m_externalFeature);
    module->OnLoggedIn();
    m_moduleActive = true;
}

void CBotHost::DeactivateModule()
{
    if (m_ready && m_moduleActive)
        m_loader.GetModule()->OnLoggingOut();
    m_moduleActive = false;
    m_pendingPickId = 0;
    m_pendingDeptPickId = 0;
}

//======================== flamingobot::IBotHost ========================

void CBotHost::Log(int level, const char* text)
{
    if (text == NULL)
        return;
    long logLevel = LOG_LEVEL_INFO;
    switch (level)
    {
    //DLL 报告的警告
    case flamingobot::bot_log_warning:
        logLevel = LOG_LEVEL_WARNING;
        break;

    //DLL 报告的错误
    case flamingobot::bot_log_error:
        logLevel = LOG_LEVEL_ERROR;
        break;

    //普通信息及不认识的级别
    default:
        logLevel = LOG_LEVEL_INFO;
        break;
    }
    //行号一栏没有意义，固定写 0；正文作为参数传入，避免其中的 % 被当成格式符
    CIULog::Log(logLevel, kBotLogSource, 0, "%s", text);
}

const wchar_t* CBotHost::Translate(const wchar_t* context, const wchar_t* source)
{
    return i18n::Translate(context, source);
}

bool CBotHost::GetUiLanguage(wchar_t* langBuf, uint32_t langChars, wchar_t* fontBuf, uint32_t fontChars)
{
    if (langBuf == NULL || langChars == 0 || fontBuf == NULL || fontChars == 0)
        return false;
    CopyWide(i18n::GetLanguage(), langBuf, langChars);
    CopyWide(i18n::GetUiFontFace(), fontBuf, fontChars);
    return true;
}

uint32_t CBotHost::SendBotCommand(uint16_t cmd, const char* json, uint32_t len)
{
    if (json == NULL || len == 0)
        return 0;
    //放行规则（号段 2802～2809、包体非空、已连接）由 CIUSocket::SendBotCommand 统一判定
    return CIUSocket::GetInstance().SendBotCommand(cmd, std::string(json, len));
}

int64_t CBotHost::GetMyUserId()
{
    return m_myUserId;
}

bool CBotHost::GetMyDisplayName(wchar_t* buf, uint32_t chars)
{
    if (buf == NULL || chars == 0)
        return false;
    std::wstring name = m_myName;
    if (name.empty() && m_myUserId > 0)
        name = TRF(kFallbackNameTemplate, static_cast<long long>(m_myUserId));
    CopyWide(name, buf, chars);
    return !name.empty();
}

bool CBotHost::GetUserDisplayName(int64_t userId, wchar_t* buf, uint32_t chars)
{
    if (buf == NULL || chars == 0 || userId <= 0)
        return false;
    std::wstring name;
    if (userId == m_myUserId)
        name = m_myName;
    if (name.empty() && m_callbacks.m_userName != NULL)
        name = m_callbacks.m_userName(m_callbacks.m_ctx, userId);
    if (name.empty())
        name = TRF(kFallbackNameTemplate, static_cast<long long>(userId));
    CopyWide(name, buf, chars);
    return true;
}

bool CBotHost::GetGroupBotList(int64_t groupId, flamingobot::BotGroupCapsInfo* caps, flamingobot::BotGroupItemInfo* items,
                               uint32_t capacity, uint32_t* total)
{
    if (total != NULL)
        *total = 0;
    if (groupId <= 0 || m_callbacks.m_queryGroupBots == NULL)
        return false;
    net::GroupBotsResult result;
    if (!m_callbacks.m_queryGroupBots(m_callbacks.m_ctx, groupId, result))
        return false;
    const uint32_t count = FillGroupBotList(result, caps, items, capacity);
    if (total != NULL)
        *total = count;
    return true;
}

void CBotHost::RequestGroupBots(int64_t groupId)
{
    if (!m_loggedIn || groupId <= 0)
        return;
    if (m_callbacks.m_refreshGroupBots != NULL)
        m_callbacks.m_refreshGroupBots(m_callbacks.m_ctx, groupId);
    else
        CIUSocket::GetInstance().SendGetGroupBots(groupId);
    LOG_INFO("bot module asked for group bots, groupid=%lld.", static_cast<long long>(groupId));
}

bool CBotHost::PickContacts(HWND owner, uint32_t requestId, const int64_t* preselected, uint32_t count)
{
    if (!m_loggedIn || requestId == 0 || m_callbacks.m_pickContacts == NULL)
        return false;
    std::vector<int64_t> pre;
    for (uint32_t i = 0; preselected != NULL && i < count; ++i)
        pre.push_back(preselected[i]);
    if (!m_callbacks.m_pickContacts(m_callbacks.m_ctx, owner, pre, this))
    {
        LOG_WARNING("bot contacts picker could not be opened, request=%u.", static_cast<unsigned>(requestId));
        return false;
    }
    //选人对话框只有一个：后发起的请求顶替先发起的
    m_pendingPickId = requestId;
    return true;
}

bool CBotHost::GetDepartmentName(int64_t deptId, wchar_t* buf, uint32_t chars)
{
    if (buf == NULL || chars == 0 || deptId <= 0 || m_callbacks.m_deptName == NULL)
        return false;
    const std::wstring name = m_callbacks.m_deptName(m_callbacks.m_ctx, deptId);
    if (name.empty())
        return false;
    CopyWide(name, buf, chars);
    return true;
}

bool CBotHost::PickDepartments(HWND owner, uint32_t requestId, const int64_t* preselected, uint32_t count)
{
    if (!m_loggedIn || requestId == 0 || m_callbacks.m_pickDepartments == NULL)
        return false;
    std::vector<int64_t> pre;
    for (uint32_t i = 0; preselected != NULL && i < count; ++i)
        pre.push_back(preselected[i]);
    if (!m_callbacks.m_pickDepartments(m_callbacks.m_ctx, owner, pre, this))
    {
        LOG_WARNING("bot departments picker could not be opened, request=%u.", static_cast<unsigned>(requestId));
        return false;
    }
    //选部门对话框只有一个：后发起的请求顶替先发起的
    m_pendingDeptPickId = requestId;
    return true;
}

void CBotHost::OnDepartmentsPicked(const std::vector<int64_t>& deptIds)
{
    const uint32_t requestId = m_pendingDeptPickId;
    m_pendingDeptPickId = 0;
    if (requestId == 0 || !m_ready || !m_moduleActive)
        return;
    LOG_INFO("bot departments picked, request=%u, count=%u.", static_cast<unsigned>(requestId),
             static_cast<unsigned>(deptIds.size()));
    m_loader.GetModule()->OnDepartmentsPicked(requestId, deptIds.empty() ? NULL : &deptIds[0],
                                              static_cast<uint32_t>(deptIds.size()));
}

void CBotHost::OnMembersPicked(const std::vector<int64_t>& userIds)
{
    const uint32_t requestId = m_pendingPickId;
    m_pendingPickId = 0;
    if (requestId == 0 || !m_ready || !m_moduleActive)
        return;
    LOG_INFO("bot contacts picked, request=%u, count=%u.", static_cast<unsigned>(requestId),
             static_cast<unsigned>(userIds.size()));
    m_loader.GetModule()->OnContactsPicked(requestId, userIds.empty() ? NULL : &userIds[0],
                                           static_cast<uint32_t>(userIds.size()));
}

} // namespace balloonbot
