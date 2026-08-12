/**
 *  服务端启动期的配置文件定位与必需配置项校验实现，ServerConfigLoader.cpp
 *  balloonwj@qq.com   2026-08-12
 */
#include "ServerConfigLoader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "AppPath.h"

#ifdef WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace serverconfig {

//配置文件所在目录的固定名字。三个服务的配置文件都放在 etc/ 下。
//这一项不做成可配置的：它回答的正是「去哪里找配置文件」，没法再由配置文件决定
static const char* const CONFIG_DIR_NAME = "etc";

//上一级目录的写法。用 "/" 而不是平台分隔符：Windows 的文件 API 同样接受正斜杠，
//而候选路径要参与字符串比较去重，统一成一种写法才不会出现只是分隔符不同的重复项
static const char* const PARENT_DIR_PREFIX = "../";

//报错信息里分隔多个候选路径用的字符串。用 " | " 而不是逗号或空格，
//因为 Windows 的目录名里本来就可能含有空格与逗号，那样分不清边界
static const char* const TRIED_PATHS_SEPARATOR = " | ";

//缺失配置项名之间的分隔符
static const char* const MISSING_KEYS_SEPARATOR = ", ";

//从可执行文件所在目录起，最多向上找几级 etc 目录。取 3 的依据是 Windows 的 x64 产物
//落在 windowsproj\x64\<配置>\ 下，要向上三级才够得着 flamingoserver\etc；
//再往上找没有意义，只会在配置文件确实缺失时多试几个不存在的路径
static const int CONFIG_SEARCH_MAX_PARENT_LEVELS = 3;

/**
 *  把一个候选路径追加进列表；空串与内容重复的不再追加。
 *  @param candidates [出参] 候选列表
 *  @param path       待追加的路径
 */
static void AppendCandidate(std::vector<std::string>& candidates, const std::string& path)
{
    if (path.empty())
        return;

    for (size_t i = 0; i < candidates.size(); ++i)
    {
        if (candidates[i] == path)
            return;
    }

    candidates.push_back(path);
}

void BuildConfigCandidatePaths(const std::string& exeDir, const std::string& confFileName,
                               std::vector<std::string>& outCandidates)
{
    outCandidates.clear();

    if (confFileName.empty())
        return;

    //"../etc/xxx.conf"：可执行文件在子目录、etc 在上一级的布局
    const std::string relativeFromParent =
        std::string(PARENT_DIR_PREFIX) + CONFIG_DIR_NAME + "/" + confFileName;
    //"etc/xxx.conf"：可执行文件与 etc 平级的布局
    const std::string relativeFromHere =
        std::string(CONFIG_DIR_NAME) + "/" + confFileName;

    //可执行文件所在目录优先，并逐级向上找：同一份二进制无论从哪里启动，
    //读到的都是同一份配置。向上找是为了照顾产物放在多层子目录里的构建配置，
    //典型是 Windows 的 x64 产物在 windowsproj\x64\<配置>\ 下
    if (!exeDir.empty())
    {
        std::string prefix;
        for (int level = 0; level <= CONFIG_SEARCH_MAX_PARENT_LEVELS; ++level)
        {
            AppendCandidate(outCandidates,
                            apppath::ResolveAgainstDirectory(exeDir, prefix + relativeFromHere));
            prefix += PARENT_DIR_PREFIX;
        }
    }

    //工作目录相对路径兜底：这两项就是改造前 Windows 与 Linux 各自写死的路径，
    //保留下来使既有的启动方式与文档里的运行说明继续有效
    AppendCandidate(outCandidates, relativeFromParent);
    AppendCandidate(outCandidates, relativeFromHere);
}

bool LoadServerConfigFrom(CConfigFileReader& config, const std::string& exeDir,
                          const std::string& confFileName, std::string& outTriedPaths)
{
    outTriedPaths.clear();

    std::vector<std::string> candidates;
    BuildConfigCandidatePaths(exeDir, confFileName, candidates);

    for (size_t i = 0; i < candidates.size(); ++i)
    {
        if (!outTriedPaths.empty())
        {
            outTriedPaths += TRIED_PATHS_SEPARATOR;
        }
        outTriedPaths += candidates[i];

        if (config.load(candidates[i].c_str()))
            return true;
    }

    return false;
}

bool LoadServerConfig(CConfigFileReader& config, const std::string& confFileName,
                      std::string& outTriedPaths)
{
    return LoadServerConfigFrom(config, apppath::GetExecutableDirectory(),
                                confFileName, outTriedPaths);
}

CRequiredConfig::CRequiredConfig(CConfigFileReader& config)
    : m_config(config)
{
}

std::string CRequiredConfig::getString(const char* key)
{
    const char* value = m_config.getConfigName(key);
    if (value == NULL)
    {
        m_missingKeys.push_back(key);
        return std::string();
    }

    return std::string(value);
}

int CRequiredConfig::getInt(const char* key)
{
    const char* value = m_config.getConfigName(key);
    if (value == NULL)
    {
        m_missingKeys.push_back(key);
        return 0;
    }

    return atoi(value);
}

bool CRequiredConfig::hasMissing() const
{
    return !m_missingKeys.empty();
}

std::string CRequiredConfig::missingKeys() const
{
    std::string text;
    for (size_t i = 0; i < m_missingKeys.size(); ++i)
    {
        if (i != 0)
        {
            text += MISSING_KEYS_SEPARATOR;
        }
        text += m_missingKeys[i];
    }

    return text;
}

std::string StripTrailingSeparators(const std::string& path)
{
    std::string result = path;
    //留一个字符：路径本身就是 "/" 或 "\\" 时代表根目录，全裁掉就变成空串了
    while (result.size() > 1 &&
           (result[result.size() - 1] == '/' || result[result.size() - 1] == '\\'))
    {
        result.erase(result.size() - 1);
    }

    return result;
}

bool EnsureDirectoryExists(const std::string& dir, std::string& outError)
{
    outError.clear();

    if (dir.empty())
    {
        outError = "empty directory path";
        return false;
    }

    //两个平台的建目录接口对结尾分隔符的处理并不一致，统一裁掉最省事
    const std::string path = StripTrailingSeparators(dir);

#ifdef WIN32
    if (::CreateDirectoryA(path.c_str(), NULL))
        return true;

    //已经存在不算失败，这是最常见的一种「失败」
    DWORD lastError = ::GetLastError();
    if (lastError == ERROR_ALREADY_EXISTS)
        return true;

    char reason[64] = { 0 };
    snprintf(reason, sizeof(reason), "CreateDirectory failed, error %lu", lastError);
    outError = reason;
    return false;
#else
    //权限沿用改造前 main.cpp 里建日志目录时用的那一组：属主可读写执行、同组可读写执行、其他人可读可执行
    if (mkdir(path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == 0)
        return true;

    if (errno == EEXIST)
        return true;

    outError = strerror(errno);
    return false;
#endif
}

#ifdef WIN32
/**
 *  判断当前控制台窗口是否只挂着本进程。
 *  @return true 表示控制台上只有自己一个进程，典型情况是从资源管理器双击启动、
 *          窗口由系统为本进程新建，进程一退出窗口就跟着消失；从 cmd / PowerShell
 *          启动时控制台上至少还有外壳进程，返回 false。取不到控制台时也返回 false。
 */
static bool IsConsoleOwnedByThisProcess()
{
    //只需要区分「只有自己」与「不止自己」，数组给 2 个就够
    const DWORD CONSOLE_PROCESS_PROBE_COUNT = 2;

    DWORD processIds[CONSOLE_PROCESS_PROBE_COUNT] = { 0 };
    DWORD count = ::GetConsoleProcessList(processIds, CONSOLE_PROCESS_PROBE_COUNT);

    return count == 1;
}
#endif

void ReportStartupFailure(const char* serverName, const std::string& reason)
{
    fprintf(stderr, "\n[%s] startup failed: %s\n", serverName, reason.c_str());
    fprintf(stderr, "[%s] server not started.\n", serverName);
    fflush(stderr);

#ifdef WIN32
    if (IsConsoleOwnedByThisProcess())
    {
        fprintf(stderr, "\nPress Enter to close this window...\n");
        fflush(stderr);
        getchar();
    }
#endif
}

} // namespace serverconfig
