#pragma once

#include "resource.h"
#include "SkinLib/SkinLib.h"

#define THUMB_WINDOW_BASE_ID 0x4000

class CChangePicHeadDlg : public CDialogImpl<CChangePicHeadDlg>
{
public:
	CChangePicHeadDlg();
	virtual ~CChangePicHeadDlg();

	enum { IDD = IDD_CHANGEPICDLG };

	BEGIN_MSG_MAP_EX(CChangePicHeadDlg)
		MSG_WM_INITDIALOG(OnInitDialog)
		MSG_WM_CLOSE(OnClose)
		MSG_WM_DESTROY(OnDestroy)

		COMMAND_HANDLER_EX(IDOK, BN_CLICKED, OnOK)
		COMMAND_HANDLER_EX(IDCANCEL, BN_CLICKED, OnCancel)
		COMMAND_RANGE_HANDLER_EX(THUMB_WINDOW_BASE_ID, THUMB_WINDOW_BASE_ID+35, OnSelectThumb);
	END_MSG_MAP()

	long GetSelection();
	void SetSelection(long nIndex);

private:
	BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
	void OnClose();
	void OnDestroy();

	BOOL Init();
	BOOL InitThumbImages();
	void DestroyThumbImages();

	void OnOK(UINT uNotifyCode, int nID, CWindow wndCtl);
	void OnCancel(UINT uNotifyCode, int nID, CWindow wndCtl);
	void OnSelectThumb(UINT uNotifyCode, int nID, CWindow wndCtl);

private:
	CSkinDialog							m_SkinDlg;	
	CSkinButton							m_btnOK;
	CSkinButton							m_btnCancel;

	CSkinPictureBox						m_ThumbBox;

	std::vector<CSkinPictureBox*>		m_aryThumbWindows;	//供选择的头像列表

	long								m_SelectedIndex;	//选中的头像索引
	long								m_LastSelectedIndex;//上一次选中的头像索引
};