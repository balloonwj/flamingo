#include "stdafx.h"
#include "SkinManager.h"
#include "../File.h"
#include "../Path.h"

CSkinManager* CSkinManager::m_lpSkinMgr = NULL;

CSkinManager::CSkinManager(void)
{
}

CSkinManager::~CSkinManager(void)
{
}

BOOL CSkinManager::Init()
{
	m_lpSkinMgr = new CSkinManager;
	if (NULL == m_lpSkinMgr)
		return FALSE;

	m_lpSkinMgr->m_strPath = _T("");
	m_lpSkinMgr->m_nCurSkinID = 0;

	return TRUE;
}

void CSkinManager::UnInit()
{
	if (m_lpSkinMgr != NULL)
	{
		m_lpSkinMgr->ClearSkinList();
		m_lpSkinMgr->ClearImage();
		m_lpSkinMgr->m_strPath = _T("");
		m_lpSkinMgr->m_nCurSkinID = 0;

		delete m_lpSkinMgr;
		m_lpSkinMgr = NULL;
	}
}

CSkinManager* CSkinManager::GetInstance()
{
	return m_lpSkinMgr;
}

void CSkinManager::SetSkinPath(LPCTSTR lpszPath)
{
	m_strPath = lpszPath;
}

BOOL CSkinManager::LoadConfigXml()
{
	CXmlDocument xmlDoc;
	CXmlNode xmlNode, xmlSubNode, xmlSubNode2;
	CString strFileName;
	BOOL bRet;

	strFileName = m_strPath + _T("SkinConfig.xml");

	bRet = xmlDoc.Load(strFileName);
	if (!bRet)
		return FALSE;

	ClearSkinList();
	m_nCurSkinID = 0;

	bRet = xmlDoc.SelectSingleNode(_T("/SkinList"), xmlNode);
	if (bRet)
	{
		bRet = xmlNode.GetFirstChildNode(_T("Skin"), xmlSubNode);
		while (bRet)
		{
			SKIN_INFO* lpSkinInfo = new SKIN_INFO;
			if (lpSkinInfo != NULL)
			{
				xmlSubNode.GetFirstChildNode(_T("SkinID"), xmlSubNode2);
				lpSkinInfo->nSkinID = xmlSubNode2.GetTextInt();

				xmlSubNode.GetFirstChildNode(_T("SkinName"), xmlSubNode2);
				lpSkinInfo->strSkinName = xmlSubNode2.GetText().c_str();

				xmlSubNode.GetFirstChildNode(_T("SkinPath"), xmlSubNode2);
				lpSkinInfo->strSkinPath = m_strPath + xmlSubNode2.GetText().c_str();

				m_arrSkinList.push_back(lpSkinInfo);
			}
			
			bRet = xmlSubNode.GetNextSiblingNode(_T("Skin"), xmlSubNode);
		}

		xmlNode.GetFirstChildNode(_T("CurSkinID"), xmlSubNode);
		m_nCurSkinID = xmlSubNode.GetTextInt();
	}

	xmlSubNode2.Release();
	xmlSubNode.Release();
	xmlNode.Release();
	xmlDoc.Release();

	return TRUE;
}

BOOL CSkinManager::SaveConfigXml()
{
	return TRUE;
}

BOOL CSkinManager::SetCurSkin(int nSkinID)
{
	SKIN_INFO* lpSkinInfo;

	lpSkinInfo = GetSkinInfo(m_nCurSkinID);
	if (lpSkinInfo != NULL)
	{
		m_nCurSkinID = nSkinID;
		return TRUE;
	}
	else
	{
		return FALSE;
	}
}

std::vector<SKIN_INFO*>& CSkinManager::GetSkinList()
{
	return m_arrSkinList;
}

BOOL CSkinManager::LoadImage(LPCTSTR lpszFileName)
{
	return TRUE;
}

BOOL CSkinManager::DestroyImage(LPCTSTR lpszFileName)
{
	return TRUE;
}

CImageEx* CSkinManager::GetImage(LPCTSTR lpszFileName)
{
	std::map<CString, SKIN_IMAGE_INFO*>::iterator iter;
	std::pair<std::map<CString, SKIN_IMAGE_INFO*>::iterator, bool> pairInsert;
	SKIN_IMAGE_INFO* lpSkinImgInfo;
	CImageEx* lpImgEx;
	BOOL bRet;

	if (NULL == lpszFileName || NULL ==*lpszFileName)
		return NULL;

	iter = m_mapImage.find(lpszFileName);
	if (iter != m_mapImage.end())
	{
		lpSkinImgInfo = iter->second;
		if (lpSkinImgInfo != NULL)
		{
			lpSkinImgInfo->nRef++;
			return lpSkinImgInfo->lpImage;
		}
		else
		{
			return NULL;
		}
	}
	else
	{
		lpSkinImgInfo = new SKIN_IMAGE_INFO;
		lpImgEx = new CImageEx;
		if (NULL == lpSkinImgInfo || NULL == lpImgEx)
		{
			if (lpSkinImgInfo != NULL)
				delete lpSkinImgInfo;
			if (lpImgEx != NULL)
				delete lpImgEx;
			return NULL;
		}
		
		pairInsert = m_mapImage.insert(std::pair<CString, SKIN_IMAGE_INFO*>(lpszFileName, lpSkinImgInfo));
		if (!pairInsert.second)
		{
			delete lpSkinImgInfo;
			delete lpImgEx;
			return NULL;
		}

		if (Hootina::CPath::IsFileExist(lpszFileName))
			bRet = lpImgEx->LoadFromFile(lpszFileName);
		else
			bRet = lpImgEx->LoadFromFile(GetAbsolutePath(lpszFileName));
		if (!bRet)
		{
			m_mapImage.erase(pairInsert.first);
			delete lpSkinImgInfo;
			delete lpImgEx;
			return NULL;
		}

		lpSkinImgInfo->lpImage = lpImgEx;
		lpSkinImgInfo->nRef = 1;

		return lpImgEx;
	}
}

void CSkinManager::ReleaseImage(CImageEx*& lpImgEx)
{
	std::map<CString, SKIN_IMAGE_INFO*>::iterator iter;
	SKIN_IMAGE_INFO* lpSkinImgInfo;

	if (NULL == lpImgEx)
		return;

	for (iter = m_mapImage.begin(); iter != m_mapImage.end(); iter++)
	{
		lpSkinImgInfo = iter->second;
		if (lpSkinImgInfo != NULL)
		{
			if (lpSkinImgInfo->lpImage == lpImgEx)
			{
				lpSkinImgInfo->nRef--;
				if (lpSkinImgInfo->nRef <= 0)
				{
					lpSkinImgInfo->lpImage->Destroy();
					delete lpSkinImgInfo->lpImage;
					lpSkinImgInfo->lpImage = NULL;

					delete lpSkinImgInfo;
					m_mapImage.erase(iter);
				}
				lpImgEx = NULL;
				break;
			}
		}
	}
}

CString CSkinManager::GetAbsolutePath(LPCTSTR lpFileName)
{
	SKIN_INFO* lpSkinInfo;

	lpSkinInfo = GetSkinInfo(m_nCurSkinID);
	if (lpSkinInfo != NULL)
		return lpSkinInfo->strSkinPath + lpFileName;
	else
		return _T("");
}

SKIN_INFO* CSkinManager::GetSkinInfo(int nSkinID)
{
	SKIN_INFO* lpSkinInfo;

	for (int i = 0; i < (int)m_arrSkinList.size(); i++)
	{
		lpSkinInfo = m_arrSkinList[i];
		if (lpSkinInfo != NULL)
		{
			if (lpSkinInfo->nSkinID == nSkinID)
				return lpSkinInfo;
		}
	}

	return NULL;
}

void CSkinManager::ClearSkinList()
{
	SKIN_INFO* lpSkinInfo;

	for (int i = 0; i < (int)m_arrSkinList.size(); i++)
	{
		lpSkinInfo = m_arrSkinList[i];
		if (lpSkinInfo != NULL)
			delete lpSkinInfo;
	}
	m_arrSkinList.clear();
}

void CSkinManager::ClearImage()
{
	std::map<CString, SKIN_IMAGE_INFO*>::iterator iter;
	SKIN_IMAGE_INFO* lpSkinImgInfo;

	for (iter = m_mapImage.begin(); iter != m_mapImage.end(); iter++)
	{
		lpSkinImgInfo = iter->second;
		if (lpSkinImgInfo != NULL)
		{
			if (lpSkinImgInfo->lpImage != NULL)
			{
				lpSkinImgInfo->lpImage->Destroy();
				delete lpSkinImgInfo->lpImage;
				lpSkinImgInfo->lpImage = NULL;
			}
			delete lpSkinImgInfo;
		}
	}

	m_mapImage.clear();
}