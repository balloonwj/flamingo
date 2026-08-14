/**
 *  网络 I/O 工作线程数配置项的取值判定单元测试（Suite IOT），test_iothreadcount.cpp
 *
 *  覆盖 utils/ServerConfigLoader 里的 serverconfig::ResolveIoThreadCount 与
 *  serverconfig::DescribeIoThreadCountStatus 两个纯函数。这两个函数回答的是同一个问题：
 *  「配置文件里写的这个值，最终该起几条线程」。
 *
 *  这段判定之所以值得单独测：它决定的是三个服务共五个监听端口的实际线程数，而配错的
 *  表现极其隐蔽 —— 写成 0 会让服务连一条工作线程都不建，表现为「端口能连上但请求永远
 *  没有响应」；写成上万则会在启动阶段耗光资源。两种情况都不会报错，只能靠这里的判定
 *  在启动时把它们挡下来并回落到默认值。
 *
 *  被测函数是纯函数：不读配置文件、不写日志、不访问文件系统，故本文件不需要临时目录，
 *  也不需要初始化日志。带日志的那层封装是 ReadIoThreadCountConfig，它只是「取配置项 +
 *  调用本函数 + 打一条日志」，逻辑全在这里，不另测。
 *
 *  balloonwj@qq.com   2026-08-14
 */
#include <string>

#include "doctest.h"
#include "../utils/ServerConfigLoader.h"

//三个服务改造前写死在代码里的线程数，也就是各配置项的默认值。用例里反复要拿它们做
//「回落到默认值」的期望值，故在此集中列出，与产品代码里的 kDefaultIoThreadCount 对应
static const int IOT_CHAT_SERVER_DEFAULT    = 6;   //chatserver 主服务（客户端连接）
static const int IOT_MONITOR_SERVER_DEFAULT = 1;   //chatserver 监控端口
static const int IOT_HTTP_SERVER_DEFAULT    = 4;   //chatserver HTTP 端口
static const int IOT_FILE_SERVER_DEFAULT    = 6;   //fileserver / imgserver 的文件端口
static const int IOT_RELAY_SERVER_DEFAULT   = 4;   //imgserver 的中转端口

/**
 *  跑一次判定，同时把判定结果带出来，省得每条用例都写一遍出参声明。
 *  @param configuredValue 配置项的原始值，允许为 NULL
 *  @param defaultValue    回落用的默认值
 *  @param outStatus       [出参] 判定结果
 *  @return 实际采用的线程数
 */
static int resolve(const char* configuredValue, int defaultValue,
                   serverconfig::IoThreadCountStatus& outStatus)
{
    return serverconfig::ResolveIoThreadCount(configuredValue, defaultValue, outStatus);
}

// ---------------------------------------------------------------------------
// IOT1  合法取值原样采用
// ---------------------------------------------------------------------------

TEST_CASE("IOT1 取值合法时原样采用配置里写的值，不受默认值影响")
{
    serverconfig::IoThreadCountStatus status = serverconfig::io_thread_count_missing;

    //配置里写的与默认值不同，采用的必须是配置里那个
    CHECK(resolve("12", IOT_CHAT_SERVER_DEFAULT, status) == 12);
    CHECK(status == serverconfig::io_thread_count_ok);

    //配置里写的恰好等于默认值，同样算「按配置采用」而不是「回落」
    CHECK(resolve("6", IOT_CHAT_SERVER_DEFAULT, status) == 6);
    CHECK(status == serverconfig::io_thread_count_ok);

    //监控端口的默认值是 1，配大了照样按配置来
    CHECK(resolve("3", IOT_MONITOR_SERVER_DEFAULT, status) == 3);
    CHECK(status == serverconfig::io_thread_count_ok);

    //带一个前导正号仍是合法的十进制整数，运维手写配置时可能这么写
    CHECK(resolve("+8", IOT_FILE_SERVER_DEFAULT, status) == 8);
    CHECK(status == serverconfig::io_thread_count_ok);
}

// ---------------------------------------------------------------------------
// IOT2  配置项缺失
// ---------------------------------------------------------------------------

TEST_CASE("IOT2 配置项缺失时回落默认值，判定结果为缺失")
{
    serverconfig::IoThreadCountStatus status = serverconfig::io_thread_count_ok;

    //CConfigFileReader::getConfigName 对不存在的配置项返回 NULL，这是老配置文件的常态
    CHECK(resolve(NULL, IOT_CHAT_SERVER_DEFAULT, status) == IOT_CHAT_SERVER_DEFAULT);
    CHECK(status == serverconfig::io_thread_count_missing);

    //写成 "key=" 时解析器根本不会把这一项放进 map，效果与整项缺失相同；
    //这里另外覆盖一次空串，防止将来解析器改成保留空值时这条路径失守
    CHECK(resolve("", IOT_RELAY_SERVER_DEFAULT, status) == IOT_RELAY_SERVER_DEFAULT);
    CHECK(status == serverconfig::io_thread_count_missing);
}

// ---------------------------------------------------------------------------
// IOT3  非数字
// ---------------------------------------------------------------------------

TEST_CASE("IOT3 取值不是十进制整数时回落默认值")
{
    serverconfig::IoThreadCountStatus status = serverconfig::io_thread_count_ok;

    //完全不是数字
    CHECK(resolve("abc", IOT_HTTP_SERVER_DEFAULT, status) == IOT_HTTP_SERVER_DEFAULT);
    CHECK(status == serverconfig::io_thread_count_not_number);

    //数字后面跟了别的字符。这一条特意留着：atoi 会把它当成 4，配置写错却照常启动，
    //问题就此被掩盖，所以判定必须比 atoi 严
    CHECK(resolve("4x", IOT_HTTP_SERVER_DEFAULT, status) == IOT_HTTP_SERVER_DEFAULT);
    CHECK(status == serverconfig::io_thread_count_not_number);

    //小数：线程数没有小数的说法，且 atoi 同样会把它截成 1
    CHECK(resolve("1.5", IOT_HTTP_SERVER_DEFAULT, status) == IOT_HTTP_SERVER_DEFAULT);
    CHECK(status == serverconfig::io_thread_count_not_number);

    //十六进制写法不受支持，避免 "0x10" 到底算 0 还是算 16 的歧义
    CHECK(resolve("0x10", IOT_HTTP_SERVER_DEFAULT, status) == IOT_HTTP_SERVER_DEFAULT);
    CHECK(status == serverconfig::io_thread_count_not_number);

    //只有一个符号、没有数字
    CHECK(resolve("+", IOT_HTTP_SERVER_DEFAULT, status) == IOT_HTTP_SERVER_DEFAULT);
    CHECK(status == serverconfig::io_thread_count_not_number);

    //值里夹了空白。配置项的值在 CConfigFileReader::trimSpace 里已经去过首尾空白，
    //到这里还留着空白就说明中间混进了别的东西
    CHECK(resolve("6 6", IOT_HTTP_SERVER_DEFAULT, status) == IOT_HTTP_SERVER_DEFAULT);
    CHECK(status == serverconfig::io_thread_count_not_number);
}

// ---------------------------------------------------------------------------
// IOT4  0 与负数
// ---------------------------------------------------------------------------

TEST_CASE("IOT4 取值为 0 或负数时回落默认值")
{
    serverconfig::IoThreadCountStatus status = serverconfig::io_thread_count_ok;

    //0 在 TcpServer::start 的语义下等于「不建工作线程」，表现为端口连得上但请求没有响应
    CHECK(resolve("0", IOT_CHAT_SERVER_DEFAULT, status) == IOT_CHAT_SERVER_DEFAULT);
    CHECK(status == serverconfig::io_thread_count_out_of_range);

    //负数同理，且它是「非数字」之外的另一类错误，判定结果必须区分开
    CHECK(resolve("-1", IOT_CHAT_SERVER_DEFAULT, status) == IOT_CHAT_SERVER_DEFAULT);
    CHECK(status == serverconfig::io_thread_count_out_of_range);

    CHECK(resolve("-6", IOT_MONITOR_SERVER_DEFAULT, status) == IOT_MONITOR_SERVER_DEFAULT);
    CHECK(status == serverconfig::io_thread_count_out_of_range);
}

// ---------------------------------------------------------------------------
// IOT5  超出上界
// ---------------------------------------------------------------------------

TEST_CASE("IOT5 取值超出上界时回落默认值，而不是截断到上界")
{
    serverconfig::IoThreadCountStatus status = serverconfig::io_thread_count_ok;

    //刚好越过上界一格
    const std::string justAboveMax = std::to_string(serverconfig::MAX_IO_THREAD_COUNT + 1);
    CHECK(resolve(justAboveMax.c_str(), IOT_FILE_SERVER_DEFAULT, status) == IOT_FILE_SERVER_DEFAULT);
    CHECK(status == serverconfig::io_thread_count_out_of_range);

    //把端口号误填进线程数是很常见的一类笔误
    CHECK(resolve("20000", IOT_FILE_SERVER_DEFAULT, status) == IOT_FILE_SERVER_DEFAULT);
    CHECK(status == serverconfig::io_thread_count_out_of_range);

    //大到 long 都装不下：strtol 在这里会置 ERANGE，判定必须仍归为「越界」而不是「非数字」，
    //否则日志会把原因说反，看日志的人会往「值里有非法字符」的方向查
    CHECK(resolve("99999999999999999999", IOT_FILE_SERVER_DEFAULT, status) == IOT_FILE_SERVER_DEFAULT);
    CHECK(status == serverconfig::io_thread_count_out_of_range);

    //同样大小的负值也是越界
    CHECK(resolve("-99999999999999999999", IOT_FILE_SERVER_DEFAULT, status) == IOT_FILE_SERVER_DEFAULT);
    CHECK(status == serverconfig::io_thread_count_out_of_range);

    //明确固定住「回落而不是截断」这条约定：越界时采用的是默认值，绝不是上界
    CHECK(resolve(justAboveMax.c_str(), IOT_FILE_SERVER_DEFAULT, status)
              != serverconfig::MAX_IO_THREAD_COUNT);
}

// ---------------------------------------------------------------------------
// IOT6  边界值本身可用
// ---------------------------------------------------------------------------

TEST_CASE("IOT6 下界 1 与上界本身都是合法取值")
{
    serverconfig::IoThreadCountStatus status = serverconfig::io_thread_count_missing;

    //下界：监控端口的默认值就是 1，说明单线程是正当配置，不能被当成非法
    CHECK(resolve("1", IOT_CHAT_SERVER_DEFAULT, status) == serverconfig::MIN_IO_THREAD_COUNT);
    CHECK(status == serverconfig::io_thread_count_ok);

    //上界本身也在区间内，判定用的是「大于上界才算越界」
    const std::string atMax = std::to_string(serverconfig::MAX_IO_THREAD_COUNT);
    CHECK(resolve(atMax.c_str(), IOT_CHAT_SERVER_DEFAULT, status)
              == serverconfig::MAX_IO_THREAD_COUNT);
    CHECK(status == serverconfig::io_thread_count_ok);
}

// ---------------------------------------------------------------------------
// IOT7  默认值由调用方给出
// ---------------------------------------------------------------------------

TEST_CASE("IOT7 同一个非法取值在不同服务上回落到各自的默认值")
{
    serverconfig::IoThreadCountStatus status = serverconfig::io_thread_count_ok;

    //五个配置项共用这一段判定，但默认值各不相同 —— 回落的目标必须是调用方传进来的那个，
    //不能在函数内部固定成某一个数，否则会把监控端口从 1 条线程悄悄改成 6 条
    CHECK(resolve("0", IOT_CHAT_SERVER_DEFAULT, status)    == IOT_CHAT_SERVER_DEFAULT);
    CHECK(resolve("0", IOT_MONITOR_SERVER_DEFAULT, status) == IOT_MONITOR_SERVER_DEFAULT);
    CHECK(resolve("0", IOT_HTTP_SERVER_DEFAULT, status)    == IOT_HTTP_SERVER_DEFAULT);
    CHECK(resolve("0", IOT_FILE_SERVER_DEFAULT, status)    == IOT_FILE_SERVER_DEFAULT);
    CHECK(resolve("0", IOT_RELAY_SERVER_DEFAULT, status)   == IOT_RELAY_SERVER_DEFAULT);
}

// ---------------------------------------------------------------------------
// IOT8  判定结果的英文说明
// ---------------------------------------------------------------------------

TEST_CASE("IOT8 每个判定结果都有一句非空的英文说明，且互不相同")
{
    const std::string ok         = serverconfig::DescribeIoThreadCountStatus(serverconfig::io_thread_count_ok);
    const std::string missing    = serverconfig::DescribeIoThreadCountStatus(serverconfig::io_thread_count_missing);
    const std::string notNumber  = serverconfig::DescribeIoThreadCountStatus(serverconfig::io_thread_count_not_number);
    const std::string outOfRange = serverconfig::DescribeIoThreadCountStatus(serverconfig::io_thread_count_out_of_range);

    //日志里要靠这句话说明回落的原因，空串等于什么都没说
    CHECK_FALSE(ok.empty());
    CHECK_FALSE(missing.empty());
    CHECK_FALSE(notNumber.empty());
    CHECK_FALSE(outOfRange.empty());

    //三种非法原因必须能在日志里区分开，否则看日志的人不知道该往哪个方向改配置
    CHECK(missing != notNumber);
    CHECK(missing != outOfRange);
    CHECK(notNumber != outOfRange);
}
