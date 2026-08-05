/**
 *  PanelDragZone.h
 *
 *  "按住窗口空白处也能拖动整窗"的命中判定。
 *
 *  balloonui 的 DuiFrameWindow 只把标题栏那一条判成 HTCAPTION，客户区一律是
 *  HTCLIENT —— 于是主面板顶部渐变 Banner、底部栏这些大片空白按下去窗口纹丝不动。
 *  本模块负责回答一个纯几何问题：某个点是否落在"可拖动的空白"里。窗口侧在
 *  WM_NCHITTEST 里据此把 HTCLIENT 升级成 HTCAPTION，交给系统走原生拖窗逻辑
 *  （吸附、贴边、Shake 全都免费拿到）。
 *
 *  典型用法（见 CBalloonMainPanel::IsPointInPanelDragZone）：
 *      RECT drags[balloonmain::kMaxDragRects];
 *      RECT excludes[balloonmain::kMaxDragExcludeRects];
 *      int  dragCount = 0;
 *      int  excludeCount = 0;
 *      // …把"整块可拖的容器矩形"填进 drags，把里面"可点击、不能被拖动吃掉"的
 *      //   子矩形填进 excludes（均为同一坐标系，主面板用窗口客户区坐标）…
 *      if (balloonmain::IsPointInDragZone(pt, drags, dragCount, excludes, excludeCount))
 *          return HTCAPTION;
 *
 *  本模块只做坐标运算、不碰窗口 / GDI，便于 FlamingoUnit 测试工程直接断言。
 *
 *  balloonwj@qq.com   2026-08-05
 */

#pragma once

#include <windows.h>      // POINT / RECT / PtInRect

namespace balloonmain {

// 可拖矩形的最大条数。主面板当前用掉 2 条（顶部渐变 Header、底部栏），
// 留 1 条余量给日后新增的可拖区域，改这里即可，不必改函数签名。
const int kMaxDragRects = 3;

// 排除矩形的最大条数。主面板当前用掉 7 条：Header 里的头像 / 昵称 / 个性签名 /
// 在线状态胶囊 / 消息盒子 5 块，加底部栏的汉堡菜单按钮与主操作按钮 2 块；
// 同样留 1 条余量。
const int kMaxDragExcludeRects = 8;

/**
 *  判定某个点是否落在"按住可拖动窗口"的区域内（纯函数）。
 *
 *  规则：点必须落在<u>任意一条</u>可拖矩形内，且<u>不落在任何一条</u>排除矩形内。
 *  边界语义随 Win32 ::PtInRect —— 左边界与上边界算"在内"，右边界与下边界算
 *  "在外"，与控件矩形本身的语义一致，相邻矩形之间不会重叠也不会漏缝。
 *
 *  空矩形（右 <= 左 或 下 <= 上）天然不命中，无须调用方过滤：控件尚未布局、
 *  或 MainPanelHeader 还没画过第一帧时，那些命中矩形就是全 0 的空矩形，此时
 *  "整块可拖、什么都不排除"即为期望行为。
 *
 *    pt            待判定的点。坐标系由调用方决定，但必须与两组矩形一致
 *                  （主面板用的是窗口客户区坐标）。
 *    dragRects     可拖矩形数组；NULL 或 dragCount <= 0 一律判为不可拖。
 *                  指针只在调用期间借用，本函数不持有、不修改。
 *    dragCount     dragRects 的有效条数，取值 0..kMaxDragRects。
 *    excludeRects  排除矩形数组；NULL 或 excludeCount <= 0 表示不排除任何区域。
 *                  指针只在调用期间借用，本函数不持有、不修改。
 *    excludeCount  excludeRects 的有效条数，取值 0..kMaxDragExcludeRects。
 *
 *  返回：true 表示该点可用于拖动窗口；false 表示不可（落在按钮等可点击区域上，
 *        或压根不在可拖区内）。无副作用。
 */
bool IsPointInDragZone(POINT pt,
                       const RECT* dragRects, int dragCount,
                       const RECT* excludeRects, int excludeCount);

} // namespace balloonmain
