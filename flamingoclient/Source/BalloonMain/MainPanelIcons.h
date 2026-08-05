/**
 *  MainPanelIcons.h
 *
 *  主面板用到的一组线性图标的 GDI+ 描边绘制 helper。图标形状逐字取自
 *  design 的 icons.jsx（统一 viewBox 24、圆头圆角描边）。Tab 栏、底部栏、
 *  列表项等处都从这里取图标，避免各控件各画一份。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 <windows.h>
 *  （GDI+ 头文件依赖它）。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#pragma once

#include <gdiplus.h>

namespace balloonmain {

/**
 *  图标类型。逐值对应 design icons.jsx 里的同名图标。
 */
enum IconKind
{
    kIconHistory = 0,   // 时钟 —— "消息 / 会话"Tab
    kIconUser    = 1,   // 人形 —— "联系人"Tab
    kIconGroup   = 2,   // 群组 —— "群"Tab
    kIconList    = 3,   // 三横线 —— 底部汉堡菜单
    kIconPlus    = 4,   // 加号 —— 底部"查找 / 建群"按钮
    kIconSearch  = 5,   // 放大镜 —— 搜索框
    kIconChevron = 6,   // 右尖括号 —— 分组展开 / 折叠

    // 以下为"主面板系统菜单"（左下角汉堡按钮弹出菜单）各项左侧图标。
    kIconLock    = 7,   // 挂锁 —— 系统菜单"修改密码"
    kIconSend    = 8,   // 纸飞机 —— 当前无使用方（原先挂在一个已下线的菜单项上），
                        // 绘制实现保留在 MainPanelIcons.cpp，新菜单项要用直接取即可
    kIconBell    = 9,   // 铃铛 —— 系统菜单"公告与通知"
    kIconGear    = 10,  // 齿轮 —— 系统菜单"系统设置"
    kIconGlobe   = 11,  // 地球 —— 系统菜单"网络设置"
    kIconInfo    = 12,  // 圆圈 i —— 系统菜单"关于 FlamingoIM"

    // 以下为"系统托盘右键菜单"（MainPanelTrayMenu）各项左侧图标。
    kIconOnline  = 13,  // 实心圆点 —— 托盘菜单"我在线上"（在线 presence 点）
    kIconOffline = 14,  // 空心圆环 —— 托盘菜单"离线"
    kIconPanel   = 15,  // 窗口 —— 托盘菜单"打开主面板"
    kIconExit    = 16,  // 登出门 + 箭头 —— 托盘菜单"退出"
    kIconSound   = 17,  // 喇叭 + 声波 —— 托盘菜单"关闭所有声音"（未静音态：声音开着）
    kIconMute    = 18,  // 喇叭 + 叉 —— 托盘菜单"关闭所有声音"（已静音态：声音已关）

    // "登出"（断开当前连接、回到登录界面）。单独一个图标而非复用 kIconExit：
    // 托盘菜单里登出与退出上下紧挨着，用同一个门形图标无法区分，故登出画成
    // "人形 + 向右箭头"（这个人离开），退出仍是门框加箭头。
    kIconLogout  = 19,  // 人形 + 向右箭头 —— 系统菜单 / 托盘菜单"登出"

    // "当前所处的在线状态"专用圆点：外环 + 内实心点，也就是单选按钮选中的样子。
    // 与普通实心点(kIconOnline)配对使用 —— 状态菜单 / 托盘菜单里当前所处的那一项
    // 换成它、其余项仍用 kIconOnline，用户一眼就能看出自己现在是哪个状态。
    // 颜色仍由调用方按状态传入（在线绿 / 离开橙 / 忙碌红 / 离线灰）。
    kIconStatusCurrent = 20,
};

/**
 *  在指定区域内描边绘制一个线性图标。
 *
 *    g：          目标 Graphics（调用方应已开启抗锯齿）。
 *    kind：       图标类型，取值见 IconKind；越界值不绘制。
 *    x / y：      绘制区域左上角（设备坐标）。
 *    size：       绘制区域边长（图标按 viewBox 24 等比缩放到此尺寸）。
 *    color：      描边颜色。
 *    strokeWidth：描边宽度，以 viewBox 24 为基准的单位（一般取 2.0）。
 */
void DrawLineIcon(Gdiplus::Graphics& g, int kind,
                  Gdiplus::REAL x, Gdiplus::REAL y, Gdiplus::REAL size,
                  const Gdiplus::Color& color, Gdiplus::REAL strokeWidth);

} // namespace balloonmain
