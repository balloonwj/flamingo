/**
 *  FlamingoLink.dll —— 「在线用户」页的数据模型：合并 2504 推送，按设备展开成表格行，并提供筛选、排序与汇总。
 *
 *  名单以用户为单位保存，每条 2504 里的用户整体替换；设备列表为空的用户从名单里删除。
 *  表格每台设备一行（docs/FlamingoLink方案.md 决定 D4），行按「在线 > 勿扰 > 离开」、昵称、账号、
 *  设备类型排序。只在界面线程上使用，不加锁。
 *  单元测试见 flamingoAdmin/Tests/FlamingoLinkTests.cpp（FLK4~FLK6）。
 *
 *  典型用法：
 *      OnlineTable table;
 *      table.Apply(notify, currentEnterpriseId);
 *      std::vector<int> view;
 *      table.Filter(keyword, view);            //view 里是 Rows() 的下标
 *      OnlineSummary s = table.Summary();
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <map>
#include <vector>
#include <atlstr.h>

#include "FlamingoLink/Core/LinkProtocol.h"

namespace flamingolink {

/**
 *  表格的一行：一个用户的一台设备。
 */
struct OnlineRow
{
    OnlineRow() : m_userId(0), m_clientType(0), m_presence(0), m_loginTimeMs(0) {}

    int64_t m_userId;       //用户ID
    CString m_nickname;     //昵称；为空时界面显示账号
    CString m_account;      //登录账号
    CString m_depts;        //所在部门，多个部门用「、」连接，主部门在前
    int     m_clientType;   //客户端类型：1 PC、2 Android、3 iOS、4 Mac、5 Web
    int     m_presence;     //在线状态：1 在线、2 离开、3 勿扰
    int64_t m_loginTimeMs;  //该设备本次登录的时间，Unix 毫秒
    CString m_ip;           //该设备的来源地址
};

/**
 *  名单汇总：人数、设备数与各类设备的台数。
 */
struct OnlineSummary
{
    OnlineSummary() : m_users(0), m_devices(0), m_pc(0), m_android(0), m_ios(0), m_mac(0), m_web(0), m_other(0) {}

    int m_users;    //在线人数（有至少一台设备在线的用户数）
    int m_devices;  //在线设备数，即表格行数
    int m_pc;       //PC 台数
    int m_android;  //Android 台数
    int m_ios;      //iOS 台数
    int m_mac;      //Mac 台数
    int m_web;      //Web 台数
    int m_other;    //未知类型的台数
};

/**
 *  在线名单模型。
 */
class OnlineTable
{
public:
    OnlineTable();

    /**
     *  清空名单（换企业、退出登录时调用）。
     */
    void Clear();

    /**
     *  应用一条 2504 推送。enterpriseId 与 expectedEnterprise 不同的推送整条忽略
     *  （退订或换企业之后，服务端在途的旧推送仍可能到达）。
     *  @param notify              推送内容
     *  @param expectedEnterprise  页面当前的企业ID
     *  @return 名单确实被应用时返回 true，忽略时返回 false
     */
    bool Apply(const OnlineNotify& notify, int64_t expectedEnterprise);

    /**
     *  按排序规则排好的全部行（每台设备一行）。
     */
    const std::vector<OnlineRow>& Rows() const { return m_rows; }

    /**
     *  按关键字筛选行：昵称、账号、部门中任一项包含关键字（不区分大小写）即保留。
     *  @param keyword  关键字，首尾空白会被去掉；为空时保留全部行
     *  @param outIndex [出参] 保留下来的行在 Rows() 里的下标，按显示顺序；先清空再填充
     */
    void Filter(const CString& keyword, std::vector<int>& outIndex) const;

    /**
     *  汇总全部行（不受筛选影响）。
     */
    OnlineSummary Summary() const;

    /**
     *  在线人数。
     */
    int UserCount() const { return static_cast<int>(m_users.size()); }

    /**
     *  在线状态的排序名次：在线 0、勿扰 1、离开 2、其它 3。名次小的排在前面。
     *  @param presence 在线状态
     */
    static int PresenceRank(int presence);

    /**
     *  把部门名称用「、」连接成一段文字。
     *  @param depts 部门名称，主部门在前
     */
    static CString JoinDepts(const std::vector<CString>& depts);

private:
    //名单变化后按排序规则重建 m_rows
    void RebuildRows();

    std::map<int64_t, LinkUser> m_users;   //用户ID → 用户（只保存至少有一台设备在线的用户）
    std::vector<OnlineRow>      m_rows;    //按排序规则排好的行，由 RebuildRows 生成
};

} // namespace flamingolink
