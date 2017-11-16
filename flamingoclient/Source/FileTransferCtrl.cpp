#include "stdafx.h"
#include "FileTransferCtrl.h"
#include "GDIFactory.h"
#include "net/IUProtocolData.h"


CFileTransferItem::CFileTransferItem()
{
	m_nID = 0;
	m_nFileSize = 0;
	m_bOfflineSent = TRUE;
	m_pImgFileType = NULL;
	m_nProgressPercent = 0;
	m_nVerificationPercent = 0;
	m_nTargetType = SEND_TYPE;

	m_pFileItemRequest = NULL;

	m_bAcceptBtnVisible = TRUE;
	m_bSaveAsBtnVisible = TRUE;
	m_bCancelBtnVisible = TRUE;
}

CFileTransferItem::~CFileTransferItem()
{
	if(m_pImgFileType != NULL)
	{
		delete m_pImgFileType;
		m_pImgFileType = NULL;
	}
}


CFileTransferCtrl::CFileTransferCtrl()
{
	m_bTransparent = FALSE;
	m_hBgDC = NULL;
	m_nID = 0;

	m_nCurrentHoverID = -1;

	m_bHoverOnChangeMethodBtn = FALSE;

	m_bHoverOnSaveAsBtn = FALSE;
	m_bHoverOnAcceptBtn = FALSE;
	m_bHoverOnCancelBtn = FALSE;
}

CFileTransferCtrl::~CFileTransferCtrl()
{

}

long CFileTransferCtrl::AddItem()
{
	CFileTransferItem* pItem = new CFileTransferItem;
	if (NULL == pItem)
		return -1;

	pItem->m_nID = m_nID;
	++m_nID;
	m_arrFileTransferItems.push_back(pItem);
	return pItem->m_nID;
}

BOOL CFileTransferCtrl::RemoveItemByID(long nID)
{
	if(m_arrFileTransferItems.empty())
		return FALSE;
	
	CFileTransferItem* pItem = NULL;
	std::vector<CFileTransferItem*>::iterator iter = m_arrFileTransferItems.begin();
	for(; iter!=m_arrFileTransferItems.end(); ++iter)
	{
		if((*iter)->m_nID == nID)
		{
			pItem =*iter;
			if(pItem != NULL)
				delete pItem;
			m_arrFileTransferItems.erase(iter);
			Invalidate(FALSE);
			return TRUE;
		}
	}
	
	return FALSE;
}

void CFileTransferCtrl::SetTransparent(BOOL bTransparent, HDC hBgDC)
{
	m_bTransparent = bTransparent;
	m_hBgDC = hBgDC;
}

long CFileTransferCtrl::GetItemCount() const
{
	return (long)m_arrFileTransferItems.size();
}

BOOL CFileTransferCtrl::SetItemFileTypePicByID(long nID, LPCTSTR lpszFileName)
{
	CFileTransferItem* pItem = GetItemByID(nID);
	if (NULL == pItem)
		return FALSE;

	if (pItem->m_pImgFileType != NULL)
		delete pItem->m_pImgFileType;

	pItem->m_pImgFileType = new CImageEx;
	if (NULL == pItem->m_pImgFileType)
		return FALSE;

	BOOL bRet = pItem->m_pImgFileType->LoadFromFile(lpszFileName);
	if (!bRet)
	{
		delete pItem->m_pImgFileType;
		pItem->m_pImgFileType = NULL;
		return FALSE;
	}

	if (IsWindowVisible())
		Invalidate(FALSE);

	return TRUE;
}

CFileTransferItem* CFileTransferCtrl::GetItemByID(long nID)
{
	CFileTransferItem* pItem = NULL;
	std::vector<CFileTransferItem*>::iterator iter = m_arrFileTransferItems.begin();
	for(; iter!=m_arrFileTransferItems.end(); ++iter)
	{
		if((*iter)->m_nID == nID)
		{
			pItem = (*iter);
			return pItem;
		}
	}

	return NULL;
}

CFileTransferItem* CFileTransferCtrl::GetItemByIndex(size_t nIndex)
{
	if (nIndex>=0 && nIndex<m_arrFileTransferItems.size())
		return m_arrFileTransferItems[nIndex];
	
	return NULL;
}

BOOL CFileTransferCtrl::SetItemTargetTypeByID(long nID, FILE_TARGET_TYPE nType)
{
	CFileTransferItem* pItem = GetItemByID(nID);
	if (NULL == pItem)
		return FALSE;

	pItem->m_nTargetType = nType;
	return TRUE;
}

FILE_TARGET_TYPE CFileTransferCtrl::GetItemTargetTypeByID(long nID)
{
	CFileTransferItem* pItem = GetItemByID(nID);
	if (NULL == pItem)
		return (FILE_TARGET_TYPE)-1;

	return pItem->m_nTargetType;
}

FILE_TARGET_TYPE CFileTransferCtrl::GetItemTargetTypeByIndex(size_t nIndex)
{
	CFileTransferItem* pItem = GetItemByIndex(nIndex);
	if (NULL == pItem)
		return (FILE_TARGET_TYPE)-1;

	return pItem->m_nTargetType;
}

BOOL CFileTransferCtrl::SetFileItemRequestByID(long nID, CFileItemRequest* pFileItemRequest)
{
	CFileTransferItem* pItem = GetItemByID(nID);
	if (NULL == pItem)
		return FALSE;

	pItem->m_pFileItemRequest = pFileItemRequest;
	return TRUE;
}

CFileItemRequest* CFileTransferCtrl::GetFileItemRequestByID(long nID)
{
	CFileTransferItem* pItem = GetItemByID(nID);
	if (NULL == pItem)
		return NULL;

	return pItem->m_pFileItemRequest;
}

CFileItemRequest* CFileTransferCtrl::GetFileItemRequestByIndex(size_t nIndex)
{
	CFileTransferItem* pItem = GetItemByIndex(nIndex);
	if (NULL == pItem)
		return NULL;

	return pItem->m_pFileItemRequest;
}


void CFileTransferCtrl::SetAcceptButtonVisibleByID(long nID, BOOL bVisible)
{
	CFileTransferItem* pItem = GetItemByID(nID);
	if (NULL == pItem)
		return;

	pItem->m_bAcceptBtnVisible = bVisible;
}

void CFileTransferCtrl::SetSaveAsButtonVisibleByID(long nID, BOOL bVisible)
{
	CFileTransferItem* pItem = GetItemByID(nID);
	if (NULL == pItem)
		return;

	pItem->m_bSaveAsBtnVisible = bVisible;
}

void CFileTransferCtrl::SetCancelButtonVisibleByID(long nID, BOOL bVisible)
{
	CFileTransferItem* pItem = GetItemByID(nID);
	if (NULL == pItem)
		return;

	pItem->m_bCancelBtnVisible = bVisible;
}

BOOL CFileTransferCtrl::SetItemFileNameByID(long nID, LPCTSTR lpszFileName)
{
	CFileTransferItem* pItem = GetItemByID(nID);
	if (NULL == pItem)
		return FALSE;

	pItem->m_strFileName = lpszFileName;
	return TRUE;
}

BOOL CFileTransferCtrl::SetItemProgressPercentByID(long nID, long nProgressPercent)
{
	CFileTransferItem* pItem = GetItemByID(nID);
	if (NULL == pItem)
		return FALSE;
	
	if(nProgressPercent > 100)
		nProgressPercent = 100;

	pItem->m_nProgressPercent = nProgressPercent;
	Invalidate(FALSE);

	return TRUE;
}

BOOL CFileTransferCtrl::SetItemVerificationPercentByID(long nID, long nVerificationPercent)
{
	CFileTransferItem* pItem = GetItemByID(nID);
	if (NULL == pItem)
		return FALSE;
	
	if(nVerificationPercent > 100)
		nVerificationPercent = 100;

	pItem->m_nVerificationPercent = nVerificationPercent;
	Invalidate(FALSE);

	return TRUE;
}

BOOL CFileTransferCtrl::SetItemFileSizeByID(long nID, long nFileSize)
{
	CFileTransferItem* pItem = GetItemByID(nID);
	if (NULL == pItem)
		return FALSE;
	if(nFileSize < 0)
		return FALSE;

	pItem->m_nFileSize = nFileSize;

	return TRUE;
}


long CFileTransferCtrl::GetItemFileSizeByID(long nID)
{
	CFileTransferItem* pItem = GetItemByID(nID);
	if (NULL == pItem)
		return 0;

	return pItem->m_nFileSize;
}

long CFileTransferCtrl::GetItemIDByFileName(PCTSTR pszFileName)
{
	size_t nSize = m_arrFileTransferItems.size();
	CFileTransferItem* pItem = NULL;
	
	for(size_t i=0; i<nSize; ++i)
	{
		pItem = m_arrFileTransferItems[i];
		if(pItem == NULL)
			continue;

		if(_tcsicmp(pItem->m_strFileName, pszFileName) == 0)
			return pItem->m_nID;
	}

	return -1;
}

long CFileTransferCtrl::GetItemIDByFullName(PCTSTR pszFullName)
{
	size_t nSize = m_arrFileTransferItems.size();
	CFileTransferItem* pItem = NULL;
	
	for(size_t i=0; i<nSize; ++i)
	{
		pItem = m_arrFileTransferItems[i];
		if(pItem == NULL)
			continue;

		if(_tcsicmp(pItem->m_strFullName, pszFullName) == 0)
			return pItem->m_nID;
	}

	return -1;
}

long CFileTransferCtrl::GetItemIDBySaveName(PCTSTR pszSaveName)
{
	size_t nSize = m_arrFileTransferItems.size();
	CFileTransferItem* pItem = NULL;
	
	for(size_t i=0; i<nSize; ++i)
	{
		pItem = m_arrFileTransferItems[i];
		if(pItem == NULL)
			continue;

		if(_tcsicmp(pItem->m_strFileSaveName, pszSaveName) == 0)
			return pItem->m_nID;
	}

	return -1;
}

BOOL CFileTransferCtrl::SetItemDownloadNameByID(long nID, PCSTR pszDownloadName)
{
	CFileTransferItem* pItem = GetItemByID(nID);
	if (NULL == pItem)
		return FALSE;

	pItem->m_strFileDownloadName = pszDownloadName;
	return TRUE;
}

PCSTR CFileTransferCtrl::GetItemDownloadNameByID(long nID)
{
	CFileTransferItem* pItem = GetItemByID(nID);
	if (NULL == pItem)
		return NULL;

	return pItem->m_strFileDownloadName;
}


PCTSTR CFileTransferCtrl::GetItemFileNameByID(long nID)
{
	CFileTransferItem* pItem = GetItemByID(nID);
	if (NULL == pItem)
		return NULL;

	return pItem->m_strFileName;
}

PCTSTR CFileTransferCtrl::GetItemFileNameByIndex(size_t nIndex)
{
	CFileTransferItem* pItem = GetItemByIndex(nIndex);
	if (NULL == pItem)
		return NULL;

	return pItem->m_strFileName;
}

BOOL CFileTransferCtrl::SetItemFileFullNameByID(long nID, LPCTSTR lpszFileName)
{
	CFileTransferItem* pItem = GetItemByID(nID);
	if (NULL == pItem)
		return FALSE;

	pItem->m_strFullName = lpszFileName;
	return TRUE;
}

PCTSTR CFileTransferCtrl::GetItemFileFullNameByID(long nID)
{
	CFileTransferItem* pItem = GetItemByID(nID);
	if (NULL == pItem)
		return _T("");

	return pItem->m_strFullName;
}

BOOL CFileTransferCtrl::SetItemSaveNameByID(long nID, LPCTSTR lpszSaveName)
{
	CFileTransferItem* pItem = GetItemByID(nID);
	if (NULL == pItem)
		return FALSE;

	pItem->m_strFileSaveName = lpszSaveName;
	return TRUE;
}

int CFileTransferCtrl::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	return 0;
}

BOOL CFileTransferCtrl::OnEraseBkgnd(CDCHandle dc)
{
	return TRUE;
}

void CFileTransferCtrl::OnPaint(CDCHandle dc)
{
	CRect rcClient;
	GetClientRect(&rcClient);

	CPaintDC PaintDC(m_hWnd);

	CMemoryDC MemDC(PaintDC.m_hDC, rcClient);

	//CheckScrollBarStatus();

	if (m_bTransparent)
		DrawParentWndBg(MemDC.m_hDC);

	//if (m_lpBgImg != NULL && !m_lpBgImg->IsNull())
	//	m_lpBgImg->Draw2(MemDC.m_hDC, rcClient);

	//TODO: 加上滚动条
	HFONT hFontBtn = CGDIFactory::GetFont(17);	
	HFONT hOldFont = (HFONT)::SelectObject(MemDC.m_hDC, hFontBtn);
	
	size_t nItemCount = m_arrFileTransferItems.size();
	for (size_t i=0; i<nItemCount; ++i)
	{
		DrawItem(MemDC.m_hDC, i);
	}

	::SelectObject(MemDC.m_hDC, hOldFont);
	//m_VScrollBar.OnPaint(MemDC.m_hDC);
}

void CFileTransferCtrl::OnLButtonUp(UINT nFlags, CPoint point)
{
	SetMsgHandled(FALSE);
	
	long nID = -1;
	HITTEST_BTN_AREA nBtnArea = BTN_NONE;
	HitTest(point, nID, nBtnArea);
	if(nBtnArea==BTN_NONE || nID==-1)
		return;

	FILE_TRANSFER_NMHDREX stNmdhdrEx = {0};
	stNmdhdrEx.nmhdr.hwndFrom = m_hWnd;
	stNmdhdrEx.nmhdr.idFrom = GetDlgCtrlID();
	stNmdhdrEx.nmhdr.code = NM_CLICK;
	stNmdhdrEx.nTargetType = GetItemTargetTypeByID(nID);
	stNmdhdrEx.btnArea = nBtnArea;
	stNmdhdrEx.nID = nID;
	::SendMessage(::GetParent(m_hWnd), WM_NOTIFY, GetDlgCtrlID(), (LPARAM)&stNmdhdrEx);

	
	//TODO: 后期加上滚动条
	//m_VScrollBar.OnLButtonUp(nFlags, point);
}

void CFileTransferCtrl::OnMouseMove(UINT nFlags, CPoint point)
{
	SetMsgHandled(FALSE);
	
	HITTEST_BTN_AREA nBtnArea = BTN_NONE;
	HitTest(point, m_nCurrentHoverID, nBtnArea);

	CFileTransferItem* pItem = GetItemByID(m_nCurrentHoverID);
	if(pItem == NULL)
	{		
		m_bHoverOnAcceptBtn = FALSE;
		m_bHoverOnSaveAsBtn = FALSE;
		m_bHoverOnCancelBtn = FALSE;	
	}
	else
	{
		if(pItem->m_nTargetType == SEND_TYPE)
		{
			m_bHoverOnAcceptBtn = FALSE;
			m_bHoverOnSaveAsBtn = FALSE;
			m_bHoverOnCancelBtn = TRUE;
		}
		else if(pItem->m_nTargetType == RECV_TYPE)
		{
			if(nBtnArea == BTN_ACCEPT)
			{
				m_bHoverOnAcceptBtn = TRUE;
				m_bHoverOnSaveAsBtn = FALSE;
				m_bHoverOnCancelBtn = FALSE;
			}
			else if(nBtnArea == BTN_SAVEAS)
			{
				m_bHoverOnAcceptBtn = FALSE;
				m_bHoverOnSaveAsBtn = TRUE;
				m_bHoverOnCancelBtn = FALSE;
			}
			else if(nBtnArea == BTN_CANCEL)
			{
				m_bHoverOnAcceptBtn = FALSE;
				m_bHoverOnSaveAsBtn = FALSE;
				m_bHoverOnCancelBtn = TRUE;
			}
		}// end inner-if
	
	}// end outer-if

	Invalidate(FALSE);
}

void CFileTransferCtrl::HitTest(POINT pt, long& nID, HITTEST_BTN_AREA& nBtnArea)
{
	size_t nSize = m_arrFileTransferItems.size();
	CRect rtItem;
	CRect rtAcceptBtn;
	CRect rtSaveAsBtn;
	CRect rtCancelBtn;
	CFileTransferItem* pItem = NULL;
	for(size_t i=0; i<nSize; ++i)
	{
		if(!GetItemRectByIndex(i, rtItem))
			continue;

		pItem = GetItemByIndex(i);
		if(pItem == NULL)
			continue;

		if(pItem->m_nTargetType == SEND_TYPE)
		{
			rtCancelBtn.left = rtItem.left+265;
			rtCancelBtn.top = rtItem.top+50;
			rtCancelBtn.right = rtCancelBtn.left+45;
			rtCancelBtn.bottom = rtCancelBtn.top+20;
			if(::PtInRect(&rtCancelBtn, pt))
			{
				nID = pItem->m_nID;
				nBtnArea = BTN_CANCEL;
				return;
			}
		}
		else if(pItem->m_nTargetType == RECV_TYPE)
		{
			rtAcceptBtn.left = rtItem.left + 165;
			rtAcceptBtn.right = rtAcceptBtn.left + 45;
			rtAcceptBtn.top = rtItem.top + 50;
			rtAcceptBtn.bottom = rtAcceptBtn.top + 20;

			if(::PtInRect(&rtAcceptBtn, pt))
			{
				nID = pItem->m_nID;
				nBtnArea = BTN_ACCEPT;
				return;
			}
			
			rtSaveAsBtn.left = rtAcceptBtn.right+5;
			rtSaveAsBtn.right = rtAcceptBtn.right+45;
			rtSaveAsBtn.top = rtAcceptBtn.top;
			rtSaveAsBtn.bottom = rtAcceptBtn.bottom;
			if(::PtInRect(&rtSaveAsBtn, pt))
			{
				nID = pItem->m_nID;
				nBtnArea = BTN_SAVEAS;
				return;
			}

			rtCancelBtn.left = rtSaveAsBtn.right+5;
			rtCancelBtn.top = rtSaveAsBtn.top;
			rtCancelBtn.right = rtSaveAsBtn.right+45;
			rtCancelBtn.bottom = rtSaveAsBtn.bottom;
			if(::PtInRect(&rtCancelBtn, pt))
			{
				nID = pItem->m_nID;
				nBtnArea = BTN_CANCEL;
				return;
			}
		}// end if
	}// end for-loop

	nID = -1;
	nBtnArea = BTN_NONE;
}

void CFileTransferCtrl::DrawParentWndBg(HDC hDC)
{
	HWND hParentWnd = ::GetParent(m_hWnd);

	CRect rcWindow;
	GetWindowRect(&rcWindow);
	::ScreenToClient(hParentWnd, (LPPOINT)&rcWindow); 
	::ScreenToClient(hParentWnd, ((LPPOINT)&rcWindow)+1);

	::BitBlt(hDC, 0, 0, rcWindow.Width(), rcWindow.Height(), m_hBgDC, rcWindow.left, rcWindow.top, SRCCOPY);
}

void CFileTransferCtrl::DrawItem(HDC hDC, size_t nIndex)
{
	CFileTransferItem* pItem = GetItemByIndex(nIndex);
	if(pItem == NULL)
		return;

	CRect rcItem;
	GetItemRectByIndex(nIndex, rcItem);

	CRect rcImageFileType;
	if(pItem->m_pImgFileType!=NULL && !pItem->m_pImgFileType->IsNull())
	{
		rcImageFileType.left = rcItem.left+10;
		rcImageFileType.top = rcItem.top+20;
		rcImageFileType.right = rcImageFileType.left+40;
		rcImageFileType.bottom = rcImageFileType.top+40;

		pItem->m_pImgFileType->Draw2(hDC, rcImageFileType);
	}
	
	UINT nFormat = DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;
	
	//文件名长度大于25的时候，只显示文件名首尾，中间用省略号代替
	CString strTempFileName;
	if(pItem->m_strFileName.GetLength() > 15)
	{
		strTempFileName.Format(_T("%s...%s"), pItem->m_strFileName.Left(6), pItem->m_strFileName.Right(9));
	}
	else
		strTempFileName = pItem->m_strFileName;
	
	CRect rcFileDesc;
	CString strFileDesc;
	if(pItem->m_nProgressPercent == -1)
		strFileDesc.Format(_T("%s(校验文件%d%%)"), strTempFileName, pItem->m_nVerificationPercent);
    else
    {
        if (pItem->m_nTargetType == SEND_TYPE)
        {
            strFileDesc.Format(_T("%s(发送%d%%)"), strTempFileName, pItem->m_nProgressPercent);
        }
        else
        {
            strFileDesc.Format(_T("%s(接收%d%%)"), strTempFileName, pItem->m_nProgressPercent);
        }
    }
	long nFileDescLength = strFileDesc.GetLength();
	
	CRect rcText(0,0,0,0);	// 计算文字宽高
	::DrawText(hDC, strFileDesc, nFileDescLength, &rcText, DT_SINGLELINE | DT_CALCRECT);

	rcFileDesc.left = rcImageFileType.right + 10;
	rcFileDesc.right = rcFileDesc.left + 250;
	rcFileDesc.top = rcImageFileType.top;
	rcFileDesc.bottom = rcFileDesc.top + rcText.Height();
	if (rcFileDesc.right > rcItem.right - 10)
		rcFileDesc.right = rcItem.right - 10;

    int nOldMode = ::SetBkMode(hDC, TRANSPARENT);
	::SetTextColor(hDC, RGB(0, 0, 0));
	::DrawText(hDC, strFileDesc, nFileDescLength, &rcFileDesc, nFormat);
    ::SetBkMode(hDC, nOldMode);
	

	//画进度条
	CRect rtProgress;
	rtProgress.left = rcItem.left + 60;
	rtProgress.right = rtProgress.left + 250;
	rtProgress.top = rcItem.top + 40;
	rtProgress.bottom = rtProgress.top + 10;

	HPEN hPenProgressRect = ::CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
	HPEN hOldPen = (HPEN)::SelectObject(hDC, hPenProgressRect);
	::Rectangle(hDC, rtProgress.left, rtProgress.top, rtProgress.right, rtProgress.bottom);
	::SelectObject(hDC, hOldPen);
	::DeleteObject(hPenProgressRect);
	
	HBRUSH hBrushProgressRect = ::CreateSolidBrush(RGB(62, 206, 79));
	HBRUSH hOldBrush = (HBRUSH)::SelectObject(hDC, hBrushProgressRect);
	long nPercent = pItem->m_nProgressPercent;
	if(nPercent < 0)
		nPercent = 0;
	::PatBlt(hDC, rtProgress.left, rtProgress.top, (rtProgress.Width()*nPercent)/100, 10, PATCOPY);
	::SelectObject(hDC, hOldBrush);
	::DeleteObject(hBrushProgressRect);
	
	//绘制发送方按钮
	if(pItem->m_nTargetType == SEND_TYPE)
	{
		//转离线/在线发送按钮
		CRect rtChangeMethodBtn;
		rtChangeMethodBtn.left = rcItem.left + 190;
		rtChangeMethodBtn.right = rtChangeMethodBtn.left + 70;
		rtChangeMethodBtn.top = rcItem.top + 50;
		rtChangeMethodBtn.bottom = rtChangeMethodBtn.top + 20;
		//::Rectangle(hDC, rtChangeMethodBtn.left, rtChangeMethodBtn.top, rtChangeMethodBtn.right, rtChangeMethodBtn.bottom);
		CRect rtChangeMethodBtnLabel(rtChangeMethodBtn.left+2, rtChangeMethodBtn.top+2, rtChangeMethodBtn.right-2, rtChangeMethodBtn.bottom-2);
#ifdef _DEBUG		
		::DrawText(hDC, _T("转在线发送"), _tcslen(_T("转在线发送")), &rtChangeMethodBtnLabel, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
#endif	
		//取消按钮
		CRect rtCancelBtn(rtChangeMethodBtn.right+5, rtChangeMethodBtn.top, rtChangeMethodBtn.right+50, rtChangeMethodBtn.bottom);
		//::Rectangle(hDC, rtCancelBtn.left, rtCancelBtn.top, rtCancelBtn.right, rtCancelBtn.bottom);
		CRect rtCancelBtnLabel(rtCancelBtn.left+2, rtCancelBtn.top+2, rtCancelBtn.right-2, rtCancelBtn.bottom-2);
		COLORREF clrText(RGB(0, 0, 0));
		if(m_bHoverOnCancelBtn && pItem->m_nID==m_nCurrentHoverID)
			clrText = RGB(38, 156, 236);
		::SetTextColor(hDC, clrText);
		::DrawText(hDC, _T("取消"), _tcslen(_T("取消")), &rtCancelBtnLabel, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
	}
	//绘制接收方按钮
	else if(pItem->m_nTargetType == RECV_TYPE)
	{
		//接受按钮
		CRect rtAcceptBtn;
		rtAcceptBtn.left = rcItem.left + 165;
		rtAcceptBtn.right = rtAcceptBtn.left + 45;
		rtAcceptBtn.top = rcItem.top + 50;
		rtAcceptBtn.bottom = rtAcceptBtn.top + 20;
		//::Rectangle(hDC, rtAcceptBtn.left, rtAcceptBtn.top, rtAcceptBtn.right, rtAcceptBtn.bottom);
		CRect rtAcceptBtnLabel(rtAcceptBtn.left+2, rtAcceptBtn.top+2, rtAcceptBtn.right-2, rtAcceptBtn.bottom-2);
		COLORREF clrText(RGB(0, 0, 0));
		if(m_bHoverOnAcceptBtn && pItem->m_nID==m_nCurrentHoverID)
			clrText = RGB(38, 156, 236);
		::SetTextColor(hDC, clrText);
		if(pItem->m_bAcceptBtnVisible)
			::DrawText(hDC, _T("接受"), _tcslen(_T("接受")), &rtAcceptBtnLabel, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
	
		//另存为按钮
		CRect rtSaveAsBtn(rtAcceptBtn.right+5, rtAcceptBtn.top, rtAcceptBtn.right+50, rtAcceptBtn.bottom);
		//::Rectangle(hDC, rtSaveAsBtn.left, rtSaveAsBtn.top, rtSaveAsBtn.right, rtSaveAsBtn.bottom);
		CRect rtSaveAsBtnLabel(rtSaveAsBtn.left+2, rtSaveAsBtn.top+2, rtSaveAsBtn.right-2, rtSaveAsBtn.bottom-2);
		if(m_bHoverOnSaveAsBtn && pItem->m_nID==m_nCurrentHoverID)
			clrText = RGB(38, 156, 236);
		else
			clrText = RGB(0, 0, 0);
		::SetTextColor(hDC, clrText);
		if(pItem->m_bSaveAsBtnVisible)
			::DrawText(hDC, _T("另存为"), _tcslen(_T("另存为")), &rtSaveAsBtnLabel, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

		//取消按钮
		CRect rtCancelBtn(rtSaveAsBtn.right+5, rtSaveAsBtn.top, rtSaveAsBtn.right+50, rtSaveAsBtn.bottom);
		//::Rectangle(hDC, rtCancelBtn.left, rtCancelBtn.top, rtCancelBtn.right, rtCancelBtn.bottom);
		CRect rtCancelBtnLabel(rtCancelBtn.left+2, rtCancelBtn.top+2, rtCancelBtn.right-2, rtCancelBtn.bottom-2);
		if(m_bHoverOnCancelBtn && pItem->m_nID==m_nCurrentHoverID)
			clrText = RGB(38, 156, 236);
		else
			clrText = RGB(0, 0, 0);
		::SetTextColor(hDC, clrText);
		if(pItem->m_bCancelBtnVisible)
			::DrawText(hDC, _T("取消"), _tcslen(_T("取消")), &rtCancelBtnLabel, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
	}
}

BOOL CFileTransferCtrl::GetItemRectByIndex(size_t nIndex, CRect& rect)
{
	size_t nItemCount = m_arrFileTransferItems.size();
	if(nIndex >= nItemCount)
		return FALSE;
	
	//CFileTransferItem* pItem;
	int nLeft = 0, nTop = 0;
	int nItemWidth = 300, nItemHeight = 100;

	CRect rcClient;
	GetClientRect(&rcClient);

	//int nVScrollBarWidth = 0;
	//if (m_VScrollBar.IsVisible())
	//{
	//	CRect rcVScrollBar;
	//	m_VScrollBar.GetRect(&rcVScrollBar);
	//	nVScrollBarWidth = rcVScrollBar.Width();
	//}
	
	
	rect.left = 0;
	rect.right = nItemWidth;
	rect.top = nIndex* nItemHeight;
	rect.bottom = (nIndex+1)* nItemHeight;
	

	return TRUE;
}

void CFileTransferCtrl::OnDestroy()
{

}

