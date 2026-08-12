/**
 *  服务端启动期配置加载单元测试（Suite SCL），test_serverconfigloader.cpp
 *
 *  覆盖 utils/ServerConfigLoader 的三部分：
 *    - BuildConfigCandidatePaths：候选路径的顺序、去重、以及取不到可执行文件目录时的回落；
 *    - LoadServerConfigFrom：真的在临时目录里建一个 etc/xxx.conf 并加载，
 *      验证「可执行文件所在目录优先」确实生效，以及全部候选都打不开时的返回值与尝试记录；
 *    - CRequiredConfig：必需配置项的取值、缺失项的收集与报错文案。
 *
 *  第一部分是纯函数，路径都是虚构的；后两部分要落到真实文件系统，故在当前目录下
 *  建一个临时目录，跑完删掉。用例里的配置文件名带 flamingo_scl 前缀，
 *  与仓库 etc 目录下的真实配置文件不会重名 —— 否则工作目录兜底的那两个候选路径
 *  可能意外命中真实配置文件，让「找不到」的用例失真。
 *
 *  balloonwj@qq.com   2026-08-12
 */
#include <stdio.h>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "doctest.h"
#include "../utils/ServerConfigLoader.h"

//本套用例专用的临时目录：模拟一个「可执行文件在 bin/、配置在 etc/」的部署
static const char* const SCL_ROOT_DIR = "flamingo_scl_test";
static const char* const SCL_BIN_DIR  = "flamingo_scl_test/bin";
static const char* const SCL_ETC_DIR  = "flamingo_scl_test/etc";

//本套用例用的配置文件名。带 flamingo_scl 前缀，保证不会与仓库 etc 目录下的真实配置文件撞名
static const char* const SCL_CONF_NAME    = "flamingo_scl_test.conf";
static const char* const SCL_MISSING_CONF = "flamingo_scl_missing.conf";

//用例里统一使用的虚构可执行文件目录，两种分隔符各一份
static const char* const FAKE_EXE_DIR_POSIX = "/opt/flamingo/bin";
static const char* const FAKE_EXE_DIR_WIN   = "E:\\flamingo\\flamingoserver\\windowsproj";

//建目录；已存在不算失败
static void makeDir(const char* path)
{
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
#endif
}

//删目录；非空时删不掉，调用方须先清文件
static void removeDir(const char* path)
{
#ifdef _WIN32
    _rmdir(path);
#else
    rmdir(path);
#endif
}

//在临时目录里建好 bin/ 与 etc/，并往 etc/ 里写一个配置文件，返回该文件的路径
static std::string setupFakeDeployment(const std::string& content)
{
    makeDir(SCL_ROOT_DIR);
    makeDir(SCL_BIN_DIR);
    makeDir(SCL_ETC_DIR);

    const std::string path = std::string(SCL_ETC_DIR) + "/" + SCL_CONF_NAME;
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
    REQUIRE(out.is_open());
    out << content;
    out.close();

    return path;
}

//清掉 setupFakeDeployment 建出来的东西
static void teardownFakeDeployment()
{
    const std::string path = std::string(SCL_ETC_DIR) + "/" + SCL_CONF_NAME;
    remove(path.c_str());
    removeDir(SCL_ETC_DIR);
    removeDir(SCL_BIN_DIR);
    removeDir(SCL_ROOT_DIR);
}

//判断 text 里是否含有 sub
static bool contains(const std::string& text, const std::string& sub)
{
    return text.find(sub) != std::string::npos;
}

// ---------------------------------------------------------------------------
// SCL1  候选路径的顺序与内容
// ---------------------------------------------------------------------------

TEST_CASE("SCL1 候选路径从可执行文件所在目录逐级向上，最后才回落到工作目录")
{
    std::vector<std::string> candidates;
    serverconfig::BuildConfigCandidatePaths(FAKE_EXE_DIR_POSIX, "chatserver.conf", candidates);

    REQUIRE(candidates.size() == 6);
    //可执行文件与 etc 平级
    CHECK(candidates[0] == "/opt/flamingo/bin/etc/chatserver.conf");
    //可执行文件在子目录、etc 在上一级：Linux 的 build-linux/ 就是这种布局
    CHECK(candidates[1] == "/opt/flamingo/bin/../etc/chatserver.conf");
    //再往上两级，照顾产物放在多层子目录里的构建配置
    CHECK(candidates[2] == "/opt/flamingo/bin/../../etc/chatserver.conf");
    CHECK(candidates[3] == "/opt/flamingo/bin/../../../etc/chatserver.conf");
    //改造前 Windows 写死的那条相对工作目录的路径
    CHECK(candidates[4] == "../etc/chatserver.conf");
    //改造前 Linux 写死的那条相对工作目录的路径
    CHECK(candidates[5] == "etc/chatserver.conf");
}

TEST_CASE("SCL2 基准目录用反斜杠时，拼出来的候选路径跟随同一种分隔符")
{
    std::vector<std::string> candidates;
    serverconfig::BuildConfigCandidatePaths(FAKE_EXE_DIR_WIN, "chatserver.conf", candidates);

    REQUIRE(candidates.size() == 6);
    CHECK(candidates[0] == "E:\\flamingo\\flamingoserver\\windowsproj\\etc/chatserver.conf");
    CHECK(candidates[1] == "E:\\flamingo\\flamingoserver\\windowsproj\\../etc/chatserver.conf");
}

TEST_CASE("SCL11 Windows 的 x64 产物在 windowsproj\\x64\\<配置>\\ 下，向上三级能够着 flamingoserver\\etc")
{
    std::vector<std::string> candidates;
    serverconfig::BuildConfigCandidatePaths(
        "E:\\flamingo\\flamingoserver\\windowsproj\\x64\\Debug", "chatserver.conf", candidates);

    //第四条（向上三级）正是 flamingoserver\etc 那一份，没有它 x64 产物就找不到配置文件
    REQUIRE(candidates.size() == 6);
    CHECK(candidates[3] == "E:\\flamingo\\flamingoserver\\windowsproj\\x64\\Debug\\../../../etc/chatserver.conf");
}

TEST_CASE("SCL3 取不到可执行文件目录时，只剩两条相对工作目录的候选")
{
    std::vector<std::string> candidates;
    serverconfig::BuildConfigCandidatePaths("", "fileserver.conf", candidates);

    REQUIRE(candidates.size() == 2);
    CHECK(candidates[0] == "../etc/fileserver.conf");
    CHECK(candidates[1] == "etc/fileserver.conf");
}

TEST_CASE("SCL12 候选路径不重复：可执行文件目录恰好等于工作目录时不会产出两条相同路径")
{
    std::vector<std::string> candidates;
    //传相对目录 "." 时，向上逐级拼出来的路径与工作目录那两条形态不同，
    //这里验证的是去重逻辑本身不会漏掉完全相同的项
    serverconfig::BuildConfigCandidatePaths("", "imgserver.conf", candidates);

    for (size_t i = 0; i < candidates.size(); ++i)
    {
        for (size_t j = i + 1; j < candidates.size(); ++j)
        {
            CHECK(candidates[i] != candidates[j]);
        }
    }
}

TEST_CASE("SCL4 配置文件名为空时不产出任何候选，且出参会被先清空")
{
    std::vector<std::string> candidates;
    candidates.push_back("残留内容，应当被清掉");

    serverconfig::BuildConfigCandidatePaths(FAKE_EXE_DIR_POSIX, "", candidates);

    CHECK(candidates.empty());
}

TEST_CASE("SCL10 去掉目录结尾的分隔符，根目录与空串保持原样")
{
    //配置里最常见的写法，去掉结尾斜杠后才能与文件名拼出单一分隔符的路径
    CHECK(serverconfig::StripTrailingSeparators("logs/") == "logs");
    CHECK(serverconfig::StripTrailingSeparators("E:\\flamingo\\logs\\") == "E:\\flamingo\\logs");
    //连着好几个分隔符也要一次裁干净
    CHECK(serverconfig::StripTrailingSeparators("/var/log/flamingo///") == "/var/log/flamingo");
    //本来就没有结尾分隔符的原样返回
    CHECK(serverconfig::StripTrailingSeparators("logs") == "logs");
    //根目录不能裁成空串，否则含义从「根目录」变成「当前目录」
    CHECK(serverconfig::StripTrailingSeparators("/") == "/");
    CHECK(serverconfig::StripTrailingSeparators("\\") == "\\");
    //空串原样返回
    CHECK(serverconfig::StripTrailingSeparators("").empty());
}

// ---------------------------------------------------------------------------
// SCL5  真实文件系统上的加载
// ---------------------------------------------------------------------------

TEST_CASE("SCL5 配置放在可执行文件目录的上一级 etc 里时能被找到")
{
    setupFakeDeployment("listenip=127.0.0.1\nlistenport=20000\n");

    CConfigFileReader config;
    std::string triedPaths;
    const bool loaded = serverconfig::LoadServerConfigFrom(config, SCL_BIN_DIR,
                                                           SCL_CONF_NAME, triedPaths);

    CHECK(loaded);
    CHECK(config.isLoadOk());
    //第一个候选是「与可执行文件平级的 etc」，这里不存在，命中的是向上一级的那条，
    //所以尝试记录里是这两条
    CHECK(triedPaths == std::string(SCL_BIN_DIR) + "/etc/" + SCL_CONF_NAME
                      + " | " + SCL_BIN_DIR + "/../etc/" + SCL_CONF_NAME);
    CHECK(config.getFilePath() == std::string(SCL_BIN_DIR) + "/../etc/" + SCL_CONF_NAME);
    REQUIRE(config.getConfigName("listenip") != NULL);
    CHECK(std::string(config.getConfigName("listenip")) == "127.0.0.1");
    CHECK(std::string(config.getConfigName("listenport")) == "20000");

    teardownFakeDeployment();
}

TEST_CASE("SCL6 所有候选都打不开时返回 false，并把试过的路径都记下来")
{
    CConfigFileReader config;
    std::string triedPaths;
    const bool loaded = serverconfig::LoadServerConfigFrom(config, SCL_BIN_DIR,
                                                           SCL_MISSING_CONF, triedPaths);

    CHECK_FALSE(loaded);
    CHECK_FALSE(config.isLoadOk());
    //加载失败后取任何配置项都必须是 NULL —— 调用方过去正是把这个 NULL 直接拿去用才崩溃的
    CHECK(config.getConfigName("listenip") == NULL);

    //四条候选路径都应当出现在尝试记录里，且以 " | " 分隔
    CHECK(contains(triedPaths, std::string(SCL_BIN_DIR) + "/../etc/" + SCL_MISSING_CONF));
    CHECK(contains(triedPaths, std::string(SCL_BIN_DIR) + "/etc/" + SCL_MISSING_CONF));
    CHECK(contains(triedPaths, std::string("../etc/") + SCL_MISSING_CONF));
    CHECK(contains(triedPaths, std::string("etc/") + SCL_MISSING_CONF));
    CHECK(contains(triedPaths, " | "));
}

// ---------------------------------------------------------------------------
// SCL7  必需配置项的收集
// ---------------------------------------------------------------------------

TEST_CASE("SCL7 必需配置项齐全时不报缺失，取值与类型转换都正确")
{
    setupFakeDeployment("listenip=0.0.0.0\nlistenport=20001\nlogfilename=chatserver\n");

    CConfigFileReader config;
    std::string triedPaths;
    REQUIRE(serverconfig::LoadServerConfigFrom(config, SCL_BIN_DIR, SCL_CONF_NAME, triedPaths));

    serverconfig::CRequiredConfig required(config);
    CHECK(required.getString("listenip") == "0.0.0.0");
    CHECK(required.getInt("listenport") == 20001);
    CHECK(required.getString("logfilename") == "chatserver");
    CHECK_FALSE(required.hasMissing());
    CHECK(required.missingKeys().empty());

    teardownFakeDeployment();
}

TEST_CASE("SCL8 缺失的必需配置项被逐个记下来，按发现顺序以逗号连接")
{
    setupFakeDeployment("listenip=0.0.0.0\n");

    CConfigFileReader config;
    std::string triedPaths;
    REQUIRE(serverconfig::LoadServerConfigFrom(config, SCL_BIN_DIR, SCL_CONF_NAME, triedPaths));

    serverconfig::CRequiredConfig required(config);
    //存在的项照常返回值，不计入缺失
    CHECK(required.getString("listenip") == "0.0.0.0");
    //缺失的字符串项返回空串、整数项返回 0，绝不返回空指针
    CHECK(required.getString("logfilename").empty());
    CHECK(required.getInt("listenport") == 0);

    CHECK(required.hasMissing());
    CHECK(required.missingKeys() == "logfilename, listenport");

    teardownFakeDeployment();
}

TEST_CASE("SCL9 配置文件根本没加载成功时，所有必需项都算缺失")
{
    CConfigFileReader config;
    std::string triedPaths;
    REQUIRE_FALSE(serverconfig::LoadServerConfigFrom(config, SCL_BIN_DIR,
                                                     SCL_MISSING_CONF, triedPaths));

    serverconfig::CRequiredConfig required(config);
    CHECK(required.getString("logfilename").empty());
    CHECK(required.getInt("listenport") == 0);
    CHECK(required.hasMissing());
    CHECK(required.missingKeys() == "logfilename, listenport");
}
