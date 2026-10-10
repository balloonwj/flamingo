/**
 *  flamingoAdmin —— 批量导入敏感词弹窗 SensitiveImportModal（docs/敏感词方案.md 第 7.4 节）。
 *
 *  敏感词界面点「批量导入」时弹出：说明文字、多行输入框（可直接粘贴，每行「词条[Tab 分类名[Tab 备注]]」）、
 *  默认分类下拉框、「从文件读取」按钮。点「开始导入」后由 AdminFrame 交给敏感词界面分批提交；导入过程中
 *  输入框只读、各按钮不可用，状态行显示进度；导入结束后输入框换成只读的汇总（成功几条、跳过几条，
 *  被跳过的行逐条列出行号、词条与原因），主按钮变为「完成」，点了关闭弹窗。
 *
 *  实现与添加 / 编辑弹窗一致：半透明遮罩 + 居中卡片，常驻 RootContainer 弹窗层，开 / 关只切显隐；
 *  点遮罩不关闭。各按钮经自己的 ctrlId 上报 AdminFrame。单元测试见 Tests/SensitiveImportExportTests.cpp 的 SIM 系列。
 *
 *  典型用法（AdminFrame 内）：
 *      m_swImportModal->ShowFresh();
 *      m_root->ShowModal(m_swImportModal);
 *      //点「开始导入」时
 *      if (screen->StartImport(m_swImportModal->GetText(), m_swImportModal->GetDefaultCategory(), &error))
 *          m_swImportModal->SetImporting(true);
 *      //导入结束时
 *      m_swImportModal->ShowResult(summary, isError);
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include "DuiControl.h"

namespace balloonwjui { class DuiEdit; class DuiButton; class DuiComboBox; }

namespace flamingoadmin {

// 批量导入敏感词弹窗。
class SensitiveImportModal : public balloonwjui::DuiControl
{
public:
    SensitiveImportModal();

    // 恢复到初始状态：清空输入框并恢复可编辑，默认分类选「其他」，清掉状态行，
    // 按钮恢复为「取消」「开始导入」且都可用。
    void ShowFresh();

    // 输入框里的全文。
    CString GetText() const;

    // 用读到的文件内容覆盖输入框。
    //   text：文件内容（已解码）。
    void SetText(const CString& text);

    // 默认分类，1～6。
    int GetDefaultCategory() const;

    // 设置状态行并重绘：进度、错误、「已读取 N 行」等。
    //   text：状态文字，空串表示不显示。isError：true 时用红字。
    void SetStatus(const CString& text, bool isError);

    // 当前的状态文字。
    const CString& StatusText() const { return m_status; }

    // 状态行是否按错误显示。
    bool IsStatusError() const { return m_statusIsError; }

    // 进入 / 退出导入中：导入中输入框只读，「开始导入」「从文件读取」「取消」与关闭按钮都不可用。
    void SetImporting(bool importing);

    // 是否处于导入中。
    bool IsImporting() const { return m_importing; }

    // 导入结束：输入框换成只读的汇总文字，「取消」与「从文件读取」隐藏，主按钮改为「完成」并恢复可用。
    //   summary：汇总文字（SensitiveWordsScreen::BuildImportSummary 的结果）。
    //   isError：true 表示中途失败，状态行用红字写「导入中途失败」，否则写「导入完成」。
    void ShowResult(const CString& summary, bool isError);

    // 是否已显示导入结果（此时点主按钮的含义是「完成」，关闭弹窗）。
    bool IsDone() const { return m_done; }

    // 子控件（单元测试据此核对状态）。所有权在子控件列表。
    balloonwjui::DuiButton* SubmitButton() { return m_btnSubmit; }
    balloonwjui::DuiButton* CancelButton() { return m_btnCancel; }
    balloonwjui::DuiButton* ReadFileButton() { return m_btnReadFile; }
    balloonwjui::DuiButton* CloseButton() { return m_btnClose; }
    balloonwjui::DuiEdit*   TextEdit() { return m_editText; }

    void Layout(const RECT& rcAvail) override;
    void OnPaint(HDC hdc, const RECT& rcDirty) override;

private:
    CString                   m_status;         // 状态行文字，空串表示不显示
    bool                      m_statusIsError;  // 状态行是否用红字
    bool                      m_importing;      // 是否处于导入中
    bool                      m_done;           // 是否已显示导入结果
    RECT                      m_panelRect;      // 卡片矩形
    int                       m_statusTop;      // 状态行的顶边（Layout 时算出）
    balloonwjui::DuiEdit*     m_editText;       // 多行输入框；所有权在子控件列表
    balloonwjui::DuiComboBox* m_category;       // 默认分类下拉框；所有权在子控件列表
    balloonwjui::DuiButton*   m_btnReadFile;    // 「从文件读取」按钮
    balloonwjui::DuiButton*   m_btnClose;       // 右上角关闭按钮
    balloonwjui::DuiButton*   m_btnCancel;      // 「取消」按钮
    balloonwjui::DuiButton*   m_btnSubmit;      // 「开始导入」/「完成」按钮
};

} // namespace flamingoadmin
