/**
 *  更新器 FlamingoUpdater 的入口，Main.cpp
 *
 *  执行流程（见 docs/客户端自动更新方案.md 6.5）：
 *    ① 按 --lang 确定界面语言（多语言第 3c 步），解析命令行、打开日志；
 *    ② 按 PID 等主进程退出（超时则提示用户手动关闭后重试，<u>绝不 TerminateProcess</u>）；
 *    ③ 交给 Installer 完成"校验 → 备份 → 覆盖"，失败自动回滚；
 *    ④ 成功后重新启动主程序。
 *
 *  与老版本的几处关键差别：
 *    · 老版本遍历进程列表按名字找 Flamingo.exe，多开或改名就失灵；现在按 PID 等。
 *    · 老版本只等 5 秒，且超时后照样往下走 —— 主进程还没退出就开始覆盖文件，必然失败；
 *      现在等足 30 秒，超时就如实报错退出，绝不动文件。
 *    · 老版本没有任何日志与回滚，失败后客户端可能起不来。
 *
 *  balloonwj@qq.com   2026-08-12
 */
#include <windows.h>
#include <shellapi.h>   // CommandLineToArgvW

#include <string>
#include <vector>

#include "CommandLine.h"
#include "Installer.h"
#include "UpdateLog.h"
#include "../Source/App/BrandText.h"   // ReadAppTitle（提示框里的产品名称）
#include "../Source/Common/I18n/I18n.h"   // TR / TRF 与 i18n::Init：提示框文字的多语言

namespace {

//等待主进程退出的超时（毫秒）。老版本只等 5 秒，实测不够——主程序退出前还要保存
//会话、关闭数据库。给到 30 秒，超时就如实报错，不去强杀。
const DWORD kWaitMainExitMs = 30 * 1000;

//主进程退出后再多等一会儿（毫秒）：进程对象已经结束，但它打开的文件句柄由系统回收
//还需要一点时间，立刻去覆盖偶尔会撞上"文件被占用"。
const DWORD kGraceAfterExitMs = 500;

//产品名称：解析出安装目录后，从 <安装目录>\config\flamingo.ini 的 [ui] apptitle 读取（规则见
//Source/App/BrandText.h）。读到之前（命令行解析失败时）为空，按缺省名称 FlamingoIM 处理。
std::wstring s_productName;

//主程序配置文件相对安装目录的路径。
const wchar_t* const kConfigRelPath = L"\\config\\flamingo.ini";

//取产品名称；还没读到时返回缺省名称。
std::wstring ProductName()
{
    return s_productName.empty() ? std::wstring(brandtext::kDefaultAppTitle) : s_productName;
}

//提示框标题：「<产品名称> 更新」。
std::wstring MsgBoxTitle()
{
    return TRF(L"{0} 更新", ProductName());
}

//取一个文件路径的所在目录，定义在下面
std::wstring DirectoryOf(const std::wstring& path);

//语言包临时复制品的文件名前缀与扩展名；完整文件名为「前缀 + 进程号 + 扩展名」，放在 %TEMP% 下
const wchar_t* const kLangCopyPrefix = L"FlamingoLang_updater_";
const wchar_t* const kLangCopySuffix = L".dll";

//语言初始化的结果，打开日志之后写进日志
i18n::InitResult s_langResult;

//删除以前运行留下的语言包复制品。仍被别的更新器进程加载着的会删除失败，留到下一次再删
void DeleteStaleLanguageCopies(const std::wstring& tempDir, const std::wstring& keepName)
{
    WIN32_FIND_DATAW fd;
    const std::wstring pattern = tempDir + kLangCopyPrefix + L"*" + kLangCopySuffix;
    HANDLE hFind = ::FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        return;
    }
    do
    {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 && keepName != fd.cFileName)
        {
            ::DeleteFileW((tempDir + fd.cFileName).c_str());
        }
    } while (::FindNextFileW(hFind, &fd));
    ::FindClose(hFind);
}

/**
 *  按 --lang 的值初始化界面语言（多语言第 3c 步）。
 *
 *    language：主程序传来的语言代码；为空时跟随系统界面语言
 *
 *  语言包先复制到 %TEMP% 再加载：升级包里可能带有新的 FlamingoLang.dll，更新器若直接加载安装目录里的那一份，
 *  安装时会因文件被占用而无法替换。复制或加载失败时退回简体中文，不影响升级本身。
 */
void InitUpdaterLanguage(const std::wstring& language)
{
    const LANGID systemUiLang = ::GetUserDefaultUILanguage();
    //期望的语言就是简体中文：不需要语言包
    const std::wstring requested = i18n::ResolveRequestedLanguage(language, systemUiLang, NULL);
    if (_wcsicmp(requested.c_str(), i18n::kSourceLanguage) == 0)
    {
        s_langResult = i18n::Init(language, std::wstring(), systemUiLang);
        return;
    }

    wchar_t exePath[MAX_PATH] = { 0 };
    ::GetModuleFileNameW(NULL, exePath, MAX_PATH);
    const std::wstring source = DirectoryOf(exePath) + L"\\" + FLAMINGO_LANG_DLL_NAME;

    wchar_t tempBuf[MAX_PATH] = { 0 };
    const DWORD tempLen = ::GetTempPathW(MAX_PATH, tempBuf);
    std::wstring copyPath;
    if (tempLen > 0 && tempLen < MAX_PATH)
    {
        const std::wstring tempDir = tempBuf;   //GetTempPathW 返回的目录以反斜杠结尾
        const std::wstring copyName = kLangCopyPrefix + std::to_wstring(::GetCurrentProcessId()) + kLangCopySuffix;
        DeleteStaleLanguageCopies(tempDir, copyName);
        if (::CopyFileW(source.c_str(), (tempDir + copyName).c_str(), FALSE))
        {
            copyPath = tempDir + copyName;
        }
    }
    //复制失败（语言包不存在、临时目录不可写）时传空路径：Init 加载失败，退回简体中文
    s_langResult = i18n::Init(language, copyPath, systemUiLang);
}

//取一个文件路径的所在目录；没有分隔符时返回空串。
std::wstring DirectoryOf(const std::wstring& path)
{
    const size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos)
    {
        return std::wstring();
    }
    return path.substr(0, pos);
}

/**
 *  按 PID 等待主进程退出。
 *
 *    pid：      主进程 id
 *    timeoutMs：最长等待毫秒数
 *
 *  @return 主进程已退出（或本来就不存在）返回 true；超时返回 false。
 *
 *  OpenProcess 失败通常意味着进程已经没了（也可能是权限不足，但更新器与主程序同一
 *  用户启动，实际不会出现），按"已退出"处理即可。
 */
bool WaitForMainProcessExit(unsigned long pid, DWORD timeoutMs)
{
    HANDLE hProcess = ::OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
    if (hProcess == NULL)
    {
        iupdate::UpdateLog::Instance().Write(L"Cannot open the main process handle, treating it as exited.");
        return true;
    }

    const DWORD wait = ::WaitForSingleObject(hProcess, timeoutMs);
    ::CloseHandle(hProcess);

    if (wait == WAIT_OBJECT_0)
    {
        //句柄已释放，但它打开的文件还要一点时间才被系统回收
        ::Sleep(kGraceAfterExitMs);
        return true;
    }

    return false;
}

/**
 *  启动主程序。
 *
 *    execPath： 要启动的可执行文件完整路径
 *
 *  @return 启动成功返回 true。
 *
 *  <u>待办（方案第 9 条决策，尚未验证）</u>：如果本更新器自身是被提权拉起来的，
 *  这里用 CreateProcess 启动的主程序会继承管理员令牌，之后客户端将收不到从资源管理器
 *  拖进来的文件，日常用管理员权限跑 IM 也不合适。业界做法是借 explorer.exe 的令牌以
 *  普通用户身份启动，但那条路径在本项目尚未实测。按既定决策，先保持直接启动，等安排
 *  专门验证、确认行为之后再补 —— 不凭推测写这段代码。
 */
bool LaunchMainProgram(const std::wstring& execPath)
{
    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    //工作目录设为主程序所在目录：客户端按相对路径找 Skins / config 等资源
    const std::wstring workDir = DirectoryOf(execPath);

    //CreateProcessW 会修改命令行缓冲，故不能直接传 c_str()
    std::wstring cmdline = L"\"" + execPath + L"\"";
    std::vector<wchar_t> buf(cmdline.begin(), cmdline.end());
    buf.push_back(L'\0');

    const BOOL ok = ::CreateProcessW(execPath.c_str(), &buf[0], NULL, NULL, FALSE, 0, NULL,
                                     workDir.empty() ? NULL : workDir.c_str(), &si, &pi);
    if (!ok)
    {
        iupdate::UpdateLog::Instance().WriteWithError(L"Failed to start the main program: " + execPath,
                                                      ::GetLastError());
        return false;
    }

    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    return true;
}

//按是否静默决定弹不弹提示框。静默模式下只写日志。
void ReportToUser(const std::wstring& text, bool silent, UINT icon)
{
    iupdate::UpdateLog::Instance().Write(text);
    if (!silent)
    {
        ::MessageBoxW(NULL, text.c_str(), MsgBoxTitle().c_str(), MB_OK | icon);
    }
}

} // namespace

int APIENTRY wWinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/,
                      LPWSTR /*lpCmdLine*/, int /*nCmdShow*/)
{
    //① 解析命令行。用 CommandLineToArgvW 拆，带空格的路径由系统按引号规则处理好。
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (argv == NULL)
    {
        //拿不到参数，也就不知道主程序的界面语言：跟随系统界面语言
        InitUpdaterLanguage(std::wstring());
        ::MessageBoxW(NULL, TR(L"无法解析命令行参数。"), MsgBoxTitle().c_str(), MB_OK | MB_ICONERROR);
        return 1;
    }

    //先确定界面语言，之后的提示（含解析失败的说明）都用这种语言
    InitUpdaterLanguage(iupdate::PeekLanguageArg(argc, argv));

    iupdate::UpdateArgs args;
    std::wstring errmsg;
    const bool parsed = iupdate::ParseCommandLine(argc, argv, args, errmsg);
    ::LocalFree(argv);

    if (!parsed)
    {
        //参数不对时一律弹框：这种情况只会出现在开发期或有人手工运行它，
        //此刻还没解析出 --silent，也谈不上静默。
        ::MessageBoxW(NULL, errmsg.c_str(), MsgBoxTitle().c_str(), MB_OK | MB_ICONERROR);
        return 1;
    }

    //知道安装目录之后，读主程序配置里的产品名称，之后的提示框都用它
    s_productName = brandtext::ReadAppTitle((args.m_targetDir + kConfigRelPath).c_str());

    //② 打开日志。日志放在安装目录下的 update 子目录，与升级包同处一地，便于打包排查。
    iupdate::UpdateLog& log = iupdate::UpdateLog::Instance();
    log.Open(args.m_targetDir + L"\\update");
    log.Write(L"======== updater started ========");
    log.Write(L"package = " + args.m_zipPath);
    log.Write(L"target dir = " + args.m_targetDir);
    log.Write(L"main program = " + args.m_execPath);
    log.Write(L"main process id = " + std::to_wstring(args.m_pid));
    {
        //语言初始化的说明是纯 ASCII，逐字节转成宽字符写进日志
        const std::string langLine = i18n::DescribeInitResult(s_langResult);
        log.Write(std::wstring(langLine.begin(), langLine.end()));
    }

    //③ 等主进程退出。等不到就如实报错退出，绝不强杀，也绝不在它还开着的时候动文件。
    if (!WaitForMainProcessExit(args.m_pid, kWaitMainExitMs))
    {
        ReportToUser(TRF(L"{0} 仍在运行，升级已取消。请关闭程序后重试。", ProductName()),
                     args.m_silent, MB_ICONWARNING);
        log.Write(L"======== updater exited (timed out waiting for the main process) ========");
        return 2;
    }
    log.Write(L"Main process has exited, installing.");

    //④ 安装。失败时 Installer 内部已经回滚完毕，安装目录回到升级前的状态。
    iupdate::Installer installer;
    if (!installer.Install(args.m_zipPath, args.m_targetDir, errmsg))
    {
        ReportToUser(TRF(L"升级失败，已还原到升级前的版本。\r\n原因：{0}", errmsg),
                     args.m_silent, MB_ICONERROR);

        //即便升级失败也要把客户端拉起来 —— 用户总得能继续用
        LaunchMainProgram(args.m_execPath);
        log.Write(L"======== updater exited (install failed, rolled back) ========");
        return 3;
    }

    //⑤ 重新启动主程序
    if (!LaunchMainProgram(args.m_execPath))
    {
        ReportToUser(TRF(L"升级已完成，但自动启动失败，请手动启动 {0}。", ProductName()),
                     args.m_silent, MB_ICONWARNING);
        log.Write(L"======== updater exited (updated, but failed to start the main program) ========");
        return 4;
    }

    log.Write(L"======== updater exited (updated) ========");
    return 0;
}
