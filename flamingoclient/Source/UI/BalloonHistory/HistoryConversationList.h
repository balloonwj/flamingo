/**
 *  聊天记录管理器左栏会话列表的纯逻辑，HistoryConversationList.h
 *
 *  聊天记录管理器（docs/聊天记录管理器方案.md）的左栏列出所有有聊天记录的会话。数据有两份：
 *  主面板的会话缓存（服务端会话列表）与本地库里有记录的会话（CV2MsgStore::QueryConversationList）。
 *  本文件负责把两份合并成一份有序的列表，以及按类型、名字、搜索范围筛选，列表重建后保持选中项，
 *  范围按钮与搜索结果行的文字。不碰窗口、数据库与网络，全部由单元测试覆盖
 *  （Tests/HistoryConversationListTests.cpp，标签 [historymgr]）。
 *
 *  典型用法（消息记录窗）：
 *      std::vector<ManagerConversationItem> items =
 *          MergeManagerConversations(serverItems, localBriefs, myGroupIds, groupsLoaded);
 *      //逐项填 m_name（名字要经主面板解析）
 *      std::vector<int> rows = FilterManagerConversations(items, kManagerTypeAll, query, scope);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <set>
#include <string>
#include <vector>
#include <stdint.h>
#include "V2MsgQuery.h"   //LocalConversationBrief / HistoryConversationKey

namespace balloonhistory {

//服务端会话列表里的一条，由主面板会话缓存（net::ConversationItem）折算而来。摘要由主面板按
//会话列表的规则算好（撤回的改写成「你 / 某某撤回了一条消息」），这里只是照搬。
struct ManagerServerConversation
{
    int          m_ctype;          //会话类型：1单聊 2群聊
    int64_t      m_cid;            //会话ID（我方视角）
    int64_t      m_lastMsgid;      //最后一条消息的服务器 msgid
    int64_t      m_lastTimestamp;  //最后一条消息的时间（Unix毫秒）
    std::wstring m_digest;         //显示用摘要

    ManagerServerConversation()
        : m_ctype(1)
        , m_cid(0)
        , m_lastMsgid(0)
        , m_lastTimestamp(0)
    {
    }
};

//左栏里的一个会话（合并后的结果）。
struct ManagerConversationItem
{
    int          m_ctype;          //会话类型：1单聊 2群聊
    int64_t      m_cid;            //会话ID（我方视角）
    int64_t      m_lastMsgid;      //最后一条消息的 msgid（取两份里较新的那一份）
    int64_t      m_lastTimestamp;  //最后一条消息的时间（Unix毫秒），排序依据
    std::wstring m_digest;         //显示用摘要，与 m_lastTimestamp 取自同一份数据
    bool         m_inServerList;   //服务端会话列表里有没有这个会话；false 表示只在本地库里有
                                   //（例如在主面板「从列表删除」过）
    std::wstring m_name;           //显示名。合并时留空，由窗口经主面板解析后填入，名字筛选按它匹配

    ManagerConversationItem()
        : m_ctype(1)
        , m_cid(0)
        , m_lastMsgid(0)
        , m_lastTimestamp(0)
        , m_inServerList(false)
    {
    }
};

//左栏顶部的类型筛选
enum ManagerTypeFilter
{
    kManagerTypeAll    = 0,   //全部：单聊与群聊都列出
    kManagerTypeSingle = 1,   //单聊：只列 ctype 为 1 的会话
    kManagerTypeGroup  = 2    //群聊：只列 ctype 为 2 的会话
};

/**
 *  合并服务端会话列表与本地库的会话汇总。
 *
 *  规则：
 *    1. 按 (ctype, cid) 去重。两份都有时，最后时间取较晚的那一份，摘要与 msgid 跟着取那一份；
 *       时间相同时取 msgid 较大的；两项都相同时取服务端那份（它的摘要处理过撤回）。
 *    2. 只在本地有的群会话：groupsLoaded 为 true 且本人不在 myGroupIds 里时不列出 —— 离开群时
 *       本地记录理应已被删除，这一条用来处理旧版本留下的残余数据。群列表还没加载时照常列出。
 *       服务端列表里的群会话不受此限（服务端本来就按当前成员关系返回）。
 *    3. 只在本地有的会话，摘要由最后一条的内容经 PreviewFromContent 生成。
 *    4. 结果按 SortManagerConversations 排序；m_name 一律留空。
 *
 *  @param server        服务端会话列表
 *  @param local         本地库的会话汇总
 *  @param myGroupIds    本人所在的全部群的群 id
 *  @param groupsLoaded  群列表是否已经从服务端加载；false 时 myGroupIds 不可信，不据它过滤
 *  @return 合并、排序后的会话列表
 */
std::vector<ManagerConversationItem> MergeManagerConversations(
    const std::vector<ManagerServerConversation>& server,
    const std::vector<LocalConversationBrief>& local,
    const std::set<int64_t>& myGroupIds, bool groupsLoaded);

/**
 *  把会话列表按最后时间倒序排列；时间相同按最后一条的 msgid 倒序，再相同按会话类型、cid 升序。
 *  不把置顶会话排在前面 —— 管理器按时间浏览记录，置顶是收消息时的概念。
 *  @param items  [入出参] 待排序的列表
 */
void SortManagerConversations(std::vector<ManagerConversationItem>& items);

/**
 *  在列表里找某个会话。
 *  @return 下标；找不到返回 -1
 */
int FindManagerConversation(const std::vector<ManagerConversationItem>& items, int ctype, int64_t cid);

/**
 *  某个会话是否符合类型筛选。
 *  @param ctype   会话类型
 *  @param filter  类型筛选，取值见 ManagerTypeFilter；不认识的取值按「全部」处理
 */
bool MatchesManagerTypeFilter(int ctype, int filter);

/**
 *  按类型、名字与搜索范围筛选左栏要显示的会话。
 *  @param items      合并后的会话列表（m_name 应已填好）
 *  @param filter     类型筛选，取值见 ManagerTypeFilter
 *  @param nameQuery  名字查询词（已去首尾空白）；为空表示不按名字筛选。匹配规则与通讯录搜索相同
 *                    （contactsearch::Match：名字子串、拼音首字母、全拼，ASCII 大小写不敏感）
 *  @param scope      搜索范围；为空表示不限，不为空时只保留范围里的会话
 *  @return 符合条件的会话在 items 里的下标，保持 items 的顺序
 */
std::vector<int> FilterManagerConversations(const std::vector<ManagerConversationItem>& items,
                                            int filter, const std::wstring& nameQuery,
                                            const std::vector<HistoryConversationKey>& scope);

/**
 *  列表重建后（会话增减、重新排序）应选中哪一行。
 *  @param items        重建后的列表
 *  @param selCtype     重建前选中的会话类型
 *  @param selCid       重建前选中的会话ID；0 表示重建前没有选中项
 *  @param oldIndex     重建前选中项的下标；没有选中项时传 -1
 *  @return 原来选中的会话还在就返回它的新下标；已经不在了（被移出群、群被解散等）返回原位置
 *          上的那一行，原位置已超出末尾时返回最后一行；没有选中项时返回 0；列表为空返回 -1
 */
int ResolveManagerSelection(const std::vector<ManagerConversationItem>& items,
                            int selCtype, int64_t selCid, int oldIndex);

/**
 *  范围按钮的文字。
 *  @param selectedCount  勾选的会话个数；0 表示不限
 *  @return 「范围：全部会话」或「范围：已选 N 个会话」
 */
std::wstring BuildManagerScopeButtonText(size_t selectedCount);

/**
 *  左栏搜索结果里「聊天记录」一段每一行的副行文字。
 *  @param hitCount    该会话里命中的条数
 *  @param latestText  最近一条命中消息的搜索文本（UTF-16）
 *  @return 只命中一条时就是那一条的文字（为空时退回条数说明）；否则为「N 条相关聊天记录」
 */
std::wstring BuildManagerHitText(int hitCount, const std::wstring& latestText);

/**
 *  把搜索范围整理成去重后的列表，供 CV2MsgStore::SearchConversationsForManager 使用。
 *  @param selected  勾选的会话（可能有重复）
 *  @return 去重后的范围，按 (ctype, cid) 升序
 */
std::vector<HistoryConversationKey> NormalizeManagerScope(const std::vector<HistoryConversationKey>& selected);

/**
 *  某个会话是否在搜索范围里。
 *  @param scope  NormalizeManagerScope 整理过的范围（已去重、升序）
 *  @param key    要判断的会话
 */
bool IsInManagerScope(const std::vector<HistoryConversationKey>& scope, const HistoryConversationKey& key);

/**
 *  在搜索范围里勾选或取消一个会话：不在范围里就加入，在就移除。范围始终保持去重、升序。
 *  @param scope  [入出参] NormalizeManagerScope 整理过的范围
 *  @param key    被点的会话
 *  @return 操作之后这个会话是否在范围里
 */
bool ToggleManagerScopeKey(std::vector<HistoryConversationKey>& scope, const HistoryConversationKey& key);

/**
 *  范围弹层底部的勾选个数文字。
 *  @param selectedCount  勾选的会话个数
 *  @return 「未选择」或「已选 N 个」
 */
std::wstring BuildManagerScopeSelectedText(size_t selectedCount);

} // namespace balloonhistory
