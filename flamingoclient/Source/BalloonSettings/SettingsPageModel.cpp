/**
 *  SettingsPageModel.cpp —— 系统设置导航纯数据模型实现。
 *
 *  仅含无副作用的纯逻辑（行模型构造 + 选中夹取 + 可见性计算），与 GDI / HWND /
 *  balloonui 完全解耦，故可同时编进主程序（Flamingo）与单测工程（FlamingoUnit）。
 *
 *  balloonwj@qq.com   2026-06-03
 */

// 纯数据模型，与 FlamingoUnit 测试工程共用，<u>不</u> include stdafx.h，保持 PCH 无关。
#include "SettingsPageModel.h"

namespace balloonsettings {

std::vector<SettingsNavEntry> BuildSettingsNavModel()
{
    std::vector<SettingsNavEntry> items;
    items.reserve(kPageCount);

    // 顺序、文本严格对照 design/系统设置1~7.png 左侧导航。implemented 标记本期
    // 是否已接入真实内容：前三页为本期范围，其余四页占位。
    items.push_back(SettingsNavEntry{ kPageGeneral,    L"通用",     true  });
    items.push_back(SettingsNavEntry{ kPageNotify,     L"消息通知", true  });
    items.push_back(SettingsNavEntry{ kPageChat,       L"聊天",     true  });
    items.push_back(SettingsNavEntry{ kPageAudioVideo, L"音视频",   false });
    items.push_back(SettingsNavEntry{ kPageShortcut,   L"快捷键",   false });
    items.push_back(SettingsNavEntry{ kPageStorage,    L"存储",     false });
    items.push_back(SettingsNavEntry{ kPageAccount,    L"账号安全", false });

    return items;
}

int ClampNavSelection(int index, int count)
{
    if (count <= 0)
    {
        return -1;
    }
    if (index < 0)
    {
        return 0;
    }
    if (index >= count)
    {
        return count - 1;
    }
    return index;
}

std::vector<bool> ComputePageVisibility(int selectedIndex, int pageCount)
{
    if (pageCount <= 0)
    {
        return std::vector<bool>();
    }

    int sel = ClampNavSelection(selectedIndex, pageCount);

    std::vector<bool> vis((size_t)pageCount, false);
    if (sel >= 0)
    {
        vis[(size_t)sel] = true;
    }
    return vis;
}

} // namespace balloonsettings
