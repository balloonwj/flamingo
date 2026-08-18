/**
 *  SignatureEditPopup.cpp
 *
 *  个性签名内联编辑弹层的实现。子类化 DuiPopupHost，内部一个单行 DuiEdit；
 *  打开时精确盖在签名文字屏幕矩形上，回车/失焦提交、ESC 取消。
 *
 *  balloonwj@qq.com   2026-06-03
 */
#include "stdafx.h"
#include "SignatureEditPopup.h"

#include <memory>

#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "SignatureEditModel.h"   // kSignatureMaxLen
#include "../../Common/Log/IULog.h"

namespace {

// 内部编辑框控件 id（弹层内唯一即可）。
const UINT kSignEditCtrlId = 1;

// 编辑框最小宽 / 高（签名很短或为空时也给一个好编辑的尺寸）。
const int  kMinEditWidth  = 160;
const int  kMinEditHeight = 24;

// 编辑框相对签名矩形左右各留的内边距，避免文字贴边。
const int  kEditPadX      = 6;

} // anonymous namespace

CSignatureEditPopup::CSignatureEditPopup()
    : m_edit(NULL)
    , m_built(false)
    , m_handled(false)
    , m_commitCb(NULL)
    , m_commitUd(NULL)
{
}

CSignatureEditPopup::~CSignatureEditPopup()
{
}

void CSignatureEditPopup::SetCommitCallback(CommitCallback cb, void* userdata)
{
    m_commitCb = cb;
    m_commitUd = userdata;
}

void CSignatureEditPopup::BuildContentOnce()
{
    if (m_built)
    {
        return;
    }

    // 单行编辑框作内容根：白底、带 1px 边框（编辑态有个框）、单行垂直居中、限长。
    std::unique_ptr<balloonwjui::DuiEdit> edit(new balloonwjui::DuiEdit());
    edit->SetCtrlId(kSignEditCtrlId);
    edit->SetMaxLength(balloonmain::kSignatureMaxLen);
    edit->SetVerticalCenter(true);
    edit->SetShowBorder(true);
    m_edit = edit.get();   // 借用；下面 SetContent 转移所有权给内容树

    SetContent(std::move(edit));

    // 失焦关闭→提交；ESC / 程序化关闭→不提交。统一在 dismiss 回调里处理。
    SetDismissCallback(&CSignatureEditPopup::OnDismissThunk, this);

    m_built = true;
}

void CSignatureEditPopup::OpenForSignature(const RECT& signRectScreen, HWND /*owner*/,
                                           LPCTSTR currentSign)
{
    BuildContentOnce();

    int signW = signRectScreen.right - signRectScreen.left;
    int signH = signRectScreen.bottom - signRectScreen.top;

    int w = signW + 2 * kEditPadX;
    if (w < kMinEditWidth)
    {
        w = kMinEditWidth;
    }
    int h = signH;
    if (h < kMinEditHeight)
    {
        h = kMinEditHeight;
    }
    SetSize(w, h);

    // 让弹层左上角正好落在签名矩形左上角：EdgeBelow 取 (anchor.left, anchor.bottom)，
    // 故传一个"零高、bottom=签名顶"的 anchor（见 DuiPopupHost::ComputePositionScreen）。
    RECT anchor;
    anchor.left   = signRectScreen.left - kEditPadX;
    anchor.right  = signRectScreen.right;
    anchor.top    = signRectScreen.top;
    anchor.bottom = signRectScreen.top;
    SetEdge(balloonwjui::DuiPopupHost::EdgeBelow);

    m_handled = false;

    // owner 传 NULL 而不是调用方给的那个句柄：DuiHost::SendNotify 以
    // GetParent(m_hWnd) 为通知目标，而 WS_POPUP 窗口的 GetParent 返回的正是
    // owner —— 传了 owner，编辑框的回车通知就会发到主面板而不是本弹层。传 NULL
    // 让 SendNotify 走"自路由"分支把通知送回本弹层的 OnChildNotify。与
    // EmojiPickerPopup / FontSettingPopup 同一口径。
    Show(anchor, NULL);

    if (m_edit != NULL)
    {
        m_edit->SetText(currentSign != NULL ? currentSign : _T(""));
        // 无窗口编辑框取焦点即走控件自己的 SetFocus：它会把 DUI 焦点指到本控件，
        // 并让弹层窗口把 Win32 键盘焦点拿到手（无窗口控件的按键消息要先到宿主）。
        m_edit->SetFocus();
        m_edit->SelectAll();   // 全选，便于直接覆写
    }

    LOG_INFO(_T("签名内联编辑已打开。"));
}

LRESULT CSignatureEditPopup::OnChildNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                           LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify =
        reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL)
    {
        bHandled = FALSE;
        return 0;
    }

    // 通知码与控件编号必须一起判：balloonui 的自定义通知码按控件各自编号，
    // 不同控件的同序号自定义码数值相同，只比通知码会把别的控件的通知截住。
    if (pNotify->code == (UINT)balloonwjui::DuiEdit::DUIN_EDIT_ENTER
        && pNotify->ctrlId == kSignEditCtrlId)
    {
        OnEditEnter();
        return 0;
    }

    // ESC 一般走不到这里 —— DuiPopupHost 在自己的按键处理里就把 ESC 拦下关闭了，
    // 按键到不了编辑框。留着这一支是为了不依赖那条时序：万一将来基类不再拦，
    // 本弹层仍按"取消"处理。
    if (pNotify->code == (UINT)balloonwjui::DuiEdit::DUIN_EDIT_ESCAPE
        && pNotify->ctrlId == kSignEditCtrlId)
    {
        OnEditEscape();
        return 0;
    }

    bHandled = FALSE;
    return 0;
}

void CSignatureEditPopup::OnEditEnter()
{
    // 回车：提交后程序化关闭（关闭原因 Programmatic，不会再触发提交）。
    DoCommit();
    Hide(balloonwjui::DuiPopupHost::ReasonProgrammatic);
}

void CSignatureEditPopup::OnEditEscape()
{
    // ESC：取消。先标记已处理，避免随后失焦关闭又走提交。
    m_handled = true;
    Hide(balloonwjui::DuiPopupHost::ReasonEscape);
}

void CSignatureEditPopup::OnDismissThunk(void* userdata, balloonwjui::DuiPopupHost::Reason r)
{
    CSignatureEditPopup* self = (CSignatureEditPopup*)userdata;
    if (self != NULL)
    {
        self->OnDismiss(r);
    }
}

void CSignatureEditPopup::OnDismiss(balloonwjui::DuiPopupHost::Reason r)
{
    // ESC 关闭：标记本次会话已处理，后续任何路径都不再提交。基类 DuiPopupHost
    // 自己就会在按键处理里拦下 ESC 并以该原因关闭，不经过本弹层的 OnEditEscape，
    // 故这个标记要在这里补上。
    if (r == balloonwjui::DuiPopupHost::ReasonEscape)
    {
        m_handled = true;
        return;
    }

    // 仅"失焦关闭"视为提交；程序化关闭不提交（回车路径已在 OnEditEnter 提交）。
    if (r == balloonwjui::DuiPopupHost::ReasonLostFocus)
    {
        DoCommit();
    }
}

void CSignatureEditPopup::DoCommit()
{
    if (m_handled)
    {
        return;   // 幂等：本次会话已提交/取消过
    }
    m_handled = true;

    CString text = (m_edit != NULL) ? m_edit->GetText() : CString();
    if (m_commitCb != NULL)
    {
        m_commitCb(m_commitUd, text);
    }
}
