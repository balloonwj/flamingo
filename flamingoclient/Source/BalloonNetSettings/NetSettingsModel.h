/**
 *  NetSettingsModel.h
 *
 *  网络设置窗口的数据模型 —— 持有"聊天 / 图片 / 文件"三组服务器的
 *  地址 / 端口配置，以及当前正在编辑的是哪一组。纯数据结构，
 *  不涉及 flamingo.ini 文件读写（读写在 BalloonNetSettingsWindow 里）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL
 *  （需要 CString）。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#pragma once

namespace balloonnetsettings {

/**
 *  三组服务器。枚举值同时用作 NetSettingsModel 内部数组的下标。
 */
enum NetServerKind
{
    kNetServerChat  = 0,   // 聊天服务器（flamingo.ini: server / port）
    kNetServerImage = 1,   // 图片服务器（flamingo.ini: imgserver / imgport）
    kNetServerFile  = 2,   // 文件服务器（flamingo.ini: fileserver / fileport）
};

// 服务器组数。
const int kNetServerCount = 3;

/**
 *  服务器连接状态 —— 由后台 TCP 探测（CIUSocket::TestConnect）的结果驱动。
 *  含义是"这个地址端口此刻连不连得上"，而非"客户端当前是否与它保持着连接"：
 *  聊天服务器是登录后的长连接，图片 / 文件服务器则是用到才连的短连接，
 *  统一按可达性判定，三张卡片的语义才一致。
 */
enum NetConnState
{
    kNetConnConnected    = 0,   // 已连接
    kNetConnConnecting   = 1,   // 连接中
    kNetConnDisconnected = 2,   // 未连接
};

/**
 *  一组服务器的配置。
 */
struct NetServerEntry
{
    CString host;   // 服务器地址（域名或 IP）
    int     port;   // 端口，取值 1..65535

    NetServerEntry()
        : port(0)
    {
    }
};

/**
 *  NetSettingsModel —— 网络设置数据模型。
 *
 *  持有三组服务器配置 + 当前选中（正在编辑）的服务器下标。窗口在切换
 *  卡片时：先把表单内容提交回当前选中组，再 Select 新组、把新组的值
 *  灌回表单 —— "分别配置三组服务器"的暂存语义全靠本模型承载。
 */
class NetSettingsModel
{
public:
    // 构造：三组均置为各自的默认地址 / 端口，默认选中聊天组。
    NetSettingsModel();

    // 当前选中（正在编辑）的服务器组，取值见 NetServerKind。
    int  Selected() const;

    // 切换选中的服务器组。kind 越界则忽略。
    void Select(int kind);

    // 读取某组服务器配置。kind 越界时回退到聊天组，保证返回有效引用。
    //   kind：服务器组，取值见 NetServerKind。
    const NetServerEntry& Server(int kind) const;

    // 写入某组服务器配置。kind 越界则忽略。
    //   kind：服务器组，取值见 NetServerKind；entry：要写入的配置（拷贝）。
    void SetServer(int kind, const NetServerEntry& entry);

private:
    // 判定 kind 是否是合法的服务器组下标。
    static bool IsValidKind(int kind);

private:
    NetServerEntry m_servers[kNetServerCount];  // 三组服务器配置
    int            m_selected;                  // 当前选中组，见 NetServerKind
};

// 某组服务器的默认地址（flamingo.ini 缺省值）。kind 越界返回聊天组默认。
const TCHAR* NetServerDefaultHost(int kind);

// 某组服务器的默认端口。kind 越界返回聊天组默认端口。
int NetServerDefaultPort(int kind);

// 某组服务器的中文显示名（"聊天服务器" 等）。kind 越界返回聊天组名。
const TCHAR* NetServerDisplayName(int kind);

} // namespace balloonnetsettings
