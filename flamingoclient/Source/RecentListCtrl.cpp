#include "stdafx.h"
#include "RecentListCtrl.h"
#include "GDIFactory.h"
#include <Gdiplus.h>

CRecentItem::CRecentItem(void)
{
	m_nID = 0;
	m_uAccountID = 0;
	m_nOnlineStatus = RCLCTRL_STATUS_TYPE_OFFLINE;
	m_lpHeadImg = NULL;
	m_lpMobileImg = NULL;
	m_nNewMsgCount = 0;
}

CRecentItem::~CRecentItem(void)
{
	DELETE_IMAGE(m_lpHeadImg);
	DELETE_IMAGE(m_lpMobileImg);
}

CRecentTeamItem::CRecentTeamItem(void)
{
	m_nID = 0;
	m_nCurCnt = -1;
	m_nMaxCnt = 0;
	m_bExpand = FALSE;
	m_nHeadFlashAnim = 0;
	m_nHeadFlashAnimState = 0;
}

CRecentTeamItem::~CRecentTeamItem(void)
{
	for (int i = 0; i < (int)m_arrBuddys.size(); i++)
	{
		CRecentItem* lpItem = m_arrBuddys[i];
		if (lpItem != NULL)
			delete lpItem;
	}
	m_arrBuddys.clear();
}

CRecentListCtrl::CRecentListCtrl(void)
{
	m_lpBgImg = NULL;
	m_lpBuddyTeamBgImgH = NULL;
	m_lpArrowImgN = NULL;
	m_lpArrowImgH = NULL;
	m_lpArrowImgS = NULL;
	m_lpExpArrowImgN = NULL;
	m_lpExpArrowImgH = NULL;
	m_lpExpArrowImgS = NULL;
	m_lpBuddyItemBgImgH = NULL;
	m_lpBuddyItemBgImgS = NULL;
	m_lpHeadFrameImg = NULL;
	m_lpStdGGHeadImg = NULL;
	m_lpStdMMHeadImg = NULL;

	m_nSelTeamIndex = m_nSelIndex = -1;
	m_nHoverTeamIndex = m_nHoverIndex = -1;
	m_nHoverSmallIconIndex = -1;
	m_nPressTeamIndex = m_nPressIndex = -1;
	m_nLeft = m_nTop = 0;
	m_bMouseTracking = FALSE;
	m_bTransparent = FALSE;
	m_hBgDC = NULL;

	//m_nStyle = BLC_BIG_ICON_STYLE;
	m_bShowBigIconInSel = TRUE;
	m_nBuddyTeamHeight = 0;
	m_nBuddyItemHeightInBig = 0;
	m_nBuddyItemHeightInSmall = 0;
	m_nBuddyItemHeightInStd = 0;
	m_nBuddyTeamPadding = 0;
	m_nBuddyItemPadding = 0;
	m_rcMargin = CRect(0,0,0,0);
	m_dwOnlineAnimTimerId = NULL;
	m_dwOfflineAnimTimerId = NULL;
	m_dwHeadFlashAnimTimerId = NULL;
}

CRecentListCtrl::~CRecentListCtrl(void)
{
}

BOOL CRecentListCtrl::SetBgPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImg);
	m_lpBgImg = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpBgImg != NULL) ? TRUE : FALSE;
}

BOOL CRecentListCtrl::SetBuddyTeamHotBgPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpBuddyTeamBgImgH);
	m_lpBuddyTeamBgImgH = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpBuddyTeamBgImgH != NULL) ? TRUE : FALSE;
}

BOOL CRecentListCtrl::SetBuddyItemHotBgPic(LPCTSTR lpszFileName, RECT* lpNinePart/* = NULL*/)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpBuddyItemBgImgH);
	m_lpBuddyItemBgImgH = CSkinManager::GetInstance()->GetImage(lpszFileName);
	if (m_lpBuddyItemBgImgH != NULL)
		m_lpBuddyItemBgImgH->SetNinePart(lpNinePart);
	return (m_lpBuddyItemBgImgH != NULL) ? TRUE : FALSE;
}

BOOL CRecentListCtrl::SetBuddyItemSelBgPic(LPCTSTR lpszFileName, RECT* lpNinePart/* = NULL*/)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpBuddyItemBgImgS);
	m_lpBuddyItemBgImgS = CSkinManager::GetInstance()->GetImage(lpszFileName);
	if (m_lpBuddyItemBgImgS != NULL)
		m_lpBuddyItemBgImgS->SetNinePart(lpNinePart);
	return (m_lpBuddyItemBgImgS != NULL) ? TRUE : FALSE;
}

BOOL CRecentListCtrl::SetHeadFramePic(LPCTSTR lpszFileName, RECT* lpNinePart/* = NULL*/)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpHeadFrameImg);
	m_lpHeadFrameImg = CSkinManager::GetInstance()->GetImage(lpszFileName);
	if (m_lpHeadFrameImg != NULL)
		m_lpHeadFrameImg->SetNinePart(lpNinePart);
	return (m_lpHeadFrameImg != NULL) ? TRUE : FALSE;
}

BOOL CRecentListCtrl::SetNormalArrowPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpArrowImgN);
	m_lpArrowImgN = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpArrowImgN != NULL) ? TRUE : FALSE;
}

BOOL CRecentListCtrl::SetHotArrowPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpArrowImgH);
	m_lpArrowImgH = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpArrowImgH != NULL) ? TRUE : FALSE;
}

BOOL CRecentListCtrl::SetSelArrowPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpArrowImgS);
	m_lpArrowImgS = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpArrowImgS != NULL) ? TRUE : FALSE;
}

BOOL CRecentListCtrl::SetNormalExpArrowPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpExpArrowImgN);
	m_lpExpArrowImgN = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpExpArrowImgN != NULL) ? TRUE : FALSE;
}

BOOL CRecentListCtrl::SetHotExpArrowPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpExpArrowImgH);
	m_lpExpArrowImgH = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpExpArrowImgH != NULL) ? TRUE : FALSE;
}

BOOL CRecentListCtrl::SetSelExpArrowPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpExpArrowImgS);
	m_lpExpArrowImgS = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpExpArrowImgS != NULL) ? TRUE : FALSE;
}

BOOL CRecentListCtrl::SetStdGGHeadPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpStdGGHeadImg);
	m_lpStdGGHeadImg = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpStdGGHeadImg != NULL) ? TRUE : FALSE;
}

BOOL CRecentListCtrl::SetStdMMHeadPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpStdMMHeadImg);
	m_lpStdMMHeadImg = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpStdMMHeadImg != NULL) ? TRUE : FALSE;
}

//void CRecentListCtrl::SetStyle(BLCTRL_STYLE nStyle)
//{
//	m_nStyle = nStyle;
//}

void CRecentListCtrl::SetShowBigIconInSel(BOOL bShowBigIconInSel)
{
	m_bShowBigIconInSel = bShowBigIconInSel;
}

void CRecentListCtrl::SetBuddyTeamHeight(int nHeight)
{
	m_nBuddyTeamHeight = nHeight;
}

void CRecentListCtrl::SetBuddyItemHeightInBigIcon(int nHeight)
{
	m_nBuddyItemHeightInBig = nHeight;
}

void CRecentListCtrl::SetBuddyItemHeightInSmallIcon(int nHeight)
{
	m_nBuddyItemHeightInSmall = nHeight;
}

void CRecentListCtrl::SetBuddyItemHeightInStandardIcon(int nHeight)
{
	m_nBuddyItemHeightInStd = nHeight;
}

void CRecentListCtrl::SetBuddyTeamPadding(int nPadding)
{
	m_nBuddyTeamPadding = nPadding;
}

void CRecentListCtrl::SetBuddyItemPadding(int nPadding)
{
	m_nBuddyItemPadding = nPadding;
}

void CRecentListCtrl::SetMargin(const RECT* lpMargin)
{
	if (lpMargin != NULL)
		m_rcMargin =*lpMargin;
}

int CRecentListCtrl::AddBuddyTeam(int nID)
{
	CRecentTeamItem* lpItem = new CRecentTeamItem;
	if (NULL == lpItem)
		return -1;
	lpItem->m_nID = nID;
	m_arrBuddyTeamItems.push_back(lpItem);
	return m_arrBuddyTeamItems.size() - 1;
}

int CRecentListCtrl::InsertBuddyTeam(int nTeamIndex, int nID)
{
	return -1;
}

BOOL CRecentListCtrl::DelBuddyTeam(int nTeamIndex)
{
	if (nTeamIndex >= 0 && nTeamIndex < (int)m_arrBuddyTeamItems.size())
	{
		CRecentTeamItem* lpBuddyTeamItem = m_arrBuddyTeamItems[nTeamIndex];
		if (lpBuddyTeamItem != NULL)
			delete lpBuddyTeamItem;
		m_arrBuddyTeamItems.erase(m_arrBuddyTeamItems.begin()+nTeamIndex);
		return TRUE;
	}
	return FALSE;
}

int CRecentListCtrl::AddBuddyItem(int nTeamIndex, int nID)
{
	CRecentTeamItem* lpTeamItem = GetBuddyTeamByIndex(nTeamIndex);
	if (NULL == lpTeamItem)
		return -1;
	CRecentItem* lpItem = new CRecentItem;
	if (NULL == lpItem)
		return -1;
	lpItem->m_nID = nID;
	lpTeamItem->m_arrBuddys.push_back(lpItem);
	return lpTeamItem->m_arrBuddys.size() - 1;
}

int CRecentListCtrl::InsertBuddyItem(int nTeamIndex, int nIndex, int nID)
{
	return -1;
}

BOOL CRecentListCtrl::DelBuddyItem(int nTeamIndex, int nIndex)
{
	if (nTeamIndex >= 0 && nTeamIndex < (int)m_arrBuddyTeamItems.size())
	{
		CRecentTeamItem* lpTeamItem = m_arrBuddyTeamItems[nTeamIndex];
		if (lpTeamItem != NULL)
		{
			if (nIndex >= 0 && nIndex < (int)lpTeamItem->m_arrBuddys.size())
			{
				CRecentItem* lpBuddyItem = lpTeamItem->m_arrBuddys[nIndex];
				if (lpBuddyItem != NULL)
					delete lpBuddyItem;
				lpTeamItem->m_arrBuddys.erase(lpTeamItem->m_arrBuddys.begin()+nIndex);
				return TRUE;
			}
		}
	}
	return FALSE;
}

void CRecentListCtrl::DelAllItems()
{
	for (int i = 0; i < (int)m_arrBuddyTeamItems.size(); i++)
	{
		CRecentTeamItem* lpTeamItem = m_arrBuddyTeamItems[i];
		if (lpTeamItem != NULL)
			delete lpTeamItem;
	}
	m_arrBuddyTeamItems.clear();
}

void CRecentListCtrl::SetBuddyTeamID(int nTeamIndex, int nID)
{
	CRecentTeamItem* lpItem = GetBuddyTeamByIndex(nTeamIndex);
	if (lpItem != NULL)
		lpItem->m_nID = nID;
}

void CRecentListCtrl::SetBuddyTeamName(int nTeamIndex, LPCTSTR lpszText)
{
	CRecentTeamItem* lpItem = GetBuddyTeamByIndex(nTeamIndex);
	if (lpItem != NULL)
		lpItem->m_strName = lpszText;
}

void CRecentListCtrl::SetBuddyTeamMaxCnt(int nTeamIndex, int nMaxCnt)
{
	CRecentTeamItem* lpItem = GetBuddyTeamByIndex(nTeamIndex);
	if (lpItem != NULL)
		lpItem->m_nMaxCnt = nMaxCnt;
}
// 更新好友列表在线人数
void CRecentListCtrl::SetBuddyTeamCurCnt(int nTeamIndex, int nCurCnt)
{
	CRecentTeamItem* lpItem = GetBuddyTeamByIndex(nTeamIndex);
	if (lpItem != NULL)
		lpItem->m_nCurCnt = nCurCnt;
}

void CRecentListCtrl::SetBuddyTeamExpand(int nTeamIndex, BOOL bExpand)
{
	CRecentTeamItem* lpItem = GetBuddyTeamByIndex(nTeamIndex);
	if (lpItem != NULL)
		lpItem->m_bExpand = bExpand;
}

int CRecentListCtrl::GetBuddyTeamID(int nTeamIndex)
{
	CRecentTeamItem* lpItem = GetBuddyTeamByIndex(nTeamIndex);
	if (lpItem != NULL)
		return lpItem->m_nID;
	else
		return 0;
}

CString CRecentListCtrl::GetBuddyTeamName(int nTeamIndex)
{
	CRecentTeamItem* lpItem = GetBuddyTeamByIndex(nTeamIndex);
	if (lpItem != NULL)
		return lpItem->m_strName;
	else
		return _T("");
}

int CRecentListCtrl::GetBuddyTeamMaxCnt(int nTeamIndex)
{
	CRecentTeamItem* lpItem = GetBuddyTeamByIndex(nTeamIndex);
	if (lpItem != NULL)
		return lpItem->m_nMaxCnt;
	else
		return 0;
}

int CRecentListCtrl::GetBuddyTeamCurCnt(int nTeamIndex)
{
	CRecentTeamItem* lpItem = GetBuddyTeamByIndex(nTeamIndex);
	if (lpItem != NULL)
		return lpItem->m_nCurCnt;
	else
		return 0;
}

BOOL CRecentListCtrl::IsBuddyTeamExpand(int nTeamIndex)
{
	CRecentTeamItem* lpItem = GetBuddyTeamByIndex(nTeamIndex);
	if (lpItem != NULL)
		return lpItem->m_bExpand;
	else
		return FALSE;
}

void CRecentListCtrl::SetBuddyItemID(int nTeamIndex, int nIndex, int nID)
{
	CRecentItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (lpItem != NULL)
		lpItem->m_nID = nID;
}

void CRecentListCtrl::SetBuddyItemNickName(int nTeamIndex, int nIndex, LPCTSTR lpszText, BOOL bShow)
{
	CRecentItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (lpItem != NULL)
	{
		lpItem->m_strNickName = lpszText;
	}
}

void CRecentListCtrl::SetBuddyItemAccountID(int nTeamIndex, int nIndex, UINT  uAccountID)
{
	CRecentItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (lpItem != NULL)
	{
		lpItem->m_uAccountID = uAccountID;
	}
}

void CRecentListCtrl::SetBuddyItemAccount(int nTeamIndex, int nIndex, LPCTSTR lpszText, BOOL bShow)
{
	CRecentItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (lpItem != NULL)
	{
		//lpItem->m_strAccount = lpszText;
		//lpItem->m_bShowAccountName = bShow;
	}
}

void CRecentListCtrl::SetBuddyItemDate(int nTeamIndex, int nIndex, LPCTSTR lpszText)
{
	CRecentItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (lpItem != NULL)
		lpItem->m_strDate = lpszText;
}

void CRecentListCtrl::SetBuddyItemLastMsg(int nTeamIndex, int nIndex, LPCTSTR lpszText)
{
	CRecentItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (lpItem != NULL)
	{
		lpItem->m_strLastMsg = lpszText;
	}
}

void CRecentListCtrl::SetBuddyItemGender(int nTeamIndex, int nIndex, BOOL bGender)
{
	CRecentItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (lpItem != NULL)
	{
		//lpItem->m_bGender = bGender;
	}
}

BOOL CRecentListCtrl::SetBuddyItemHeadPic(int nTeamIndex, int nIndex, LPCTSTR lpszFileName, BOOL bGray)
{
	CRecentItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (NULL == lpItem)
		return FALSE;

	if (lpItem->m_lpHeadImg != NULL)
		delete lpItem->m_lpHeadImg;

	lpItem->m_lpHeadImg = new CImageEx;
	if (NULL == lpItem->m_lpHeadImg)
		return FALSE;

	BOOL bRet = lpItem->m_lpHeadImg->LoadFromFile(lpszFileName);
	if (!bRet)
	{
		delete lpItem->m_lpHeadImg;
		lpItem->m_lpHeadImg = NULL;
		return FALSE;
	}

	if (bGray)
		lpItem->m_lpHeadImg->GrayScale();

	if (IsWindowVisible())
		Invalidate();

	return TRUE;
}

BOOL CRecentListCtrl::SetBuddyItemMobilePic(int nTeamIndex, int nIndex, LPCTSTR lpszFileName)
{
	CRecentItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (NULL == lpItem)
		return FALSE;

	if (lpItem->m_lpMobileImg != NULL)
		delete lpItem->m_lpMobileImg;

	lpItem->m_lpMobileImg = new CImageEx;
	if (NULL == lpItem->m_lpMobileImg)
		return FALSE;

	BOOL bRet = lpItem->m_lpMobileImg->LoadFromFile(lpszFileName);
	if (!bRet)
	{
		delete lpItem->m_lpMobileImg;
		lpItem->m_lpMobileImg = NULL;
		return FALSE;
	}

	if (IsWindowVisible())
		Invalidate();

	return TRUE;
}

void CRecentListCtrl::SetBuddyItemHeadFlashAnim(int nTeamIndex, int nIndex, BOOL bHeadFlashAnim)
{
	//CRecentItem* lpBuddyItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	//if (NULL == lpBuddyItem || bHeadFlashAnim == lpBuddyItem->m_bHeadFlashAnim)
	//	return;

	//lpBuddyItem->m_bHeadFlashAnim = bHeadFlashAnim;
	//lpBuddyItem->m_nHeadFlashAnimState = 0;
	//if (bHeadFlashAnim)
	//{
	//	CRecentTeamItem* lpBuddyTeam = GetBuddyTeamByIndex(nTeamIndex);
	//	if (lpBuddyTeam != NULL)
	//	{
	//		lpBuddyTeam->m_nHeadFlashAnim++;
	//	}

	//	if (NULL == m_dwHeadFlashAnimTimerId)	// 启动头像闪动动画计时器
	//		m_dwHeadFlashAnimTimerId = SetTimer(2, 250, NULL);
	//}
	//else
	//{
	//	CRecentTeamItem* lpBuddyTeam = GetBuddyTeamByIndex(nTeamIndex);
	//	if (lpBuddyTeam != NULL)
	//	{
	//		lpBuddyTeam->m_nHeadFlashAnim--;
	//		if (lpBuddyTeam->m_nHeadFlashAnim <= 0)
	//			lpBuddyTeam->m_nHeadFlashAnimState = 0;
	//	}

	//	BOOL bHasAnim = FALSE;

	//	int nTeamCnt = GetBuddyTeamCount();
	//	for (int i = 0; i < nTeamCnt; i++)
	//	{
	//		lpBuddyTeam = GetBuddyTeamByIndex(i);
	//		if (lpBuddyTeam != NULL && lpBuddyTeam->m_nHeadFlashAnim > 0)
	//		{
	//			bHasAnim = TRUE;
	//			break;
	//		}
	//	}

	//	if (!bHasAnim)
	//	{
	//		KillTimer(m_dwHeadFlashAnimTimerId);
	//		m_dwHeadFlashAnimTimerId = NULL;

	//		if (::IsWindowVisible(m_hWnd))
	//			Invalidate();
	//	}
	//}
}

void CRecentListCtrl::SetBuddyItemNewMsgCount(int nTeamIndex, int nIndex, long nNewMsgCount)
{
	CRecentItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (lpItem != NULL)
		lpItem->m_nNewMsgCount = nNewMsgCount;	
}

int CRecentListCtrl::GetBuddyItemID(int nTeamIndex, int nIndex)
{
	CRecentItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (lpItem != NULL)
		return lpItem->m_nID;
	else
		return 0;
}

UINT CRecentListCtrl::GetBuddyItemAccountID(int nTeamIndex, int nIndex)
{
	CRecentItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (lpItem != NULL)
		return lpItem->m_uAccountID;
	else
		return 0;
}

CString CRecentListCtrl::GetBuddyItemNickName(int nTeamIndex, int nIndex)
{
	CRecentItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (lpItem != NULL)
		return lpItem->m_strNickName;
	else
		return _T("");
}

CString CRecentListCtrl::GetBuddyItemAccount(int nTeamIndex, int nIndex)
{
	//CRecentItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	//if (lpItem != NULL)
	//	return lpItem->m_strAccount;
	//else
		return _T("");
}

CString CRecentListCtrl::GetBuddyItemLastMsg(int nTeamIndex, int nIndex)
{
	CRecentItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (lpItem != NULL)
		return lpItem->m_strLastMsg;
	else
		return _T("");
}

BOOL CRecentListCtrl::GetBuddyItemGender(int nTeamIndex, int nIndex)
{
	//CRecentItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	//if (lpItem != NULL)
	//	return lpItem->m_bGender;
	//else
		return FALSE;
}

BOOL CRecentListCtrl::IsBuddyItemHasMsg(int nTeamIndex, int nIndex)
{
	//CRecentItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	//if (lpItem != NULL)
	//	return lpItem->m_bHeadFlashAnim;
	//else
		return FALSE;
}

int CRecentListCtrl::GetBuddyTeamCount()
{
	return m_arrBuddyTeamItems.size();
}

int CRecentListCtrl::GetBuddyItemCount(int nTeamIndex)
{
	CRecentTeamItem* lpTeamItem = GetBuddyTeamByIndex(nTeamIndex);
	if (lpTeamItem != NULL)
		return lpTeamItem->m_arrBuddys.size();
	else
		return 0;
}

BOOL CRecentListCtrl::GetItemIndexByID(int nID, int& nTeamIndex, int& nIndex)
{
	nTeamIndex = -1;
	nIndex = -1;

	for (int i = 0; i < (int)m_arrBuddyTeamItems.size(); i++)
	{
		CRecentTeamItem* lpTeamItem = m_arrBuddyTeamItems[i];
		if (lpTeamItem != NULL)
		{
			if (nID == lpTeamItem->m_nID)
			{
				nTeamIndex = i;
				return TRUE;
			}

			for (int j = 0; j < (int)lpTeamItem->m_arrBuddys.size(); j++)
			{
				CRecentItem* lpItem = lpTeamItem->m_arrBuddys[j];
				if (lpItem != NULL && nID == lpItem->m_nID)
				{
					if (nID == lpItem->m_nID)
					{
						nTeamIndex = i;
						nIndex = j;
						return TRUE;
					}
				}
			}
		}
	}
	return FALSE;
}

BOOL CRecentListCtrl::GetItemRectByIndex(int nTeamIndex, int nIndex, CRect& rect)
{
	CRecentTeamItem* lpTeamItem;
	CRecentItem* lpItem;
	int nLeft = m_rcMargin.left+m_nLeft, nTop = m_nTop;
	int nBuddyTeamWidth, nBuddyItemWidth, nBuddyItemHeight;

	CRect rcClient;
	GetClientRect(&rcClient);

	int nVScrollBarWidth = 0;
	if (m_VScrollBar.IsVisible())
	{
		CRect rcVScrollBar;
		m_VScrollBar.GetRect(&rcVScrollBar);
		nVScrollBarWidth = rcVScrollBar.Width();
	}

	nBuddyTeamWidth = rcClient.Width() - m_rcMargin.left - m_rcMargin.right - nVScrollBarWidth;
	nBuddyItemWidth = nBuddyTeamWidth;
	//if (BLC_BIG_ICON_STYLE == m_nStyle)
		nBuddyItemHeight = m_nBuddyItemHeightInBig;
	//else if (BLC_SMALL_ICON_STYLE == m_nStyle)
		//nBuddyItemHeight = m_nBuddyItemHeightInSmall;
	//else
		//nBuddyItemHeight = m_nBuddyItemHeightInStd;

	for (int i = 0; i < (int)m_arrBuddyTeamItems.size(); i++)
	{
		lpTeamItem = m_arrBuddyTeamItems[i];
		if (lpTeamItem != NULL)
		{
			if (-1 == nIndex && i == nTeamIndex)
			{
				rect = CRect(nLeft, nTop, nLeft+nBuddyTeamWidth, nTop+m_nBuddyTeamHeight);
				return TRUE;
			}

			nTop += m_nBuddyTeamHeight;
			nTop += m_nBuddyTeamPadding;

			if (lpTeamItem->m_bExpand)
			{
				for (int j = 0; j < (int)lpTeamItem->m_arrBuddys.size(); j++)
				{
					lpItem = lpTeamItem->m_arrBuddys[j];
					if (lpItem != NULL)
					{
						//if (BLC_SMALL_ICON_STYLE == m_nStyle && m_bShowBigIconInSel
						//	&& (m_nSelTeamIndex == i && m_nSelIndex == j))
						//	nBuddyItemHeight = m_nBuddyItemHeightInBig;

						if (i == nTeamIndex && j == nIndex)
						{
							rect = CRect(nLeft, nTop, nLeft+nBuddyItemWidth, nTop+nBuddyItemHeight);
							return TRUE;
						}
						nTop += nBuddyItemHeight;
						nTop += m_nBuddyItemPadding;

						//if (BLC_SMALL_ICON_STYLE == m_nStyle && m_bShowBigIconInSel
						//	&& (m_nSelTeamIndex == i && m_nSelIndex == j))
						//	nBuddyItemHeight = m_nBuddyItemHeightInSmall;
					}
				}
			}
		}
	}

	return FALSE;
}

void CRecentListCtrl::GetCurSelIndex(int& nTeamIndex, int& nIndex)
{
	nTeamIndex = m_nSelTeamIndex;
	nIndex = m_nSelIndex;
}

//BLCTRL_STYLE CRecentListCtrl::GetStyle()
//{
//	return m_nStyle;
//}

BOOL CRecentListCtrl::IsShowBigIconInSel()
{
	return m_bShowBigIconInSel;
}

void CRecentListCtrl::SetTransparent(BOOL bTransparent, HDC hBgDC)
{
	m_bTransparent = bTransparent;
	m_hBgDC = hBgDC;
}

int CRecentListCtrl::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	CRect rcClient;
	GetClientRect(&rcClient);

	CRect rc = CRect(rcClient.Width()-14, 0, rcClient.Width(), rcClient.Height());
	m_VScrollBar.Create(m_hWnd, 1, &rc, 1, FALSE, FALSE);

	m_VScrollBar.SetBgNormalPic(_T("ScrollBar\\scrollbar_bkg.png"));
	m_VScrollBar.SetBgHotPic(_T("ScrollBar\\scrollbar_bkg.png"));
	m_VScrollBar.SetBgPushedPic(_T("ScrollBar\\scrollbar_bkg.png"));
	m_VScrollBar.SetBgDisabledPic(_T("ScrollBar\\scrollbar_bkg.png"));

	m_VScrollBar.SetShowLeftUpBtn(TRUE);
	m_VScrollBar.SetLeftUpBtnNormalPic(_T("ScrollBar\\scrollbar_arrowup_normal.png"));
	m_VScrollBar.SetLeftUpBtnHotPic(_T("ScrollBar\\scrollbar_arrowup_highlight.png"));
	m_VScrollBar.SetLeftUpBtnPushedPic(_T("ScrollBar\\scrollbar_arrowup_down.png"));
	m_VScrollBar.SetLeftUpBtnDisabledPic(_T("ScrollBar\\scrollbar_arrowup_normal.png"));

	m_VScrollBar.SetShowRightDownBtn(TRUE);
	m_VScrollBar.SetRightDownBtnNormalPic(_T("ScrollBar\\scrollbar_arrowdown_normal.png"));
	m_VScrollBar.SetRightDownBtnHotPic(_T("ScrollBar\\scrollbar_arrowdown_highlight.png"));
	m_VScrollBar.SetRightDownBtnPushedPic(_T("ScrollBar\\scrollbar_arrowdown_down.png"));
	m_VScrollBar.SetRightDownBtnDisabledPic(_T("ScrollBar\\scrollbar_arrowdown_normal.png"));

	m_VScrollBar.SetThumbNormalPic(_T("ScrollBar\\scrollbar_bar_normal.png"), CRect(0,1,0,1));
	m_VScrollBar.SetThumbHotPic(_T("ScrollBar\\scrollbar_bar_highlight.png"), CRect(0,1,0,1));
	m_VScrollBar.SetThumbPushedPic(_T("ScrollBar\\scrollbar_bar_down.png"), CRect(0,1,0,1));
	m_VScrollBar.SetThumbDisabledPic(_T("ScrollBar\\scrollbar_bar_normal.png"), CRect(0,1,0,1));
	
	return 0;
}

BOOL CRecentListCtrl::OnEraseBkgnd(CDCHandle dc)
{
	return TRUE;
}

void CRecentListCtrl::OnPaint(CDCHandle dc)
{
	CRect rcClient;
	GetClientRect(&rcClient);

	CPaintDC PaintDC(m_hWnd);

	CMemoryDC MemDC(PaintDC.m_hDC, rcClient);

	CheckScrollBarStatus();

	if (m_bTransparent)
		DrawParentWndBg(MemDC.m_hDC);

	if (m_lpBgImg != NULL && !m_lpBgImg->IsNull())
		m_lpBgImg->Draw2(MemDC.m_hDC, rcClient);

	CRecentTeamItem* lpTeamItem;
	size_t nTeamCount = m_arrBuddyTeamItems.size();
	for (size_t i = 0; i < nTeamCount; ++i)
	{
		DrawBuddyTeam(MemDC.m_hDC, i);

		lpTeamItem = m_arrBuddyTeamItems[i];
		if (lpTeamItem != NULL && lpTeamItem->m_bExpand)
		{
			for (size_t j = 0; j < lpTeamItem->m_arrBuddys.size(); ++j)
			{
				DrawBuddyItem(MemDC.m_hDC, i, j);
			}
		}
	}

	m_VScrollBar.OnPaint(MemDC.m_hDC);
}

void CRecentListCtrl::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	SetMsgHandled(FALSE);

	SetFocus();

	int nTeamIndex = -1, nIndex = -1;
	HitTest(point, nTeamIndex, nIndex);

	m_nSelTeamIndex = nTeamIndex;
	m_nSelIndex = nIndex;

	Invalidate();

	NMHDR stNmhdr = {m_hWnd, GetDlgCtrlID(), NM_DBLCLK};
	::SendMessage(::GetParent(m_hWnd), WM_NOTIFY, GetDlgCtrlID(), (LPARAM)&stNmhdr);
}

void CRecentListCtrl::OnLButtonDown(UINT nFlags, CPoint point)
{
	SetMsgHandled(FALSE);

	SetFocus();

	int nTeamIndex = -1, nIndex = -1;
	HitTest(point, nTeamIndex, nIndex);

	m_nPressTeamIndex = nTeamIndex;
	m_nPressIndex = nIndex;

	m_VScrollBar.OnLButtonDown(nFlags, point);
}

void CRecentListCtrl::OnLButtonUp(UINT nFlags, CPoint point)
{
	SetMsgHandled(FALSE);

	int nTeamIndex = -1, nIndex = -1;
	HitTest(point, nTeamIndex, nIndex);

	if (nTeamIndex != -1 && nTeamIndex == m_nPressTeamIndex
		&& nIndex == m_nPressIndex)
	{
		m_nSelTeamIndex = nTeamIndex;
		m_nSelIndex = nIndex;
		if (-1 == nIndex)		// 点击的是好友分组项
		{
			BOOL bExpand = IsBuddyTeamExpand(nTeamIndex);
			SetBuddyTeamExpand(nTeamIndex, !bExpand);
		}
		
		EnsureVisible(m_nSelTeamIndex, m_nSelIndex);
		Invalidate();
	}

	m_nPressTeamIndex = -1;
	m_nPressIndex = -1;

	m_VScrollBar.OnLButtonUp(nFlags, point);
}

void CRecentListCtrl::OnRButtonUp(UINT nFlags, CPoint point)
{
	SetMsgHandled(FALSE);

	SetFocus();

	int nTeamIndex = -1, nIndex = -1;
	HitTest(point, nTeamIndex, nIndex);

	m_nSelTeamIndex = nTeamIndex;
	m_nSelIndex = nIndex;

	Invalidate();
	
	if(nTeamIndex>=0 && nIndex>=0)
	{
		NMHDR stNmhdr = {m_hWnd, GetDlgCtrlID(), NM_RCLICK};
		::SendMessage(::GetParent(m_hWnd), WM_NOTIFY, GetDlgCtrlID(), (LPARAM)&stNmhdr);
	}
}

void CRecentListCtrl::OnMouseMove(UINT nFlags, CPoint point)
{
	SetMsgHandled(FALSE);

	if (m_nPressTeamIndex != -1)
		return;

	if (!m_bMouseTracking)
	{
		StartTrackMouseLeave();
		m_bMouseTracking = TRUE;
	}

	int nTeamIndex = -1, nIndex = -1;
	HitTest(point, nTeamIndex, nIndex);

	if (nTeamIndex != m_nHoverTeamIndex || nIndex != m_nHoverIndex)
	{
		m_nHoverTeamIndex = nTeamIndex;
		m_nHoverIndex = nIndex;
		Invalidate();
	}

	m_VScrollBar.OnMouseMove(nFlags, point);
}

void CRecentListCtrl::OnMouseLeave()
{
	SetMsgHandled(FALSE);

	m_bMouseTracking = FALSE;
	if (-1 != m_nHoverTeamIndex || -1 != m_nHoverIndex)
	{
		m_nHoverTeamIndex = -1;
		m_nHoverIndex = -1;
		Invalidate();
	}

	m_VScrollBar.OnMouseLeave();
}

void CRecentListCtrl::OnTimer(UINT_PTR nIDEvent)
{
	m_VScrollBar.OnTimer(nIDEvent);

	//if (nIDEvent == m_dwHeadFlashAnimTimerId)		// 头像闪动动画
	//{
	//	OnTimer_HeadFlashAnim(nIDEvent);
	//}
	//else if (nIDEvent == m_dwOnlineAnimTimerId)		// 上线动画
	//{
	//	OnTimer_OnlineAnim(nIDEvent);
	//}
	//else if (nIDEvent == m_dwOfflineAnimTimerId)	// 下线动画
	//{
	//	OnTimer_OfflineAnim(nIDEvent);
	//}
}

void CRecentListCtrl::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar pScrollBar)
{
	CRect rcClient;
	GetClientRect(&rcClient);

	int nLineSize = 24;
	int nPageSize = rcClient.Height();

	switch (nSBCode)
	{
	case SB_LINEUP:
		Scroll(0, -nLineSize);
		break;

	case SB_LINEDOWN:
		Scroll(0, nLineSize);
		break;

	case SB_PAGEUP:
		Scroll(0, -nPageSize);
		break;

	case SB_PAGEDOWN:
		Scroll(0, nPageSize);
		break;

	case SB_THUMBTRACK:
		Scroll(0, 0);
		break;

	default:
		return;
	}
	Invalidate();
}

BOOL CRecentListCtrl::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	int nLineSize = 100;

	if (zDelta < 0)		// SB_LINEDOWN
		Scroll(0, nLineSize);
	else	// SB_LINEUP
		Scroll(0, -nLineSize);

	Invalidate();

	return TRUE;
}

void CRecentListCtrl::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if (GetBuddyTeamCount() <= 0)
		return;

	switch (nChar)
	{
	case VK_UP:	// 向上光标键
		{
			if (-1 == m_nSelIndex)
			{
				if (0 == m_nSelTeamIndex)
					return;

				m_nSelTeamIndex--;
				m_nSelIndex = -1;

				if (IsBuddyTeamExpand(m_nSelTeamIndex))
				{
					int nItemCnt = GetBuddyItemCount(m_nSelTeamIndex);
					if (nItemCnt > 0)
						m_nSelIndex = nItemCnt - 1;
				}
			}
			else
			{
				if (0 == m_nSelIndex)
					m_nSelIndex = -1;
				else
					m_nSelIndex--;
			}
		}
		break;

	case VK_DOWN:	// 向下光标键
		{
			if (-1 == m_nSelIndex)
			{
				if (IsBuddyTeamExpand(m_nSelTeamIndex) &&
					GetBuddyItemCount(m_nSelTeamIndex) > 0)
				{
					m_nSelIndex = 0;
				}
				else
				{
					if ((GetBuddyTeamCount() - 1) == m_nSelTeamIndex)
						return;

					m_nSelTeamIndex++;
					m_nSelIndex = -1;
				}
			}
			else
			{
				if (GetBuddyItemCount(m_nSelTeamIndex) - 1 == m_nSelIndex)
				{
					if (GetBuddyTeamCount() - 1 == m_nSelTeamIndex)
						return;

					m_nSelTeamIndex++;
					m_nSelIndex = -1;
				}
				else
					m_nSelIndex += 1;
			}
		}
		break;

	case VK_LEFT:	// 向左光标键
		{
			if (-1 == m_nSelIndex)	// 
			{
				if (IsBuddyTeamExpand(m_nSelTeamIndex))
					SetBuddyTeamExpand(m_nSelTeamIndex, FALSE);
			}
			else
				m_nSelIndex = -1;
		}
		break;

	case VK_RIGHT:	// 向右光标键
		{
			if (-1 == m_nSelIndex)
			{
				if (!IsBuddyTeamExpand(m_nSelTeamIndex))
					SetBuddyTeamExpand(m_nSelTeamIndex, TRUE);
				else if (GetBuddyItemCount(m_nSelTeamIndex) > 0)
					m_nSelIndex = 0;
			}
		}
		break;

	case VK_PRIOR:	// PgUp键
		{
			CRect rcClient;
			GetClientRect(&rcClient);
			Scroll(0, -rcClient.Height());
			Invalidate();
		}
		return;

	case VK_NEXT:	// PgDn键
		{
			CRect rcClient;
			GetClientRect(&rcClient);
			Scroll(0, rcClient.Height());
			Invalidate();
		}
		return;

	case VK_HOME:	// Home键
		{
			m_nSelTeamIndex = 0;
			m_nSelIndex = -1;
		}
		break;

	case VK_END:	// End键
		{
			int TeamCnt = GetBuddyTeamCount();

			m_nSelTeamIndex = TeamCnt - 1;
			m_nSelIndex = -1;
			if (IsBuddyTeamExpand(TeamCnt - 1) &&
				GetBuddyItemCount(TeamCnt - 1) > 0)
				m_nSelIndex = GetBuddyItemCount(TeamCnt - 1) - 1;
		}
		break;

	case VK_RETURN:	// 回车键
		{
			
		}
		return;

	default:
		return;
	}

	EnsureVisible(m_nSelTeamIndex, m_nSelIndex);
	Invalidate();
}

void CRecentListCtrl::OnSize(UINT nType, CSize size)
{
	SetMsgHandled(FALSE);

	CRect rcClient;
	GetClientRect(&rcClient);

	CRect rc = CRect(rcClient.Width()-14, 0, rcClient.Width(), rcClient.Height());
	m_VScrollBar.SetRect(&rc);
	Invalidate();
}

UINT CRecentListCtrl::OnGetDlgCode(LPMSG lpMsg)
{
	return DLGC_HASSETSEL | DLGC_WANTARROWS | DLGC_WANTCHARS | DLGC_WANTTAB;
}

LRESULT CRecentListCtrl::OnMouseMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	SetMsgHandled(FALSE);
	MSG msg = { m_hWnd, uMsg, wParam, lParam };
	if (m_ToolTipCtrl.IsWindow())
		m_ToolTipCtrl.RelayEvent(&msg);
	return 1;
}

void CRecentListCtrl::OnDestroy()
{
	SetMsgHandled(FALSE);

	if (m_dwHeadFlashAnimTimerId != NULL)
	{
		KillTimer(m_dwHeadFlashAnimTimerId);
		m_dwHeadFlashAnimTimerId = NULL;
	}

	if (m_ToolTipCtrl.IsWindow())	// ToolTipCtrl早在之前的不知道什么地方已销毁，这里显式置空m_hWnd
		m_ToolTipCtrl.DestroyWindow();
	m_ToolTipCtrl.m_hWnd = NULL;

	m_VScrollBar.Destroy();

	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImg);
	CSkinManager::GetInstance()->ReleaseImage(m_lpBuddyTeamBgImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpArrowImgN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpArrowImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpArrowImgS);
	CSkinManager::GetInstance()->ReleaseImage(m_lpExpArrowImgN);
	CSkinManager::GetInstance()->ReleaseImage(m_lpExpArrowImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpExpArrowImgS);
	CSkinManager::GetInstance()->ReleaseImage(m_lpBuddyItemBgImgH);
	CSkinManager::GetInstance()->ReleaseImage(m_lpBuddyItemBgImgS);
	CSkinManager::GetInstance()->ReleaseImage(m_lpHeadFrameImg);
	CSkinManager::GetInstance()->ReleaseImage(m_lpStdGGHeadImg);
	CSkinManager::GetInstance()->ReleaseImage(m_lpStdMMHeadImg);

	m_nSelTeamIndex = m_nSelIndex = -1;
	m_nHoverTeamIndex = m_nHoverIndex = -1;
	m_nHoverSmallIconIndex = -1;
	m_nPressTeamIndex = m_nPressIndex = -1;
	m_nLeft = m_nTop = 0;
	m_bMouseTracking = FALSE;
	m_bTransparent = FALSE;
	//m_nStyle = BLC_BIG_ICON_STYLE;
	m_bShowBigIconInSel = TRUE;
	m_nBuddyTeamHeight = 0;
	m_nBuddyItemHeightInBig = 0;
	m_nBuddyItemHeightInSmall = 0;
	m_nBuddyItemHeightInStd = 0;
	m_nBuddyTeamPadding = 0;
	m_nBuddyItemPadding = 0;
	m_rcMargin = CRect(0,0,0,0);

	for (int i = 0; i < (int)m_arrBuddyTeamItems.size(); i++)
	{
		CRecentTeamItem* lpTeamItem = m_arrBuddyTeamItems[i];
		if (lpTeamItem != NULL)
			delete lpTeamItem;
	}
	m_arrBuddyTeamItems.clear();
}

void CRecentListCtrl::DrawParentWndBg(HDC hDC)
{
	HWND hParentWnd = ::GetParent(m_hWnd);

	CRect rcWindow;
	GetWindowRect(&rcWindow);
	::ScreenToClient(hParentWnd, (LPPOINT)&rcWindow); 
	::ScreenToClient(hParentWnd, ((LPPOINT)&rcWindow)+1);

	::BitBlt(hDC, 0, 0, rcWindow.Width(), rcWindow.Height(), m_hBgDC, rcWindow.left, rcWindow.top, SRCCOPY);
}

void CRecentListCtrl::DrawBuddyTeam(HDC hDC, int nIndex)
{
	CRecentTeamItem* lpItem = GetBuddyTeamByIndex(nIndex);
	if (NULL == lpItem)
		return;

	CRect rcItem;
	GetItemRectByIndex(nIndex, -1, rcItem);

	int nArrowWidth = 12, nArrowHeight = 12;
	if (m_lpArrowImgN != NULL && !m_lpArrowImgN->IsNull())
	{
		nArrowWidth = m_lpArrowImgN->GetWidth();
		nArrowHeight = m_lpArrowImgN->GetHeight();
		//if (m_nType == BUDDY_LIST_TYPE_RECENT)
		//{
		//	nArrowWidth = 0;
		//	nArrowHeight = 0;
		//}
	}

	CRect rcArrow;
	CalcCenterRect(rcItem, nArrowWidth, nArrowHeight, rcArrow);
	rcArrow.left = rcItem.left + 2;
	rcArrow.right = rcArrow.left + nArrowWidth;

	CRect rcText(rcItem);
	rcText.left = rcArrow.right + 6;

	if (m_nSelTeamIndex == nIndex && m_nSelIndex == -1)
	{
		if (!lpItem->m_bExpand)
		{
			if (m_lpArrowImgS != NULL && !m_lpArrowImgS->IsNull())
				m_lpArrowImgS->Draw2(hDC, rcArrow);
		}
		else
		{
			if (m_lpExpArrowImgS != NULL && !m_lpExpArrowImgS->IsNull())
				m_lpExpArrowImgS->Draw2(hDC, rcArrow);
		}
	}
	else if (m_nHoverTeamIndex == nIndex && m_nHoverIndex == -1)
	{
		//if (m_lpBuddyTeamBgImgH != NULL && !m_lpBuddyTeamBgImgH->IsNull() && m_nType != BUDDY_LIST_TYPE_RECENT)
		//	m_lpBuddyTeamBgImgH->Draw2(hDC, rcItem);

		if (!lpItem->m_bExpand)
		{
			if (m_lpArrowImgH != NULL && !m_lpArrowImgH->IsNull())
				m_lpArrowImgH->Draw2(hDC, rcArrow);
		}
		else
		{
			if (m_lpExpArrowImgH != NULL && !m_lpExpArrowImgH->IsNull())
				m_lpExpArrowImgH->Draw2(hDC, rcArrow);
		}
	}
	else
	{
		if (!lpItem->m_bExpand)
		{
			if (m_lpArrowImgN != NULL && !m_lpArrowImgN->IsNull())
				m_lpArrowImgN->Draw2(hDC, rcArrow);
		}
		else
		{
			//if (m_lpExpArrowImgN != NULL && !m_lpExpArrowImgN->IsNull() && m_nType != BUDDY_LIST_TYPE_RECENT)
			//	m_lpExpArrowImgN->Draw2(hDC, rcArrow);
		}
	}

	BOOL bShowText = TRUE;
	if (!lpItem->m_bExpand && lpItem->m_nHeadFlashAnim > 0)
	{
		if (1 == lpItem->m_nHeadFlashAnimState)
			bShowText = FALSE;
	}

	if (bShowText)
	{
		CString strText;
		if (lpItem->m_nCurCnt != -1)
			strText.Format(_T("%s [%d/%d]"), lpItem->m_strName, lpItem->m_nCurCnt, lpItem->m_nMaxCnt);
		else
			strText.Format(_T("%s [%d]"), lpItem->m_strName, lpItem->m_nMaxCnt);

		UINT nFormat = DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;

		::SetTextColor(hDC, RGB(0, 0, 0));
		int nMode = ::SetBkMode(hDC, TRANSPARENT);
		HFONT hFont = (HFONT)SendMessage(WM_GETFONT, 0, 0);
		if (NULL == hFont)
			hFont = (HFONT)::GetStockObject(DEFAULT_GUI_FONT);
		HFONT hOldFont = (HFONT)::SelectObject(hDC, hFont);
		//if (m_nType == BUDDY_LIST_TYPE_RECENT)
		//{
			strText = _T("");
		//}
		::DrawText(hDC, strText, strText.GetLength(), &rcText, nFormat);
		::SelectObject(hDC, hOldFont);
		::SetBkMode(hDC, nMode);
	}
}

void CRecentListCtrl::DrawBuddyItem(HDC hDC, int nTeamIndex, int nIndex)
{
	int nMode = ::SetStretchBltMode(hDC, HALFTONE);
	//if (BLC_BIG_ICON_STYLE == m_nStyle)
		DrawBuddyItemInBigIcon(hDC, nTeamIndex, nIndex);
	//else if (BLC_SMALL_ICON_STYLE == m_nStyle)
		//DrawBuddyItemInSmallIcon(hDC, nTeamIndex, nIndex);
	//else if (BLC_STANDARD_ICON_STYLE == m_nStyle)
		//DrawBuddyItemInStdIcon(hDC, nTeamIndex, nIndex);
	//::SetStretchBltMode(hDC, nMode);
}

void CRecentListCtrl::DrawBuddyItemInBigIcon(HDC hDC, int nTeamIndex, int nIndex)
{
	CRecentItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (NULL == lpItem)
		return;

	CRect rcItem;
	GetItemRectByIndex(nTeamIndex, nIndex, rcItem);
	
	HPEN hPenLine = ::CreatePen(PS_SOLID, 0, RGB(232,235,227));
	HPEN hOldPen = (HPEN)::SelectObject(hDC, (HGDIOBJ)hPenLine);

	//绘制上分割线
	//::MoveToEx(hDC, rcItem.left, rcItem.top, NULL);
	//::LineTo(hDC, rcItem.right, rcItem.top);
	//绘制下分割线
	::MoveToEx(hDC, rcItem.left, rcItem.bottom, NULL);
	::LineTo(hDC, rcItem.right, rcItem.bottom);

	::SelectObject(hDC, hOldPen);
	::DeleteObject(hPenLine);

	int nHeadWidth = 40, nHeadHeight = 40;

	CRect rcHead, rcMobile;
	CalcCenterRect(rcItem, nHeadWidth, nHeadHeight, rcHead);
	rcHead.left = rcItem.left + 6;
	rcHead.right = rcHead.left + nHeadWidth;

	int nHeadRight = rcHead.right;

	
	if (m_nSelTeamIndex == nTeamIndex && m_nSelIndex == nIndex)				// 选中状态
	{
		if (m_lpBuddyItemBgImgS != NULL && !m_lpBuddyItemBgImgS->IsNull())
			m_lpBuddyItemBgImgS->Draw2(hDC, rcItem);
	}
	else if (m_nHoverTeamIndex == nTeamIndex && m_nHoverIndex == nIndex)	// 高亮状态
	{
		if (m_lpBuddyItemBgImgH != NULL && !m_lpBuddyItemBgImgH->IsNull())
			m_lpBuddyItemBgImgH->Draw2(hDC, rcItem);
	}
	else
	{

	}

	if (lpItem->m_lpHeadImg != NULL && !lpItem->m_lpHeadImg->IsNull())	// 绘制头像
		lpItem->m_lpHeadImg->Draw2(hDC, rcHead);

	
	if (lpItem->m_lpMobileImg != NULL && !lpItem->m_lpMobileImg->IsNull())	// 绘制手机图标
	{
		rcMobile.right = rcHead.right;
		rcMobile.left = rcMobile.right-16;
		rcMobile.bottom = rcHead.bottom;
		rcMobile.top = rcMobile.bottom-16;
		lpItem->m_lpMobileImg->Draw2(hDC, rcMobile);
	}
	

	if (m_nSelTeamIndex == nTeamIndex && m_nSelIndex == nIndex)
	{
		CRect rcHeadFrame(rcHead);
		rcHeadFrame.left -= 3;
		rcHeadFrame.right += 3;
		rcHeadFrame.top -= 3;
		rcHeadFrame.bottom += 3;

		if (m_lpHeadFrameImg != NULL && !m_lpHeadFrameImg->IsNull())	// 绘制头像焦点框
			m_lpHeadFrameImg->Draw2(hDC, rcHeadFrame);
	}

	UINT nFormat = DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;

	int nMode = ::SetBkMode(hDC, TRANSPARENT);
	//HFONT hFont = (HFONT)SendMessage(WM_GETFONT, 0, 0);
	//if (NULL == hFont)
	//	hFont = (HFONT)::GetStockObject(DEFAULT_GUI_FONT);
	HFONT hOldFont = NULL;
	HFONT hFontNickName = CGDIFactory::GetFont(19);
	HFONT hFontDate = CGDIFactory::GetFont(16);
	HFONT hFontLastMsg = CGDIFactory::GetFont(18);

	CRect rcName1(0, 0, 0, 0);
	CRect rcName2(0, 0, 0, 0);
	CRect rcDate(0, 0, 0, 0);
	CRect rcLastMsg(0, 0, 0, 0);
	CRect rcNewMsgCount(0, 0, 0, 0);
	
	if(lpItem->m_strNickName.IsEmpty())
		lpItem->m_strNickName = _T(" ");
	
	//画昵称
	CRect rcText0(0,0,0,0);	// 计算文字宽高
	hOldFont = (HFONT)::SelectObject(hDC, hFontNickName);
	::DrawText(hDC, lpItem->m_strNickName, lpItem->m_strNickName.GetLength(), &rcText0, DT_SINGLELINE | DT_CALCRECT);

	rcName1.left = nHeadRight + 6;
	rcName1.right = rcName1.left + rcText0.Width();
	rcName1.top = rcItem.top + 6;
	rcName1.bottom = rcName1.top + rcText0.Height();
	if (rcName1.right > rcItem.right - 10)
		rcName1.right = rcItem.right - 10;

	::SetTextColor(hDC, RGB(0, 0, 0));
	::DrawText(hDC, lpItem->m_strNickName, lpItem->m_strNickName.GetLength(), &rcName1, nFormat);
	
	if(lpItem->m_strDate.IsEmpty())
		lpItem->m_strDate = _T(" ");
	//画日期
	CRect rcText1(0,0,0,0);	// 计算文字宽高
	hOldFont = (HFONT)::SelectObject(hDC, hFontDate);
	::DrawText(hDC, lpItem->m_strDate, lpItem->m_strDate.GetLength(), &rcText1, DT_SINGLELINE | DT_CALCRECT);
	
	rcDate.left = rcItem.right-10-rcText1.Width();	
	rcDate.right = rcItem.right-10; 
	rcDate.top = rcItem.top + 9;
	rcDate.bottom = rcDate.top + rcText1.Height();
	::SetTextColor(hDC,  RGB(127, 127, 127));
	::DrawText(hDC, lpItem->m_strDate, lpItem->m_strDate.GetLength(), &rcDate, nFormat);

	if(lpItem->m_strLastMsg.IsEmpty())
		lpItem->m_strDate = _T(" ");
	//画最近一条消息
	CRect rcText2(0,0,0,0);	// 计算文字宽高
	hOldFont = (HFONT)::SelectObject(hDC, hFontLastMsg);
	::DrawText(hDC, lpItem->m_strLastMsg, lpItem->m_strLastMsg.GetLength(), &rcText2, DT_SINGLELINE | DT_CALCRECT);

	rcLastMsg.left = nHeadRight + 6;
	rcLastMsg.right = rcLastMsg.left + rcText2.Width();
	rcLastMsg.top = rcName1.bottom + 7;
	rcLastMsg.bottom = rcLastMsg.top + rcText2.Height();
	if (rcLastMsg.right > rcItem.right - 10)
		rcLastMsg.right = rcItem.right - 10;

	::SetTextColor(hDC, RGB(127, 127, 127));
	::DrawText(hDC, lpItem->m_strLastMsg, lpItem->m_strLastMsg.GetLength(), &rcLastMsg, nFormat);

	//画新消息个数
	if(lpItem->m_nNewMsgCount > 0)
	{
		rcNewMsgCount.left = rcItem.right-26;
		rcNewMsgCount.right = rcNewMsgCount.left+16;
		rcNewMsgCount.top = rcLastMsg.top;
		rcNewMsgCount.bottom = rcNewMsgCount.top+16;
		Gdiplus::Graphics graphic(hDC);
		graphic.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		Gdiplus::Pen pen(Gdiplus::Color(255, 245, 108, 11), 1.0);
		Gdiplus::SolidBrush solidBrush(Gdiplus::Color(255,  245, 108, 11));
		graphic.DrawEllipse(&pen, rcNewMsgCount.left, rcNewMsgCount.top, rcNewMsgCount.Width(), rcNewMsgCount.Height());
		graphic.FillEllipse(&solidBrush, rcNewMsgCount.left, rcNewMsgCount.top, rcNewMsgCount.Width(), rcNewMsgCount.Height());
		CString strMsgCountDesc;
		long nMsgCount = lpItem->m_nNewMsgCount;
		if(nMsgCount > 99)
			strMsgCountDesc = _T("9+");
		else
			strMsgCountDesc.Format(_T("%d"), nMsgCount);
		::SetTextColor(hDC, RGB(255, 255, 255));
		::DrawText(hDC, strMsgCountDesc, strMsgCountDesc.GetLength(), &rcNewMsgCount, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
		//::DrawText(hDC, _T("9+"), 2, &rcNewMsgCount, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
	}

	::SelectObject(hDC, hOldFont);
	::SetBkMode(hDC, nMode);
}

BOOL CRecentListCtrl::StartTrackMouseLeave()
{
	TRACKMOUSEEVENT tme = { 0 };
	tme.cbSize = sizeof(tme);
	tme.dwFlags = TME_LEAVE;
	tme.hwndTrack = m_hWnd;
	return _TrackMouseEvent(&tme);
}

void CRecentListCtrl::CalcCenterRect(CRect& rcDest, int cx, int cy, CRect& rcCenter)
{
	int x = (rcDest.Width() - cx + 1) / 2;
	int y = (rcDest.Height() - cy + 1) / 2;

	rcCenter = CRect(rcDest.left+x, rcDest.top+y, rcDest.left+x+cx, rcDest.top+y+cy);
}

void CRecentListCtrl::HitTest(POINT pt, int& nTeamIndex, int& nIndex)
{
	CRecentTeamItem* lpTeamItem;
	CRecentItem* lpItem;
	int nLeft = m_rcMargin.left+m_nLeft, nTop = m_nTop;
	int nBuddyTeamWidth, nBuddyItemWidth, nBuddyItemHeight;
	CRect rcItem;

	nTeamIndex = -1;
	nIndex = -1;

	CRect rcClient;
	GetClientRect(&rcClient);

	int nVScrollBarWidth = 0;
	if (m_VScrollBar.IsVisible())
	{
		CRect rcVScrollBar;
		m_VScrollBar.GetRect(&rcVScrollBar);
		nVScrollBarWidth = rcVScrollBar.Width();
	}

	nBuddyTeamWidth = rcClient.Width() - m_rcMargin.left - m_rcMargin.right - nVScrollBarWidth;
	nBuddyItemWidth = nBuddyTeamWidth;
	//if (BLC_BIG_ICON_STYLE == m_nStyle)
		nBuddyItemHeight = m_nBuddyItemHeightInBig;
	//else if (BLC_SMALL_ICON_STYLE == m_nStyle)
	//	nBuddyItemHeight = m_nBuddyItemHeightInSmall;
	//else
	//	nBuddyItemHeight = m_nBuddyItemHeightInStd;

	for (int i = 0; i < (int)m_arrBuddyTeamItems.size(); i++)
	{
		lpTeamItem = m_arrBuddyTeamItems[i];
		if (lpTeamItem != NULL)
		{
			rcItem = CRect(nLeft, nTop, nLeft+nBuddyTeamWidth, nTop+m_nBuddyTeamHeight);
			if (rcItem.PtInRect(pt))
			{
				nTeamIndex = i;
				nIndex = -1;
				return;
			}

			nTop += m_nBuddyTeamHeight;
			nTop += m_nBuddyTeamPadding;

			if (lpTeamItem->m_bExpand)
			{
				for (int j = 0; j < (int)lpTeamItem->m_arrBuddys.size(); j++)
				{
					lpItem = lpTeamItem->m_arrBuddys[j];
					if (lpItem != NULL)
					{
						//if (BLC_SMALL_ICON_STYLE == m_nStyle && m_bShowBigIconInSel
						//	&& (m_nSelTeamIndex == i && m_nSelIndex == j))
						//	nBuddyItemHeight = m_nBuddyItemHeightInBig;

						rcItem = CRect(nLeft, nTop, nLeft+nBuddyItemWidth, nTop+nBuddyItemHeight);
						if (rcItem.PtInRect(pt))
						{
							nTeamIndex = i;
							nIndex = j;
							return;
						}

						nTop += nBuddyItemHeight;
						nTop += m_nBuddyItemPadding;

						//if (BLC_SMALL_ICON_STYLE == m_nStyle && m_bShowBigIconInSel
						//	&& (m_nSelTeamIndex == i && m_nSelIndex == j))
						//	nBuddyItemHeight = m_nBuddyItemHeightInSmall;
					}
				}
			}
		}
	}
}

CRecentTeamItem* CRecentListCtrl::GetBuddyTeamByIndex(int nIndex)
{
	if (nIndex >= 0 && nIndex < (int)m_arrBuddyTeamItems.size())
		return m_arrBuddyTeamItems[nIndex];
	else
		return NULL;
}

CRecentItem* CRecentListCtrl::GetBuddyItemByIndex(int nTeamIndex, int nIndex)
{
	CRecentTeamItem* lpTeamItem;

	if (nTeamIndex >= 0 && nTeamIndex < (int)m_arrBuddyTeamItems.size())
	{
		lpTeamItem = m_arrBuddyTeamItems[nTeamIndex];
		if (lpTeamItem != NULL)
		{
			if (nIndex >= 0 && nIndex < (int)lpTeamItem->m_arrBuddys.size())
				return lpTeamItem->m_arrBuddys[nIndex];
		}
	}
	return NULL;
}

void CRecentListCtrl::CheckScrollBarStatus()
{
	if (!IsWindow())
		return;

	CRecentTeamItem* lpTeamItem;
	int nLeft = m_rcMargin.left+m_nLeft, nTop = m_nTop;
	int nBuddyTeamWidth, nBuddyItemWidth, nBuddyItemHeight;
	CRect rcItem;

	CRect rcClient;
	GetClientRect(&rcClient);

	int nVScrollBarWidth = 0;
	if (m_VScrollBar.IsVisible())
	{
		CRect rcVScrollBar;
		m_VScrollBar.GetRect(&rcVScrollBar);
		nVScrollBarWidth = rcVScrollBar.Width();
	}

	nBuddyTeamWidth = rcClient.Width() - m_rcMargin.left - m_rcMargin.right - nVScrollBarWidth;
	nBuddyItemWidth = nBuddyTeamWidth;
	//if (BLC_BIG_ICON_STYLE == m_nStyle)
		nBuddyItemHeight = m_nBuddyItemHeightInBig;
	//else if (BLC_SMALL_ICON_STYLE == m_nStyle)
	//	nBuddyItemHeight = m_nBuddyItemHeightInSmall;
	//else
	//	nBuddyItemHeight = m_nBuddyItemHeightInStd;

	int nHeight = (m_nBuddyTeamHeight + m_nBuddyTeamPadding)* (int)m_arrBuddyTeamItems.size();

	for (int i = 0; i < (int)m_arrBuddyTeamItems.size(); i++)
	{
		lpTeamItem = m_arrBuddyTeamItems[i];
		if (lpTeamItem != NULL)
		{
			if (lpTeamItem->m_bExpand)
			{
				nHeight += ((nBuddyItemHeight + m_nBuddyItemPadding)* (int)lpTeamItem->m_arrBuddys.size());

				//if (BLC_SMALL_ICON_STYLE == m_nStyle && m_bShowBigIconInSel
				//	&& (m_nSelTeamIndex == i && m_nSelIndex != -1))
				//{
				//	nHeight -= nBuddyItemHeight;
				//	nHeight += m_nBuddyItemHeightInBig;
				//}
			}
		}
	}

	if (nHeight > rcClient.Height())
	{
		m_VScrollBar.SetScrollRange(nHeight - rcClient.Height());
		Scroll(0, 0);
		m_VScrollBar.SetVisible(TRUE);
	}
	else
	{
		m_nTop = 0;
		m_VScrollBar.SetScrollPos(0);
		m_VScrollBar.SetVisible(FALSE);
	}
}

void CRecentListCtrl::Scroll(int cx, int cy)
{
	if (m_VScrollBar.IsVisible() &&
		m_VScrollBar.IsEnabled())
	{
		int nPos = m_VScrollBar.GetScrollPos();
		m_VScrollBar.SetScrollPos(nPos + cy);
		nPos = m_VScrollBar.GetScrollPos();
		m_nTop = 0 - nPos;
	}
}

void CRecentListCtrl::EnsureVisible(int nTeamIndex, int nIndex)
{
	CRect rcClient;
	GetClientRect(&rcClient);

	CRect rcItem;
	GetItemRectByIndex(nTeamIndex, nIndex, rcItem);

	if (rcItem.top < rcClient.top || rcItem.bottom >= rcClient.bottom)
	{
		int cy = 0;
		if (rcItem.top < rcClient.top)
			cy = rcItem.top - rcClient.top;
		if (rcItem.bottom > rcClient.bottom)
			cy = rcItem.bottom - rcClient.bottom;
		Scroll(0, cy);
	}
}

void CRecentListCtrl::SortBuddyTeam(int nTeamIndex, int nIndex)
{
	//CRecentTeamItem* lpBuddyTeam = GetBuddyTeamByIndex(nTeamIndex);			// 获取要排序的分组
	//if (NULL == lpBuddyTeam)
	//	return;

	//int nItemCnt = (int)lpBuddyTeam->m_arrBuddys.size();					// 分组的好友数小于2则不用再排序
	//if (nItemCnt < 2)
	//	return;

	//CRecentItem* lpBuddyItem1 = GetBuddyItemByIndex(nTeamIndex, nIndex);	// 获取要排序的好友
	//if (NULL == lpBuddyItem1)
	//	return;

	//lpBuddyTeam->m_arrBuddys.erase(lpBuddyTeam->m_arrBuddys.begin()+nIndex);// 先删除

	//nItemCnt = (int)lpBuddyTeam->m_arrBuddys.size();

	//int nInsert = nItemCnt-1;
	//for (int i = 0; i < nItemCnt; i++)
	//{
	//	CRecentItem* lpBuddyItem2 = lpBuddyTeam->m_arrBuddys[i];
	//	if (lpBuddyItem2 != NULL)
	//	{
	//		if (lpBuddyItem1->m_bOnline > lpBuddyItem2->m_bOnline)
	//		{
	//			nInsert = i;
	//			break;
	//		}
	//		else if (lpBuddyItem1->m_bOnline == lpBuddyItem2->m_bOnline)
	//		{
	//			if (lpBuddyItem1->m_nID <= lpBuddyItem2->m_nID)
	//			{
	//				nInsert = i;
	//				break;
	//			}
	//		}
	//	}
	//}

	//lpBuddyTeam->m_arrBuddys.insert(lpBuddyTeam->m_arrBuddys.begin()+nInsert, lpBuddyItem1);
}

