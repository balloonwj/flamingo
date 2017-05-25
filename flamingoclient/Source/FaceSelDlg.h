#pragma once

#include "resource.h"
#include "FaceCtrl.h"
#include "FaceList.h"

#define		ID_FACE_CTRL		1

class CFaceSelDlg : public CDialogImpl<CFaceSelDlg>
{
public:
	CFaceSelDlg(void);
	~CFaceSelDlg(void);

	enum { IDD = IDD_FACESELDLG };

	BEGIN_MSG_MAP_EX(CFaceSelDlg)
		MSG_WM_INITDIALOG(OnInitDialog)
		MSG_WM_ACTIVATE(OnActivate)
		MSG_WM_PAINT(OnPaint)
		MSG_WM_CLOSE(OnClose)
		MSG_WM_DESTROY(OnDestroy)
		MESSAGE_HANDLER_EX(FACE_CTRL_SEL, OnFaceCtrlSel)
	END_MSG_MAP()

public:
	void SetFaceList(CFaceList* lpFaceList);
	int GetSelFaceId();
	int GetSelFaceIndex();
	CString GetSelFaceFileName();

private:
	BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
	void OnActivate(UINT nState, BOOL bMinimized, CWindow wndOther);
	void OnPaint(CDCHandle dc);
	void OnClose();
	void OnDestroy();
	LRESULT OnFaceCtrlSel(UINT uMsg, WPARAM wParam, LPARAM lParam);

private:
	CFaceCtrl m_FaceCtrl;
	CFaceList* m_lpFaceList;
	int m_nSelFaceId;
	int m_nSelFaceIndex;
	CString m_strSelFaceFileName;
};
