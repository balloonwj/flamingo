#include "stdafx.h"
#include "ImageEx.h"

CImageEx::CImageEx(void)
{
	m_bIsNinePart = FALSE;
	::SetRectEmpty(&m_rcNinePart);
}

CImageEx::~CImageEx(void)
{
}

BOOL CImageEx::LoadFromFile(LPCTSTR pszFileName)
{
	HRESULT hr = CImage::Load(pszFileName);
	if (hr == S_OK)
	{
		if (GetFileType(pszFileName) == 0)	// png
			AlphaPremultiplication();
		return TRUE;
	}
	else
	{
		return FALSE;
	}
}

BOOL CImageEx::LoadFromIStream(IStream* pStream)
{
	HRESULT hr = CImage::Load(pStream);
	if (hr == S_OK)
	{
		AlphaPremultiplication();
		return TRUE;
	}
	else
	{
		return FALSE;
	}
}

BOOL CImageEx::LoadFromBuffer(const BYTE* lpBuf, DWORD dwSize)
{
	if (NULL == lpBuf || dwSize <= 0)
		return FALSE;

	HGLOBAL hGlobal = ::GlobalAlloc(GHND, dwSize);
	if (NULL == hGlobal)
		return FALSE;

	LPVOID lpBuffer = ::GlobalLock(hGlobal);
	if (NULL == lpBuffer)
	{
		::GlobalFree(hGlobal);
		return FALSE;
	}

	memcpy(lpBuffer, lpBuf, dwSize);
	::GlobalUnlock(hGlobal);

	LPSTREAM lpStream = NULL;
	HRESULT hr = ::CreateStreamOnHGlobal(hGlobal, TRUE, &lpStream);
	if (hr != S_OK)
	{
		::GlobalFree(hGlobal);
		return FALSE;
	}
	
	BOOL bRet = LoadFromIStream(lpStream);
	lpStream->Release();

	return bRet;
}

BOOL CImageEx::LoadFromResource(HINSTANCE hInstance, LPCTSTR pszResourceName, LPCTSTR pszResType)
{
	HRSRC hRsrc = ::FindResource(hInstance, pszResourceName, pszResType);
	if (NULL == hRsrc)
		return FALSE;

	DWORD dwSize = ::SizeofResource(hInstance, hRsrc); 
	if (0 == dwSize)
		return FALSE;

	HGLOBAL hGlobal = ::LoadResource(hInstance, hRsrc); 
	if (NULL == hGlobal)
		return FALSE;

	LPVOID pBuffer = ::LockResource(hGlobal);
	if (NULL == pBuffer)
	{
		::FreeResource(hGlobal);
		return FALSE;
	}

	HGLOBAL hGlobal2 = ::GlobalAlloc(GHND, dwSize);
	if (NULL == hGlobal2)
	{
		::FreeResource(hGlobal);
		return FALSE;
	}

	LPVOID pBuffer2 = ::GlobalLock(hGlobal2);
	if (NULL == pBuffer2)
	{
		::GlobalFree(hGlobal2);
		::FreeResource(hGlobal);
		return FALSE;
	}

	memcpy(pBuffer2, pBuffer, dwSize);
	::GlobalUnlock(hGlobal2);

	LPSTREAM pStream = NULL;
	HRESULT hr = ::CreateStreamOnHGlobal(hGlobal2, TRUE, &pStream);
	if (hr != S_OK)
	{
		::GlobalFree(hGlobal2);
		::FreeResource(hGlobal);
		return FALSE;
	}

	BOOL bRet = LoadFromIStream(pStream);

	if (pStream)
		pStream->Release();

	::FreeResource(hGlobal);

	return bRet;
}

BOOL CImageEx::LoadFromResource(HINSTANCE hInstance, UINT nIDResource, LPCTSTR pszResType)
{
	return LoadFromResource(hInstance, MAKEINTRESOURCE(nIDResource), pszResType);
}

void CImageEx::SetNinePart(const RECT* lpNinePart)
{
	if ((NULL == lpNinePart) || (0 == lpNinePart->left && 0 == lpNinePart->top
		&& 0 == lpNinePart->right && 0 == lpNinePart->bottom))
	{
		m_bIsNinePart = FALSE;
		::SetRectEmpty(&m_rcNinePart);
	}
	else
	{
		m_bIsNinePart = TRUE;
		m_rcNinePart =*lpNinePart;
	}
}

BOOL CImageEx::Draw2(HDC hDestDC, const RECT& rectDest)
{
	if (m_bIsNinePart)
	{
		int nWidth = rectDest.right - rectDest.left;
		int nHeight = rectDest.bottom - rectDest.top;
		if (GetWidth() != nWidth || GetHeight() != nHeight)
		{
			return DrawNinePartImage(hDestDC, rectDest.left, rectDest.top, nWidth, nHeight, 
				m_rcNinePart.left, m_rcNinePart.top, m_rcNinePart.right, m_rcNinePart.bottom);
		}
	}
	
	return Draw(hDestDC, rectDest);
}

// 图像灰度化
void CImageEx::GrayScale()
{
	int nWidth = GetWidth();
	int nHeight = GetHeight();

	BYTE* pArray = (BYTE*)GetBits();
	int nPitch = GetPitch();
	int nBitCount = GetBPP() / 8;

	for (int i = 0; i < nHeight; i++) 
	{
		for (int j = 0; j < nWidth; j++) 
		{
			int grayVal = (BYTE)(((*(pArray + nPitch* i + j* nBitCount)* 306)
				+ (*(pArray + nPitch* i + j* nBitCount + 1)* 601)
				+ (*(pArray + nPitch* i + j* nBitCount + 2)* 117) + 512 ) >> 10);	// 计算灰度值

			*(pArray + nPitch* i + j* nBitCount) = grayVal;							// 赋灰度值
			*(pArray + nPitch* i + j* nBitCount + 1) = grayVal;
			*(pArray + nPitch* i + j* nBitCount + 2) = grayVal;
		}
	}
}

// Alpha预乘
BOOL CImageEx::AlphaPremultiplication()
{
	LPVOID pBitsSrc = NULL;
	BYTE* psrc = NULL;
	BITMAP stBmpInfo;

	HBITMAP hBmp = (HBITMAP)*this;

	::GetObject(hBmp, sizeof(BITMAP), &stBmpInfo);

	// Only support 32bit DIB section
	if (32 != stBmpInfo.bmBitsPixel || NULL == stBmpInfo.bmBits)
		return FALSE;

	psrc = (BYTE*) stBmpInfo.bmBits;

	// Just mix it
	for (int nPosY = 0; nPosY < abs(stBmpInfo.bmHeight); nPosY++)
	{
		for (int nPosX = stBmpInfo.bmWidth; nPosX > 0; nPosX--)
		{
			BYTE alpha  = psrc[3];
			psrc[0] = (BYTE)((psrc[0]* alpha) / 255);
			psrc[1] = (BYTE)((psrc[1]* alpha) / 255);
			psrc[2] = (BYTE)((psrc[2]* alpha) / 255);
			psrc += 4;
		}
	}

	return TRUE;
}

BOOL CImageEx::DrawNinePartImage(int pleft, int ptop, int pright, int pbottom,
					   HDC hDC, int height, int width, int left, int top, int right, int bottom)
{
	// 左上
	{
		CRect rcDest(pleft, ptop, pleft+left, ptop+top);
		CRect rcSrc(0, 0, left, top);
		if (!rcDest.IsRectEmpty() && !rcSrc.IsRectEmpty())
			Draw(hDC, rcDest, rcSrc);
	}

	// 左边
	{
		CRect rcDest(pleft, top+ptop, pleft+left, top+(height-top-bottom-ptop-pbottom));
		CRect rcSrc(0, top, left, top+(GetHeight()-top-bottom));
		if (!rcDest.IsRectEmpty() && !rcSrc.IsRectEmpty())
			Draw(hDC, rcDest, rcSrc);
	}

	// 上边
	{
		CRect rcDest(left+pleft, ptop, (left+pleft)+(width-left-right-pleft-pright), ptop+top);
		CRect rcSrc(left, 0, left+(GetWidth()-left-right), top);
		if (!rcDest.IsRectEmpty() && !rcSrc.IsRectEmpty())
			Draw(hDC, rcDest, rcSrc);
	}

	// 右上
	{
		CRect rcDest(width- right-pright, ptop, (width- right-pright)+right, ptop+top);
		CRect rcSrc(GetWidth()-right, 0, (GetWidth()-right)+right, top);
		if (!rcDest.IsRectEmpty() && !rcSrc.IsRectEmpty())
			Draw(hDC, rcDest, rcSrc);
	}

	// 右边
	{
		CRect rcDest(width-right-pright, top+ptop, (width-right-pright)+right, (top+ptop)+(height-top-bottom-ptop-pbottom));
		CRect rcSrc(GetWidth()-right, top, (GetWidth()-right)+right, top+(GetHeight()-top-bottom));
		if (!rcDest.IsRectEmpty() && !rcSrc.IsRectEmpty())
			Draw(hDC, rcDest, rcSrc);
	}

	// 下边
	{
		CRect rcDest(left+pleft, height-bottom-pbottom, (left+pleft)+(width-left-right-pleft-pright), (height-bottom-pbottom)+bottom);
		CRect rcSrc(left, GetHeight()-bottom, left+(GetWidth()-left-right), (GetHeight()-bottom)+bottom);
		if (!rcDest.IsRectEmpty() && !rcSrc.IsRectEmpty())
			Draw(hDC, rcDest, rcSrc);
	}

	// 右下
	{
		CRect rcDest(width-right-pright, height-bottom-pbottom, (width-right-pright)+right, (height-bottom-pbottom)+bottom);
		CRect rcSrc(GetWidth()-right, GetHeight()-bottom, (GetWidth()-right)+right, (GetHeight()-bottom)+bottom);
		if (!rcDest.IsRectEmpty() && !rcSrc.IsRectEmpty())
			Draw(hDC, rcDest, rcSrc);
	}

	// 左下
	{
		CRect rcDest(pleft, height-bottom-pbottom, pleft+left, (height-bottom-pbottom)+bottom);
		CRect rcSrc(0, GetHeight()-bottom, left, (GetHeight()-bottom)+bottom);
		if (!rcDest.IsRectEmpty() && !rcSrc.IsRectEmpty())
			Draw(hDC, rcDest, rcSrc);
	}

	// 中间
	{
		CRect rcDest(left+pleft, top+ptop, (left+pleft)+(width-left-right-pleft-pright), (top+ptop)+(height-top-bottom-ptop-pbottom));
		CRect rcSrc(left, top, left+(GetWidth()-left-right), top+(GetHeight()-top-bottom));
		if (!rcDest.IsRectEmpty() && !rcSrc.IsRectEmpty())
			Draw(hDC, rcDest, rcSrc);
	}

	return TRUE;
}

BOOL CImageEx::DrawNinePartImage(HDC hDC, int x, int y, int cx, int cy, 
								 int nLeft, int nTop, int nRight, int nBottom)
{
	int cxImage = GetWidth();
	int cyImage = GetHeight();

	// 左上
	{
		RECT rcDest = {x, y, x+nLeft, y+nTop};
		RECT rcSrc = {0, 0, nLeft, nTop};
		if (!::IsRectEmpty(&rcDest) && !::IsRectEmpty(&rcSrc))
			Draw(hDC, rcDest, rcSrc);
	}

	// 左边
	{
		RECT rcDest = {x, y+nTop, x+nLeft, (y+nTop)+(cy-nTop-nBottom)};
		RECT rcSrc = {0, nTop, nLeft, nTop+(cyImage-nTop-nBottom)};
		if (!::IsRectEmpty(&rcDest) && !::IsRectEmpty(&rcSrc))
			Draw(hDC, rcDest, rcSrc);
	}

	// 上边
	{
		RECT rcDest = {x+nLeft, y, (x+nLeft)+(cx-nLeft-nRight), y+nTop};
		RECT rcSrc = {nLeft, 0, nLeft+(cxImage-nLeft-nRight), nTop};
		if (!::IsRectEmpty(&rcDest) && !::IsRectEmpty(&rcSrc))
			Draw(hDC, rcDest, rcSrc);
	}

	// 右上
	{
		RECT rcDest = {x+(cx-nRight), y, (x+(cx-nRight))+nRight, y+nTop};
		RECT rcSrc = {cxImage-nRight, 0, (cxImage-nRight)+nRight, nTop};
		if (!::IsRectEmpty(&rcDest) && !::IsRectEmpty(&rcSrc))
			Draw(hDC, rcDest, rcSrc);
	}

	// 右边
	{
		RECT rcDest = {x+(cx-nRight), y+nTop, (x+(cx-nRight))+nRight, (y+nTop)+(cy-nTop-nBottom)};
		RECT rcSrc = {cxImage-nRight, nTop, (cxImage-nRight)+nRight, nTop+(cyImage-nTop-nBottom)};
		if (!::IsRectEmpty(&rcDest) && !::IsRectEmpty(&rcSrc))
			Draw(hDC, rcDest, rcSrc);
	}

	// 下边
	{
		RECT rcDest = {x+nLeft, y+(cy-nBottom), (x+nLeft)+(cx-nLeft-nRight), (y+(cy-nBottom))+nBottom};
		RECT rcSrc = {nLeft, cyImage-nBottom, nLeft+(cxImage-nLeft-nRight), (cyImage-nBottom)+nBottom};
		if (!::IsRectEmpty(&rcDest) && !::IsRectEmpty(&rcSrc))
			Draw(hDC, rcDest, rcSrc);
	}

	// 右下
	{
		RECT rcDest = {x+(cx-nRight), y+(cy-nBottom), (x+(cx-nRight))+nRight, (y+(cy-nBottom))+nBottom};
		RECT rcSrc = {cxImage-nRight, cyImage-nBottom, (cxImage-nRight)+nRight, (cyImage-nBottom)+nBottom};
		if (!::IsRectEmpty(&rcDest) && !::IsRectEmpty(&rcSrc))
			Draw(hDC, rcDest, rcSrc);
	}

	// 左下
	{
		RECT rcDest = {x, y+(cy-nBottom), x+nLeft, (y+(cy-nBottom))+nBottom};
		RECT rcSrc = {0, cyImage-nBottom, nLeft, (cyImage-nBottom)+nBottom};
		if (!::IsRectEmpty(&rcDest) && !::IsRectEmpty(&rcSrc))
			Draw(hDC, rcDest, rcSrc);
	}

	// 中间
	{
		RECT rcDest = {x+nLeft, y+nTop, (x+nLeft)+(cx-nLeft-nRight), (y+nTop)+(cy-nTop-nBottom)};
		RECT rcSrc = {nLeft, nTop, nLeft+(cxImage-nLeft-nRight), nTop+(cyImage-nTop-nBottom)};
		if (!::IsRectEmpty(&rcDest) && !::IsRectEmpty(&rcSrc))
			Draw(hDC, rcDest, rcSrc);
	}

	return TRUE;
}

//获取文件类型(通过文件头几个字节获取)
int CImageEx::GetFileType(LPCTSTR lpszFileName)
{
	unsigned char png_head[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
	unsigned char gif89a_head[6] = {'G','I','F','8','9','a'};
	unsigned char gif87a_head[6] = {'G','I','F','8','7','a'};
	unsigned char jpg_head[2] = {0xFF, 0xD8};
	unsigned char bmp_head[2] = {0x42, 0x4D};
	unsigned char cData[16] = {0};

	FILE* fp = _tfopen(lpszFileName, _T("rb"));
	if (NULL == fp)
		return -1;
	fread(cData, sizeof(cData), 1, fp);
	fclose(fp);

	if (!memcmp(cData, png_head, 8))
		return 0;
	else if (!memcmp(cData, gif89a_head, 6) || !memcmp(cData, gif87a_head, 6))
		return 1;
	if (!memcmp(cData, jpg_head, 2))
		return 2;
	else if (!memcmp(cData, bmp_head, 2))
		return 3;
	else
		return -1;
}