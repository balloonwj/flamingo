/**
 *  MainPanelSystemMenu.h
 *
 *  登录后主面板左下角"汉堡"按钮弹出的系统菜单的<u>纯数据模型</u>。把"菜单
 *  有哪些项、什么顺序、各项 id / 文本 / 图标 / 是否可用"从对话框类里抽出成
 *  一个不依赖 HWND、不依赖 Flamingo 业务对象的纯函数，便于被 FlamingoUnit
 *  测试工程直接调用断言（弹出 / 路由等带窗口副作用的部分仍留在
 *  CBalloonMainPanel::ShowSystemMenu）。
 *
 *  设计稿：design/主面板系统菜单.png（个人信息 / 修改密码 ── 公告
 *  与通知 ── 系统设置 / 网络设置 ── 关于 FlamingoIM，共 7 项 + 3 条分隔线）。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#pragma once

#include <windows.h>      // UINT
#include <vector>

namespace balloonmain {

/**
 *  SystemMenuId —— 系统菜单各功能项的命令 id。点击后由 DuiMenu::TrackPopup
 *  返回该值；CBalloonMainPanel::ShowSystemMenu 据此分发到对应动作。
 *
 *  取值<u>必须非 0</u>：0 是 DuiMenu::TrackPopup 的"未选中"（失焦 / ESC）返回值，
 *  不能占用。分隔条不是功能项、没有 id（其 SystemMenuEntry::id 取 0）。
 */
enum SystemMenuId : UINT
{
    kSysMenuProfile      = 1,   // 个人信息 —— 打开本人个人信息窗
    kSysMenuChangePwd    = 2,   // 修改密码 —— 打开修改密码对话框
    // 3 号被一个已下线的功能占用过，号段留空不复用 —— 避免旧版本的截图、日志里
    // 记着的 id=3 被新菜单项顶替，排查历史问题时对不上号
    kSysMenuAnnounce     = 4,   // 公告与通知 —— 打开「消息盒子」（平台公告收件箱），与 Banner 底部那一行同一去处
    kSysMenuSettings     = 5,   // 系统设置 —— 打开系统设置对话框
    kSysMenuNetSettings  = 6,   // 网络设置 —— 打开新版网络设置窗
    kSysMenuAbout        = 7,   // 关于 FlamingoIM —— 打开关于对话框
    kSysMenuLogout       = 8,   // 登出 —— 二次确认后断开当前连接、销毁主面板并回到登录界面
                                // （实际动作由 ShowSystemMenu 投递 FMG_MSG_LOGOUT 给 CMainDlg 执行）
};

/**
 *  SystemMenuEntry —— 系统菜单里的一行（功能项或分隔条）。
 */
struct SystemMenuEntry
{
    UINT           id;            // 功能项命令 id（见 SystemMenuId）；分隔条取 0
    bool           isSeparator;   // 是否为分隔条；true 时 label/iconKind 无意义
    const wchar_t* label;         // 显示文本（宽字符常量串）；分隔条取 nullptr
    int            iconKind;      // 左侧图标类型（MainPanelIcons.h 的 IconKind）；分隔条取 -1
    bool           enabled;       // 是否可点击；false 为灰显 no-op（当前各项均 true）
};

/**
 *  构造系统菜单的行模型 —— 按设计稿顺序返回 7 个功能项 + 3 条分隔线（共 10 行）。
 *  纯函数、无副作用、不创建窗口；调用方（ShowSystemMenu）据此逐行喂给 DuiMenu。
 *
 *  返回：行模型 vector（值语义，调用方拥有）。label 指向静态常量串，生命周期为
 *        进程级，调用方可直接持有 / 转宽字符使用。
 */
std::vector<SystemMenuEntry> BuildSystemMenuModel();

} // namespace balloonmain
