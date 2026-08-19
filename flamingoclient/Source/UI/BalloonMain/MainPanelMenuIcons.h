/**
 *  MainPanelMenuIcons.h
 *
 *  把 MainPanelIcons 的线描图标渲染成一张带透明通道的 CImageEx 位图，供
 *  balloonui 的 DuiMenu（系统菜单各项左侧图标）使用 —— DuiMenu::AppendItem
 *  接收 CImageEx*，而 MainPanelIcons 的 DrawLineIcon 是 GDI+ 即时描边，两者
 *  之间需要这层"画到位图"的桥接。
 *
 *  除了单张图标的渲染，本文件还提供"在线状态菜单整段装配"的共用函数
 *  AppendStatusMenuItems —— 主面板状态菜单与登录窗状态菜单的装配代码原本一字不差
 *  地各写了一份，当前项标记这类改动要同时改两处才不会走样，故收拢到这里。
 *  （托盘右键菜单的行类型不同、还夹着分隔条与非状态项，在托盘控制器里自行装配，
 *   但它取的图标类型 / 文字同样来自 MainPanelStatusMenu 的共用映射。）
 *
 *  注意：DuiMenu 以源矩形 (0,0,16,16) 采样图标（kIconSize=16），故 sizePx 必须
 *  传 16，否则会被裁成左上角一块。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 <windows.h>
 *  （COLORREF）。CImageEx 定义在 ImageEx.h、位于<u>全局</u>命名空间，故前向
 *  声明放在 balloonmain 命名空间<u>外</u>。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#pragma once

#include <windows.h>
#include <vector>
#include <memory>

#include "MainPanelStatusMenu.h"                              // StatusMenuEntry（状态菜单行模型）
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"  // DuiMenu（装配目标）

// CImageEx 在全局命名空间（ImageEx.h），前向声明须放命名空间外。
class CImageEx;

namespace balloonmain {

/**
 *  渲染一个线描图标到 sizePx × sizePx 的 32bpp 预乘 alpha 位图，返回新建的
 *  CImageEx（带 AlphaBlend 绘制所需的 alpha 通道，背景完全透明、图标边缘抗锯齿）。
 *
 *    iconKind：图标类型，取值见 MainPanelIcons.h 的 IconKind；越界值得到一张空透明图。
 *    color：   描边颜色（RGB）。
 *    sizePx：  位图边长（逻辑像素），>0；给 DuiMenu 用时须传 16。
 *
 *  @return 新建的 CImageEx，<u>所有权归调用方</u>，用完 delete；分配失败返回 NULL。
 */
CImageEx* CreateMenuIconImage(int iconKind, COLORREF color, int sizePx);

/**
 *  渲染一个<u>在线状态徽标</u>到 sizePx × sizePx 的 32bpp 预乘 alpha 位图。与
 *  CreateMenuIconImage 的区别是：字形与颜色都由 presence 决定（走 PresenceBadge 的
 *  共用映射），调用方不必自己查配色表；且支持把该项标记为"当前所处的状态"。
 *
 *    presence：  v2 在线状态（presence_type）；越界取值按"离线"画。
 *    markCurrent：true 时徽标缩小居中、外面套一圈同色细环，表示这是用户当前所处的
 *                 状态。菜单里至多一项该传 true。
 *    sizePx：    位图边长（逻辑像素），>0；给 DuiMenu 用时须传 16。
 *
 *  @return 新建的 CImageEx，<u>所有权归调用方</u>，用完 delete；分配失败返回 NULL。
 */
CImageEx* CreateStatusIconImage(int presence, bool markCurrent, int sizePx);

/**
 *  把一份在线状态菜单的行模型整段装配进 DuiMenu：逐行按 entry.presence 渲染左侧
 *  状态徽标（当前项按 entry.isCurrent 额外套一圈外环），再以 entry.label 作文本
 *  追加为普通菜单项，项 id 取 entry.id。
 *
 *    menu：      目标菜单，函数只往里追加、不清空已有项。
 *    icons：     图标暂存容器。函数把新建的每张图标存进去 —— DuiMenu 只<u>借用</u>
 *                图标指针、不持有所有权，故调用方必须让本容器活到 TrackPopup 返回
 *                之后（TrackPopup 是同步阻塞的，放成局部变量即可）。函数只追加，
 *                不清空调用方已有的元素。
 *    model：     行模型，一般来自 BuildStatusMenuModel / BuildLoginStatusMenuModel。
 *    iconSizePx：图标位图边长（逻辑像素），给 DuiMenu 用时须传 16（见上文说明）。
 *
 *  副作用：向 menu 追加 model.size() 个菜单项，向 icons 追加同样数量的元素
 *          （单张渲染失败时存入空指针，DuiMenu 容忍图标为 NULL）。
 */
void AppendStatusMenuItems(balloonwjui::DuiMenu& menu,
                           std::vector<std::unique_ptr<CImageEx> >& icons,
                           const std::vector<StatusMenuEntry>& model,
                           int iconSizePx);

} // namespace balloonmain
