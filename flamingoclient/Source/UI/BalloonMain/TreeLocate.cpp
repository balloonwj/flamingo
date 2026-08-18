/**
 *  TreeLocate.cpp
 *
 *  TreeLocate.h 所声明的两个树内定位函数的实现。纯逻辑，只依赖 STL，不触及控件、
 *  GDI 与 Win32 窗口，因此可以在 FlamingoUnit 里直接构造数据断言。
 *
 *  balloonwj@qq.com   2026-08-17
 */

#include "TreeLocate.h"

namespace balloonmain {

void CollectAncestorChain(const std::map<int, int>& parent, int node,
                          int maxDepth, std::vector<int>& outAncestors)
{
    outAncestors.clear();

    // 层数上限至少为 1：调用方传 0 或负数属于参数写错，此时按"只往上找一层"处理，
    // 而不是一层都不找 —— 后者会让调用方以为节点没有父节点，静默少展开一层。
    int limit = maxDepth;
    if (limit < 1)
    {
        limit = 1;
    }

    // 逐层向上走。这里不额外维护"已访问节点集合"：按 TreeLocate.h 的约定，防止父指针
    // 成环导致死循环的手段就是 limit 本身。成环时结果里会出现重复 id，调用方拿它去
    // SetExpanded(true) 是幂等的，不会有副作用。
    int current = node;
    for (int step = 0; step < limit; ++step)
    {
        std::map<int, int>::const_iterator it = parent.find(current);
        if (it == parent.end())
        {
            // 当前节点没在映射表里登记 —— 视作已经到根（起点本身不在表里时结果为空）。
            break;
        }

        const int parentId = it->second;
        if (parentId == kTreeNodeNoParent)
        {
            // 显式标记为根，到此为止。
            break;
        }

        outAncestors.push_back(parentId);
        current = parentId;
    }
}

int ComputeLocateScrollPos(int rowIndex, int rowHeight, int headerOffset,
                           int viewHeight, int contentHeight, int curScrollPos)
{
    // 三种无效输入：行不可见、行高未知、视口还没量出高度。此时算不出有意义的落点，
    // 保持当前滚动位置不动，让界面停在原处而不是跳到 0。
    if (rowIndex < 0)
    {
        return curScrollPos;
    }
    if (rowHeight <= 0)
    {
        return curScrollPos;
    }
    if (viewHeight <= 0)
    {
        return curScrollPos;
    }

    // 目标行在滚动内容坐标系里的上下边界（像素）。
    const int rowTop = headerOffset + rowIndex * rowHeight;
    const int rowBottom = rowTop + rowHeight;

    // 已经整行落在视口内就原地不动 —— 否则用户每点一次定位，界面都要没来由地滚一下。
    if (rowTop >= curScrollPos && rowBottom <= curScrollPos + viewHeight)
    {
        return curScrollPos;
    }

    // 把目标行放到视口上部三分之一处，它上方的部门层级也就跟着留在可见范围内。
    int target = rowTop - viewHeight / kLocateRowTopFraction;

    // 可滚动的最大位置。内容比视口还短时无处可滚，最大值为 0。
    int maxScrollPos = contentHeight - viewHeight;
    if (maxScrollPos < 0)
    {
        maxScrollPos = 0;
    }

    // 夹进 [0, maxScrollPos]：目标行靠近顶部时上式会算出负数，靠近底部时会超出最大值。
    if (target < 0)
    {
        target = 0;
    }
    if (target > maxScrollPos)
    {
        target = maxScrollPos;
    }

    return target;
}

} // namespace balloonmain
