/**
 *  flamingoAdmin —— 应用配置（服务器连接设置）。
 *
 *  flamingoAdmin 当前数据全部 mock，本配置只负责持久化「管理控制台要连接
 *  的服务器地址」，供登录界面的网络设置页读写；将来接入真实服务端时，
 *  联网层从此读取目标地址。配置存放在 exe 同目录的 flamingoAdmin.ini，
 *  [server] 段。
 *
 *  典型用法：
 *      AppConfig::Instance().Load();              // 进程启动时一次
 *      const ServerConfig& s = AppConfig::Instance().Server();
 *      AppConfig::Instance().SetServer(newCfg);
 *      AppConfig::Instance().Save();              // 落盘
 *
 *  balloonwj@qq.com   2026-05-21
 */
#pragma once

namespace flamingoadmin {

// 服务器连接配置。
struct ServerConfig
{
    CString m_host;     // 服务器主机名 / IP
    int     m_port;     // 服务器端口（取值 1 - 65535）
    bool    m_useTls;   // 是否使用 TLS 加密连接

    // 默认指向本机 chatserver 的 HTTP 端口、不加密。
    ServerConfig()
        : m_host(_T("127.0.0.1")), m_port(12345), m_useTls(false)
    {
    }
};

// 应用配置单例：读写 exe 同目录的 flamingoAdmin.ini。
class AppConfig
{
public:
    // 取全局唯一实例。
    static AppConfig& Instance();

    // 从 ini 载入配置；文件 / 键缺失或非法时回退到默认值。
    // 进程启动时调用一次。
    void Load();

    // 把当前内存态配置写回 ini。
    void Save();

    // 读取当前服务器配置（只读引用）。
    const ServerConfig& Server() const { return m_server; }

    // 设置服务器配置。仅改内存态，需再调 Save() 才落盘。
    //   cfg：新的服务器配置。
    void SetServer(const ServerConfig& cfg) { m_server = cfg; }

private:
    AppConfig() {}
    AppConfig(const AppConfig&) = delete;
    AppConfig& operator=(const AppConfig&) = delete;

    // 计算 ini 文件完整路径（exe 同目录 / flamingoAdmin.ini）。
    static CString IniPath();

    ServerConfig m_server;   // 当前服务器配置（内存态）
};

} // namespace flamingoadmin
