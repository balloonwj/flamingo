/**
 *  flamingoAdmin —— 添加 / 编辑敏感词弹窗 SensitiveWordModal（docs/敏感词方案.md 第 7.3 节）。
 *
 *  敏感词界面点「添加」或某一行的「编辑」时弹出：词条输入框（最多 32 个字，超出输入不进去）、
 *  分类下拉框（默认「其他」）、备注输入框（最多 64 个字）。点「保存」时先在本地校验，通过后由
 *  AdminFrame 交给敏感词界面提交；服务端拒绝（词条为空、超长、重复、词库已满、词条不存在等）时
 *  在卡片里用红字提示，弹窗不关闭，管理员改了再提交。提交过程中「保存」按钮不可用，防止连点。
 *
 *  实现与其它弹窗一致：半透明遮罩 + 居中卡片，常驻 RootContainer 弹窗层，开 / 关只切显隐；点遮罩
 *  不关闭。各按钮经自己的 ctrlId 上报 AdminFrame。单元测试见 Tests/SensitiveWordsScreenTests.cpp 的 SB-5。
 *
 *  典型用法（AdminFrame 内）：
 *      m_swModal->ShowForAdd();
 *      m_root->ShowModal(m_swModal);
 *      //点「保存」时
 *      CString error;
 *      if (!m_swModal->Validate(&error)) { m_swModal->SetError(error); return; }
 *      m_swModal->SetBusy(true);
 *      screen->SubmitWord(m_swModal->EditingId(), m_swModal->GetWord(), m_swModal->GetCategory(), m_swModal->GetRemark());
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include "DuiControl.h"
#include "Util/AdminApi.h"

namespace balloonwjui { class DuiEdit; class DuiButton; class DuiComboBox; }

namespace flamingoadmin {

// 添加 / 编辑敏感词弹窗。
class SensitiveWordModal : public balloonwjui::DuiControl
{
public:
    SensitiveWordModal();

    // 切到「添加」：清空表单，分类选「其他」，清掉错误提示与提交中状态。
    void ShowForAdd();

    // 切到「编辑」：用 item 预填表单，清掉错误提示与提交中状态。
    //   item：要编辑的词条。
    void ShowForEdit(const adminapi::SensitiveWordItem& item);

    // 是否处于编辑态（false 为添加态）。
    bool IsEditing() const { return m_editingId > 0; }

    // 编辑态下正在编辑的词条 ID；添加态返回 0。
    long long EditingId() const { return m_editingId; }

    // 本地校验：词条不能为空、不超过 32 个字，备注不超过 64 个字。
    //   error：[出参，可为 NULL] 不通过时的中文原因。
    //   返回：通过返回 true。
    bool Validate(CString* error) const;

    // 词条（首尾空白已去掉）。
    CString GetWord() const;

    // 分类，1～6。
    int GetCategory() const;

    // 备注（首尾空白已去掉）。
    CString GetRemark() const;

    // 设置卡片里的红字错误提示并重绘；空串表示不显示。
    void SetError(const CString& text);

    // 当前的错误提示。
    const CString& ErrorText() const { return m_error; }

    // 设置「提交中」状态：提交中「保存」按钮不可用并显示「保存中…」。
    void SetBusy(bool busy);

    // 是否处于提交中。
    bool IsBusy() const { return m_busy; }

    void Layout(const RECT& rcAvail) override;
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    long long                 m_editingId;  // 编辑态下的词条 ID；0 表示添加态
    CString                   m_error;      // 红字错误提示，空串表示不显示
    bool                      m_busy;       // 是否处于提交中
    RECT                      m_panelRect;  // 卡片矩形
    int                       m_errorTop;   // 错误提示行的顶边（Layout 时算出）
    balloonwjui::DuiEdit*     m_editWord;   // 词条输入框；所有权在子控件列表
    balloonwjui::DuiComboBox* m_category;   // 分类下拉框；所有权在子控件列表
    balloonwjui::DuiEdit*     m_editRemark; // 备注输入框；所有权在子控件列表
    balloonwjui::DuiButton*   m_btnClose;   // 右上角关闭按钮
    balloonwjui::DuiButton*   m_btnCancel;  // 「取消」按钮
    balloonwjui::DuiButton*   m_btnSave;    // 「保存」按钮
};

} // namespace flamingoadmin
