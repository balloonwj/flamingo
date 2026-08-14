/**
 *  TagEditPopup.cpp
 *
 *  标签输入弹层的实现。子类化 DuiPopupHost，内部一个单行 DuiEditHost；打开时
 *  盖在目标矩形上、带入当前文本并全选，回车 / 失焦提交、ESC 取消。
 *
 *  balloonwj@qq.com   2026-08-14
 */

#include "stdafx.h"
#include "TagEditPopup.h"

#include <commctrl.h>   // SetWindowSubclass（给输入框 EDIT 挂回车 / ESC 拦截）
#include <memory>

#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEditHost.h"
#include "ContactTagModel.h"   // kContactTagMaxLen
#include "../IULog.h"

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

// 子类化过程的 id。同一个 HWND 上多份子类化靠它区分；本进程内只在这里用一次。
const UINT_PTR kTagEditSubclassId = 1;

/**
 *  内部 EDIT 的子类化过程：拦截回车（提交）与 ESC（取消）。
 *
 *  单行 EDIT 默认会吞掉这两个键（且按回车会发出系统提示音），连 DuiPopupHost
 *  自带的 ESC 关闭都收不到，故必须在这里补上。
 *    ref：注册时传入的 CTagEditPopup*。
 */
LRESULT CALLBACK TagEditSubclassProc(HWND hWnd, UINT msg, WPARAM wParam,
                                     LPARAM lParam, UINT_PTR idSubclass,
                                     DWORD_PTR ref)
{
    if (msg == WM_KEYDOWN && wParam == VK_RETURN)
    {
        CTagEditPopup* self = (CTagEditPopup*)ref;
        if (self != NULL)
        {
            self->OnEditEnter();
        }
        return 0;   // 吞掉，避免单行 EDIT 的系统提示音
    }
    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE)
    {
        CTagEditPopup* self = (CTagEditPopup*)ref;
        if (self != NULL)
        {
            self->OnEditEscape();
        }
        return 0;
    }
    if (msg == WM_NCDESTROY)
    {
        ::RemoveWindowSubclass(hWnd, TagEditSubclassProc, idSubclass);
    }
    return ::DefSubclassProc(hWnd, msg, wParam, lParam);
}

} // anonymous namespace

CTagEditPopup::CTagEditPopup()
    : m_edit(NULL)
    , m_built(false)
    , m_editCreated(false)
    , m_keyHooked(false)
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
    std::unique_ptr<balloonwjui::DuiEditHost> edit(new balloonwjui::DuiEditHost());
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

void CTagEditPopup::OpenAt(const RECT& anchorScreen, HWND owner, LPCTSTR currentText)
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
    Show(anchor, owner);

    // 内部真 Win32 EDIT 须在弹层 HWND 就绪之后创建一次。
    if (!m_editCreated && m_edit != NULL)
    {
        if (!m_edit->EnsureCreated(m_hWnd))
        {
            LOG_WARNING("contact tag popup: edit EnsureCreated failed.");
        }
        m_editCreated = true;
    }

    // 给 EDIT 挂回车 / ESC 拦截（只挂一次）。
    if (!m_keyHooked && m_edit != NULL)
    {
        HWND hEdit = m_edit->GetHostedHwnd();
        if (hEdit != NULL)
        {
            ::SetWindowSubclass(hEdit, TagEditSubclassProc, kTagEditSubclassId,
                                (DWORD_PTR)this);
            m_keyHooked = true;
        }
    }

    if (m_edit != NULL)
    {
        m_edit->SetText(currentText != NULL ? currentText : _T(""));
        SetDuiFocus(m_edit);
        HWND hEdit = m_edit->GetHostedHwnd();
        if (hEdit != NULL)
        {
            ::SetFocus(hEdit);
            ::SendMessage(hEdit, EM_SETSEL, 0, -1);   // 全选，便于直接覆盖输入
        }
    }

    LOG_INFO("contact tag popup: opened.");
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
    // 只有"失焦关闭"才视为提交；ESC 与程序化关闭都不提交（回车那条路径已经在
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
