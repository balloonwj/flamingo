/**
 *  HistoryDateJumpPopup.cpp —— 消息记录窗口「选日期」弹层的实现，说明见头文件。
 *  balloonwj@qq.com   2026-10-01
 */
#include "stdafx.h"
#include "HistoryDateJumpPopup.h"

#include <memory>

#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiSpinBox.h"
#include "../../Common/Log/IULog.h"
#include "HistoryWindowLayout.h"   // BuildHistoryDatePopupContent / HistoryDatePopupActionFromNotify / 控件编号

using balloonwjui::DuiControl;
using balloonwjui::DuiSpinBox;

namespace balloonhistory {

namespace {

//按编号从内容树里取回一个微调框。
DuiSpinBox* FindSpin(DuiControl* root, UINT ctrlId)
{
    return (root != NULL) ? dynamic_cast<DuiSpinBox*>(root->FindCtrlById(ctrlId)) : NULL;
}

//把一个日期写进三个微调框。预置值不发通知：弹层自己不关心取值变化，只在提交时读一次
void SetSpins(DuiSpinBox* y, DuiSpinBox* m, DuiSpinBox* d, const HistoryDate& date)
{
    y->SetValue(date.m_year, false);
    m->SetValue(date.m_month, false);
    d->SetValue(date.m_day, false);
}

} // namespace

CHistoryDateJumpPopup::CHistoryDateJumpPopup()
    : m_fromYear(NULL)
    , m_fromMonth(NULL)
    , m_fromDay(NULL)
    , m_toYear(NULL)
    , m_toMonth(NULL)
    , m_toDay(NULL)
    , m_built(false)
    , m_committed(false)
    , m_submitCb(NULL)
    , m_submitUd(NULL)
{
}

CHistoryDateJumpPopup::~CHistoryDateJumpPopup()
{
}

void CHistoryDateJumpPopup::SetSubmitCallback(SubmitCallback cb, void* userdata)
{
    m_submitCb = cb;
    m_submitUd = userdata;
}

void CHistoryDateJumpPopup::BuildContentOnce()
{
    if (m_built)
    {
        return;
    }
    std::unique_ptr<DuiControl> content = BuildHistoryDatePopupContent();
    DuiControl* root = content.get();   //借用；所有权随即交给弹层
    m_fromYear  = FindSpin(root, kIdDatePopupFromYear);
    m_fromMonth = FindSpin(root, kIdDatePopupFromMonth);
    m_fromDay   = FindSpin(root, kIdDatePopupFromDay);
    m_toYear    = FindSpin(root, kIdDatePopupToYear);
    m_toMonth   = FindSpin(root, kIdDatePopupToMonth);
    m_toDay     = FindSpin(root, kIdDatePopupToDay);
    SetContent(std::move(content));
    m_built = (m_fromYear != NULL && m_fromMonth != NULL && m_fromDay != NULL
               && m_toYear != NULL && m_toMonth != NULL && m_toDay != NULL);
    if (!m_built)
    {
        LOG_ERROR("history date popup: content tree is missing a spin box.");
    }
}

void CHistoryDateJumpPopup::OpenBelow(const RECT& anchorScreen, const HistoryDate& from,
                                      const HistoryDate& to, int minYear, int maxYear)
{
    BuildContentOnce();
    if (!m_built)
    {
        return;
    }
    if (minYear > maxYear)
    {
        const int t = minYear;
        minYear = maxYear;
        maxYear = t;
    }
    m_fromYear->SetRange(minYear, maxYear);
    m_toYear->SetRange(minYear, maxYear);
    SetSpins(m_fromYear, m_fromMonth, m_fromDay, from);
    SetSpins(m_toYear, m_toMonth, m_toDay, to);
    m_committed = false;

    SetSize(kHistoryDatePopupW, kHistoryDatePopupH);
    SetEdge(balloonwjui::DuiPopupHost::EdgeBelow);
    //弹层右缘对齐锚点右缘：DuiPopupHost 放在下方时让弹层左缘对齐锚点左缘，而两个锚点（「选日期」
    //按钮、日期区间标签）都在窗口右侧、又比弹层窄得多，左对齐会让弹层大半伸出窗口右边。
    //这里把锚点矩形的左缘改为"右缘减弹层宽度"再交给它，效果即右对齐
    RECT anchor = anchorScreen;
    anchor.left = anchor.right - kHistoryDatePopupW;
    //owner 传 NULL（与 TagEditPopup / EmojiPickerPopup 同一做法）。原为避免按钮的点击通知被发到消息记录窗口：
    //当时 DuiHost::SendNotify 以 GetParent(m_hWnd) 为通知目标，而有所有者的 WS_POPUP 窗口的 GetParent 返回所有者。
    //2026-10-14 起浮层内容的通知先到浮层自身，本类没有处理的才转给所有者（DuiPopupHost::OnPopupNotify，
    //见开发约定文档 UI 约定第 11 条），这一原因已不存在
    Show(anchor, NULL);

    //焦点放在「从」的「日」上并全选：最常见的是只改日，直接输入即可
    if (m_fromDay->GetEdit() != NULL)
    {
        m_fromDay->GetEdit()->SetFocus();
        m_fromDay->GetEdit()->SelectAll();
    }
    LOG_INFO("history date popup: opened, from %d-%d-%d to %d-%d-%d, years %d..%d.",
             from.m_year, from.m_month, from.m_day, to.m_year, to.m_month, to.m_day, minYear, maxYear);
}

int CHistoryDateJumpPopup::CommitTypedValue(DuiSpinBox* spin)
{
    //手输的数字不会自动同步到微调框（见 DuiSpinBox.h），这里读输入框的文字写回去；
    //SetValue 会把它限制在范围内。输入框为空或不是数字时 _ttoi 返回 0，同样被限制到下限
    if (spin->GetEdit() != NULL)
    {
        spin->SetValue(_ttoi(spin->GetEdit()->GetText()), false);
    }
    return spin->GetValue();
}

void CHistoryDateJumpPopup::DoSubmit(int action)
{
    if (m_committed || !m_built)
    {
        return;
    }
    const HistoryDate from(CommitTypedValue(m_fromYear), CommitTypedValue(m_fromMonth),
                           CommitTypedValue(m_fromDay));
    const HistoryDate to(CommitTypedValue(m_toYear), CommitTypedValue(m_toMonth),
                         CommitTypedValue(m_toDay));
    LOG_INFO("history date popup: submit action %d, from %d-%d-%d to %d-%d-%d.", action,
             from.m_year, from.m_month, from.m_day, to.m_year, to.m_month, to.m_day);
    //先置位再回调：回调里若有消息循环（如轻提示的动画计时）再次送来回车，不会重复提交
    m_committed = true;
    const bool accepted = (m_submitCb == NULL) || m_submitCb(m_submitUd, action, from, to);
    if (!accepted)
    {
        //日期有误：调用方已经给出提示，弹层保持打开，允许改了再提交
        m_committed = false;
        LOG_INFO("history date popup: submit rejected, popup stays open.");
        return;
    }
    Hide();
}

LRESULT CHistoryDateJumpPopup::OnChildNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }
    const int action = HistoryDatePopupActionFromNotify(pNotify->code, pNotify->ctrlId);
    if (action != kHistoryDatePopupNone)
    {
        DoSubmit(action);
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

} // namespace balloonhistory
