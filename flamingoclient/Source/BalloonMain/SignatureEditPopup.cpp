/**
 *  SignatureEditPopup.cpp
 *
 *  个性签名内联编辑弹层的实现。子类化 DuiPopupHost，内部一个单行 DuiEditHost；
 *  打开时精确盖在签名文字屏幕矩形上，回车/失焦提交、ESC 取消。
 *
 *  balloonwj@qq.com   2026-06-03
 */
#include "stdafx.h"
#include "SignatureEditPopup.h"

#include <commctrl.h>   // SetWindowSubclass（给输入框 EDIT 挂 回车/ESC 拦截）
#include <memory>

#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEditHost.h"
#include "SignatureEditModel.h"   // kSignatureMaxLen
#include "../IULog.h"

namespace {

// 内部编辑框控件 id（弹层内唯一即可）。
const UINT kSignEditCtrlId = 1;

// 编辑框最小宽 / 高（签名很短或为空时也给一个好编辑的尺寸）。
const int  kMinEditWidth  = 160;
const int  kMinEditHeight = 24;

// 编辑框相对签名矩形左右各留的内边距，避免文字贴边。
const int  kEditPadX      = 6;

// 内部 EDIT 的子类化过程：拦截 回车=提交、ESC=取消。单行 EDIT 默认会吞掉这两个
// 键（且回车会"叮"一声），使 DuiPopupHost 自带的 ESC 关闭也收不到，故在此补上。
// ref 为 CSignatureEditPopup*。
LRESULT CALLBACK SignEditSubclassProc(HWND hWnd, UINT msg, WPARAM wParam,
                                      LPARAM lParam, UINT_PTR idSubclass,
                                      DWORD_PTR ref)
{
    if (msg == WM_KEYDOWN && wParam == VK_RETURN)
    {
        CSignatureEditPopup* self = (CSignatureEditPopup*)ref;
        if (self != NULL)
            self->OnEditEnter();
        return 0;   // 吞掉，避免单行 EDIT 的提示音
    }
    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE)
    {
        CSignatureEditPopup* self = (CSignatureEditPopup*)ref;
        if (self != NULL)
            self->OnEditEscape();
        return 0;
    }
    if (msg == WM_NCDESTROY)
    {
        ::RemoveWindowSubclass(hWnd, SignEditSubclassProc, idSubclass);
    }
    return ::DefSubclassProc(hWnd, msg, wParam, lParam);
}

} // anonymous namespace

CSignatureEditPopup::CSignatureEditPopup()
    : m_edit(NULL)
    , m_built(false)
    , m_editCreated(false)
    , m_keyHooked(false)
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
    std::unique_ptr<balloonwjui::DuiEditHost> edit(new balloonwjui::DuiEditHost());
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

void CSignatureEditPopup::OpenForSignature(const RECT& signRectScreen, HWND owner,
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
    Show(anchor, owner);

    // 内部真 Win32 EDIT 须在弹层 HWND 就绪后建一次。
    if (!m_editCreated && m_edit != NULL)
    {
        if (!m_edit->EnsureCreated(m_hWnd))
        {
            LOG_WARNING(_T("签名编辑框 EnsureCreated 失败。"));
        }
        m_editCreated = true;
    }

    // 给 EDIT 挂 回车/ESC 拦截（一次）。
    if (!m_keyHooked && m_edit != NULL)
    {
        HWND hEdit = m_edit->GetHostedHwnd();
        if (hEdit != NULL)
        {
            ::SetWindowSubclass(hEdit, SignEditSubclassProc, 1, (DWORD_PTR)this);
            m_keyHooked = true;
        }
    }

    if (m_edit != NULL)
    {
        m_edit->SetText(currentSign != NULL ? currentSign : _T(""));
        SetDuiFocus(m_edit);
        HWND hEdit = m_edit->GetHostedHwnd();
        if (hEdit != NULL)
        {
            ::SetFocus(hEdit);
            ::SendMessage(hEdit, EM_SETSEL, 0, -1);   // 全选，便于直接覆写
        }
    }

    LOG_INFO(_T("签名内联编辑已打开。"));
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
    // 仅"失焦关闭"视为提交；ESC / 程序化关闭不提交（回车路径已在 OnEditEnter 提交）。
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
