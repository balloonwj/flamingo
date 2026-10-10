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

// 上次登录的账号与口令（"记住密码"用）。
//
// 密码在 ini 里存的是 DPAPI 密文（见 Util/DpapiSecret.h），本结构里持有的
// m_password 则是已经解开的明文 —— 只在内存中存在，供登录页回填密码框。
// 解不开（换机器 / 换 Windows 用户 / ini 被改过）时 m_password 为空、
// m_rememberPassword 视为 false，等同于"没记过密码"。
struct LoginConfig
{
    CString m_account;           // 上次登录成功的账号（管理员用户名或邮箱）
    CString m_password;          // 上次登录成功的口令明文；仅内存态，落盘时加密
    bool    m_rememberPassword;  // 用户是否勾了"记住密码"；false 时只记账号、不记口令

    LoginConfig()
        : m_rememberPassword(false)
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

    // 读取上次登录信息（只读引用）。密码字段是已解开的明文；解不开或没记过时为空。
    const LoginConfig& Login() const { return m_login; }

    // 设置上次登录信息。仅改内存态，需再调 Save() 才落盘；落盘时密码会经
    // DPAPI 加密，且仅在 m_rememberPassword 为 true 时才写入。
    //   cfg：新的登录信息。
    void SetLogin(const LoginConfig& cfg) { m_login = cfg; }

    // 修改登录密码成功后，算出应保存的登录配置：保存的账号与本次登录的账号相同（不区分
    // 大小写）且勾了「记住密码」时，把口令换成新密码；其余情况原样返回。纯函数，不读写 ini。
    //   saved：当前保存的登录配置；loginAccount：本次登录时输入的账号；
    //   newPassword：刚修改成功的新口令明文。
    //   返回：应保存的登录配置（调用方据此 SetLogin + Save）。
    static LoginConfig ApplyPasswordChange(const LoginConfig& saved, const CString& loginAccount,
                                           const CString& newPassword);

private:
    AppConfig() {}
    AppConfig(const AppConfig&) = delete;
    AppConfig& operator=(const AppConfig&) = delete;

    // 计算 ini 文件完整路径（exe 同目录 / flamingoAdmin.ini）。
    static CString IniPath();

    // 载入 [login] 段。密码解不开时按"没记过密码"处理（清空并把记住标志置 false）。
    //   ini：ini 文件完整路径。
    void LoadLogin(const CString& ini);

    // 写出 [login] 段。未勾"记住密码"或口令加密失败时，会把 ini 里已存的密码清掉。
    //   ini：ini 文件完整路径。
    void SaveLogin(const CString& ini);

    ServerConfig m_server;   // 当前服务器配置（内存态）
    LoginConfig  m_login;    // 上次登录的账号 / 口令（内存态；口令为明文，落盘时才加密）
};

} // namespace flamingoadmin
