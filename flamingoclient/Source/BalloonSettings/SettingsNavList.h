/**
 *  SettingsNavList.h
 *
 *  "系统设置"对话框左侧的竖向导航列表。复用 balloonui 的 DuiVirtualList（数据
 *  源型列表，自带单选 / 滚动 / 键盘上下键），子类化它只为两件事：持有各行标题
 *  文本，以及通过 PaintRowFn 回调把选中 / hover 行画成设计稿那种"圆角胶囊"
 *  （浅橙底 + 橙字），其余沿用 DuiVirtualList 的既有行为。<u>不修改 balloonui 库</u>。
 *
 *  设计稿：design/系统设置1.png 左侧导航（选中项为浅橙圆角胶囊 + 橙色文字）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/List/DuiListBox.h"   // balloonwjui::DuiVirtualList
#include <vector>

namespace balloonsettings {

/**
 *  CSettingsNavList —— 系统设置左侧圆角胶囊导航（DuiVirtualList 子类）。
 *
 *  用法：SetTitles 填入各页标题；SetCtrlId 设一个 id；选中变化时 DuiVirtualList
 *  以该 id 冒 DUIN_VALUECHANGED（extra = 选中行索引），宿主对话框据此切页。
 */
class CSettingsNavList : public balloonwjui::DuiVirtualList
{
public:
    CSettingsNavList();

    /**
     *  设置导航各行标题（一项一行），并同步行数。
     *    titles：标题文本数组，按显示顺序排列；空数组表示无导航项。
     */
    void SetTitles(const std::vector<CString>& titles);

private:
    /**
     *  DuiVirtualList 行绘制回调的静态转发器：把 user 还原为 this 后转 PaintRow。
     *  （遵循"尽量不用 lambda"，用自由静态函数做 thunk。）
     */
    static void PaintRowThunk(void* user, HDC hdc, int rowIndex,
                              const RECT& rowRect, bool selected, bool hover);

    /**
     *  画单行：选中 / hover 行先铺圆角胶囊底，再画标题文字。
     *    hdc：宿主背缓冲 DC。
     *    rowIndex：行索引（[0, m_titles.size())）。
     *    rowRect：行矩形（宿主客户区坐标，已应用滚动）。
     *    selected / hover：该行当前是否处于选中 / 悬停态（由控件维护）。
     */
    void PaintRow(HDC hdc, int rowIndex, const RECT& rowRect, bool selected, bool hover);

private:
    std::vector<CString> m_titles;   // 各行标题文本；行数 = size()，生命周期同本控件
};

} // namespace balloonsettings
