/**
 *  MainPanelTrayMenu.cpp
 *
 *  托盘菜单行模型的实现。仅装配数据（项 id / 文本 / 图标 / 勾选 / 分隔条），
 *  不碰任何窗口 —— 故可被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程
 *  逐字共用。在 Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  本文件含中文宽字符字面量（菜单文本）。两个工程均已开启 /utf-8 编译选项，
 *  源码按 UTF-8 解析，L"中文" 字面量正常，无需 BOM。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#include "MainPanelTrayMenu.h"
#include "MainPanelIcons.h"        // IconKind（各项左侧图标类型）
#include "PresenceBadge.h"         // GetPresenceBadgeIconKind（在线状态项的徽标字形）
#include "MainPanelStatusMenu.h"   // GetPresenceVisual / GetPresenceMenuLabel /
                                   // NormalizePresenceForMenu（在线状态项的配色、文字与
                                   // "当前项"判定，与主面板状态菜单同源）
#include "../net/Protocol.h"       // presence_type（取各状态的圆点色）

namespace balloonmain {

namespace {

// 无图标 / 分隔条 / 可勾选项的 iconKind 占位值（< 0 表示"不画图标"）。
const int kNoIcon = -1;

// 追加一行普通图标项（非分隔）。dotColor 默认 0：非状态项用默认单色图标。
void PushIconItem(std::vector<TrayMenuEntry>& items, UINT id,
                  const wchar_t* label, int iconKind, COLORREF dotColor = 0)
{
    TrayMenuEntry e;
    e.id          = id;
    e.isSeparator = false;
    e.label       = label;
    e.iconKind    = iconKind;
    e.enabled     = true;
    e.dotColor    = dotColor;
    e.isCurrent   = false;   // 非在线状态项，永远不是"当前状态"
    e.presence    = 0;       // 非在线状态项，本字段无意义（判据是 dotColor == 0）
    items.push_back(e);
}

// 追加一行"在线状态"项。文字、颜色、徽标字形都取自 MainPanelStatusMenu /
// PresenceBadge 的共用映射，与主面板状态菜单、胶囊逐字同源：图标是本行所代表状态的
// 徽标（四种字形各不相同），当前所处的那一项文字带"（当前）"后缀、渲染期再给徽标
// 套一圈外环。
//   presence：       本行代表的在线状态（v2 presence_type）。
//   currentPresence：用户当前所处的在线状态，须由调用方先经 NormalizePresenceForMenu 归一。
void PushStatusItem(std::vector<TrayMenuEntry>& items, UINT id,
                    int presence, int currentPresence)
{
    const bool isCurrent = (presence == currentPresence);

    TrayMenuEntry e;
    e.id          = id;
    e.isSeparator = false;
    e.label       = GetPresenceMenuLabel(presence, isCurrent);
    e.iconKind    = GetPresenceBadgeIconKind(presence);
    e.enabled     = true;
    e.dotColor    = GetPresenceVisual(presence).dotColor;
    e.isCurrent   = isCurrent;
    e.presence    = presence;
    items.push_back(e);
}

// 追加一条分隔线。
void PushSeparator(std::vector<TrayMenuEntry>& items)
{
    TrayMenuEntry e;
    e.id          = 0;
    e.isSeparator = true;
    e.label       = nullptr;
    e.iconKind    = kNoIcon;
    e.enabled     = true;
    e.dotColor    = 0;
    e.isCurrent   = false;
    e.presence    = 0;       // 分隔条，本字段无意义
    items.push_back(e);
}

} // anonymous namespace

std::vector<TrayMenuEntry> BuildTrayMenuModel(TrayMenuKind kind, bool muted,
                                              int currentPresence)
{
    std::vector<TrayMenuEntry> items;

    if (kind == kTrayNormal)
    {
        // 当前状态先归一：越界值一律落到"离线"项上，与胶囊显示保持一致
        // （见 NormalizePresenceForMenu 的说明）。
        const int cur = NormalizePresenceForMenu(currentPresence);

        // 登录后正常菜单（锁定功能已随旧主窗口一并移除，菜单不再含"锁定 Flamingo"）：
        //   在线 / 离开 / 忙碌 / 离线（四个在线状态，彩色圆点与状态菜单同源，
        //                              当前所处的那一项带外环圆点与"（当前）"后缀）
        //   ──────────（分隔）
        //   关闭所有声音 / 开启所有声音（文字与图标都随静音态切换：未静音→"关闭所有声音"+
        //                                 kIconSound；已静音→"开启所有声音"+kIconMute）
        //   ──────────（分隔）
        //   打开主面板
        //   ──────────（分隔）
        //   登出 / 退出（同组：都是"结束当前会话"，登出回登录界面、退出关掉程序）
        PushStatusItem(items, kTrayMenuOnline,  presence_online,  cur);
        PushStatusItem(items, kTrayMenuAway,    presence_away,    cur);
        PushStatusItem(items, kTrayMenuBusy,    presence_busy,    cur);
        PushStatusItem(items, kTrayMenuOffline, presence_offline, cur);
        PushSeparator(items);
        // 文字与图标都随静音态切换：未静音显示"关闭所有声音"（喇叭图标，点一下转静音）；
        // 已静音显示"开启所有声音"（静音图标，点一下恢复声音）。
        PushIconItem(items, kTrayMenuMute,
                     muted ? L"开启所有声音" : L"关闭所有声音",
                     muted ? kIconMute : kIconSound);
        PushSeparator(items);
        PushIconItem(items, kTrayMenuOpenPanel, L"打开主面板",  kIconPanel);
        PushSeparator(items);
        // 登出排在退出上方、与之同组：两者都结束当前会话，区别在于登出之后程序继续
        // 运行并回到登录界面。图标必须区分（登出为人形加箭头、退出为门框加箭头），
        // 否则两行紧挨着无法辨认。
        PushIconItem(items, kTrayMenuLogout,    L"登出",        kIconLogout);
        PushIconItem(items, kTrayMenuExit,      L"退出",        kIconExit);
        return items;
    }

    // 未登录（kTrayBeforeLogin）与面板锁定（kTrayLocked）内容相同，对照
    // flamingo.rc "系统托盘菜单一" / "面板锁定状态"：打开主面板 ── 退出。
    PushIconItem(items, kTrayMenuOpenPanel, L"打开主面板", kIconPanel);
    PushSeparator(items);
    PushIconItem(items, kTrayMenuExit,      L"退出",       kIconExit);
    return items;
}

} // namespace balloonmain
