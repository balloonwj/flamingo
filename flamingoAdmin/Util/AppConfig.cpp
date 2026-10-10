/**
 *  flamingoAdmin —— 应用配置实现。
 *
 *  balloonwj@qq.com   2026-05-21
 */
#include "stdafx.h"
#include "Util/AppConfig.h"
#include "Util/DpapiSecret.h"   // ProtectSecret / UnprotectSecret（口令落盘前加密）
#include "Util/Logger.h"

#include <tchar.h>

namespace flamingoadmin {

namespace {

// ini 文件名、段名、键名。
const TCHAR* const kIniFileName   = _T("flamingoAdmin.ini");
const TCHAR* const kSectionServer = _T("server");
const TCHAR* const kKeyHost       = _T("host");
const TCHAR* const kKeyPort       = _T("port");
const TCHAR* const kKeyTls        = _T("tls");

// [login] 段：上次登录的账号与口令（"记住密码"）。
const TCHAR* const kSectionLogin  = _T("login");
const TCHAR* const kKeyAccount    = _T("account");
const TCHAR* const kKeyPassword   = _T("password");   // 存的是 DPAPI 密文的 Base64
const TCHAR* const kKeyRememberPwd = _T("rememberpwd");

// 读取账号 / 密码密文的临时缓冲长度。账号是用户名或邮箱，几十字符足矣；
// 密码那一栏存的是 DPAPI 密文的 Base64，一条几十字节的口令加密后约 300-400 字节，
// Base64 再涨 1/3，这里给到 4096 是留足余量，读不下会被截断、解密随之失败。
const int kAccountBufLen  = 256;
const int kPasswordBufLen = 4096;

// 默认值。
const TCHAR* const kDefaultHost = _T("127.0.0.1");
const int          kDefaultPort = 12345;

// 端口合法区间。
const int kPortMin = 1;
const int kPortMax = 65535;

// 读取主机名的临时缓冲长度（主机名 / IP 远短于此）。
const int kHostBufLen = 256;

} // anonymous namespace

LoginConfig AppConfig::ApplyPasswordChange(const LoginConfig& saved,
                                           const CString& loginAccount,
                                           const CString& newPassword)
{
    LoginConfig out = saved;
    if (saved.m_rememberPassword && !saved.m_account.IsEmpty()
        && saved.m_account.CompareNoCase(loginAccount) == 0)
    {
        out.m_password = newPassword;
    }
    return out;
}

AppConfig& AppConfig::Instance()
{
    static AppConfig s_instance;
    return s_instance;
}

CString AppConfig::IniPath()
{
    // 取 exe 完整路径，去掉文件名，拼上 ini 文件名。
    TCHAR path[MAX_PATH] = {};
    ::GetModuleFileName(nullptr, path, MAX_PATH);
    TCHAR* lastSlash = _tcsrchr(path, _T('\\'));
    if (lastSlash != nullptr)
    {
        *(lastSlash + 1) = _T('\0');
    }
    CString full(path);
    full += kIniFileName;
    return full;
}

void AppConfig::Load()
{
    const CString ini = IniPath();

    // 主机。
    TCHAR host[kHostBufLen] = {};
    ::GetPrivateProfileString(kSectionServer, kKeyHost, kDefaultHost,
                              host, kHostBufLen, ini);
    m_server.m_host = host;
    m_server.m_host.Trim();
    if (m_server.m_host.IsEmpty())
    {
        m_server.m_host = kDefaultHost;
    }

    // 端口（越界则回退默认值）。
    m_server.m_port = static_cast<int>(
        ::GetPrivateProfileInt(kSectionServer, kKeyPort, kDefaultPort, ini));
    if (m_server.m_port < kPortMin || m_server.m_port > kPortMax)
    {
        m_server.m_port = kDefaultPort;
    }

    // TLS 开关（非 0 即开）。
    m_server.m_useTls =
        ::GetPrivateProfileInt(kSectionServer, kKeyTls, 0, ini) != 0;

    FA_LOGI(_T("载入服务器配置：%s:%d tls=%d"),
            m_server.m_host.GetString(), m_server.m_port,
            m_server.m_useTls ? 1 : 0);

    LoadLogin(ini);
}

void AppConfig::LoadLogin(const CString& ini)
{
    m_login = LoginConfig();

    // 账号。没记过就是空串，登录页照常显示占位提示。
    TCHAR account[kAccountBufLen] = {};
    ::GetPrivateProfileString(kSectionLogin, kKeyAccount, _T(""),
                              account, kAccountBufLen, ini);
    m_login.m_account = account;
    m_login.m_account.Trim();

    m_login.m_rememberPassword =
        ::GetPrivateProfileInt(kSectionLogin, kKeyRememberPwd, 0, ini) != 0;

    // 没勾"记住密码"就不去碰密码那一栏 —— 即便 ini 里还留着上一次的密文，
    // 也不该解出来用，用户已经表达过"别记我的密码"了。
    if (!m_login.m_rememberPassword)
    {
        FA_LOGI(_T("载入登录信息：account=%s 记住密码=否"), m_login.m_account.GetString());
        return;
    }

    TCHAR cipher[kPasswordBufLen] = {};
    ::GetPrivateProfileString(kSectionLogin, kKeyPassword, _T(""),
                              cipher, kPasswordBufLen, ini);

    // 解不开一律按"没记过密码"处理：换了机器、换了 Windows 用户、或 ini 被手工
    // 改过都会走到这里。这不是错误，让用户重新输一次即可，故用 LOGI 而非 LOGE。
    if (!UnprotectSecret(CString(cipher), m_login.m_password))
    {
        m_login.m_password.Empty();
        m_login.m_rememberPassword = false;
        FA_LOGI(_T("载入登录信息：account=%s 密码解密失败（换机器/换用户/文件被改），按未记住处理"),
                m_login.m_account.GetString());
        return;
    }

    // 勾了"记住密码"、密文却是空的（例如上次加密失败没写成）：同样按没记过处理。
    if (m_login.m_password.IsEmpty())
    {
        m_login.m_rememberPassword = false;
    }

    FA_LOGI(_T("载入登录信息：account=%s 记住密码=%d"),
            m_login.m_account.GetString(), m_login.m_rememberPassword ? 1 : 0);
}

void AppConfig::Save()
{
    const CString ini = IniPath();

    ::WritePrivateProfileString(kSectionServer, kKeyHost, m_server.m_host, ini);

    CString portStr;
    portStr.Format(_T("%d"), m_server.m_port);
    ::WritePrivateProfileString(kSectionServer, kKeyPort, portStr, ini);

    ::WritePrivateProfileString(kSectionServer, kKeyTls,
                                m_server.m_useTls ? _T("1") : _T("0"), ini);

    FA_LOGI(_T("保存服务器配置：%s:%d tls=%d"),
            m_server.m_host.GetString(), m_server.m_port,
            m_server.m_useTls ? 1 : 0);

    SaveLogin(ini);
}

void AppConfig::SaveLogin(const CString& ini)
{
    ::WritePrivateProfileString(kSectionLogin, kKeyAccount, m_login.m_account, ini);
    ::WritePrivateProfileString(kSectionLogin, kKeyRememberPwd,
                                m_login.m_rememberPassword ? _T("1") : _T("0"), ini);

    // 没勾"记住密码"：把密码那一栏抹掉。用户取消勾选的意思就是"别再留着我的密码"，
    // 只把标志位置 0、密文还躺在 ini 里，不算真的删掉。
    if (!m_login.m_rememberPassword || m_login.m_password.IsEmpty())
    {
        ::WritePrivateProfileString(kSectionLogin, kKeyPassword, nullptr, ini);
        FA_LOGI(_T("保存登录信息：account=%s 记住密码=否（已清除已存口令）"),
                m_login.m_account.GetString());
        return;
    }

    CString cipher;
    if (!ProtectSecret(m_login.m_password, cipher) || cipher.IsEmpty())
    {
        // 加密失败就不写 —— 宁可下次让用户重输，也不能退而求其次存明文。
        ::WritePrivateProfileString(kSectionLogin, kKeyPassword, nullptr, ini);
        ::WritePrivateProfileString(kSectionLogin, kKeyRememberPwd, _T("0"), ini);
        FA_LOGW(_T("保存登录信息：口令加密失败，本次不保存密码（account=%s）"),
                m_login.m_account.GetString());
        return;
    }

    ::WritePrivateProfileString(kSectionLogin, kKeyPassword, cipher, ini);
    FA_LOGI(_T("保存登录信息：account=%s 记住密码=是"), m_login.m_account.GetString());
}

} // namespace flamingoadmin
