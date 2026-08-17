/**
 *  flamingoAdmin —— 新建服务号弹窗 ServiceModal。
 *
 *  企业详情屏「服务号」tab 点击「新建」时弹出。填写服务号的基本资料：
 *  名称、头像文字、头像配色、类型、描述。提交后由 AdminFrame 写入 DataStore；
 *  新服务号默认绑定到整个企业（全员），绑定范围的精细编辑在 Phase 4b-②
 *  的绑定编辑器里完成。
 *
 *  实现与其它弹窗一致：半透明遮罩 + 居中表单卡片，常驻于 RootContainer
 *  弹窗层，开 / 关只切显隐；点遮罩不关闭。名称 / 头像文字 / 描述是
 *  DuiEdit 真输入框；配色与类型是自绘可点区。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

#include "DuiControl.h"
#include "Mock/MockData.h"

namespace balloonwjui { class DuiEdit; }

namespace flamingoadmin {

// 新建服务号弹窗。
class ServiceModal : public balloonwjui::DuiControl
{
public:
    ServiceModal();

    // 把表单清空到初始状态（每次打开前调用）。
    void Reset();

    // 校验必填项：名称与头像文字均非空。
    bool Validate() const;

    // ---- 表单字段读取 ----
    CString     GetName() const;    // 服务号名称
    CString     GetGlyph() const;   // 头像文字
    CString     GetDesc() const;    // 描述
    COLORREF    GetColor() const;   // 头像配色
    ServiceKind GetKind() const;    // 服务号类型

    void                     Layout(const RECT& rcAvail) override;
    void                     OnPaint(HDC hdc, const RECT& rcDirty) override;
    balloonwjui::DuiControl*  HitTest(POINT ptHostClient) override;
    bool                     OnLButtonUp(POINT pt, UINT mkFlags) override;

private:
    // 一个可点区（配色色块 / 类型卡片）。
    struct Hit
    {
        RECT m_rc;
        int  m_kind;   // 0=选配色 1=选类型
        int  m_arg;    // 配色下标 / 类型下标
    };

    RECT             m_panelRect{};
    int              m_colorIndex = 0;   // 当前配色下标
    int              m_kindIndex  = 0;   // 当前类型下标
    std::vector<Hit> m_hits;             // 本帧记录的可点区

    balloonwjui::DuiEdit* m_editName  = nullptr;
    balloonwjui::DuiEdit* m_editGlyph = nullptr;
    balloonwjui::DuiEdit* m_editDesc  = nullptr;
};

} // namespace flamingoadmin
