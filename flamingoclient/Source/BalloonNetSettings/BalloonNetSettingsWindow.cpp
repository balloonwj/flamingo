/**
 *  BalloonNetSettingsWindow.cpp
 *
 *  新版网络设置窗口的实现。三组服务器卡片切换 + 表单编辑 + 读写
 *  flamingo.ini。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#include "stdafx.h"
#include "BalloonNetSettingsWindow.h"

#include <memory>
#include <process.h>                  // _beginthreadex

#include "../CustomMsgDef.h"          // WM_CLOSE_MAINDLG
#include "../IULog.h"                 // LOG_INFO / LOG_ERROR
#include "../IniFile.h"               // CIniFile
#include "../UserSessionData.h"       // g_szHomePath
#include "../FlamingoClient.h"        // CFlamingoClient::SetServer / SetPort 等
#include "NetSettingsLayout.h"        // BuildNetSettingsBody / NetCtrlId
#include "NetServerCard.h"            // NetServerCard
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEditHost.h"
#include "../net/IUSocket.h"            // CIUSocket::TestConnect

using namespace balloonwjui;
using namespace balloonnetsettings;

// ---- 文件作用域常量 -----------------------------------------------------

// 网络设置窗口固定客户区尺寸（逻辑像素）。design 原值 470x530 的 85%。
static const int kNetWindowWidth  = 400;
static const int kNetWindowHeight = 415;

// 标题栏高度（逻辑像素）。
static const int kNetWindowTitleBarHeight = 31;

// 端口合法取值范围。
static const int kNetPortMin = 1;
static const int kNetPortMax = 65535;

// TCP 探测的超时时间（秒）。"测试连接"按钮与开窗时的自动探测共用这一个值：
// 两者做的是同一件事（能否在超时内建起 TCP 连接），没有理由给不同的耐心。
static const int kTestConnectTimeoutSec = 3;

// flamingo.ini 里三组服务器各自的键名（[server] 段下）。下标对应
// NetServerKind：0=聊天，1=图片，2=文件。
namespace {

// 后台 TCP 探测任务 —— StartProbe 在堆上创建并交给工作线程；探测完工作线程
// 把它作为 resultMsg 的 LPARAM 回投，由 OnTestResult / OnProbeResult 取用并
// delete；若窗口已关闭、回投失败，则工作线程自行 delete，杜绝泄漏。
// "测试连接"按钮与开窗时的自动探测共用本结构，靠 resultMsg 区分善后方式。
struct NetTestTask
{
    HWND    hwnd;        // 结果回投的目标窗口
    UINT    resultMsg;   // 探测完回投的消息：WM_NETTEST_RESULT 或 WM_NETPROBE_RESULT
    int     kind;        // 本次探测针对哪一组服务器，见 NetServerKind
    CString host;        // 待探测的服务器地址
    int     port;        // 待探测的端口
    int     timeoutSec;  // 连接超时时间（秒）
    bool    ok;          // [出参] 探测结果：true 表示可连通
};

struct NetIniKeys
{
    LPCTSTR addrKey;   // 地址键
    LPCTSTR portKey;   // 端口键
};

const NetIniKeys kIniKeys[kNetServerCount] =
{
    { _T("server"),     _T("port")     },   // 聊天服务器
    { _T("imgserver"),  _T("imgport")  },   // 图片服务器
    { _T("fileserver"), _T("fileport") },   // 文件服务器
};

// 三张卡片控件的 id，下标对应 NetServerKind。
const int kCardIds[kNetServerCount] =
{
    kIdCardChat, kIdCardImage, kIdCardFile,
};

// 拼出 flamingo.ini 的完整路径。
CString IniFilePath()
{
    CString path(g_szHomePath);
    path += _T("config\\flamingo.ini");
    return path;
}

} // anonymous namespace

// =======================================================================
// 构造 / 析构
// =======================================================================

CBalloonNetSettingsWindow::CBalloonNetSettingsWindow(CFlamingoClient* pFMGClient)
    : m_lpFMGClient(pFMGClient)
    , m_hMainDlg(NULL)
    , m_modalRunning(false)
    , m_modalResult(IDCANCEL)
    , m_testing(false)
{
    // 三张卡片初始都置"连接中"：窗口一显示就会发起探测，结果回来才有结论。
    for (int i = 0; i < kNetServerCount; ++i)
    {
        m_connState[i] = kNetConnConnecting;
    }
}

CBalloonNetSettingsWindow::~CBalloonNetSettingsWindow()
{
    // 客户区控件树与 HWND 由 DuiFrameWindow 基类析构兜底。
}

// =======================================================================
// 创建 / 显示
// =======================================================================

bool CBalloonNetSettingsWindow::BuildWindow(HWND hWndParent)
{
    if (Create(hWndParent, CWindow::rcDefault, _T("FlamingoIM"),
               WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR(_T("网络设置窗口 Create 失败。"));
        return false;
    }

    SetTitle(_T("网络设置"));
    SetTitleBarHeight(kNetWindowTitleBarHeight);
    SetButtons(false, false, true);   // 只保留关闭按钮
    SetResizable(false);              // 固定尺寸对话框
    SetMinSize(kNetWindowWidth, kNetWindowHeight);

    InitUi();
    ResizeClient(kNetWindowWidth, kNetWindowHeight);
    CenterWindow();
    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);
    return true;
}

void CBalloonNetSettingsWindow::CreateAndShow(HWND hMainDlg)
{
    // 调试入口（CMainDlg --netsettings）：无父窗口、非模态。
    m_hMainDlg = hMainDlg;
    if (!BuildWindow(NULL))
        return;

    LOG_INFO(_T("网络设置窗口已创建并显示。"));
}

INT_PTR CBalloonNetSettingsWindow::DoModal(HWND hWndParent)
{
    // 接入登录流程：模态于父窗口（登录窗），跑独立消息循环，直至
    // EndModal 把 m_modalRunning 置 false。
    m_modalResult  = IDCANCEL;
    m_modalRunning = true;

    if (!BuildWindow(hWndParent))
    {
        m_modalRunning = false;
        return IDCANCEL;
    }

    if (hWndParent != NULL)
        ::EnableWindow(hWndParent, FALSE);

    MSG msg;
    while (::GetMessage(&msg, NULL, 0, 0))
    {
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
        if (!m_modalRunning)
            break;
    }

    if (hWndParent != NULL)
    {
        ::EnableWindow(hWndParent, TRUE);
        ::SetForegroundWindow(hWndParent);
    }

    // 模态循环结束后统一销毁窗口。
    if (IsWindow())
        DestroyWindow();

    return m_modalResult;
}

void CBalloonNetSettingsWindow::EndModal(INT_PTR result)
{
    m_modalResult  = result;
    m_modalRunning = false;
}

// =======================================================================
// 客户区
// =======================================================================

void CBalloonNetSettingsWindow::InitUi()
{
    std::unique_ptr<DuiControl> root = BuildNetSettingsBody();
    if (!root)
    {
        LOG_ERROR(_T("BuildNetSettingsBody 返回空 —— 网络设置窗客户区未能构建。"));
        return;
    }
    SetClientContent(std::move(root));

    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        LOG_ERROR(_T("SetClientContent 之后 GetClientContent 仍为空。"));
        return;
    }

    // 地址与端口两个输入框都要画出边框，让用户看出它们是可以输入的。
    DuiEditHost* hostEdit = dynamic_cast<DuiEditHost*>(
        content->FindCtrlById(kIdEditHost));
    if (hostEdit != NULL)
    {
        hostEdit->SetShowBorder(true);
    }
    DuiEditHost* portEdit = dynamic_cast<DuiEditHost*>(
        content->FindCtrlById(kIdEditPort));
    if (portEdit != NULL)
    {
        portEdit->SetShowBorder(true);
    }

    // 从 flamingo.ini 载入三组配置，刷新卡片，并把选中组灌进表单。
    LoadModelFromIni();
    RefreshCards();
    LoadFormFromModel();

    // 配置就位后立刻探一次三组服务器的可达性，把卡片上的状态从"连接中"
    // 落到真实结论。探测在后台线程做，不阻塞窗口显示。
    StartProbeAll();

    LOG_INFO(_T("网络设置窗客户区 UI 初始化完成。"));
}

// =======================================================================
// flamingo.ini 读 / 写
// =======================================================================

void CBalloonNetSettingsWindow::LoadModelFromIni()
{
    const CString iniPath = IniFilePath();
    CIniFile ini;

    for (int i = 0; i < kNetServerCount; ++i)
    {
        NetServerEntry entry;

        // 地址。
        TCHAR addrBuf[128] = { 0 };
        ini.ReadString(_T("server"), kIniKeys[i].addrKey,
                       NetServerDefaultHost(i), addrBuf, 128, iniPath);
        entry.host = addrBuf;

        // 端口。
        CString defPort;
        defPort.Format(_T("%d"), NetServerDefaultPort(i));
        TCHAR portBuf[32] = { 0 };
        ini.ReadString(_T("server"), kIniKeys[i].portKey,
                       defPort, portBuf, 32, iniPath);
        entry.port = _ttoi(portBuf);

        m_model.SetServer(i, entry);
    }
}

void CBalloonNetSettingsWindow::SaveModelToIni()
{
    const CString iniPath = IniFilePath();
    CIniFile ini;

    for (int i = 0; i < kNetServerCount; ++i)
    {
        const NetServerEntry& entry = m_model.Server(i);

        ini.WriteString(_T("server"), kIniKeys[i].addrKey, entry.host, iniPath);

        CString portStr;
        portStr.Format(_T("%d"), entry.port);
        ini.WriteString(_T("server"), kIniKeys[i].portKey, portStr, iniPath);
    }

    LOG_INFO(_T("网络设置已写回 flamingo.ini。"));
}

// =======================================================================
// 表单 <-> 模型
// =======================================================================

void CBalloonNetSettingsWindow::CommitFormToModel()
{
    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    NetServerEntry entry = m_model.Server(m_model.Selected());

    DuiEditHost* hostEdit = dynamic_cast<DuiEditHost*>(
        content->FindCtrlById(kIdEditHost));
    if (hostEdit != NULL)
    {
        CString host = hostEdit->GetText();
        host.Trim();
        entry.host = host;
    }

    DuiEditHost* portEdit = dynamic_cast<DuiEditHost*>(
        content->FindCtrlById(kIdEditPort));
    if (portEdit != NULL)
    {
        CString portStr = portEdit->GetText();
        portStr.Trim();
        const int port = _ttoi(portStr);
        // 仅在端口合法时更新，避免清空 / 误输入把端口写坏。
        if (port >= kNetPortMin && port <= kNetPortMax)
        {
            entry.port = port;
        }
    }

    m_model.SetServer(m_model.Selected(), entry);
}

void CBalloonNetSettingsWindow::LoadFormFromModel()
{
    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    const NetServerEntry& entry = m_model.Server(m_model.Selected());

    DuiEditHost* hostEdit = dynamic_cast<DuiEditHost*>(
        content->FindCtrlById(kIdEditHost));
    if (hostEdit != NULL)
    {
        hostEdit->SetText(entry.host);
    }

    DuiEditHost* portEdit = dynamic_cast<DuiEditHost*>(
        content->FindCtrlById(kIdEditPort));
    if (portEdit != NULL)
    {
        CString portStr;
        portStr.Format(_T("%d"), entry.port);
        portEdit->SetText(portStr);
    }
}

void CBalloonNetSettingsWindow::RefreshCards()
{
    DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    for (int i = 0; i < kNetServerCount; ++i)
    {
        NetServerCard* card = dynamic_cast<NetServerCard*>(
            content->FindCtrlById(kCardIds[i]));
        if (card == NULL)
        {
            continue;
        }
        const NetServerEntry& entry = m_model.Server(i);
        card->SetEndpoint(entry.host, entry.port);
        card->SetConnState(m_connState[i]);
        card->SetCardSelected(i == m_model.Selected());
    }
}

void CBalloonNetSettingsWindow::SwitchToServer(int kind)
{
    if (kind == m_model.Selected())
    {
        return;   // 已是当前编辑的服务器组
    }

    CommitFormToModel();   // 暂存当前组的编辑
    m_model.Select(kind);
    LoadFormFromModel();   // 载入新选中组
    RefreshCards();        // 选中态 + 副标题刷新
}

// =======================================================================
// 按钮动作
// =======================================================================

void CBalloonNetSettingsWindow::DoTestConnection()
{
    // 防重入：已有一次探测在后台进行时，忽略再次点击
    if (m_testing)
        return;

    CommitFormToModel();
    const int kind = m_model.Selected();
    const NetServerEntry& entry = m_model.Server(kind);

    // 探测任务交后台线程执行，避免阻塞 UI（同步探测最长约 3 秒会卡窗）。
    if (!StartProbe(kind, entry.host, entry.port, WM_NETTEST_RESULT))
    {
        // 线程创建失败（极罕见）：提示用户重试
        ::MessageBox(m_hWnd, _T("无法启动连接测试，请稍后重试。"),
                     _T("网络设置"), MB_OK | MB_ICONWARNING);
        return;
    }

    // 进入"测试中"状态：按钮文字提示，并防止重复点击；被测的那张卡片同时
    // 回到"连接中"，等结果回来再落到"已连接" / "未连接"。
    m_testing = true;
    SetTestButtonText(_T("测试中…"));
    m_connState[kind] = kNetConnConnecting;
    RefreshCards();
}

void CBalloonNetSettingsWindow::StartProbeAll()
{
    for (int i = 0; i < kNetServerCount; ++i)
    {
        const NetServerEntry& entry = m_model.Server(i);

        // 地址为空 / 端口越界的那组不必起线程，直接判为"未连接"——
        // 这种配置本来就连不上，探测纯属浪费 3 秒。
        if (entry.host.IsEmpty()
            || entry.port < kNetPortMin
            || entry.port > kNetPortMax)
        {
            m_connState[i] = kNetConnDisconnected;
            LOG_WARNING(_T("%s 配置无效（地址 / 端口不合法），直接判为未连接。"),
                        NetServerDisplayName(i));
            continue;
        }

        m_connState[i] = kNetConnConnecting;
        if (!StartProbe(i, entry.host, entry.port, WM_NETPROBE_RESULT))
        {
            // 线程起不来就地判负，否则这张卡会永远停在"连接中"。
            m_connState[i] = kNetConnDisconnected;
            LOG_ERROR(_T("%s 的探测线程创建失败。"), NetServerDisplayName(i));
        }
    }

    RefreshCards();
}

bool CBalloonNetSettingsWindow::StartProbe(int kind, LPCTSTR host, int port,
                                           UINT resultMsg)
{
    NetTestTask* task = new NetTestTask();
    task->hwnd       = m_hWnd;
    task->resultMsg  = resultMsg;
    task->kind       = kind;
    task->host       = host;
    task->port       = port;
    task->timeoutSec = kTestConnectTimeoutSec;
    task->ok         = false;

    HANDLE hThread =
        (HANDLE)::_beginthreadex(NULL, 0, ProbeThreadProc, task, 0, NULL);
    if (hThread == NULL)
    {
        delete task;
        return false;
    }
    ::CloseHandle(hThread);
    return true;
}

unsigned __stdcall CBalloonNetSettingsWindow::ProbeThreadProc(void* pParam)
{
    NetTestTask* task = static_cast<NetTestTask*>(pParam);
    if (task == NULL)
        return 0;

    // 后台线程只做 socket 探测 + 回投消息，绝不直接碰窗口 / 控件。
    task->ok = CIUSocket::GetInstance().TestConnect(
        task->host, (short)task->port, task->timeoutSec);

    // 把任务作为 LPARAM 回投本窗口；窗口若已销毁，PostMessage 失败 ——
    // 此时由本线程自行回收任务，避免内存泄漏。
    if (!::PostMessage(task->hwnd, task->resultMsg, 0, (LPARAM)task))
        delete task;

    return 0;
}

void CBalloonNetSettingsWindow::ApplyProbeResult(int kind, LPCTSTR host, int port,
                                                 bool ok)
{
    if (kind < 0 || kind >= kNetServerCount)
    {
        return;
    }

    // 探测是异步的，这 3 秒内用户可能切卡提交了新编辑、或保存了新地址。
    // 结果对不上当前配置就是过期结论，丢弃并按新值重探一次，避免卡片
    // 停在"连接中"下不来，也避免显示旧地址的探测结果。
    const NetServerEntry& entry = m_model.Server(kind);
    if (entry.host.CompareNoCase(host) != 0 || entry.port != port)
    {
        LOG_INFO(_T("%s 的探测结果已过期（探的是 %s:%d，现配置为 %s:%d），重新探测。"),
                 NetServerDisplayName(kind), host, port,
                 (LPCTSTR)entry.host, entry.port);
        m_connState[kind] = kNetConnConnecting;
        if (!StartProbe(kind, entry.host, entry.port, WM_NETPROBE_RESULT))
        {
            m_connState[kind] = kNetConnDisconnected;
        }
        RefreshCards();
        return;
    }

    m_connState[kind] = ok ? kNetConnConnected : kNetConnDisconnected;
    RefreshCards();

    LOG_INFO(_T("%s 探测完成：%s:%d %s。"), NetServerDisplayName(kind),
             host, port, ok ? _T("可连通") : _T("连不上"));
}

LRESULT CBalloonNetSettingsWindow::OnProbeResult(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                 LPARAM lParam, BOOL& /*bHandled*/)
{
    NetTestTask* task = reinterpret_cast<NetTestTask*>(lParam);
    if (task == NULL)
    {
        return 0;
    }

    // 开窗自动探测：只更新卡片，不弹任何对话框。
    ApplyProbeResult(task->kind, task->host, task->port, task->ok);
    delete task;
    return 0;
}

LRESULT CBalloonNetSettingsWindow::OnTestResult(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                LPARAM lParam, BOOL& /*bHandled*/)
{
    NetTestTask* task = reinterpret_cast<NetTestTask*>(lParam);

    // 退出"测试中"状态：恢复按钮文字、放开重入
    m_testing = false;
    SetTestButtonText(_T("测试连接"));

    if (task != NULL)
    {
        // 按钮发起的探测同样回写卡片状态 —— 用户刚测过的结论应当留在卡片上。
        ApplyProbeResult(task->kind, task->host, task->port, task->ok);

        CString msg;
        if (task->ok)
        {
            msg.Format(_T("已连通 %s:%d。"), (LPCTSTR)task->host, task->port);
            ::MessageBox(m_hWnd, msg, _T("网络设置"), MB_OK | MB_ICONINFORMATION);
        }
        else
        {
            msg.Format(_T("无法连接 %s:%d。\n\n请检查地址 / 端口，或确认服务器是否已启动。"),
                       (LPCTSTR)task->host, task->port);
            ::MessageBox(m_hWnd, msg, _T("网络设置"), MB_OK | MB_ICONWARNING);
        }
        delete task;
    }
    return 0;
}

void CBalloonNetSettingsWindow::SetTestButtonText(LPCTSTR text)
{
    DuiControl* content = GetClientContent();
    if (content == NULL)
        return;

    DuiButton* btn = dynamic_cast<DuiButton*>(content->FindCtrlById(kIdBtnTest));
    if (btn != NULL)
    {
        btn->SetText(text);
        btn->Invalidate();
    }
}

void CBalloonNetSettingsWindow::DoSave()
{
    CommitFormToModel();
    SaveModelToIni();
    RefreshCards();

    // 地址 / 端口可能刚被改过，按新值重新探一遍三组服务器的可达性。
    StartProbeAll();

    // 写回 ini 后，把三组服务器配置同步推进运行中的 CIUSocket，使本次
    // 运行内（无需重启）登录即用新配置 —— CIUSocket 的 m_strServer /
    // m_nPort 是启动时 LoadConfig 一次性载入的，只写 ini 不会刷新它。
    // CFlamingoClient::SetXxx 内部转调 CIUSocket::SetXxx，并会 Close() 掉
    // 旧连接；网络设置在登录前打开、此刻尚未连接，无副作用。
    if (m_lpFMGClient != NULL)
    {
        const NetServerEntry& chat  = m_model.Server(kNetServerChat);
        const NetServerEntry& image = m_model.Server(kNetServerImage);
        const NetServerEntry& file  = m_model.Server(kNetServerFile);
        m_lpFMGClient->SetServer(chat.host);
        m_lpFMGClient->SetPort((short)chat.port);
        m_lpFMGClient->SetImgServer(image.host);
        m_lpFMGClient->SetImgPort((short)image.port);
        m_lpFMGClient->SetFileServer(file.host);
        m_lpFMGClient->SetFilePort((short)file.port);
    }

    ::MessageBox(m_hWnd,
                 _T("服务器设置已保存，本次登录即生效。"),
                 _T("网络设置"), MB_OK | MB_ICONINFORMATION);
}

// =======================================================================
// 消息处理
// =======================================================================

LRESULT CBalloonNetSettingsWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                              LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 模态接入登录流程时：仅结束模态循环，窗口由 DoModal 统一销毁，
    // 控制权回到登录窗。
    if (m_modalRunning)
    {
        EndModal(IDCANCEL);
        return 0;
    }

    // 调试入口（--netsettings）下，本窗口是唯一可见窗口，关闭即退出
    // 程序，交由 CMainDlg 统一收尾。
    if (m_hMainDlg != NULL && ::IsWindow(m_hMainDlg))
    {
        ::PostMessage(m_hMainDlg, WM_CLOSE_MAINDLG, 0, 0);
    }
    if (IsWindow())
    {
        DestroyWindow();
    }
    return 0;
}

LRESULT CBalloonNetSettingsWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                               LPARAM lParam, BOOL& bHandled)
{
    const DuiNotify* pNotify = reinterpret_cast<const DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }

    // 卡片 / 按钮的点击。
    if (pNotify->code == DUIN_CLICK)
    {
        switch (pNotify->ctrlId)
        {
        case kIdCardChat:
            SwitchToServer(kNetServerChat);
            return 0;
        case kIdCardImage:
            SwitchToServer(kNetServerImage);
            return 0;
        case kIdCardFile:
            SwitchToServer(kNetServerFile);
            return 0;
        case kIdBtnTest:
            DoTestConnection();
            return 0;
        case kIdBtnSave:
            DoSave();
            return 0;
        default:
            break;
        }
    }

    // 其它通知交回 DuiFrameWindow 链。
    bHandled = FALSE;
    return 0;
}
