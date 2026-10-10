/**
 *  本会话共享列表浮层纯逻辑单元测试（Suite SCV，标签 [shareconv]），ShareConversationModelTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 1 期第 16 步）：
 *    - SCV1 合并：按 shareid 去重，未失效的在前、已失效的在后，各自按发起时间从新到旧，时间相同按 shareid 从大到小；
 *    - SCV2 每行的操作：本人发起的可以调整成员与取消；共享给本人的可以打开；群主、群管理员还可以取消群里别人的共享，
 *           普通成员与单聊不行；已失效的没有操作；
 *    - SCV3 状态文字：可用时为「可用」，其余与浏览窗口横幅一致。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../Engine/ShareConversationModel.h"
#include "../Engine/ShareBrowseModel.h"
#include "../../Source/net/Protocol.h"

using namespace fileshare;

namespace {

//本人与另一名成员
const int64_t kSelf = 40001;
const int64_t kOther = 40002;

//几个发起时间（Unix 毫秒）
const int64_t kT1 = 1759370000000LL;
const int64_t kT2 = kT1 + 1000;
const int64_t kT3 = kT1 + 2000;

ShareListEntry Entry(int64_t shareId, int64_t ownerId, int64_t createTime, int state, int ctype)
{
    ShareListEntry e;
    e.m_shareId = shareId;
    e.m_ownerId = ownerId;
    e.m_createTime = createTime;
    e.m_state = state;
    e.m_ctype = ctype;
    return e;
}

} // namespace

TEST_CASE("SCV1 merge and order", "[fileshare][shareconv]")
{
    std::vector<ShareListEntry> mine;
    mine.push_back(Entry(1, kSelf, kT1, share_state_available, conversation_group));
    mine.push_back(Entry(2, kSelf, kT3, share_state_invalid, conversation_group));
    std::vector<ShareListEntry> sharedMe;
    sharedMe.push_back(Entry(3, kOther, kT2, share_state_unavailable, conversation_group));
    sharedMe.push_back(Entry(4, kOther, kT2, share_state_available, conversation_group));
    sharedMe.push_back(Entry(1, kSelf, kT1, share_state_available, conversation_group));   //重复的不再出现

    const std::vector<ShareListEntry> rows = ShareConversationModel::Merge(mine, sharedMe);
    REQUIRE(rows.size() == 4);
    CHECK(rows[0].m_shareId == 4);   //kT2，shareid 大的在前
    CHECK(rows[1].m_shareId == 3);
    CHECK(rows[2].m_shareId == 1);
    CHECK(rows[3].m_shareId == 2);   //已失效的在最后，即使它最新
    CHECK(ShareConversationModel::Merge(std::vector<ShareListEntry>(), std::vector<ShareListEntry>()).empty());
}

TEST_CASE("SCV2 row actions", "[fileshare][shareconv]")
{
    const ShareListEntry myShare = Entry(1, kSelf, kT1, share_state_available, conversation_group);
    const ShareListEntry othersGroup = Entry(2, kOther, kT1, share_state_available, conversation_group);
    const ShareListEntry othersSingle = Entry(3, kOther, kT1, share_state_unavailable, conversation_single);
    const ShareListEntry invalid = Entry(4, kOther, kT1, share_state_invalid, conversation_group);

    CHECK(ShareConversationModel::RowActions(myShare, kSelf, kShareRoleMember)
          == (share_row_action_edit | share_row_action_cancel));
    CHECK(ShareConversationModel::RowActions(othersGroup, kSelf, kShareRoleMember) == share_row_action_open);
    CHECK(ShareConversationModel::RowActions(othersGroup, kSelf, kShareRoleUnknown) == share_row_action_open);
    CHECK(ShareConversationModel::RowActions(othersGroup, kSelf, kShareRoleAdmin)
          == (share_row_action_open | share_row_action_cancel));
    CHECK(ShareConversationModel::RowActions(othersGroup, kSelf, kShareRoleOwner)
          == (share_row_action_open | share_row_action_cancel));
    //单聊没有群角色，暂不可用的也能点「打开」（浏览窗口里显示原因）
    CHECK(ShareConversationModel::RowActions(othersSingle, kSelf, kShareRoleOwner) == share_row_action_open);
    CHECK(ShareConversationModel::RowActions(invalid, kSelf, kShareRoleOwner) == 0);
}

TEST_CASE("SCV3 status text", "[fileshare][shareconv]")
{
    CHECK(ShareConversationModel::StatusText(share_state_available, 0) == L"可用");
    CHECK(ShareConversationModel::StatusText(share_state_unavailable, share_unavailable_owner_offline)
          == ShareBrowseModel::StateText(share_state_unavailable, share_unavailable_owner_offline));
    CHECK(ShareConversationModel::StatusText(share_state_invalid, share_invalid_owner_canceled).find(L"已失效")
          != std::wstring::npos);
}
