/**
 *  更新器的命令行解析，CommandLine.h
 *
 *  命令行契约（见 docs/客户端自动更新方案.md 6.4）：
 *
 *      FlamingoUpdater.exe --zip "<升级包绝对路径>" --target "<安装目录>"
 *                      --exec "<要重启的exe>" --pid <主进程PID> [--silent] [--lang <语言代码>]
 *
 *  四个带值参数都是必填，`--silent` 可选；`--lang`（多语言第 3c 步）可选，取主程序当前生效的界面语言，
 *  如 zh-CN、en-US，省略时跟随系统界面语言。参数顺序不限。
 *
 *  之所以做成"传 argc / argv 的纯函数"而不是直接读 GetCommandLineW()：这样它不依赖
 *  进程环境，可以被 FlamingoUnit 直接喂各种参数组合断言。真实入口在 Main.cpp 里用
 *  CommandLineToArgvW 拿到 argv 再调它 —— 带空格的路径由系统按引号规则拆好，不必
 *  自己解析引号。
 *
 *  balloonwj@qq.com   2026-08-12
 */
#pragma once

#include <string>

namespace iupdate {

//一次升级任务的全部输入参数。
struct UpdateArgs
{
    //升级包（zip）的完整路径。由主程序下载落地后传进来。
    std::wstring  m_zipPath;
    //安装目录，即要被覆盖的那个目录（主程序所在目录）。
    std::wstring  m_targetDir;
    //升级完成后要重新启动的可执行文件完整路径。
    std::wstring  m_execPath;
    //主进程的进程 id。更新器按它等待主进程退出。
    //老版本是遍历进程列表按名字找 Flamingo.exe，多开或改名就失灵，故改成按 id。
    unsigned long m_pid;
    //静默模式：不弹任何提示框，失败只写日志。供无人值守场景使用。
    bool          m_silent;
    //界面语言代码（--lang 的值）；没有给出时为空串，表示跟随系统界面语言。
    std::wstring  m_language;

    UpdateArgs()
        : m_pid(0), m_silent(false)
    {
    }
};

/**
 *  解析命令行参数。
 *
 *    argc：   参数个数，含 argv[0]（程序自身路径）
 *    argv：   参数数组，元素为宽字符串；argc > 0 时 argv 不得为 NULL
 *    out：    [出参] 解析结果；返回 false 时其内容未定义，调用方不应使用
 *    errmsg： [出参] 失败原因（当前界面语言，可直接写日志或提示给用户）
 *
 *  @return 四个必填参数齐备且取值合法时返回 true。
 *
 *  失败的情形：缺任一必填参数、带值参数后面没有跟值、--pid 不是正整数。
 *  未知参数一律忽略而不报错 —— 将来加新开关时，旧版更新器碰到新参数不至于直接罢工。
 */
bool ParseCommandLine(int argc, wchar_t* const* argv,
                      UpdateArgs& out, std::wstring& errmsg);

/**
 *  只取出 --lang 的值，不检查其它参数（多语言第 3c 步）。入口要先据它确定界面语言，再做完整解析，
 *  这样解析失败时的提示也是对应语言的文字。
 *
 *    argc：参数个数，含 argv[0]
 *    argv：参数数组；argc > 0 时不得为 NULL
 *
 *  @return 第一个 --lang 之后的那个参数；没有 --lang、或它后面没有值时返回空串。
 */
std::wstring PeekLanguageArg(int argc, wchar_t* const* argv);

} // namespace iupdate
