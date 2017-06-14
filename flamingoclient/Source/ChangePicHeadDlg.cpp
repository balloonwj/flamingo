#include "stdafx.h"
#include "ChangePicHeadDlg.h"
#include "UserSessionData.h"


CChangePicHeadDlg::CChangePicHeadDlg()
{
	m_SelectedIndex = -1;
	m_LastSelectedIndex = -1;
}

CChangePicHeadDlg::~CChangePicHeadDlg()
{

}

void CChangePicHeadDlg::SetSelection(long nIndex)
{
	if(nIndex < 0)
		nIndex = 0;
	m_SelectedIndex = nIndex;
}


long CChangePicHeadDlg::GetSelection()
{
	return m_SelectedIndex;
}

BOOL CChangePicHeadDlg::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
	SetWindowPos(NULL, 0, 0, 587, 500, SWP_NOMOVE);
	
	Init();

	ModifyStyleEx(WS_EX_APPWINDOW, WS_EX_TOOLWINDOW);

	CenterWindow(::GetParent(m_hWnd));

	return TRUE;
}

BOOL CChangePicHeadDlg::Init()
{
	m_SkinDlg.SetBgPic(_T("LoginPanel_window_windowBkg.png"), CRect(4, 69, 4, 33));
	m_SkinDlg.SetMinSysBtnPic(_T("SysBtn\\btn_mini_normal.png"), 
		_T("SysBtn\\btn_mini_highlight.png"), _T("SysBtn\\btn_mini_down.png"));
	m_SkinDlg.SetCloseSysBtnPic(_T("SysBtn\\btn_close_normal.png"), 
		_T("SysBtn\\btn_close_highlight.png"), _T("SysBtn\\btn_close_down.png"));
	m_SkinDlg.SubclassWindow(m_hWnd);
	m_SkinDlg.SetTitleText(_T("头像修改"));

	HDC hDlgBgDC = m_SkinDlg.GetBgDC();

	m_btnOK.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnOK.SetTransparent(TRUE, hDlgBgDC);
	m_btnOK.SetBgPic(_T("Button\\btn_normal.png"), _T("Button\\btn_highlight.png"),
		_T("Button\\btn_down.png"), _T("Button\\btn_focus.png"));
	m_btnOK.SubclassWindow(GetDlgItem(IDOK));
	m_btnOK.MoveWindow(470, 360, 80, 28, FALSE);

	m_btnCancel.SetButtonType(SKIN_PUSH_BUTTON);
	m_btnCancel.SetTransparent(TRUE, hDlgBgDC);
	m_btnCancel.SetBgPic(_T("Button\\btn_normal.png"), _T("Button\\btn_highlight.png"),
		_T("Button\\btn_down.png"), _T("Button\\btn_focus.png"));
	m_btnCancel.SubclassWindow(GetDlgItem(IDCANCEL));
	m_btnCancel.MoveWindow(470, 420, 80, 28, FALSE);

	//初始化头像窗口
	InitThumbImages();

	return 1;
}

BOOL CChangePicHeadDlg::InitThumbImages()
{
	//TODO: 后期改成自动判断\Image\UserThumbs\文件夹里面头像文件的个数
	const long THUMB_NUM = 36;

	CString strThumbPath;
	HDC hDlgBgDC = m_SkinDlg.GetBgDC();

	CString strThumbDesc;

	CSkinPictureBox* pSkinPictureBox = NULL;
	m_aryThumbWindows.resize(THUMB_NUM);
	RECT rtThumb = {0};
	long cxThumb = 54;
	long cyThumb = 54;
	for(size_t i=0; i<6; ++i)
	{
		for(size_t j=0; j<6; ++j)
		{
			strThumbPath.Format(_T("%sImage\\UserThumbs\\%d.png"), g_szHomePath, j+i*6);
			rtThumb.left = 20 + 74*j;
			rtThumb.right = rtThumb.left+cxThumb;
			rtThumb.top = 40 + 74*i;
			rtThumb.bottom = rtThumb.top+cyThumb;
		
			pSkinPictureBox = new CSkinPictureBox();
			pSkinPictureBox->SetBitmap(strThumbPath);
			pSkinPictureBox->SetTransparent(TRUE, hDlgBgDC);
			pSkinPictureBox->SetShowCursor(TRUE);
			//注意：skinLib的SkinPictureBox用CreateWindow创建出来必须加上SS_NOTIFY才能响应鼠标事件
			pSkinPictureBox->Create(m_hWnd, rtThumb, NULL, WS_CHILD|WS_VISIBLE|SS_NOTIFY, NULL, THUMB_WINDOW_BASE_ID+j+i*6, NULL);
			pSkinPictureBox->ShowWindow(SW_SHOW);
			pSkinPictureBox->SetBgPic(_T("HeadCtrl\\Padding4Normal.png"), _T("HeadCtrl\\Padding4Hot.png"), _T("HeadCtrl\\Padding4Hot.png"));
			strThumbDesc.Format(_T("头像%d"), 1+j+i*6);
			pSkinPictureBox->SetToolTipText(strThumbDesc);
			m_aryThumbWindows[j+i*6] = pSkinPictureBox;
		}
	}

	m_aryThumbWindows[m_SelectedIndex]->SetSelectedStatus(TRUE);
	m_LastSelectedIndex = m_SelectedIndex;
	
	return TRUE;
}

void CChangePicHeadDlg::DestroyThumbImages()
{
	CSkinPictureBox* pSkinPictureBox = NULL;
	for(size_t i=0; i<m_aryThumbWindows.size(); ++i)
	{
		pSkinPictureBox = m_aryThumbWindows[i];
		if(pSkinPictureBox == NULL)
			continue;

		pSkinPictureBox->DestroyWindow();
		delete pSkinPictureBox;
	}
}

void CChangePicHeadDlg::OnOK(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	EndDialog(IDOK);
}

void CChangePicHeadDlg::OnCancel(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	EndDialog(IDCANCEL);
}

void CChangePicHeadDlg::OnClose()
{
	EndDialog(IDCANCEL);
}

void CChangePicHeadDlg::OnSelectThumb(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	long nIndex = nID - THUMB_WINDOW_BASE_ID;
	if(nIndex<0 || nIndex>=(long)m_aryThumbWindows.size())
		return;
	
	//先取消前一个选中项
	if(m_LastSelectedIndex>=0)
		m_aryThumbWindows[m_LastSelectedIndex]->SetSelectedStatus(FALSE);
	//再选中当前项
	m_aryThumbWindows[nIndex]->SetSelectedStatus(TRUE);
	m_SelectedIndex = nIndex;
	m_LastSelectedIndex = nIndex;
}

void CChangePicHeadDlg::OnDestroy()
{
	SetMsgHandled(FALSE);

	DestroyThumbImages();
}