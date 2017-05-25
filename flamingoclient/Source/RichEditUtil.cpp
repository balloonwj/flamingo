#include "stdafx.h"
#include "RichEditUtil.h"
#include "UserSessionData.h"


static BOOL bIsInstallImageOleCtrl = FALSE;

//注册名称为ImageOleCtrl的COM组件
void RegisterCom_ImageOleCtrl()
{
	CString strImageOleCtrlFilePath = g_szHomePath;
	strImageOleCtrlFilePath += _T("richFace.dll");
	if(RunTimeHelper::IsVista())
	{	
		SHELLEXECUTEINFOW sei={0};
		sei.cbSize = sizeof(SHELLEXECUTEINFOW);
		sei.lpVerb = _T("runas");
		sei.lpFile = _T("regsvr32.exe");
		sei.lpParameters = strImageOleCtrlFilePath.GetString();
		sei.lpDirectory = NULL;
		sei.nShow = SW_SHOW;
		ShellExecuteEx(&sei);
	}
	else
	{
		STARTUPINFO si={0};
		PROCESS_INFORMATION pi={0};
		si.cb = sizeof(si);
		CreateProcess(_T("regsvr32.exe"), strImageOleCtrlFilePath.GetBuffer(), NULL,NULL,FALSE,0,NULL,NULL,&si,&pi);
	}
}

IRichEditOle* RichEdit_GetOleInterface(HWND hWnd)
{
	IRichEditOle*pRichEditOle = NULL;
	::SendMessage(hWnd, EM_GETOLEINTERFACE, 0, (LPARAM)&pRichEditOle);
	return pRichEditOle;
}

int RichEdit_GetWindowTextLength(HWND hWnd)
{
	return ::GetWindowTextLength(hWnd);
}

int RichEdit_GetWindowText(HWND hWnd, LPTSTR lpszStringBuf, int nMaxCount)
{
	return ::GetWindowText(hWnd, lpszStringBuf, nMaxCount);
}

int RichEdit_GetWindowText(HWND hWnd, tstring& strText)
{
	int nLength;
	TCHAR* pszText;

	nLength = RichEdit_GetWindowTextLength(hWnd);
	pszText = new TCHAR[nLength+1];
	if (NULL == pszText)
		return 0;
	memset(pszText, 0, (nLength+1)*sizeof(TCHAR));
	nLength = RichEdit_GetWindowText(hWnd, pszText, nLength+1);
	strText = pszText;
	delete []pszText;

	return nLength;
}

int RichEdit_GetTextRange(HWND hWnd, CHARRANGE* lpchrg, tstring& strText)
{
	strText = _T("");

	if (NULL == lpchrg || lpchrg->cpMax <= lpchrg->cpMin)
		return 0;

	LONG nLen = lpchrg->cpMax - lpchrg->cpMin;
	TCHAR* pText = new TCHAR[nLen+1];
	if (NULL == pText)
		return 0;

	memset(pText, 0, (nLen+1)*sizeof(TCHAR));

	TEXTRANGE tr = { 0 };
	tr.chrg =*lpchrg;
	tr.lpstrText = pText;
	int nRet = (int)::SendMessage(hWnd, EM_GETTEXTRANGE, 0, (LPARAM)&tr);

	strText = pText;
	delete []pText;

	return nRet;
}

DWORD RichEdit_GetDefaultCharFormat(HWND hWnd, CHARFORMAT& cf)
{
	cf.cbSize = sizeof(CHARFORMAT);
	return (DWORD)::SendMessage(hWnd, EM_GETCHARFORMAT, 0, (LPARAM)&cf);
}

BOOL RichEdit_SetDefaultCharFormat(HWND hWnd, CHARFORMAT& cf)
{
	//必须关闭这个选项，否则设置的默认字体只对中文起作用，对英文不起作用.
	//see: https://msdn.microsoft.com/en-us/library/windows/desktop/bb774250(v=vs.85).aspx
	::SendMessage(hWnd, EM_SETLANGOPTIONS, 0, 0);

	cf.cbSize = sizeof(CHARFORMAT);
	return (BOOL)::SendMessage(hWnd, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);
}

DWORD RichEdit_GetSelectionCharFormat(HWND hWnd, CHARFORMAT& cf)
{
	cf.cbSize = sizeof(CHARFORMAT);
	return (DWORD)::SendMessage(hWnd, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

BOOL RichEdit_SetSelectionCharFormat(HWND hWnd, CHARFORMAT& cf)
{
	cf.cbSize = sizeof(CHARFORMAT);
	return (BOOL)::SendMessage(hWnd, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

void RichEdit_ReplaceSel(HWND hWnd, LPCTSTR lpszNewText, BOOL bCanUndo/* = FALSE*/)
{
	::SendMessage(hWnd, EM_REPLACESEL, (WPARAM)bCanUndo, (LPARAM)lpszNewText);
}

void RichEdit_GetSel(HWND hWnd, LONG& nStartChar, LONG& nEndChar)
{
	CHARRANGE cr = { 0, 0 };
	::SendMessage(hWnd, EM_EXGETSEL, 0, (LPARAM)&cr);
	nStartChar = cr.cpMin;
	nEndChar = cr.cpMax;
}

int RichEdit_SetSel(HWND hWnd, LONG nStartChar, LONG nEndChar)
{
	CHARRANGE cr = { nStartChar, nEndChar };
	return (int)::SendMessage(hWnd, EM_EXSETSEL, 0, (LPARAM)&cr);
}

// 设置默认字体
void RichEdit_SetDefFont(HWND hWnd, LPCTSTR lpFontName, int nFontSize,
						 COLORREF clrText, BOOL bBold, BOOL bItalic, 
						 BOOL bUnderLine, BOOL bIsLink)
{
	CHARFORMAT cf;
	memset(&cf, 0, sizeof(cf));
	RichEdit_GetDefaultCharFormat(hWnd, cf);

	if (lpFontName != NULL)					// 设置字体名称
	{
		cf.dwMask = cf.dwMask | CFM_FACE;
		_tcscpy(cf.szFaceName, lpFontName);
	}

	if (nFontSize > 0)						// 设置字体高度
	{
		cf.dwMask = cf.dwMask | CFM_SIZE;
		cf.yHeight = nFontSize* 20;
	}

	cf.dwMask |= CFM_COLOR | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_LINK;

	cf.crTextColor = clrText;				// 设置字体颜色
	cf.dwEffects &= ~CFE_AUTOCOLOR;

	if (bBold)								// 设置粗体
		cf.dwEffects |= CFE_BOLD;
	else
		cf.dwEffects &= ~CFE_BOLD;

	if (bItalic)							// 设置倾斜
		cf.dwEffects |= CFE_ITALIC;
	else
		cf.dwEffects &= ~CFE_ITALIC;

	if (bUnderLine)							// 设置下划线
		cf.dwEffects |= CFE_UNDERLINE;
	else
		cf.dwEffects &= ~CFE_UNDERLINE;

	if (bIsLink)							// 设置超链接
		cf.dwEffects |= CFE_LINK;
	else
		cf.dwEffects &= ~CFE_LINK;

	RichEdit_SetDefaultCharFormat(hWnd, cf);
}

// 设置默认字体名称和大小
void RichEdit_SetDefFont(HWND hWnd, LPCTSTR lpFontName, int nFontSize)
{
	CHARFORMAT cf;
	memset(&cf, 0, sizeof(cf));
	RichEdit_GetDefaultCharFormat(hWnd, cf);

	if (lpFontName != NULL)					// 设置字体名称
	{
		cf.dwMask = cf.dwMask | CFM_FACE;
		_tcscpy(cf.szFaceName, lpFontName);
	}

	if (nFontSize > 0)						// 设置字体高度
	{
		cf.dwMask = cf.dwMask | CFM_SIZE;
		cf.yHeight = nFontSize* 20;
	}

	RichEdit_SetDefaultCharFormat(hWnd, cf);
}

// 设置默认字体颜色
void RichEdit_SetDefTextColor(HWND hWnd, COLORREF clrText)
{
	CHARFORMAT cf;
	memset(&cf, 0, sizeof(cf));
	RichEdit_GetDefaultCharFormat(hWnd, cf);

	cf.dwMask = cf.dwMask | CFM_COLOR;
	cf.crTextColor = clrText;
	cf.dwEffects &= ~CFE_AUTOCOLOR;
	RichEdit_SetDefaultCharFormat(hWnd, cf);
}

// 设置默认超链接
void RichEdit_SetDefLinkText(HWND hWnd, BOOL bEnable)
{
	CHARFORMAT cf;
	memset(&cf, 0, sizeof(cf));
	RichEdit_GetDefaultCharFormat(hWnd, cf);

	cf.dwMask = cf.dwMask | CFM_LINK;
	if (bEnable)							// 设置超链接
		cf.dwEffects |= CFE_LINK;
	else
		cf.dwEffects &= ~CFE_LINK;

	RichEdit_SetDefaultCharFormat(hWnd, cf);
}

// 设置字体
void RichEdit_SetFont(HWND hWnd, LPCTSTR lpFontName, int nFontSize,
					  COLORREF clrText, BOOL bBold, BOOL bItalic, 
					  BOOL bUnderLine, BOOL bIsLink)
{
	CHARFORMAT cf;
	memset(&cf, 0, sizeof(cf));
	RichEdit_GetSelectionCharFormat(hWnd, cf);

	if (lpFontName != NULL)					// 设置字体名称
	{
		cf.dwMask = cf.dwMask | CFM_FACE;
		_tcscpy(cf.szFaceName, lpFontName);
	}

	if (nFontSize > 0)						// 设置字体高度
	{
		cf.dwMask = cf.dwMask | CFM_SIZE;
		cf.yHeight = nFontSize* 20;
	}

	cf.dwMask |= CFM_COLOR | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_LINK;

	cf.crTextColor = clrText;				// 设置字体颜色
	cf.dwEffects &= ~CFE_AUTOCOLOR;

	if (bBold)								// 设置粗体
		cf.dwEffects |= CFE_BOLD;
	else
		cf.dwEffects &= ~CFE_BOLD;

	if (bItalic)							// 设置倾斜
		cf.dwEffects |= CFE_ITALIC;
	else
		cf.dwEffects &= ~CFE_ITALIC;

	if (bUnderLine)							// 设置下划线
		cf.dwEffects |= CFE_UNDERLINE;
	else
		cf.dwEffects &= ~CFE_UNDERLINE;

	if (bIsLink)							// 设置超链接
		cf.dwEffects |= CFE_LINK;
	else
		cf.dwEffects &= ~CFE_LINK;

	RichEdit_SetSelectionCharFormat(hWnd, cf);
}

// 设置字体名称和大小
void RichEdit_SetFont(HWND hWnd, LPCTSTR lpFontName, int nFontSize)
{
	CHARFORMAT cf;
	memset(&cf, 0, sizeof(cf));
	RichEdit_GetSelectionCharFormat(hWnd, cf);

	if (lpFontName != NULL)					// 设置字体名称
	{
		cf.dwMask = cf.dwMask | CFM_FACE;
		_tcscpy(cf.szFaceName, lpFontName);
	}

	if (nFontSize > 0)						// 设置字体高度
	{
		cf.dwMask = cf.dwMask | CFM_SIZE;
		cf.yHeight = nFontSize* 20;
	}

	RichEdit_SetSelectionCharFormat(hWnd, cf);
}

// 设置字体颜色
void RichEdit_SetTextColor(HWND hWnd, COLORREF clrText)
{
	CHARFORMAT cf;
	memset(&cf, 0, sizeof(cf));
	RichEdit_GetSelectionCharFormat(hWnd, cf);

	cf.dwMask = cf.dwMask | CFM_COLOR;
	cf.crTextColor = clrText;
	cf.dwEffects &= ~CFE_AUTOCOLOR;

	RichEdit_SetSelectionCharFormat(hWnd, cf);
}

// 设置超链接
void RichEdit_SetLinkText(HWND hWnd, BOOL bEnable)
{
	CHARFORMAT cf;
	memset(&cf, 0, sizeof(cf));
	RichEdit_GetSelectionCharFormat(hWnd, cf);

	cf.dwMask = cf.dwMask | CFM_LINK;
	if (bEnable)							// 设置超链接
		cf.dwEffects |= CFE_LINK;
	else
		cf.dwEffects &= ~CFE_LINK;

	RichEdit_SetSelectionCharFormat(hWnd, cf);
}

// 设置左缩进(单位:缇)
BOOL RichEdit_SetStartIndent(HWND hWnd, int nSize)
{
	PARAFORMAT2 pf2;
	memset(&pf2, 0, sizeof(pf2));
	pf2.cbSize = sizeof(PARAFORMAT2);
	pf2.dwMask = PFM_STARTINDENT;
	pf2.dxStartIndent = nSize;
	return (BOOL)::SendMessage(hWnd, EM_SETPARAFORMAT, 0, (LPARAM)&pf2);
}

// 插入表情图片
BOOL RichEdit_InsertFace(HWND hWnd, LPCTSTR lpszFileName, int nFaceId,
						 int nFaceIndex, COLORREF clrBg, BOOL bAutoScale, int nReservedWidth)
{
	BSTR bstrFileName = NULL;
	IRichEditOle* pRichEditOle = NULL;
	IOleClientSite*pOleClientSite = NULL;
	IImageOle* pImageOle = NULL;
	IOleObject*pOleObject = NULL;
	REOBJECT reobject = {0};
	HRESULT hr = E_FAIL;

	if (NULL == lpszFileName || NULL ==*lpszFileName)
		return FALSE;

	TCHAR cProtocol[16] = {0};
	_tcsncpy(cProtocol, lpszFileName, 7);
	if ((_tcsicmp(cProtocol, _T("http://")) != 0) && ::GetFileAttributes(lpszFileName) == 0xFFFFFFFF)
		return FALSE;

	bstrFileName = ::SysAllocString(lpszFileName);
	if (NULL == bstrFileName)
		return FALSE;

	pRichEditOle = RichEdit_GetOleInterface(hWnd);
	if (NULL == pRichEditOle)
		goto Ret0;

	hr = ::CoCreateInstance(CLSID_ImageOle, NULL, 
		CLSCTX_INPROC_SERVER, IID_IImageOle, (void**)&pImageOle);
	if (FAILED(hr))
		goto Ret0;

	hr = pImageOle->QueryInterface(IID_IOleObject, (void**)&pOleObject);
	if (FAILED(hr))
		goto Ret0;

	pRichEditOle->GetClientSite(&pOleClientSite);
	if (NULL == pOleClientSite)
		goto Ret0;

	pOleObject->SetClientSite(pOleClientSite);

	pImageOle->SetRichEditHwnd(hWnd);
	pImageOle->SetTextServices(NULL);
	pImageOle->SetTextHost(NULL);
	pImageOle->SetFaceId(nFaceId);
	pImageOle->SetFaceIndex(nFaceIndex);
	pImageOle->SetBgColor(clrBg);
	pImageOle->SetAutoScale(bAutoScale, nReservedWidth);
	pImageOle->LoadFromFile(bstrFileName);

	hr = ::OleSetContainedObject(pOleObject, TRUE);

	reobject.cbStruct = sizeof(REOBJECT);
	reobject.clsid    = CLSID_ImageOle;
	reobject.cp       = REO_CP_SELECTION;
	reobject.dvaspect = DVASPECT_CONTENT;
	reobject.dwFlags  = REO_BELOWBASELINE;
	reobject.poleobj  = pOleObject;
	reobject.polesite = pOleClientSite;
	reobject.dwUser   = 0;

	hr = pRichEditOle->InsertObject(&reobject);

Ret0:
	if (pOleObject != NULL)
		pOleObject->Release();

	if (pImageOle != NULL)
		pImageOle->Release();

	if (pOleClientSite != NULL)
		pOleClientSite->Release();

	if (pRichEditOle != NULL)
		pRichEditOle->Release();

	if (bstrFileName != NULL)
		::SysFreeString(bstrFileName);

	return SUCCEEDED(hr);
}

// 获取文本
void RichEdit_GetText(HWND hWnd, tstring& strText)
{
	REOBJECT reobject;
	LONG nFaceId, nPos = 0;
	tstring strOrgText, strTemp;

	IRichEditOle* pRichEditOle = RichEdit_GetOleInterface(hWnd);
	if (NULL == pRichEditOle)
		return;

	CHARRANGE chrg = {0, RichEdit_GetWindowTextLength(hWnd)};
	RichEdit_GetTextRange(hWnd, &chrg, strOrgText);

	for (LONG i = 0; i < (int)strOrgText.size(); i++)
	{
		memset(&reobject, 0, sizeof(REOBJECT));
		reobject.cbStruct = sizeof(REOBJECT);
		reobject.cp = i;
		HRESULT hr = pRichEditOle->GetObject(REO_IOB_USE_CP, &reobject, REO_GETOBJ_POLEOBJ);
		if (SUCCEEDED(hr))
		{
			if (reobject.cp > 0 && reobject.cp > nPos)
			{
				strTemp = strOrgText.substr(nPos, reobject.cp-nPos);
				Replace(strTemp, _T("/"), _T("//"));
				strText += strTemp;
			}
			nPos = reobject.cp + 1;

			if (NULL == reobject.poleobj)
				continue;

			if (CLSID_ImageOle == reobject.clsid)
			{
				IImageOle* pImageOle = NULL;
				hr = reobject.poleobj->QueryInterface(__uuidof(IImageOle), (void**)&pImageOle);
				if (SUCCEEDED(hr))
				{
					pImageOle->GetFaceId(&nFaceId);
					if (nFaceId != -1)
					{
						TCHAR cBuf[32] = {0};
						wsprintf(cBuf, _T("/f[\"%03d\"]"), nFaceId);
						strText += cBuf;
					}
					else
					{
						strText += _T("/c[\"");
						BSTR bstrFileName = NULL;
						pImageOle->GetFileName(&bstrFileName);
						strText += bstrFileName;
						::SysFreeString(bstrFileName);
						strText += _T("\"]");
					}
					pImageOle->Release();
				}
			}
			reobject.poleobj->Release();
		}
	}

// 	int nCount = pRichEditOle->GetObjectCount();
// 	for (int i = 0; i < nCount; i++)
// 	{
// 		memset(&reobject, 0, sizeof(REOBJECT));
// 		reobject.cbStruct = sizeof(REOBJECT);
// 		HRESULT hr = pRichEditOle->GetObject(i, &reobject, REO_GETOBJ_POLEOBJ);
// 		if (hr == S_OK)
// 		{
// 			if (reobject.cp > 0 && reobject.cp > nPos)
// 			{
// 				strTemp = strOrgText.substr(nPos, reobject.cp-nPos);
// 				Replace(strTemp, _T("/"), _T("//"));
// 				strText += strTemp;
// 			}
// 			nPos = reobject.cp+1;
// 
// 			if (NULL == reobject.poleobj)
// 				continue;
// 
// 			if (CLSID_ImageOle == reobject.clsid)
// 			{
// 				pImageOle = (IImageOle*)reobject.poleobj;
// 				pImageOle = NULL;
// 				reobject.poleobj->QueryInterface(__uuidof(IImageOle), (void**)&pImageOle);
// 
// 				pImageOle->GetFaceId(&nFaceId);
// 				if (nFaceId != -1)
// 				{
// 					TCHAR cBuf[32] = {0};
// 					wsprintf(cBuf, _T("/f[\"%03d\"]"), nFaceId);
// 					strText += cBuf;
// 				}
// 				else
// 				{
// 					strText += _T("/c[\"");
// 					BSTR bstrFileName = NULL;
// 					pImageOle->GetFileName(&bstrFileName);
// 					strText += bstrFileName;
// 					::SysFreeString(bstrFileName);
// 					strText += _T("\"]");
// 				}
// 				pImageOle->Release();
// 			}
// 
// 			reobject.poleobj->Release();
// 		}
// 	}

	if (nPos < (int)strOrgText.size())
	{
		strTemp = strOrgText.substr(nPos);
		Replace(strTemp, _T("/"), _T("//"));
		strText += strTemp;
	}

	pRichEditOle->Release();
}


// 获取文本
void RichEdit_GetImageInfo(HWND hWnd, std::vector<ImageInfo*>& arrImageInfo)
{
	REOBJECT reobject;
	LONG nFaceId, nPos = 0;
	tstring strOrgText, strTemp;
	tstring strText;

	IRichEditOle* pRichEditOle = RichEdit_GetOleInterface(hWnd);
	if (NULL == pRichEditOle)
		return;

	CHARRANGE chrg = {0, RichEdit_GetWindowTextLength(hWnd)};
	RichEdit_GetTextRange(hWnd, &chrg, strOrgText);
	ImageInfo* pImageInfo = NULL;

	for (LONG i = 0; i < (int)strOrgText.size(); i++)
	{
		memset(&reobject, 0, sizeof(REOBJECT));
		reobject.cbStruct = sizeof(REOBJECT);
		reobject.cp = i;
		HRESULT hr = pRichEditOle->GetObject(REO_IOB_USE_CP, &reobject, REO_GETOBJ_POLEOBJ);
		if (SUCCEEDED(hr))
		{
			if (reobject.cp > 0 && reobject.cp > nPos)
			{
				strTemp = strOrgText.substr(nPos, reobject.cp-nPos);
				Replace(strTemp, _T("/"), _T("//"));
				strText += strTemp;
			}
			nPos = reobject.cp + 1;

			if (NULL == reobject.poleobj)
				continue;

			if (CLSID_ImageOle == reobject.clsid)
			{
				IImageOle* pImageOle = NULL;
				hr = reobject.poleobj->QueryInterface(__uuidof(IImageOle), (void**)&pImageOle);
				if (SUCCEEDED(hr))
				{
					pImageOle->GetFaceId(&nFaceId);
					if (nFaceId != -1)
					{
						TCHAR cBuf[32] = {0};
						wsprintf(cBuf, _T("/f[\"%03d\"]"), nFaceId);
						strText += cBuf;
					}
					else
					{
						pImageInfo = new ImageInfo();
						//pImageInfo
						memset(pImageInfo, 0, sizeof(ImageInfo));
						strText += _T("/c[\"");
						BSTR bstrFileName = NULL;
						pImageOle->GetFileName(&bstrFileName);
						_tcscpy_s(pImageInfo->szPath, ARRAYSIZE(pImageInfo->szPath), bstrFileName);
						pImageInfo->nStartPos = nPos-1;
						pImageInfo->nEndPos = nPos;
						arrImageInfo.push_back(pImageInfo);
						strText += bstrFileName;
						::SysFreeString(bstrFileName);
						strText += _T("\"]");
					}
					pImageOle->Release();
				}
			}
			reobject.poleobj->Release();
		}
	}


	if (nPos < (int)strOrgText.size())
	{
		strTemp = strOrgText.substr(nPos);
		Replace(strTemp, _T("/"), _T("//"));
		strText += strTemp;
	}

	pRichEditOle->Release();
}

// 替换选中文本
void RichEdit_ReplaceSel(HWND hWnd, LPCTSTR lpszNewText, LPCTSTR lpFontName,
						 int nFontSize,	COLORREF clrText, BOOL bBold, BOOL bItalic, 
						 BOOL bUnderLine, BOOL bIsLink, int nStartIndent, BOOL bCanUndo/* = FALSE*/)
{
	long lStartChar = 0, lEndChar = 0;
	RichEdit_GetSel(hWnd, lStartChar, lEndChar);
	RichEdit_ReplaceSel(hWnd, lpszNewText, bCanUndo);
	lEndChar = lStartChar + _tcslen(lpszNewText);
	RichEdit_SetSel(hWnd, lStartChar, lEndChar);
	RichEdit_SetFont(hWnd, lpFontName, nFontSize, clrText, bBold, bItalic, bUnderLine, bIsLink);
	RichEdit_SetStartIndent(hWnd, nStartIndent);
	RichEdit_SetSel(hWnd, lEndChar, lEndChar);
}

BOOL RichEdit_GetImageOle(HWND hWnd, POINT pt, IImageOle** pImageOle)
{
	IRichEditOle* pRichEditOle;
	REOBJECT reobject;
	HRESULT hr;
	BOOL bRet = FALSE;

	if (NULL == pImageOle)
		return FALSE;

	*pImageOle = NULL;

	pRichEditOle = RichEdit_GetOleInterface(hWnd);
	if (NULL == pRichEditOle)
		return FALSE;

	int nCount = pRichEditOle->GetObjectCount();
	for (int i = 0; i < nCount; i++)
	{
		memset(&reobject, 0, sizeof(REOBJECT));
		reobject.cbStruct = sizeof(REOBJECT);
		hr = pRichEditOle->GetObject(i, &reobject, REO_GETOBJ_POLEOBJ);
		if (S_OK == hr)
		{
			if (reobject.poleobj != NULL)
			{
				if (CLSID_ImageOle == reobject.clsid)
				{
					IImageOle* pImageOle2 = NULL;
					hr = reobject.poleobj->QueryInterface(__uuidof(IImageOle), (void**)&pImageOle2);
					reobject.poleobj->Release();
					if (SUCCEEDED(hr))
					{
						RECT rcObject = {0};
						hr = pImageOle2->GetObjectRect(&rcObject);
						if (SUCCEEDED(hr))
						{
							if (::PtInRect(&rcObject, pt))
							{
								*pImageOle = pImageOle2;
								bRet = TRUE;
								break;
							}
						}
						pImageOle2->Release();
					}
				}
				else
				{
					reobject.poleobj->Release();
				}
			}
		}
	}

	pRichEditOle->Release();

	return bRet;
}

BOOL RichEdit_GetImageOle(HWND hWnd, CRect& rcRect, IImageOle** pImageOle)
{
	IRichEditOle* pRichEditOle;
	REOBJECT reobject;
	HRESULT hr;
	BOOL bRet = FALSE;

	if (NULL == pImageOle)
		return FALSE;

	*pImageOle = NULL;

	pRichEditOle = RichEdit_GetOleInterface(hWnd);
	if (NULL == pRichEditOle)
		return FALSE;

	int nCount = pRichEditOle->GetObjectCount();
	for (int i = nCount-1; i >= 0; --i)
	{
		memset(&reobject, 0, sizeof(REOBJECT));
		reobject.cbStruct = sizeof(REOBJECT);
		hr = pRichEditOle->GetObject(i, &reobject, REO_GETOBJ_POLEOBJ);
		if (S_OK == hr)
		{
			if (reobject.poleobj != NULL)
			{
				if (CLSID_ImageOle == reobject.clsid)
				{
					IImageOle* pImageOle2 = NULL;
					hr = reobject.poleobj->QueryInterface(__uuidof(IImageOle), (void**)&pImageOle2);
					reobject.poleobj->Release();
					if (SUCCEEDED(hr))
					{
						RECT rcObject = {0};
						hr = pImageOle2->GetObjectRect(&rcObject);
						if (SUCCEEDED(hr))
						{
							rcRect = rcObject;
							break;
						}
						pImageOle2->Release();
					}
				}
				else
				{
					reobject.poleobj->Release();
				}
			}
		}
	}

	pRichEditOle->Release();

	return !bRet;
}

int RichEdit_GetCustomPicCount(HWND hWnd)
{
	IRichEditOle* pRichEditOle;
	REOBJECT reobject;
	HRESULT hr;

	pRichEditOle = RichEdit_GetOleInterface(hWnd);
	if (NULL == pRichEditOle)
		return 0;

	int nCustomPicCnt = 0;
	for (int i = 0; i < pRichEditOle->GetObjectCount(); i++)
	{
		memset(&reobject, 0, sizeof(REOBJECT));
		reobject.cbStruct = sizeof(REOBJECT);
		hr = pRichEditOle->GetObject(i, &reobject, REO_GETOBJ_POLEOBJ);
		if (S_OK == hr)
		{
			if (reobject.poleobj != NULL)
			{
				if (CLSID_ImageOle == reobject.clsid)
				{
					IImageOle* pImageOle2 = NULL;
					hr = reobject.poleobj->QueryInterface(__uuidof(IImageOle), (void**)&pImageOle2);
					reobject.poleobj->Release();
					if (SUCCEEDED(hr))
					{
						LONG lFaceId = -1, lFaceIndex = -1;
						pImageOle2->GetFaceId(&lFaceId);
						pImageOle2->GetFaceIndex(&lFaceIndex);
						if (-1 == lFaceId && -1 == lFaceIndex)
							nCustomPicCnt++;
						pImageOle2->Release();
					}
				}
				else
				{
					reobject.poleobj->Release();
				}
			}
		}
	}

	pRichEditOle->Release();

	return nCustomPicCnt;
}

IRichEditOle* RichEdit_GetOleInterface(ITextServices* pTextServices)
{
	IRichEditOle*pRichEditOle = NULL;
	pTextServices->TxSendMessage(EM_GETOLEINTERFACE, 0, (LPARAM)&pRichEditOle, NULL);
	return pRichEditOle;
}

int RichEdit_GetWindowTextLength(ITextServices* pTextServices)
{
	HRESULT lRes = 0;
	pTextServices->TxSendMessage(WM_GETTEXTLENGTH, 0, 0, &lRes);
	return (int)lRes;
}

int RichEdit_GetWindowText(ITextServices* pTextServices, LPTSTR lpszStringBuf, int nMaxCount)
{
	HRESULT lRes = 0;
	pTextServices->TxSendMessage(WM_GETTEXT, nMaxCount, (LPARAM)lpszStringBuf, &lRes);
	return (int)lRes;
}

int RichEdit_GetWindowText(ITextServices* pTextServices, tstring& strText)
{
	int nLength;
	TCHAR* pszText;

	nLength = RichEdit_GetWindowTextLength(pTextServices);
	pszText = new TCHAR[nLength+1];
	if (NULL == pszText)
		return 0;
	memset(pszText, 0, (nLength+1)*sizeof(TCHAR));
	nLength = RichEdit_GetWindowText(pTextServices, pszText, nLength+1);
	strText = pszText;
	delete []pszText;

	return nLength;
}

int RichEdit_GetTextRange(ITextServices* pTextServices, CHARRANGE* lpchrg, tstring& strText)
{
	strText = _T("");

	if (NULL == lpchrg || lpchrg->cpMax <= lpchrg->cpMin)
		return 0;

	LONG nLen = lpchrg->cpMax - lpchrg->cpMin;
	TCHAR* pText = new TCHAR[nLen+1];
	if (NULL == pText)
		return 0;

	memset(pText, 0, (nLen+1)*sizeof(TCHAR));

	TEXTRANGE tr = { 0 };
	tr.chrg =*lpchrg;
	tr.lpstrText = pText;

	HRESULT lRes = 0;
	pTextServices->TxSendMessage(EM_GETTEXTRANGE, 0, (LPARAM)&tr, &lRes);

	strText = pText;
	delete []pText;

	return (int)lRes;
}

DWORD RichEdit_GetDefaultCharFormat(ITextServices* pTextServices, CHARFORMAT& cf)
{
	cf.cbSize = sizeof(CHARFORMAT);
	HRESULT lRes = 0;
	pTextServices->TxSendMessage(EM_GETCHARFORMAT, 0, (LPARAM)&cf, &lRes);
	return (DWORD)lRes;
}

BOOL RichEdit_SetDefaultCharFormat(ITextServices* pTextServices, CHARFORMAT& cf)
{
	cf.cbSize = sizeof(CHARFORMAT);
	HRESULT lRes = 0;
	pTextServices->TxSendMessage(EM_SETCHARFORMAT, 0, (LPARAM)&cf, &lRes);
	return (BOOL)lRes;
}

DWORD RichEdit_GetSelectionCharFormat(ITextServices* pTextServices, CHARFORMAT& cf)
{
	cf.cbSize = sizeof(CHARFORMAT);
	LRESULT lRes = 0;
	pTextServices->TxSendMessage(EM_GETCHARFORMAT, 1, (LPARAM)&cf, &lRes);
	return (DWORD)lRes;
}

BOOL RichEdit_SetSelectionCharFormat(ITextServices* pTextServices, CHARFORMAT& cf)
{
	cf.cbSize = sizeof(CHARFORMAT);
	LRESULT lRes = 0;
	pTextServices->TxSendMessage(EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf, &lRes);
	return (BOOL)lRes;
}

void RichEdit_ReplaceSel(ITextServices* pTextServices, LPCTSTR lpszNewText, BOOL bCanUndo/* = FALSE*/)
{
	pTextServices->TxSendMessage(EM_REPLACESEL, (WPARAM)bCanUndo, (LPARAM)lpszNewText, NULL);
}

void RichEdit_GetSel(ITextServices* pTextServices, LONG& nStartChar, LONG& nEndChar)
{
	CHARRANGE cr = { 0, 0 };
	pTextServices->TxSendMessage(EM_EXGETSEL, 0, (LPARAM)&cr, NULL);
	nStartChar = cr.cpMin;
	nEndChar = cr.cpMax;
}

int RichEdit_SetSel(ITextServices* pTextServices, LONG nStartChar, LONG nEndChar)
{
	CHARRANGE cr = { nStartChar, nEndChar };
	HRESULT lRes = 0;
	pTextServices->TxSendMessage(EM_EXSETSEL, 0, (LPARAM)&cr, &lRes);
	return (int)lRes;
}

// 设置默认字体
void RichEdit_SetDefFont(ITextServices* pTextServices, LPCTSTR lpFontName,	
						 int nFontSize,	COLORREF clrText, BOOL bBold, 
						 BOOL bItalic, BOOL bUnderLine, BOOL bIsLink)
{
	CHARFORMAT cf;
	memset(&cf, 0, sizeof(cf));
	RichEdit_GetDefaultCharFormat(pTextServices, cf);

	if (lpFontName != NULL)					// 设置字体名称
	{
		cf.dwMask = cf.dwMask | CFM_FACE;
		_tcscpy(cf.szFaceName, lpFontName);
	}

	if (nFontSize > 0)						// 设置字体高度
	{
		cf.dwMask = cf.dwMask | CFM_SIZE;
		cf.yHeight = nFontSize* 20;
	}

	cf.dwMask |= CFM_COLOR | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_LINK;

	cf.crTextColor = clrText;				// 设置字体颜色
	cf.dwEffects &= ~CFE_AUTOCOLOR;

	if (bBold)								// 设置粗体
		cf.dwEffects |= CFE_BOLD;
	else
		cf.dwEffects &= ~CFE_BOLD;

	if (bItalic)							// 设置倾斜
		cf.dwEffects |= CFE_ITALIC;
	else
		cf.dwEffects &= ~CFE_ITALIC;

	if (bUnderLine)							// 设置下划线
		cf.dwEffects |= CFE_UNDERLINE;
	else
		cf.dwEffects &= ~CFE_UNDERLINE;

	if (bIsLink)							// 设置超链接
		cf.dwEffects |= CFE_LINK;
	else
		cf.dwEffects &= ~CFE_LINK;

	RichEdit_SetDefaultCharFormat(pTextServices, cf);
}

// 设置字体
void RichEdit_SetFont(ITextServices* pTextServices, LPCTSTR lpFontName, int nFontSize,
					  COLORREF clrText, BOOL bBold, BOOL bItalic, BOOL bUnderLine, BOOL bIsLink)
{
	CHARFORMAT cf;
	memset(&cf, 0, sizeof(cf));
	RichEdit_GetSelectionCharFormat(pTextServices, cf);

	if (lpFontName != NULL)					// 设置字体名称
	{
		cf.dwMask = cf.dwMask | CFM_FACE;
		_tcscpy(cf.szFaceName, lpFontName);
	}

	if (nFontSize > 0)						// 设置字体高度
	{
		cf.dwMask = cf.dwMask | CFM_SIZE;
		cf.yHeight = nFontSize* 20;
	}

	cf.dwMask |= CFM_COLOR | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_LINK;

	cf.crTextColor = clrText;				// 设置字体颜色
	cf.dwEffects &= ~CFE_AUTOCOLOR;

	if (bBold)								// 设置粗体
		cf.dwEffects |= CFE_BOLD;
	else
		cf.dwEffects &= ~CFE_BOLD;

	if (bItalic)							// 设置倾斜
		cf.dwEffects |= CFE_ITALIC;
	else
		cf.dwEffects &= ~CFE_ITALIC;

	if (bUnderLine)							// 设置下划线
		cf.dwEffects |= CFE_UNDERLINE;
	else
		cf.dwEffects &= ~CFE_UNDERLINE;

	if (bIsLink)							// 设置超链接
		cf.dwEffects |= CFE_LINK;
	else
		cf.dwEffects &= ~CFE_LINK;

	RichEdit_SetSelectionCharFormat(pTextServices, cf);
}

// 设置左缩进(单位:缇)
BOOL RichEdit_SetStartIndent(ITextServices* pTextServices, int nSize)
{
	PARAFORMAT2 pf2;
	memset(&pf2, 0, sizeof(pf2));
	pf2.cbSize = sizeof(PARAFORMAT2);
	pf2.dwMask = PFM_STARTINDENT;
	pf2.dxStartIndent = nSize;
	HRESULT lRes = 0;
	pTextServices->TxSendMessage(EM_SETPARAFORMAT, 0, (LPARAM)&pf2, &lRes);
	return (BOOL)lRes;
}

// 插入表情图片
BOOL RichEdit_InsertFace(ITextServices*pTextServices, ITextHost*pTextHost,	
						 LPCTSTR lpszFileName, int nFaceId,	int nFaceIndex, 
						 COLORREF clrBg, BOOL bAutoScale, int nReservedWidth, long cxImage/*=0*/, long cyImage/*=0*/)
{
	BSTR bstrFileName = NULL;
	IRichEditOle* pRichEditOle = NULL;
	IOleClientSite*pOleClientSite = NULL;
	IImageOle* pImageOle = NULL;
	IOleObject*pOleObject = NULL;
	REOBJECT reobject = {0};
	HRESULT hr = E_FAIL;

	if (NULL == pTextServices || NULL == pTextHost ||
		NULL == lpszFileName || NULL ==*lpszFileName)
		return FALSE;

	TCHAR cProtocol[16] = {0};
	_tcsncpy(cProtocol, lpszFileName, 7);
	if ((_tcsicmp(cProtocol, _T("http://")) != 0) && ::GetFileAttributes(lpszFileName) == 0xFFFFFFFF)
		return FALSE;

	bstrFileName = ::SysAllocString(lpszFileName);
	if (NULL == bstrFileName)
		return FALSE;

	pTextServices->TxSendMessage(EM_GETOLEINTERFACE, 0, (LPARAM)&pRichEditOle, NULL);
	if (NULL == pRichEditOle)
		goto Ret0;

	hr = ::CoCreateInstance(CLSID_ImageOle, NULL, 
		CLSCTX_INPROC_SERVER, IID_IImageOle, (void**)&pImageOle);
	if (FAILED(hr))
	{
		if(!bIsInstallImageOleCtrl)
		{
			RegisterCom_ImageOleCtrl();
			bIsInstallImageOleCtrl = TRUE;
		}
		
		goto Ret0;	
	}
		

	hr = pImageOle->QueryInterface(IID_IOleObject, (void**)&pOleObject);
	if (FAILED(hr))
		goto Ret0;

	pRichEditOle->GetClientSite(&pOleClientSite);
	if (NULL == pOleClientSite)
		goto Ret0;

	pOleObject->SetClientSite(pOleClientSite);

	pImageOle->SetTextServices(pTextServices);
	pImageOle->SetTextHost(pTextHost);
	pImageOle->SetFaceId(nFaceId);
	pImageOle->SetFaceIndex(nFaceIndex);
	pImageOle->SetBgColor(clrBg);
	pImageOle->SetAutoScale(bAutoScale, nReservedWidth);
	pImageOle->LoadFromFile(bstrFileName);

	hr = ::OleSetContainedObject(pOleObject, TRUE);

	reobject.cbStruct = sizeof(REOBJECT);
	reobject.clsid    = CLSID_ImageOle;
	reobject.cp       = REO_CP_SELECTION;
	reobject.dvaspect = DVASPECT_CONTENT;
	reobject.dwFlags  = REO_BELOWBASELINE;
	reobject.poleobj  = pOleObject;
	reobject.polesite = pOleClientSite;
	reobject.dwUser   = 0;
	reobject.sizel.cx = cxImage;
	reobject.sizel.cy = cyImage;

	hr = pRichEditOle->InsertObject(&reobject);

Ret0:
	if (pOleObject != NULL)
		pOleObject->Release();

	if (pImageOle != NULL)
	{
		//pImageOle->Destroy();
		pImageOle->Release();
	}
	

	if (pOleClientSite != NULL)
		pOleClientSite->Release();

	if (pRichEditOle != NULL)
		pRichEditOle->Release();

	if (bstrFileName != NULL)
		::SysFreeString(bstrFileName);

	return SUCCEEDED(hr);
}

// 获取文本
void RichEdit_GetText(ITextServices* pTextServices, tstring& strText)
{
	REOBJECT reobject;
	LONG nFaceId, nPos = 0;
	tstring strOrgText, strTemp;

	IRichEditOle* pRichEditOle = RichEdit_GetOleInterface(pTextServices);
	if (NULL == pRichEditOle)
		return;

	CHARRANGE chrg = {0, RichEdit_GetWindowTextLength(pTextServices)};
	RichEdit_GetTextRange(pTextServices, &chrg, strOrgText);

	for (LONG i = 0; i < (int)strOrgText.size(); i++)
	{
		memset(&reobject, 0, sizeof(REOBJECT));
		reobject.cbStruct = sizeof(REOBJECT);
		reobject.cp = i;
		HRESULT hr = pRichEditOle->GetObject(REO_IOB_USE_CP, &reobject, REO_GETOBJ_POLEOBJ);
		if (SUCCEEDED(hr))
		{
			if (reobject.cp > 0 && reobject.cp > nPos)
			{
				strTemp = strOrgText.substr(nPos, reobject.cp-nPos);
				Replace(strTemp, _T("/"), _T("//"));
				strText += strTemp;
			}
			nPos = reobject.cp + 1;

			if (NULL == reobject.poleobj)
				continue;

			if (CLSID_ImageOle == reobject.clsid)
			{
				IImageOle* pImageOle = NULL;
				hr = reobject.poleobj->QueryInterface(__uuidof(IImageOle), (void**)&pImageOle);
				if (SUCCEEDED(hr))
				{
					pImageOle->GetFaceId(&nFaceId);
					if (nFaceId != -1)
					{
						TCHAR cBuf[32] = {0};
						wsprintf(cBuf, _T("/f[\"%03d\"]"), nFaceId);
						strText += cBuf;
					}
					else
					{
						strText += _T("/c[\"");
						BSTR bstrFileName = NULL;
						pImageOle->GetFileName(&bstrFileName);
						strText += bstrFileName;
						::SysFreeString(bstrFileName);
						strText += _T("\"]");
					}
					pImageOle->Release();
				}
			}
			reobject.poleobj->Release();
		}
	}

	if (nPos < (int)strOrgText.size())
	{
		strTemp = strOrgText.substr(nPos);
		Replace(strTemp, _T("/"), _T("//"));
		strText += strTemp;
	}

	pRichEditOle->Release();
}

// 替换选中文本
void RichEdit_ReplaceSel(ITextServices* pTextServices, LPCTSTR lpszNewText,	
						 LPCTSTR lpFontName, int nFontSize,	COLORREF clrText, 
						 BOOL bBold, BOOL bItalic, BOOL bUnderLine, BOOL bIsLink, 
						 int nStartIndent, BOOL bCanUndo/* = FALSE*/)
{
	long lStartChar = 0, lEndChar = 0;
	RichEdit_GetSel(pTextServices, lStartChar, lEndChar);
	RichEdit_ReplaceSel(pTextServices, lpszNewText, bCanUndo);
	lEndChar = lStartChar + _tcslen(lpszNewText);
	RichEdit_SetSel(pTextServices, lStartChar, lEndChar);
	RichEdit_SetFont(pTextServices, lpFontName, nFontSize, clrText, bBold, bItalic, bUnderLine, bIsLink);
	RichEdit_SetStartIndent(pTextServices, nStartIndent);
	RichEdit_SetSel(pTextServices, lEndChar, lEndChar);
}

BOOL RichEdit_GetImageOle(ITextServices* pTextServices, POINT pt, IImageOle** pImageOle)
{
	IRichEditOle* pRichEditOle;
	REOBJECT reobject;
	HRESULT hr;
	BOOL bRet = FALSE;

	if (NULL == pImageOle)
		return FALSE;

	*pImageOle = NULL;

	pRichEditOle = RichEdit_GetOleInterface(pTextServices);
	if (NULL == pRichEditOle)
		return FALSE;

	int nCount = pRichEditOle->GetObjectCount();
	for (int i = 0; i < nCount; i++)
	{
		memset(&reobject, 0, sizeof(REOBJECT));
		reobject.cbStruct = sizeof(REOBJECT);
		hr = pRichEditOle->GetObject(i, &reobject, REO_GETOBJ_POLEOBJ);
		if (S_OK == hr)
		{
			if (reobject.poleobj != NULL)
			{
				if (CLSID_ImageOle == reobject.clsid)
				{
					IImageOle* pImageOle2 = NULL;
					hr = reobject.poleobj->QueryInterface(__uuidof(IImageOle), (void**)&pImageOle2);
					reobject.poleobj->Release();
					if (SUCCEEDED(hr))
					{
						RECT rcObject = {0};
						hr = pImageOle2->GetObjectRect(&rcObject);
						if (SUCCEEDED(hr))
						{
							if (::PtInRect(&rcObject, pt))
							{
								*pImageOle = pImageOle2;
								bRet = TRUE;
								break;
							}
						}
						pImageOle2->Release();
					}
				}
				else
				{
					reobject.poleobj->Release();
				}
			}
		}
	}

	pRichEditOle->Release();

	return bRet;
}

int RichEdit_GetCustomPicCount(ITextServices* pTextServices)
{
	IRichEditOle* pRichEditOle;
	REOBJECT reobject;
	HRESULT hr;

	pRichEditOle = RichEdit_GetOleInterface(pTextServices);
	if (NULL == pRichEditOle)
		return FALSE;

	int nCustomPicCnt = 0;
	for (int i = 0; i < pRichEditOle->GetObjectCount(); i++)
	{
		memset(&reobject, 0, sizeof(REOBJECT));
		reobject.cbStruct = sizeof(REOBJECT);
		hr = pRichEditOle->GetObject(i, &reobject, REO_GETOBJ_POLEOBJ);
		if (S_OK == hr)
		{
			if (reobject.poleobj != NULL)
			{
				if (CLSID_ImageOle == reobject.clsid)
				{
					IImageOle* pImageOle2 = NULL;
					hr = reobject.poleobj->QueryInterface(__uuidof(IImageOle), (void**)&pImageOle2);
					reobject.poleobj->Release();
					if (SUCCEEDED(hr))
					{
						LONG lFaceId = -1, lFaceIndex = -1;
						pImageOle2->GetFaceId(&lFaceId);
						pImageOle2->GetFaceIndex(&lFaceIndex);
						if (-1 == lFaceId && -1 == lFaceIndex)
							nCustomPicCnt++;
						pImageOle2->Release();
					}
				}
				else
				{
					reobject.poleobj->Release();
				}
			}
		}
	}

	pRichEditOle->Release();

	return nCustomPicCnt;
}