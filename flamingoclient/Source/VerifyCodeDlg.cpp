#include "stdafx.h"
#include "VerifyCodeDlg.h"

CVerifyCodeDlg::CVerifyCodeDlg(void)
{
}

CVerifyCodeDlg::~CVerifyCodeDlg(void)
{
}

void CVerifyCodeDlg::SetVerifyCodePic(const BYTE* lpData, DWORD dwSize)
{
	m_picVerifyCode.SetBitmap(lpData, dwSize);
	if (::IsWindowVisible(m_hWnd))
		m_picVerifyCode.Invalidate();
}

CString CVerifyCodeDlg::GetVerifyCode()
{
	return m_strVerifyCode;
}

BOOL CVerifyCodeDlg::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
	CenterWindow(GetParent());

	InitUI();

	return TRUE;
}

void CVerifyCodeDlg::OnClose()
{
	EndDialog(IDCANCEL);
}

void CVerifyCodeDlg::OnDestroy()
{
	SetMsgHandled(FALSE);

	UninitUI();
}

void CVerifyCodeDlg::OnBtn_Ok(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	CWindow ctrl;

	ctrl = GetDlgItem(ID_EDIT_VERIFY_CODE);
	ctrl.GetWindowText(m_strVerifyCode);
	m_strVerifyCode.Left(8);

	EndDialog(IDOK);
}

void CVerifyCodeDlg::OnBtn_Cancel(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	EndDialog(IDCANCEL);
}

BOOL CVerifyCodeDlg::InitUI()
{
	m_SkinDlg.SubclassWindow(m_hWnd);
	m_SkinDlg.SetBgPic(_T("LoginPanel_window_windowBkg.png"), CRect(4, 86, 4, 4));
	m_SkinDlg.SetCloseSysBtnPic(_T("SysBtn\\btn_close_normal.png"), 
		_T("SysBtn\\btn_close_highlight.png"), _T("SysBtn\\btn_close_down.png"));
	m_SkinDlg.SetTitleText(_T("«Î ‰»Î—È÷§¬Î"));

	HDC hDlgBgDC = m_SkinDlg.GetBgDC();

	m_picVerifyCode.SubclassWindow(GetDlgItem(ID_PIC_VERIFY_CODE));
	m_picVerifyCode.MoveWindow(25, 32, 130, 53, FALSE);
	m_picVerifyCode.SetTransparent(TRUE, hDlgBgDC);

	m_edtVerifyCode.SubclassWindow(GetDlgItem(ID_EDIT_VERIFY_CODE));
	m_edtVerifyCode.MoveWindow(28, 90, 220, 20, FALSE);
	m_edtVerifyCode.SetTransparent(TRUE, hDlgBgDC);
	m_edtVerifyCode.SetBgNormalPic(_T("frameBorderEffect_normalDraw.png"), CRect(2,2,2,2));
	m_edtVerifyCode.SetBgHotPic(_T("frameBorderEffect_mouseDownDraw.png"), CRect(2,2,2,2));

	m_btnOk.SubclassWindow(GetDlgItem(ID_BTN_OK));
	m_btnOk.MoveWindow(28, 123, 69, 22, FALSE);
	m_btnOk.SetTransparent(TRUE, hDlgBgDC);
	m_btnOk.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnOk.SetRound(4, 4);
	m_btnOk.SetBgPic(_T("Button\\btn_normal.png"), _T("Button\\btn_highlight.png"),
		_T("Button\\btn_down.png"), _T("Button\\btn_focus.png"));

	m_btnCancel.SubclassWindow(GetDlgItem(ID_BTN_CANCEL));
	m_btnCancel.MoveWindow(103, 123, 69, 22, FALSE);
	m_btnCancel.SetTransparent(TRUE, hDlgBgDC);
	m_btnCancel.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnCancel.SetRound(4, 4);
	m_btnCancel.SetBgPic(_T("Button\\btn_normal.png"), _T("Button\\btn_highlight.png"),
		_T("Button\\btn_down.png"), _T("Button\\btn_focus.png"));

	return TRUE;
}

void CVerifyCodeDlg::UninitUI()
{
	if (m_picVerifyCode.IsWindow())
		m_picVerifyCode.DestroyWindow();

	if (m_edtVerifyCode.IsWindow())
		m_edtVerifyCode.DestroyWindow();

	if (m_btnOk.IsWindow())
		m_btnOk.DestroyWindow();

	if (m_btnCancel.IsWindow())
		m_btnCancel.DestroyWindow();
}