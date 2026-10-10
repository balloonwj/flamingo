/**
 *  MainPanelStatusMenu.cpp
 *
 *  状态菜单行模型与"状态→视觉"映射的实现。仅装配数据（项 id / 文本 / presence /
 *  圆点色），不碰任何窗口 —— 故可被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个
 *  工程逐字共用。在 Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  本文件含中文宽字符字面量（菜单文本）。两个工程均已开启 /utf-8 编译选项，源码
 *  按 UTF-8 解析，L"中文" 字面量正常，无需 BOM。
 *
 *  balloonwj@qq.com   2026-06-05
 */

#include "MainPanelStatusMenu.h"
#include "PresenceBadge.h"          // GetPresenceBadgeIconKind（状态 → 徽标字形）
#include "../../net/Protocol.h"        // presence_type（在线状态取值的单一事实来源）
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace balloonmain {

namespace {

// ---- 各在线状态的徽标配色 ---------------------------------------------------
// 颜色既用于菜单项左侧的状态徽标，也复用为胶囊上的状态点，保证各处一致。
//
// 2026-08-12 状态图标改版时整体压深了一档（改版前是 design/主面板状态菜单.png 的
// #22C55E / #F59E0B / #EF4444 / #9CA3AF）。压深的两个理由：忙碌的红与离开的琥珀在
// 10px 圆点上色相太近，离线灰对白底只有 2.3:1 的对比度、在浅色行底上近乎消失。
// 注意颜色现在只是<u>辅助</u>通道 —— 四个状态靠字形本身就能区分（见 PresenceBadge.h）。

// 在线：绿 #16A34A。对白底对比度 3.1:1（改版前 #22C55E 只有 1.9:1，小圆点发飘）。
const COLORREF kDotOnline  = RGB(0x16, 0xA3, 0x4A);
// 离开：琥珀橙 #F59E0B。这一个不动 —— 琥珀本身没问题，问题出在红离它太近。
const COLORREF kDotAway    = RGB(0xF5, 0x9E, 0x0B);
// 忙碌：深红 #DC2626。往深红走，与琥珀拉开色相与明度两重差距。
const COLORREF kDotBusy    = RGB(0xDC, 0x26, 0x26);
// 离线：中性灰 #6B7280。对白底对比度 4.8:1 —— 离线徽标是个空心环，环细，灰浅了就没了。
const COLORREF kDotOffline = RGB(0x6B, 0x72, 0x80);

// 装配一行状态项。文字、圆点色、图标类型全部从"状态 → 视觉"映射里取，保证与
// 胶囊以及另外两处菜单同源。currentPresence 须由调用方先经 NormalizePresenceForMenu
// 归一，这里只做相等比较。
StatusMenuEntry MakeStatusEntry(UINT id, int presence, int currentPresence)
{
    StatusMenuEntry e;
    e.id        = id;
    e.presence  = presence;
    e.isCurrent = (presence == currentPresence);
    e.label     = GetPresenceMenuLabel(presence, e.isCurrent);
    e.dotColor  = GetPresenceVisual(presence).dotColor;
    // 图标一律是"本行代表的那个状态"的徽标 —— 四种字形各不相同，这一列本身就在回答
    // "这一行是哪个状态"。当前项<u>不</u>换图标：它的"当前"由文字后缀"（当前）"以及
    // 渲染期在徽标外圈另加的一环表达（见 MainPanelMenuIcons::CreateStatusIconImage）。
    e.iconKind  = GetPresenceBadgeIconKind(presence);
    return e;
}

} // anonymous namespace

std::vector<StatusMenuEntry> BuildStatusMenuModel(int currentPresence)
{
    // 顺序严格对照 design/主面板状态菜单.png（共 4 项）：
    //   在线 / 离开 / 勿扰 / 离线
    const int cur = NormalizePresenceForMenu(currentPresence);

    std::vector<StatusMenuEntry> items;
    items.push_back(MakeStatusEntry(kStatusMenuOnline,  presence_online,  cur));
    items.push_back(MakeStatusEntry(kStatusMenuAway,    presence_away,    cur));
    items.push_back(MakeStatusEntry(kStatusMenuBusy,    presence_busy,    cur));
    items.push_back(MakeStatusEntry(kStatusMenuOffline, presence_offline, cur));
    return items;
}

std::vector<StatusMenuEntry> BuildLoginStatusMenuModel(int currentPresence)
{
    // 登录窗可选三态：在线 / 离开 / 忙碌。不含"离线"—— 登录时选离线没有意义。
    // 若 currentPresence 归一后是"离线"，下面三行的 isCurrent 都会是 false，
    // 即本菜单里没有任何一项被标记为当前项，这是预期行为。
    const int cur = NormalizePresenceForMenu(currentPresence);

    std::vector<StatusMenuEntry> items;
    items.push_back(MakeStatusEntry(kStatusMenuOnline, presence_online, cur));
    items.push_back(MakeStatusEntry(kStatusMenuAway,   presence_away,   cur));
    items.push_back(MakeStatusEntry(kStatusMenuBusy,   presence_busy,   cur));
    return items;
}

int NormalizePresenceForMenu(int presence)
{
    switch (presence)
    {
    case presence_online:
        return presence_online;
    case presence_away:
        return presence_away;
    case presence_busy:
        return presence_busy;
    case presence_offline:
        return presence_offline;
    default:
        // 一切越界取值统一归到"离线"，与 GetPresenceVisual 的兜底口径一致 ——
        // 否则会出现"胶囊显示离线、菜单里却没有任何一项被标记"的自相矛盾。
        // 取值 4 是已作废的旧状态（见 net/Protocol.h），老服务端仍可能推上来，
        // 走的就是这条兜底。
        return presence_offline;
    }
}

const wchar_t* GetPresenceMenuLabel(int presence, bool isCurrent)
{
    if (!isCurrent)
        return GetPresenceVisual(presence).text;

    // 当前项在状态名后缀上"（当前）"。这里把四种文字逐条写成完整字面量、而不是在
    // 运行时拼接，是为了让返回值仍然是进程级生命周期的静态常量串 —— 这是
    // StatusMenuEntry::label / TrayMenuEntry::label 对调用方的承诺。
    switch (NormalizePresenceForMenu(presence))
    {
    case presence_online:
        return TR(L"在线（当前）");
    case presence_away:
        return TR(L"离开（当前）");
    case presence_busy:
        return TR(L"忙碌（当前）");
    default:
        // 归一后只剩"离线"这一种可能。
        return TR(L"离线（当前）");
    }
}

PresenceVisual GetPresenceVisual(int presence)
{
    PresenceVisual v;
    switch (presence)
    {
    case presence_online:
        v.text     = TR(L"在线");
        v.dotColor = kDotOnline;
        break;
    case presence_away:
        v.text     = TR(L"离开");
        v.dotColor = kDotAway;
        break;
    case presence_busy:
        v.text     = TR(L"忙碌");
        v.dotColor = kDotBusy;
        break;
    case presence_offline:
        v.text     = TR(L"离线");
        v.dotColor = kDotOffline;
        break;
    default:
        // 本菜单不可选中的取值（含已作废的旧状态 4）统一退化为"离线"灰点。
        // 正常流程不会走到这里（菜单只产出上面 4 种），仅作兜底防御。
        v.text     = TR(L"离线");
        v.dotColor = kDotOffline;
        break;
    }
    return v;
}

} // namespace balloonmain
