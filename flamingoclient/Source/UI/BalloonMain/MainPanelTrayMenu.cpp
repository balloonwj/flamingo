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
#include "MainPanelStatusMenu.h"   // GetPresenceVisual / GetPresenceMenuLabel（在线状态项的
                                   // 配色与文字，与主面板状态菜单同源）
#include "OnlineCapability.h"      // GetConnectionBadgePresence / GetConnectionMenuLabel /
                                   // IsActionAllowedInState（"当前项是哪一行、显示什么文字、
                                   // 四项能不能点"三个判定的唯一出处，本文件不另立口径）
#include "../../net/Protocol.h"       // presence_type（取各状态的圆点色）

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
//   presence：      本行代表的在线状态（v2 presence_type）。
//   badgePresence： 当前该被标为"当前项"的那个状态，由调用方经 GetConnectionBadgePresence
//                   按连接态算得 —— 连接未就绪时它是 presence_offline，即"离线"那一行被标记。
//   userPresence：  用户自己选的在线状态，仅用于取当前项的文字（连接就绪时才用得上）。
//   connUiState：   界面连接态，交给 GetConnectionMenuLabel 决定当前项的文字。
//   enabled：       本行是否可点，由调用方按连接态一次算好，四行取同一个值。
void PushStatusItem(std::vector<TrayMenuEntry>& items, UINT id,
                    int presence, int badgePresence, int userPresence,
                    int connUiState, bool enabled)
{
    const bool isCurrent = (presence == badgePresence);

    TrayMenuEntry e;
    e.id          = id;
    e.isSeparator = false;
    if (isCurrent)
    {
        // 当前项的文字按<u>连接态</u>取：掉线 / 重连中显示"正在连接（当前）"，
        // 与主面板状态胶囊逐字一致。原先这里只看用户选的 presence，于是掉线时
        // 胶囊已经写着"正在连接"、托盘菜单里却还标着"在线（当前）"。
        e.label = GetConnectionMenuLabel(userPresence, connUiState);
    }
    else
    {
        e.label = GetPresenceMenuLabel(presence, false);
    }
    e.iconKind    = GetPresenceBadgeIconKind(presence);
    e.enabled     = enabled;
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
                                              int currentPresence, int connUiState)
{
    std::vector<TrayMenuEntry> items;

    if (kind == kTrayNormal)
    {
        // "哪一行是当前项"按<u>连接态</u>算，而不是只看用户选的那个状态：连接未就绪时
        // 它给出 presence_offline，于是"离线"那一行被标记、并显示"正在连接（当前）"，
        // 与主面板状态胶囊完全一致（越界值同样落到"离线"行上，兜底口径不变）。
        const int cur = GetConnectionBadgePresence(currentPresence, connUiState);

        // 四个状态项是否可点，走与聊天窗 / 主面板门禁同一层的能力判定，不在本文件另写。
        const bool bPresenceEnabled = IsActionAllowedInState(connUiState, kActionChangePresence);

        // 登录后正常菜单（锁定功能已随旧主窗口一并移除，菜单不再含"锁定 Flamingo"）：
        //   在线 / 离开 / 忙碌 / 离线（四个在线状态，彩色圆点与状态菜单同源，
        //                              当前所处的那一项带外环圆点与"（当前）"后缀；
        //                              连接未就绪时四项一并灰显不可点）
        //   ──────────（分隔）
        //   关闭所有声音 / 开启所有声音（文字与图标都随静音态切换：未静音→"关闭所有声音"+
        //                                 kIconSound；已静音→"开启所有声音"+kIconMute）
        //   ──────────（分隔）
        //   打开主面板
        //   ──────────（分隔）
        //   登出 / 退出（同组：都是"结束当前会话"，登出回登录界面、退出关掉程序）
        PushStatusItem(items, kTrayMenuOnline,  presence_online,  cur,
                       currentPresence, connUiState, bPresenceEnabled);
        PushStatusItem(items, kTrayMenuAway,    presence_away,    cur,
                       currentPresence, connUiState, bPresenceEnabled);
        PushStatusItem(items, kTrayMenuBusy,    presence_busy,    cur,
                       currentPresence, connUiState, bPresenceEnabled);
        PushStatusItem(items, kTrayMenuOffline, presence_offline, cur,
                       currentPresence, connUiState, bPresenceEnabled);
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
