/**
 *  标注模式下的输入面与工具条锚点，说明见 ShareAnnotModePolicy.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"

#include "ShareAnnotModePolicy.h"

namespace screenshare {

ShareAnnotSurfacePlan::ShareAnnotSurfacePlan()
    : m_hostOverlayInput(false)
    , m_viewerCanvasInput(false)
    , m_publishOverlayInput(false)
    , m_boardInput(false)
    , m_screensCanvasInput(false)
    , m_anchor(annot_anchor_none)
{
}

ShareAnnotSurfacePlan DecideAnnotSurfaces(bool on, bool hosting, bool watching, bool ownScreen, bool onBoard,
                                          bool pointerMode, bool onScreens)
{
    ShareAnnotSurfacePlan plan;
    if (!on || (!hosting && !watching))
        return plan;
    //在本人的白板上标注：白板窗口的画布自己接收鼠标，任何屏幕输入层都不打开，否则盖住白板窗口的标题栏
    if (onBoard)
    {
        plan.m_boardInput = !pointerMode;
        plan.m_anchor = annot_anchor_board;
        return plan;
    }
    //主持人在「其他人的屏幕」窗口里对别人的画面标注（2026-10-07）：那个窗口的画面接住输入，共享内容上的输入层不打开
    if (hosting && onScreens)
    {
        plan.m_screensCanvasInput = !pointerMode;
        plan.m_anchor = annot_anchor_screens;
        return plan;
    }
    //主持人在共享的内容上标注
    if (hosting)
    {
        plan.m_hostOverlayInput = !pointerMode;
        plan.m_anchor = annot_anchor_host_source;
        return plan;
    }
    //共享者在自己共享的内容上标注（多人共享）
    if (ownScreen)
    {
        plan.m_publishOverlayInput = !pointerMode;
        plan.m_anchor = annot_anchor_own_source;
        return plan;
    }
    //观看者在观看窗的画面上标注
    plan.m_viewerCanvasInput = !pointerMode;
    plan.m_anchor = annot_anchor_viewer;
    return plan;
}

LPCWSTR AnnotSelectCursorName(bool overObject)
{
    return overObject ? IDC_SIZEALL : IDC_CROSS;
}

} // namespace screenshare
