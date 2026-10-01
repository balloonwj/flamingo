/**
 *  主面板压测数据生成器：按指定规模造出一整套通讯录（含多级部门）、群列表与会话列表，
 *  数据结构与网络层解析出来的应答完全相同（net::ContactsResult 等），因此可以直接交给
 *  主面板的 OnContactsArrived / OnUserGroupsArrived / OnConversationsArrived，走与正式
 *  登录后完全相同的填充路径。
 *
 *  只供调试入口 `Flamingo.exe --mainpanel --stress` 与主面板基准测试
 *  （MainPanelBench）使用，正式流程不调用。给定相同的种子，生成的数据逐字节相同，
 *  优化前后的测量结果因此可以直接对比。
 *
 *  典型用法：
 *      balloonmain::StressDataSpec spec;          // 默认规模：5000 人 / 500 群 / 3000 会话
 *      balloonmain::StressDataSet data;
 *      balloonmain::BuildStressData(spec, data);
 *      panel.OnContactsArrived(data.m_contacts);
 *      panel.OnUserGroupsArrived(data.m_groups);
 *      panel.OnConversationsArrived(data.m_conversations);
 *
 *  balloonwj@qq.com   2026-09-30
 */

#pragma once

#include "../../net/ContactsProtocolClient.h"       // net::ContactsResult
#include "../../net/ConversationsProtocolClient.h"  // net::ConversationsResult
#include "../../net/GroupProtocolClient.h"          // net::UserGroupsResult

namespace balloonmain {

// 默认规模：按一家中型企业的量级取值，足以把逐节点线性查找一类的问题放大到肉眼可见。
const int kStressDefaultContacts      = 5000;   // 联系人数
const int kStressDefaultDepartments   = 120;    // 部门数（三级，含顶级部门）
const int kStressDefaultGroups        = 500;    // 群数
const int kStressDefaultConversations = 3000;   // 会话数（单聊与群聊合计）
const unsigned int kStressDefaultSeed = 20260930;   // 默认随机种子

/**
 *  压测数据的规模与随机种子。
 */
struct StressDataSpec
{
    int          m_contactCount;       // 联系人数，>= 1
    int          m_departmentCount;    // 部门数，>= 1
    int          m_groupCount;         // 群数，>= 0
    int          m_conversationCount;  // 会话数，>= 0；超过"联系人数 + 群数"时按后者截断
    unsigned int m_seed;               // 随机种子；相同种子生成相同的数据

    StressDataSpec()
        : m_contactCount(kStressDefaultContacts)
        , m_departmentCount(kStressDefaultDepartments)
        , m_groupCount(kStressDefaultGroups)
        , m_conversationCount(kStressDefaultConversations)
        , m_seed(kStressDefaultSeed)
    {
    }
};

/**
 *  一整套压测数据。三份结果的 m_code 均为 0（成功），可以直接交给主面板。
 */
struct StressDataSet
{
    net::ContactsResult      m_contacts;       // 通讯录：部门树 + 联系人资料池 + 收藏 + 特别关注
    net::UserGroupsResult    m_groups;         // 我加入的群
    net::ConversationsResult m_conversations;  // 会话列表，按最后一条消息时间倒序

    /**
     *  返回第 index 个联系人的 userid，供基准测试挑选"收到消息的人""上下线的人"。
     *    index：0 起的序号，会对联系人数取模，任意非负值都合法。
     *  返回：userid；通讯录为空时返回 0。
     */
    int64_t ContactUserIdAt(int index) const;
};

/**
 *  按规格生成一整套压测数据。
 *    spec：规模与种子，取值范围见 StressDataSpec 各字段。
 *    out： 输出，原有内容被整体覆盖。
 *
 *  生成规则（与正式数据的形态保持一致，便于暴露真实场景下的开销）：
 *    · 部门分三级，每个联系人有一个主部门，约一成另有一个兼属部门；
 *    · 在线状态按在线四成、离开一成、勿扰半成、其余离线分布；
 *    · 特别关注 50 人、收藏 150 人加 50 个群，均不超过服务端上限；
 *    · 会话中约三成有未读，8 个置顶，约二十分之一免打扰；
 *    · 头像一律为自动头像（按名字生成字形头像），不依赖本地图片缓存。
 */
void BuildStressData(const StressDataSpec& spec, StressDataSet& out);

} // namespace balloonmain
