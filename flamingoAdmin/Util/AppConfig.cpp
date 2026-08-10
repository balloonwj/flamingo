/**
 *  flamingoAdmin —— 应用配置实现。
 *
 *  balloonwj@qq.com   2026-05-21
 */
#include "stdafx.h"
#include "Util/AppConfig.h"
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

// 默认值。
const TCHAR* const kDefaultHost = _T("127.0.0.1");
const int          kDefaultPort = 12345;

// 端口合法区间。
const int kPortMin = 1;
const int kPortMax = 65535;

// 读取主机名的临时缓冲长度（主机名 / IP 远短于此）。
const int kHostBufLen = 256;

} // anonymous namespace

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
}

} // namespace flamingoadmin
