/**
 *  远程桌面命令行调试入口的实现，见 RdDebugEntries.h。各入口的说明照搬自迁移前的 exe App/DebugEntryPoints.cpp。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "RdDebugEntries.h"

#include <wchar.h>
#include <string>
#include "RdRelaySelfTest.h"       //--rdrelayselftest
#include "DxgiScreenCapturer.h"    //--dxgicap
#include "RdScreenCapturer.h"      //--dxgipipe / --dxgipipegdi
#include "../../Source/Common/Log/IULog.h"

namespace remotedesktop {

#ifdef _DEBUG
//中转自测连接的 imgserver 地址与端口（本机开发服务端的中转端口），以及签发令牌用的密钥
//（须与 imgserver.conf 的 relaytokenkey 一致）
static const char* kSelfTestRelayIp   = "127.0.0.1";
static const int   kSelfTestRelayPort = 20003;
static const char* kSelfTestTokenKey  = "flamingo-relay-token-secret";

//--rddlg 的假数据：对端昵称与 userid、被控端授权形态用的会话 id
static const TCHAR* kMockPeerName  = _T("林知夏");
static const int64_t kMockPeerId   = 700002;
static const char*  kMockSessionId = "mock-rd-session";

//--rdbar 的假数据：控制方昵称
static const wchar_t* kMockControllerName = L"张三";

//命令行里是否带某个开关
static bool HasSwitch(const wchar_t* commandLine, const wchar_t* sw)
{
    return ::wcsstr(commandLine, sw) != NULL;
}

//把 exe 所在目录下的某个文件名拼成完整路径（--dxgicap 等把结果图存到 exe 同目录，与迁移前一致）
static std::wstring PathNextToExe(const wchar_t* fileName)
{
    wchar_t path[MAX_PATH] = { 0 };
    ::GetModuleFileNameW(NULL, path, MAX_PATH);
    wchar_t* slash = ::wcsrchr(path, L'\\');
    if (slash != NULL)
    {
        *(slash + 1) = L'\0';
        return std::wstring(path) + fileName;
    }
    return std::wstring(fileName);
}
#endif

CRdDebugEntries::CRdDebugEntries()
{
}

CRdDebugEntries::~CRdDebugEntries()
{
    DestroyWindows();
}

void CRdDebugEntries::DestroyWindows()
{
    //逐个销毁：窗口过程都在本 DLL 里，DLL 卸载之前必须全部销毁
    if (m_rdDlg.IsWindow())
    {
        m_rdDlg.CloseDlg();
    }
    m_selfTestWindow.Destroy();
    if (m_controlledBar.IsWindow())
    {
        m_controlledBar.Hide();
    }
    if (m_toolbar.IsWindow())
    {
        m_toolbar.Hide();
    }
    if (IsWindow())
    {
        DestroyWindow();
    }
}

LRESULT CRdDebugEntries::OnControlledEndByUserMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    LOG_INFO("RdDebugEntries: controlled bar end clicked, hide the preview bar.");
    m_controlledBar.Hide();
    return 0;
}

int CRdDebugEntries::Run(const wchar_t* commandLine, HWND hwndHost, flamingomedia::IMediaHost* host)
{
#ifdef _DEBUG
    if (commandLine == NULL)
    {
        return flamingomedia::media_debug_not_handled;
    }
    //--rddlg：弹出远程桌面会话对话框（假数据），核对它贴桌面右下角的落点、置顶效果与标题栏按钮。
    //默认弹被控端那一路（「XXX 请求远程控制你的电脑」，拒绝 / 接受两个按钮）；再带 --calling 则弹发起方那一路
    //（「正在呼叫 XXX…」，只有取消按钮）。两路的显示收尾都走 ShowAtWorkAreaCorner，位置与置顶行为一致。
    if (HasSwitch(commandLine, L"--rddlg"))
    {
        if (HasSwitch(commandLine, L"--calling"))
        {
            m_rdDlg.OpenCalling(hwndHost, kMockPeerName, kMockPeerId);
        }
        else
        {
            m_rdDlg.OpenAuth(hwndHost, kMockPeerName, std::string(kMockSessionId), kMockPeerId);
        }
        return flamingomedia::media_debug_handled;
    }
    //--rdpselftest：远程桌面引擎自环测试窗口（抓本机屏 → 编码 → 解码 → 绘制），目视确认引擎闭环出画面
    if (HasSwitch(commandLine, L"--rdpselftest"))
    {
        m_selfTestWindow.CreateAndShow(hwndHost);
        return flamingomedia::media_debug_handled;
    }
    //--rdrelayselftest：进程内开两条 relay 连接连 imgserver 中转端口，互发字节验证「连接 → 握手 → 双向透传」
    //整条中转通路。需 imgserver 在跑、且其 relaytokenkey 与 kSelfTestTokenKey 一致。
    if (HasSwitch(commandLine, L"--rdrelayselftest"))
    {
        bool selfTestOk = false;
        const unsigned char* serverPubKey = (host != NULL) ? host->GetServerPublicKey() : NULL;
        std::string report = RunRdRelaySelfTest(kSelfTestRelayIp, kSelfTestRelayPort, kSelfTestTokenKey,
                                                serverPubKey, selfTestOk);
        CString text(CA2W(report.c_str(), CP_UTF8));
        ::MessageBox(hwndHost, text, _T("远程桌面中转自环自测"), MB_OK | (selfTestOk ? MB_ICONINFORMATION : MB_ICONWARNING));
        return flamingomedia::media_debug_handled;
    }
    //--rdbar：在屏幕顶部弹出被控端受控提示条，核对外观 / 布局 / 结束按钮（点「结束控制」会关掉提示条）
    if (HasSwitch(commandLine, L"--rdbar"))
    {
        if (!IsWindow() && Create(HWND_MESSAGE) == NULL)
        {
            LOG_ERROR("RdDebugEntries: create message window failed, err=%lu.", ::GetLastError());
        }
        m_controlledBar.Show(kMockControllerName, m_hWnd);
        return flamingomedia::media_debug_handled;
    }
    //--rdtoolbar：在主显示器顶部弹出控制端全屏工具条，核对外观 / 布局 / 两个按钮（实际运行时它只在观看窗全屏态出现）。
    //命令消息传 WM_NULL：点了按钮也只是给宿主窗口投递一条无副作用的空消息。
    if (HasSwitch(commandLine, L"--rdtoolbar"))
    {
        HMONITOR mon = ::MonitorFromWindow(hwndHost, MONITOR_DEFAULTTOPRIMARY);
        m_toolbar.ShowOnMonitor(mon, hwndHost, WM_NULL);
        return flamingomedia::media_debug_handled;
    }
    //--dxgicap：用 DXGI Desktop Duplication 抓一帧屏幕存成 exe 同目录的 dxgicap.bmp，弹框报结果。
    //用于在本机显卡上验证 DuplicateOutput 能否跑通（不同显卡 / 虚拟机 / 远程登录下行为差异大）。
    if (HasSwitch(commandLine, L"--dxgicap"))
    {
        std::wstring msg;
        bool ok = CDxgiScreenCapturer::DebugCaptureToBmp(PathNextToExe(L"dxgicap.bmp").c_str(), msg);
        ::MessageBoxW(hwndHost, msg.c_str(), L"DXGI 抓屏自测", MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
        return flamingomedia::media_debug_handled;
    }
    //--dxgipipegdi：强制走 GDI 降级后端跑同一闭环，存 dxgipipegdi.bmp，验证 GDI 抓取出图正确。
    //须在 --dxgipipe 之前判断（--dxgipipegdi 含 "--dxgipipe" 子串）。
    if (HasSwitch(commandLine, L"--dxgipipegdi"))
    {
        std::wstring msg;
        bool ok = RdScreenCapturer::DebugPipelineToBmp(PathNextToExe(L"dxgipipegdi.bmp").c_str(), msg, true);
        ::MessageBoxW(hwndHost, msg.c_str(), L"GDI 降级抓屏链路闭环自测", MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
        return flamingomedia::media_debug_handled;
    }
    //--dxgipipe：本地走通整条抓屏链路（DXGI / GDI 抓 → 16 位编码 → DIFF → 解码 → 绘制），还原画面存成 exe
    //同目录的 dxgipipe.bmp，不连服务器即可确认编码链路出图正确（方向 / 颜色 / 光标）。后端走哪条看客户端日志。
    if (HasSwitch(commandLine, L"--dxgipipe"))
    {
        std::wstring msg;
        bool ok = RdScreenCapturer::DebugPipelineToBmp(PathNextToExe(L"dxgipipe.bmp").c_str(), msg);
        ::MessageBoxW(hwndHost, msg.c_str(), L"DXGI 抓屏链路闭环自测", MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
        return flamingomedia::media_debug_handled;
    }
    return flamingomedia::media_debug_not_handled;
#else
    (void)commandLine;
    (void)hwndHost;
    (void)host;
    return flamingomedia::media_debug_not_handled;
#endif
}

} // namespace remotedesktop
