/**
 *  服务端启动期的配置文件定位与必需配置项校验实现，ServerConfigLoader.cpp
 *  balloonwj@qq.com   2026-08-12
 */
#include "ServerConfigLoader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>               //LONG_MAX / LONG_MIN：线程数配置项的越界判定

#include "AppPath.h"
#include "../base/AsyncLog.h"     //线程数配置项非法时要记一条告警日志
#include "../net/FrameCrypto.h"   //协议加密的进程级配置装载

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

//身份密钥文件的默认文件名，落在配置文件所在目录（通常就是 etc/）
static const char* const DEFAULT_SERVER_KEY_FILE = "server_key";

//取一个文件路径的所在目录；没有目录部分时返回空串
static std::string DirectoryOfFile(const std::string& filePath)
{
    const std::string::size_type pos = filePath.find_last_of("/\\");
    if (pos == std::string::npos)
        return std::string();

    return filePath.substr(0, pos);
}

bool InitProtocolEncryption(CConfigFileReader& config, std::string& outError)
{
    outError.clear();

    //缺配置项时按开启处理，理由见头文件注释
    bool encryptionEnabled = true;
    const char* encryptionCfg = config.getConfigName("encryption");
    if (encryptionCfg != NULL)
    {
        encryptionEnabled = (atoi(encryptionCfg) != 0);
    }

    std::string serverKeyFile = DEFAULT_SERVER_KEY_FILE;
    const char* serverKeyCfg = config.getConfigName("serverkeyfile");
    if (serverKeyCfg != NULL && serverKeyCfg[0] != '\0')
    {
        serverKeyFile = serverKeyCfg;
    }

    //相对路径按<u>实际加载的那个配置文件所在目录</u>解析，而不是可执行文件所在目录。
    //这与 logfiledir / dbpath 的规则不同，是刻意的：那两个是各服务自己的产出目录，
    //而身份密钥要求三个服务共用同一份 —— 三个服务的 .conf 本来就在同一个 etc/ 里，
    //按配置文件定位天然就落到同一个文件上。若按可执行文件目录解析，Windows 下
    //可执行文件在 windowsproj\、Linux 下在 build-linux/，各自会算出一个不存在的
    //子目录，还要额外约定怎么把三份对齐。
    const std::string configDir = DirectoryOfFile(config.getFilePath());
    if (!configDir.empty())
    {
        serverKeyFile = apppath::ResolveAgainstDirectory(configDir, serverKeyFile);
    }
    else
    {
        //配置文件是以纯文件名加载的（当前目录），退回可执行文件所在目录兜底
        serverKeyFile = apppath::ResolveAgainstDirectory(apppath::GetExecutableDirectory(),
                                                         serverKeyFile);
    }

    return net::cryptoConfigInit(serverKeyFile, encryptionEnabled, outError);
}

//配置项的值取不到时，日志里用来代替它的占位文字。写成一个具名常量而不是就地写字面量，
//是为了让日志格式在五个配置项之间保持一致，便于按同一条正则捞出来
static const char* const IO_THREAD_COUNT_NOT_SET_TEXT = "(not set)";

/**
 *  严格按十进制解析一个整数。
 *
 *  不用 atoi 的原因有两条：其一，atoi 对 "abc" 与 "0" 都返回 0，而这两者在线程数
 *  这里必须分开报（一个是把类型写错了、一个是写了个非法数量），报错文案不同；
 *  其二，atoi 会把 "4x" 当成 4，配置写错了却照常启动，问题就此被掩盖。
 *
 *  @param text     待解析的文本，允许为 NULL 或空串，此时判为非法。
 *  @param outValue [出参] 解析出的值。文本表示的数超出 long 的范围时给出
 *                  LONG_MAX / LONG_MIN，调用方的越界判定会照常把它挡下。
 *  @return 文本是一个合法的十进制整数（允许一个前导正负号）返回 true，否则 false。
 */
static bool ParseDecimalInteger(const char* text, long& outValue)
{
    if (text == NULL || text[0] == '\0')
        return false;

    //起始位置允许一个正负号，其后必须全是十进制数字。这里手写一遍字符判定而不是
    //只看 strtol 的 endptr，是为了把 " 6"、"6 " 这类夹了空白的值也判为非法 ——
    //配置项的值在 CConfigFileReader::trimSpace 里已经去过首尾空白，到这里还留着
    //空白就说明值里混进了别的东西
    size_t firstDigitIndex = 0;
    if (text[0] == '+' || text[0] == '-')
        firstDigitIndex = 1;

    if (text[firstDigitIndex] == '\0')
        return false;

    for (size_t i = firstDigitIndex; text[i] != '\0'; ++i)
    {
        if (text[i] < '0' || text[i] > '9')
            return false;
    }

    errno = 0;
    char* endPtr = NULL;
    const long value = strtol(text, &endPtr, 10);

    //数值大到 long 装不下时 strtol 置 ERANGE 并返回 LONG_MAX / LONG_MIN。
    //这种值本来就远超线程数上界，原样交回去让调用方按「越界」处理即可，
    //不能在这里返回 false —— 那会把它误报成「不是数字」
    if (errno == ERANGE)
    {
        outValue = (value > 0) ? LONG_MAX : LONG_MIN;
        return true;
    }

    outValue = value;
    return true;
}

int ResolveIoThreadCount(const char* configuredValue, int defaultValue,
                         IoThreadCountStatus& outStatus)
{
    //配置项缺失，或写成 "key=" 这种空值。后者在 CConfigFileReader 里根本不会进 map，
    //getConfigName 同样返回 NULL，两种情况在这里归为一类
    if (configuredValue == NULL || configuredValue[0] == '\0')
    {
        outStatus = io_thread_count_missing;
        return defaultValue;
    }

    long parsedValue = 0;
    if (!ParseDecimalInteger(configuredValue, parsedValue))
    {
        outStatus = io_thread_count_not_number;
        return defaultValue;
    }

    //0 与负数按 TcpServer::start 的语义等于「不建工作线程」，大得离谱的值则会在启动
    //阶段耗光资源，两者都不该悄悄生效
    if (parsedValue < MIN_IO_THREAD_COUNT || parsedValue > MAX_IO_THREAD_COUNT)
    {
        outStatus = io_thread_count_out_of_range;
        return defaultValue;
    }

    outStatus = io_thread_count_ok;
    return static_cast<int>(parsedValue);
}

const char* DescribeIoThreadCountStatus(IoThreadCountStatus status)
{
    switch (status)
    {
    //取值合法，配置里写的值被原样采用
    case io_thread_count_ok:
        return "ok";

    //配置项整项缺失或写成空值，老配置文件走的就是这一条
    case io_thread_count_missing:
        return "config item is not set or empty";

    //取值不是十进制整数，如 "abc"、"4x"
    case io_thread_count_not_number:
        return "value is not a decimal integer";

    //取值是整数但落在有效区间之外：0、负数，或大于上界
    case io_thread_count_out_of_range:
        return "value is out of the valid range";

    //枚举以后若有新增分支而这里忘了跟上，返回一句兜底文字，不让日志里出现空指针
    default:
        return "unknown";
    }
}

int ReadIoThreadCountConfig(CConfigFileReader& config, const char* key, int defaultValue)
{
    const char* configuredValue = config.getConfigName(key);

    IoThreadCountStatus status = io_thread_count_ok;
    const int adoptedValue = ResolveIoThreadCount(configuredValue, defaultValue, status);

    if (status == io_thread_count_ok)
    {
        LOGI("config item %s: using %d io thread(s) from the config file.", key, adoptedValue);
    }
    //配置项没写：这是正常情况而非故障 —— 本项是可选配置，老部署的 .conf 里本来就没有它，
    //缺失时按默认值运行的行为与本功能上线前完全一致。记 LOGI 而不是 LOGW，把告警留给
    //"确实写错了"的那两类（非数字、越界），免得每次启动都刷出几条无需处理的告警。
    else if (status == io_thread_count_missing)
    {
        LOGI("config item %s is not set, using the default: %d io thread(s).", key, adoptedValue);
    }
    else
    {
        LOGW("config item %s is invalid, configured value: \"%s\", reason: %s, "
             "valid range: [%d, %d], falling back to the default: %d io thread(s).",
             key,
             (configuredValue != NULL) ? configuredValue : IO_THREAD_COUNT_NOT_SET_TEXT,
             DescribeIoThreadCountStatus(status),
             MIN_IO_THREAD_COUNT, MAX_IO_THREAD_COUNT,
             adoptedValue);
    }

    return adoptedValue;
}

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
