#pragma once

#include <vector>
#include "SkinLib/SkinLib.h"

class CFileItemRequest;

enum FILE_TARGET_TYPE
{
	SEND_TYPE,		//发送方
	RECV_TYPE		//接收方
};

enum HITTEST_BTN_AREA
{
	BTN_NONE,
	BTN_CHANGE_TO_ONLINE,
	BTN_CHANGE_TO_OFFLINE,
	BTN_ACCEPT,
	BTN_SAVEAS,
	BTN_CANCEL
};

typedef struct tagFileTransferNMHDREx
{
	NMHDR				nmhdr;
	FILE_TARGET_TYPE	nTargetType;
	HITTEST_BTN_AREA	btnArea;
	long				nID;
}FILE_TRANSFER_NMHDREX,*PFILE_TRANSFER_NMHDREX;

class CFileTransferItem
{
public:
	CFileTransferItem();
	~CFileTransferItem();

public:
	long					m_nID;
	CString					m_strFileName;			//文件显示名称
	CStringA				m_strFileDownloadName;	//文件下载名
	CString					m_strFullName;			//文件全饰名
	CString					m_strFileSaveName;		//下载文件时的存储名
	CString					m_strFilePath;
	CString					m_strFileType;
	DWORD					m_nFileSize;
	BOOL					m_bOfflineSent;
	CImageEx*				m_pImgFileType;
	long					m_nProgressPercent;
	long					m_nVerificationPercent;	//校验文件进度
	FILE_TARGET_TYPE		m_nTargetType;			//到底是发送方还是接收方

	CFileItemRequest*		m_pFileItemRequest;		//与该项相匹配的文件传输队列项

	BOOL					m_bAcceptBtnVisible;
	BOOL					m_bSaveAsBtnVisible;
	BOOL					m_bCancelBtnVisible;

	//TODO:转离线发送和转在线发送两个按钮可见性
};

class CFileTransferCtrl : public CWindowImpl<CFileTransferCtrl, CWindow>
{
public:
	CFileTransferCtrl();
	~CFileTransferCtrl();

	DECLARE_WND_CLASS(_T("__FileTansfer__"))

	BEGIN_MSG_MAP_EX(CFileTransferCtrl)
		MSG_WM_CREATE(OnCreate)
		MSG_WM_ERASEBKGND(OnEraseBkgnd)
		MSG_WM_PAINT(OnPaint)
		//MSG_WM_LBUTTONDBLCLK(OnLButtonDblClk)
		//MSG_WM_LBUTTONDOWN(OnLButtonDown)
		MSG_WM_LBUTTONUP(OnLButtonUp)
		//MSG_WM_RBUTTONUP(OnRButtonUp)
		MSG_WM_MOUSEMOVE(OnMouseMove)
		//MSG_WM_MOUSELEAVE(OnMouseLeave)
		//MSG_WM_TIMER(OnTimer)
		//MSG_WM_VSCROLL(OnVScroll)
		//MSG_WM_MOUSEWHEEL(OnMouseWheel)
		//MSG_WM_KEYDOWN(OnKeyDown)
		//MSG_WM_SIZE(OnSize)
		//MSG_WM_GETDLGCODE(OnGetDlgCode)
		//MESSAGE_RANGE_HANDLER_EX(WM_MOUSEFIRST, WM_MOUSELAST, OnMouseMessage)
		MSG_WM_DESTROY(OnDestroy)
	END_MSG_MAP()

public:
	void SetTransparent(BOOL bTransparent, HDC hBgDC);

	long GetItemCount() const;
	
	long AddItem();					//返回值为ID号，该ID号唯一标识了该文件项
	BOOL RemoveItemByID(long nID);

	BOOL SetItemFileTypePicByID(long nID, LPCTSTR lpszFileName);
	

	BOOL SetItemProgressPercentByID(long nID, long nProgressPercent);
	BOOL SetItemVerificationPercentByID(long nID, long nVerificationPercent);
	BOOL SetItemFileSizeByID(long nID, long nFileSize);
	long GetItemFileSizeByID(long nID);
	
	long GetItemIDByFileName(PCTSTR pszFileName);
	long GetItemIDByFullName(PCTSTR pszFullName);
	long GetItemIDBySaveName(PCTSTR pszSaveName);

	BOOL SetItemDownloadNameByID(long nID, PCSTR pszDownloadName);
	PCSTR GetItemDownloadNameByID(long nID);
	
	BOOL SetItemFileNameByID(long nID, LPCTSTR lpszFileName);
	PCTSTR GetItemFileNameByID(long nID);
	PCTSTR GetItemFileNameByIndex(size_t nIndex);

	BOOL SetItemFileFullNameByID(long nID, LPCTSTR lpszFileName);
	PCTSTR GetItemFileFullNameByID(long nID);

	BOOL SetItemSaveNameByID(long nID, LPCTSTR lpszSaveName);
	
	CFileTransferItem* GetItemByID(long nID);
	CFileTransferItem* GetItemByIndex(size_t nIndex);

	BOOL SetItemTargetTypeByID(long nID, FILE_TARGET_TYPE nType);
	FILE_TARGET_TYPE GetItemTargetTypeByID(long nID);
	FILE_TARGET_TYPE GetItemTargetTypeByIndex(size_t nIndex);

	BOOL SetFileItemRequestByID(long nID, CFileItemRequest* pFileItemRequest);
	CFileItemRequest* GetFileItemRequestByID(long nID);
	CFileItemRequest* GetFileItemRequestByIndex(size_t nIndex);

	void SetAcceptButtonVisibleByID(long nID, BOOL bVisible);
	void SetSaveAsButtonVisibleByID(long nID, BOOL bVisible);
	void SetCancelButtonVisibleByID(long nID, BOOL bVisible);

private:
	int OnCreate(LPCREATESTRUCT lpCreateStruct);
	BOOL OnEraseBkgnd(CDCHandle dc);
	void OnPaint(CDCHandle dc);
	void OnLButtonDblClk(UINT nFlags, CPoint point);
	void OnLButtonDown(UINT nFlags, CPoint point);
	void OnLButtonUp(UINT nFlags, CPoint point);
	void OnRButtonUp(UINT nFlags, CPoint point);
	void OnMouseMove(UINT nFlags, CPoint point);
	void OnMouseLeave();
	void OnTimer(UINT_PTR nIDEvent);
	void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar pScrollBar);
	BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	void OnSize(UINT nType, CSize size);
	UINT OnGetDlgCode(LPMSG lpMsg);
	LRESULT OnMouseMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
	void OnDestroy();
	void HitTest(POINT pt, long& nID, HITTEST_BTN_AREA& nBtnArea);

	void DrawParentWndBg(HDC hDC);
	void DrawItem(HDC hDC, size_t nIndex);
	BOOL GetItemRectByIndex(size_t nIndex, CRect& rect);

private:
	//CSkinScrollBar						m_VScrollBar;
	
	BOOL								m_bTransparent;
	HDC									m_hBgDC;
	long								m_nID;
	
	std::vector<CFileTransferItem*>		m_arrFileTransferItems;

	long								m_nCurrentHoverID;

	BOOL								m_bHoverOnChangeMethodBtn;
	
	BOOL								m_bHoverOnSaveAsBtn;
	BOOL								m_bHoverOnAcceptBtn;
	BOOL								m_bHoverOnCancelBtn;
};