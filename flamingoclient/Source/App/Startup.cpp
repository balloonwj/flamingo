/**
* 程序入口函数
* zhangyl 2017.03.16
**/

#include "stdafx.h"

#include "..\resource.h"
#include "Startup.h"

#include "AppShell.h"
#include "..\Common\Util\File2.h"
#include "..\Common\Log\IULog.h"
#include "..\Session\UserSessionData.h"
#include "Crash.h"                    // ConfigureCrashDumpEnvironment（转储目录 + WER 配置）
#include "HangWatchdog.h"             // CHangWatchdog：界面线程无响应时自动写转储（bugs.md BUG-120）
#include "..\Common\Util\Utils.h"
#include "..\Common\Util\IniFile.h"
#include "../net/StorageLocation.h"      // ResolveDataRoot（本地数据根目录的归一）
#include "../net/FrameCrypto.h"          // net::cryptoInit（协议加密的一次性初始化）
#include "UIText.h"
#include "BrandText.h"                // 品牌文字与注册默认公司的读取（[ui] apptitle / slogan / defaultcompany 等）
#include "../Common/I18n/I18n.h"      // 界面语言（[ui] language）与可选语言包 FlamingoLang.dll
#include "AppRestart.h"               // 重启时新进程先等旧进程退出（--wait-pid）
#include "..\Common\Log\LogCleanupPolicy.h"         // 「启动时清空历史日志」开关的取值规则
#include "../../third-party/balloonui/balloonui/DuiDpi.h"
#include "../../third-party/balloonui/balloonui/DuiTextResolver.h"   // SetTextResolver：balloonui 内置文字的转换回调
#include "../Common/I18n/BalloonUiText.h"   // ResolveBalloonUiText：balloonui 内置文字的译文
#include "../../third-party/balloonui/balloonui/DuiTheme.h"   // SetDefaultFontFace：balloonui 的默认字体随界面语言
#include "UrlProtocolRegistry.h"                  // RegisterFlamingoUrlProtocol：注册 flamingo:// 协议
#include "../UI/BalloonMain/GroupLinkFlow.h"      // FindJoinUrlInCommandLine / kCopyDataJoinGroupLink：启动参数里的加群链接

CAppModule _Module;

//关于g_hwndOwner的说明：
//之所以创建这个窗口为为了产生主窗口和登录对话框不在任务栏显示的效果，
//如果单纯地将窗口风格由WS_EX_APPWINDOW设置为WS_EX_TOOLWINDOW后，此种风格的窗口
//会在失去焦点后默认Z轴变为0，这点特别讨厌。
HWND	   g_hwndOwner = NULL;

//启动命令行里的加群链接（docs/邀请入群方案.md §12）：本进程是唯一的客户端时，留给登录成功后处理；没有时为空
static std::wstring s_pendingJoinUrl;

//把加群链接交给已运行的客户端时，等对方处理 WM_COPYDATA 的上限（毫秒）
static const UINT kCopyDataTimeoutMs = 5000;

//代理窗的窗口类名（与 CFlamingoClient 注册的一致），用来找已运行的客户端
static const TCHAR kProxyWindowClass[] = _T("FLAMINGO_PROXY_WND");

/**
 *  命令行里有加群链接、且已有客户端在运行时，把链接交给它（WM_COPYDATA 发给它的代理窗）。
 *    url：加群链接。
 *  @return 已交给运行中的客户端返回 true，本进程随即退出；没有在运行的客户端返回 false
 */
static bool ForwardJoinUrlToRunningInstance(const std::wstring& url)
{
    HWND hRunning = ::FindWindow(kProxyWindowClass, NULL);
    if (hRunning == NULL)
    {
        return false;
    }
    //本进程由用户点击启动、可以前置窗口：把前置的权利让给运行中的客户端，它弹出的确认框才能到最前面
    DWORD runningPid = 0;
    ::GetWindowThreadProcessId(hRunning, &runningPid);
    if (runningPid != 0)
    {
        ::AllowSetForegroundWindow(runningPid);
    }
    COPYDATASTRUCT cds;
    cds.dwData = balloonmain::kCopyDataJoinGroupLink;
    cds.cbData = (DWORD)((url.size() + 1) * sizeof(wchar_t));
    cds.lpData = (PVOID)url.c_str();
    DWORD_PTR result = 0;
    const LRESULT sent = ::SendMessageTimeout(hRunning, WM_COPYDATA, 0, (LPARAM)&cds, SMTO_ABORTIFHUNG,
                                              kCopyDataTimeoutMs, &result);
    LOG_INFO("join link: forwarded to the running instance (pid %lu), sent=%d, result=%d.", runningPid,
             sent != 0 ? 1 : 0, (int)result);
    return sent != 0 && result != 0;
}

HWND CreateOwnerWindow()
{
    PCTSTR pszOwnerWindowClass = _T("__Flamingo_Owner__");
    HINSTANCE hInstance = ::GetModuleHandle(NULL);
    WNDCLASSEX wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = DefWindowProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = NULL;
    wcex.hCursor = NULL;
    wcex.hbrBackground = NULL;
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = pszOwnerWindowClass;
    wcex.hIconSm = NULL;

    if (!::RegisterClassEx(&wcex))
        return NULL;

    HWND hOwnerWindow = ::CreateWindow(pszOwnerWindowClass, NULL, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, hInstance, NULL);

    return hOwnerWindow;
}

int Run(LPTSTR /*lpstrCmdLine = NULL*/, int nCmdShow/* = SW_SHOWDEFAULT*/)
{
    CMessageLoop theLoop;
    _Module.AddMessageLoop(&theLoop);

    g_hwndOwner = CreateOwnerWindow();

    //界面线程无响应检测（bugs.md BUG-120）：g_hwndOwner 在界面线程上、由 DefWindowProc 处理消息，用它作探测目标。
    //界面线程连续无响应 15 秒时自动写一份转储到 Users\dump，事后据此查看各线程的堆栈
    balloonapp::CHangWatchdog hangWatchdog;
    if (g_hwndOwner != NULL)
    {
        hangWatchdog.Start(g_hwndOwner, PrepareCrashDumpDirectory(), balloonapp::HangWatchConfig());
    }

    //nCmdShow 不予理会：本程序没有「主窗口」这个概念上的启动窗口 —— 未登录时可见的是
    //登录窗，登录后是主面板，两者的显示时机都由登录流程决定。
    UNREFERENCED_PARAMETER(nCmdShow);

    balloonapp::CAppShell shell;
    //启动命令行里的加群链接：交给进程外壳，主面板建好后转交，登录成功后处理
    shell.SetPendingJoinUrl(s_pendingJoinUrl);
    int nRet = 0;
    if (shell.Startup(g_hwndOwner))
    {
        nRet = theLoop.Run();
    }
    //Startup 返回 false（调试入口要求立即退出、或用户直接关掉了登录窗）时也要收尾。
    shell.Shutdown();
    //退出收尾也在检测范围内，收尾完成后再停
    hangWatchdog.Stop();

    _Module.RemoveMessageLoop();
    return nRet;
}

// 注意本函数<u>没有任何时间判断</u>：它把 Logs 目录下该后缀的文件<u>全部</u>删掉，
// 只有本次运行的那一份因为正被自己占用着删不掉，于是留了下来。也就是说它的真实
// 语义是「清空历史日志，只保留本次这一份」，函数名里的"过期"没有对应的实现。
// 名字保留是因为配置项键名（clearexpirelog）已经在用户的配置文件里了，一并改动
// 会让老配置里的用户选择失效。
void ClearExpiredLog(PCTSTR pszFileSuffixName)
{
    if (pszFileSuffixName == NULL)
        return;

    WIN32_FIND_DATA win32FindData = { 0 };
    TCHAR szLogFilePath[MAX_PATH] = { 0 };
    _stprintf_s(szLogFilePath, MAX_PATH, _T("%sLogs\\*.%s"), g_szHomePath, pszFileSuffixName);
    HANDLE hFindFile = ::FindFirstFile(szLogFilePath, &win32FindData);
    if (hFindFile == INVALID_HANDLE_VALUE)
        return;

    do
    {
        //跳过目录项 "." 与 ".."。这里原先写的是 ||，两个条件不可能同时成立，整个
        //判断恒为真；因为通配符是 "*.<后缀>"、两个目录项根本匹配不到，所以一直没
        //造成后果。2026-08-17 顺手改正为 &&。
        if (_tcsicmp(win32FindData.cFileName, _T(".")) != 0 &&
            _tcsicmp(win32FindData.cFileName, _T("..")) != 0)
        {
            memset(szLogFilePath, 0, sizeof(szLogFilePath));
            _stprintf_s(szLogFilePath, MAX_PATH, _T("%sLogs\\%s"), g_szHomePath, win32FindData.cFileName);
            //这里不用检测是否删除成功,因为最新的一个log是我们需要的,不能删除,它正被此进程占用着,所以删不掉
            ::DeleteFile(szLogFilePath);
        }

        if (!::FindNextFile(hFindFile, &win32FindData))
            break;

    } while (true);

    ::FindClose(hFindFile);
}


BOOL InitSocket()
{
    WORD wVersionRequested = MAKEWORD(2, 2);
    WSADATA wsaData;
    int nErrorID = ::WSAStartup(wVersionRequested, &wsaData);
    if (nErrorID != 0)
        return FALSE;

    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
    {
        UnInitSocket();
        return FALSE;
    }

    return TRUE;
}

void UnInitSocket()
{
    ::WSACleanup();
}

int WINAPI _tWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPTSTR lpstrCmdLine, int nCmdShow)
{
    //声明本进程为 per-monitor DPI 感知。必须放在整个函数最前面：进程的 DPI 感知级别
    //一旦有窗口创建就无法再更改。
    //
    //不声明的后果是进程被判定为 DPI unaware，系统改为把整个窗口的位图拉伸到目标 DPI
    //（125% 缩放下就是整体放大 1.25 倍并发虚）。此前之所以看起来正常，是因为开发机上
    //手工给 Bin\Flamingo.exe 勾了「替代高 DPI 缩放行为：应用程序」，该设置记在注册表
    //HKCU\...\AppCompatFlags\Layers 下、且<u>按 exe 完整路径匹配</u> —— 换个输出目录、
    //换台机器、或改个文件名，全都会立刻退回被拉伸的状态。实测：同一份二进制改个文件名
    //后，DPI 感知级别就从 PER_MONITOR 掉回 UNAWARE，主窗口由 338x727 被拉伸成 420x899。
    //
    //balloonui 的各个 Demo（DuiGallery / XChat 等）都在 WinMain 开头调了这一句，
    //主客户端此前是漏掉的。
    balloonwjui::DuiDpi::OptInPerMonitorV2();

#ifdef _DEBUG
    _CrtSetDebugFillThreshold(0);
#endif
    _tcscpy_s(g_szHomePath, MAX_PATH, Hootina::CPath::GetAppPath().c_str());
    strcpy_s(g_szHomePathAscii, MAX_PATH, Hootina::CPath::GetAppPathAscii().c_str());

    //本地数据根目录：读 config\flamingo.ini 的 [storage] datapath，经 ResolveDataRoot
    //归一（未配置、相对路径、含上跳的一律回落到程序目录）。必须在这里算 —— 日志、
    //各个库与缓存目录都要用它，比登录还早。用户改了这一项须重启才生效，正是因为它
    //在此处只算一次。
    {
        TCHAR szIniPath[MAX_PATH] = { 0 };
        _stprintf_s(szIniPath, MAX_PATH, _T("%sconfig\\flamingo.ini"), g_szHomePath);

        TCHAR szConfigured[MAX_PATH] = { 0 };
        CIniFile iniFile;
        iniFile.ReadString(_T("storage"), _T("datapath"), _T(""),
                           szConfigured, MAX_PATH, szIniPath);

        const std::wstring dataRoot = net::ResolveDataRoot(szConfigured, g_szHomePath);
        _tcscpy_s(g_szDataRoot, MAX_PATH, dataRoot.c_str());
    }

    //日志文件名由日志模块在此前缀后追加"年月日时分秒.进程 id.log"，同一目录下
    //多个进程的日志据此区分；单个文件写满 10 MB 后自动另起一个
    TCHAR szLogFilePathPrefix[MAX_PATH] = { 0 };
    _stprintf_s(szLogFilePathPrefix, MAX_PATH, _T("%sLogs\\flamingo"), g_szHomePath);
    CIULog::Init(true, false, szLogFilePathPrefix);

    //加群链接（docs/邀请入群方案.md §12）：系统按 flamingo:// 协议启动本程序时，链接在命令行里。已有客户端在运行
    //就交给它并退出；没有时记下来，登录成功后由主面板处理
    {
        const std::wstring joinUrl = balloonmain::FindJoinUrlInCommandLine(::GetCommandLineW());
        if (!joinUrl.empty())
        {
            if (ForwardJoinUrlToRunningInstance(joinUrl))
            {
                CIULog::Uninit();
                return 0;
            }
            s_pendingJoinUrl = joinUrl;
            LOG_INFO("join link: no running instance, the link is kept until login.");
        }
    }

    //把 flamingo:// 注册为当前用户的协议，指向本程序（已是本程序时不重复写）
    {
        TCHAR szExePath[MAX_PATH] = { 0 };
        if (::GetModuleFileName(NULL, szExePath, MAX_PATH) > 0)
        {
            apputil::RegisterFlamingoUrlProtocol(szExePath);
        }
    }

    //由「立即重启」启动的新进程：命令行带着旧进程的 pid，先等它退出再往下走，避免新旧两个进程
    //同时读写配置与本地数据库（见 App/AppRestart.h）。等待有上限，旧进程卡住时照常启动。
    {
        DWORD previousPid = apprestart::ParseWaitPid(lpstrCmdLine);
        if (previousPid != 0)
        {
            bool exited = apprestart::WaitForProcessExit(previousPid, apprestart::kWaitPreviousMs);
            LOG_INFO("restart: waited for the previous instance (pid %lu), %s.", previousPid,
                     exited ? "it has exited" : "timed out, starting anyway");
        }
    }

    //TODO: 统一到AppConfig类中去
    CIniFile iniFile;
    CString strIniFilePath(g_szHomePath);
    strIniFilePath += _T("config\\flamingo.ini");

    //界面语言：读 [ui] language（没有这一项或为空表示跟随系统界面语言），需要时加载 exe 同目录下的
    //语言包 FlamingoLang.dll（规则见 Common/I18n/I18n.h、docs/多语言方案.md）。必须在创建任何窗口、
    //取用任何界面文字之前完成——此后各处 TR 取到的都是这里定下的语言，切换语言要重启才生效。
    //语言包不存在或加载失败时一律退回简体中文，不影响启动。
    {
        TCHAR szLanguage[MAX_PATH] = { 0 };
        iniFile.ReadString(i18n::kIniSection, i18n::kIniKey, _T(""), szLanguage, MAX_PATH, strIniFilePath);
        std::wstring langDllPath = std::wstring(g_szHomePath) + FLAMINGO_LANG_DLL_NAME;
        i18n::InitResult langResult = i18n::Init(szLanguage, langDllPath, ::GetUserDefaultUILanguage());
        LOG_INFO("%s", i18n::DescribeInitResult(langResult).c_str());
        if (i18n::GetEarlyLookupCount() > 0)
            LOG_WARNING("i18n: %u text lookups happened before the language was decided.", i18n::GetEarlyLookupCount());
        //balloonui 的默认字体也换成当前界面语言的界面字体（多语言第 3e 步），各控件的默认字体由它决定
        balloonwjui::DuiTheme::Inst().SetDefaultFontFace(i18n::UiFontName());
        //balloonui 自带的文字（输入框右键菜单等）也经语言模块翻译（多语言第 3d 步）
        balloonwjui::SetTextResolver(&i18n::ResolveBalloonUiText);
    }

    //产品名称、标语、版权文字与注册窗的默认公司（[ui] apptitle / slogan / copyrighttext /
    //defaultcompany，读取与编码识别规则见 BrandText.h）。放在这里读，是因为下面加密库初始化
    //失败时弹出的提示框就要用产品名称做标题，各窗口的标题、登录窗与注册窗的品牌文字、关于
    //对话框、注册窗「公司名称」的默认项也都取自这几个全局变量。
    g_strAppTitle = brandtext::ReadAppTitle(strIniFilePath);
    g_strAppSlogan = brandtext::ReadSlogan(strIniFilePath);
    g_strAppCopyright = brandtext::ReadCopyright(strIniFilePath);
    g_strDefaultCompany = brandtext::ReadDefaultCompany(strIniFilePath);
    LOG_INFO("brand: apptitle %s, slogan %s, copyright %s, defaultcompany %s.",
             (g_strAppTitle == brandtext::kDefaultAppTitle) ? "default" : "customized",
             (g_strAppSlogan == brandtext::kDefaultSlogan) ? "default"
                 : (g_strAppSlogan.empty() ? "hidden" : "customized"),
             (g_strAppCopyright == brandtext::kDefaultCopyright) ? "default" : "customized",
             (g_strDefaultCompany == brandtext::kDefaultCompany) ? "default" : "customized");
    //没有配置标语时用的内置标语要随界面语言翻译（BrandText.cpp 只做标记）。放在写日志之后，
    //上面「default / customized」的判断比的仍是原文
    if (g_strAppSlogan == brandtext::kDefaultSlogan)
        g_strAppSlogan = TR(brandtext::kDefaultSlogan);

    //「启动时清空历史日志」：取值规则（含配置项缺失 / 被写坏时的默认值）收口在
    //LogCleanupPolicy 里，系统设置的存储页改的也是同一项，两边不会漂移。
    //Debug 构建默认关闭（开发期要能翻前几次运行的日志），Release 默认开启。
    if (logcleanup::ReadClearLogOnStart(strIniFilePath))
    {
        //清空历史日志，只保留本次运行的这一份（正被本进程占用着，删不掉）。
        ClearExpiredLog(_T("log"));
        //性能计数功能已于 2026-08-17 移除、不再产生 .perf 文件；这一句留着是为了
        //把历史遗留的那些一并清掉，等用户机器上不再有残留后可以去掉。
        ClearExpiredLog(_T("perf"));
    }

    //日志级别
    //取值 0 输出全部，1 只输出 WARNING 与 ERROR，2 只输出 ERROR，换算见 CIULog::SetLevel
    int nLogLevel = iniFile.ReadInt(_T("app"), _T("loglevel"), 0, strIniFilePath);
    CIULog::SetLevel(nLogLevel);

    //建好转储目录；若本次<u>恰好</u>以管理员权限运行，顺手把 WER LocalDumps 配置写进注册表。
    //未提权时静默跳过——不弹界面、不请求提权（详见 docs/崩溃转储方案.md）。放在这里是因为
    //它依赖 g_szHomePath 拼目录，又需要日志已初始化才记得下那两条说明。
    ConfigureCrashDumpEnvironment();

    //初始化底层密码学库（libsodium）。协议加密的握手与帧体加解密全都依赖它，
    //而它<u>必须在任何工作线程创建之前调用</u>——内部要做一次性的随机源与 CPU 特性
    //探测，多线程环境下首次调用不是线程安全的。放在这里正好早于 InitSocket 与
    //后面各个网络线程的启动。失败时直接终止启动：此时加密不可用，而服务端开着
    //加密的话哪条链路也连不上，继续跑只会得到一堆莫名其妙的连接失败。
    if (!net::cryptoInit())
    {
        LOG_ERROR("sodium_init failed, protocol encryption is unavailable.");
        ::MessageBox(NULL, TR(UI_MSG_CRYPTO_INIT_FAILED), g_strAppTitle.c_str(), MB_OK | MB_ICONERROR);
        return 0;
    }

    if (!InitSocket())
        return 0;

    //tstring strFileName = Hootina::CPath::GetAppPath() + _T("richFace.dll");
    //BOOL bRet = DllRegisterServer(strFileName.c_str());	// 注册COM组件
    //if (!bRet)
    //{
    //	::MessageBox(NULL, _T("COM组件注册失败，应用程序无法完成初始化操作！"), _T("提示"), MB_OK);
    //	return 0;
    //}

    HRESULT hRes = ::OleInitialize(NULL);
    ATLASSERT(SUCCEEDED(hRes));

    // this resolves ATL window thunking problem when Microsoft Layer for Unicode (MSLU) is used
    ::DefWindowProc(NULL, 0, 0, 0L);

    AtlInitCommonControls(ICC_BAR_CLASSES);	// add flags to support other controls
    HMODULE hRichEditDll = ::LoadLibrary(CRichEditCtrl::GetLibraryName());	// 加载RichEdit控件DLL

    hRes = _Module.Init(NULL, hInstance);
    ATLASSERT(SUCCEEDED(hRes));




    int nRet = Run(lpstrCmdLine, nCmdShow);


    if (hRichEditDll != NULL)		// 卸载RichEdit控件DLL
    {
        ::FreeLibrary(hRichEditDll);
        hRichEditDll = NULL;
    }

    _Module.Term();

    UnInitSocket();

    CIULog::Uninit();

    ::OleUninitialize();

    return nRet;
}
