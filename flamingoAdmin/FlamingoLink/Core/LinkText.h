/**
 *  FlamingoLink.dll —— 「在线用户」页上显示的文字与颜色：设备类型、在线状态、登录时间、汇总、
 *  连接状态，以及表格中央的提示文字。全部是纯函数，单元测试见 flamingoAdmin/Tests/FlamingoLinkTests.cpp。
 *
 *  典型用法：
 *      label->SetText(flamingolink::SummaryText(table.Summary()));
 *      status->SetText(flamingolink::ConnectionText(state, attempt, remainSec));
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <atlstr.h>

#include "FlamingoLink/Core/OnlineTable.h"

namespace flamingolink {

/**
 *  表格中央提示文字的输入：页面与连接的当前情况。
 */
struct OverlayInput
{
    OverlayInput()
        : m_hasEnterprise(false), m_state(0), m_lastError(0), m_received(false), m_totalRows(0), m_filteredRows(0)
    {
    }

    bool m_hasEnterprise;  //页面是否设置了企业
    int  m_state;          //连接状态，取值见 FlamingoLinkApi.h 的 enum LinkState
    int  m_lastError;      //最近一次订阅失败的错误码；0 表示没有失败
    bool m_received;       //当前企业是否已经收到过完整名单
    int  m_totalRows;      //名单的总行数
    int  m_filteredRows;   //按搜索框筛选后的行数
};

/**
 *  客户端类型的名称：PC、Android、iOS、Mac、Web，其余为「未知设备」。
 */
CString ClientTypeText(int clientType);

/**
 *  在线状态的名称：在线、离开、勿扰，其余为「未知」。
 */
CString PresenceText(int presence);

/**
 *  在线状态圆点的颜色：在线绿、勿扰红、离开橙，其余灰。
 */
COLORREF PresenceColor(int presence);

/**
 *  把 Unix 毫秒格式化成本地时间「yyyy-MM-dd HH:mm」。
 *  @param ms Unix 毫秒；不大于 0 时返回「—」
 */
CString FormatLoginTime(int64_t ms);

/**
 *  名单汇总：「在线 23 人、30 台设备（PC 18 · Android 7 · iOS 5）」，台数为 0 的设备类型不列；
 *  没有任何设备在线时为「当前没有在线用户」。
 */
CString SummaryText(const OnlineSummary& s);

/**
 *  连接状态文字。
 *  @param state         连接状态，取值见 enum LinkState
 *  @param attempt       waiting 状态下是第几次重连
 *  @param remainingSec  waiting 状态下距下次重连还有几秒；为 0 时显示「正在第 n 次重连」
 */
CString ConnectionText(int state, int attempt, int remainingSec);

/**
 *  连接状态文字的颜色：正常绿、连接中与等待重连橙、登录过期红，其余灰。
 */
COLORREF ConnectionColor(int state);

/**
 *  表格中央的提示文字。为空串表示不显示提示（正常显示表格）。按以下先后判定：
 *  没选企业、服务器未开启、未连接、登录过期、订阅失败、尚未收到名单、名单为空、筛选后为空。
 */
CString OverlayText(const OverlayInput& in);

/**
 *  距下次重连还有几秒（向上取整，不小于 0）。
 *  @param elapsedMs 进入等待以来经过的毫秒数
 *  @param delayMs   这次等待的总毫秒数
 */
int RemainingSeconds(int elapsedMs, int delayMs);

} // namespace flamingolink
