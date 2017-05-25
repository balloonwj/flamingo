#include "stdafx.h"
#include "FaceList.h"

CFaceInfo::CFaceInfo(void)
{
	m_nId = -1;
	m_nIndex = -1;
	m_strTip = _T("");
	m_strFileName = _T("");
}

CFaceInfo::~CFaceInfo(void)
{

}

CFaceList::CFaceList(void)
{
	Reset();
}

CFaceList::~CFaceList(void)
{
	Reset();
}

void CFaceList::Reset()
{
// 	m_nWidth = 28;
// 	m_nHeight = 28;
// 	m_nZoomWidth = 86;
// 	m_nZoomHeight = 86;
// 	m_nCol = 15;
	m_nItemWidth = m_nItemHeight = 0;
	m_nZoomWidth = m_nZoomHeight = 0;
	m_nRow = m_nCol = 0;
	
	for (int i = 0; i < (int)m_arrFaceInfo.size(); i++)
	{
		CFaceInfo* lpFaceInfo = m_arrFaceInfo[i];
		if (lpFaceInfo != NULL)
			delete lpFaceInfo;
	}
	m_arrFaceInfo.clear();
}

BOOL CFaceList::LoadConfigFile(LPCTSTR lpszFileName)
{
	CXmlDocument xmlDoc;
	CXmlNode xmlNode, xmlSubNode;

	Reset();

	BOOL bRet = xmlDoc.Load(lpszFileName);
	if (!bRet)
		return FALSE;

	bRet = xmlDoc.SelectSingleNode(_T("/faceconfig"), xmlNode);
	if (bRet)
	{
		m_nItemWidth = xmlNode.GetAttributeInt(_T("item_width"));
		m_nItemHeight = xmlNode.GetAttributeInt(_T("item_height"));
		m_nZoomWidth = xmlNode.GetAttributeInt(_T("zoom_width"));
		m_nZoomHeight = xmlNode.GetAttributeInt(_T("zoom_height"));
		m_nRow = xmlNode.GetAttributeInt(_T("row"));
		m_nCol = xmlNode.GetAttributeInt(_T("col"));

		bRet = xmlNode.GetFirstChildNode(_T("face"), xmlSubNode);
		while (bRet)
		{
			CFaceInfo* lpFaceInfo = new CFaceInfo;
			if (lpFaceInfo != NULL)
			{
				lpFaceInfo->m_nId = xmlSubNode.GetAttributeInt(_T("id"));
				lpFaceInfo->m_strTip = xmlSubNode.GetAttribute(_T("tip"));
				lpFaceInfo->m_strFileName = Hootina::CPath::GetAppPath() + xmlSubNode.GetAttribute(_T("file"));
				tstring strIndex = Hootina::CPath::GetFileNameWithoutExtension(lpFaceInfo->m_strFileName.c_str());
				if (IsDigit(strIndex.c_str()))
					lpFaceInfo->m_nIndex = _tcstol(strIndex.c_str(), NULL, 10);
				m_arrFaceInfo.push_back(lpFaceInfo);
			}
			
			bRet = xmlSubNode.GetNextSiblingNode(_T("face"), xmlSubNode);
		}
	}

	xmlSubNode.Release();
	xmlNode.Release();
	xmlDoc.Release();

	return TRUE;
}

CFaceInfo* CFaceList::GetFaceInfo(int nIndex)
{
	if (nIndex >= 0 && nIndex < (int)m_arrFaceInfo[nIndex])
		return m_arrFaceInfo[nIndex];
	else
		return NULL;
}

CFaceInfo* CFaceList::GetFaceInfoById(int nFaceId)
{
	for (int i = 0; i < (int)m_arrFaceInfo.size(); i++)
	{
		CFaceInfo* lpFaceInfo = m_arrFaceInfo[i];
		if (lpFaceInfo != NULL && lpFaceInfo->m_nId == nFaceId)
			return lpFaceInfo;
	}

	return NULL;
}

CFaceInfo* CFaceList::GetFaceInfoByIndex(int nFaceIndex)
{
	for (int i = 0; i < (int)m_arrFaceInfo.size(); i++)
	{
		CFaceInfo* lpFaceInfo = m_arrFaceInfo[i];
		if (lpFaceInfo != NULL && lpFaceInfo->m_nIndex == nFaceIndex)
			return lpFaceInfo;
	}

	return NULL;
}