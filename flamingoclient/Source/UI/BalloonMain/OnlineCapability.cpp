/**
 *  OnlineCapability.cpp
 *
 *  "当前状态下哪些功能可用"判定的实现。仅做纯计算，不碰窗口 / 网络对象，故可被
 *  Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程逐字共用。在 Flamingo.vcxproj
 *  里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  本文件含中文宽字符字面量（提示文案）。两个工程均已开启 /utf-8 编译选项，源码按
 *  UTF-8 解析，L"中文" 字面量正常，无需 BOM。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#include "OnlineCapability.h"
#include "MainPanelStatusMenu.h"    // NormalizePresenceForMenu（未知 presence 的兜底口径）
#include "../../net/Protocol.h"        // presence_type（在线状态取值的单一事实来源）

namespace balloonmain {

ConnectionUiState ComputeConnectionUiState(int presence, bool connected)
{
    // 先归一：一切越界取值都当作离线，与状态菜单 / 胶囊的兜底口径保持一致，
    // 免得出现"胶囊显示离线、功能却按在线放行"这种自相矛盾。
    const int cur = NormalizePresenceForMenu(presence);

    if (cur == presence_offline)
    {
        // 用户主动选了离线：连接已断、且不会自动重连，这是终态而非过渡态。
        return kConnUiOffline;
    }

    if (!connected)
    {
        // 状态是在线 / 离开 / 忙碌，但连接还没就绪。两种情形会落到这里：用户刚从
        // 离线切回在线、正在重新登录；或者网络掉了正在自动重连。对用户而言都是
        // "还连不上，稍等"，功能同样要拦住。
        return kConnUiConnecting;
    }

    return kConnUiOnline;
}

bool IsActionAllowed(int presence, bool connected, int action)
{
    // 先把"在线状态 + 连接是否就绪"折成连接态，再走同一条规则，保证两副入口
    // 永远给出一致的答案。
    return IsActionAllowedInState((int)ComputeConnectionUiState(presence, connected), action);
}

bool IsActionAllowedInState(int connUiState, int action)
{
    if (action < 0 || action >= kActionKindCount)
    {
        // 越界类别一律不放行：宁可误拦（用户会来反馈），也好过把一个没登记过的
        // 操作在离线时放出去（默默失败，用户不知道消息没发出去）。
        return false;
    }

    // 各类操作目前放行条件相同：必须真正在线（连接已就绪）。连接中、用户主动离线，
    // 以及一切越界的连接态都拦住 —— 与托盘图标 / 头像置灰的兜底方向一致。
    return connUiState == (int)kConnUiOnline;
}

const wchar_t* GetBlockedActionTip(int action)
{
    switch (action)
    {
    case kActionSendMessage:
        return L"当前为离线状态，消息未发送。恢复在线后可以继续聊天。";
    case kActionTransferFile:
        return L"当前为离线状态，无法传输文件和图片。请先恢复在线。";
    case kActionShakeWindow:
        return L"当前为离线状态，窗口抖动未发送。请先恢复在线。";
    case kActionRealtimeCall:
        return L"当前为离线状态，无法发起通话或远程协助。请先恢复在线。";
    case kActionAccountOp:
        return L"当前为离线状态，该操作需要连接服务器。请先恢复在线。";
    case kActionChangePresence:
        // 这一条不能照抄上面几条的"当前为离线状态"：切换在线状态被拦住时，用户既可能
        // 主动离线、也可能正在断线重连，后者说成"离线"会让他以为要自己动手恢复。
        return L"当前处于离线状态或正在连接服务器，无法切换在线状态。连接恢复后可以再切换。";
    default:
        // 越界类别的通用兜底文案。正常流程不会走到这里。
        return L"当前为离线状态，该操作需要连接服务器。请先恢复在线。";
    }
}

const wchar_t* GetConnectionBannerText(int state)
{
    switch (state)
    {
    case kConnUiOffline:
        // 明确告诉用户离线期间的消息不会丢（服务端存为离线消息、上线后补推），
        // 否则用户容易以为这段时间别人发来的消息没了。提示带只作告知、不可点击，
        // 恢复在线统一走托盘 / 主面板的状态菜单那个唯一入口。
        return L"当前为离线状态，无法收发消息。离线期间的消息会在恢复在线后收到。";
    case kConnUiConnecting:
        return L"正在连接服务器，请稍候……";
    case kConnUiOnline:
    default:
        // 在线态不显示提示条。调用方须判空。
        return NULL;
    }
}

const wchar_t* GetTrayIconFileName(int state)
{
    if (state == kConnUiOnline)
    {
        return L"imonline.ico";
    }
    // 主动离线与"连接尚未就绪"（恢复中 / 重连中）都用灰色图标：此刻都收发不了消息，
    // 托盘显示彩色会让人以为一切正常。
    return L"offline.ico";
}

int GetTrayOverlayPresence(int presence, int connUiState)
{
    // 连接没就绪时底图本来就是灰的（见 GetTrayIconFileName），此刻消息收发不了，
    // 再在灰底图上贴一个"离开 / 忙碌"徽标是自相矛盾的说法。
    if (connUiState != kConnUiOnline)
    {
        return kNoTrayOverlay;
    }

    switch (presence)
    {
    case presence_away:
        return presence_away;
    case presence_busy:
        return presence_busy;
    default:
        // 在线：彩色底图本身就表示在线，不叠。
        // 离线与一切越界取值：同样不叠（离线那一路底图已经是灰的）。
        return kNoTrayOverlay;
    }
}

PresenceVisual GetConnectionPresenceVisual(int presence, int connUiState)
{
    if (connUiState == kConnUiOnline)
    {
        // 连接就绪：显示用户自己选的那个状态（在线 / 离开 / 忙碌），配各自的圆点色。
        return GetPresenceVisual(presence);
    }

    if (connUiState == kConnUiConnecting)
    {
        // 掉线重连中，或刚从离线切回在线正在登录。此刻用户选的仍是"在线"，但消息
        // 发不出去，照抄 presence 会骗人；说成"离线"又不对 —— 用户并没有主动离线，
        // 而且马上就会自己连回来。圆点色沿用"离线"的灰，与灰色托盘图标观感一致。
        PresenceVisual v;
        v.text     = L"正在连接";
        v.dotColor = GetPresenceVisual(presence_offline).dotColor;
        return v;
    }

    // kConnUiOffline，以及一切越界取值：一律"离线"灰点（越界兜底的理由见头文件注释）。
    return GetPresenceVisual(presence_offline);
}

int GetConnectionBadgePresence(int presence, int connUiState)
{
    if (connUiState == kConnUiOnline)
    {
        // 连接就绪：画用户自己选的那个状态的徽标。
        return NormalizePresenceForMenu(presence);
    }

    // 重连中与主动离线都画"离线"的空心环。这必须与 GetConnectionPresenceVisual 给出的
    // 灰色圆点色保持同一口径 —— 两者一旦分叉，就会出现"点是灰的、形状却画成在线实心圆"
    // 这种自相矛盾的胶囊。
    return presence_offline;
}

const wchar_t* GetConnectionMenuLabel(int presence, int connUiState)
{
    if (connUiState == kConnUiOnline)
    {
        // 连接就绪：菜单里被标记的那一行就是用户自己选的状态，文字与主面板状态菜单一致。
        return GetPresenceMenuLabel(presence, true);
    }

    if (connUiState == kConnUiConnecting)
    {
        // 掉线重连中，或刚从离线切回在线正在登录。这里写成完整字面量而不是运行时拼接，
        // 是为了让返回值仍是进程级生命周期的静态常量串 —— 这是 TrayMenuEntry::label
        // 对调用方的承诺（同 GetPresenceMenuLabel 的做法）。
        return L"正在连接（当前）";
    }

    // kConnUiOffline，以及一切越界取值：一律"离线（当前）"，兜底方向与
    // GetConnectionPresenceVisual / GetConnectionBadgePresence 保持一致。
    return L"离线（当前）";
}

bool ShouldGrayAvatar(int connUiState)
{
    // 与 GetTrayIconFileName 同一个判断：只有真正在线才是彩色，其余（含越界值）全灰。
    return connUiState != kConnUiOnline;
}

bool ShouldAutoReconnect(bool userWentOffline, bool enableReconnect)
{
    if (userWentOffline)
    {
        // 用户主动离线的语义就是"别再连了"，此时无论配置如何都不重连。
        return false;
    }
    return enableReconnect;
}

} // namespace balloonmain
