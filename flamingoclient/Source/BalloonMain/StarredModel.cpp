/**
 *  「特别关注」内存模型实现，StarredModel.cpp
 *  设计与约定见 StarredModel.h。
 *  balloonwj@qq.com   2026-08-12
 */

#include "StarredModel.h"

namespace balloonmain {

StarredModel::StarredModel()
{
}

void StarredModel::Reset(const std::vector<int64_t>& userids)
{
    m_starred.clear();
    for (std::vector<int64_t>::size_type i = 0; i < userids.size(); ++i)
    {
        //非法项直接丢掉：名单里混进一条脏数据不该让后面的判定跟着出错。
        if (userids[i] > 0)
        {
            m_starred.insert(userids[i]);
        }
    }
}

bool StarredModel::IsStarred(int64_t userid) const
{
    if (userid <= 0)
    {
        return false;
    }
    return m_starred.find(userid) != m_starred.end();
}

void StarredModel::SetStarred(int64_t userid, bool starred)
{
    if (userid <= 0)
    {
        return;
    }
    if (starred)
    {
        m_starred.insert(userid);   //已在集合里时 insert 什么都不做，天然幂等
    }
    else
    {
        m_starred.erase(userid);    //不在集合里时 erase 返回 0，同样不出错
    }
}

int StarredModel::Count() const
{
    return (int)m_starred.size();
}

void StarredModel::ExportItems(std::vector<int64_t>& out) const
{
    out.clear();
    for (std::set<int64_t>::const_iterator it = m_starred.begin(); it != m_starred.end(); ++it)
    {
        out.push_back(*it);
    }
    //导出的顺序是 userid 升序，与服务端按标记时间倒序下发的顺序<u>不同</u>。
    //这不要紧：名单只用来判定"这个人关注了没有"，没有任何按序展示的地方。
}

void StarredModel::Clear()
{
    m_starred.clear();
}

const wchar_t* StarredModel::GetMenuLabel(bool isStarred)
{
    //与「收藏」那一项的措辞风格保持一致：动作 + 对象，取消方向加"取消"前缀。
    return isStarred ? L"取消特别关注" : L"特别关注";
}

} // namespace balloonmain
