/**
 *  帖子群不可用时的列表过滤，见 ThreadGroupFilter.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "ThreadGroupFilter.h"

#include "ThreadGroupGate.h"
#include "../../net/Protocol.h"   // conversation_group

namespace balloonthread {

std::set<int64_t> HiddenThreadGroupIds(const std::map<int64_t, int>& groupTypes, bool available)
{
    std::set<int64_t> hidden;
    for (std::map<int64_t, int>::const_iterator it = groupTypes.begin(); it != groupTypes.end(); ++it)
    {
        if (IsHiddenGroup(it->second, available))
            hidden.insert(it->first);
    }
    return hidden;
}

std::vector<net::UserGroupItem> FilterGroups(const std::vector<net::UserGroupItem>& groups, const std::set<int64_t>& hidden)
{
    std::vector<net::UserGroupItem> visible;
    visible.reserve(groups.size());
    for (size_t i = 0; i < groups.size(); ++i)
    {
        if (hidden.find(groups[i].m_groupid) == hidden.end())
            visible.push_back(groups[i]);
    }
    return visible;
}

bool IsHiddenConversation(int ctype, int64_t cid, const std::set<int64_t>& hidden)
{
    return ctype == conversation_group && hidden.find(cid) != hidden.end();
}

std::vector<net::ConversationItem> FilterConversations(const std::vector<net::ConversationItem>& conversations,
                                                       const std::set<int64_t>& hidden)
{
    std::vector<net::ConversationItem> visible;
    visible.reserve(conversations.size());
    for (size_t i = 0; i < conversations.size(); ++i)
    {
        if (!IsHiddenConversation(conversations[i].m_ctype, conversations[i].m_cid, hidden))
            visible.push_back(conversations[i]);
    }
    return visible;
}

//UTF-8 → UTF-16；空串或转换失败返回空串
static std::wstring Utf8ToWideText(const std::string& text)
{
    if (text.empty())
        return std::wstring();
    const int chars = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), NULL, 0);
    if (chars <= 0)
        return std::wstring();
    std::wstring out(static_cast<size_t>(chars), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], chars);
    return out;
}

std::wstring ThreadMentionName(const std::string& cardUtf8, const std::string& nicknameUtf8)
{
    const std::wstring card = Utf8ToWideText(cardUtf8);
    if (!card.empty())
        return card;
    return Utf8ToWideText(nicknameUtf8);
}

size_t DigestTagLength(const std::wstring& digest, const std::vector<std::wstring>& tags)
{
    size_t best = 0;
    for (size_t i = 0; i < tags.size(); ++i)
    {
        const std::wstring& tag = tags[i];
        if (!tag.empty() && tag.size() > best && digest.compare(0, tag.size(), tag) == 0)
            best = tag.size();
    }
    return best;
}

} // namespace balloonthread
