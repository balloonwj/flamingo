/**
 *  flamingoAdmin —— 新增企业弹窗 NewEnterpriseModal。
 *
 *  对应设计稿企业管理界面的「新增企业」弹窗：压暗的半透明遮罩 + 居中的
 *  白色卡片（标题栏 + 提示横幅 + 表单 + 取消 / 创建按钮）。
 *
 *  实现要点：
 *  (1) 单 HWND 内的浮层。本控件铺满整个客户区，自绘半透明遮罩，再把固定
 *      尺寸的表单卡片居中绘制 —— 不另开顶层窗口。
 *  (2) 常驻、切显隐。弹窗一经构建便常驻于 RootContainer 的「弹窗层」，
 *      打开 / 关闭只切换可见性，<u>绝不销毁</u>，从根本上杜绝「点击关闭
 *      按钮后销毁自身所在控件树」导致的悬空指针崩溃。
 *  (3) 表单里的文本框是 DuiEditHost，它是无窗口控件，随本弹窗一起参与绘制，
 *      构建完即可读写文本，不需要额外的创建步骤。
 *
 *  弹窗内控件的通知（X / 取消 / 创建 按钮）经 WM_DUI_NOTIFY 上报对应
 *  ctrlId（kIdModal*），由 AdminFrame 处理。点击遮罩空白处<u>不关闭</u>
 *  弹窗，仅吞掉该点击以阻断对背后界面的操作；关闭弹窗只能经 X / 取消。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#pragma once

#include "DuiControl.h"

namespace balloonwjui
{
    class DuiEditHost;
    class DuiComboBox;
}

namespace flamingoadmin {

// 新增企业弹窗。
class NewEnterpriseModal : public balloonwjui::DuiControl
{
public:
    // 构造时一次性建好遮罩 + 表单卡片整棵控件树。
    NewEnterpriseModal();

    // 把表单清空到初始状态（每次打开弹窗前调用）。
    void Reset();

    // 校验必填项（企业全称 / 主管理员姓名 / 管理员邮箱均非空）。
    //   返回：true 通过；false 有必填项为空。
    bool Validate() const;

    // ---- 表单字段读取（供 AdminFrame 提交时取值）----
    CString GetEnterpriseName() const;   // 企业全称
    CString GetShortName() const;        // 企业简称
    CString GetIndustry() const;         // 所属行业
    CString GetRegion() const;           // 主要地区
    CString GetPlan() const;             // 套餐
    CString GetAdminName() const;        // 主管理员姓名
    CString GetContact() const;          // 管理员邮箱

    void                     Layout(const RECT& rcAvail) override;
    void                     OnPaint(HDC hdc, const RECT& rcDirty) override;
    balloonwjui::DuiControl*  HitTest(POINT ptHostClient) override;
    bool                     OnLButtonUp(POINT pt, UINT mkFlags) override;

private:
    balloonwjui::DuiControl*  m_panel       = nullptr;  // 表单卡片（借用，在控件树中）
    balloonwjui::DuiEditHost* m_editName    = nullptr;  // 企业全称
    balloonwjui::DuiEditHost* m_editShort   = nullptr;  // 企业简称
    balloonwjui::DuiEditHost* m_editRegion  = nullptr;  // 主要地区
    balloonwjui::DuiEditHost* m_editAdmin   = nullptr;  // 主管理员姓名
    balloonwjui::DuiEditHost* m_editContact = nullptr;  // 管理员邮箱
    balloonwjui::DuiComboBox* m_cbIndustry  = nullptr;  // 所属行业
    balloonwjui::DuiComboBox* m_cbPlan      = nullptr;  // 套餐
    RECT                      m_panelRect{};            // 表单卡片矩形（居中算出）
};

} // namespace flamingoadmin
