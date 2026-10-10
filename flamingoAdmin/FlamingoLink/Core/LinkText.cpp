/**
 *  FlamingoLink.dll —— 「在线用户」页文字与颜色的实现，说明见 LinkText.h。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "FlamingoLink/Core/LinkText.h"

#include <time.h>

#include "FlamingoLink/FlamingoLinkApi.h"
#include "Theme/AdminTheme.h"
//错误码取值，取自 PC 客户端那份协议头（与服务端那份由 PP 组测试保证一致）
#include "../../../flamingoclient/Source/net/Protocol.h"

namespace flamingolink {

namespace {

//一秒折合的毫秒数
const int64_t kMsPerSecond = 1000;

//客户端类型取值，与协议 platform_type 一致
const int kClientPc      = 1;
const int kClientAndroid = 2;
const int kClientIos     = 3;
const int kClientMac     = 4;
const int kClientWeb     = 5;

//在线状态取值，与协议 presence_type 一致
const int kPresenceOnline = 1;
const int kPresenceAway   = 2;
const int kPresenceBusy   = 3;

//strftime 输出缓冲的长度，容纳「yyyy-MM-dd HH:mm」绰绰有余
const size_t kTimeBufLen = 32;

//在汇总文字里追加一项「名称 台数」，台数为 0 时不追加
void AppendDeviceCount(CString& parts, LPCTSTR name, int count)
{
    if (count <= 0)
        return;

    if (!parts.IsEmpty())
        parts += _T(" · ");
    CString one;
    one.Format(_T("%s %d"), name, count);
    parts += one;
}

} // anonymous namespace

CString ClientTypeText(int clientType)
{
    switch (clientType)
    {
    //Windows PC 客户端
    case kClientPc:
        return CString(_T("PC"));

    //Android 客户端
    case kClientAndroid:
        return CString(_T("Android"));

    //iOS 客户端
    case kClientIos:
        return CString(_T("iOS"));

    //macOS 客户端
    case kClientMac:
        return CString(_T("Mac"));

    //Web 客户端
    case kClientWeb:
        return CString(_T("Web"));

    //本版本不认识的客户端类型
    default:
        return CString(_T("未知设备"));
    }
}

CString PresenceText(int presence)
{
    switch (presence)
    {
    //在线
    case kPresenceOnline:
        return CString(_T("在线"));

    //离开（客户端无操作一段时间后自动切换，或用户手动设置）
    case kPresenceAway:
        return CString(_T("离开"));

    //勿扰
    case kPresenceBusy:
        return CString(_T("勿扰"));

    //本版本不认识的状态
    default:
        return CString(_T("未知"));
    }
}

COLORREF PresenceColor(int presence)
{
    switch (presence)
    {
    //在线：绿
    case kPresenceOnline:
        return flamingoadmin::theme::kColorOk;

    //勿扰：红
    case kPresenceBusy:
        return flamingoadmin::theme::kColorDanger;

    //离开：橙
    case kPresenceAway:
        return flamingoadmin::theme::kColorWarn;

    //未知：灰
    default:
        return flamingoadmin::theme::kInk300;
    }
}

CString FormatLoginTime(int64_t ms)
{
    if (ms <= 0)
        return CString(_T("—"));

    const time_t sec = static_cast<time_t>(ms / kMsPerSecond);
    struct tm local;
    if (localtime_s(&local, &sec) != 0)
        return CString(_T("—"));

    wchar_t buf[kTimeBufLen] = { 0 };
    wcsftime(buf, kTimeBufLen, L"%Y-%m-%d %H:%M", &local);
    return CString(buf);
}

CString SummaryText(const OnlineSummary& s)
{
    if (s.m_devices <= 0)
        return CString(_T("当前没有在线用户"));

    CString parts;
    AppendDeviceCount(parts, _T("PC"), s.m_pc);
    AppendDeviceCount(parts, _T("Android"), s.m_android);
    AppendDeviceCount(parts, _T("iOS"), s.m_ios);
    AppendDeviceCount(parts, _T("Mac"), s.m_mac);
    AppendDeviceCount(parts, _T("Web"), s.m_web);
    AppendDeviceCount(parts, _T("未知设备"), s.m_other);

    CString text;
    text.Format(_T("在线 %d 人、%d 台设备（%s）"), s.m_users, s.m_devices, parts.GetString());
    return text;
}

CString ConnectionText(int state, int attempt, int remainingSec)
{
    CString text;
    switch (state)
    {
    //服务器未开启管理端口
    case link_state_disabled:
        text = _T("服务器未开启实时连接");
        break;

    //正在连接、握手或鉴权
    case link_state_connecting:
        text = _T("正在连接…");
        break;

    //已鉴权，连接正常
    case link_state_connected:
        text = _T("实时连接正常");
        break;

    //等待下一次重连
    case link_state_waiting:
        if (remainingSec > 0)
            text.Format(_T("连接已断开，%d 秒后第 %d 次重连"), remainingSec, attempt);
        else
            text.Format(_T("连接已断开，正在第 %d 次重连"), attempt);
        break;

    //鉴权时 token 已失效
    case link_state_expired:
        text = _T("登录已过期");
        break;

    //未启动
    default:
        text = _T("未连接");
        break;
    }
    return text;
}

COLORREF ConnectionColor(int state)
{
    switch (state)
    {
    //连接正常：绿
    case link_state_connected:
        return flamingoadmin::theme::kColorOk;

    //连接中、等待重连：橙
    case link_state_connecting:
    case link_state_waiting:
        return flamingoadmin::theme::kColorWarn;

    //登录过期：红
    case link_state_expired:
        return flamingoadmin::theme::kColorDanger;

    //未启动、服务器未开启：灰
    default:
        return flamingoadmin::theme::kInk400;
    }
}

CString OverlayText(const OverlayInput& in)
{
    if (!in.m_hasEnterprise)
        return CString(_T("请选择企业"));

    if (in.m_state == link_state_disabled)
        return CString(_T("服务器未开启实时连接，无法查看在线用户"));

    if (in.m_state == link_state_stopped)
        return CString(_T("尚未连接服务器"));

    if (in.m_state == link_state_expired)
        return CString(_T("登录已过期，请重新登录"));

    if (in.m_lastError == error_code_adminlink_forbidden)
        return CString(_T("无权查看该企业的在线用户"));

    if (in.m_lastError == error_code_adminlink_enterprise_not_exist)
        return CString(_T("企业不存在"));

    if (in.m_lastError != 0)
    {
        CString text;
        text.Format(_T("获取在线名单失败（错误码 %d）"), in.m_lastError);
        return text;
    }

    if (!in.m_received)
    {
        if (in.m_state == link_state_connected)
            return CString(_T("正在加载在线名单…"));
        return CString(_T("正在连接服务器…"));
    }

    if (in.m_totalRows <= 0)
        return CString(_T("当前没有在线用户"));

    if (in.m_filteredRows <= 0)
        return CString(_T("没有符合条件的在线用户"));

    return CString();
}

int RemainingSeconds(int elapsedMs, int delayMs)
{
    const int left = delayMs - elapsedMs;
    if (left <= 0)
        return 0;
    return static_cast<int>((left + kMsPerSecond - 1) / kMsPerSecond);
}

} // namespace flamingolink
