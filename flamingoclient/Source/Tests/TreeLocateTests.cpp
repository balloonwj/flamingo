/**
 *  TreeLocateTests.cpp
 *
 *  通讯录树内定位（BalloonMain/TreeLocate）的单元测试，分两组：
 *    A. 祖先链 CollectAncestorChain —— 多级部门路径的收集顺序、起点已是根 / 起点
 *       不在映射表里这两种空结果、父指针成环时的止损，以及 maxDepth 传 0 或负数
 *       时按 1 处理。
 *    B. 滚动位置 ComputeLocateScrollPos —— 目标行在视口上方 / 下方 / 已完整可见
 *       三种走向，落点的上下界夹取，内容比视口还短的退化情形，三种无效输入的原样
 *       返回（顺带验证不会除零），以及 headerOffset 对结果的平移。
 *
 *  这一层值得测的原因：真实的定位动作要操作 DuiTreeView 与滚动容器，得先把窗口创
 *  建出来才跑得动，手工点界面又极难撞上"父指针成环""目标行正好卡在视口底边"这类
 *  分支；而算错的表现只是"定位过去看不见人"，没有报错也没有日志，只能靠断言钉住。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不碰 GDI。
 *
 *  balloonwj@qq.com   2026-08-17
 */

#include "catch_amalgamated.hpp"

#include <map>
#include <vector>

#include "../UI/BalloonMain/TreeLocate.h"

using namespace balloonmain;

// =====================================================================
// 构造测试数据的小工具与公用常量
// =====================================================================

namespace {

// 一棵贴近真实数据的组织架构（只记父子关系，节点内容与定位无关）：
//   公司根(1)
//     └ 技术中心(2)
//         └ 研发部(3)
//             └ 张三(100)   —— 叶子，即定位的目标行
// 公司根的父节点显式写成 kTreeNodeNoParent，表示它自己就是根。
const int kNodeCompany = 1;      // 公司根节点 id
const int kNodeCenter  = 2;      // 技术中心节点 id
const int kNodeDept    = 3;      // 研发部节点 id
const int kNodeLeaf    = 100;    // 叶子（人员）节点 id

std::map<int, int> MakeSampleTree()
{
    std::map<int, int> parent;
    parent[kNodeCompany] = kTreeNodeNoParent;
    parent[kNodeCenter]  = kNodeCompany;
    parent[kNodeDept]    = kNodeCenter;
    parent[kNodeLeaf]    = kNodeDept;
    return parent;
}

// 滚动相关用例共用的一组尺寸，取值贴近主面板通讯录页的实际观感（像素）。
const int kRowHeight     = 30;      // 单行行高
const int kViewHeight    = 300;     // 视口可见高度，正好 10 行
const int kContentHeight = 3000;    // 滚动内容总高，正好 100 行
const int kMaxScrollPos  = kContentHeight - kViewHeight;   // 可滚到的最大位置：2700

// 视口上部三分之一处对应的偏移量：定位时目标行顶端会落在距视口顶端这么远的地方。
const int kTopThird = kViewHeight / kLocateRowTopFraction;    // 100

} // anonymous namespace

// =====================================================================
// A. 祖先链 CollectAncestorChain
// =====================================================================

TEST_CASE("TL1 多级路径按由近及远收集：叶子 → 部门 → 上级部门 → 公司根", "[locate]")
{
    std::map<int, int> parent = MakeSampleTree();

    std::vector<int> ancestors;
    CollectAncestorChain(parent, kNodeLeaf, kTreeAncestorMaxDepth, ancestors);

    REQUIRE(ancestors.size() == 3);
    CHECK(ancestors[0] == kNodeDept);       // 第 0 项必须是直接父节点
    CHECK(ancestors[1] == kNodeCenter);
    CHECK(ancestors[2] == kNodeCompany);    // 最后一项是公司根
}

TEST_CASE("TL2 起点本身就是根时返回空", "[locate]")
{
    std::map<int, int> parent = MakeSampleTree();

    std::vector<int> ancestors;
    CollectAncestorChain(parent, kNodeCompany, kTreeAncestorMaxDepth, ancestors);

    CHECK(ancestors.empty());
}

TEST_CASE("TL3 起点不在映射表里时返回空", "[locate]")
{
    std::map<int, int> parent = MakeSampleTree();

    // 树还没建好、或者节点已被移除时会走到这里，不能当成"到根了"以外的任何情况。
    const int kNodeNotInTree = 999;

    std::vector<int> ancestors;
    CollectAncestorChain(parent, kNodeNotInTree, kTreeAncestorMaxDepth, ancestors);

    CHECK(ancestors.empty());
}

TEST_CASE("TL4 出参在函数内先清空，不会把上一次的结果留下来", "[locate]")
{
    std::map<int, int> parent = MakeSampleTree();

    // 复用同一个 vector 连着定位两次是常见写法，残留会让调用方多展开一堆无关节点。
    std::vector<int> ancestors;
    ancestors.push_back(7);
    ancestors.push_back(8);

    CollectAncestorChain(parent, kNodeCompany, kTreeAncestorMaxDepth, ancestors);

    CHECK(ancestors.empty());
}

TEST_CASE("TL5 父指针成环时不死循环，结果长度不超过 maxDepth", "[locate]")
{
    // 脏数据：10 的父是 11、11 的父是 12、12 的父又回到 10，三者连成一个环。
    const int kCycleA = 10;
    const int kCycleB = 11;
    const int kCycleC = 12;
    const int kCycleMaxDepth = 5;    // 故意取得比环长（3）大，逼函数走满上限

    std::map<int, int> parent;
    parent[kCycleA] = kCycleB;
    parent[kCycleB] = kCycleC;
    parent[kCycleC] = kCycleA;

    std::vector<int> ancestors;
    CollectAncestorChain(parent, kCycleA, kCycleMaxDepth, ancestors);

    // 能返回就说明没有卡死；长度按约定由 maxDepth 兜住。
    CHECK(ancestors.size() <= (size_t)kCycleMaxDepth);

    // 环外的前两级仍然是真实的父子关系，顺序不能乱。
    REQUIRE(ancestors.size() >= 2);
    CHECK(ancestors[0] == kCycleB);
    CHECK(ancestors[1] == kCycleC);
}

TEST_CASE("TL6 maxDepth 传 0 时按 1 处理，只收一层", "[locate]")
{
    std::map<int, int> parent = MakeSampleTree();

    std::vector<int> ancestors;
    CollectAncestorChain(parent, kNodeLeaf, 0, ancestors);

    REQUIRE(ancestors.size() == 1);
    CHECK(ancestors[0] == kNodeDept);
}

TEST_CASE("TL7 maxDepth 传负数时同样按 1 处理", "[locate]")
{
    std::map<int, int> parent = MakeSampleTree();

    const int kNegativeDepth = -5;

    std::vector<int> ancestors;
    CollectAncestorChain(parent, kNodeLeaf, kNegativeDepth, ancestors);

    REQUIRE(ancestors.size() == 1);
    CHECK(ancestors[0] == kNodeDept);
}

TEST_CASE("TL8 maxDepth 小于实际层数时只收到上限那么多层", "[locate]")
{
    std::map<int, int> parent = MakeSampleTree();

    // 叶子上方实际有三级，只允许追溯两级。
    std::vector<int> ancestors;
    CollectAncestorChain(parent, kNodeLeaf, 2, ancestors);

    REQUIRE(ancestors.size() == 2);
    CHECK(ancestors[0] == kNodeDept);
    CHECK(ancestors[1] == kNodeCenter);
}

// =====================================================================
// B. 滚动位置 ComputeLocateScrollPos
// =====================================================================

TEST_CASE("TL9 目标行在视口上方时向上滚，落到视口上部三分之一处", "[locate]")
{
    // 第 10 行的顶端在内容坐标 300 处，而当前已经滚到 1000，行在视口上方。
    const int kRowIndex = 10;
    const int kCurScrollPos = 1000;
    const int kExpected = kRowIndex * kRowHeight - kTopThird;    // 300 - 100 = 200

    const int pos = ComputeLocateScrollPos(kRowIndex, kRowHeight, 0,
                                           kViewHeight, kContentHeight, kCurScrollPos);

    CHECK(pos == kExpected);
    CHECK(pos < kCurScrollPos);    // 方向必须是往上滚
}

TEST_CASE("TL10 目标行在视口下方时向下滚，落到视口上部三分之一处", "[locate]")
{
    // 第 20 行的顶端在内容坐标 600 处，当前滚到 0，视口只到 300，行在视口下方。
    const int kRowIndex = 20;
    const int kCurScrollPos = 0;
    const int kExpected = kRowIndex * kRowHeight - kTopThird;    // 600 - 100 = 500

    const int pos = ComputeLocateScrollPos(kRowIndex, kRowHeight, 0,
                                           kViewHeight, kContentHeight, kCurScrollPos);

    CHECK(pos == kExpected);
    CHECK(pos > kCurScrollPos);    // 方向必须是往下滚
}

TEST_CASE("TL11 目标行已经完整落在视口内时原地不动", "[locate]")
{
    // 第 5 行占内容坐标 [150, 180)，视口是 [100, 400)，整行都看得见。
    const int kRowIndex = 5;
    const int kCurScrollPos = 100;

    const int pos = ComputeLocateScrollPos(kRowIndex, kRowHeight, 0,
                                           kViewHeight, kContentHeight, kCurScrollPos);

    CHECK(pos == kCurScrollPos);
}

TEST_CASE("TL12 目标行底端露出视口外时仍要滚，不算完整可见", "[locate]")
{
    // headerOffset 取 15，第 9 行占内容坐标 [285, 315)，视口是 [0, 300)，底端超出 15 像素。
    const int kHeaderOffset = 15;
    const int kRowIndex = 9;
    const int kCurScrollPos = 0;
    const int kRowTop = kHeaderOffset + kRowIndex * kRowHeight;    // 285
    const int kExpected = kRowTop - kTopThird;                     // 185

    const int pos = ComputeLocateScrollPos(kRowIndex, kRowHeight, kHeaderOffset,
                                           kViewHeight, kContentHeight, kCurScrollPos);

    CHECK(pos == kExpected);
}

TEST_CASE("TL13 内容总高比视口还短时滚动位置为 0", "[locate]")
{
    // 只有几行内容、视口反而更高，无处可滚；即便传进来的当前位置是个脏值也要归零。
    const int kShortContentHeight = 200;
    const int kDirtyScrollPos = 50;

    const int pos = ComputeLocateScrollPos(0, kRowHeight, 0,
                                           kViewHeight, kShortContentHeight, kDirtyScrollPos);

    CHECK(pos == 0);
}

TEST_CASE("TL14 落点算成负数时夹到 0", "[locate]")
{
    // 第 1 行顶端在内容坐标 30 处，减掉三分之一视口高（100）会变成负数。
    const int kRowIndex = 1;
    const int kCurScrollPos = 500;

    const int pos = ComputeLocateScrollPos(kRowIndex, kRowHeight, 0,
                                           kViewHeight, kContentHeight, kCurScrollPos);

    CHECK(pos == 0);
}

TEST_CASE("TL15 落点超过最大值时夹到最大值", "[locate]")
{
    // 最后一行（第 99 行）顶端在内容坐标 2970 处，减掉 100 仍然大于最大滚动位置 2700。
    const int kRowIndex = 99;
    const int kCurScrollPos = 0;

    const int pos = ComputeLocateScrollPos(kRowIndex, kRowHeight, 0,
                                           kViewHeight, kContentHeight, kCurScrollPos);

    CHECK(pos == kMaxScrollPos);
}

TEST_CASE("TL16 rowIndex 为负（目标行不可见）时原样返回当前滚动位置", "[locate]")
{
    const int kCurScrollPos = 777;

    const int pos = ComputeLocateScrollPos(-1, kRowHeight, 0,
                                           kViewHeight, kContentHeight, kCurScrollPos);

    CHECK(pos == kCurScrollPos);
}

TEST_CASE("TL17 rowHeight 为 0（行高还没量出来）时原样返回当前滚动位置", "[locate]")
{
    const int kCurScrollPos = 777;

    const int pos = ComputeLocateScrollPos(10, 0, 0,
                                           kViewHeight, kContentHeight, kCurScrollPos);

    CHECK(pos == kCurScrollPos);
}

TEST_CASE("TL18 viewHeight 为 0（视口还没排版）时原样返回，且不会除零", "[locate]")
{
    // 视口高度是三分之一那步的被除数所在处，为 0 时必须在做除法之前就返回。
    const int kCurScrollPos = 777;

    const int pos = ComputeLocateScrollPos(10, kRowHeight, 0,
                                           0, kContentHeight, kCurScrollPos);

    CHECK(pos == kCurScrollPos);
}

TEST_CASE("TL19 headerOffset 不为 0 时结果跟着平移同样的像素数", "[locate]")
{
    // 树上方多了一块页标题（48 像素）之后，同一行的落点应当整体下移 48。
    const int kHeaderOffset = 48;
    const int kRowIndex = 20;
    const int kCurScrollPos = 0;

    const int posNoHeader = ComputeLocateScrollPos(kRowIndex, kRowHeight, 0,
                                                   kViewHeight, kContentHeight, kCurScrollPos);
    const int posWithHeader = ComputeLocateScrollPos(kRowIndex, kRowHeight, kHeaderOffset,
                                                     kViewHeight, kContentHeight, kCurScrollPos);

    CHECK(posWithHeader == posNoHeader + kHeaderOffset);
}
