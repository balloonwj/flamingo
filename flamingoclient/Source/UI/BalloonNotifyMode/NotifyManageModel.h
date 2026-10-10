/**
 *  NotifyManageModel.h
 *
 *  批量管理窗口「消息提醒管理」的纯逻辑（docs/消息提醒模式与消息盒子方案.md §4.6）：
 *    · 两个页签各列哪些会话（BuildVisibleItems）：群聊页是我加入的全部群；单聊页是聊过的人加上
 *      设置过非默认模式的人，搜索框有字时再在通讯录全体同事里找；
 *    · 搜索过滤（MatchesFilter）：名字或附加字段（账号 / 群号）里包含搜索词，不区分大小写；
 *    · 勾选集合（ManageSelection）：按 (ctype, cid) 记，换搜索词不丢；
 *    · 点「应用」时要提交的条目（BuildApplyEntries）与特别关注上限的预检（WouldExceedStarredLimit）。
 *
 *  只依赖 STL、net::NotifyModeEntry 与 NotifyModeModel，不依赖 balloonui 与 stdafx.h，可被
 *  Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程逐字共用；在 Flamingo.vcxproj 里本文件需
 *  标记 PrecompiledHeader=NotUsing。
 *
 *  典型用法（CBalloonNotifyManageWindow）：
 *      bool truncated = false;
 *      m_visible = balloonnotifymode::BuildVisibleItems(m_pools, m_ctype, filter, truncated);
 *      ...用户勾选...
 *      int skipped = 0;
 *      std::vector<net::NotifyModeEntry> entries =
 *          balloonnotifymode::BuildApplyEntries(m_selection, m_ctype, target, modes, skipped);
 *
 *  balloonwj@qq.com   2026-10-03
 */

#pragma once

#include <stdint.h>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "../../net/ConversationsProtocolClient.h"   // net::NotifyModeEntry
#include "../BalloonMain/NotifyModeModel.h"

namespace balloonnotifymode {

// 一个页签最多列出的行数。单聊页搜索时要在全体同事里找，几千人一次铺开既没法看、
// 排版也慢；超出时截断，并提示用户输入更精确的搜索词。
const size_t kNotifyManageMaxRows = 500;

/**
 *  ManageItem —— 列表里的一行：一个会话。
 */
struct ManageItem
{
    int          m_ctype;    // 会话类型：1 单聊、2 群聊
    int64_t      m_cid;      // 会话 ID：单聊为对端 userid，群聊为群 id
    std::wstring m_name;     // 显示名（联系人备注优先，群取群名），也是搜索的主要字段
    std::wstring m_extra;    // 附加搜索字段（联系人账号、群号），可空；不显示

    ManageItem()
        : m_ctype(0)
        , m_cid(0)
    {
    }
};

/**
 *  ManagePools —— 两个页签的候选来源，由主面板按会话缓存、群列表、通讯录与模式表组好。
 */
struct ManagePools
{
    std::vector<ManageItem> m_groups;       // 群聊页：我加入的全部群
    std::vector<ManageItem> m_singleBase;   // 单聊页的默认内容：聊过的人 + 设置过非默认模式的人（已去重，不含自己）
    std::vector<ManageItem> m_contacts;     // 单聊页搜索时的补充：通讯录全体同事（不含自己）
};

/**
 *  某一行是否匹配搜索词：名字或附加字段里包含搜索词（不区分大小写）。搜索词为空时总是匹配。
 *    item：   一行。
 *    filter： 搜索词（调用方已去掉首尾空白）。
 */
bool MatchesFilter(const ManageItem& item, const std::wstring& filter);

/**
 *  某个页签当前要列出的行。纯函数。
 *    pools：     候选来源。
 *    ctype：     页签：1 单聊页、2 群聊页；其它值返回空。
 *    filter：    搜索词（调用方已去掉首尾空白）；空表示不过滤。
 *    truncated： [出参] 是否因超过 kNotifyManageMaxRows 被截断。
 *  返回：群聊页为群池里匹配的群；单聊页为基础池里匹配的人，搜索词非空时再接上通讯录里匹配、
 *        且不在基础池里的人。保持来源里的顺序，最多 kNotifyManageMaxRows 行。
 */
std::vector<ManageItem> BuildVisibleItems(const ManagePools& pools, int ctype,
                                          const std::wstring& filter, bool& truncated);

/**
 *  勾选态：三种取值用于「全选」框。
 */
enum ManageCheckState
{
    kManageCheckNone = 0,   // 列表里一行都没勾（或列表为空）
    kManageCheckAll  = 1,   // 列表里每一行都勾了
    kManageCheckSome = 2,   // 勾了一部分
};

/**
 *  ManageSelection —— 勾选集合，按 (ctype, cid) 记。换搜索词、换页签都不丢：用户可以搜几次、
 *  每次勾几个，最后一起应用。
 */
class ManageSelection
{
public:
    /**
     *  切换某个会话的勾选态。
     *    ctype / cid：会话。
     */
    void Toggle(int ctype, int64_t cid);

    /**
     *  设置某个会话的勾选态。
     *    ctype / cid：会话。
     *    checked：    true 勾选、false 取消。
     */
    void SetChecked(int ctype, int64_t cid, bool checked);

    /**
     *  查询某个会话是否勾选。
     */
    bool IsChecked(int ctype, int64_t cid) const;

    /**
     *  把一批行全部勾上或全部取消（「全选」框）。
     *    items：  当前列出的行。
     *    checked：true 全勾、false 全取消。
     */
    void SetAll(const std::vector<ManageItem>& items, bool checked);

    /**
     *  一批行的整体勾选态（ManageCheckState），供「全选」框显示。
     */
    int  StateOf(const std::vector<ManageItem>& items) const;

    /**
     *  某个页签里勾选了几个会话（不论当前是否列出）。
     *    ctype：1 单聊、2 群聊。
     */
    int  CountOf(int ctype) const;

    /**
     *  清空全部勾选。
     */
    void Clear();

    /**
     *  取某个页签里勾选的全部会话 ID，按 cid 升序。
     *    ctype：1 单聊、2 群聊。
     */
    std::vector<int64_t> CheckedIds(int ctype) const;

private:
    std::set<std::pair<int, int64_t> > m_checked;   // 勾选的会话 (ctype, cid)
};

/**
 *  点「应用」时要提交的条目：某个页签里勾选的会话中，当前模式与目标模式不同的那些。纯函数。
 *    sel：        勾选集合。
 *    ctype：      页签：1 单聊、2 群聊。
 *    targetMode： 目标模式（Protocol.h 的 enum conversation_notify_mode）。
 *    modes：      当前的提醒模式表。
 *    skipped：    [出参] 勾选了、但不生成条目的会话数：模式已经是目标模式的，以及单聊设屏蔽（单聊
 *                 没有屏蔽）的。
 *  返回：条目列表，按 cid 升序；可以为空。
 */
std::vector<net::NotifyModeEntry> BuildApplyEntries(const ManageSelection& sel, int ctype, int targetMode,
                                                    const balloonmain::NotifyModeModel& modes, int& skipped);

/**
 *  按这些条目设完之后，特别关注的会话数（单聊与群聊合计）会不会超过上限 kNotifyModeStarredMax。
 *  点「应用」前预检，超了就提示用户、不发请求（服务端同样会拒，提示要在本地先给）。纯函数。
 *    modes：   当前的提醒模式表。
 *    entries： 要提交的条目。
 */
bool WouldExceedStarredLimit(const balloonmain::NotifyModeModel& modes,
                             const std::vector<net::NotifyModeEntry>& entries);

} // namespace balloonnotifymode
