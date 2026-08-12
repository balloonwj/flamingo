/**
 *  服务端启动期的配置文件定位与必需配置项校验，ServerConfigLoader.h
 *
 *  解决两个问题，chatserver / fileserver / imgserver 三个服务共用：
 *
 *  1. 配置文件按相对路径查找时会随启动方式漂移。以前 Windows 下写死
 *     "../etc/chatserver.conf"、Linux 下写死 "etc/chatserver.conf"，两者都相对
 *     进程的工作目录解析，而工作目录取决于怎么启动的：在可执行文件所在目录里双击、
 *     用起始位置为空的快捷方式启动、以管理员身份运行、从别的目录敲命令，拿到的
 *     工作目录各不相同。本模块改为「可执行文件所在目录优先」，并保留原来的
 *     工作目录相对路径作为兜底，既修掉漂移又不影响既有的运行方式。
 *
 *  2. 配置项缺失时的表现是崩溃而不是报错。CConfigFileReader::getConfigName() 在
 *     配置文件没加载成功、或者配置项不存在时返回 NULL，而调用方过去直接把它交给
 *     std::string::operator+= 或 atol()，于是在日志系统初始化之前就触发访问违例 ——
 *     双击启动时控制台窗口随进程一起消失，屏幕上一个字都看不到。本模块提供
 *     CRequiredConfig 统一收集缺失项，并提供 ReportStartupFailure() 打印可读的原因。
 *
 *  典型用法（见 chatserversrc/main.cpp）：
 *      CConfigFileReader config;
 *      std::string triedPaths;
 *      if (!serverconfig::LoadServerConfig(config, "chatserver.conf", triedPaths))
 *      {
 *          serverconfig::ReportStartupFailure("chatserver",
 *              std::string("cannot open config file, tried: ") + triedPaths);
 *          return 1;
 *      }
 *
 *      serverconfig::CRequiredConfig required(config);
 *      std::string logfilename = required.getString("logfilename");
 *      int         listenport  = required.getInt("listenport");
 *      if (required.hasMissing())
 *      {
 *          serverconfig::ReportStartupFailure("chatserver", ...);
 *          return 1;
 *      }
 *
 *  balloonwj@qq.com   2026-08-12
 */
#pragma once

#include <string>
#include <vector>

#include "../base/ConfigFileReader.h"

namespace serverconfig {

/**
 *  按固定优先级构造配置文件的候选路径列表。
 *
 *  纯函数，不访问文件系统，结果只取决于两个入参，因此可以直接单元测试
 *  （见 tests/test_serverconfigloader.cpp）。候选顺序为：
 *      1. <exeDir>/etc/<confFileName>          —— 可执行文件与 etc 平级的部署布局；
 *      2. <exeDir>/../etc/<confFileName>       —— Windows 的 windowsproj\ 与 Linux 的
 *         build-linux/ 都是「可执行文件在子目录、etc 在上一级」这种布局；
 *      3. <exeDir>/../../etc/<confFileName>    —— 再往上一级；
 *      4. <exeDir>/../../../etc/<confFileName> —— Windows 的 x64 产物在
 *         windowsproj\x64\<配置>\ 下，正好要向上三级才够得着 flamingoserver\etc；
 *      5. ../etc/<confFileName>                —— 旧行为：Windows 下相对工作目录；
 *      6. etc/<confFileName>                   —— 旧行为：Linux 下相对工作目录。
 *  也就是「从可执行文件所在目录起逐级向上找 etc，向上的级数见 CONFIG_SEARCH_MAX_PARENT_LEVELS」，
 *  这样同一份二进制无论从哪里启动都读同一份配置；工作目录相关的两项留在最后兜底，
 *  使既有的运行方式与文档描述继续有效。
 *
 *  @param exeDir        可执行文件所在目录，通常传 apppath::GetExecutableDirectory()
 *                       的返回值。传空串表示取不到，此时只产出后两项。
 *  @param confFileName  配置文件名，如 "chatserver.conf"，不含目录部分。
 *  @param outCandidates [出参] 候选路径，按上面的顺序排列，内容完全相同的重复项已去掉。
 *                       函数内部会先清空该容器。
 *  副作用：无。
 */
void BuildConfigCandidatePaths(const std::string& exeDir, const std::string& confFileName,
                               std::vector<std::string>& outCandidates);

/**
 *  依次尝试候选路径，加载第一个能打开的配置文件。
 *
 *  @param config        [出参] 加载目标。成功时其 isLoadOk() 为 true、
 *                       getFilePath() 为实际用到的那个路径。
 *  @param exeDir        可执行文件所在目录，语义同 BuildConfigCandidatePaths。
 *  @param confFileName  配置文件名，如 "chatserver.conf"。
 *  @param outTriedPaths [出参] 所有尝试过的路径，以 " | " 分隔，供报错信息回显。
 *                       无论成功与否都会填写。
 *  @return 任一候选路径加载成功返回 true；全部打不开返回 false。
 *  副作用：读取文件系统。
 */
bool LoadServerConfigFrom(CConfigFileReader& config, const std::string& exeDir,
                          const std::string& confFileName, std::string& outTriedPaths);

/**
 *  LoadServerConfigFrom 的便捷重载，exeDir 取当前可执行文件所在目录。
 *
 *  @param config        [出参] 加载目标，语义同上。
 *  @param confFileName  配置文件名，如 "chatserver.conf"。
 *  @param outTriedPaths [出参] 所有尝试过的路径，语义同上。
 *  @return 语义同上。
 *  副作用：读取文件系统；调用一次 GetModuleFileName / /proc/self/exe。
 */
bool LoadServerConfig(CConfigFileReader& config, const std::string& confFileName,
                      std::string& outTriedPaths);

/**
 *  必需配置项的读取器。
 *
 *  把「取值 + 判空」收拢到一处：取不到的配置项名被记下来，调用方读完全部必需项后
 *  用 hasMissing() 一次性判断。这样一次就能把缺失的配置项报全，不必让运维改一项、
 *  重启一次、再看下一条报错。
 */
class CRequiredConfig
{
public:
    /**
     *  @param config 配置源。本类只保存引用，不接管其生命周期，
     *                调用方须保证 config 的存活期覆盖本对象。
     */
    explicit CRequiredConfig(CConfigFileReader& config);

    /**
     *  取一个必需的字符串配置项。
     *  @param key 配置项名，不得为 NULL。
     *  @return 配置项的值；配置项不存在时记入缺失列表并返回空串。
     *  副作用：配置项缺失时修改内部的缺失列表。
     */
    std::string getString(const char* key);

    /**
     *  取一个必需的整数配置项，按十进制解析。
     *  @param key 配置项名，不得为 NULL。
     *  @return 配置项的值；配置项不存在时记入缺失列表并返回 0。
     *          值不是合法数字时按 atoi 的规则解析（通常得到 0），不额外报错。
     *  副作用：配置项缺失时修改内部的缺失列表。
     */
    int getInt(const char* key);

    /**
     *  是否有必需配置项缺失。
     *  @return true 表示至少有一项取不到，调用方应当停止启动并报错。
     */
    bool hasMissing() const;

    /**
     *  取缺失的配置项名，按发现顺序以 ", " 连接，用于报错信息。
     *  @return 形如 "logfilename, listenport" 的字符串；没有缺失时为空串。
     */
    std::string missingKeys() const;

private:
    CConfigFileReader&       m_config;       //配置源，仅引用，生命周期由调用方保证
    std::vector<std::string> m_missingKeys;  //缺失的配置项名，按发现顺序追加
};

/**
 *  去掉目录路径结尾的路径分隔符。
 *
 *  配置里习惯把目录写成 "logs/"，直接拿去与文件名拼接会得到
 *  "E:\...\windowsproj\logs/chatserver" 这种两种分隔符混用的路径。功能上没问题，
 *  但它会原样出现在日志与报错信息里，读起来别扭。纯函数，不访问文件系统。
 *
 *  @param path 目录路径，可以为空串。
 *  @return 去掉结尾 '/' 与 '\\' 之后的路径。根目录（"/" 或 "\\"）原样返回，
 *          否则去掉分隔符后就成了空串，反而指向当前目录。
 *  副作用：无。
 */
std::string StripTrailingSeparators(const std::string& path);

/**
 *  确保目录存在，不存在就创建它。
 *
 *  只创建最后一级目录，不递归创建父目录 —— 服务用到的目录（日志、缓存）都挂在
 *  可执行文件或工作目录下面，父目录必然已经存在，递归创建反而会在路径写错时
 *  悄悄造出一整串目录来。
 *
 *  @param dir      目录路径，允许带结尾的路径分隔符。空串直接返回 false。
 *  @param outError [出参] 失败原因（英文，取自 errno / GetLastError），成功时清空。
 *  @return 目录本来就存在、或创建成功，返回 true；否则返回 false。
 *  副作用：可能在文件系统上创建一个目录。
 */
bool EnsureDirectoryExists(const std::string& dir, std::string& outError);

/**
 *  打印启动失败的原因。
 *
 *  这条信息之所以不用 LOGF / LOGE 输出，是因为启动失败往往发生在 CAsyncLog::init()
 *  之前，那时异步日志线程还没起来，日志只会堆在内存队列里没人写出去；而 LOGF 本身
 *  的语义是「打完日志立即让进程崩溃」，用在这里等于把可控的退出变成崩溃。
 *
 *  @param serverName 服务名，如 "chatserver"，让用户一眼看出是哪个进程报的错，不得为 NULL。
 *  @param reason     失败原因。按仓库约定用英文书写，同时也避开了 Windows 控制台
 *                    代码页与源文件 UTF-8 不一致导致的乱码。
 *  副作用：向标准错误输出若干行文本；在 Windows 上、且当前控制台窗口只挂着本进程时
 *          （典型情况是双击启动，窗口随进程退出一起消失），额外等待一次回车，
 *          否则错误信息一闪而过根本看不到。从 cmd / PowerShell 里启动时不等待。
 */
void ReportStartupFailure(const char* serverName, const std::string& reason);

} // namespace serverconfig
