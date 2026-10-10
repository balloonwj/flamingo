/**
 *  屏幕共享「共享内容」选择的纯逻辑规则，ShareSourcePolicy.cpp
 *  规则说明见 ShareSourcePolicy.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "ShareSourcePolicy.h"

namespace screenshare {

ShareWindowFacts::ShareWindowFacts()
    : m_visible(false)
    , m_cloaked(false)
    , m_toolWindow(false)
    , m_appWindow(false)
    , m_noActivate(false)
    , m_hasOwner(false)
    , m_ownProcess(false)
    , m_minimized(false)
    , m_width(0)
    , m_height(0)
{
}

namespace {

//系统外壳窗口的类名：桌面（Progman / WorkerW）与主、副任务栏，它们可见且有时带标题，但不是用户要共享的内容
const wchar_t* const kShellClassNames[] =
{
    L"Progman",
    L"WorkerW",
    L"Shell_TrayWnd",
    L"Shell_SecondaryTrayWnd",
};

bool isBlankTitle(const std::wstring& title)
{
    for (size_t i = 0; i < title.size(); ++i)
    {
        if (title[i] != L' ' && title[i] != L'\t')
            return false;
    }
    return true;
}

} // namespace

bool ShouldListShareWindow(const ShareWindowFacts& facts)
{
    if (!facts.m_visible || facts.m_cloaked || facts.m_ownProcess || isBlankTitle(facts.m_title))
        return false;

    for (size_t i = 0; i < sizeof(kShellClassNames) / sizeof(kShellClassNames[0]); ++i)
    {
        if (facts.m_className == kShellClassNames[i])
            return false;
    }

    //工具窗口、有属主的窗口、不可激活的窗口一般是浮动面板或提示，只有强制出现在任务栏的才算应用窗口
    if ((facts.m_toolWindow || facts.m_hasOwner || facts.m_noActivate) && !facts.m_appWindow)
        return false;

    //最小化的窗口外框很小，不看尺寸
    if (!facts.m_minimized && (facts.m_width < kShareWindowMinSize || facts.m_height < kShareWindowMinSize))
        return false;
    return true;
}

ShareDxgiOutputRef::ShareDxgiOutputRef(int adapter, int output, HMONITOR monitor)
    : m_adapter(adapter)
    , m_output(output)
    , m_monitor(monitor)
{
}

bool FindDxgiOutputForMonitor(const std::vector<ShareDxgiOutputRef>& outputs, HMONITOR monitor, int& adapter,
                              int& output)
{
    for (size_t i = 0; i < outputs.size(); ++i)
    {
        if (outputs[i].m_monitor == monitor)
        {
            adapter = outputs[i].m_adapter;
            output = outputs[i].m_output;
            return true;
        }
    }
    return false;
}

void FitShareSize(int srcW, int srcH, int boxW, int boxH, int& outW, int& outH)
{
    outW = 0;
    outH = 0;
    if (srcW <= 0 || srcH <= 0 || boxW <= 0 || boxH <= 0)
        return;

    //已在框内：不放大
    if (srcW <= boxW && srcH <= boxH)
    {
        outW = srcW;
        outH = srcH;
        return;
    }
    //比较 boxW / srcW 与 boxH / srcH，用交叉相乘避免浮点：宽度先到框边则按宽缩，否则按高缩
    const long long widthLimited = static_cast<long long>(boxW) * srcH;
    const long long heightLimited = static_cast<long long>(boxH) * srcW;
    if (widthLimited <= heightLimited)
    {
        outW = boxW;
        outH = static_cast<int>(static_cast<long long>(srcH) * boxW / srcW);
    }
    else
    {
        outH = boxH;
        outW = static_cast<int>(static_cast<long long>(srcW) * boxH / srcH);
    }
    if (outW < 1)
        outW = 1;
    if (outH < 1)
        outH = 1;
}

} // namespace screenshare
