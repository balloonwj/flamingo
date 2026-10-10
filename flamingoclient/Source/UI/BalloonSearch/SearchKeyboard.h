/**
 *  主面板搜索下拉的键盘操作（纯逻辑，不依赖窗口与控件）：上下方向键在结果行之间移动的
 *  选中状态，以及 Ctrl+Tab 在筛选标签之间循环切换时跳过置灰的标签。
 *
 *  SearchDropdown 把每次重建出来的结果行按显示顺序编号，交给 SearchSelection 记住当前选中
 *  的是第几行；按键时问它下一步选哪一行、回车时打开哪一行，再自己去画选中底色、滚动到可见。
 *  规则可以脱离界面单独测试（Tests/SearchKeyboardTests.cpp）。
 *
 *  典型用法：
 *      m_selection.Reset((int)m_selectableRows.size());   //结果重建后
 *      if (m_selection.MoveDown()) { 画选中底色、滚动到 m_selection.Current() }
 *      int row = m_selection.ActivationIndex();            //回车
 *
 *  本文件不 include stdafx.h，被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 共用。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

namespace balloonsearch {

/**
 *  SearchSelection —— 下拉结果行的键盘选中状态。
 *  选中下标从 0 起；-1 表示没有选中任何行（刚输入、结果刚重建、或从第一行再往上按）。
 */
class SearchSelection
{
public:
    SearchSelection();

    /**
     *  结果重建后调用：记下新的行数并取消选中（行已经换了一批，旧下标没有意义）。
     *    count：可选中的行数；小于 0 时按 0 处理。
     */
    void Reset(int count);

    // 可选中的行数。
    int Count() const { return m_count; }

    // 当前选中的下标；没有选中时返回 -1。
    int Current() const { return m_current; }

    /**
     *  按下方向键：没有选中时选中第一行，否则选中下一行；已在最后一行时不动。
     *  @return 选中下标变了返回 true（调用方据此重画、滚动）；没有可选的行或已在末行返回 false。
     */
    bool MoveDown();

    /**
     *  按上方向键：选中上一行；已在第一行时取消选中（回到只在搜索框里打字的状态）；
     *  没有选中时不动。
     *  @return 选中下标变了返回 true。
     */
    bool MoveUp();

    /**
     *  按回车时要打开的行：有选中行时就是它；没有选中但有结果时打开第一行。
     *  @return 行下标；一行结果都没有时返回 -1。
     */
    int ActivationIndex() const;

private:
    int m_count;    // 可选中的行数，Reset 时更新
    int m_current;  // 当前选中的下标，-1 表示没有选中
};

/**
 *  Ctrl+Tab / Ctrl+Shift+Tab 循环切换筛选标签：从 current 出发按 step 方向找下一个可用的标签，
 *  越过末尾回到开头（反向同理），置灰的标签跳过。
 *    current：当前标签下标（0 起）。
 *    step：+1 向后（Ctrl+Tab），-1 向前（Ctrl+Shift+Tab）；其它值按 +1 处理。
 *    available：各标签是否可用，长度为 count；为 NULL 时视为都可用。
 *    count：标签个数；不大于 0 时返回 current。
 *  @return 下一个可用标签的下标；除 current 外没有可用的标签时返回 current。
 */
int NextAvailableTab(int current, int step, const bool* available, int count);

} // namespace balloonsearch
