/**
 *  flamingoAdmin —— 企业详情屏组织树工具条的几何计算（纯函数）。
 *
 *  组织树左栏顶部那条工具条上排着「搜索框 + 若干图标按钮（刷新 / 展开全部 /
 *  折叠）」：图标按钮作为一组右对齐，搜索框占据左侧剩余宽度。这里把这套摆位
 *  算法从 EnterpriseDetailScreen::Layout 里抽出来做成纯函数，只依赖 RECT /
 *  int，不引用 balloonui 与任何 UI 运行期状态，因此能被 flamingoAdminTests
 *  直接编入单测。
 *
 *  典型用法（调用方按下标取矩形，下标 0 是最左边那个按钮）：
 *      OrgToolbarSpec spec;
 *      spec.m_buttonCount = 3;
 *      const OrgToolbarMetrics m = LayoutOrgToolbar(rcToolbar, spec);
 *      m_btnRefresh  ->ForceLayout(m.m_rcButtons[0]);
 *      m_btnExpandAll->ForceLayout(m.m_rcButtons[1]);
 *      m_btnCollapse ->ForceLayout(m.m_rcButtons[2]);
 *      m_orgSearch   ->ForceLayout(m.m_rcSearch);
 *
 *  balloonwj@qq.com   2026-08-11
 */
#pragma once

#include <windows.h>

namespace flamingoadmin {

// 工具条上图标按钮个数的上限。当前实际用到 3 个（刷新 / 展开全部 / 折叠），
// 留一个余量以便后续增加；超过此数的请求会被 LayoutOrgToolbar 夹到本值。
const int kMaxOrgToolbarButtons = 4;

// 组织树工具条的摆位参数。各字段默认值即 EnterpriseDetailScreen 当前采用的
// 尺寸（96 dpi 逻辑像素），单测里可按需改写以覆盖边界情形。
struct OrgToolbarSpec
{
    // 图标按钮个数。<= 0 表示不放按钮，搜索框独占整条工具条；
    // 超过 kMaxOrgToolbarButtons 时按上限处理。
    int m_buttonCount = 0;

    // 单个图标按钮的边长（像素，正方形）。
    int m_buttonSize = 28;

    // 相邻两个图标按钮之间的水平间距（像素）。
    int m_buttonGap = 4;

    // 工具条左 / 右内边距（像素）：搜索框左边界与最右侧按钮右边界各留这么多。
    int m_sidePad = 12;

    // 搜索框右边界与最左侧图标按钮之间的水平间距（像素）。
    int m_searchGap = 8;

    // 搜索框高度（像素），在工具条内竖向居中。
    int m_searchH = 28;
};

// LayoutOrgToolbar 的计算结果。
struct OrgToolbarMetrics
{
    // 搜索框矩形（host 客户区坐标）。工具条过窄放不下时宽度会被夹成 0，
    // 即 m_rcSearch.right == m_rcSearch.left，绝不会出现负宽度。
    RECT m_rcSearch = { 0, 0, 0, 0 };

    // 图标按钮矩形，按屏幕从左到右的顺序填入 [0, m_buttonCount)；
    // 其余元素保持 {0,0,0,0}，调用方不应读取。
    RECT m_rcButtons[kMaxOrgToolbarButtons] = { { 0, 0, 0, 0 } };

    // 实际排布的按钮个数（已按 [0, kMaxOrgToolbarButtons] 夹取）。
    int m_buttonCount = 0;
};

/**
 *  计算组织树工具条上搜索框与各图标按钮的矩形。
 *
 *  摆位规则：图标按钮作为一组紧贴工具条右侧（距右边界 m_sidePad），彼此间隔
 *  m_buttonGap，全部在工具条内竖向居中；搜索框从工具条左侧 m_sidePad 处起，
 *  右到最左侧按钮之前 m_searchGap 处，高 m_searchH 且竖向居中。
 *
 *    rcToolbar：工具条矩形（host 客户区坐标）。
 *    spec：摆位参数，含义见 OrgToolbarSpec 各字段注释。
 *
 *  @return 各控件矩形。纯函数、无副作用，可直接单测。
 */
OrgToolbarMetrics LayoutOrgToolbar(const RECT& rcToolbar,
                                   const OrgToolbarSpec& spec);

} // namespace flamingoadmin
