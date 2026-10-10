/**
 *  flamingoAdmin —— FlamingoLink.dll 页面窗口状态判定的实现，说明见 LinkPageState.h。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "Util/LinkPageState.h"

namespace flamingoadmin {

LinkPageState DecideLinkPageState(bool loggedIn, bool detailVisible, bool onlineTab, bool modalVisible)
{
    LinkPageState s;
    s.m_active = loggedIn && detailVisible && onlineTab;
    //页面不活动时窗口本来就隐藏，「被盖住」没有意义，一律记为 false
    s.m_covered = s.m_active && modalVisible;
    return s;
}

} // namespace flamingoadmin
