/**
 *  配置里的主机地址检查，ConfigHostCheck.h
 *
 *  chatserver.conf 里的几个主机地址会被原样下发给客户端：通话、远程桌面、屏幕共享、群通话与 P2P 回落的中转
 *  （imgserverhost + relayport），P2P 地址探测（p2pprobehost，不配时取 imgserverhost），云端录像的上传下载
 *  （imgserverhost + imgserverport），升级包下载、文件夹传输与文件共享（fileserverhost + fileserverport）。
 *  这些地址若是只有本机才能访问的地址（如默认的 127.0.0.1），其他机器上的客户端连的是它自己那台电脑，
 *  对应的功能全部失败，而服务端日志里看不出任何异常（2026-10-12 屏幕共享开始 5 秒后提示「与屏幕共享服务的连接已断开」）。
 *  chatserver 启动时把实际下发的地址按用途交给本类检查，命中时打 WARN，提示部署人员修改。
 *
 *  2026-10-14 起由 chatserversrc/ 挪到 utils/，并改为检查「实际下发的地址」：某项功能没有配置
 *  （端口为 0、不下发）时，它的地址不再告警；P2P 探测地址按回落之后的实际取值检查
 *  （docs/远程桌面控制权限与观看工具条方案.md 8.2 第 7 条）。判定地址是否只有本机可访问的规则不变。
 *
 *  典型用法（chatserver 的 main.cpp）：
 *      std::vector<PublishedHost> published;
 *      P2PServerSetup::appendPublishedHosts(p2pConfig, probeHostConfigured, published);
 *      published.push_back(PublishedHost("fileserverhost", fileHost, filePort, "folder transfer"));
 *      std::vector<LocalOnlyHostWarning> warnings = ConfigHostCheck::findLocalOnly(published);
 *
 *  单元测试见 flamingoserver/tests/test_confighostcheck.cpp（Suite HAC）。
 *
 *  balloonwj@qq.com   2026-10-12
 */
#pragma once

#include <string>
#include <vector>

/**
 *  一个要下发给客户端的主机地址及其用途。由调用方按各功能实际下发的值填写。
 */
struct PublishedHost
{
    std::string m_key;     //地址来自的配置项名（如 "imgserverhost"），原样写进告警日志
    std::string m_host;    //实际下发的主机地址；为空表示这项功能不下发地址
    int         m_port;    //与地址一起下发的端口；不大于 0 表示这项功能没有配置、不下发
    std::string m_usage;   //用这个地址的功能（英文），原样写进告警日志

    PublishedHost();

    /**
     *  @param key   配置项名
     *  @param host  实际下发的主机地址
     *  @param port  一起下发的端口
     *  @param usage 用这个地址的功能（英文）
     */
    PublishedHost(const std::string& key, const std::string& host, int port, const std::string& usage);
};

/**
 *  一条「只有本机才能访问」的告警：同一配置项的同一个地址只出一条，合并它的全部用途。
 */
struct LocalOnlyHostWarning
{
    std::string m_key;      //配置项名
    std::string m_host;     //地址
    std::string m_usages;   //用这个地址的全部功能，按首次出现的顺序以 ", " 连接，重复的只列一次
};

/**
 *  主机地址的可达范围判定。只有静态函数，不需要实例；纯文本判定，不做 DNS 解析、不访问网络。
 */
class ConfigHostCheck
{
public:
    /**
     *  判断一个配置的主机地址是否只有本机才能访问（其他机器上的客户端拿到它连不上服务端）。
     *  @param host 配置项的值：点分十进制 IPv4、IPv6 文本（可带方括号）或主机名；不做首尾空白裁剪
     *  @return 以下情况返回 true：
     *            - 127.0.0.0/8 内的 IPv4 地址，或未指定地址 0.0.0.0；
     *            - 主机名 localhost（不区分大小写）；
     *            - IPv6 的 ::1 与 ::（带不带方括号都算）。
     *          空串、普通 IPv4 地址、普通主机名（包括以 127 开头的域名）以及不合法的 IPv4 文本返回 false。
     *          IPv6 只识别上述两种简写，其他写法（如 0:0:0:0:0:0:0:1）返回 false。
     */
    static bool isLocalOnlyHost(const std::string& host);

    /**
     *  从实际下发的地址里找出只有本机才能访问的那些。
     *  @param published 各功能实际下发的地址；地址为空或端口不大于 0 的项视为没有下发，跳过
     *  @return 告警列表：每个「配置项名 + 地址」一条，按首次出现的顺序排列，用途合并；没有问题时为空
     */
    static std::vector<LocalOnlyHostWarning> findLocalOnly(const std::vector<PublishedHost>& published);
};
