/**
 *  MainPanelSystemMenu.cpp
 *
 *  主面板系统菜单行模型的实现。仅装配数据（项 id / 文本 / 图标 / 分隔条），
 *  不碰任何窗口 —— 故可被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程
 *  逐字共用。在 Flamingo.vcxproj 里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  本文件含中文宽字符字面量（菜单文本）。两个工程均已开启 /utf-8 编译选项，
 *  源码按 UTF-8 解析，L"中文" 字面量正常，无需 BOM。
 *
 *  balloonwj@qq.com   2026-06-03
 */

#include "MainPanelSystemMenu.h"
#include "MainPanelIcons.h"        // IconKind（各项左侧图标类型）

namespace balloonmain {

std::vector<SystemMenuEntry> BuildSystemMenuModel()
{
    // 顺序严格对照 design/主面板系统菜单.png：
    //   个人信息 / 修改密码
    //   ──────────（分隔）
    //   公告与通知
    //   ──────────（分隔）
    //   系统设置 / 网络设置
    //   ──────────（分隔）
    //   关于 FlamingoIM
    std::vector<SystemMenuEntry> items;

    items.push_back(SystemMenuEntry{ kSysMenuProfile,     false, L"个人信息",        kIconUser,  true });
    items.push_back(SystemMenuEntry{ kSysMenuChangePwd,   false, L"修改密码",        kIconLock,  true });

    items.push_back(SystemMenuEntry{ 0,                   true,  nullptr,            -1,         false });

    items.push_back(SystemMenuEntry{ kSysMenuAnnounce,    false, L"公告与通知",      kIconBell,  true });

    items.push_back(SystemMenuEntry{ 0,                   true,  nullptr,            -1,         false });

    items.push_back(SystemMenuEntry{ kSysMenuSettings,    false, L"系统设置",        kIconGear,  true });
    items.push_back(SystemMenuEntry{ kSysMenuNetSettings, false, L"网络设置",        kIconGlobe, true });

    items.push_back(SystemMenuEntry{ 0,                   true,  nullptr,            -1,         false });

    // 登出：单独一组，排在"关于 FlamingoIM"之上。它是"结束当前会话"类操作（断开连接、
    // 关掉所有聊天窗、回登录界面），与上下两侧的设置 / 关于等查看类操作性质不同，
    // 故两侧都用分隔线隔开，降低误点概率。
    items.push_back(SystemMenuEntry{ kSysMenuLogout,      false, L"登出",            kIconLogout, true });

    items.push_back(SystemMenuEntry{ 0,                   true,  nullptr,            -1,          false });

    items.push_back(SystemMenuEntry{ kSysMenuAbout,       false, L"关于 FlamingoIM", kIconInfo,   true });

    return items;
}

} // namespace balloonmain
