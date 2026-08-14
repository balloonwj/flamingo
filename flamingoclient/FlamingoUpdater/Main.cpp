/**
 *  更新器 FlamingoUpdater 的入口，Main.cpp
 *
 *  执行流程（见 docs/客户端自动更新方案.md 6.5）：
 *    ① 解析命令行、打开日志；
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

namespace {

//等待主进程退出的超时（毫秒）。老版本只等 5 秒，实测不够——主程序退出前还要保存
//会话、关闭数据库。给到 30 秒，超时就如实报错，不去强杀。
const DWORD kWaitMainExitMs = 30 * 1000;

//主进程退出后再多等一会儿（毫秒）：进程对象已经结束，但它打开的文件句柄由系统回收
//还需要一点时间，立刻去覆盖偶尔会撞上"文件被占用"。
const DWORD kGraceAfterExitMs = 500;

//提示框标题。
const wchar_t* const kMsgBoxTitle = L"FlamingoIM 更新";

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
        iupdate::UpdateLog::Instance().Write(L"主进程句柄打不开，视为已退出。");
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
        iupdate::UpdateLog::Instance().WriteWithError(L"启动主程序失败：" + execPath,
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
        ::MessageBoxW(NULL, text.c_str(), kMsgBoxTitle, MB_OK | icon);
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
        ::MessageBoxW(NULL, L"无法解析命令行参数。", kMsgBoxTitle, MB_OK | MB_ICONERROR);
        return 1;
    }

    iupdate::UpdateArgs args;
    std::wstring errmsg;
    const bool parsed = iupdate::ParseCommandLine(argc, argv, args, errmsg);
    ::LocalFree(argv);

    if (!parsed)
    {
        //参数不对时一律弹框：这种情况只会出现在开发期或有人手工运行它，
        //此刻还没解析出 --silent，也谈不上静默。
        ::MessageBoxW(NULL, errmsg.c_str(), kMsgBoxTitle, MB_OK | MB_ICONERROR);
        return 1;
    }

    //② 打开日志。日志放在安装目录下的 update 子目录，与升级包同处一地，便于打包排查。
    iupdate::UpdateLog& log = iupdate::UpdateLog::Instance();
    log.Open(args.m_targetDir + L"\\update");
    log.Write(L"======== 更新器启动 ========");
    log.Write(L"包 = " + args.m_zipPath);
    log.Write(L"目标目录 = " + args.m_targetDir);
    log.Write(L"主程序 = " + args.m_execPath);
    log.Write(L"主进程 id = " + std::to_wstring(args.m_pid));

    //③ 等主进程退出。等不到就如实报错退出，绝不强杀，也绝不在它还开着的时候动文件。
    if (!WaitForMainProcessExit(args.m_pid, kWaitMainExitMs))
    {
        ReportToUser(L"FlamingoIM 仍在运行，升级已取消。请关闭程序后重试。",
                     args.m_silent, MB_ICONWARNING);
        log.Write(L"======== 更新器退出（等待主进程超时）========");
        return 2;
    }
    log.Write(L"主进程已退出，开始安装。");

    //④ 安装。失败时 Installer 内部已经回滚完毕，安装目录回到升级前的状态。
    iupdate::Installer installer;
    if (!installer.Install(args.m_zipPath, args.m_targetDir, errmsg))
    {
        ReportToUser(L"升级失败，已还原到升级前的版本。\r\n原因：" + errmsg,
                     args.m_silent, MB_ICONERROR);

        //即便升级失败也要把客户端拉起来 —— 用户总得能继续用
        LaunchMainProgram(args.m_execPath);
        log.Write(L"======== 更新器退出（安装失败，已回滚）========");
        return 3;
    }

    //⑤ 重新启动主程序
    if (!LaunchMainProgram(args.m_execPath))
    {
        ReportToUser(L"升级已完成，但自动启动失败，请手动启动 FlamingoIM。",
                     args.m_silent, MB_ICONWARNING);
        log.Write(L"======== 更新器退出（升级成功但启动失败）========");
        return 4;
    }

    log.Write(L"======== 更新器退出（升级成功）========");
    return 0;
}
