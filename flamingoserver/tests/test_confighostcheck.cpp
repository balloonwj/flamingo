/**
 *  配置里的主机地址是否只有本机才能访问的判定用例（Suite HAC），test_confighostcheck.cpp
 *
 *  背景：chatserver.conf 的 imgserverhost / fileserverhost 会被原样下发给客户端，作为通话、远程桌面、
 *  屏幕共享、文件夹传输、文件共享的中转地址与 P2P 地址探测地址。跨机器部署时保留默认的 127.0.0.1，
 *  客户端连的是它自己那台电脑，这些功能全部失败（2026-10-12 屏幕共享开始 5 秒后提示连接已断开）。
 *  chatserver 启动时用 ConfigHostCheck::isLocalOnlyHost 检查这两项，命中时打一条 WARN。
 *
 *  覆盖（2026-10-12 与用户确认的用例）：
 *    - HAC1 回环地址判为只有本机可访问：127/8 整段、localhost（不区分大小写）、::1、[::1]；
 *    - HAC2 未指定地址同样判为只有本机可访问：0.0.0.0、::；
 *    - HAC3 普通地址不告警：公网地址、内网地址、普通域名、以 127 开头的域名、不合法的 IPv4 文本；
 *    - HAC4 空串不告警：空值表示不启用该功能，各模块已有自己的日志。
 *  2026-10-14 起检查改为按实际下发的地址、按用途进行（ConfigHostCheck 挪到 utils/，
 *  docs/远程桌面控制权限与观看工具条方案.md 8.4）：
 *    - HAC5 p2pprobehost 为本机地址、imgserverhost 为外网地址时只报地址探测；p2pprobehost 为空时回落到
 *      imgserverhost，两者都是本机地址时中转与探测都报；
 *    - HAC6 只检查实际下发的地址：端口为 0、不下发的功能不报；同一地址的用途合并、去重。
 *  HAC5、HAC6 的配置文件真的写到当前目录再由 CConfigFileReader 读取，文件名带 flamingo_hac 前缀，读完即删除。
 *
 *  典型用法：Tests.exe --test-case="HAC*"
 *
 *  balloonwj@qq.com   2026-10-12
 */
#include <string>

#include <stdio.h>
#include <fstream>
#include <vector>

#include "doctest.h"
#include "../base/ConfigFileReader.h"
#include "../chatserversrc/P2PServerSetup.h"
#include "../utils/ConfigHostCheck.h"

namespace {

//HAC5、HAC6 的临时配置文件，写在当前目录，读完即删除
const char* const kHacConfPath = "flamingo_hac_test.conf";

//用例里的地址与端口
const char* const kLoopbackHost = "127.0.0.1";
const char* const kPublicHost = "101.37.25.166";
const int kFileServerPort = 20001;

//P2PServerSetup::appendPublishedHosts 写进告警的两种用途里的关键词
const char* const kRelayUsageWord = "relay";
const char* const kProbeUsage = "p2p address probing";

//把 content 写成配置文件、用 CConfigFileReader 读出，经 P2PServerSetup 列出要下发的中转与探测地址
std::vector<PublishedHost> PublishedFrom(const std::string& content, bool probeHostConfigured)
{
    {
        std::ofstream out(kHacConfPath, std::ios::binary | std::ios::trunc);
        REQUIRE(out.is_open());
        out << content;
    }
    CConfigFileReader reader(kHacConfPath);
    const bool loaded = reader.isLoadOk();
    std::vector<PublishedHost> published;
    if (loaded)
        P2PServerSetup::appendPublishedHosts(P2PServerSetup::loadConfig(reader), probeHostConfigured, published);
    remove(kHacConfPath);
    REQUIRE(loaded);
    return published;
}

//usages 里 word 出现的次数
size_t CountOf(const std::string& usages, const std::string& word)
{
    size_t count = 0;
    for (size_t pos = usages.find(word); pos != std::string::npos; pos = usages.find(word, pos + word.size()))
        ++count;
    return count;
}

} // namespace

TEST_CASE("HAC1 回环地址判为只有本机可访问")
{
    //127.0.0.0/8 整段都是回环地址，不只 127.0.0.1
    CHECK(ConfigHostCheck::isLocalOnlyHost("127.0.0.1"));
    CHECK(ConfigHostCheck::isLocalOnlyHost("127.0.0.2"));
    CHECK(ConfigHostCheck::isLocalOnlyHost("127.255.255.254"));

    //主机名 localhost 不区分大小写
    CHECK(ConfigHostCheck::isLocalOnlyHost("localhost"));
    CHECK(ConfigHostCheck::isLocalOnlyHost("LOCALHOST"));

    //IPv6 回环地址，带方括号与不带方括号两种写法
    CHECK(ConfigHostCheck::isLocalOnlyHost("::1"));
    CHECK(ConfigHostCheck::isLocalOnlyHost("[::1]"));
}

TEST_CASE("HAC2 未指定地址同样判为只有本机可访问")
{
    //0.0.0.0 与 :: 只能用作监听地址，客户端拿它什么也连不上
    CHECK(ConfigHostCheck::isLocalOnlyHost("0.0.0.0"));
    CHECK(ConfigHostCheck::isLocalOnlyHost("::"));
}

TEST_CASE("HAC3 普通地址不告警")
{
    //公网地址与内网地址
    CHECK_FALSE(ConfigHostCheck::isLocalOnlyHost("101.37.25.166"));
    CHECK_FALSE(ConfigHostCheck::isLocalOnlyHost("192.168.1.10"));
    CHECK_FALSE(ConfigHostCheck::isLocalOnlyHost("10.0.0.1"));

    //普通域名，以及恰好以 127 开头的域名
    CHECK_FALSE(ConfigHostCheck::isLocalOnlyHost("im.example.com"));
    CHECK_FALSE(ConfigHostCheck::isLocalOnlyHost("127.example.com"));

    //不是合法的 IPv4 文本：第一段有四位数字
    CHECK_FALSE(ConfigHostCheck::isLocalOnlyHost("1270.0.0.1"));
}

TEST_CASE("HAC4 空串不告警")
{
    CHECK_FALSE(ConfigHostCheck::isLocalOnlyHost(""));
}

TEST_CASE("HAC5 按用途检查：探测地址按回落后的实际取值，各用途分别判断")
{
    SUBCASE("只有探测地址是本机地址：只报 p2pprobehost 的地址探测，不报中转")
    {
        const std::vector<PublishedHost> published = PublishedFrom(
            std::string("relaytokenkey=hac-secret\nimgserverhost=") + kPublicHost + "\nrelayport=20003\n"
            "p2pprobehost=" + kLoopbackHost + "\n", true);
        const std::vector<LocalOnlyHostWarning> warnings = ConfigHostCheck::findLocalOnly(published);
        REQUIRE(warnings.size() == 1);
        CHECK(warnings[0].m_key == "p2pprobehost");
        CHECK(warnings[0].m_host == kLoopbackHost);
        CHECK(warnings[0].m_usages == kProbeUsage);
    }

    SUBCASE("p2pprobehost 为空时回落到 imgserverhost：两者都是本机地址，中转与探测都报，且探测只列一次")
    {
        const std::vector<PublishedHost> published = PublishedFrom(
            std::string("relaytokenkey=hac-secret\nimgserverhost=") + kLoopbackHost + "\nrelayport=20003\n", false);
        const std::vector<LocalOnlyHostWarning> warnings = ConfigHostCheck::findLocalOnly(published);
        REQUIRE(warnings.size() == 1);
        CHECK(warnings[0].m_key == "imgserverhost");
        CHECK(warnings[0].m_host == kLoopbackHost);
        CHECK(CountOf(warnings[0].m_usages, kRelayUsageWord) == 1);
        //默认两个探测端口各下发一次，用途合并后只列一次
        CHECK(CountOf(warnings[0].m_usages, kProbeUsage) == 1);
    }
}

TEST_CASE("HAC6 只检查实际下发的地址：没有配置的功能不报，同一地址的用途合并")
{
    SUBCASE("中转端口没配、两个探测端口都配成 0：imgserverhost 虽是本机地址，也不下发、不报")
    {
        const std::vector<PublishedHost> published = PublishedFrom(
            std::string("imgserverhost=") + kLoopbackHost + "\np2pprobeport1=0\np2pprobeport2=0\n", false);
        CHECK(ConfigHostCheck::findLocalOnly(published).empty());
    }

    SUBCASE("端口不大于 0 或地址为空的项跳过")
    {
        std::vector<PublishedHost> published;
        published.push_back(PublishedHost("fileserverhost", kLoopbackHost, 0, "file transfer"));
        published.push_back(PublishedHost("fileserverhost", "", kFileServerPort, "file transfer"));
        CHECK(ConfigHostCheck::findLocalOnly(published).empty());

        published.push_back(PublishedHost("fileserverhost", kLoopbackHost, kFileServerPort, "file transfer"));
        const std::vector<LocalOnlyHostWarning> warnings = ConfigHostCheck::findLocalOnly(published);
        REQUIRE(warnings.size() == 1);
        CHECK(warnings[0].m_usages == "file transfer");
    }

    SUBCASE("同一配置项的同一地址合并用途、重复的用途只列一次；不同配置项各报一条；普通地址不报")
    {
        std::vector<PublishedHost> published;
        published.push_back(PublishedHost("imgserverhost", kLoopbackHost, kFileServerPort, "a"));
        published.push_back(PublishedHost("imgserverhost", kLoopbackHost, kFileServerPort, "b"));
        published.push_back(PublishedHost("imgserverhost", kLoopbackHost, kFileServerPort, "a"));
        published.push_back(PublishedHost("fileserverhost", kLoopbackHost, kFileServerPort, "c"));
        published.push_back(PublishedHost("p2pprobehost", kPublicHost, kFileServerPort, "d"));
        const std::vector<LocalOnlyHostWarning> warnings = ConfigHostCheck::findLocalOnly(published);
        REQUIRE(warnings.size() == 2);
        CHECK(warnings[0].m_key == "imgserverhost");
        CHECK(warnings[0].m_usages == "a, b");
        CHECK(warnings[1].m_key == "fileserverhost");
        CHECK(warnings[1].m_usages == "c");
    }
}
