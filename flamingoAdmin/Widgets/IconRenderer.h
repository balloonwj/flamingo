/**
 *  flamingoAdmin —— 矢量图标绘制。
 *
 *  设计稿采用 Phosphor / Lucide 风格的描边图标。本文件用 GDI+ 在一个
 *  归一化的 24x24 坐标空间里绘制所需图标，再缩放到目标矩形，保证任意
 *  尺寸都清晰。Phase 2 实现侧边栏 / 顶栏 / 登录页用到的图标。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

#include <windows.h>

namespace flamingoadmin { namespace ui {

// 图标种类。每个值对应设计稿里的一个图标。
enum class Icon
{
    Dashboard,    // 仪表板：四宫格
    Building,     // 企业管理：办公楼
    Megaphone,    // 公告中心：扩音喇叭
    Shield,       // 审计日志：盾牌
    Settings,     // 平台设置：齿轮
    Search,       // 顶栏搜索：放大镜
    Bell,         // 顶栏通知：铃铛
    Logout,       // 侧栏登出：登出箭头
    User,         // 登录页账户输入框：人像
    Lock,         // 登录页密码输入框：挂锁
    Eye,          // 登录页密码显隐：眼睛
    ArrowRight,   // 登录按钮：右箭头
    Plus,         // 新增：加号（新增企业按钮）
    Close,        // 关闭：叉号（弹窗关闭按钮）
    Info,         // 信息：圆圈内一个 i（弹窗提示横幅）
    Folder,       // 部门节点 / 文件夹
    Trash,        // 删除：垃圾桶
    ChevronRight, // 右尖角（面包屑分隔 / 折叠）
    ChevronDown,  // 下尖角（展开）
    ChevronLeft,  // 左尖角（返回）
    Menu,         // 汉堡菜单：三横线（侧边栏折叠开关）
    Terminal,     // 调试控制台：终端窗口（带「>」命令提示符）
    Check,        // 对勾：成功 / 完成提示（toast Info 用）
    Warning,      // 警告：三角形内一个感叹号（toast Warn 用）
};

// 在 rc 内绘制一个图标（建议 rc 为正方形）。
//   icon：图标种类。
//   rc：目标矩形（图标会缩放铺满之）。
//   color：描边 / 填充颜色。
void DrawIcon(HDC hdc, Icon icon, const RECT& rc, COLORREF color);

// 把一个图标渲染成 32bpp PARGB(预乘 alpha) DIBSection HBITMAP, 供
// balloonui DuiButton::SetLeadingIcon 等接收 HBITMAP 的 API 使用。
// 透明背景, AA 边缘正确; 后续 ::AlphaBlend 渲染不会有黑边。
//   icon:图标种类;
//   size:位图边长(像素, 正方形;<= 0 返回 nullptr);
//   color:描边 / 填充颜色。
//   返回:caller-owned HBITMAP, 用完调 ::DeleteObject 释放。失败返回 nullptr。
// 典型用法:给 balloonwjui::DuiButton::SetLeadingIcon 提供 HBITMAP:
//
//     static HBITMAP s_plus = ui::RenderIconToBitmap(ui::Icon::Plus, 16,
//                                                     RGB(255, 255, 255));
//     btn->SetLeadingIcon(s_plus);
//
// 静态缓存比较合适 —— 一个 icon × size × color 组合产物一份 HBITMAP, 进程
// 级生命期, 退出时统一释放(或留 OS 回收)。
HBITMAP RenderIconToBitmap(Icon icon, int size, COLORREF color);

}} // namespace flamingoadmin::ui
