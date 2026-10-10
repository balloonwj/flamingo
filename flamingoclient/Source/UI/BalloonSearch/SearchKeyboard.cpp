/**
 *  主面板搜索下拉键盘操作的纯逻辑实现。整体说明见 SearchKeyboard.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "SearchKeyboard.h"

namespace balloonsearch {

SearchSelection::SearchSelection()
    : m_count(0)
    , m_current(-1)
{
}

void SearchSelection::Reset(int count)
{
    m_count   = (count > 0) ? count : 0;
    m_current = -1;
}

bool SearchSelection::MoveDown()
{
    if (m_count <= 0 || m_current >= m_count - 1)
    {
        return false;
    }
    ++m_current;
    return true;
}

bool SearchSelection::MoveUp()
{
    if (m_current < 0)
    {
        return false;
    }
    //第一行再往上：取消选中，回到只在搜索框里打字的状态。
    --m_current;
    return true;
}

int SearchSelection::ActivationIndex() const
{
    if (m_count <= 0)
    {
        return -1;
    }
    return (m_current >= 0) ? m_current : 0;
}

int NextAvailableTab(int current, int step, const bool* available, int count)
{
    if (count <= 0)
    {
        return current;
    }
    const int dir = (step == -1) ? -1 : 1;
    int idx = current;
    //最多走一圈：找到第一个可用的标签就停；一圈下来都不可用时留在原地。
    for (int i = 0; i < count; ++i)
    {
        idx = (idx + dir + count) % count;
        if (idx == current)
        {
            break;
        }
        if (available == nullptr || available[idx])
        {
            return idx;
        }
    }
    return current;
}

} // namespace balloonsearch
