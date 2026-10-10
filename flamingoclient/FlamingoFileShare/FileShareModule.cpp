/**
 *  模块总控 CFileShareModule 的实现，见 FileShareModule.h。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"
#include "FileShareModule.h"

#include "../Source/Common/Log/IULog.h"
#include "../Source/Common/Graphics/ShellTypeIcon.h"
#include "Engine/ShareLog.h"
#include "UI/ShareLocalOpen.h"
#include "../Source/Common/I18n/I18n.h"   // i18n::InitFromHost：DLL 的界面文字经宿主的语言模块翻译
#include "../../third-party/balloonui/balloonui/DuiTextResolver.h"   // SetTextResolver：本 DLL 那份 balloonui 的内置文字转换
#include "../Source/Common/I18n/BalloonUiText.h"   // ResolveBalloonUiText：balloonui 内置文字的译文
#include "../../third-party/balloonui/balloonui/DuiTheme.h"   // SetDefaultFontFace：本 DLL 那份 balloonui 的默认字体随界面语言
#include <sodium.h>   // sodium_init：初始化本 DLL 自己那一份 libsodium

namespace fileshare {

namespace {

//语言代码缓冲区的容量（字符），足够放下 "zh-CN"、"qps-ploc" 一类的代码
const uint32_t kLangCodeChars = 32;

//DLL 里 TR 查找时转交的宿主对象；Init 时登记，Shutdown 时清空。可能在任意线程读取
IFileShareHost* volatile g_translateHost = NULL;

//交给 i18n::InitFromHost 的查找函数：转给宿主的 Translate；宿主已解除登记时返回原文
const wchar_t* HostTranslate(const wchar_t* context, const wchar_t* source)
{
    IFileShareHost* host = g_translateHost;
    return (host != NULL) ? host->Translate(context, source) : source;
}

//收帧消息窗口的窗口类名（按 DLL 的模块实例注册，与 exe 里的窗口类互不相干）
const wchar_t* const kMessageWindowClass = L"FlamingoFileShareMessageWindow";

//m_browserOpenOrder 最多保留的条数：超出时丢弃最早的。只用来找最近打开的浏览窗口，不需要很长的历史
const size_t kMaxBrowserOpenOrder = 64;

//消息窗口收到的私有消息：网络接收线程排进了新的帧，请 UI 线程取出处理。
//DLL 自己的窗口私有消息走 WM_APP 段（开发约定文档 UI 约定第 6 条），只投给本窗口；取 0x80 是为了与
//开发约定文档登记的已占用偏移错开，排查时一眼看出号属于文件共享 DLL
const UINT kMsgFramesQueued = WM_APP + 0x80;

//消息窗口收到的私有消息：CShareService 的事件队列里有了提供会话事件（中转连接成败、访问者 sf_bye、通道断开），
//请 UI 线程取出处理。与 kMsgFramesQueued 投给同一个窗口，同样登记在开发约定文档的已占用偏移表里
const UINT kMsgServiceEvents = WM_APP + 0x81;

//消息窗口上驱动访问状态机的定时器：编号与间隔（毫秒）。访问状态机的各项超时都以秒计，每秒检查一次足够
const UINT_PTR kServiceTimerId = 1;
const UINT kServiceTimerMs = 1000;

//「在本机打开」打不开时提示框的标题
const wchar_t* const kLocalOpenFailTitle = TR_NOOP(L"文件共享");

//取本 DLL 的模块实例句柄（链接器提供的 __ImageBase 就是本模块的基址）
HINSTANCE GetDllInstance()
{
    return reinterpret_cast<HINSTANCE>(&__ImageBase);
}

} // namespace

CFileShareModule& CFileShareModule::Instance()
{
    static CFileShareModule s_instance;
    return s_instance;
}

CFileShareModule::CFileShareModule()
    : m_initialized(false)
    , m_msgWnd(NULL)
    , m_host(NULL)
    , m_previewSource(NULL)
    , m_targetOwner(NULL)
{
}

CFileShareModule::~CFileShareModule()
{
}

uint32_t CFileShareModule::GetApiVersion() const
{
    return kApiVersion;
}

bool CFileShareModule::Init(IFileShareHost* host, const FileShareInitParams* params)
{
    if (host == NULL || params == NULL)
        return false;
    if (params->m_cbSize != sizeof(FileShareInitParams) || params->m_appDir == NULL)
        return false;

    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (m_initialized)
            return true;
    }

    HWND msgWnd = CreateMessageWindow();
    if (msgWnd == NULL)
        return false;

    m_host = host;
    m_appDir = params->m_appDir;
    ShareLogSetHost(host);

    //本 DLL 静态链接了自己的一份 libsodium，全局状态与 exe 那一份互不相干，exe 启动时的 sodium_init 初始化不到这里。
    //不初始化时随机数源与 ChaCha20、Poly1305 等算法的实现没有按 CPU 特性选择，中转加解密慢一倍多。
    //须在挂接共享服务（会建立加密的中转连接）之前调用；可重复调用。返回值写进日志：0 为本次完成初始化，1 为此前已初始化，-1 为失败
    const int sodiumRet = sodium_init();
    LOG_INFO(L"FlamingoFileShare: sodium_init returned %d.", sodiumRet);

    //多语言（第 3b 步）：界面文字经宿主的语言模块翻译，界面字体取宿主当前生效的字体；须在创建任何界面窗口之前
    wchar_t language[kLangCodeChars] = { 0 };
    wchar_t uiFont[LF_FACESIZE] = { 0 };
    g_translateHost = host;
    if (host->GetUiLanguage(language, kLangCodeChars, uiFont, LF_FACESIZE))
    {
        i18n::InitFromHost(&HostTranslate, language, uiFont);
        //本 DLL 静态链接了自己的一份 balloonui，它的默认字体也要换成界面字体（第 3e 步）
        balloonwjui::DuiTheme::Inst().SetDefaultFontFace(i18n::UiFontName());
        balloonwjui::SetTextResolver(&i18n::ResolveBalloonUiText);   //内置文字（右键菜单等）同样经宿主翻译
        LOG_INFO(L"FlamingoFileShare ui language %s, ui font %s.", language, uiFont);
    }
    else
    {
        LOG_WARNING(L"FlamingoFileShare: host did not report the ui language, using built-in source text.");
    }

    m_service.Attach(host, msgWnd, kMsgServiceEvents);
    m_shareCounter.Attach(&m_service, this);
    m_service.SetCardStateListener(&m_shareCounter);
    //弹窗通知（第 4 期起）：访问者一端传输结束、分享者一端的操作都交给弹窗总控
    m_notices.Attach(host, this);
    m_service.SetNoticeListeners(&m_notices, &m_notices);
    ::SetTimer(msgWnd, kServiceTimerId, kServiceTimerMs, NULL);

    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_msgWnd = msgWnd;
        m_initialized = true;
    }

    LOG_INFO(L"FlamingoFileShare initialized, api version: %u, app dir: %s.",
             static_cast<unsigned>(kApiVersion), m_appDir.c_str());
    return true;
}

void CFileShareModule::Shutdown()
{
    HWND msgWnd = NULL;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (!m_initialized)
            return;

        //先置为未初始化，此后网络接收线程送来的帧一律拒收；已排队但尚未处理的帧直接丢弃
        m_initialized = false;
        msgWnd = m_msgWnd;
        m_msgWnd = NULL;
        m_frames.clear();
    }

    LOG_INFO("FlamingoFileShare shutting down.");

    //此后被中断的传输不弹窗；业务总控结束传输之前先解除弹窗总控的登记
    m_notices.SetSuppressed(true);
    m_service.SetNoticeListeners(NULL, NULL);
    m_notices.Detach();

    //先关浏览窗口（它们持有访问状态机），再结束全部提供会话与访问会话（等待尚在连接中的线程），
    //之后才销毁接收其事件的消息窗口
    CloseBrowsers();
    m_browsers.clear();
    CloseCreateDialogs();
    m_createDialogs.clear();
    m_editDialogs.clear();
    //「文件共享」窗口在业务总控结束请求之前关闭，免得回调已关闭的窗口
    m_centerWindow.Close();
    //计数类先解除订阅与关联，业务总控结束请求时就不会再回调它
    m_service.SetCardStateListener(NULL);
    m_shareCounter.Detach();
    m_service.Detach();
    if (msgWnd != NULL && ::IsWindow(msgWnd))
    {
        ::KillTimer(msgWnd, kServiceTimerId);
        ::DestroyWindow(msgWnd);
    }

    //各窗口都已关闭：释放按类型缓存的系统图标句柄（DLL 卸载后这些句柄无人释放）
    shellicon::ReleaseCache();

    //最后解除宿主：此后的日志被丢弃，DLL 不再调用任何宿主接口（TR 返回原文）
    ShareLogSetHost(NULL);
    g_translateHost = NULL;
    m_host = NULL;
}

void CFileShareModule::OnLoggedIn()
{
    if (m_host == NULL)
        return;

    LOG_INFO("file share module notified: logged in.");

    //退出登录期间压下的弹窗通知恢复
    m_notices.SetSuppressed(false);
    //拉取「我的共享」，应答到达后与本机记录比对并报告可提供状态
    m_service.OnLoggedIn();
    //断线重连：登记过的会话（仍打开着的聊天窗）重新拉取，角标按重连后的状态更新
    m_shareCounter.RefreshAll();
}

void CFileShareModule::OnLoggingOut()
{
    if (m_host == NULL)
        return;

    LOG_INFO("file share module notified: logging out, closing windows.");
    //退出登录会中断后台的传输：这些不弹窗，已弹出的通知一并关闭（第 4 期起）
    m_notices.SetSuppressed(true);
    CloseBrowsers();
    CloseCreateDialogs();
    m_centerWindow.Close();
    //先丢弃计数类的登记：业务总控结束列表请求时就不会再回调它
    m_shareCounter.Reset();
    m_service.OnLoggingOut();
    m_notices.Reset();
}

bool CFileShareModule::OnServerFrame(uint16_t cmd, uint32_t seq, const char* body, uint32_t len)
{
    if (!IsShareCommand(cmd))
        return false;
    if (body == NULL && len > 0)
        return false;

    std::lock_guard<std::mutex> guard(m_mutex);
    if (!m_initialized || m_msgWnd == NULL)
        return false;

    PendingFrame frame;
    frame.m_cmd = cmd;
    frame.m_seq = seq;
    if (len > 0)
        frame.m_body.assign(body, len);

    //队列由空变非空时才通知一次 UI 线程；UI 线程每次把队列整个取空，故不会漏处理
    const bool wasEmpty = m_frames.empty();
    m_frames.push_back(frame);
    if (wasEmpty)
        ::PostMessageW(m_msgWnd, kMsgFramesQueued, 0, 0);

    return true;
}

bool CFileShareModule::OpenShareCenter(HWND owner)
{
    if (m_host == NULL)
        return false;

    //从系统菜单、主面板打开：不属于任何会话，「发起共享」先选人
    m_centerWindow.SetLaunchConversation(0, 0);
    return m_centerWindow.Open(owner, &m_service, m_host, this);
}

bool CFileShareModule::UiOpenShareCenter(HWND owner, int ctype, int64_t cid)
{
    if (m_host == NULL || cid <= 0)
        return false;
    //从聊天窗浮层的「管理全部共享」打开：记住这个会话，「发起共享」直接在这里发起
    m_centerWindow.SetLaunchConversation(ctype, cid);
    const bool ok = m_centerWindow.Open(owner, &m_service, m_host, this);
    LOG_INFO("open share center from conversation, ctype: %d, cid: %lld, ok: %d.", ctype, static_cast<long long>(cid),
             ok ? 1 : 0);
    return ok;
}

bool CFileShareModule::OpenShareFromCard(HWND owner, int64_t shareId)
{
    if (m_host == NULL || shareId <= 0)
        return false;

    std::unique_ptr<CShareBrowserWindow>& window = m_browsers[shareId];
    if (!window)
    {
        window.reset(new CShareBrowserWindow());
        window->SetUiHost(this);
    }
    //要新建窗口（2026-10-07）：位置按 fileshare_ui.ini 的 [ShareBrowser] 恢复，并相对最近打开、仍开着的另一个浏览窗口错开
    if (!window->IsWindow())
    {
        std::wstring iniPath;
        FileShareSessionInfo info;
        ::ZeroMemory(&info, sizeof(info));
        info.m_cbSize = sizeof(info);
        if (m_host->GetSessionInfo(&info))
            iniPath = SharePlacementIniPath(info.m_personalDir);
        std::set<int64_t> openShares;
        for (std::map<int64_t, std::unique_ptr<CShareBrowserWindow> >::const_iterator it = m_browsers.begin();
             it != m_browsers.end(); ++it)
        {
            if (it->second && it->second->IsWindow())
                openShares.insert(it->first);
        }
        HWND cascadeFrom = NULL;
        const int64_t previous = LatestOpenShare(m_browserOpenOrder, openShares, shareId);
        std::map<int64_t, std::unique_ptr<CShareBrowserWindow> >::const_iterator prev = m_browsers.find(previous);
        if (previous != 0 && prev != m_browsers.end() && prev->second)
            cascadeFrom = prev->second->m_hWnd;
        window->SetPlacement(iniPath, cascadeFrom);
    }
    const bool ok = window->Open(owner, &m_service, shareId);
    if (ok)
    {
        m_browserOpenOrder.push_back(shareId);
        if (m_browserOpenOrder.size() > kMaxBrowserOpenOrder)
            m_browserOpenOrder.erase(m_browserOpenOrder.begin());
    }
    LOG_INFO("open share from card, shareid: %lld, ok: %d.", static_cast<long long>(shareId), ok ? 1 : 0);
    return ok;
}

bool CFileShareModule::OpenCreateShare(HWND owner, int ctype, int64_t cid)
{
    //宿主的入口（聊天窗工具栏、会话右键菜单）：都在聊天窗或主面板上，发起成功后不另开聊天窗
    return OpenCreateShareDialog(owner, ctype, cid, false);
}

bool CFileShareModule::OpenCreateShareDialog(HWND owner, int ctype, int64_t cid, bool openConversationAfter)
{
    if (m_host == NULL || cid <= 0)
        return false;

    std::unique_ptr<CShareCreateDialog>& dialog = m_createDialogs[std::make_pair(ctype, cid)];
    if (!dialog)
        dialog.reset(new CShareCreateDialog());
    dialog->SetOpenConversationOnSuccess(openConversationAfter);
    const bool ok = dialog->OpenForCreate(owner, &m_service, m_host, ctype, cid);
    LOG_INFO("open create share dialog, ctype: %d, cid: %lld, open conversation after: %d, ok: %d.", ctype,
             static_cast<long long>(cid), openConversationAfter ? 1 : 0, ok ? 1 : 0);
    return ok;
}

void CFileShareModule::OnConversationMembers(int ctype, int64_t cid, int code, const FileShareMemberInfo* members,
                                             uint32_t count)
{
    if (m_host == NULL)
        return;
    LOG_INFO("conversation members from host, ctype: %d, cid: %lld, code: %d, count: %u.", ctype,
             static_cast<long long>(cid), code, static_cast<unsigned>(count));
    std::map<std::pair<int, int64_t>, std::unique_ptr<CShareCreateDialog> >::iterator it =
        m_createDialogs.find(std::make_pair(ctype, cid));
    if (it != m_createDialogs.end() && it->second)
        it->second->OnConversationMembers(ctype, cid, code, members, count);
    //调整成员窗口与浮层：各自核对会话，不符的忽略
    for (std::map<int64_t, std::unique_ptr<CShareCreateDialog> >::iterator e = m_editDialogs.begin(); e != m_editDialogs.end(); ++e)
    {
        if (e->second)
            e->second->OnConversationMembers(ctype, cid, code, members, count);
    }
    m_convPopup.OnConversationMembers(ctype, cid, code, members, count);
}

bool CFileShareModule::OpenConversationShares(HWND owner, int ctype, int64_t cid, const RECT* anchor)
{
    if (m_host == NULL || cid <= 0)
        return false;
    const bool ok = m_convPopup.Open(owner, &m_service, m_host, this, ctype, cid, anchor);
    LOG_INFO("open conversation shares, ctype: %d, cid: %lld, ok: %d.", ctype, static_cast<long long>(cid), ok ? 1 : 0);
    return ok;
}

bool CFileShareModule::GetShareState(int64_t shareId, FileShareCardState* out)
{
    if (m_host == NULL || out == NULL || out->m_cbSize != sizeof(FileShareCardState))
        return false;
    ShareCardState state;
    if (!m_service.GetCardState(shareId, state))
        return false;
    out->m_state = state.m_state;
    out->m_reason = state.m_reason;
    out->m_myPerms = state.m_myPerms;
    return true;
}

void CFileShareModule::UiOpenCreateShare(HWND owner, int ctype, int64_t cid, bool openConversationAfter)
{
    OpenCreateShareDialog(owner, ctype, cid, openConversationAfter);
}

void CFileShareModule::UiOpenShare(HWND owner, int64_t shareId)
{
    OpenShareFromCard(owner, shareId);
}

void CFileShareModule::UiEditMembers(HWND owner, const ShareListEntry& share)
{
    if (m_host == NULL || share.m_shareId <= 0)
        return;
    std::unique_ptr<CShareCreateDialog>& dialog = m_editDialogs[share.m_shareId];
    if (!dialog)
        dialog.reset(new CShareCreateDialog());
    const bool ok = dialog->OpenForEdit(owner, &m_service, m_host, share);
    LOG_INFO("open edit members dialog, shareid: %lld, ok: %d.", static_cast<long long>(share.m_shareId), ok ? 1 : 0);
}

bool CFileShareModule::UiOpenImage(HWND owner, const std::wstring& path, IShareImagePreviewSource* source)
{
    if (m_host == NULL || path.empty())
        return false;
    m_previewSource = source;
    const bool ok = m_host->OpenImageViewer(owner, path.c_str(), source != NULL);
    LOG_INFO("open image viewer for preview, navigable: %d, ok: %d.", source != NULL ? 1 : 0, ok ? 1 : 0);
    return ok;
}

void CFileShareModule::UiPreviewSourceGone(IShareImagePreviewSource* source)
{
    if (m_previewSource == source)
        m_previewSource = NULL;
}

void CFileShareModule::NavigateImagePreview(int delta)
{
    if (m_previewSource != NULL)
        m_previewSource->OnPreviewNavigate(delta);
}

bool CFileShareModule::WatchConversationShares(int ctype, int64_t cid)
{
    if (m_host == NULL)
        return false;
    return m_shareCounter.Watch(ctype, cid);
}

void CFileShareModule::UnwatchConversationShares(int ctype, int64_t cid)
{
    if (m_host == NULL)
        return;
    m_shareCounter.Unwatch(ctype, cid);
}

bool CFileShareModule::OpenOwnShareLocally(HWND owner, int64_t shareId)
{
    if (m_host == NULL || shareId <= 0)
        return false;
    const int result = OpenShareLocally(owner, &m_service, shareId);
    if (result == share_local_open_ok)
        return true;
    //打不开：以聊天窗为所有者说明原因（共享在别的电脑上发起、本机记录已被清理、共享的内容已被移走）
    const std::wstring text = ShareLocalOpenFailText(result, m_service.GetLocalStatus(shareId));
    ::MessageBoxW((owner != NULL && ::IsWindow(owner)) ? owner : NULL, text.c_str(), TR(kLocalOpenFailTitle),
                  MB_OK | MB_ICONINFORMATION);
    return false;
}

bool CFileShareModule::UiStartDirectShare(HWND owner)
{
    if (m_host == NULL)
        return false;
    //上一次选好人的发起共享对话框还开着：前置它，不再弹选人窗口
    if (m_targetDialog.IsWindow())
    {
        ::SetForegroundWindow(m_targetDialog.m_hWnd);
        return true;
    }
    m_targetOwner = owner;
    const bool ok = m_host->PickShareTargets(owner);
    LOG_INFO("start direct share, picker shown: %d.", ok ? 1 : 0);
    return ok;
}

void CFileShareModule::OnShareTargetsPicked(const FileShareMemberInfo* targets, uint32_t count)
{
    if (m_host == NULL || targets == NULL || count == 0)
        return;
    //本人不是自己共享的成员：宿主的候选不含本人，这里再挡一次
    const int64_t self = m_service.GetUserId();
    std::vector<ShareMemberRow> rows;
    for (uint32_t i = 0; i < count; ++i)
    {
        if (targets[i].m_userId <= 0 || targets[i].m_userId == self)
            continue;
        ShareMemberRow row;
        row.m_userId = targets[i].m_userId;
        row.m_name = targets[i].m_name;
        rows.push_back(row);
    }
    LOG_INFO("share targets picked, count: %u, usable: %u.", static_cast<unsigned>(count), static_cast<unsigned>(rows.size()));
    if (rows.empty())
        return;
    const HWND owner = (m_targetOwner != NULL && ::IsWindow(m_targetOwner)) ? m_targetOwner : NULL;
    m_targetDialog.OpenForTargets(owner, &m_service, m_host, rows);
}

void CFileShareModule::OnShareGroupCreated(int code, int64_t groupId)
{
    LOG_INFO("share group created, code: %d, groupid: %lld.", code, static_cast<long long>(groupId));
    m_targetDialog.OnShareGroupCreated(code, groupId);
}

void CFileShareModule::OnAvatarsUpdated()
{
    LOG_INFO("avatars updated by host, create dialogs: %u, edit dialogs: %u.", static_cast<unsigned>(m_createDialogs.size()),
             static_cast<unsigned>(m_editDialogs.size()));
    //三种发起共享对话框：按会话发起、调整成员、按选好的对象发起；没打开的窗口 RefreshAvatars 什么也不做
    for (std::map<std::pair<int, int64_t>, std::unique_ptr<CShareCreateDialog> >::iterator it = m_createDialogs.begin();
         it != m_createDialogs.end(); ++it)
    {
        if (it->second)
            it->second->RefreshAvatars();
    }
    for (std::map<int64_t, std::unique_ptr<CShareCreateDialog> >::iterator it = m_editDialogs.begin();
         it != m_editDialogs.end(); ++it)
    {
        if (it->second)
            it->second->RefreshAvatars();
    }
    m_targetDialog.RefreshAvatars();
    //「文件共享」窗口：详情里成员、访问者、分享者的头像
    m_centerWindow.RefreshAvatars();
}

void CFileShareModule::OnConversationShareCount(int ctype, int64_t cid, int count)
{
    if (m_host != NULL)
        m_host->NotifyConversationShareCount(ctype, cid, count);
}

uint32_t CFileShareModule::GetActiveTransferCount()
{
    if (m_host == NULL)
        return 0;
    return static_cast<uint32_t>(m_service.GetTransferCount(false));
}

bool CFileShareModule::IsShareBrowserForeground(int64_t shareId)
{
    std::map<int64_t, std::unique_ptr<CShareBrowserWindow> >::const_iterator it = m_browsers.find(shareId);
    if (it == m_browsers.end() || !it->second || !it->second->IsWindow())
        return false;
    const HWND browser = it->second->m_hWnd;
    if (!::IsWindowVisible(browser) || ::IsIconic(browser))
        return false;
    //前台窗口是浏览窗口本身，或它拥有的对话框（新建文件夹、移动到、确认框等）：用户正看着它
    for (HWND h = ::GetForegroundWindow(); h != NULL; h = ::GetWindow(h, GW_OWNER))
    {
        if (h == browser)
            return true;
    }
    return false;
}

void CFileShareModule::NoticeOpenShareCenter(int64_t shareId)
{
    if (!OpenShareCenter(NULL))
        return;
    if (shareId > 0)
        m_centerWindow.FocusShare(shareId);
}

void CFileShareModule::NoticeBrowse(int64_t shareId, const std::string& dir)
{
    if (!OpenShareFromCard(NULL, shareId))
        return;
    std::map<int64_t, std::unique_ptr<CShareBrowserWindow> >::iterator it = m_browsers.find(shareId);
    if (it != m_browsers.end() && it->second)
        it->second->BrowseTo(dir);
}

bool CFileShareModule::NoticeRetry(const ShareVisitorTransferResult& result)
{
    if (m_host == NULL)
        return false;
    //浏览窗口开着：经它发起，传输列表里多一行
    std::map<int64_t, std::unique_ptr<CShareBrowserWindow> >::iterator it = m_browsers.find(result.m_shareId);
    if (it != m_browsers.end() && it->second && it->second->IsWindow())
        return it->second->RetryTransfer(result);
    //浏览窗口没开：业务总控在后台发起（需要时新建不挂窗口的访问状态机），结束时照常弹窗
    ShareTransferOptions options;
    options.m_label = result.m_label;
    uint32_t id = 0;
    if (result.m_direction == share_transfer_download)
    {
        if (!result.m_downloadItems.empty())
            id = m_service.StartDownload(result.m_shareId, result.m_downloadItems, result.m_targetDir, NULL, options);
    }
    else if (!result.m_uploadItems.empty())
    {
        int errCode = 0;
        id = m_service.StartUpload(result.m_shareId, result.m_uploadItems, result.m_toDir, NULL, errCode, options);
    }
    LOG_INFO("share transfer retried in the background, shareid: %lld, direction: %d, new local id: %u.",
             static_cast<long long>(result.m_shareId), result.m_direction, id);
    return id != 0;
}

bool CFileShareModule::NoticeOpenPath(const std::wstring& path)
{
    return ShareShellOpenPath(path);
}

bool CFileShareModule::NoticeRevealPath(const std::wstring& path)
{
    return ShareShellRevealPath(path);
}

void CFileShareModule::CloseCreateDialogs()
{
    for (std::map<std::pair<int, int64_t>, std::unique_ptr<CShareCreateDialog> >::iterator it = m_createDialogs.begin();
         it != m_createDialogs.end(); ++it)
    {
        if (it->second)
            it->second->Close();
    }
    for (std::map<int64_t, std::unique_ptr<CShareCreateDialog> >::iterator e = m_editDialogs.begin(); e != m_editDialogs.end(); ++e)
    {
        if (e->second)
            e->second->Close();
    }
    m_convPopup.Close();
    m_targetDialog.Close();
}

void CFileShareModule::CloseBrowsers()
{
    for (std::map<int64_t, std::unique_ptr<CShareBrowserWindow> >::iterator it = m_browsers.begin();
         it != m_browsers.end(); ++it)
    {
        if (it->second)
            it->second->Close();
    }
}

HWND CFileShareModule::CreateMessageWindow()
{
    HINSTANCE instance = GetDllInstance();

    //窗口类按模块实例注册一次；已注册（Shutdown 后再 Init）时 GetClassInfoExW 返回成功，直接复用
    WNDCLASSEXW existing = { 0 };
    existing.cbSize = sizeof(existing);
    if (!::GetClassInfoExW(instance, kMessageWindowClass, &existing))
    {
        WNDCLASSEXW wc = { 0 };
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = &CFileShareModule::MessageWndProc;
        wc.hInstance     = instance;
        wc.lpszClassName = kMessageWindowClass;
        if (::RegisterClassExW(&wc) == 0)
            return NULL;
    }

    //HWND_MESSAGE：纯消息窗口，不显示、不参与枚举，只用来把网络线程的通知转到 UI 线程
    return ::CreateWindowExW(0, kMessageWindowClass, L"", 0, 0, 0, 0, 0,
                             HWND_MESSAGE, NULL, instance, NULL);
}

LRESULT CALLBACK CFileShareModule::MessageWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == kMsgFramesQueued)
    {
        CFileShareModule::Instance().DrainFrames();
        return 0;
    }
    if (msg == kMsgServiceEvents)
    {
        CFileShareModule::Instance().m_service.DrainEvents();
        return 0;
    }
    if (msg == WM_TIMER && wParam == kServiceTimerId)
    {
        CFileShareModule::Instance().m_service.OnTimer();
        //锁屏期间推迟的弹窗通知：解锁后补弹
        CFileShareModule::Instance().m_notices.OnTimer();
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

void CFileShareModule::DrainFrames()
{
    std::deque<PendingFrame> frames;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (!m_initialized)
            return;
        frames.swap(m_frames);
    }

    //每帧记一行收到的日志，再交给业务总控分发
    for (size_t i = 0; i < frames.size(); ++i)
    {
        LOG_INFO("share frame received, cmd: %u, seq: %u, body bytes: %u.",
                 static_cast<unsigned>(frames[i].m_cmd), static_cast<unsigned>(frames[i].m_seq),
                 static_cast<unsigned>(frames[i].m_body.size()));
        m_service.HandleServerFrame(frames[i].m_cmd, frames[i].m_seq, frames[i].m_body);
    }
}

} // namespace fileshare
