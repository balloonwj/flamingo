/**
 *  机器人模块对象 CBotModule 的实现，见 BotModule.h。
 *  balloonwj@qq.com   2026-10-08
 */
#include "BotModule.h"

#include <string.h>
#include <wchar.h>

#include "BotLog.h"
#include "../../Source/Common/I18n/I18n.h"   // TRF / TR_NOOP：显示名的兜底文字
#include "../../Source/Common/Log/IULog.h"

//本模块所在映像的基址（链接器提供），用来注册消息窗口类
extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace flamingobot {

namespace {

//消息窗口类名
const wchar_t* const kMsgWndClass = L"FlamingoBotMsgWnd";

//取显示名的缓冲区长度（字符）
const uint32_t kNameBufferChars = 128;

//宿主取不到显示名时的兜底（{0} 为 userid），与 exe 的 ContactDisplayName 一致
const wchar_t* const kFallbackNamePattern = TR_NOOP(L"用户{0}");

//宿主取不到部门名称时的兜底（{0} 为部门ID）
const wchar_t* const kFallbackDeptPattern = TR_NOOP(L"部门{0}");

//一次向宿主取群机器人列表时先准备的条数；不够时按宿主报告的个数重取一次
const uint32_t kInitialBotCapacity = 64;

//从定长数组里读一段宽字符串：宿主没写结尾 0 时也不越界
std::wstring ReadFixed(const wchar_t* text, size_t capacity)
{
    return std::wstring(text, wcsnlen(text, capacity));
}

} // namespace

CBotModule& CBotModule::Instance()
{
    static CBotModule s_module;
    return s_module;
}

CBotModule::CBotModule()
    : m_host(NULL)
    , m_factory(NULL)
    , m_initialized(false)
    , m_headless(false)
    , m_loggedIn(false)
    , m_hwnd(NULL)
    , m_postTarget(NULL)
    , m_nextClientId(0)
    , m_nextPickId(0)
{
}

CBotModule::~CBotModule()
{
}

void CBotModule::SetWindowFactory(IBotWindowFactory* factory)
{
    m_factory = factory;
}

void CBotModule::SetHeadlessForTest(bool headless)
{
    m_headless = headless;
}

//======================== IBotModule ========================

bool CBotModule::Init(IBotHost* host, const BotInitParams* params)
{
    if (m_initialized)
        return true;
    if (host == NULL || params == NULL || params->m_cbSize != sizeof(BotInitParams))
        return false;
    m_host = host;
    BotLogSetHost(host);
    if (m_factory != NULL && !m_factory->InitUi(host))
    {
        LOG_ERROR("bot ui init failed.");
        BotLogSetHost(NULL);
        m_host = NULL;
        return false;
    }
    if (!m_headless && !CreateMessageWindow())
    {
        LOG_ERROR("bot message window creation failed, error=%u.", static_cast<unsigned>(::GetLastError()));
        if (m_factory != NULL)
            m_factory->ShutdownUi();
        BotLogSetHost(NULL);
        m_host = NULL;
        return false;
    }
    m_initialized = true;
    LOG_INFO("FlamingoBot initialized, api version %u.", static_cast<unsigned>(kApiVersion));
    return true;
}

void CBotModule::Shutdown()
{
    if (!m_initialized)
        return;
    CloseAll();
    DestroyMessageWindow();
    if (m_factory != NULL)
        m_factory->ShutdownUi();
    m_book.Clear();
    m_picks.clear();
    m_clients.clear();
    m_loggedIn = false;
    m_initialized = false;
    LOG_INFO("FlamingoBot shut down.");
    BotLogSetHost(NULL);
    m_host = NULL;
}

uint32_t CBotModule::GetApiVersion() const
{
    return kApiVersion;
}

void CBotModule::OnLoggedIn()
{
    m_loggedIn = true;
    //断线之前发出的请求不会再有应答，重连后服务端的 seq 也可能从头计数：清掉登记，再让打开着的窗口重新加载
    m_book.Clear();
    m_picks.clear();
    ManagerMap managers = m_managers;
    for (ManagerMap::iterator it = managers.begin(); it != managers.end(); ++it)
    {
        if (!it->second->m_closed && it->second->m_window)
            it->second->m_window->OnReconnected();
    }
    if (m_myBots && !m_myBots->m_closed && m_myBots->m_window)
        m_myBots->m_window->OnReconnected();
    LOG_INFO("bot module logged in, %u manager window(s) reloaded.", static_cast<unsigned>(managers.size()));
}

void CBotModule::OnLoggingOut()
{
    CloseAll();
    m_book.Clear();
    m_picks.clear();
    m_loggedIn = false;
    LOG_INFO("bot module logging out, all windows closed.");
}

void CBotModule::OnServerFrame(uint16_t cmd, uint32_t seq, const char* body, uint32_t len)
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
    if (target == NULL || !::PostMessageW(target, kMsgBotFrameArrived, 0, reinterpret_cast<LPARAM>(packet)))
    {
        LOG_WARNING("bot frame dropped, cmd=%u, seq=%u, module not ready.", static_cast<unsigned>(cmd),
                    static_cast<unsigned>(seq));
        delete packet;
    }
}

bool CBotModule::OpenGroupBotManager(HWND owner, int64_t groupId, const wchar_t* groupName)
{
    if (!m_initialized || groupId <= 0)
        return false;
    if (!m_features.m_bot)
    {
        LOG_WARNING("bot manager not opened, groupid=%lld: bot feature is off.", static_cast<long long>(groupId));
        return false;
    }
    if (m_factory == NULL)
        return false;
    const std::wstring name = (groupName != NULL) ? std::wstring(groupName) : std::wstring();
    ManagerMap::iterator it = m_managers.find(groupId);
    //上一次关闭的窗口还没来得及释放：现在释放（此时不在那个窗口的消息处理里）
    if (it != m_managers.end() && it->second->m_closed)
    {
        m_managers.erase(it);
        it = m_managers.end();
    }
    if (it == m_managers.end())
    {
        std::shared_ptr<TopEntry> entry(new TopEntry());
        entry->m_window.reset(m_factory->CreateGroupManager(groupId, name, this));
        if (!entry->m_window)
        {
            LOG_ERROR("bot manager creation failed, groupid=%lld.", static_cast<long long>(groupId));
            return false;
        }
        m_managers[groupId] = entry;
    }
    std::shared_ptr<TopEntry> entry = m_managers[groupId];
    if (!name.empty())
        entry->m_window->SetGroupName(name);
    if (!entry->m_window->Open(owner))
    {
        LOG_ERROR("bot manager open failed, groupid=%lld.", static_cast<long long>(groupId));
        m_managers.erase(groupId);
        return false;
    }
    LOG_INFO("bot manager opened, groupid=%lld.", static_cast<long long>(groupId));
    return true;
}

void CBotModule::CloseGroupBotManager(int64_t groupId)
{
    ManagerMap::iterator it = m_managers.find(groupId);
    if (it == m_managers.end())
        return;
    std::shared_ptr<TopEntry> entry = it->second;
    m_managers.erase(it);
    if (!entry->m_closed && entry->m_window)
        entry->m_window->Close();
    LOG_INFO("bot manager closed by host, groupid=%lld.", static_cast<long long>(groupId));
}

bool CBotModule::OpenMyBots(HWND owner)
{
    if (!m_initialized || m_factory == NULL)
        return false;
    if (!m_features.m_bot)
    {
        LOG_WARNING("my bots window not opened: bot feature is off.");
        return false;
    }
    if (m_myBots && m_myBots->m_closed)
        m_myBots.reset();
    if (!m_myBots)
    {
        std::shared_ptr<TopEntry> entry(new TopEntry());
        entry->m_window.reset(m_factory->CreateMyBots(this));
        if (!entry->m_window)
        {
            LOG_ERROR("my bots window creation failed.");
            return false;
        }
        m_myBots = entry;
    }
    if (!m_myBots->m_window->Open(owner))
    {
        LOG_ERROR("my bots window open failed.");
        m_myBots.reset();
        return false;
    }
    LOG_INFO("my bots window opened.");
    return true;
}

void CBotModule::OnFeaturesChanged(bool bot, bool groupBot, bool personal, bool groupExternal)
{
    m_features.m_bot = bot;
    m_features.m_groupBot = groupBot;
    m_features.m_personal = personal;
    m_features.m_groupExternal = groupExternal;
    LOG_INFO("bot features changed: bot=%d, groupbot=%d, personal=%d, groupexternal=%d.", bot ? 1 : 0,
             groupBot ? 1 : 0, personal ? 1 : 0, groupExternal ? 1 : 0);
    //总开关关闭：全部窗口关闭（宿主随后也会按退出登录处理）
    if (!bot)
    {
        CloseAll();
        return;
    }
    ManagerMap managers = m_managers;
    for (ManagerMap::iterator it = managers.begin(); it != managers.end(); ++it)
    {
        if (!it->second->m_closed && it->second->m_window)
            it->second->m_window->OnFeaturesChanged();
    }
    if (m_myBots && !m_myBots->m_closed && m_myBots->m_window)
        m_myBots->m_window->OnFeaturesChanged();
}

void CBotModule::OnGroupBotsChanged(int64_t groupId)
{
    ManagerMap managers = m_managers;
    for (ManagerMap::iterator it = managers.begin(); it != managers.end(); ++it)
    {
        if (groupId != 0 && it->first != groupId)
            continue;
        if (!it->second->m_closed && it->second->m_window)
            it->second->m_window->OnGroupBotsChanged();
    }
}

IBotReplySink* CBotModule::TakePick(uint32_t requestId, const int64_t* ids, uint32_t count, const char* what,
                                    std::vector<int64_t>& out)
{
    out.clear();
    std::map<uint32_t, uint32_t>::iterator pick = m_picks.find(requestId);
    if (pick == m_picks.end())
    {
        LOG_INFO("bot %s pick ignored, request=%u is no longer pending.", what, static_cast<unsigned>(requestId));
        return NULL;
    }
    const uint32_t clientId = pick->second;
    m_picks.erase(pick);
    std::map<uint32_t, IBotReplySink*>::iterator client = m_clients.find(clientId);
    if (client == m_clients.end() || client->second == NULL)
        return NULL;
    for (uint32_t i = 0; ids != NULL && i < count; ++i)
    {
        if (ids[i] > 0)
            out.push_back(ids[i]);
    }
    LOG_INFO("bot %s picked, request=%u, count=%u.", what, static_cast<unsigned>(requestId),
             static_cast<unsigned>(out.size()));
    return client->second;
}

void CBotModule::OnContactsPicked(uint32_t requestId, const int64_t* userIds, uint32_t count)
{
    std::vector<int64_t> ids;
    IBotReplySink* sink = TakePick(requestId, userIds, count, "contacts", ids);
    if (sink != NULL)
        sink->OnContactsPicked(requestId, ids);
}

void CBotModule::OnDepartmentsPicked(uint32_t requestId, const int64_t* deptIds, uint32_t count)
{
    std::vector<int64_t> ids;
    IBotReplySink* sink = TakePick(requestId, deptIds, count, "departments", ids);
    if (sink != NULL)
        sink->OnDepartmentsPicked(requestId, ids);
}

//======================== 收帧 ========================

void CBotModule::ProcessFrame(uint16_t cmd, uint32_t seq, const std::string& body)
{
    BotReply reply;
    if (!BotCodec::ParseReply(cmd, seq, body, reply))
        LOG_WARNING("bot reply unparsable, cmd=%u, seq=%u, %u bytes.", static_cast<unsigned>(cmd),
                    static_cast<unsigned>(seq), static_cast<unsigned>(body.size()));
    uint32_t clientId = 0;
    if (!m_book.Take(seq, cmd, clientId))
    {
        LOG_INFO("bot reply without pending request dropped, cmd=%u, seq=%u.", static_cast<unsigned>(cmd),
                 static_cast<unsigned>(seq));
        return;
    }
    std::map<uint32_t, IBotReplySink*>::iterator client = m_clients.find(clientId);
    if (client == m_clients.end() || client->second == NULL)
        return;
    LOG_INFO("bot reply delivered, cmd=%u, seq=%u, code=%d.", static_cast<unsigned>(cmd), static_cast<unsigned>(seq),
             reply.m_code);
    client->second->OnBotReply(reply);
}

void CBotModule::ReleaseClosedWindows()
{
    ManagerMap::iterator it = m_managers.begin();
    while (it != m_managers.end())
    {
        if (it->second->m_closed)
            it = m_managers.erase(it);
        else
            ++it;
    }
    if (m_myBots && m_myBots->m_closed)
        m_myBots.reset();
}

size_t CBotModule::PendingRequestCount() const
{
    return m_book.Size();
}

size_t CBotModule::ManagerCount() const
{
    return m_managers.size();
}

//======================== IBotWindowOwner ========================

IBotHost* CBotModule::Host()
{
    return m_host;
}

BotFeatures CBotModule::Features() const
{
    return m_features;
}

int64_t CBotModule::MyUserId()
{
    return (m_host != NULL) ? m_host->GetMyUserId() : 0;
}

std::wstring CBotModule::UserName(int64_t userId)
{
    wchar_t buf[kNameBufferChars] = { 0 };
    if (m_host != NULL && m_host->GetUserDisplayName(userId, buf, kNameBufferChars))
    {
        const std::wstring name = ReadFixed(buf, kNameBufferChars);
        if (!name.empty())
            return name;
    }
    return TRF(kFallbackNamePattern, static_cast<long long>(userId));
}

std::wstring CBotModule::DeptName(int64_t deptId)
{
    wchar_t buf[kNameBufferChars] = { 0 };
    if (m_host != NULL && m_host->GetDepartmentName(deptId, buf, kNameBufferChars))
    {
        const std::wstring name = ReadFixed(buf, kNameBufferChars);
        if (!name.empty())
            return name;
    }
    return TRF(kFallbackDeptPattern, static_cast<long long>(deptId));
}

uint32_t CBotModule::RegisterClient(IBotReplySink* sink)
{
    ++m_nextClientId;
    if (m_nextClientId == 0)
        ++m_nextClientId;
    m_clients[m_nextClientId] = sink;
    return m_nextClientId;
}

void CBotModule::UnregisterClient(uint32_t clientId)
{
    m_clients.erase(clientId);
    const size_t dropped = m_book.RemoveClient(clientId);
    std::map<uint32_t, uint32_t>::iterator it = m_picks.begin();
    while (it != m_picks.end())
    {
        if (it->second == clientId)
            it = m_picks.erase(it);
        else
            ++it;
    }
    if (dropped > 0)
        LOG_INFO("bot client %u unregistered, %u pending request(s) dropped.", static_cast<unsigned>(clientId),
                 static_cast<unsigned>(dropped));
}

uint32_t CBotModule::SendCommand(uint32_t clientId, uint16_t cmd, const std::string& body)
{
    if (m_host == NULL || body.empty() || !IsBotDllCommand(cmd))
        return 0;
    const uint32_t seq = m_host->SendBotCommand(cmd, body.data(), static_cast<uint32_t>(body.size()));
    if (seq == 0)
    {
        LOG_WARNING("bot command not sent, cmd=%u, client=%u.", static_cast<unsigned>(cmd),
                    static_cast<unsigned>(clientId));
        return 0;
    }
    m_book.Add(seq, cmd, clientId);
    return seq;
}

bool CBotModule::GetGroupBots(int64_t groupId, GroupBotList& out)
{
    out = GroupBotList();
    if (m_host == NULL || groupId <= 0)
        return false;
    BotGroupCapsInfo caps;
    memset(&caps, 0, sizeof(caps));
    caps.m_cbSize = sizeof(caps);
    std::vector<BotGroupItemInfo> items(kInitialBotCapacity);
    for (size_t i = 0; i < items.size(); ++i)
    {
        memset(&items[i], 0, sizeof(BotGroupItemInfo));
        items[i].m_cbSize = sizeof(BotGroupItemInfo);
    }
    uint32_t total = 0;
    if (!m_host->GetGroupBotList(groupId, &caps, &items[0], static_cast<uint32_t>(items.size()), &total))
        return false;
    //机器人比预备的多：按宿主报告的个数重取一次
    if (total > items.size())
    {
        items.resize(total);
        for (size_t i = 0; i < items.size(); ++i)
        {
            memset(&items[i], 0, sizeof(BotGroupItemInfo));
            items[i].m_cbSize = sizeof(BotGroupItemInfo);
        }
        if (!m_host->GetGroupBotList(groupId, &caps, &items[0], static_cast<uint32_t>(items.size()), &total))
            return false;
    }
    out.m_caps.m_isOwner = caps.m_isOwner;
    out.m_caps.m_groupBotAllowed = caps.m_groupBotAllowed;
    out.m_caps.m_externalAllowed = caps.m_externalAllowed;
    out.m_caps.m_groupBotMax = (caps.m_groupBotMax > 0) ? caps.m_groupBotMax : kGroupBotMax;
    const size_t count = (total < items.size()) ? total : items.size();
    for (size_t i = 0; i < count; ++i)
    {
        const BotGroupItemInfo& src = items[i];
        GroupBotItem item;
        item.m_botId = src.m_botId;
        item.m_userId = src.m_userId;
        item.m_ownerType = src.m_ownerType;
        item.m_isGroupBot = src.m_isGroupBot;
        item.m_name = ReadFixed(src.m_name, kBotNameChars);
        item.m_desc = ReadFixed(src.m_desc, kBotDescChars);
        item.m_glyph = ReadFixed(src.m_glyph, kBotGlyphChars);
        item.m_color = ReadFixed(src.m_color, kBotColorChars);
        item.m_status = src.m_status;
        item.m_off = src.m_off;
        item.m_active = src.m_active;
        item.m_canOff = src.m_canOff;
        item.m_atable = src.m_atable;
        out.m_bots.push_back(item);
    }
    return true;
}

void CBotModule::RequestGroupBots(int64_t groupId)
{
    if (m_host == NULL || groupId <= 0)
        return;
    m_host->RequestGroupBots(groupId);
    LOG_INFO("bot module asked host to refresh group bots, groupid=%lld.", static_cast<long long>(groupId));
}

uint32_t CBotModule::BeginPick(uint32_t clientId)
{
    ++m_nextPickId;
    if (m_nextPickId == 0)
        ++m_nextPickId;
    const uint32_t requestId = m_nextPickId;
    m_picks[requestId] = clientId;
    return requestId;
}

uint32_t CBotModule::PickContacts(uint32_t clientId, HWND owner, const std::vector<int64_t>& preselected)
{
    if (m_host == NULL)
        return 0;
    const uint32_t requestId = BeginPick(clientId);
    const int64_t* ids = preselected.empty() ? NULL : &preselected[0];
    if (!m_host->PickContacts(owner, requestId, ids, static_cast<uint32_t>(preselected.size())))
    {
        m_picks.erase(requestId);
        LOG_WARNING("bot contacts picker refused by host, request=%u.", static_cast<unsigned>(requestId));
        return 0;
    }
    return requestId;
}

uint32_t CBotModule::PickDepartments(uint32_t clientId, HWND owner, const std::vector<int64_t>& preselected)
{
    if (m_host == NULL)
        return 0;
    const uint32_t requestId = BeginPick(clientId);
    const int64_t* ids = preselected.empty() ? NULL : &preselected[0];
    if (!m_host->PickDepartments(owner, requestId, ids, static_cast<uint32_t>(preselected.size())))
    {
        m_picks.erase(requestId);
        LOG_WARNING("bot departments picker refused by host, request=%u.", static_cast<unsigned>(requestId));
        return 0;
    }
    return requestId;
}

void CBotModule::OnTopWindowClosed(int kind, int64_t groupId)
{
    if (kind == bot_window_group_manager)
    {
        ManagerMap::iterator it = m_managers.find(groupId);
        if (it != m_managers.end())
            it->second->m_closed = true;
    }
    else if (kind == bot_window_my_bots && m_myBots)
    {
        m_myBots->m_closed = true;
    }
    LOG_INFO("bot window closed by user, kind=%d, groupid=%lld.", kind, static_cast<long long>(groupId));
    ScheduleRelease();
}

//======================== 内部 ========================

void CBotModule::CloseAll()
{
    ManagerMap managers;
    managers.swap(m_managers);
    for (ManagerMap::iterator it = managers.begin(); it != managers.end(); ++it)
    {
        if (!it->second->m_closed && it->second->m_window)
            it->second->m_window->Close();
    }
    managers.clear();
    if (m_myBots)
    {
        std::shared_ptr<TopEntry> entry = m_myBots;
        m_myBots.reset();
        if (!entry->m_closed && entry->m_window)
            entry->m_window->Close();
    }
}

void CBotModule::ScheduleRelease()
{
    if (m_hwnd != NULL)
        ::PostMessageW(m_hwnd, kMsgBotReleaseWindows, 0, 0);
}

bool CBotModule::CreateMessageWindow()
{
    HINSTANCE inst = reinterpret_cast<HINSTANCE>(&__ImageBase);
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &CBotModule::MessageWndProc;
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

void CBotModule::DestroyMessageWindow()
{
    {
        std::lock_guard<std::mutex> guard(m_wndMutex);
        m_postTarget = NULL;
    }
    if (m_hwnd == NULL)
        return;
    //丢弃还在队列里的帧，释放它们的包体
    MSG msg;
    while (::PeekMessageW(&msg, m_hwnd, kMsgBotFrameArrived, kMsgBotFrameArrived, PM_REMOVE))
        delete reinterpret_cast<FramePacket*>(msg.lParam);
    ::DestroyWindow(m_hwnd);
    m_hwnd = NULL;
}

LRESULT CALLBACK CBotModule::MessageWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    CBotModule* self = reinterpret_cast<CBotModule*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != NULL && msg == kMsgBotFrameArrived)
    {
        FramePacket* packet = reinterpret_cast<FramePacket*>(lParam);
        if (packet != NULL)
        {
            self->ProcessFrame(packet->m_cmd, packet->m_seq, packet->m_body);
            delete packet;
        }
        return 0;
    }
    if (self != NULL && msg == kMsgBotReleaseWindows)
    {
        self->ReleaseClosedWindows();
        return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace flamingobot
