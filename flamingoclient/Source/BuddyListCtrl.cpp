#include "stdafx.h"
#include "BuddyListCtrl.h"
#include "GDIFactory.h"

CBuddyItem::CBuddyItem(void)
{
	m_nID = 0;
	m_nDisplayMode = BLCTRL_DISPLAY_SHOW_NICKNAME_ACCOUNT;
	m_bShowSign = TRUE;
	m_bOnline = FALSE;
	m_bOnlineAnim = FALSE;
	m_nOnlineAnimState = 0;
	m_bOfflineAnim = FALSE;
	m_bHeadFlashAnim = FALSE;
	m_nHeadFlashAnimState = 0;
	m_lpHeadImg = NULL;
	m_bGender = FALSE;
	m_bMobile = FALSE;
	m_lpMobileImg = NULL;
}

CBuddyItem::~CBuddyItem(void)
{
	if (m_lpHeadImg != NULL)
	{
		delete m_lpHeadImg;
		m_lpHeadImg = NULL;
	}
	if(m_lpMobileImg != NULL)
	{
		delete m_lpMobileImg;
		m_lpMobileImg = NULL;
	}
}

CBuddyTeamItem::CBuddyTeamItem(void)
{
	m_nID = 0;
	m_nCurCnt = -1;
	m_nMaxCnt = 0;
	m_bExpand = FALSE;
	m_nHeadFlashAnim = 0;
	m_nHeadFlashAnimState = 0;
}

CBuddyTeamItem::~CBuddyTeamItem(void)
{
	for (int i = 0; i < (int)m_arrBuddys.size(); i++)
	{
		CBuddyItem* lpItem = m_arrBuddys[i];
		if (lpItem != NULL)
			delete lpItem;
	}
	m_arrBuddys.clear();
}

CBuddyListCtrl::CBuddyListCtrl(void)
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
	m_nStyle = BLC_BIG_ICON_STYLE;
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

CBuddyListCtrl::~CBuddyListCtrl(void)
{
}

BOOL CBuddyListCtrl::SetBgPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpBgImg);
	m_lpBgImg = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpBgImg != NULL) ? TRUE : FALSE;
}

BOOL CBuddyListCtrl::SetBuddyTeamHotBgPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpBuddyTeamBgImgH);
	m_lpBuddyTeamBgImgH = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpBuddyTeamBgImgH != NULL) ? TRUE : FALSE;
}

BOOL CBuddyListCtrl::SetBuddyItemHotBgPic(LPCTSTR lpszFileName, RECT* lpNinePart/* = NULL*/)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpBuddyItemBgImgH);
	m_lpBuddyItemBgImgH = CSkinManager::GetInstance()->GetImage(lpszFileName);
	if (m_lpBuddyItemBgImgH != NULL)
		m_lpBuddyItemBgImgH->SetNinePart(lpNinePart);
	return (m_lpBuddyItemBgImgH != NULL) ? TRUE : FALSE;
}

BOOL CBuddyListCtrl::SetBuddyItemSelBgPic(LPCTSTR lpszFileName, RECT* lpNinePart/* = NULL*/)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpBuddyItemBgImgS);
	m_lpBuddyItemBgImgS = CSkinManager::GetInstance()->GetImage(lpszFileName);
	if (m_lpBuddyItemBgImgS != NULL)
		m_lpBuddyItemBgImgS->SetNinePart(lpNinePart);
	return (m_lpBuddyItemBgImgS != NULL) ? TRUE : FALSE;
}

BOOL CBuddyListCtrl::SetHeadFramePic(LPCTSTR lpszFileName, RECT* lpNinePart/* = NULL*/)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpHeadFrameImg);
	m_lpHeadFrameImg = CSkinManager::GetInstance()->GetImage(lpszFileName);
	if (m_lpHeadFrameImg != NULL)
		m_lpHeadFrameImg->SetNinePart(lpNinePart);
	return (m_lpHeadFrameImg != NULL) ? TRUE : FALSE;
}

BOOL CBuddyListCtrl::SetNormalArrowPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpArrowImgN);
	m_lpArrowImgN = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpArrowImgN != NULL) ? TRUE : FALSE;
}

BOOL CBuddyListCtrl::SetHotArrowPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpArrowImgH);
	m_lpArrowImgH = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpArrowImgH != NULL) ? TRUE : FALSE;
}

BOOL CBuddyListCtrl::SetSelArrowPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpArrowImgS);
	m_lpArrowImgS = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpArrowImgS != NULL) ? TRUE : FALSE;
}

BOOL CBuddyListCtrl::SetNormalExpArrowPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpExpArrowImgN);
	m_lpExpArrowImgN = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpExpArrowImgN != NULL) ? TRUE : FALSE;
}

BOOL CBuddyListCtrl::SetHotExpArrowPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpExpArrowImgH);
	m_lpExpArrowImgH = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpExpArrowImgH != NULL) ? TRUE : FALSE;
}

BOOL CBuddyListCtrl::SetSelExpArrowPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpExpArrowImgS);
	m_lpExpArrowImgS = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpExpArrowImgS != NULL) ? TRUE : FALSE;
}

BOOL CBuddyListCtrl::SetStdGGHeadPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpStdGGHeadImg);
	m_lpStdGGHeadImg = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpStdGGHeadImg != NULL) ? TRUE : FALSE;
}

BOOL CBuddyListCtrl::SetStdMMHeadPic(LPCTSTR lpszFileName)
{
	CSkinManager::GetInstance()->ReleaseImage(m_lpStdMMHeadImg);
	m_lpStdMMHeadImg = CSkinManager::GetInstance()->GetImage(lpszFileName);
	return (m_lpStdMMHeadImg != NULL) ? TRUE : FALSE;
}

void CBuddyListCtrl::SetStyle(BLCTRL_STYLE nStyle)
{
	m_nStyle = nStyle;
}

void CBuddyListCtrl::SetShowBigIconInSel(BOOL bShowBigIconInSel)
{
	m_bShowBigIconInSel = bShowBigIconInSel;
}

void CBuddyListCtrl::SetBuddyTeamHeight(int nHeight)
{
	m_nBuddyTeamHeight = nHeight;
}

void CBuddyListCtrl::SetBuddyItemHeightInBigIcon(int nHeight)
{
	m_nBuddyItemHeightInBig = nHeight;
}

void CBuddyListCtrl::SetBuddyItemHeightInSmallIcon(int nHeight)
{
	m_nBuddyItemHeightInSmall = nHeight;
}

void CBuddyListCtrl::SetBuddyItemHeightInStandardIcon(int nHeight)
{
	m_nBuddyItemHeightInStd = nHeight;
}

void CBuddyListCtrl::SetBuddyTeamPadding(int nPadding)
{
	m_nBuddyTeamPadding = nPadding;
}

void CBuddyListCtrl::SetBuddyItemPadding(int nPadding)
{
	m_nBuddyItemPadding = nPadding;
}

void CBuddyListCtrl::SetMargin(const RECT* lpMargin)
{
	if (lpMargin != NULL)
		m_rcMargin =*lpMargin;
}

int CBuddyListCtrl::AddBuddyTeam(int nID)
{
	CBuddyTeamItem* lpItem = new CBuddyTeamItem;
	if (NULL == lpItem)
		return -1;
	lpItem->m_nID = nID;
	m_arrBuddyTeamItems.push_back(lpItem);
	return m_arrBuddyTeamItems.size() - 1;
}

int CBuddyListCtrl::InsertBuddyTeam(int nTeamIndex, int nID)
{
	return -1;
}

BOOL CBuddyListCtrl::DelBuddyTeam(int nTeamIndex)
{
	if (nTeamIndex >= 0 && nTeamIndex < (int)m_arrBuddyTeamItems.size())
	{
		CBuddyTeamItem* lpBuddyTeamItem = m_arrBuddyTeamItems[nTeamIndex];
		if (lpBuddyTeamItem != NULL)
			delete lpBuddyTeamItem;
		m_arrBuddyTeamItems.erase(m_arrBuddyTeamItems.begin()+nTeamIndex);
		return TRUE;
	}
	return FALSE;
}

int CBuddyListCtrl::AddBuddyItem(int nTeamIndex, int nID)
{
	CBuddyTeamItem* lpTeamItem = GetBuddyTeamByIndex(nTeamIndex);
	if (NULL == lpTeamItem)
		return -1;
	CBuddyItem* lpItem = new CBuddyItem;
	if (NULL == lpItem)
		return -1;
	lpItem->m_nID = nID;
	lpTeamItem->m_arrBuddys.push_back(lpItem);
	return lpTeamItem->m_arrBuddys.size() - 1;
}

int CBuddyListCtrl::InsertBuddyItem(int nTeamIndex, int nIndex, int nID)
{
	return -1;
}

BOOL CBuddyListCtrl::DelBuddyItem(int nTeamIndex, int nIndex)
{
	if (nTeamIndex >= 0 && nTeamIndex < (int)m_arrBuddyTeamItems.size())
	{
		CBuddyTeamItem* lpTeamItem = m_arrBuddyTeamItems[nTeamIndex];
		if (lpTeamItem != NULL)
		{
			if (nIndex >= 0 && nIndex < (int)lpTeamItem->m_arrBuddys.size())
			{
				CBuddyItem* lpBuddyItem = lpTeamItem->m_arrBuddys[nIndex];
				if (lpBuddyItem != NULL)
					delete lpBuddyItem;
				lpTeamItem->m_arrBuddys.erase(lpTeamItem->m_arrBuddys.begin()+nIndex);
				return TRUE;
			}
		}
	}
	return FALSE;
}

void CBuddyListCtrl::DelAllItems()
{
	for (int i = 0; i < (int)m_arrBuddyTeamItems.size(); i++)
	{
		CBuddyTeamItem* lpTeamItem = m_arrBuddyTeamItems[i];
		if (lpTeamItem != NULL)
			delete lpTeamItem;
	}
	m_arrBuddyTeamItems.clear();
}

void CBuddyListCtrl::SetBuddyTeamID(int nTeamIndex, int nID)
{
	CBuddyTeamItem* lpItem = GetBuddyTeamByIndex(nTeamIndex);
	if (lpItem != NULL)
		lpItem->m_nID = nID;
}

void CBuddyListCtrl::SetBuddyTeamName(int nTeamIndex, LPCTSTR lpszText)
{
	CBuddyTeamItem* lpItem = GetBuddyTeamByIndex(nTeamIndex);
	if (lpItem != NULL)
		lpItem->m_strName = lpszText;
}

void CBuddyListCtrl::SetBuddyTeamMaxCnt(int nTeamIndex, int nMaxCnt)
{
	CBuddyTeamItem* lpItem = GetBuddyTeamByIndex(nTeamIndex);
	if (lpItem != NULL)
		lpItem->m_nMaxCnt = nMaxCnt;
}
// 更新好友列表在线人数
void CBuddyListCtrl::SetBuddyTeamCurCnt(int nTeamIndex, int nCurCnt)
{
	CBuddyTeamItem* lpItem = GetBuddyTeamByIndex(nTeamIndex);
	if (lpItem != NULL)
		lpItem->m_nCurCnt = nCurCnt;
}

void CBuddyListCtrl::SetBuddyTeamExpand(int nTeamIndex, BOOL bExpand)
{
	CBuddyTeamItem* lpItem = GetBuddyTeamByIndex(nTeamIndex);
	if (lpItem != NULL)
		lpItem->m_bExpand = bExpand;
}

int CBuddyListCtrl::GetBuddyTeamID(int nTeamIndex)
{
	CBuddyTeamItem* lpItem = GetBuddyTeamByIndex(nTeamIndex);
	if (lpItem != NULL)
		return lpItem->m_nID;
	else
		return 0;
}

CString CBuddyListCtrl::GetBuddyTeamName(int nTeamIndex)
{
	CBuddyTeamItem* lpItem = GetBuddyTeamByIndex(nTeamIndex);
	if (lpItem != NULL)
		return lpItem->m_strName;
	else
		return _T("");
}

int CBuddyListCtrl::GetBuddyTeamMaxCnt(int nTeamIndex)
{
	CBuddyTeamItem* lpItem = GetBuddyTeamByIndex(nTeamIndex);
	if (lpItem != NULL)
		return lpItem->m_nMaxCnt;
	else
		return 0;
}

int CBuddyListCtrl::GetBuddyTeamCurCnt(int nTeamIndex)
{
	CBuddyTeamItem* lpItem = GetBuddyTeamByIndex(nTeamIndex);
	if (lpItem != NULL)
		return lpItem->m_nCurCnt;
	else
		return 0;
}

BOOL CBuddyListCtrl::IsBuddyTeamExpand(int nTeamIndex)
{
	CBuddyTeamItem* lpItem = GetBuddyTeamByIndex(nTeamIndex);
	if (lpItem != NULL)
		return lpItem->m_bExpand;
	else
		return FALSE;
}

void CBuddyListCtrl::SetBuddyItemID(int nTeamIndex, int nIndex, int nID)
{
	CBuddyItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (lpItem != NULL)
		lpItem->m_nID = nID;
}

void CBuddyListCtrl::SetBuddyItemMode(int nTeamIndex, int nIndex, BLCTRL_DISPLAY_MODE nMode)
{
	CBuddyItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (lpItem != NULL)
		lpItem->m_nDisplayMode = nMode;
}

void CBuddyListCtrl::SetBuddyItemUTalkNum(int nTeamIndex, int nIndex, LPCTSTR lpszText)
{
	CBuddyItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (lpItem != NULL)
		lpItem->m_strUTalkNum = lpszText;
}

void CBuddyListCtrl::SetBuddyItemNickName(int nTeamIndex, int nIndex, LPCTSTR lpszText)
{
	CBuddyItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (lpItem != NULL)
	{
		lpItem->m_strNickName = lpszText;
	}
}

void CBuddyListCtrl::SetBuddyItemMarkName(int nTeamIndex, int nIndex, LPCTSTR lpszText)
{
	CBuddyItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (lpItem != NULL)
	{
		lpItem->m_strMarkName = lpszText;
	}
}

void CBuddyListCtrl::SetBuddyItemSign(int nTeamIndex, int nIndex, LPCTSTR lpszText, BOOL bShowSign)
{
	CBuddyItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (lpItem != NULL)
	{
		lpItem->m_strSign = lpszText;
		lpItem->m_bShowSign = bShowSign;
	}
}

void CBuddyListCtrl::SetBuddyItemGender(int nTeamIndex, int nIndex, BOOL bGender)
{
	CBuddyItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (lpItem != NULL)
		lpItem->m_bGender = bGender;
}

BOOL CBuddyListCtrl::SetBuddyItemHeadPic(int nTeamIndex, int nIndex, LPCTSTR lpszFileName, BOOL bGray)
{
	CBuddyItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (NULL == lpItem)
		return FALSE;

	if (lpItem->m_lpHeadImg != NULL)
		delete lpItem->m_lpHeadImg;

	lpItem->m_lpHeadImg = new CImageEx;
	if (NULL == lpItem->m_lpHeadImg)
		return FALSE;

    lpItem->m_strHeadImgName = lpszFileName;

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

CString CBuddyListCtrl::GetBuddyItemHeadPic(int nTeamIndex, int nIndex)
{
    CBuddyItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
    if (NULL == lpItem)
        return _T("");

    return lpItem->m_strHeadImgName;
}

BOOL CBuddyListCtrl::SetBuddyItemMobilePic(int nTeamIndex, int nIndex, LPCTSTR lpszFileName, BOOL bShow/*=TRUE*/)
{
	CBuddyItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
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

	lpItem->m_bMobile = bShow;

	if (IsWindowVisible())
		Invalidate();

	return TRUE;
}

void CBuddyListCtrl::SetBuddyItemHeadFlashAnim(int nTeamIndex, int nIndex, BOOL bHeadFlashAnim)
{
	CBuddyItem* lpBuddyItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (NULL == lpBuddyItem || bHeadFlashAnim == lpBuddyItem->m_bHeadFlashAnim)
		return;

	lpBuddyItem->m_bHeadFlashAnim = bHeadFlashAnim;
	lpBuddyItem->m_nHeadFlashAnimState = 0;
	if (bHeadFlashAnim)
	{
		CBuddyTeamItem* lpBuddyTeam = GetBuddyTeamByIndex(nTeamIndex);
		if (lpBuddyTeam != NULL)
		{
			lpBuddyTeam->m_nHeadFlashAnim++;
		}

		if (NULL == m_dwHeadFlashAnimTimerId)	// 启动头像闪动动画计时器
			m_dwHeadFlashAnimTimerId = SetTimer(2, 250, NULL);
	}
	else
	{
		CBuddyTeamItem* lpBuddyTeam = GetBuddyTeamByIndex(nTeamIndex);
		if (lpBuddyTeam != NULL)
		{
			lpBuddyTeam->m_nHeadFlashAnim--;
			if (lpBuddyTeam->m_nHeadFlashAnim <= 0)
				lpBuddyTeam->m_nHeadFlashAnimState = 0;
		}

		BOOL bHasAnim = FALSE;

		int nTeamCnt = GetBuddyTeamCount();
		for (int i = 0; i < nTeamCnt; i++)
		{
			lpBuddyTeam = GetBuddyTeamByIndex(i);
			if (lpBuddyTeam != NULL && lpBuddyTeam->m_nHeadFlashAnim > 0)
			{
				bHasAnim = TRUE;
				break;
			}
		}

		if (!bHasAnim)
		{
			KillTimer(m_dwHeadFlashAnimTimerId);
			m_dwHeadFlashAnimTimerId = NULL;

			if (::IsWindowVisible(m_hWnd))
				Invalidate();
		}
	}
}

void CBuddyListCtrl::SetBuddyItemOnline(int nTeamIndex, int nIndex, BOOL bOnline, BOOL bAnim)
{
	CBuddyItem* lpBuddyItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (NULL == lpBuddyItem || bOnline == lpBuddyItem->m_bOnline)
		return;

	lpBuddyItem->m_bOnline = bOnline;
	if (bAnim)	// 显示动画
	{
		if (bOnline)	// 上线动画
		{
			lpBuddyItem->m_bOfflineAnim = FALSE;

			lpBuddyItem->m_bOnlineAnim = TRUE;
			lpBuddyItem->m_nOnlineAnimState = 0;

			if (NULL == m_dwOnlineAnimTimerId)	// 启动上线动画计时器
				m_dwOnlineAnimTimerId = SetTimer(3, 250, NULL);
		}
		else			// 下线动画
		{
			lpBuddyItem->m_bOnlineAnim = FALSE;
			lpBuddyItem->m_nOnlineAnimState = 0;

			lpBuddyItem->m_bOfflineAnim = TRUE;

			if (NULL == m_dwOfflineAnimTimerId)	// 启动下线动画计时器
				m_dwOfflineAnimTimerId = SetTimer(4, 1000, NULL);
		}
	}
}

int CBuddyListCtrl::GetBuddyItemID(int nTeamIndex, int nIndex)
{
	CBuddyItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (lpItem != NULL)
		return lpItem->m_nID;
	else
		return 0;
}

BLCTRL_DISPLAY_MODE CBuddyListCtrl::GetBuddyItemMode(int nTeamIndex, int nIndex)
{
	CBuddyItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (lpItem != NULL)
		return lpItem->m_nDisplayMode;
	else
		return BLCTRL_DISPLAY_SHOW_NICKNAME_ACCOUNT;
}

CString CBuddyListCtrl::GetBuddyItemUTalkNum(int nTeamIndex, int nIndex)
{
	CBuddyItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (lpItem != NULL)
		return lpItem->m_strUTalkNum;
	else
		return _T("");
}

CString CBuddyListCtrl::GetBuddyItemNickName(int nTeamIndex, int nIndex)
{
	CBuddyItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (lpItem != NULL)
		return lpItem->m_strNickName;
	else
		return _T("");
}

CString CBuddyListCtrl::GetBuddyItemMarkName(int nTeamIndex, int nIndex)
{
	CBuddyItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (lpItem != NULL)
		return lpItem->m_strMarkName;
	else
		return _T("");
}

CString CBuddyListCtrl::GetBuddyItemSign(int nTeamIndex, int nIndex)
{
	CBuddyItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (lpItem != NULL)
		return lpItem->m_strSign;
	else
		return _T("");
}

BOOL CBuddyListCtrl::GetBuddyItemGender(int nTeamIndex, int nIndex)
{
	CBuddyItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (lpItem != NULL)
		return lpItem->m_bGender;
	else
		return FALSE;
}

BOOL CBuddyListCtrl::IsBuddyItemHasMsg(int nTeamIndex, int nIndex)
{
	CBuddyItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (lpItem != NULL)
		return lpItem->m_bHeadFlashAnim;
	else
		return FALSE;
}

int CBuddyListCtrl::GetBuddyTeamCount()
{
	return m_arrBuddyTeamItems.size();
}

int CBuddyListCtrl::GetBuddyItemCount(int nTeamIndex)
{
	CBuddyTeamItem* lpTeamItem = GetBuddyTeamByIndex(nTeamIndex);
	if (lpTeamItem != NULL)
		return lpTeamItem->m_arrBuddys.size();
	else
		return 0;
}

BOOL CBuddyListCtrl::GetItemIndexByID(int nID, int& nTeamIndex, int& nIndex)
{
	nTeamIndex = -1;
	nIndex = -1;

	for (int i = 0; i < (int)m_arrBuddyTeamItems.size(); i++)
	{
		CBuddyTeamItem* lpTeamItem = m_arrBuddyTeamItems[i];
		if (lpTeamItem != NULL)
		{
			if (nID == lpTeamItem->m_nID)
			{
				nTeamIndex = i;
				return TRUE;
			}

			for (int j = 0; j < (int)lpTeamItem->m_arrBuddys.size(); j++)
			{
				CBuddyItem* lpItem = lpTeamItem->m_arrBuddys[j];
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

BOOL CBuddyListCtrl::GetItemRectByIndex(int nTeamIndex, int nIndex, CRect& rect)
{
	CBuddyTeamItem* lpTeamItem;
	CBuddyItem* lpItem;
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
	if (BLC_BIG_ICON_STYLE == m_nStyle)
		nBuddyItemHeight = m_nBuddyItemHeightInBig;
	else if (BLC_SMALL_ICON_STYLE == m_nStyle)
		nBuddyItemHeight = m_nBuddyItemHeightInSmall;
	else
		nBuddyItemHeight = m_nBuddyItemHeightInStd;

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
						if (BLC_SMALL_ICON_STYLE == m_nStyle && m_bShowBigIconInSel
							&& (m_nSelTeamIndex == i && m_nSelIndex == j))
							nBuddyItemHeight = m_nBuddyItemHeightInBig;

						if (i == nTeamIndex && j == nIndex)
						{
							rect = CRect(nLeft, nTop, nLeft+nBuddyItemWidth, nTop+nBuddyItemHeight);
							return TRUE;
						}
						nTop += nBuddyItemHeight;
						nTop += m_nBuddyItemPadding;

						if (BLC_SMALL_ICON_STYLE == m_nStyle && m_bShowBigIconInSel
							&& (m_nSelTeamIndex == i && m_nSelIndex == j))
							nBuddyItemHeight = m_nBuddyItemHeightInSmall;
					}
				}
			}
		}
	}

	return FALSE;
}

void CBuddyListCtrl::GetCurSelIndex(int& nTeamIndex, int& nIndex)
{
	nTeamIndex = m_nSelTeamIndex;
	nIndex = m_nSelIndex;
}

BLCTRL_STYLE CBuddyListCtrl::GetStyle()
{
	return m_nStyle;
}

BOOL CBuddyListCtrl::IsShowBigIconInSel()
{
	return m_bShowBigIconInSel;
}

void CBuddyListCtrl::SetTransparent(BOOL bTransparent, HDC hBgDC)
{
	m_bTransparent = bTransparent;
	m_hBgDC = hBgDC;
}

int CBuddyListCtrl::OnCreate(LPCREATESTRUCT lpCreateStruct)
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

BOOL CBuddyListCtrl::OnEraseBkgnd(CDCHandle dc)
{
	return TRUE;
	//return FALSE;
}

void CBuddyListCtrl::OnPaint(CDCHandle dc)
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

	CBuddyTeamItem* lpTeamItem;
	for (int i = 0; i < (int)m_arrBuddyTeamItems.size(); i++)
	{
		DrawBuddyTeam(MemDC.m_hDC, i);

		lpTeamItem = m_arrBuddyTeamItems[i];
		if (lpTeamItem != NULL && lpTeamItem->m_bExpand)
		{
			for (int j = 0; j < (int)lpTeamItem->m_arrBuddys.size(); j++)
			{
				DrawBuddyItem(MemDC.m_hDC, i, j);
			}
		}
	}

	m_VScrollBar.OnPaint(MemDC.m_hDC);
}

void CBuddyListCtrl::OnLButtonDblClk(UINT nFlags, CPoint point)
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

void CBuddyListCtrl::OnLButtonDown(UINT nFlags, CPoint point)
{
	SetMsgHandled(FALSE);

	SetFocus();

	int nTeamIndex = -1, nIndex = -1;
	HitTest(point, nTeamIndex, nIndex);

	m_nPressTeamIndex = nTeamIndex;
	m_nPressIndex = nIndex;

	m_VScrollBar.OnLButtonDown(nFlags, point);
}

void CBuddyListCtrl::OnLButtonUp(UINT nFlags, CPoint point)
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

void CBuddyListCtrl::OnRButtonUp(UINT nFlags, CPoint point)
{
	SetMsgHandled(FALSE);

	SetFocus();

	int nTeamIndex = -1, nIndex = -1;
	HitTest(point, nTeamIndex, nIndex);

	m_nSelTeamIndex = nTeamIndex;
	m_nSelIndex = nIndex;

	Invalidate();

	BLNMHDREx stNmhdr;
	stNmhdr.hdr.hwndFrom = m_hWnd;
	stNmhdr.hdr.idFrom = GetDlgCtrlID();
	stNmhdr.hdr.code = NM_RCLICK;
	
	if(nTeamIndex!=-1 && nIndex!=-1)
		stNmhdr.nPostionFlag = POSITION_ON_ITEM;
	else if(nTeamIndex!=-1 && nIndex==-1)
		stNmhdr.nPostionFlag = POSITION_ON_TEAM;
	else if(nTeamIndex==-1 && nIndex==-1)
		stNmhdr.nPostionFlag = POSITION_ON_BLANK;

	::SendMessage(::GetParent(m_hWnd), WM_NOTIFY, GetDlgCtrlID(), (LPARAM)&stNmhdr);
}

void CBuddyListCtrl::OnMouseMove(UINT nFlags, CPoint point)
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


        CRect rtItem;
        GetItemRectByIndex(nTeamIndex, nIndex, rtItem);
        //告诉父窗口，鼠标在项上悬停
        BLNMHDREx stNmhdr;
        stNmhdr.hdr.hwndFrom = m_hWnd;
        stNmhdr.hdr.idFrom = GetDlgCtrlID();
        stNmhdr.hdr.code = NM_HOVER;
        stNmhdr.nPostionFlag = POSITION_ON_ITEM;  
        stNmhdr.nTeamIndex = nTeamIndex;
        stNmhdr.nItemIndex = nIndex;
        stNmhdr.rtItem = rtItem;

        ::SendMessage(::GetParent(m_hWnd), WM_NOTIFY, GetDlgCtrlID(), (LPARAM)&stNmhdr);
	}

	m_VScrollBar.OnMouseMove(nFlags, point);
}

void CBuddyListCtrl::OnMouseLeave()
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

    //告诉父窗口，鼠标在项外（非分组和项元素以外的地方）悬停
    BLNMHDREx stNmhdr;
    stNmhdr.hdr.hwndFrom = m_hWnd;
    stNmhdr.hdr.idFrom = GetDlgCtrlID();
    stNmhdr.hdr.code = NM_HOVER;
    stNmhdr.nPostionFlag = POSITION_ON_BLANK;
    stNmhdr.nTeamIndex = -1;
    stNmhdr.nItemIndex = -1;

    ::SendMessage(::GetParent(m_hWnd), WM_NOTIFY, GetDlgCtrlID(), (LPARAM)&stNmhdr);
}

void CBuddyListCtrl::OnTimer(UINT_PTR nIDEvent)
{
	m_VScrollBar.OnTimer(nIDEvent);

	if (nIDEvent == m_dwHeadFlashAnimTimerId)		// 头像闪动动画
	{
		OnTimer_HeadFlashAnim(nIDEvent);
	}
	else if (nIDEvent == m_dwOnlineAnimTimerId)		// 上线动画
	{
		OnTimer_OnlineAnim(nIDEvent);
	}
	else if (nIDEvent == m_dwOfflineAnimTimerId)	// 下线动画
	{
		OnTimer_OfflineAnim(nIDEvent);
	}
}

void CBuddyListCtrl::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar pScrollBar)
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

BOOL CBuddyListCtrl::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	int nLineSize = 100;

	if (zDelta < 0)		// SB_LINEDOWN
		Scroll(0, nLineSize);
	else	// SB_LINEUP
		Scroll(0, -nLineSize);

	Invalidate();

	return TRUE;
}

void CBuddyListCtrl::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
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

void CBuddyListCtrl::OnSize(UINT nType, CSize size)
{
	SetMsgHandled(FALSE);

	CRect rcClient;
	GetClientRect(&rcClient);

	CRect rc = CRect(rcClient.Width()-14, 0, rcClient.Width(), rcClient.Height());
	m_VScrollBar.SetRect(&rc);
	Invalidate();
}

UINT CBuddyListCtrl::OnGetDlgCode(LPMSG lpMsg)
{
	return DLGC_HASSETSEL | DLGC_WANTARROWS | DLGC_WANTCHARS | DLGC_WANTTAB;
}

LRESULT CBuddyListCtrl::OnMouseMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	SetMsgHandled(FALSE);
	MSG msg = { m_hWnd, uMsg, wParam, lParam };
	if (m_ToolTipCtrl.IsWindow())
		m_ToolTipCtrl.RelayEvent(&msg);
	return 1;
}

void CBuddyListCtrl::OnDestroy()
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
	m_nStyle = BLC_BIG_ICON_STYLE;
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
		CBuddyTeamItem* lpTeamItem = m_arrBuddyTeamItems[i];
		if (lpTeamItem != NULL)
			delete lpTeamItem;
	}
	m_arrBuddyTeamItems.clear();
}

void CBuddyListCtrl::DrawParentWndBg(HDC hDC)
{
	HWND hParentWnd = ::GetParent(m_hWnd);

	CRect rcWindow;
	GetWindowRect(&rcWindow);
	::ScreenToClient(hParentWnd, (LPPOINT)&rcWindow); 
	::ScreenToClient(hParentWnd, ((LPPOINT)&rcWindow)+1);

	::BitBlt(hDC, 0, 0, rcWindow.Width(), rcWindow.Height(), m_hBgDC, rcWindow.left, rcWindow.top, SRCCOPY);
}

void CBuddyListCtrl::DrawBuddyTeam(HDC hDC, int nIndex)
{
	CBuddyTeamItem* lpItem = GetBuddyTeamByIndex(nIndex);
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
		if (m_lpBuddyTeamBgImgH != NULL && !m_lpBuddyTeamBgImgH->IsNull() && m_nType != BUDDY_LIST_TYPE_RECENT)
			m_lpBuddyTeamBgImgH->Draw2(hDC, rcItem);

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
			if (m_lpExpArrowImgN != NULL && !m_lpExpArrowImgN->IsNull() && m_nType != BUDDY_LIST_TYPE_RECENT)
				m_lpExpArrowImgN->Draw2(hDC, rcArrow);
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
		//HFONT hFont = (HFONT)SendMessage(WM_GETFONT, 0, 0);
		//if (NULL == hFont)
		//	hFont = (HFONT)::GetStockObject(DEFAULT_GUI_FONT);
		HFONT hFontTeamName = CGDIFactory::GetFont(19);
		HFONT hOldFont = (HFONT)::SelectObject(hDC, hFontTeamName);
		::DrawText(hDC, strText, strText.GetLength(), &rcText, nFormat);
		::SelectObject(hDC, hOldFont);
		::SetBkMode(hDC, nMode);
	}
}

void CBuddyListCtrl::DrawBuddyItem(HDC hDC, int nTeamIndex, int nIndex)
{
	int nMode = ::SetStretchBltMode(hDC, HALFTONE);
	if (BLC_BIG_ICON_STYLE == m_nStyle)
		DrawBuddyItemInBigIcon(hDC, nTeamIndex, nIndex);
	else if (BLC_SMALL_ICON_STYLE == m_nStyle)
		DrawBuddyItemInSmallIcon(hDC, nTeamIndex, nIndex);
	else if (BLC_STANDARD_ICON_STYLE == m_nStyle)
		DrawBuddyItemInStdIcon(hDC, nTeamIndex, nIndex);
	::SetStretchBltMode(hDC, nMode);
}

void CBuddyListCtrl::DrawBuddyItemInBigIcon(HDC hDC, int nTeamIndex, int nIndex)
{
	CBuddyItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (NULL == lpItem)
		return;

	CRect rcItem;
	GetItemRectByIndex(nTeamIndex, nIndex, rcItem);

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

	if (lpItem->m_bHeadFlashAnim)	// 头像闪动动画
	{
		POINT pt[] = {{-1,1},{0,0},{1,1},{0,0}};
		if (lpItem->m_nHeadFlashAnimState >= 0 && lpItem->m_nHeadFlashAnimState < 4)
			rcHead.OffsetRect(pt[lpItem->m_nHeadFlashAnimState]);
	}

	if (lpItem->m_lpHeadImg != NULL && !lpItem->m_lpHeadImg->IsNull())	// 绘制头像
		lpItem->m_lpHeadImg->Draw2(hDC, rcHead);

	
	if (lpItem->m_lpMobileImg != NULL && !lpItem->m_lpMobileImg->IsNull() && lpItem->m_bMobile)	// 绘制手机图标
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
	//HFONT hOldFont = (HFONT)::SelectObject(hDC, hFont);

	CString strName1;
	CString strName2;
	if(lpItem->m_nDisplayMode == BLCTRL_DISPLAY_SHOW_NICKNAME_ACCOUNT)
	{
		if(!lpItem->m_strMarkName.IsEmpty())
		{
			strName1 = lpItem->m_strMarkName;
			strName2.Format(_T("(%s)"), lpItem->m_strNickName);
		}
        else
        {
            strName1.Format(_T("%s(%s)"), lpItem->m_strNickName, lpItem->m_strUTalkNum);
            //strName1 = lpItem->m_strNickName;
        }
	}
	else if(lpItem->m_nDisplayMode == BLCTRL_DISPLAY_SHOW_NICKNAME)
	{
		strName1 = lpItem->m_strNickName;
	}
	else if(lpItem->m_nDisplayMode == BLCTRL_DISPLAY_SHOW_ACCOUNT)
	{
        strName1 = lpItem->m_strUTalkNum;
		//strName1 = lpItem->m_strNickName;
	}

	if(strName1.IsEmpty())
		strName1 = _T("  ");

	CRect rcName1;
	CRect rcName2;
	CRect rcSign;
	CRect rcDate;

	HFONT hOldFont = NULL;
	HFONT hFontNickName = CGDIFactory::GetFont(19);
	HFONT hFontAccountName = CGDIFactory::GetFont(19);
	HFONT hFontSignature = CGDIFactory::GetFont(18);
	
	if(!strName1.IsEmpty() && !strName2.IsEmpty())
	{	
		hOldFont = (HFONT)::SelectObject(hDC, hFontNickName);
		CRect rcText(0,0,0,0);	// 计算文字宽高
		::DrawText(hDC, strName1, strName1.GetLength(), &rcText, DT_SINGLELINE | DT_CALCRECT);

		rcName1.left = nHeadRight + 6;
		rcName1.right = rcName1.left + rcText.Width();
		rcName1.top = rcItem.top + 6;
		rcName1.bottom = rcName1.top + rcText.Height();
		if (rcName1.right > rcItem.right - 10)
			rcName1.right = rcItem.right - 10;

		COLORREF clrText;
		if (lpItem->m_bOnlineAnim)
		{
			COLORREF colors[] = {RGB(255,0,0), RGB(255,192,0), RGB(37,228,226), RGB(28,197,28), RGB(0,111,225)};
			clrText = colors[lpItem->m_nOnlineAnimState % 5];
		}
		else
			clrText = RGB(0, 0, 0);
		::SetTextColor(hDC, clrText);
		::DrawText(hDC, strName1, strName1.GetLength(), &rcName1, nFormat);

		::SelectObject(hDC, hFontAccountName);
		CRect rcText2(0,0,0,0);	// 计算文字宽高
		::DrawText(hDC, strName2, strName2.GetLength(), &rcText2, DT_SINGLELINE | DT_CALCRECT);
		
		rcName2.left = rcName1.right;
		rcName2.right = rcName2.left + rcText2.Width();
		rcName2.top = rcItem.top + 6;
		rcName2.bottom = rcName2.top + rcText2.Height();
		if (rcName2.right > rcItem.right - 10)
			rcName2.right = rcItem.right - 10;
		
		::SetTextColor(hDC, RGB(128, 128, 128));
		::DrawText(hDC, strName2, strName2.GetLength(), &rcName2, nFormat);
	}
	else if(!strName1.IsEmpty())
	{
		hOldFont = (HFONT)::SelectObject(hDC, hFontNickName);
		CRect rcText(0,0,0,0);	// 计算文字宽高
		::DrawText(hDC, strName1, strName1.GetLength(), &rcText, DT_SINGLELINE | DT_CALCRECT);

		rcName1.left = nHeadRight + 6;
		rcName1.right = rcName1.left + rcText.Width();
		rcName1.top = rcItem.top + 6;
		rcName1.bottom = rcName1.top + rcText.Height();
		if (rcName1.right > rcItem.right - 10)
			rcName1.right = rcItem.right - 10;

		COLORREF clrText;
		if (lpItem->m_bOnlineAnim)
		{
			COLORREF colors[] = {RGB(255,0,0), RGB(255,192,0), RGB(37,228,226), RGB(28,197,28), RGB(0,111,225)};
			clrText = colors[lpItem->m_nOnlineAnimState % 5];
		}
		else
			clrText = RGB(0, 0, 0);
		::SetTextColor(hDC, clrText);
		::DrawText(hDC, strName1, strName1.GetLength(), &rcName1, nFormat);
	}
	
	if (lpItem->m_bShowSign && lpItem->m_strSign.GetLength()>0)
	{
		::SelectObject(hDC, hFontSignature);
		CRect rcText(0,0,0,0);	// 计算文字宽高
		::DrawText(hDC, lpItem->m_strSign, lpItem->m_strSign.GetLength(), &rcText, DT_SINGLELINE | DT_CALCRECT);

		rcSign.left = nHeadRight + 6;
		rcSign.right = rcSign.left + rcText.Width();
		rcSign.top = rcName1.bottom + 5;
		rcSign.bottom = rcSign.top + rcText.Height();
		if (rcSign.right > rcItem.right - 10)
			rcSign.right = rcItem.right - 10;

		COLORREF clrText;
		if (lpItem->m_bOnlineAnim)
		{
			COLORREF colors[] = {RGB(255,0,0), RGB(255,192,0), RGB(37,228,226), RGB(28,197,28), RGB(0,111,225)};
			clrText = colors[lpItem->m_nOnlineAnimState % 5];
		}
		else
		{
			clrText = RGB(128,128,128);
		}

		::SetTextColor(hDC, clrText);
		::DrawText(hDC, lpItem->m_strSign, lpItem->m_strSign.GetLength(), &rcSign, nFormat);
	}

	::SelectObject(hDC, hOldFont);
	::SetBkMode(hDC, nMode);
}

void CBuddyListCtrl::DrawBuddyItemInSmallIcon(HDC hDC, int nTeamIndex, int nIndex)
{
	if (m_bShowBigIconInSel && m_nSelTeamIndex == nTeamIndex && m_nSelIndex == nIndex)
	{
		DrawBuddyItemInBigIcon(hDC, nTeamIndex, nIndex);
		return;
	}

	CBuddyItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (NULL == lpItem)
		return;

	CRect rcItem;
	GetItemRectByIndex(nTeamIndex, nIndex, rcItem);

	int nHeadWidth = 20, nHeadHeight = 20;

	CRect rcHead;
	CalcCenterRect(rcItem, nHeadWidth, nHeadHeight, rcHead);
	rcHead.left = rcItem.left + 6;
	rcHead.right = rcHead.left + nHeadWidth;

	int nHeadRight = rcHead.right;

	//小头像只显示昵称
	CString strName;
	if(!lpItem->m_strMarkName.IsEmpty())
		strName = lpItem->m_strMarkName;
	else
		strName = lpItem->m_strNickName;

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

	if (lpItem->m_bHeadFlashAnim)	// 头像闪动动画
	{
		POINT pt[] = {{-1,1},{0,0},{1,1},{0,0}};
		if (lpItem->m_nHeadFlashAnimState >= 0 && lpItem->m_nHeadFlashAnimState < 4)
			rcHead.OffsetRect(pt[lpItem->m_nHeadFlashAnimState]);
	}

	if (lpItem->m_lpHeadImg != NULL && !lpItem->m_lpHeadImg->IsNull())	// 绘制头像
		lpItem->m_lpHeadImg->Draw2(hDC, rcHead);

	UINT nFormat = DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;

	int nMode = ::SetBkMode(hDC, TRANSPARENT);
	//HFONT hFont = (HFONT)SendMessage(WM_GETFONT, 0, 0);
	//if (NULL == hFont)
	//	hFont = (HFONT)::GetStockObject(DEFAULT_GUI_FONT);
	//HFONT hOldFont = (HFONT)::SelectObject(hDC, hFont);
	HFONT hOldFont = NULL;

	CRect rcName, rcSign;
	HFONT hFontNickName = CGDIFactory::GetFont(19);
	HFONT hFontAccountName = CGDIFactory::GetFont(19);
	HFONT hFontSignature = CGDIFactory::GetFont(18);

	if (strName.GetLength() > 0)
	{
		hOldFont = (HFONT)::SelectObject(hDC, hFontNickName);
		CRect rcText(0,0,0,0);	// 计算文字宽高
		::DrawText(hDC, strName, strName.GetLength(), &rcText, DT_SINGLELINE | DT_CALCRECT);

		rcName.left = nHeadRight + 6;
		rcName.right = rcName.left + rcText.Width();
		rcName.top = rcItem.top;
		rcName.bottom = rcItem.bottom;
		if (rcName.right > rcItem.right - 10)
			rcName.right = rcItem.right - 10;

		COLORREF clrText;
		if (lpItem->m_bOnlineAnim)
		{
			COLORREF colors[] = {RGB(255,0,0), RGB(255,192,0), RGB(37,228,226), RGB(28,197,28), RGB(0,111,225)};
			clrText = colors[lpItem->m_nOnlineAnimState % 5];
		}
		else
			clrText = RGB(0, 0, 0);
		::SetTextColor(hDC, clrText);
		::DrawText(hDC, strName, strName.GetLength(), &rcName, nFormat);
	}

	if (lpItem->m_strSign.GetLength() > 0)
	{
		::SelectObject(hDC, hFontSignature);
		CRect rcText(0,0,0,0);	// 计算文字宽高
		::DrawText(hDC, lpItem->m_strSign, lpItem->m_strSign.GetLength(), &rcText, DT_SINGLELINE | DT_CALCRECT);

		rcSign.left = rcName.right + 6;
		rcSign.right = rcSign.left + rcText.Width();
		rcSign.top = rcItem.top;
		rcSign.bottom = rcItem.bottom;
		if (rcSign.right > rcItem.right - 10)
			rcSign.right = rcItem.right - 10;

		COLORREF clrText;
		if (lpItem->m_bOnlineAnim)
		{
			COLORREF colors[] = {RGB(255,0,0), RGB(255,192,0), RGB(37,228,226), RGB(28,197,28), RGB(0,111,225)};
			clrText = colors[lpItem->m_nOnlineAnimState % 5];
		}
		else
			clrText = RGB(128, 128, 128);
		::SetTextColor(hDC, clrText);
		::DrawText(hDC, lpItem->m_strSign, lpItem->m_strSign.GetLength(), &rcSign, nFormat);
	}

	::SelectObject(hDC, hOldFont);
	::SetBkMode(hDC, nMode);
}

void CBuddyListCtrl::DrawBuddyItemInStdIcon(HDC hDC, int nTeamIndex, int nIndex)
{
	CBuddyItem* lpItem = GetBuddyItemByIndex(nTeamIndex, nIndex);
	if (NULL == lpItem)
		return;

	CRect rcItem;
	GetItemRectByIndex(nTeamIndex, nIndex, rcItem);

	CImageEx* lpHeadImg;
	if (lpItem->m_bGender)
		lpHeadImg = m_lpStdGGHeadImg;
	else
		lpHeadImg = m_lpStdMMHeadImg;

	int nHeadWidth = 16, nHeadHeight = 16;

	CRect rcHead;
	CalcCenterRect(rcItem, nHeadWidth, nHeadHeight, rcHead);
	rcHead.left = rcItem.left + 6;
	rcHead.right = rcHead.left + nHeadWidth;

	int nHeadRight = rcHead.right;

	if (lpItem->m_bHeadFlashAnim)	// 头像闪动动画
	{
		POINT pt[] = {{-1,1},{0,0},{1,1},{0,0}};
		if (lpItem->m_nHeadFlashAnimState >= 0 && lpItem->m_nHeadFlashAnimState < 4)
			rcHead.OffsetRect(pt[lpItem->m_nHeadFlashAnimState]);
	}
	
	//标准头像只显示昵称
	CString strName;
	if(!lpItem->m_strMarkName.IsEmpty())
		strName = lpItem->m_strMarkName;
	else
		strName = lpItem->m_strNickName;
	
	if (m_nSelTeamIndex == nTeamIndex && m_nSelIndex == nIndex)
	{
		if (m_lpBuddyItemBgImgS != NULL && !m_lpBuddyItemBgImgS->IsNull())
			m_lpBuddyItemBgImgS->Draw2(hDC, rcItem);

		CRect rcHeadFrame(rcHead);
		rcHeadFrame.left -= 2;
		rcHeadFrame.right += 2;
		rcHeadFrame.top -= 2;
		rcHeadFrame.bottom += 2;

		if (m_lpHeadFrameImg != NULL && !m_lpHeadFrameImg->IsNull())
			m_lpHeadFrameImg->Draw2(hDC, rcHeadFrame);
	}
	else if (m_nHoverTeamIndex == nTeamIndex && m_nHoverIndex == nIndex)
	{
		if (m_lpBuddyItemBgImgH != NULL && !m_lpBuddyItemBgImgH->IsNull())
			m_lpBuddyItemBgImgH->Draw2(hDC, rcItem);
	}
	else
	{

	}

	if (lpHeadImg != NULL && !lpHeadImg->IsNull())	// 绘制头像
		lpHeadImg->Draw2(hDC, rcHead);

	UINT nFormat = DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;

	int nMode = ::SetBkMode(hDC, TRANSPARENT);
	//HFONT hFont = (HFONT)SendMessage(WM_GETFONT, 0, 0);
	//if (NULL == hFont)
	//	hFont = (HFONT)::GetStockObject(DEFAULT_GUI_FONT);
	//HFONT hOldFont = (HFONT)::SelectObject(hDC, hFont);
	HFONT hOldFont = NULL;
	HFONT hFontNickName = CGDIFactory::GetFont(19);
	HFONT hFontSignature = CGDIFactory::GetFont(18);

	CRect rcName, rcSign;

	if (strName.GetLength() > 0)
	{
		::SelectObject(hDC, hFontNickName);
		CRect rcText(0,0,0,0);	// 计算文字宽高
		::DrawText(hDC, strName, strName.GetLength(), &rcText, DT_SINGLELINE | DT_CALCRECT);

		rcName.left = nHeadRight + 6;
		rcName.right = rcName.left + rcText.Width();
		rcName.top = rcItem.top;
		rcName.bottom = rcItem.bottom;
		if (rcName.right > rcItem.right - 10)
			rcName.right = rcItem.right - 10;

		COLORREF clrText;
		if (lpItem->m_bOnlineAnim)
		{
			COLORREF colors[] = {RGB(255,0,0), RGB(255,192,0), RGB(37,228,226), RGB(28,197,28), RGB(0,111,225)};
			clrText = colors[lpItem->m_nOnlineAnimState % 5];
		}
		else
			clrText = RGB(0, 0, 0);
		::SetTextColor(hDC, clrText);
		::DrawText(hDC, strName, strName.GetLength(), &rcName, nFormat);
	}

	if (lpItem->m_strSign.GetLength() > 0)
	{
		::SelectObject(hDC, hFontSignature);
		CRect rcText(0,0,0,0);	// 计算文字宽高
		::DrawText(hDC, lpItem->m_strSign, lpItem->m_strSign.GetLength(), &rcText, DT_SINGLELINE | DT_CALCRECT);

		rcSign.left = rcName.right + 6;
		rcSign.right = rcSign.left + rcText.Width();
		rcSign.top = rcItem.top;
		rcSign.bottom = rcItem.bottom;
		if (rcSign.right > rcItem.right - 10)
			rcSign.right = rcItem.right - 10;

		COLORREF clrText;
		if (lpItem->m_bOnlineAnim)
		{
			COLORREF colors[] = {RGB(255,0,0), RGB(255,192,0), RGB(37,228,226), RGB(28,197,28), RGB(0,111,225)};
			clrText = colors[lpItem->m_nOnlineAnimState % 5];
		}
		else
			clrText = RGB(128, 128, 128);
		::SetTextColor(hDC, clrText);
		::DrawText(hDC, lpItem->m_strSign, lpItem->m_strSign.GetLength(), &rcSign, nFormat);
	}

	::SelectObject(hDC, hOldFont);
	::SetBkMode(hDC, nMode);
}

BOOL CBuddyListCtrl::StartTrackMouseLeave()
{
	TRACKMOUSEEVENT tme = { 0 };
	tme.cbSize = sizeof(tme);
	tme.dwFlags = TME_LEAVE;
	tme.hwndTrack = m_hWnd;
	return _TrackMouseEvent(&tme);
}

void CBuddyListCtrl::CalcCenterRect(CRect& rcDest, int cx, int cy, CRect& rcCenter)
{
	int x = (rcDest.Width() - cx + 1) / 2;
	int y = (rcDest.Height() - cy + 1) / 2;

	rcCenter = CRect(rcDest.left+x, rcDest.top+y, rcDest.left+x+cx, rcDest.top+y+cy);
}

void CBuddyListCtrl::HitTest(POINT pt, int& nTeamIndex, int& nIndex)
{
	CBuddyTeamItem* lpTeamItem;
	CBuddyItem* lpItem;
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
	if (BLC_BIG_ICON_STYLE == m_nStyle)
		nBuddyItemHeight = m_nBuddyItemHeightInBig;
	else if (BLC_SMALL_ICON_STYLE == m_nStyle)
		nBuddyItemHeight = m_nBuddyItemHeightInSmall;
	else
		nBuddyItemHeight = m_nBuddyItemHeightInStd;

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
						if (BLC_SMALL_ICON_STYLE == m_nStyle && m_bShowBigIconInSel
							&& (m_nSelTeamIndex == i && m_nSelIndex == j))
							nBuddyItemHeight = m_nBuddyItemHeightInBig;

						rcItem = CRect(nLeft, nTop, nLeft+nBuddyItemWidth, nTop+nBuddyItemHeight);
						if (rcItem.PtInRect(pt))
						{
							nTeamIndex = i;
							nIndex = j;
							return;
						}

						nTop += nBuddyItemHeight;
						nTop += m_nBuddyItemPadding;

						if (BLC_SMALL_ICON_STYLE == m_nStyle && m_bShowBigIconInSel
							&& (m_nSelTeamIndex == i && m_nSelIndex == j))
							nBuddyItemHeight = m_nBuddyItemHeightInSmall;
					}
				}
			}
		}
	}
}

CBuddyTeamItem* CBuddyListCtrl::GetBuddyTeamByIndex(int nIndex)
{
	if (nIndex >= 0 && nIndex < (int)m_arrBuddyTeamItems.size())
		return m_arrBuddyTeamItems[nIndex];
	else
		return NULL;
}

CBuddyItem* CBuddyListCtrl::GetBuddyItemByIndex(int nTeamIndex, int nIndex)
{
	CBuddyTeamItem* lpTeamItem;

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

void CBuddyListCtrl::CheckScrollBarStatus()
{
	if (!IsWindow())
		return;

	CBuddyTeamItem* lpTeamItem;
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
	if (BLC_BIG_ICON_STYLE == m_nStyle)
		nBuddyItemHeight = m_nBuddyItemHeightInBig;
	else if (BLC_SMALL_ICON_STYLE == m_nStyle)
		nBuddyItemHeight = m_nBuddyItemHeightInSmall;
	else
		nBuddyItemHeight = m_nBuddyItemHeightInStd;

	int nHeight = (m_nBuddyTeamHeight + m_nBuddyTeamPadding)* (int)m_arrBuddyTeamItems.size();

	for (int i = 0; i < (int)m_arrBuddyTeamItems.size(); i++)
	{
		lpTeamItem = m_arrBuddyTeamItems[i];
		if (lpTeamItem != NULL)
		{
			if (lpTeamItem->m_bExpand)
			{
				nHeight += ((nBuddyItemHeight + m_nBuddyItemPadding)* (int)lpTeamItem->m_arrBuddys.size());

				if (BLC_SMALL_ICON_STYLE == m_nStyle && m_bShowBigIconInSel
					&& (m_nSelTeamIndex == i && m_nSelIndex != -1))
				{
					nHeight -= nBuddyItemHeight;
					nHeight += m_nBuddyItemHeightInBig;
				}
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

void CBuddyListCtrl::Scroll(int cx, int cy)
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

void CBuddyListCtrl::EnsureVisible(int nTeamIndex, int nIndex)
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

void CBuddyListCtrl::SortBuddyTeam(int nTeamIndex, int nIndex)
{
	CBuddyTeamItem* lpBuddyTeam = GetBuddyTeamByIndex(nTeamIndex);			// 获取要排序的分组
	if (NULL == lpBuddyTeam)
		return;

	int nItemCnt = (int)lpBuddyTeam->m_arrBuddys.size();					// 分组的好友数小于2则不用再排序
	if (nItemCnt < 2)
		return;

	CBuddyItem* lpBuddyItem1 = GetBuddyItemByIndex(nTeamIndex, nIndex);	// 获取要排序的好友
	if (NULL == lpBuddyItem1)
		return;

	lpBuddyTeam->m_arrBuddys.erase(lpBuddyTeam->m_arrBuddys.begin()+nIndex);// 先删除

	nItemCnt = (int)lpBuddyTeam->m_arrBuddys.size();

	int nInsert = nItemCnt-1;
	for (int i = 0; i < nItemCnt; i++)
	{
		CBuddyItem* lpBuddyItem2 = lpBuddyTeam->m_arrBuddys[i];
		if (lpBuddyItem2 != NULL)
		{
			if (lpBuddyItem1->m_bOnline > lpBuddyItem2->m_bOnline)
			{
				nInsert = i;
				break;
			}
			else if (lpBuddyItem1->m_bOnline == lpBuddyItem2->m_bOnline)
			{
				if (lpBuddyItem1->m_nID <= lpBuddyItem2->m_nID)
				{
					nInsert = i;
					break;
				}
			}
		}
	}

	lpBuddyTeam->m_arrBuddys.insert(lpBuddyTeam->m_arrBuddys.begin()+nInsert, lpBuddyItem1);
}

// 头像闪动动画
void CBuddyListCtrl::OnTimer_HeadFlashAnim(UINT_PTR nIDEvent)
{
	if (nIDEvent != m_dwHeadFlashAnimTimerId)
		return;

	int nTeamCnt = GetBuddyTeamCount();
	for (int i = 0; i < nTeamCnt; i++)
	{
		CBuddyTeamItem* lpBuddyTeam = GetBuddyTeamByIndex(i);
		if (lpBuddyTeam != NULL)
		{
			if (lpBuddyTeam->m_nHeadFlashAnim > 0)
			{
				if (!lpBuddyTeam->m_bExpand)
				{
					lpBuddyTeam->m_nHeadFlashAnimState++;
					if (lpBuddyTeam->m_nHeadFlashAnimState >= 2)
						lpBuddyTeam->m_nHeadFlashAnimState = 0;
				}
				else
				{
					int nItemCnt = GetBuddyItemCount(i);
					for (int j = 0; j < nItemCnt; j++)
					{
						CBuddyItem* lpBuddyItem = GetBuddyItemByIndex(i, j);
						if (lpBuddyItem != NULL && lpBuddyItem->m_bHeadFlashAnim)
						{
							lpBuddyItem->m_nHeadFlashAnimState++;
							if (lpBuddyItem->m_nHeadFlashAnimState >= 4)
								lpBuddyItem->m_nHeadFlashAnimState = 0;
						}
					}
				}
			}
		}
	}
	Invalidate();
}

// 上线动画
void CBuddyListCtrl::OnTimer_OnlineAnim(UINT_PTR nIDEvent)
{
	if (nIDEvent != m_dwOnlineAnimTimerId)
		return;

	int nAnimCnt = 0;

	int nTeamCnt = GetBuddyTeamCount();
	for (int i = 0; i < nTeamCnt; i++)
	{
		int nItemCnt = GetBuddyItemCount(i);
		for (int j = 0; j < nItemCnt; j++)
		{
			CBuddyItem* lpBuddyItem = GetBuddyItemByIndex(i, j);
			if (lpBuddyItem != NULL && lpBuddyItem->m_bOnlineAnim)
			{
				nAnimCnt++;
				lpBuddyItem->m_nOnlineAnimState++;
				if (lpBuddyItem->m_nOnlineAnimState == 5)
				{
					SortBuddyTeam(i, j);
				}
				else if (lpBuddyItem->m_nOnlineAnimState >= 5* 5)
				{
					lpBuddyItem->m_bOnlineAnim = FALSE;
					lpBuddyItem->m_nOnlineAnimState = 0;
				}
			}
		}
	}

	if (nAnimCnt <= 0)
	{
		KillTimer(m_dwOnlineAnimTimerId);
		m_dwOnlineAnimTimerId = NULL;
	}
	Invalidate();
}

// 下线动画
void CBuddyListCtrl::OnTimer_OfflineAnim(UINT_PTR nIDEvent)
{
	if (nIDEvent != m_dwOfflineAnimTimerId)
		return;

	int nTeamCnt = GetBuddyTeamCount();
	for (int i = 0; i < nTeamCnt; i++)
	{
		int nItemCnt = GetBuddyItemCount(i);
		for (int j = 0; j < nItemCnt; j++)
		{
			CBuddyItem* lpBuddyItem = GetBuddyItemByIndex(i, j);
			if (lpBuddyItem != NULL && lpBuddyItem->m_bOfflineAnim)
			{
				lpBuddyItem->m_bOfflineAnim = FALSE;
				SortBuddyTeam(i, j);
			}
		}
	}

	KillTimer(m_dwOfflineAnimTimerId);
	m_dwOfflineAnimTimerId = NULL;

	Invalidate();
}