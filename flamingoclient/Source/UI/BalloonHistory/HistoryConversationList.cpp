/**
 *  聊天记录管理器左栏会话列表的纯逻辑实现，说明见 HistoryConversationList.h。
 *  本文件不 include Flamingo 的 stdafx.h：它同时被 Flamingo.vcxproj（该文件设为不使用预编译头）
 *  与 FlamingoUnit.vcxproj 编译。
 *  balloonwj@qq.com   2026-10-03
 */
#include "HistoryConversationList.h"

#include <windows.h>   //MultiByteToWideChar（本地摘要由 UTF-8 转成宽字符）
#include <stdio.h>     //swprintf_s
#include <algorithm>
#include <map>
#include <utility>

#include "HistoryView.h"                    //DigestFromContent（本地摘要，Markdown 消息为「[Markdown] 标题」）
#include "../BalloonSearch/ContactMatch.h"  //contactsearch::Match（名字与拼音匹配）
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonhistory {

namespace {

//生成范围按钮、命中说明文字用的缓冲区长度（字符数）：最长的一句约 15 个字符，留足余量
const size_t kTextBufChars = 64;

//会话的排序键 (ctype, cid)
typedef std::pair<int, int64_t> ConvKey;

//UTF-8 → UTF-16。转换失败或输入为空时返回空串
std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty())
    {
        return std::wstring();
    }
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    if (n <= 0)
    {
        return std::wstring();
    }
    std::wstring out((size_t)n, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n);
    return out;
}

//SortManagerConversations 的比较函数：a 是否应排在 b 前面
bool ManagerItemBefore(const ManagerConversationItem& a, const ManagerConversationItem& b)
{
    if (a.m_lastTimestamp != b.m_lastTimestamp)
    {
        return a.m_lastTimestamp > b.m_lastTimestamp;
    }
    if (a.m_lastMsgid != b.m_lastMsgid)
    {
        return a.m_lastMsgid > b.m_lastMsgid;
    }
    if (a.m_ctype != b.m_ctype)
    {
        return a.m_ctype < b.m_ctype;
    }
    return a.m_cid < b.m_cid;
}

//本地那份是否比已有的那份新：时间较晚，或时间相同而 msgid 较大。两项都相同时不算新，
//保留已有的服务端那份
bool LocalIsNewer(const LocalConversationBrief& local, const ManagerConversationItem& existing)
{
    if (local.m_lastTimestamp != existing.m_lastTimestamp)
    {
        return local.m_lastTimestamp > existing.m_lastTimestamp;
    }
    return local.m_lastMsgid > existing.m_lastMsgid;
}

//NormalizeManagerScope 的比较函数：按 (ctype, cid) 升序
bool ScopeKeyLess(const HistoryConversationKey& a, const HistoryConversationKey& b)
{
    if (a.m_ctype != b.m_ctype)
    {
        return a.m_ctype < b.m_ctype;
    }
    return a.m_cid < b.m_cid;
}

//搜索范围里是否有这个会话。sortedScope 须已按 ScopeKeyLess 升序排好（NormalizeManagerScope 的结果）
bool ScopeContains(const std::vector<HistoryConversationKey>& sortedScope, int ctype, int64_t cid)
{
    return std::binary_search(sortedScope.begin(), sortedScope.end(),
                              HistoryConversationKey(ctype, cid), ScopeKeyLess);
}

//NormalizeManagerScope 去重用：两个键是否相同
bool ScopeKeyEqual(const HistoryConversationKey& a, const HistoryConversationKey& b)
{
    return a.m_ctype == b.m_ctype && a.m_cid == b.m_cid;
}

} // anonymous namespace

std::vector<ManagerConversationItem> MergeManagerConversations(
    const std::vector<ManagerServerConversation>& server,
    const std::vector<LocalConversationBrief>& local,
    const std::set<int64_t>& myGroupIds, bool groupsLoaded)
{
    const int kCtypeGroup = 2;
    std::vector<ManagerConversationItem> items;
    //(ctype, cid) → items 里的下标
    std::map<ConvKey, size_t> index;

    //1) 服务端那份照搬。服务端列表理论上不会有重复的会话，万一有，保留较新的一项
    for (size_t i = 0; i < server.size(); ++i)
    {
        const ManagerServerConversation& s = server[i];
        ManagerConversationItem item;
        item.m_ctype = s.m_ctype;
        item.m_cid = s.m_cid;
        item.m_lastMsgid = s.m_lastMsgid;
        item.m_lastTimestamp = s.m_lastTimestamp;
        item.m_digest = s.m_digest;
        item.m_inServerList = true;

        const ConvKey key(s.m_ctype, s.m_cid);
        std::map<ConvKey, size_t>::iterator it = index.find(key);
        if (it == index.end())
        {
            index[key] = items.size();
            items.push_back(item);
        }
        else if (ManagerItemBefore(item, items[it->second]))
        {
            items[it->second] = item;
        }
    }

    //2) 本地那份：已有的取较新的一份，没有的追加（已离开的群除外）
    for (size_t i = 0; i < local.size(); ++i)
    {
        const LocalConversationBrief& l = local[i];
        const ConvKey key(l.m_ctype, l.m_cid);
        std::map<ConvKey, size_t>::iterator it = index.find(key);
        if (it != index.end())
        {
            ManagerConversationItem& existing = items[it->second];
            if (LocalIsNewer(l, existing))
            {
                existing.m_lastMsgid = l.m_lastMsgid;
                existing.m_lastTimestamp = l.m_lastTimestamp;
                existing.m_digest = Utf8ToWide(DigestFromContent(l.m_lastMsgtype, l.m_lastContent));
            }
            continue;
        }

        if (l.m_ctype == kCtypeGroup && groupsLoaded && myGroupIds.find(l.m_cid) == myGroupIds.end())
        {
            continue;
        }

        ManagerConversationItem item;
        item.m_ctype = l.m_ctype;
        item.m_cid = l.m_cid;
        item.m_lastMsgid = l.m_lastMsgid;
        item.m_lastTimestamp = l.m_lastTimestamp;
        item.m_digest = Utf8ToWide(DigestFromContent(l.m_lastMsgtype, l.m_lastContent));
        item.m_inServerList = false;
        index[key] = items.size();
        items.push_back(item);
    }

    SortManagerConversations(items);
    return items;
}

void SortManagerConversations(std::vector<ManagerConversationItem>& items)
{
    std::stable_sort(items.begin(), items.end(), ManagerItemBefore);
}

int FindManagerConversation(const std::vector<ManagerConversationItem>& items, int ctype, int64_t cid)
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].m_ctype == ctype && items[i].m_cid == cid)
        {
            return (int)i;
        }
    }
    return -1;
}

bool MatchesManagerTypeFilter(int ctype, int filter)
{
    const int kCtypeSingle = 1;
    const int kCtypeGroup = 2;
    switch (filter)
    {
    //单聊：只认 ctype 为 1 的会话
    case kManagerTypeSingle:
        return ctype == kCtypeSingle;

    //群聊：只认 ctype 为 2 的会话
    case kManagerTypeGroup:
        return ctype == kCtypeGroup;

    //全部，以及不认识的取值：不按类型筛选
    default:
        return true;
    }
}

std::vector<int> FilterManagerConversations(const std::vector<ManagerConversationItem>& items,
                                            int filter, const std::wstring& nameQuery,
                                            const std::vector<HistoryConversationKey>& scope)
{
    const std::vector<HistoryConversationKey> sortedScope = NormalizeManagerScope(scope);
    std::vector<int> rows;
    for (size_t i = 0; i < items.size(); ++i)
    {
        const ManagerConversationItem& item = items[i];
        if (!MatchesManagerTypeFilter(item.m_ctype, filter))
        {
            continue;
        }
        if (!sortedScope.empty() && !ScopeContains(sortedScope, item.m_ctype, item.m_cid))
        {
            continue;
        }
        if (!nameQuery.empty())
        {
            //只匹配名字：会话没有账号、地区这两项，传空串
            const contactsearch::MatchResult r =
                contactsearch::Match(nameQuery, item.m_name, std::wstring(), std::wstring());
            if (!r.m_matched)
            {
                continue;
            }
        }
        rows.push_back((int)i);
    }
    return rows;
}

int ResolveManagerSelection(const std::vector<ManagerConversationItem>& items,
                            int selCtype, int64_t selCid, int oldIndex)
{
    if (items.empty())
    {
        return -1;
    }
    if (selCid != 0)
    {
        const int found = FindManagerConversation(items, selCtype, selCid);
        if (found >= 0)
        {
            return found;
        }
    }
    if (oldIndex < 0)
    {
        return 0;
    }
    const int last = (int)items.size() - 1;
    return (oldIndex <= last) ? oldIndex : last;
}

std::wstring BuildManagerScopeButtonText(size_t selectedCount)
{
    if (selectedCount == 0)
    {
        return TR(L"范围：全部会话");
    }
    wchar_t buf[kTextBufChars] = { 0 };
    swprintf_s(buf, TR(L"范围：已选 %u 个会话"), (unsigned)selectedCount);
    return buf;
}

std::wstring BuildManagerHitText(int hitCount, const std::wstring& latestText)
{
    if (hitCount == 1 && !latestText.empty())
    {
        return latestText;
    }
    wchar_t buf[kTextBufChars] = { 0 };
    swprintf_s(buf, TR(L"%d 条相关聊天记录"), hitCount);
    return buf;
}

std::vector<HistoryConversationKey> NormalizeManagerScope(const std::vector<HistoryConversationKey>& selected)
{
    std::vector<HistoryConversationKey> out(selected);
    std::sort(out.begin(), out.end(), ScopeKeyLess);
    out.erase(std::unique(out.begin(), out.end(), ScopeKeyEqual), out.end());
    return out;
}

bool IsInManagerScope(const std::vector<HistoryConversationKey>& scope, const HistoryConversationKey& key)
{
    return ScopeContains(scope, key.m_ctype, key.m_cid);
}

bool ToggleManagerScopeKey(std::vector<HistoryConversationKey>& scope, const HistoryConversationKey& key)
{
    std::vector<HistoryConversationKey>::iterator it =
        std::lower_bound(scope.begin(), scope.end(), key, ScopeKeyLess);
    if (it != scope.end() && ScopeKeyEqual(*it, key))
    {
        scope.erase(it);
        return false;
    }
    scope.insert(it, key);
    return true;
}

std::wstring BuildManagerScopeSelectedText(size_t selectedCount)
{
    if (selectedCount == 0)
    {
        //弹层底部这一格较窄（右边还有两个按钮），只写「未选择」；不选等于搜全部会话，旁边的
        //「全部会话」按钮已经说明了这一点
        return TR(L"未选择");
    }
    wchar_t buf[kTextBufChars] = { 0 };
    swprintf_s(buf, TR(L"已选 %u 个"), (unsigned)selectedCount);
    return buf;
}

} // namespace balloonhistory
