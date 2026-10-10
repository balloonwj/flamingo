/**
 *  GroupQaEditDialog.h
 *
 *  群聊窗「问答」页签的「新建问答」「修改问答」对话框（docs/群问答方案.md）。只有群主与管理员打得开。
 *  点「保存」时先做本地校验（GroupQaModel 的 CheckQaInput），通过后把问题与答案投给宿主群聊窗
 *  （resultMsg，lParam 为堆上的 QaEditResult*，由宿主 delete）并关闭；由宿主发送 cmd_save_group_qa。
 *  保存失败由主面板按应答提示，成功后服务端推 1339，各端重拉问答。
 *
 *  打开期间禁用宿主窗口，关闭时恢复（与备忘编辑窗口同一做法）。
 *
 *  典型用法：
 *      m_qaEditDlg.Open(m_hWnd, qaId, question, answer, kQaEditResultMsg);
 *      //宿主窗口收到 kQaEditResultMsg：
 *      std::unique_ptr<QaEditResult> r((QaEditResult*)lParam);
 *      CIUSocket::GetInstance().SendSaveGroupQa(m_groupId, r->m_id, r->m_questionUtf8, r->m_answerUtf8);
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <string>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../BalloonMain/TitleBarIcon.h"          // ProgramWindowIcons：标题栏左侧的程序图标
#include "../BalloonMain/ClosePromptDecision.h"   // ClosePromptHostGuard：打开期间禁用宿主窗口

namespace balloonchatwnd {

// 对话框交回宿主的内容（堆上分配，宿主处理后 delete）
struct QaEditResult
{
    int64_t     m_id;             // 要修改的问答ID；0 表示新增
    std::string m_questionUtf8;   // 问题（UTF-8，已去掉首尾空白）
    std::string m_answerUtf8;     // 答案（UTF-8，已去掉首尾空白）

    QaEditResult()
        : m_id(0)
    {
    }
};

/**
 *  CGroupQaEditDialog —— 新建 / 修改一条本群问答的对话框（非模态，打开期间禁用宿主窗口）。
 *  同一时刻只存在一个实例（宿主群聊窗持有）；已打开时再次 Open 会换成新的内容并前置。
 */
class CGroupQaEditDialog : public balloonwjui::DuiFrameWindow
{
public:
    CGroupQaEditDialog();
    ~CGroupQaEditDialog();

    /**
     *  打开对话框。
     *    owner：    宿主窗口（群聊窗），对话框居中到它、打开期间禁用它，保存时把结果投给它；
     *    qaId：     要修改的问答ID；0 表示新建；
     *    question： 原问题（新建时为空）；
     *    answer：   原答案（新建时为空）；
     *    resultMsg：点「保存」且校验通过后投给 owner 的消息号，lParam 为堆上的 QaEditResult*。
     *  @return 窗口建出或前置成功返回 true
     */
    bool Open(HWND owner, int64_t qaId, const std::wstring& question, const std::wstring& answer, UINT resultMsg);

    /**
     *  关闭对话框并恢复宿主窗口；没有打开时什么都不做。宿主窗口销毁前调用。
     */
    void CloseDialog();

    BEGIN_MSG_MAP(CGroupQaEditDialog)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 「保存」：校验，通过后投递结果并关闭；不通过时在提示行显示原因
    void OnSave();

    // 在提示行显示一句话；传空串清空
    void ShowError(const std::wstring& text);

private:
    HWND     m_ownerHwnd;   // 宿主窗口（群聊窗），借用
    int64_t  m_qaId;        // 正在修改的问答ID；0 表示新建
    UINT     m_resultMsg;   // 保存时投给宿主的消息号
    balloonmain::ClosePromptHostGuard m_ownerGuard;   // 是否由本窗口禁用了宿主、关闭时要不要恢复
    balloonmain::ProgramWindowIcons   m_programIcons; // 标题栏左侧的程序图标（本类持有）
};

} // namespace balloonchatwnd
