/**
 *  NetSettingsModel.cpp
 *
 *  网络设置数据模型的实现。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-05-22
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（等价 NewChatDemo/stdafx.h）----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0200
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>

#include "NetSettingsModel.h"

namespace balloonnetsettings {

namespace {

// 三组服务器的默认地址(与 flamingo.ini 缺省值一致;缺配置文件时兜底)。
const TCHAR* const kDefaultHost[kNetServerCount] =
{
    _T("127.0.0.1"),   // 聊天服务器
    _T("127.0.0.1"),   // 图片服务器
    _T("127.0.0.1"),   // 文件服务器
};

// 三组服务器的默认端口（与 flamingo.ini 缺省值一致）。
const int kDefaultPort[kNetServerCount] =
{
    20000,   // 聊天服务器
    20002,   // 图片服务器
    20001,   // 文件服务器
};

// 三组服务器的中文显示名。
const TCHAR* const kDisplayName[kNetServerCount] =
{
    _T("聊天服务器"),
    _T("图片服务器"),
    _T("文件服务器"),
};

} // anonymous namespace

NetSettingsModel::NetSettingsModel()
    : m_selected(kNetServerChat)
{
    for (int i = 0; i < kNetServerCount; ++i)
    {
        m_servers[i].host = kDefaultHost[i];
        m_servers[i].port = kDefaultPort[i];
    }
}

bool NetSettingsModel::IsValidKind(int kind)
{
    return kind >= 0 && kind < kNetServerCount;
}

int NetSettingsModel::Selected() const
{
    return m_selected;
}

void NetSettingsModel::Select(int kind)
{
    if (IsValidKind(kind))
    {
        m_selected = kind;
    }
}

const NetServerEntry& NetSettingsModel::Server(int kind) const
{
    if (IsValidKind(kind))
    {
        return m_servers[kind];
    }
    return m_servers[kNetServerChat];
}

void NetSettingsModel::SetServer(int kind, const NetServerEntry& entry)
{
    if (IsValidKind(kind))
    {
        m_servers[kind] = entry;
    }
}

const TCHAR* NetServerDefaultHost(int kind)
{
    if (kind >= 0 && kind < kNetServerCount)
    {
        return kDefaultHost[kind];
    }
    return kDefaultHost[kNetServerChat];
}

int NetServerDefaultPort(int kind)
{
    if (kind >= 0 && kind < kNetServerCount)
    {
        return kDefaultPort[kind];
    }
    return kDefaultPort[kNetServerChat];
}

const TCHAR* NetServerDisplayName(int kind)
{
    if (kind >= 0 && kind < kNetServerCount)
    {
        return kDisplayName[kind];
    }
    return kDisplayName[kNetServerChat];
}

} // namespace balloonnetsettings
