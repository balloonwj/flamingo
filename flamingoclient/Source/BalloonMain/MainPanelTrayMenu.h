/**
 *  MainPanelTrayMenu.h
 *
 *  系统托盘图标右键菜单的<u>纯数据模型</u>。把"托盘菜单有哪些项、什么顺序、
 *  各项 id / 文本 / 图标 / 勾选 / 是否可用"从对话框类（CMainDlg）里抽出成一个
 *  不依赖 HWND、不依赖 Flamingo 业务对象的纯函数，便于被 FlamingoUnit 测试
 *  工程直接调用断言（真正的弹出 / 命令分发等带窗口副作用的部分仍留在
 *  CMainDlg::OnTrayIcon_RButtunUp）。
 *
 *  结构对照 flamingo.rc 原 ID_MENU_MAIN_PANEL 里的三个托盘子菜单：
 *    · 登录前 / 面板锁定：打开主面板 ── 退出（两态内容相同）。
 *    · 登录后正常：我在线上 / 离线 ── 锁定 Flamingo ── 关闭所有声音 ──
 *                  打开主面板 ── 退出。
 *  原来用 SkinLib 的 CSkinMenu（随老自绘 UI shell 一并停用），现改用 balloonui
 *  的 DuiMenu 重建，视觉与左下角汉堡系统菜单（MainPanelSystemMenu）统一。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#pragma once

#include <windows.h>      // UINT
#include <vector>

namespace balloonmain {

/**
 *  TrayMenuId —— 托盘菜单各功能项的命令 id。点击后由 DuiMenu::TrackPopup 返回
 *  该值；CMainDlg::OnTrayIcon_RButtunUp 据此分发到现成的 OnMenu_* 处理函数。
 *
 *  取值<u>必须非 0</u>：0 是 DuiMenu::TrackPopup 的"未选中"（失焦 / ESC）返回值，
 *  不能占用。分隔条不是功能项、没有 id（其 TrayMenuEntry::id 取 0）。
 *
 *  注意：这里<u>另立</u>一套 id（而非直接复用 resource.h 的 ID_MENU_* 资源命令
 *  id），是为了让本模型与 resource.h 解耦、可被测试工程独立编译；分发时由
 *  OnTrayIcon_RButtunUp 把它们映射到对应动作（见该函数的 switch）。
 */
enum TrayMenuId : UINT
{
    kTrayMenuOnline    = 1,   // 在线 —— 切到在线状态（presence_online）
    kTrayMenuOffline   = 2,   // 离线 —— 切到离线状态（真正断开，GoOffline）
    kTrayMenuLock      = 3,   // 锁定 Flamingo —— 锁定主面板（已随旧主窗口移除，保留枚举值占位）
    kTrayMenuMute      = 4,   // 关闭所有声音 —— 切换静音（可勾选项）
    kTrayMenuOpenPanel = 5,   // 打开主面板 —— 显示登录窗 / 主面板
    kTrayMenuExit      = 6,   // 退出 —— 退出程序
    kTrayMenuAway      = 7,   // 离开 —— 切到离开状态（presence_away，cmd_change_presence）
    kTrayMenuBusy      = 8,   // 忙碌 —— 切到忙碌状态（presence_busy，cmd_change_presence）
    kTrayMenuLogout    = 9,   // 登出 —— 二次确认后断开当前连接、销毁主面板并回到登录界面；
                              // 与 kTrayMenuExit（退出程序）不同，登出后程序仍在运行
};

/**
 *  TrayMenuKind —— 托盘菜单的三种形态。由 CMainDlg 的登录态 / 锁定态决定取哪种。
 */
enum TrayMenuKind
{
    kTrayBeforeLogin = 0,   // 未登录：打开主面板 / 退出
    kTrayNormal      = 1,   // 已登录、未锁定：完整菜单（状态 / 锁定 / 静音 / 打开 / 退出）
    kTrayLocked      = 2,   // 已登录、面板锁定：打开主面板 / 退出（内容同未登录）
};

/**
 *  TrayMenuEntry —— 托盘菜单里的一行（功能项或分隔条）。
 *
 *  说明：每个功能项都带左侧图标（与左下角系统菜单观感统一）。静音项的当前状态
 *  <u>同时用文字与图标表达</u>而非勾选标记 —— 因为 DuiMenu 里"勾选标记"与"左侧
 *  图标"互斥（见 DuiMenu.h 项类型说明），既然要带图标就放弃勾选标记，改用：
 *  未静音 = "关闭所有声音" + kIconSound（喇叭）；已静音 = "开启所有声音" + kIconMute
 *  （静音图标）。文字与图标一起随 muted 切换。
 *
 *  四个在线状态项里"用户当前所处的那一项"同样用图标 + 文字双通道标记（原因同上，
 *  勾选标记用不了）：文字带"（当前）"后缀，图标则在该状态的徽标外圈多画一环
 *  （渲染期按 isCurrent 决定，见 MainPanelMenuIcons::CreateStatusIconImage）。
 *  文字与字形都取自 MainPanelStatusMenu / PresenceBadge 的共用映射，与主面板状态
 *  菜单逐字一致。
 */
struct TrayMenuEntry
{
    UINT           id;            // 功能项命令 id（见 TrayMenuId）；分隔条取 0
    bool           isSeparator;   // 是否为分隔条；true 时 label/iconKind 无意义
    const wchar_t* label;         // 显示文本（宽字符常量串）；分隔条取 nullptr
    int            iconKind;      // 左侧图标类型（MainPanelIcons.h 的 IconKind）；无图标 / 分隔条取 -1。
                                  // 在线状态项取该状态的徽标字形（kIconPresence* 之一），不随 isCurrent 变化
    bool           enabled;       // 是否可点击；false 为灰显 no-op（当前各项均 true）
    COLORREF       dotColor;      // 在线状态项的徽标颜色（在线绿/离开橙/忙碌红/离线灰，与状态菜单同源）；
                                  // 非状态项取 0，渲染时表示"用默认单色图标"，不做彩色覆盖。
                                  // <u>本字段兼作"是不是在线状态项"的判据</u>（!= 0 即是）
    bool           isCurrent;     // 是否为用户当前所处的在线状态；只有在线状态项可能为 true，
                                  // 且一份模型里至多一项为 true。非状态项 / 分隔条恒为 false
    int            presence;      // 本行代表的在线状态（v2 presence_type）。<u>仅 dotColor != 0
                                  // 时有意义</u>；非状态项 / 分隔条填 0，调用方不得读取
};

/**
 *  构造托盘菜单的行模型 —— 按状态返回对照 flamingo.rc 原托盘子菜单的行序列。
 *  纯函数、无副作用、不创建窗口；调用方（OnTrayIcon_RButtunUp）据此逐行喂给
 *  DuiMenu。
 *
 *    kind：           托盘菜单形态（未登录 / 正常 / 锁定）；见 TrayMenuKind。
 *    muted：          当前是否已静音；仅影响 kTrayNormal 下静音项的<u>文字与图标</u>
 *                     （未静音"关闭所有声音"+kIconSound / 已静音"开启所有声音"+kIconMute），
 *                     其余形态忽略该参数。
 *    currentPresence：用户当前所处的在线状态（v2 presence_type）。与之相符的那一个状态
 *                     项被标记为当前项（isCurrent = true、文字带"（当前）"后缀，
 *                     渲染期再给徽标套一圈外环）。取值先经 NormalizePresenceForMenu 归一，
 *                     故隐身 / 越界值会落到"离线"项上。kTrayBeforeLogin / kTrayLocked
 *                     两种形态没有状态项，忽略该参数。
 *
 *  返回：行模型 vector（值语义，调用方拥有）。label 指向静态常量串，生命周期为
 *        进程级，调用方可直接持有 / 转宽字符使用。
 */
std::vector<TrayMenuEntry> BuildTrayMenuModel(TrayMenuKind kind, bool muted,
                                              int currentPresence);

} // namespace balloonmain
