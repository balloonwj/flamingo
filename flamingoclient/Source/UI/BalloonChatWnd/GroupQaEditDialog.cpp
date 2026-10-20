/**
 *  GroupQaEditDialog.cpp —— 「新建问答」「修改问答」对话框，说明见 GroupQaEditDialog.h。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "GroupQaEditDialog.h"

#include "GroupQaEditLayout.h"                // BuildQaEditBody 与尺寸常量
#include "GroupQaModel.h"                     // CheckQaInput / QaInputCheckText / TrimQaText
#include "../../Common/Log/IULog.h"
#include "../../Common/Util/EncodeUtil.h"     // EncodeUtil::UnicodeToUtf8
#include "../../Common/I18n/I18n.h"           // TR：界面文字的多语言查找
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"

using namespace balloonwjui;

namespace balloonchatwnd {

CGroupQaEditDialog::CGroupQaEditDialog()
    : m_ownerHwnd(NULL)
    , m_qaId(0)
    , m_resultMsg(0)
{
}

CGroupQaEditDialog::~CGroupQaEditDialog()
{
    CloseDialog();
}

bool CGroupQaEditDialog::Open(HWND owner, int64_t qaId, const std::wstring& question, const std::wstring& answer,
                              UINT resultMsg)
{
    m_ownerHwnd = owner;
    m_qaId = qaId;
    m_resultMsg = resultMsg;
    const LPCTSTR title = (qaId > 0) ? TR(L"修改问答") : TR(L"新建问答");

    if (!IsWindow())
    {
        //以宿主为 owner：本窗口恒在它之上。控件通知发回本窗口（开发约定文档 UI 约定第 11 条：只有 WS_CHILD
        //宿主的通知发给父窗口）
        if (Create(owner, CWindow::rcDefault, title, WS_OVERLAPPEDWINDOW, 0) == NULL)
        {
            LOG_ERROR("group qa edit dialog: Create failed.");
            return false;
        }
        SetTitleBarHeight(kQaEditTitleBarH);
        SetIcon(m_programIcons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
        SetButtons(false, false, true);
        SetResizable(false);
        SetClientContent(BuildQaEditBody());
        SetMinSize(kQaEditWindowWidth, kQaEditWindowHeight);
        ResizeClient(kQaEditWindowWidth, kQaEditWindowHeight);
        CenterWindow(owner);
    }
    SetTitle(title);
    SetWindowText(title);

    DuiControl* root = GetClientContent();
    DuiEdit* q = (root != NULL) ? dynamic_cast<DuiEdit*>(root->FindCtrlById(kQaEditIdQuestion)) : NULL;
    DuiEdit* a = (root != NULL) ? dynamic_cast<DuiEdit*>(root->FindCtrlById(kQaEditIdAnswer)) : NULL;
    if (q != NULL)
    {
        q->SetText(question.c_str());
    }
    if (a != NULL)
    {
        a->SetText(answer.c_str());
    }
    ShowError(std::wstring());

    //打开期间禁用宿主：避免在宿主里再点一次「修改」又打开一份
    const bool ownerIsWindow = (owner != NULL) && (::IsWindow(owner) != FALSE);
    const bool ownerIsEnabled = ownerIsWindow && (::IsWindowEnabled(owner) != FALSE);
    if (m_ownerGuard.OnOpen(ownerIsWindow, ownerIsEnabled))
    {
        ::EnableWindow(owner, FALSE);
    }
    ShowWindow(SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
    if (q != NULL)
    {
        SetDuiFocus(q);
    }
    LOG_INFO("group qa edit dialog: opened, qa id=%lld.", (long long)qaId);
    return true;
}

void CGroupQaEditDialog::CloseDialog()
{
    const bool ownerIsWindow = (m_ownerHwnd != NULL) && (::IsWindow(m_ownerHwnd) != FALSE);
    if (m_ownerGuard.OnClose(ownerIsWindow))
    {
        ::EnableWindow(m_ownerHwnd, TRUE);
    }
    if (IsWindow())
    {
        DestroyWindow();
    }
}

void CGroupQaEditDialog::ShowError(const std::wstring& text)
{
    DuiControl* root = GetClientContent();
    DuiLabel* err = (root != NULL) ? dynamic_cast<DuiLabel*>(root->FindCtrlById(kQaEditIdError)) : NULL;
    if (err != NULL)
    {
        err->SetText(text.c_str());
    }
}

void CGroupQaEditDialog::OnSave()
{
    DuiControl* root = GetClientContent();
    DuiEdit* q = (root != NULL) ? dynamic_cast<DuiEdit*>(root->FindCtrlById(kQaEditIdQuestion)) : NULL;
    DuiEdit* a = (root != NULL) ? dynamic_cast<DuiEdit*>(root->FindCtrlById(kQaEditIdAnswer)) : NULL;
    if (q == NULL || a == NULL)
    {
        return;
    }
    const std::wstring question = TrimQaText(std::wstring((LPCWSTR)q->GetText()));
    const std::wstring answer = TrimQaText(std::wstring((LPCWSTR)a->GetText()));
    const int check = CheckQaInput(question, answer);
    if (check != kQaInputOk)
    {
        ShowError(QaInputCheckText(check));
        return;
    }
    QaEditResult* result = new QaEditResult();
    result->m_id = m_qaId;
    result->m_questionUtf8 = EncodeUtil::UnicodeToUtf8(question);
    result->m_answerUtf8 = EncodeUtil::UnicodeToUtf8(answer);
    if (m_ownerHwnd == NULL || !::PostMessage(m_ownerHwnd, m_resultMsg, 0, (LPARAM)result))
    {
        delete result;
        LOG_ERROR("group qa edit dialog: post result to owner failed.");
        return;
    }
    LOG_INFO("group qa edit dialog: saved, qa id=%lld.", (long long)m_qaId);
    CloseDialog();
}

LRESULT CGroupQaEditDialog::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    CloseDialog();
    return 0;
}

LRESULT CGroupQaEditDialog::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
{
    const DuiNotify* pNotify = reinterpret_cast<const DuiNotify*>(lParam);
    if (pNotify == NULL || pNotify->code != DUIN_CLICK)
    {
        bHandled = FALSE;
        return 0;
    }
    switch (pNotify->ctrlId)
    {
    //「保存」：校验后交回宿主
    case kQaEditIdSave:
        OnSave();
        return 0;

    //「取消」：直接关闭
    case kQaEditIdCancel:
        CloseDialog();
        return 0;

    //其它控件的点击：交给基类
    default:
        break;
    }
    bHandled = FALSE;
    return 0;
}

} // namespace balloonchatwnd
