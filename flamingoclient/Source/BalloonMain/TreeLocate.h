/**
 *  TreeLocate.h
 *
 *  通讯录树内「定位到某一行」用的两个纯函数：一个沿父指针向上收集祖先节点，供调用方
 *  把目标行所在的那条部门路径整条展开；另一个算出让目标行落进视口所需的滚动位置。
 *
 *  抽成不依赖控件的自由函数是为了能在 FlamingoUnit 里直接断言 —— 真实的定位动作要
 *  操作 DuiTreeView 与滚动容器，得先创建窗口才跑得起来，单元测试覆盖不到；而这两段
 *  逻辑恰恰是最容易出错的部分（父指针成环、滚动位置越界、行不可见时的退化处理）。
 *  用例见 Source/Tests/TreeLocateTests.cpp。
 *
 *  典型用法：
 *      // 第一步：把目标行所在的那条部门路径整条展开
 *      std::vector<int> ancestors;
 *      balloonmain::CollectAncestorChain(parentMap, targetNode,
 *                                        balloonmain::kTreeAncestorMaxDepth, ancestors);
 *      for (size_t i = 0; i < ancestors.size(); ++i)
 *      {
 *          pTree->SetExpanded(ancestors[i], true);
 *      }
 *
 *      // 第二步：展开会改变行数，重新排版之后再把目标行滚进视口
 *      int pos = balloonmain::ComputeLocateScrollPos(rowIndex, rowHeight, headerOffset,
 *                                                    viewHeight, contentHeight, curPos);
 *      pScrollView->SetScrollPos(pos);
 *
 *  balloonwj@qq.com   2026-08-17
 */

#pragma once

#include <map>
#include <vector>

namespace balloonmain {

// 父节点映射里表示"没有父节点（本身就是根）"的取值。
const int kTreeNodeNoParent = -1;

// 向上追溯祖先的默认层数上限。组织架构再深也不会有这么多层，取这个值只是为了
// 在父指针意外成环时不至于死循环。
const int kTreeAncestorMaxDepth = 64;

// 定位时目标行落在视口自顶向下几分之一处的分母 —— 取 3 即"视口上部三分之一"。
// 不把目标行贴到视口顶端，是为了让它上方的几级部门标题也留在可见范围内，
// 用户一眼能看出定位到的这个人挂在哪个部门下面。
const int kLocateRowTopFraction = 3;

/**
 *  从 node 出发沿父指针向上收集它的各级祖先节点，由近及远（第 0 项是直接父节点）。
 *  调用方拿到之后逐个 SetExpanded(true)，把目标行所在的那条部门路径整条展开。
 *
 *    parent：节点 id → 父节点 id 的映射。缺项、或值为 kTreeNodeNoParent 表示已经到根。
 *    node：起点节点 id（不会被放进结果）。
 *    maxDepth：向上追溯的层数上限，用于在父指针意外成环时兜底止损；小于 1 时按 1 处理。
 *    outAncestors：出参，函数内先清空。
 *
 *  返回：无。结果全部写在 outAncestors 里，长度不会超过按上述规则修正后的 maxDepth。
 *  副作用：无，只写 outAncestors；parent 不会被修改。
 */
void CollectAncestorChain(const std::map<int, int>& parent, int node,
                          int maxDepth, std::vector<int>& outAncestors);

/**
 *  算出让目标行落进视口所需的滚动位置。
 *
 *    rowIndex：目标行在树里的可见行号（0 起）；小于 0 表示行不可见，直接返回 curScrollPos。
 *    rowHeight：单行高度（像素）；小于等于 0 时无法定位，直接返回 curScrollPos。
 *    headerOffset：树的第 0 行在滚动内容里的纵向起点（页标题高度 + 容器上内边距），像素。
 *    viewHeight：视口可见高度（像素）；小于等于 0 时直接返回 curScrollPos。
 *    contentHeight：滚动内容总高（像素）。
 *    curScrollPos：当前滚动位置（像素）。
 *  返回：新的滚动位置，已夹在 [0, max(0, contentHeight - viewHeight)] 内。
 *  策略：目标行已经完整落在视口内就原地不动（避免用户每定位一次界面都跳一下）；
 *        否则把它放到视口上部三分之一处，好让它上方的部门层级也能看见。
 *  副作用：无，纯计算。
 */
int ComputeLocateScrollPos(int rowIndex, int rowHeight, int headerOffset,
                           int viewHeight, int contentHeight, int curScrollPos);

} // namespace balloonmain
