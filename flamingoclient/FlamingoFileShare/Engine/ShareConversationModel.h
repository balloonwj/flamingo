/**
 *  本会话共享列表浮层的纯逻辑，ShareConversationModel.h
 *
 *  聊天窗工具栏的「文件共享」按钮弹出的浮层（docs/文件共享方案.md 7.4「本会话共享列表」）列出本会话里本人能看到的共享：
 *  本人发起的（2205 我的共享，按会话筛选）与共享给本人的（2205 共享给我的，按会话筛选）。本文件负责：
 *    - Merge：合并两份列表（按 shareid 去重），按发起时间从新到旧排序，已失效的排在最后；
 *    - RowActions：每一行可做的操作 —— 本人发起的可以「调整成员」「取消共享」，共享给本人的可以「打开」，
 *      群主与群管理员还可以取消群里别人的共享（服务端仍是权威判定方）；已失效的只能看，没有操作；
 *    - StatusText：一行的状态文字（可用时显示「可用」）。
 *
 *  单元测试见 FlamingoFileShare/Tests/ShareConversationModelTests.cpp（标签 [shareconv]）。
 *
 *  典型用法：
 *      std::vector<ShareListEntry> rows = ShareConversationModel::Merge(mine, sharedMe);
 *      int actions = ShareConversationModel::RowActions(rows[i], selfId, myRole);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "ShareProtocolCodec.h"   //ShareListEntry

namespace fileshare {

//浮层一行可做的操作，按位组合
enum share_row_action
{
    share_row_action_open   = 1,   //打开（共享给本人的）：显示浏览窗口
    share_row_action_edit   = 2,   //调整成员（本人发起的）
    share_row_action_cancel = 4,   //取消共享（本人发起的；群主、群管理员对群里别人的共享）
};

//本人在群里的角色（与 Protocol.h 的 group_member_role 一致）；单聊或不知道时取 kShareRoleUnknown
const int kShareRoleUnknown = -1;
const int kShareRoleMember = 0;
const int kShareRoleAdmin = 1;
const int kShareRoleOwner = 2;

/**
 *  ShareConversationModel —— 本会话共享列表的纯逻辑；全部为静态方法，不可实例化。
 */
class ShareConversationModel
{
public:
    /**
     *  合并「我的共享」与「共享给我的」两份列表。
     *  @param mine     我的共享（按会话筛选后的 2205 应答）
     *  @param sharedMe 共享给我的（按会话筛选后的 2205 应答）
     *  @return 按 shareid 去重后的列表：未失效的在前、已失效的在后，各自按发起时间从新到旧（时间相同按 shareid 从大到小）
     */
    static std::vector<ShareListEntry> Merge(const std::vector<ShareListEntry>& mine,
                                             const std::vector<ShareListEntry>& sharedMe);

    /**
     *  一行可做的操作。
     *  @param entry  共享
     *  @param selfId 本人 userid
     *  @param myRole 本人在群里的角色；单聊或不知道时传 kShareRoleUnknown
     *  @return share_row_action 的组合；没有可做的操作时为 0
     */
    static int RowActions(const ShareListEntry& entry, int64_t selfId, int myRole);

    /**
     *  一行的状态文字：可用时为「可用」，其余用浏览窗口横幅的同一套说法。
     *  @param state  对本人的状态
     *  @param reason 原因
     */
    static std::wstring StatusText(int state, int reason);

private:
    ShareConversationModel() = delete;
};

} // namespace fileshare
