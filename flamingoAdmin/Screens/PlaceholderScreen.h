/**
 *  flamingoAdmin —— 占位界面。
 *
 *  居中显示「图标方块 + 标题 + 副标题」。Phase 2 阶段 5 个主界面（仪表板 /
 *  企业管理 / 公告中心 / 审计日志 / 平台设置）均以本控件占位；后续阶段把
 *  前四者逐一替换为真实界面，平台设置则长期保持占位。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

#include "DuiControl.h"
#include "Widgets/IconRenderer.h"

namespace flamingoadmin {

// 占位界面控件（自绘，无子控件）。
class PlaceholderScreen : public balloonwjui::DuiControl
{
public:
    // 设置占位内容。
    //   icon：居中图标；title：标题；subtitle：副标题说明。
    void SetContent(ui::Icon icon, LPCTSTR title, LPCTSTR subtitle);

    void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    ui::Icon m_icon = ui::Icon::Dashboard;
    CString  m_title;
    CString  m_subtitle;
};

} // namespace flamingoadmin
