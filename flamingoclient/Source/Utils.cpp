#include "stdafx.h"
#include "Utils.h"
#include "Path.h"
#include <Iphlpapi.h>
#include <Wininet.h>
#include <Sensapi.h>

#pragma comment(lib,"Iphlpapi.lib")
#pragma comment (lib,"WinInet.lib")
#pragma comment(lib, "Sensapi.lib")

using namespace Gdiplus;

BOOL ToHexStr(const CHAR* lpStr, int nSrcLen, CHAR* lpHex, int nDestLen)
{
	const CHAR cHexTable[] = "0123456789ABCDEF";

	if (lpStr == NULL || nSrcLen <= 0
		|| lpHex == NULL || nDestLen <= 0)
		return FALSE;

	if (nDestLen < nSrcLen* 2)
		return FALSE;

	int i = 0;
	for (int j = 0; j < nSrcLen; j++)
	{
		unsigned int a = (unsigned int)lpStr[j];
		lpHex[i++] = cHexTable[(a & 0xf0) >> 4];
		lpHex[i++] = cHexTable[(a & 0x0f)];
	}
	lpHex[i] = '\0';

	return TRUE;
}

BOOL StrToHex(const CHAR* lpStr, CHAR* lpHex, int nLen)
{
	const CHAR cHexTable[] = "0123456789ABCDEF";

	if (lpStr == NULL || lpHex == NULL || nLen <= 0)
		return FALSE;

	int nSrcLen = strlen(lpStr);
	if (nLen <= nSrcLen* 2)
		return FALSE;

	int i = 0;
	for (int j = 0; j < (int)strlen(lpStr); j++)
	{
		unsigned int a = (unsigned int)lpStr[j];
		lpHex[i++] = cHexTable[(a & 0xf0) >> 4];
		lpHex[i++] = cHexTable[(a & 0x0f)];
	}
	lpHex[i] = '\0';

	return TRUE;
}

// 16位整型数据网络字节序与正常字节序转换
u_short Swap16(u_short nValue)
{
	u_short nRetValue = (u_short)((nValue & 0xff00) >> 0x08);
	nRetValue |= (u_short)(nValue << 0x08);
	return nRetValue;

}

// 32位整型数据网络字节序与正常字节序转换
u_long Swap32(u_long nValue)
{
	u_long nRetValue = ((nValue & 0xff000000) >> 0x18);
	nRetValue |= ((nValue & 0x00ff0000) >> 0x08);
	nRetValue |= ((nValue & 0x0000ff00) << 0x08);
	nRetValue |= ((nValue & 0x000000ff) << 0x18);

	return nRetValue;
}

COLORREF HexStrToRGB(LPCTSTR lpszStr)
{
	unsigned long lValue = _tcstoul(lpszStr, NULL, 16);
	return RGB((lValue & 0xFF0000) >> 16,  (lValue & 0xFF00 ) >> 8, lValue & 0xFF);
}

void RGBToHexStr(COLORREF color, TCHAR* lpBuf, int nLen)
{
	if (NULL == lpBuf || nLen <= 0)
		return;
	wsprintf(lpBuf, _T("%02x%02x%02x"), GetRValue(color), GetGValue(color), GetBValue(color));
}

BOOL IsToday(time_t lTime)
{
	time_t lCurTime = time(NULL);
	struct tm* lpCurTimeInfo = localtime(&lCurTime);
	TCHAR szCurrentTime[16] = {0};
	if (lpCurTimeInfo == NULL)
		return FALSE;
	_tcsftime(szCurrentTime, 16, _T("%Y-%m-%d"), lpCurTimeInfo);

	struct tm* lpTimeInfo = localtime(&lTime);
	TCHAR szTime[16] = {0};
	if (lpTimeInfo == NULL)
		return FALSE;
	_tcsftime(szTime, 16, _T("%Y-%m-%d"), lpTimeInfo);

	if(_tcscmp(szCurrentTime, szTime) == 0)
		return TRUE;

	return FALSE;

	//if (lpCurTimeInfo != NULL && lpTimeInfo != NULL
	//	&& lpCurTimeInfo->tm_year == lpTimeInfo->tm_year
	//	&& lpCurTimeInfo->tm_mon == lpTimeInfo->tm_mon
	//	&& lpCurTimeInfo->tm_mday == lpTimeInfo->tm_mday)
	//	return TRUE;
	//else
	//	return FALSE;
}

//将日期转换成UTC秒数
time_t DateToUTCSeconds(long nYear, long nMonth, long nDay)
{
	struct tm  T = {0};
	T.tm_year = nYear-1990;
	T.tm_mon = nMonth-1;
	T.tm_mday = nDay-1;

	return mktime(&T);
}

// _T("%Y-%m-%d %H:%M:%S")
void FormatTime(time_t lTime, LPCTSTR lpFmt, TCHAR* lpBuf, int nLen)
{
	if (NULL == lpFmt || NULL == lpBuf || nLen <= 0)
		return;

	memset(lpBuf, 0, nLen*sizeof(TCHAR));

	struct tm* lpTimeInfo = localtime(&lTime);
	if (lpTimeInfo != NULL)
		_tcsftime(lpBuf, nLen, lpFmt, lpTimeInfo);
}

BOOL IsToday(SYSTEMTIME* lpTime)
{
	if (NULL == lpTime)
		return FALSE;

	SYSTEMTIME stSysTime = {0};
	::GetLocalTime(&stSysTime);

	if (stSysTime.wYear == lpTime->wYear
		&& stSysTime.wMonth == lpTime->wMonth
		&& stSysTime.wDay == lpTime->wDay)
		return TRUE;
	else
		return FALSE;
}

// 获取文件最后修改时间
BOOL GetFileLastWriteTime(LPCTSTR lpszFileName, SYSTEMTIME* lpSysTime)
{
	BOOL bRet = FALSE;

	if (NULL == lpszFileName || NULL ==*lpszFileName
		|| NULL == lpSysTime)
		return FALSE;

	HANDLE hFile = ::CreateFile(lpszFileName, GENERIC_READ, FILE_SHARE_READ, 
		NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL); 
	if (hFile != INVALID_HANDLE_VALUE)
	{
		FILETIME stLastWriteTime = {0};
		bRet = ::GetFileTime(hFile, NULL, NULL, &stLastWriteTime);
		if (bRet)
		{
			FILETIME stLocalFileTime = {0};
			bRet = ::FileTimeToLocalFileTime(&stLastWriteTime, &stLocalFileTime);
			if (bRet)
				bRet = ::FileTimeToSystemTime(&stLocalFileTime, lpSysTime);
		}
		::CloseHandle(hFile);
	}
	return bRet;
}

// 读取整个文件
BOOL File_ReadAll(const TCHAR* lpszFileName, CHAR**lpData, LONG*lSize)
{
	if (NULL == lpData || NULL == lSize)
		return FALSE;

	CHAR* lpFileData = NULL;
	LONG lFileSize = 0;

	FILE* fp = _tfopen(lpszFileName, _T("rb"));
	if (fp != NULL)
	{
		fseek(fp, 0, SEEK_END);
		lFileSize = ftell(fp);
		fseek(fp, 0, SEEK_SET);

		if (lFileSize > 0)
		{
			lpFileData = new CHAR[lFileSize+16];
			if (lpFileData != NULL)
			{
				memset(lpFileData, 0, lFileSize+16);
				fread(lpFileData, lFileSize, 1, fp);
			}
		}

		fclose(fp);
	}

	if (lpFileData != NULL && lFileSize > 0)
	{
		*lpData = lpFileData;
		*lSize = lFileSize;
		return TRUE;
	}
	else
	{
		*lpData = NULL;
		*lSize = 0;
		return FALSE;
	}
}

// 检测指定字符是否是字母(A-Z，a-z)或数字(0-9)
BOOL my_isalnum(unsigned char cChar)
{
	return ((cChar | 0x20) - 'a') < 26u  || (cChar - '0') < 10u;
}

// 检测指定字符是否是字母(A-Z，a-z)或数字(0-9)
BOOL my_isalnum(wchar_t cChar)
{
	return ((cChar | 0x20) - L'a') < 26u  || (cChar - L'0') < 10u;
}

// 枚举系统字体回调函数
int CALLBACK EnumSysFontProc(const LOGFONT*lpelf, const TEXTMETRIC*lpntm, DWORD dwFontType, LPARAM lParam)
{
	if (dwFontType & TRUETYPE_FONTTYPE)
	{
		std::vector<tstring>* arrSysFont = (std::vector<tstring>*)lParam;
		if (arrSysFont != NULL)
		{
			for (int i = 0; i < (int)arrSysFont->size(); i++)
			{
				if ((*arrSysFont)[i] == lpelf->lfFaceName)
					return TRUE;
			}
			arrSysFont->push_back(lpelf->lfFaceName);
		}
	}

	return TRUE;
}

// 枚举系统字体
BOOL EnumSysFont(std::vector<tstring>* arrSysFont)
{
	if (NULL == arrSysFont)
		return FALSE;

	HDC hDC = ::GetDC(NULL);
	::EnumFontFamiliesEx(hDC, NULL, EnumSysFontProc, (LPARAM)arrSysFont, 0);
	::ReleaseDC(NULL, hDC);

	return TRUE;
}

// 闪烁窗口标题栏
BOOL FlashWindowEx(HWND hWnd, int nCount)
{
	FLASHWINFO stFlashInfo = {0};
	stFlashInfo.cbSize = sizeof(FLASHWINFO);
	stFlashInfo.hwnd = hWnd;
	stFlashInfo.dwFlags = FLASHW_TRAY | FLASHW_TIMERNOFG;
	stFlashInfo.uCount = nCount;
	stFlashInfo.dwTimeout = 0;
	return ::FlashWindowEx(&stFlashInfo);
}

// 获取系统任务栏区域
BOOL GetTrayWndRect(RECT* lpRect)
{
	if (NULL == lpRect)
		return FALSE;

	HWND hWnd = ::FindWindow(_T("Shell_TrayWnd"), NULL);
	if (hWnd != NULL)
		return ::GetWindowRect(hWnd, lpRect);
	else
		return FALSE;
}

BOOL IsDigit(const WCHAR* lpStr)
{
	for (const WCHAR* p = lpStr;*p != _T('\0'); p++)
	{
		if (!isdigit(*p))
			return FALSE;
	}
	return TRUE;
}

void Replace(std::string& strText, const CHAR* lpOldStr, const CHAR* lpNewStr)
{
	if (NULL == lpOldStr || NULL == lpNewStr)
		return;

	int nOldStrLen = strlen(lpOldStr);
	int nNewStrLen = strlen(lpNewStr);

	std::string::size_type nPos = 0;
	while ((nPos = strText.find(lpOldStr, nPos)) != std::string::npos)
	{
		strText.replace(nPos, nOldStrLen, lpNewStr);
		nPos += nNewStrLen;
	}
}

void Replace(std::wstring& strText, const WCHAR* lpOldStr, const WCHAR* lpNewStr)
{
	if (NULL == lpOldStr || NULL == lpNewStr)
		return;

	int nOldStrLen = wcslen(lpOldStr);
	int nNewStrLen = wcslen(lpNewStr);

	std::wstring::size_type nPos = 0;
	while ((nPos = strText.find(lpOldStr, nPos)) != std::wstring::npos)
	{
		strText.replace(nPos, nOldStrLen, lpNewStr);
		nPos += nNewStrLen;
	}
}

inline WCHAR ToLower(WCHAR c)
{
	return (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
}

void ToLower(WCHAR* lpText)
{
	for (WCHAR* p = lpText;*p != _T('\0'); p++)
		*p = ToLower(*p);
}

void ToLower(std::wstring& strText)
{
	for (std::wstring::iterator i = strText.begin(); i != strText.end(); ++i)
		*i = ToLower(*i);
}

inline WCHAR ToUpper(WCHAR c)
{
	return (c >= 'a' && c <= 'z') ? (c + ('A' - 'a')) : c;
}

void ToUpper(WCHAR* lpText)
{
	for (WCHAR* p = lpText;*p != _T('\0'); p++)
		*p = ToUpper(*p);
}

void ToUpper(std::wstring& strText)
{
	for (std::wstring::iterator i = strText.begin(); i != strText.end(); ++i)
		*i = ToUpper(*i);
}

// 编码Html特殊字符
void EncodeHtmlSpecialChars(std::wstring& strText)
{
	Replace(strText, _T("&"), _T("&amp;"));
	Replace(strText, _T("'"), _T("&#39;"));
	Replace(strText, _T("\""), _T("&quot;"));
	Replace(strText, _T("<"), _T("&lt;"));
	Replace(strText, _T(">"), _T("&gt;"));
	Replace(strText, _T(" "), _T("&nbsp;"));
}

// 解码Html特殊字符
void DecodeHtmlSpecialChars(std::wstring& strText)
{
	Replace(strText, _T("&#39;"), _T("'"));
	Replace(strText, _T("&quot;"), _T("\""));
	Replace(strText, _T("&lt;"), _T("<"));
	Replace(strText, _T("&gt;"), _T(">"));
	Replace(strText, _T("&nbsp;"), _T(" "));
	Replace(strText, _T("&amp;"), _T("&"));
}

tstring GetBetweenString(const TCHAR* pStr, TCHAR cStart, TCHAR cEnd)
{
	tstring strText;

	if (NULL == pStr)
		return _T("");

	const TCHAR* p1 = _tcschr(pStr, cStart);
	if (NULL == p1)
		return _T("");

	const TCHAR* p2 = _tcschr(p1+1, cEnd);
	if (NULL == p2)
		return _T("");

	int nLen = p2-(p1+1);
	if (nLen <= 0)
		return _T("");

	TCHAR* lpText = new TCHAR[nLen+1];
	if (NULL == lpText)
		return _T("");

	memset(lpText, 0, (nLen+1)*sizeof(TCHAR));
	_tcsncpy(lpText, p1+1, nLen);
	strText = lpText;
	delete []lpText;

	return strText;
}

int GetBetweenInt(const TCHAR* pStr, TCHAR cStart, TCHAR cEnd, int nDefValue/* = 0*/)
{
	tstring strText = GetBetweenString(pStr, cStart, cEnd);
	if (!strText.empty() && IsDigit(strText.c_str()))
		return _tcstol(strText.c_str(), NULL, 10);
	else
		return nDefValue;
}

tstring GetBetweenString(const WCHAR* pStr, const WCHAR* pStart, const WCHAR* pEnd)
{
	tstring strText;

	if (NULL == pStr || NULL == pStart || NULL == pEnd)
		return _T("");

	int nStartLen = _tcslen(pStart);

	const TCHAR* p1 = _tcsstr(pStr, pStart);
	if (NULL == p1)
		return _T("");

	const TCHAR* p2 = _tcsstr(p1+nStartLen, pEnd);
	if (NULL == p2)
		return _T("");

	int nLen = p2-(p1+nStartLen);
	if (nLen <= 0)
		return _T("");

	TCHAR* lpText = new TCHAR[nLen+1];
	if (NULL == lpText)
		return _T("");

	memset(lpText, 0, (nLen+1)*sizeof(TCHAR));
	_tcsncpy(lpText, p1+nStartLen, nLen);
	strText = lpText;
	delete []lpText;

	return strText;
}

int GetBetweenInt(const WCHAR* pStr, const WCHAR* pStart, 
				  const WCHAR* pEnd, int nDefValue/* = 0*/)
{
	tstring strText = GetBetweenString(pStr, pStart, pEnd);
	if (!strText.empty() && IsDigit(strText.c_str()))
		return _tcstol(strText.c_str(), NULL, 10);
	else
		return nDefValue;
}

BOOL DllRegisterServer(LPCTSTR lpszFileName, BOOL bUnregister)
{
	typedef HRESULT (WINAPI* FREG)();

	BOOL bRet = FALSE;

	HMODULE hDLL = ::LoadLibrary(lpszFileName);
	if (NULL == hDLL)
		return FALSE;

	CHAR* lpszFuncName;
	if (!bUnregister)
		lpszFuncName = "DllRegisterServer";
	else
		lpszFuncName = "DllUnregisterServer";

	FREG lpfunc = (FREG)::GetProcAddress(hDLL, lpszFuncName);
	if (lpfunc != NULL)
	{
		lpfunc();
		bRet = TRUE;
	}

	::FreeLibrary(hDLL);

	return bRet;
}

BOOL DllRegisterServer(LPCTSTR lpszFileName)
{
	return DllRegisterServer(lpszFileName, FALSE);
}

BOOL DllUnregisterServer(LPCTSTR lpszFileName)
{
	return DllRegisterServer(lpszFileName, TRUE);
}

tstring GetMimeTypeByExtension(const TCHAR* lpExtension)
{
	if (NULL == lpExtension)
		return _T("");

	if (_tcsicmp(lpExtension, _T("jpg")) == 0
		|| _tcsicmp(lpExtension, _T("jpeg")) == 0
		|| _tcsicmp(lpExtension, _T("jpe")) == 0)
		return _T("image/jpeg");
	if (_tcsicmp(lpExtension, _T("png")) == 0)
		return _T("image/png");
	else if (_tcsicmp(lpExtension, _T("gif")) == 0)
		return _T("image/gif");
	else if (_tcsicmp(lpExtension, _T("bmp")) == 0)
		return _T("image/bmp");
	else if (_tcsicmp(lpExtension, _T("tiff")) == 0
		|| _tcsicmp(lpExtension, _T("tif")) == 0)
		return _T("image/tiff");
	else if (_tcsicmp(lpExtension, _T("ico")) == 0)
		return _T("image/vnd.microsoft.icon");
	else if (_tcsicmp(lpExtension, _T("svg")) == 0
		|| _tcsicmp(lpExtension, _T("svgz")) == 0)
		return _T("image/svg+xml");
	else
		return _T("");	// application/octet-stream
}

GUID GetFileTypeGuidByExtension(const WCHAR* lpExtension)
{
	GUID guid = GUID_NULL;

	if (NULL == lpExtension)
		return guid;

	UINT numEncoders = 0, size = 0;
	Gdiplus::Status status = Gdiplus::GetImageEncodersSize(&numEncoders, &size);  
	if (status != Gdiplus::Ok)
		return guid;

	Gdiplus::ImageCodecInfo* lpEncoders = (Gdiplus::ImageCodecInfo*)(malloc(size));
	if (NULL == lpEncoders)
		return guid;

	status = Gdiplus::GetImageEncoders(numEncoders, size, lpEncoders);
	if (Gdiplus::Ok == status)
	{
		for (UINT i = 0; i < numEncoders; i++)
		{
			BOOL bFind = FALSE;
			const WCHAR* pStart = lpEncoders[i].FilenameExtension;
			const WCHAR* pEnd = wcschr(pStart, L';');
			do 
			{
				if (NULL == pEnd)
				{
					LPCWSTR lpExt = ::wcsrchr(pStart, L'.');
					if ((lpExt != NULL) && (_wcsicmp(lpExt, lpExtension) == 0))
					{
						guid = lpEncoders[i].FormatID;
						bFind = TRUE;
					}
					break;
				}

				int nLen = pEnd-pStart;
				if (nLen < MAX_PATH)
				{
					WCHAR cBuf[MAX_PATH] = {0};
					wcsncpy(cBuf, pStart, nLen);
					LPCWSTR lpExt = ::wcsrchr(cBuf, L'.');
					if ((lpExt != NULL) && (_wcsicmp(lpExt, lpExtension) == 0))
					{
						guid = lpEncoders[i].FormatID;
						bFind = TRUE;
						break;
					}
				}
				pStart = pEnd+1;
				if (L'\0' ==*pStart)
					break;
				pEnd = wcschr(pStart, L';');
			} while (1);
			if (bFind)
				break;
		}
	}

	free(lpEncoders);

	return guid;
}

CLSID GetEncoderClsidByExtension(const WCHAR* lpExtension)
{
	CLSID clsid = CLSID_NULL;

	if (NULL == lpExtension)
		return clsid;

	UINT numEncoders = 0, size = 0;
	Gdiplus::Status status = Gdiplus::GetImageEncodersSize(&numEncoders, &size);  
	if (status != Gdiplus::Ok)
		return clsid;

	Gdiplus::ImageCodecInfo* lpEncoders = (Gdiplus::ImageCodecInfo*)(malloc(size));
	if (NULL == lpEncoders)
		return clsid;

	status = Gdiplus::GetImageEncoders(numEncoders, size, lpEncoders);
	if (Gdiplus::Ok == status)
	{
		for (UINT i = 0; i < numEncoders; i++)
		{
			BOOL bFind = FALSE;
			const WCHAR* pStart = lpEncoders[i].FilenameExtension;
			const WCHAR* pEnd = wcschr(pStart, L';');
			do 
			{
				if (NULL == pEnd)
				{
					LPCWSTR lpExt = ::wcsrchr(pStart, L'.');
					if ((lpExt != NULL) && (_wcsicmp(lpExt, lpExtension) == 0))
					{
						clsid = lpEncoders[i].Clsid;
						bFind = TRUE;
					}
					break;
				}

				int nLen = pEnd-pStart;
				if (nLen < MAX_PATH)
				{
					WCHAR cBuf[MAX_PATH] = {0};
					wcsncpy(cBuf, pStart, nLen);
					LPCWSTR lpExt = ::wcsrchr(cBuf, L'.');
					if ((lpExt != NULL) && (_wcsicmp(lpExt, lpExtension) == 0))
					{
						clsid = lpEncoders[i].Clsid;
						bFind = TRUE;
						break;
					}
				}
				pStart = pEnd+1;
				if (L'\0' ==*pStart)
					break;
				pEnd = wcschr(pStart, L';');
			} while (1);
			if (bFind)
				break;
		}
	}

	free(lpEncoders);

	return clsid;
}

// ImageFormatBMP, ImageFormatJPEG, ImageFormatPNG, ImageFormatGIF, ImageFormatTIFF
CLSID GetEncoderClsidByFileType(REFGUID guidFileType)
{
	CLSID clsid = CLSID_NULL;

	UINT numEncoders = 0, size = 0;
	Gdiplus::Status status = Gdiplus::GetImageEncodersSize(&numEncoders, &size);  
	if (status != Gdiplus::Ok)
		return clsid;

	Gdiplus::ImageCodecInfo* lpEncoders = (Gdiplus::ImageCodecInfo*)(malloc(size));
	if (NULL == lpEncoders)
		return clsid;

	status = Gdiplus::GetImageEncoders(numEncoders, size, lpEncoders);
	if (Gdiplus::Ok == status)
	{
		for (UINT i = 0; i < numEncoders; i++)
		{
			if (lpEncoders[i].FormatID == guidFileType)
				clsid = lpEncoders[i].Clsid;
		}
	}

	free(lpEncoders);

	return clsid;
}

// image/bmp, image/jpeg, image/gif, image/tiff, image/png
CLSID GetEncoderClsidByMimeType(const WCHAR* lpMimeType)
{
	CLSID clsid = CLSID_NULL;

	if (NULL == lpMimeType)
		return clsid;

	UINT numEncoders = 0, size = 0;
	Gdiplus::Status status = Gdiplus::GetImageEncodersSize(&numEncoders, &size);  
	if (status != Gdiplus::Ok)
		return clsid;

	Gdiplus::ImageCodecInfo* lpEncoders = (Gdiplus::ImageCodecInfo*)(malloc(size));
	if (NULL == lpEncoders)
		return clsid;

	status = Gdiplus::GetImageEncoders(numEncoders, size, lpEncoders);
	if (Gdiplus::Ok == status)
	{
		for (UINT i = 0; i < numEncoders; i++)
		{
			if (_wcsicmp(lpEncoders[i].MimeType, lpMimeType) == 0)
				clsid = lpEncoders[i].Clsid;
		}
	}

	free(lpEncoders);

	return clsid;
}

/* RFC1123: Sun, 06 Nov 1994 08:49:37 GMT*/
#define RFC1123_FORMAT _T("%3s, %02d %3s %4d %02d:%02d:%02d GMT")
/* RFC850:  Sunday, 06-Nov-94 08:49:37 GMT*/
#define RFC1036_FORMAT _T("%10s %2d-%3s-%2d %2d:%2d:%2d GMT")
/* asctime: Wed Jun 30 21:49:08 1993*/
#define ASCTIME_FORMAT _T("%3s %3s %2d %2d:%2d:%2d %4d")

static const TCHAR*const rfc1123_weekdays[7] = { 
    _T("Sun"), _T("Mon"), _T("Tue"), _T("Wed"), _T("Thu"), _T("Fri"), _T("Sat") 
};
static const TCHAR*const short_months[12] = { 
    _T("Jan"), _T("Feb"), _T("Mar"), _T("Apr"), _T("May"), _T("Jun"),
    _T("Jul"), _T("Aug"), _T("Sep"), _T("Oct"), _T("Nov"), _T("Dec")
};

time_t gmt_to_local_win32(void)
{
    TIME_ZONE_INFORMATION tzinfo;
    DWORD dwStandardDaylight;
    long bias;

    dwStandardDaylight = GetTimeZoneInformation(&tzinfo);
    bias = tzinfo.Bias;

    if (dwStandardDaylight == TIME_ZONE_ID_STANDARD)
        bias += tzinfo.StandardBias;
    
    if (dwStandardDaylight == TIME_ZONE_ID_DAYLIGHT)
        bias += tzinfo.DaylightBias;
    
    return (- bias* 60);
}

/* Takes an RFC1123-formatted date string and returns the time_t.
* Returns (time_t)-1 if the parse fails.*/
time_t rfc1123_parse(const TCHAR*date) 
{
    struct tm gmt = {0};
    TCHAR wkday[4], mon[4];
    int n;
/*  it goes: Sun, 06 Nov 1994 08:49:37 GMT*/
    n = _stscanf(date, RFC1123_FORMAT,
        wkday, &gmt.tm_mday, mon, &gmt.tm_year, &gmt.tm_hour,
        &gmt.tm_min, &gmt.tm_sec);
    /* Is it portable to check n==7 here?*/
    gmt.tm_year -= 1900;
    for (n=0; n<12; n++)
    if (_tcscmp(mon, short_months[n]) == 0)
        break;
    /* tm_mon comes out as 12 if the month is corrupt, which is desired,
    * since the mktime will then fail*/
    gmt.tm_mon = n;
    gmt.tm_isdst = -1;
    return mktime(&gmt) + gmt_to_local_win32();
}

/* Takes a string containing a RFC1036-style date and returns the time_t*/
time_t rfc1036_parse(const TCHAR*date) 
{
    struct tm gmt = {0};
    int n;
    TCHAR wkday[11], mon[4];
    /* RFC850/1036 style dates: Sunday, 06-Nov-94 08:49:37 GMT*/
    n = _stscanf(date, RFC1036_FORMAT,
        wkday, &gmt.tm_mday, mon, &gmt.tm_year,
        &gmt.tm_hour, &gmt.tm_min, &gmt.tm_sec);
    if (n != 7) {
    return (time_t)-1;
    }

    /* portable to check n here?*/
    for (n=0; n<12; n++)
    if (_tcscmp(mon, short_months[n]) == 0)
        break;
    /* tm_mon comes out as 12 if the month is corrupt, which is desired,
    * since the mktime will then fail*/

    /* Defeat Y2K bug.*/
    if (gmt.tm_year < 50)
    gmt.tm_year += 100;

    gmt.tm_mon = n;
    gmt.tm_isdst = -1;
    return mktime(&gmt) + gmt_to_local_win32();
}


/* (as)ctime dates are like:
*    Wed Jun 30 21:49:08 1993
*/
time_t asctime_parse(const TCHAR*date) 
{
    struct tm gmt = {0};
    int n;
    TCHAR wkday[4], mon[4];
    n = _stscanf(date, ASCTIME_FORMAT,
        wkday, mon, &gmt.tm_mday, 
        &gmt.tm_hour, &gmt.tm_min, &gmt.tm_sec,
        &gmt.tm_year);
    /* portable to check n here?*/
    for (n=0; n<12; n++)
    if (_tcscmp(mon, short_months[n]) == 0)
        break;
    /* tm_mon comes out as 12 if the month is corrupt, which is desired,
    * since the mktime will then fail*/
    gmt.tm_mon = n;
    gmt.tm_isdst = -1;
    return mktime(&gmt) + gmt_to_local_win32();
}

/* HTTP-date parser*/
time_t httpdate_parse(const TCHAR*date)
{
    time_t tmp;
    tmp = rfc1123_parse(date);
    if (tmp == -1) {
        tmp = rfc1036_parse(date);
    if (tmp == -1)
        tmp = asctime_parse(date);
    }
    return tmp;
}

//只影响同一目录下的程序文件，不影响不同目录下的程序文件
//所以这里为了保证唯一性使用程序全饰路径来作为互斥体的名字
BOOL CheckOnlyOneInstance()
{
	//TCHAR szModulePath[MAX_PATH] = {0};
	//::GetModuleFileName(NULL, szModulePath, MAX_PATH);
	////将路径中反斜杠换成连字符-
	//for(long i=_tcslen(szModulePath)-1; i>=0; --i)
	//{
	//	if(szModulePath[i] == _T('\\') || szModulePath[i] == _T(':') || szModulePath[i] == _T('.'))
	//		szModulePath[i] = _T('I');
	//}
	
	HANDLE hMutex = ::CreateMutex(NULL, TRUE, _T("HootinaFlamingo2017"));
	if(::GetLastError() == ERROR_ALREADY_EXISTS)
		return FALSE;

	return TRUE;
}


long GetFileCountOfSpecifiDirectory(PCSTR pszDirectoryName, PCTSTR pszFileExtension)
{
	if(pszDirectoryName==NULL || pszFileExtension==NULL)
		return 0;

	/*WIN32_FIND_DATA findData = {0};
	HANDLE hFind = ::FindFirstFile(pszDirectoryName,&findData);
　　if(INVALID_HANDLE_VALUE == hFind)　　
		return 0;

	long nCount = 0;
  　while(TRUE)
	{
		if(findData.dwFileAttributes & FILE_ATTRIBUTE_NORMAL)
		{
			if(_tcsstr(findData.cFileName
			  　　　
			if(!FindNextFile(hFind, &findData))　　break;
	　　}
   }
	
   ::FindClose(hFind);*/

	return 0;

   //return nCount;
}

//abc|efg|hij|klm|t
void SplitString(const CString& strSource, PCTSTR pszDelimiter, std::vector<CString>& aryStringArray)
{
	aryStringArray.clear();

	CString strSrc(strSource);
	CString strTemp;
	long nIndex = -1;
	do 
	{
		nIndex = strSrc.Find(pszDelimiter);
		if(nIndex<0 && !strSrc.IsEmpty())
		{
			aryStringArray.push_back(strSrc);	//将最后剩余部分加进去
			break;
		}
		else if(nIndex<0 && strSrc.IsEmpty())
			break;
			

		strTemp = strSrc.Left(nIndex);
		if(!strTemp.IsEmpty())
			aryStringArray.push_back(strTemp);

		strSrc = strSrc.Mid(nIndex+1);
		if(strSrc.IsEmpty())
			break;

	} while (TRUE);
}

//获取本机网卡地址
DWORD GetNetcard(BYTE Address[8][6])
{
	char __local_ipaddress__[8][16]={0};
	BYTE null_card[6]={0},j=0;
	ULONG ulAdapterInfoSize(0);
	GetAdaptersInfo(NULL,&ulAdapterInfoSize);
	if(ulAdapterInfoSize)
	{
		IP_ADAPTER_INFO*pAdapterInfo=(IP_ADAPTER_INFO*)new char[ulAdapterInfoSize];
		IP_ADAPTER_INFO*pAdapterInfoBkp=pAdapterInfo;
		IP_ADDR_STRING* pIPAddr = NULL;
		if(GetAdaptersInfo(pAdapterInfo,&ulAdapterInfoSize)==ERROR_SUCCESS)
		{
			do	//遍历所有适配器
			{
				if(pAdapterInfo->Type==MIB_IF_TYPE_ETHERNET||
					pAdapterInfo->Type==IF_TYPE_IEEE80211)//判断是否为以太网接口
				{
					if(pAdapterInfo->AddressLength==6&&
						memcmp(pAdapterInfo->Address,null_card,6))
					{
						memcpy(Address[j],pAdapterInfo->Address,6);
						pIPAddr = &(pAdapterInfo->IpAddressList);
						strcpy_s(__local_ipaddress__[j], pIPAddr->IpAddress.String);
						if(++j>7) break;
					}
				}
				pAdapterInfo = pAdapterInfo->Next;
			}while(pAdapterInfo);
		}
		delete pAdapterInfoBkp;
	}

	return(j);
}

HICON ExtractIcon(LPCTSTR lpszFileName, long nSize)
{
	if (NULL == lpszFileName || NULL ==*lpszFileName)
		return NULL;

	int cx = nSize, cy = nSize;
	HBITMAP hBmp = NULL;

	Gdiplus::Bitmap imgHead(lpszFileName);
	if (imgHead.GetLastStatus() != Gdiplus::Ok)
		return NULL;

	if (imgHead.GetWidth() != cx || imgHead.GetHeight() != cy)
	{
		Gdiplus::Bitmap* pThumbnail = (Gdiplus::Bitmap*)imgHead.GetThumbnailImage(cx, cy, NULL, NULL);
		if (pThumbnail != NULL)
		{
			pThumbnail->GetHBITMAP(Gdiplus::Color(255,255,255), &hBmp);
			delete pThumbnail;
		}
	}
	else
	{
		imgHead.GetHBITMAP(Gdiplus::Color(255,255,255), &hBmp);
	}

	if (NULL == hBmp)
		return NULL;

	HICON hIcon = NULL;

	CImageList imgList;
	BOOL bRet = imgList.Create(cx, cy, TRUE | ILC_COLOR32, 1, 1);
	if (bRet)
	{
		imgList.SetBkColor(RGB(255,255,255));
		imgList.Add(hBmp);
		hIcon = imgList.ExtractIcon(0);
	}
	::DeleteObject(hBmp);
	imgList.Destroy();

	return hIcon;
}

BOOL GetImageWidthAndHeight(LPCTSTR lpszFileName, long& nWidth, long& nHeight)
{
	if (NULL == lpszFileName || NULL ==*lpszFileName)
		return FALSE;

	Gdiplus::Bitmap imgHead(lpszFileName);
	if (imgHead.GetLastStatus() != Gdiplus::Ok)
		return FALSE;

	nWidth = imgHead.GetWidth();
	nHeight = imgHead.GetHeight();

	return TRUE;
}

BOOL IUIsNetworkAlive()  
{  

	DWORD   dwFlags;			//上网方式   
	BOOL    bAlive = TRUE;		//是否在线    

	bAlive = ::IsNetworkAlive(&dwFlags);     
	
	return bAlive;
}

BOOL IUGetConnectedState()
{
	DWORD  dwFlags;				//上网方式   
	BOOL   bAlive = TRUE;		//是否在线    

	bAlive = ::InternetGetConnectedState(&dwFlags, 0);
	if(bAlive && (dwFlags&INTERNET_CONNECTION_MODEM_BUSY)==0)
		return TRUE;
	

	return FALSE;
}

//返回值是任务栏的高度和方向, -1表示出错
long GetTaskbarDirectionAndRect(CRect& rtTaskbar)
{
	long cxScreen = ::GetSystemMetrics(SM_CXSCREEN);
	long cyScreen = ::GetSystemMetrics(SM_CYSCREEN);
	
	long nDirection;
	CRect rtWorkarea;
	if(!::SystemParametersInfo(SPI_GETWORKAREA, 0, &rtWorkarea, 0))
		nDirection = -1;
	
	if(rtWorkarea.left > 0)
		nDirection = 0;			//左边
	else if(rtWorkarea.Width() < cxScreen)
		nDirection = 1;			//右边
	else if(rtWorkarea.top > 0)
		nDirection = 2;			//上边
	else if(rtWorkarea.Height() < cyScreen) //下边
		nDirection = 3;

	if(nDirection == 3)
	{
		rtTaskbar.left = 0;
		rtTaskbar.right = cyScreen;
		rtTaskbar.top = rtWorkarea.Height();
		rtTaskbar.bottom = cyScreen;
	}
	else
	{
		rtTaskbar.left = 0;
		rtTaskbar.right = cxScreen;
		rtTaskbar.top = cyScreen;
		rtTaskbar.bottom = cyScreen;
	}

	return nDirection;
}

//将图片转换成指定大小和指定格式
int  TransformImage(PCTSTR lpSrcFile, PCTSTR lpDstExt, long nDestWidth, long nDestHeight, CString& strDstFile)           
{   
	if(!Hootina::CPath::IsFileExist(lpSrcFile))
		return 0;

	Image    *psrcImg=NULL;   
	psrcImg = Image::FromFile(lpSrcFile);
	if (!psrcImg || psrcImg->GetLastStatus() != Ok)
	{
		return 0;
	}
	int   srcWidth=psrcImg->GetWidth();   
	int   srcHeight=psrcImg->GetHeight();   

	//   Construct   a   Graphics   object   based   on   the   image.   
	//Graphics   imgGraphics(psrcImg);   
	int nSaveWidth = nDestWidth;
	int nSaveHeight = nDestWidth;
	Bitmap  *pBitmap = ::new Bitmap(nSaveWidth,nSaveHeight);   
	Graphics   bmpGraphics(pBitmap);   
	//bmpGraphics.DrawImage(psrcImg,0,0,srcWidth,srcHeight);   
	bmpGraphics.DrawImage(psrcImg, RectF(0,0,(Gdiplus::REAL)nSaveWidth,(Gdiplus::REAL)nSaveHeight), (Gdiplus::REAL)0, (Gdiplus::REAL)0, (Gdiplus::REAL)srcWidth, (Gdiplus::REAL)srcHeight, UnitPixel);
	delete psrcImg;
	bmpGraphics.ReleaseHDC(NULL);

	//   Save   the   altered   image.
	LPWSTR   lpExt=PathFindExtensionW(lpSrcFile); 
	LPWSTR   lpExtDst = NULL;

	lpExt++;

	LPWSTR   lpEncoder;  
	switch (*lpExt)
	{   
	case   L'J':   
	case   L'j':   
		lpEncoder=L"image/jpeg";   
		lpExtDst = L".jpg";  
		break;   
	case   L'P':   
	case   L'p':   
		lpEncoder=L"image/png";   
		lpExtDst = L".png"; 
		break;   
	case   L'B':   
	case   L'b':   
		lpEncoder=L"image/bmp";  
		lpExtDst = L".bmp"; 
		break;   
	case   L'G':   
	case   L'g':   
		lpEncoder=L"image/gif";   
		lpExtDst = L".gif"; 
		break;   
	case   L't':   
	case   L'T':   
		lpEncoder=L"image/tiff";   
		lpExtDst = L".tiff"; 
		break;   
	default:   
		lpEncoder=L"image/jpeg";   
		lpExtDst = L".jpg"; 
	}   

	CString strPicPath(lpSrcFile);
	lpExt--;
	CString strExt(lpExt);
	int nLen = strPicPath.Find(strExt);
	if(nLen>0)
		strPicPath=strPicPath.Left(nLen);
	strPicPath += lpExtDst;

	CLSID   imgClsid;   
	GetImageCLSID(lpEncoder,&imgClsid);
	Status statusSave = pBitmap->Save(strPicPath,&imgClsid,NULL); 

	//::DeleteFile(lpSrcFile);

	//lpSrcFile = strPicPath.GetBuffer();
	//strPicPath.ReleaseBuffer();
	
	strDstFile = strPicPath;
	::delete pBitmap;

	return (statusSave == Ok )?1:0;
}

BOOL GetImageCLSID(PCTSTR format, CLSID* pCLSID)
{
	UINT num = 0;
	UINT size = 0;

	ImageCodecInfo* pImageCodecInfo = NULL;
	GetImageEncodersSize(&num, &size);
	if(size == 0){
		return FALSE;
	}
	pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
	if(pImageCodecInfo == NULL)
		return FALSE;
	GetImageEncoders(num, size, pImageCodecInfo); 

	// Find for the support of format for image in the windows
	for(UINT i = 0; i < num; ++i)
	{ 
		//MimeType: Depiction for the program image  
		if( wcscmp(pImageCodecInfo[i].MimeType, format) == 0)
		{  
			*pCLSID = pImageCodecInfo[i].Clsid; 
			free(pImageCodecInfo); 
			return TRUE;  
		}  
	}  
	free(pImageCodecInfo);  
	return FALSE; 
} 


BOOL CenterWindow2(HWND hTargetWnd, HWND hWndCenter/* = NULL*/)
{
	// determine owner window to center against
	DWORD dwStyle = ::GetWindowLong(hTargetWnd, GWL_STYLE);
	if(hWndCenter == NULL)
	{
		if(dwStyle & WS_CHILD)
			hWndCenter = ::GetParent(hTargetWnd);
		else
			hWndCenter = ::GetWindow(hTargetWnd, GW_OWNER);
	}

	// get coordinates of the window relative to its parent
	RECT rcDlg;
	::GetWindowRect(hTargetWnd, &rcDlg);
	RECT rcArea;
	RECT rcCenter;
	HWND hWndParent;
	if(!(dwStyle & WS_CHILD))
	{
		// don't center against invisible or minimized windows
		if(hWndCenter != NULL)
		{
			DWORD dwStyleCenter = ::GetWindowLong(hWndCenter, GWL_STYLE);
			if(!(dwStyleCenter & WS_VISIBLE) || (dwStyleCenter & WS_MINIMIZE))
				hWndCenter = NULL;
		}

		// center within screen coordinates
#if WINVER < 0x0500
		::SystemParametersInfo(SPI_GETWORKAREA, NULL, &rcArea, NULL);
#else
		HMONITOR hMonitor = NULL;
		if(hWndCenter != NULL)
		{
			hMonitor = ::MonitorFromWindow(hWndCenter, MONITOR_DEFAULTTONEAREST);
		}
		else
		{
			hMonitor = ::MonitorFromWindow(hTargetWnd, MONITOR_DEFAULTTONEAREST);
		}
		ATLENSURE_RETURN_VAL(hMonitor != NULL, FALSE);
		
		MONITORINFO minfo;
		minfo.cbSize = sizeof(MONITORINFO);
		BOOL bResult = ::GetMonitorInfo(hMonitor, &minfo);
		ATLENSURE_RETURN_VAL(bResult, FALSE);
		
		rcArea = minfo.rcWork;
#endif
		if(hWndCenter == NULL)
			rcCenter = rcArea;
		else
			::GetWindowRect(hWndCenter, &rcCenter);
	}
	else
	{
		// center within parent client coordinates
		hWndParent = ::GetParent(hTargetWnd);

		::GetClientRect(hWndParent, &rcArea);
		::GetClientRect(hWndCenter, &rcCenter);
		::MapWindowPoints(hWndCenter, hWndParent, (POINT*)&rcCenter, 2);
	}

	int DlgWidth = rcDlg.right - rcDlg.left;
	int DlgHeight = rcDlg.bottom - rcDlg.top;

	// find dialog's upper left based on rcCenter
	int xLeft = (rcCenter.left + rcCenter.right) / 2 - DlgWidth / 2;
	int yTop = (rcCenter.top + rcCenter.bottom) / 2 - DlgHeight / 2;

	// if the dialog is outside the screen, move it inside
	if(xLeft + DlgWidth > rcArea.right)
		xLeft = rcArea.right - DlgWidth;
	if(xLeft < rcArea.left)
		xLeft = rcArea.left;

	if(yTop + DlgHeight > rcArea.bottom)
		yTop = rcArea.bottom - DlgHeight;
	if(yTop < rcArea.top)
		yTop = rcArea.top;

	// map screen coordinates to child coordinates
	return ::SetWindowPos(hTargetWnd, NULL, xLeft, yTop, -1, -1,
		SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);
}

void ScreenToClient(HWND hwnd, RECT& rt)
{
	POINT pt1 = {rt.left, rt.top};
	POINT pt2 = {rt.right, rt.bottom};

	::ScreenToClient(hwnd, &pt1);
	::ScreenToClient(hwnd, &pt2);

	::SetRect(&rt, pt1.x, pt1.y, pt2.x, pt2.y);
}

BOOL IsMobileNumber(PCTSTR pszNumber)
{
	if(_tcslen(pszNumber)!=11 || pszNumber[0]!=L'1' || (pszNumber[1]!=L'3'&&pszNumber[1]!=L'5'&&pszNumber[1]!=L'8'))
		return FALSE;

	return TRUE;
}
