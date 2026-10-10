/**
 *  flamingoAdmin —— 新增 / 编辑企业问答弹窗 EnterpriseQaModal（docs/群问答方案.md）。
 *
 *  企业问答页点「新增问答」或某一行的「编辑」时弹出：问题输入框（最多 128 个字）、答案输入框（多行，最多 2048 个字）、
 *  应用范围下拉框（全部群 / 所选的群）、本企业群的勾选列表（应用范围为「所选的群」时可用）。点「保存」时先在本地校验，
 *  通过后由 AdminFrame 交给企业问答页提交；服务端拒绝时在卡片里用红字提示，弹窗不关闭。
 *
 *  实现与敏感词弹窗一致：半透明遮罩 + 居中卡片，常驻 RootContainer 弹窗层，开 / 关只切显隐；点遮罩不关闭。
 *  单元测试见 Tests/EnterpriseQaTests.cpp 的 EQA-3。
 *
 *  典型用法（AdminFrame 内）：
 *      m_qaModal->ShowForAdd(screen->Groups());
 *      m_root->ShowModal(m_qaModal);
 *      //点「保存」时
 *      CString error;
 *      if (!m_qaModal->Validate(&error)) { m_qaModal->SetError(error); return; }
 *      m_qaModal->SetBusy(true);
 *      screen->SubmitQa(m_qaModal->GetItem());
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <set>
#include <vector>

#include "DuiControl.h"
#include "Util/QaAdminApi.h"

namespace balloonwjui { class DuiEdit; class DuiButton; class DuiComboBox; class DuiListBox; }

namespace flamingoadmin {

// 新增 / 编辑企业问答弹窗。
class EnterpriseQaModal : public balloonwjui::DuiControl
{
public:
    EnterpriseQaModal();

    // 切到「新增」：清空表单，应用范围选「全部群」，群列表按 groups 填好、都不勾选。
    //   groups：本企业的群（可能还没拉到，为空；到了之后调 SetGroups 补上）。
    void ShowForAdd(const std::vector<qaapi::QaGroup>& groups);

    // 切到「编辑」：用 item 预填表单，群列表按 groups 填好并勾选 item 选中的群。
    void ShowForEdit(const qaapi::QaItem& item, const std::vector<qaapi::QaGroup>& groups);

    // 群列表到了（或刷新了）：按 groups 重填，保留已勾选的群。
    void SetGroups(const std::vector<qaapi::QaGroup>& groups);

    // 应用范围下拉框变了（kIdQaModalScope 的 DUIN_VALUECHANGED）：群列表只在「所选的群」时可用。
    void OnScopeChanged();

    // 是否处于编辑态（false 为新增态）。
    bool IsEditing() const { return m_editingId > 0; }

    // 编辑态下正在编辑的问答 ID；新增态返回 0。
    long long EditingId() const { return m_editingId; }

    // 表单当前的内容：问题与答案已去掉首尾空白，应用范围为「全部群」时不带群。
    qaapi::QaItem GetItem() const;

    // 本地校验（qaapi::ValidateQa）。
    //   error：[出参，可为 NULL] 不通过时的中文原因。
    //   返回：通过返回 true。
    bool Validate(CString* error) const;

    // 设置卡片里的红字错误提示并重绘；空串表示不显示。
    void SetError(const CString& text);

    // 当前的错误提示。
    const CString& ErrorText() const { return m_error; }

    // 设置「提交中」状态：提交中「保存」按钮不可用并显示「保存中…」。
    void SetBusy(bool busy);

    // 是否处于提交中。
    bool IsBusy() const { return m_busy; }

    // 群列表里的行数（单元测试用）。
    int GroupRowCount() const;

    // 第 index 行是否勾选（单元测试用，越界返回 false）。
    bool IsGroupRowChecked(int index) const;

    // 勾选或取消第 index 行（单元测试用，越界时什么都不做）。
    void SetGroupRowChecked(int index, bool checked);

    // 选中应用范围（单元测试用）：kQaScopeAll 或 kQaScopeSelected。
    void SetScope(int scope);

    void Layout(const RECT& rcAvail) override;
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    // 按 m_groups 重填群列表，勾选 checked 里的群
    void FillGroupList(const std::set<long long>& checked);

    // 当前勾选的群
    std::set<long long> CheckedGroups() const;

    long long                    m_editingId;   // 编辑态下的问答 ID；0 表示新增态
    CString                      m_error;       // 红字错误提示，空串表示不显示
    bool                         m_busy;        // 是否处于提交中
    RECT                         m_panelRect;   // 卡片矩形
    int                          m_errorTop;    // 错误提示行的顶边（Layout 时算出）
    std::vector<qaapi::QaGroup>  m_groups;      // 群列表里的群，下标与列表行一致
    balloonwjui::DuiEdit*        m_question;    // 问题输入框；所有权在子控件列表
    balloonwjui::DuiEdit*        m_answer;      // 答案输入框；所有权在子控件列表
    balloonwjui::DuiComboBox*    m_scope;       // 应用范围下拉框；所有权在子控件列表
    balloonwjui::DuiListBox*     m_groupList;   // 群勾选列表；所有权在子控件列表
    balloonwjui::DuiButton*      m_btnClose;    // 右上角关闭按钮
    balloonwjui::DuiButton*      m_btnCancel;   // 「取消」按钮
    balloonwjui::DuiButton*      m_btnSave;     // 「保存」按钮
};

} // namespace flamingoadmin
