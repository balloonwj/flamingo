/**
 *  MainPanelStatusMenu.h
 *
 *  主面板 Banner 上"状态胶囊"点击后弹出的<u>在线状态选择菜单</u>的纯数据模型，
 *  以及"在线状态 → 胶囊文字 / 圆点颜色"的视觉映射。把"菜单有哪些项、什么顺序、
 *  各项文本 / 对应 presence / 圆点色"从带窗口副作用的弹出逻辑里抽出成纯函数，
 *  既便于被 FlamingoUnit 测试工程直接断言，又能被胶囊控件（MainPanelHeader）与
 *  弹出逻辑（CBalloonMainPanel::ShowStatusMenu）共用同一份"状态→视觉"映射。
 *
 *  设计稿：design/主面板状态菜单.png —— 在线 / 离开 / 勿扰 / 离线，各带一个
 *  彩色圆点。本产品不提供隐身功能，故本模型只产出 4 项：在线 / 离开 / 勿扰 / 离线。
 *
 *  在线状态取值采用 v2 协议的 presence_type（见 net/Protocol.h），<u>不要</u>混用
 *  net/IUProtocolData.h 里的 v1 ONLINE_STATUS（两套枚举的 away/invisible 值是反的）。
 *
 *  本文件同时是"当前所处状态如何在菜单里标记"的唯一出处：托盘右键菜单
 *  （MainPanelTrayMenu）、主面板状态菜单、登录窗状态菜单三处都从这里取图标类型与
 *  文字，避免三处各判一套、判出不一致的结果。
 *
 *  balloonwj@qq.com   2026-06-05
 */

#pragma once

#include <windows.h>      // UINT / COLORREF
#include <vector>

namespace balloonmain {

/**
 *  StatusMenuId —— 状态菜单各项的<u>菜单命令 id</u>。点击后由 DuiMenu::TrackPopup
 *  返回该值；CBalloonMainPanel::ShowStatusMenu 据此找到对应行、取其 presence 再分发。
 *
 *  取值<u>必须非 0</u>：0 是 DuiMenu::TrackPopup 的"未选中"（失焦 / ESC）返回值，不能
 *  占用。注意 v2 的 presence_offline 恰好是 0，所以"离线"项的命令 id <u>不能</u>直接
 *  用 presence 值，必须另立非 0 的菜单 id —— 真正的 presence 放在 StatusMenuEntry::presence
 *  里单独携带（做法与 MainPanelTrayMenu / MainPanelSystemMenu 的"菜单 id 与业务值解耦"一致）。
 */
enum StatusMenuId : UINT
{
    kStatusMenuOnline  = 1,   // "在线"项 —— 对应 presence_online(1)：正常在线、好友可见
    kStatusMenuAway    = 2,   // "离开"项 —— 对应 presence_away(2)：暂时离开
    kStatusMenuBusy    = 3,   // "勿扰"项 —— 对应 presence_busy(3)：忙碌 / 请勿打扰
    kStatusMenuOffline = 4,   // "离线"项 —— 对应 presence_offline(0)：真正断开连接（非"假离线"）
};

/**
 *  StatusMenuEntry —— 状态菜单里的一行。本菜单无分隔条，故不设 isSeparator 字段。
 *
 *  "当前所处的那一项"用<u>图标 + 文字</u>双通道标记：label 带"（当前）"后缀，
 *  图标则在该状态的徽标外圈多画一环（渲染期按 isCurrent 决定，见
 *  MainPanelMenuIcons::CreateStatusIconImage）。之所以不用 balloonui 自带的勾选
 *  标记，是因为 DuiMenu 的左侧列里"图标"与"勾选标记"互斥（DuiMenu.cpp 绘制时
 *  有图标就只画图标），而状态徽标必须保留 —— 它是辨认状态的主要线索。
 *
 *  2026-08-12 改版前，"当前"是靠把 iconKind 换成 kIconStatusCurrent（外环 + 内点）
 *  表达的；改版后 iconKind 这一列要用来表达"这一行是哪个状态"（四种字形各不相同），
 *  腾不出手再兼表"当前"，故当前项的外环挪到渲染期叠加。
 */
struct StatusMenuEntry
{
    UINT           id;         // 菜单命令 id（见 StatusMenuId）；非 0
    const wchar_t* label;      // 显示文本（宽字符常量串，如 L"在线" / L"在线（当前）"）
    int            presence;   // 该项对应的 v2 在线状态（presence_type，见 net/Protocol.h）
    COLORREF       dotColor;   // 该项左侧圆点颜色（RGB）；同时复用为胶囊圆点色
    bool           isCurrent;  // 是否为用户当前所处的状态；一份模型里至多一项为 true
    int            iconKind;   // 左侧图标类型（MainPanelIcons.h 的 IconKind）：恒为本行
                               // 所代表状态的徽标字形（kIconPresenceOnline / Away /
                               // Busy / Offline 之一），不随 isCurrent 变化
};

/**
 *  PresenceVisual —— 某个在线状态在胶囊上的视觉表达（文字 + 圆点颜色）。
 *  供 MainPanelHeader 画胶囊、CBalloonMainPanel 装配菜单共用，保证两处一致。
 */
struct PresenceVisual
{
    const wchar_t* text;       // 胶囊显示文字（如 L"在线" / L"离线"）
    COLORREF       dotColor;   // 胶囊圆点颜色（RGB）
};

/**
 *  构造状态菜单的行模型 —— 按设计稿顺序返回 4 行：在线 / 离开 / 勿扰 / 离线
 *  （共四项）。纯函数、无副作用、不创建窗口；调用方（ShowStatusMenu）据此
 *  逐行喂给 DuiMenu，圆点图标用 iconKind + dotColor 渲染。
 *
 *    currentPresence：用户当前所处的在线状态（v2 presence_type）。与之相符的那一行
 *                     被标记为当前项（isCurrent = true、文字带"（当前）"后缀，
 *                     渲染期再给徽标套一圈外环）。取值先经 NormalizePresenceForMenu 归一，
 *                     故越界值会落到"离线"行上，与胶囊的显示保持一致。
 *
 *  返回：行模型 vector（值语义，调用方拥有）。label 指向静态常量串，进程级生命周期。
 */
std::vector<StatusMenuEntry> BuildStatusMenuModel(int currentPresence);

/**
 *  构造<u>登录窗</u>状态菜单的行模型 —— 返回 3 行：在线 / 离开 / 忙碌。与主面板
 *  状态菜单的区别是<u>不含"离线"</u>：登录时选离线没有意义（选了也仍要连上服务器）。
 *
 *    currentPresence：登录窗当前选中的在线状态（v2 presence_type），语义同
 *                     BuildStatusMenuModel。若它归一后是"离线"，则三行都不标记为
 *                     当前项 —— 本菜单里没有对应的行可标。
 *
 *  返回：行模型 vector（值语义，调用方拥有）。label 指向静态常量串，进程级生命周期。
 */
std::vector<StatusMenuEntry> BuildLoginStatusMenuModel(int currentPresence);

/**
 *  把任意在线状态取值归一到状态菜单能表达的 4 种之一（在线 / 离开 / 忙碌 / 离线）。
 *
 *    presence：任意 v2 在线状态取值，含越界值。
 *
 *  返回：presence_online / presence_away / presence_busy / presence_offline 之一。
 *        一切未知取值统一归到 presence_offline —— 与 GetPresenceVisual 既有的
 *        兜底约定一致，保证"胶囊显示离线"和"菜单里离线项被标记"不会互相矛盾。
 */
int NormalizePresenceForMenu(int presence);

/**
 *  取某个在线状态在<u>菜单项</u>上的显示文字。
 *
 *    presence： v2 在线状态（presence_type）；未知取值退化为"离线"，同 GetPresenceVisual。
 *    isCurrent：该项是否为用户当前所处的状态。true 时返回带"（当前）"后缀的文字
 *               （如 L"在线（当前）"），false 时返回纯状态名（如 L"在线"）。
 *
 *  返回：静态常量串，进程级生命周期，调用方可直接持有。
 */
const wchar_t* GetPresenceMenuLabel(int presence, bool isCurrent);

/**
 *  取某个在线状态在胶囊上的视觉表达（文字 + 圆点色）。
 *
 *    presence：v2 在线状态（presence_type）。在线 / 离开 / 勿扰 / 离线分别映射到
 *              绿 / 橙 / 红 / 灰；其它取值（含本菜单不可选中的越界值）一律退化为
 *              "离线"灰点 —— 正常流程不会出现，仅作兜底。
 *
 *  注意：颜色只是<u>辅助</u>通道。四个状态真正的区分靠字形（在线实心圆 / 离开时钟 /
 *  忙碌横杠 / 离线空心环），取字形请用 PresenceBadge.h 的 GetPresenceBadgeIconKind，
 *  两者的兜底口径一致（越界值都落到"离线"）。
 *
 *  返回：对应的 PresenceVisual；text 指向静态常量串，进程级生命周期。
 */
PresenceVisual GetPresenceVisual(int presence);

} // namespace balloonmain
