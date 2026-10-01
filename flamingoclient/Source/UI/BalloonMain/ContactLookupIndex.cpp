/**
 *  联系人资料池 userid 索引的实现。整体说明见 ContactLookupIndex.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-01
 */

#include "ContactLookupIndex.h"

namespace balloonmain {

void ContactLookupIndex::Rebuild(const std::vector<net::ContactItem>& contacts)
{
    m_indexById.clear();
    for (size_t i = 0; i < contacts.size(); ++i)
    {
        //insert 不覆盖已有键：重复的 userid 保留第一次出现的下标
        m_indexById.insert(std::make_pair((unsigned int)contacts[i].m_userid, (int)i));
    }
}

int ContactLookupIndex::Find(unsigned int userId) const
{
    std::map<unsigned int, int>::const_iterator it = m_indexById.find(userId);
    return (it != m_indexById.end()) ? it->second : -1;
}

void ContactLookupIndex::Clear()
{
    m_indexById.clear();
}

size_t ContactLookupIndex::Count() const
{
    return m_indexById.size();
}

} // namespace balloonmain
