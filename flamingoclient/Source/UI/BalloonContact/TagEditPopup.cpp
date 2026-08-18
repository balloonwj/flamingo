/**
 *  TagEditPopup.cpp
 *
 *  标签输入弹层的实现。子类化 DuiPopupHost，内部一个单行 DuiEdit；打开时
 *  盖在目标矩形上、带入当前文本并全选，回车 / 失焦提交、ESC 取消。
 *
 *  balloonwj@qq.com   2026-08-14
 */

#include "stdafx.h"
#include "TagEditPopup.h"

#include <memory>

#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "ContactTagModel.h"   // kContactTagMaxLen
#include "../../Common/Log/IULog.h"

namespace ballooncontact {

namespace {

// 内部输入框的控件 id。弹层内只有这一个控件，取值只要在弹层内唯一即可。
const UINT kTagEditCtrlId = 1;

// 输入框的最小宽 / 高（逻辑像素）。标签胶囊本身可能只有四五十像素宽，直接照抄
// 它的尺寸会得到一个窄得没法打字的输入框，故给一个下限。高度下限对齐标签胶囊
// 的行高 24。
const int kMinEditWidth  = 150;
const int kMinEditHeight = 24;

// 输入框相对目标矩形左右各外扩的像素数，免得文字紧贴边框。
const int kEditPadX = 6;

} // anonymous namespace

CTagEditPopup::CTagEditPopup()
    : m_edit(NULL)
    , m_built(false)
    , m_handled(false)
    , m_commitCb(NULL)
    , m_commitUd(NULL)
{
}

CTagEditPopup::~CTagEditPopup()
{
}

void CTagEditPopup::SetCommitCallback(CommitCallback cb, void* userdata)
{
    m_commitCb = cb;
    m_commitUd = userdata;
}

void CTagEditPopup::BuildContentOnce()
{
    if (m_built)
    {
        return;
    }

    // 单行输入框作内容根：带 1px 边框（让人看出这是编辑态）、文字垂直居中、
    // 限长到单个标签的字符上限。限长放在这里是"从源头挡住"——用户敲不进第 17
    // 个字符，判定函数里的超长分支就只在粘贴等异常路径上才会被走到。
    std::unique_ptr<balloonwjui::DuiEdit> edit(new balloonwjui::DuiEdit());
    edit->SetCtrlId(kTagEditCtrlId);
    edit->SetMaxLength(kContactTagMaxLen);
    edit->SetVerticalCenter(true);
    edit->SetShowBorder(true);
    m_edit = edit.get();   // 借用；下面 SetContent 把所有权转移给内容树

    SetContent(std::move(edit));

    // 失焦关闭视为提交，ESC / 程序化关闭不提交，统一在 dismiss 回调里区分。
    SetDismissCallback(&CTagEditPopup::OnDismissThunk, this);

    m_built = true;
}

void CTagEditPopup::OpenAt(const RECT& anchorScreen, HWND /*owner*/, LPCTSTR currentText)
{
    BuildContentOnce();

    const int anchorW = anchorScreen.right - anchorScreen.left;
    const int anchorH = anchorScreen.bottom - anchorScreen.top;

    int width = anchorW + 2 * kEditPadX;
    if (width < kMinEditWidth)
    {
        width = kMinEditWidth;
    }
    int height = anchorH;
    if (height < kMinEditHeight)
    {
        height = kMinEditHeight;
    }
    SetSize(width, height);

    // 让弹层左上角正好落在目标矩形左上角：EdgeBelow 取 (anchor.left, anchor.bottom)，
    // 故传一个"零高、bottom = 目标顶边"的 anchor（见 DuiPopupHost::ComputePositionScreen）。
    RECT anchor;
    anchor.left   = anchorScreen.left - kEditPadX;
    anchor.right  = anchorScreen.right;
    anchor.top    = anchorScreen.top;
    anchor.bottom = anchorScreen.top;
    SetEdge(balloonwjui::DuiPopupHost::EdgeBelow);

    m_handled = false;

    // owner 传 NULL 而不是调用方给的那个句柄：DuiHost::SendNotify 以
    // GetParent(m_hWnd) 为通知目标，而 WS_POPUP 窗口的 GetParent 返回的正是
    // owner —— 传了 owner，输入框的回车通知就会发到联系人资料窗而不是本弹层。
    // 传 NULL 让 SendNotify 走"自路由"分支把通知送回本弹层的 OnChildNotify。
    // 与 EmojiPickerPopup / FontSettingPopup 同一口径。
    Show(anchor, NULL);

    if (m_edit != NULL)
    {
        m_edit->SetText(currentText != NULL ? currentText : _T(""));
        // 无窗口输入框取焦点即走控件自己的 SetFocus：它会把 DUI 焦点指到本控件，
        // 并让弹层窗口把 Win32 键盘焦点拿到手（无窗口控件的按键消息要先到宿主）。
        m_edit->SetFocus();
        m_edit->SelectAll();   // 全选，便于直接覆盖输入
    }

    LOG_INFO("contact tag popup: opened.");
}

LRESULT CTagEditPopup::OnChildNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                     LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify =
        reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }

    // 通知码与控件编号必须一起判：库里的自定义通知码是按控件各自编号的，
    // 不同控件的同序号自定义码数值相同，只比通知码会把别的控件的通知截住。
    if (pNotify->code == (UINT)balloonwjui::DuiEdit::DUIN_EDIT_ENTER
        && pNotify->ctrlId == kTagEditCtrlId)
    {
        OnEditEnter();
        return 0;
    }

    // Esc 一般走不到这里 —— DuiPopupHost 在自己的按键处理里就把 Esc 拦下关闭了，
    // 按键到不了输入框。留着这一支是为了不依赖那条时序：万一将来基类不再拦，
    // 本弹层仍按"取消"处理。
    if (pNotify->code == (UINT)balloonwjui::DuiEdit::DUIN_EDIT_ESCAPE
        && pNotify->ctrlId == kTagEditCtrlId)
    {
        OnEditEscape();
        return 0;
    }

    bHandled = FALSE;
    return 0;
}

void CTagEditPopup::OnEditEnter()
{
    // 回车：先提交，再以"程序化"原因关闭（该原因不会再触发一次提交）。
    DoCommit();
    Hide(balloonwjui::DuiPopupHost::ReasonProgrammatic);
}

void CTagEditPopup::OnEditEscape()
{
    // ESC：取消。先标记本次已处理，免得随后的失焦关闭又走一次提交。
    m_handled = true;
    Hide(balloonwjui::DuiPopupHost::ReasonEscape);
}

void CTagEditPopup::OnDismissThunk(void* userdata, balloonwjui::DuiPopupHost::Reason r)
{
    CTagEditPopup* self = (CTagEditPopup*)userdata;
    if (self != NULL)
    {
        self->OnDismiss(r);
    }
}

void CTagEditPopup::OnDismiss(balloonwjui::DuiPopupHost::Reason r)
{
    // ESC 关闭：标记本次打开已处理，后续任何路径都不再提交。基类 DuiPopupHost
    // 自己就会在按键处理里拦下 ESC 并以该原因关闭，不经过本弹层的 OnEditEscape，
    // 故这个标记要在这里补上。
    if (r == balloonwjui::DuiPopupHost::ReasonEscape)
    {
        m_handled = true;
        return;
    }

    // 只有"失焦关闭"才视为提交；程序化关闭不提交（回车那条路径已经在
    // OnEditEnter 里提交过了）。
    if (r == balloonwjui::DuiPopupHost::ReasonLostFocus)
    {
        DoCommit();
    }
}

void CTagEditPopup::DoCommit()
{
    if (m_handled)
    {
        return;   // 本次打开已经提交或取消过
    }
    m_handled = true;

    CString text = (m_edit != NULL) ? m_edit->GetText() : CString();
    if (m_commitCb != NULL)
    {
        m_commitCb(m_commitUd, text);
    }
}

} // namespace ballooncontact
