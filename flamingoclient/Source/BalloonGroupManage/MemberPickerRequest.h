/**
 *  MemberPickerRequest.h
 *
 *  群管理窗 → 主面板 的"打开选人对话框"请求载荷（FMG_MSG_OPEN_MEMBER_PICKER
 *  的 lParam，堆分配，主面板处理后 delete）。
 *
 *  为什么要这么一个载荷、而不是让某一边独自组装候选列表：候选来源被分在两边。
 *    · 邀请入群 —— 候选是通讯录里还不在本群的人。通讯录只有主面板有，
 *      "谁已经在群里"只有群管理窗有，故窗口把成员 id 集合传过去当排除集，
 *      由主面板做差集。
 *    · 添加禁言成员 / 转让群主 —— 候选就是本群成员，群管理窗自己的模型里
 *      现成有，直接把组好的候选列表传过去。
 *
 *  balloonwj@qq.com   2026-07-31
 */

#pragma once

#include <vector>
#include <stdint.h>

#include "../net/ContactsProtocolClient.h"   // net::ContactItem

namespace balloongroupmanage {

struct MemberPickerRequest
{
    // 用途，取值见 CCreateGroupDlg::Purpose。用 int 而不是那个枚举，是为了
    // 不让本头依赖 BalloonMain/CreateGroupDlg.h（那边反过来是要用本头的）。
    int     m_purpose;

    // 目标群ID。
    int64_t m_groupId;

    // 已在本群的成员 userid。仅"邀请入群"用得上 —— 主面板据它从通讯录里
    // 剔掉已入群的人。其余用途留空。
    std::vector<int64_t> m_excludeUserIds;

    // 已组装好的候选列表。仅"添加禁言成员 / 转让群主"用得上（候选来自群成员，
    // 由群管理窗按自己的模型现造）。"邀请入群"留空，由主面板用通讯录填。
    std::vector<net::ContactItem> m_candidates;

    MemberPickerRequest()
        : m_purpose(0)
        , m_groupId(0)
    {
    }
};

} // namespace balloongroupmanage
