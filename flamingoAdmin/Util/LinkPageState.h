/**
 *  flamingoAdmin —— 决定 FlamingoLink.dll「在线用户」页面窗口的状态（纯函数）。
 *
 *  页面窗口是原生子窗口，会画在控制台自己绘制的一切之上，所以控制台要明确告诉 DLL 两件事：
 *    - 页面是否处于活动状态：企业详情屏确实可见（它与它的每一层父控件都可见），且选中了「在线用户」子 tab。
 *      活动时 DLL 显示窗口并订阅在线名单，不活动时隐藏并退订；
 *    - 页面是否被盖住：叠层弹窗显示时暂时隐藏窗口，但不退订，弹窗关掉后照原样显示。
 *  单元测试见 Tests/FlamingoLinkHostTests.cpp 的 FLK11。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

namespace flamingoadmin {

// 页面窗口的状态
struct LinkPageState
{
    bool m_active;    // 页面是否处于活动状态（显示并订阅）
    bool m_covered;   // 页面是否被叠层弹窗盖住（只隐藏，不退订）

    LinkPageState()
        : m_active(false), m_covered(false)
    {
    }
};

// 按控制台界面的当前情况决定页面窗口的状态。
//   loggedIn：是否已登录（未登录时外壳页不显示，页面一律不活动）。
//   detailVisible：企业详情屏是否确实可见（本身与每一层父控件都可见）。
//   onlineTab：企业详情屏是否选中了「在线用户」子 tab。
//   modalVisible：叠层弹窗是否显示。
//   返回：页面窗口的状态。
LinkPageState DecideLinkPageState(bool loggedIn, bool detailVisible, bool onlineTab, bool modalVisible);

} // namespace flamingoadmin
