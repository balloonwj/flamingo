#pragma once

#include <vector>
#include <string>
#include <time.h>
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

#if defined(UNICODE) || defined(_UNICODE)
	typedef std::wstring tstring;
#else
	typedef std::string tstring;
#endif

#define DEL(p) \
	if(p != NULL) \
	{			  \
		delete p; \
		p = NULL; \
	}

#define DEL_ARR(p) \
	if(p != NULL) \
	{			  \
		delete[] p; \
		p = NULL; \
	}




BOOL ToHexStr(const CHAR* lpStr, int nSrcLen, CHAR* lpHex, int nDestLen);
BOOL StrToHex(const CHAR* lpStr, CHAR* lpHex, int nLen);

u_short Swap16(u_short nValue);		// 16位整型数据网络字节序与正常字节序转换
u_long Swap32(u_long nValue);		// 32位整型数据网络字节序与正常字节序转换

COLORREF HexStrToRGB(LPCTSTR lpszStr);
void RGBToHexStr(COLORREF color, TCHAR* lpBuf, int nLen);

time_t DateToUTCSeconds(long nYear, long nMonth, long nDay);				//将日期转换成UTC秒数
void FormatTime(time_t lTime, LPCTSTR lpFmt, TCHAR* lpBuf, int nLen);
BOOL IsToday(time_t lTime);

BOOL IsToday(SYSTEMTIME* lpTime);
BOOL GetFileLastWriteTime(LPCTSTR lpszFileName, SYSTEMTIME* lpSysTime);	// 获取文件最后修改时间

BOOL File_ReadAll(const TCHAR* lpszFileName, CHAR**lpData, LONG*lSize);	// 读取整个文件

BOOL my_isalnum(unsigned char cChar);		// 检测指定字符是否是字母(A-Z，a-z)或数字(0-9)
BOOL my_isalnum(wchar_t cChar);				// 检测指定字符是否是字母(A-Z，a-z)或数字(0-9)

BOOL EnumSysFont(std::vector<tstring>* arrSysFont);	// 枚举系统字体
BOOL FlashWindowEx(HWND hWnd, int nCount);				// 闪烁窗口标题栏
BOOL GetTrayWndRect(RECT* lpRect);						// 获取系统任务栏区域

BOOL IsDigit(const WCHAR* lpStr);
void Replace(std::string& strText, const CHAR* lpOldStr, const CHAR* lpNewStr);
void Replace(std::wstring& strText, const WCHAR* lpOldStr, const WCHAR* lpNewStr);
WCHAR ToLower(WCHAR c);
void ToLower(WCHAR* lpText);
void ToLower(std::wstring& strText);
WCHAR ToUpper(WCHAR c);
void ToUpper(WCHAR* lpText);
void ToUpper(std::wstring& strText);
void EncodeHtmlSpecialChars(std::wstring& strText);		// 编码Html特殊字符
void DecodeHtmlSpecialChars(std::wstring& strText);		// 解码Html特殊字符

tstring GetBetweenString(const TCHAR* pStr, TCHAR cStart, TCHAR cEnd);
int GetBetweenInt(const TCHAR* pStr, TCHAR cStart, TCHAR cEnd, int nDefValue = 0);

tstring GetBetweenString(const WCHAR* pStr, const WCHAR* pStart, const WCHAR* pEnd);
int GetBetweenInt(const WCHAR* pStr, const WCHAR* pStart, 
				  const WCHAR* pEnd, int nDefValue = 0);

BOOL DllRegisterServer(LPCTSTR lpszFileName);
BOOL DllUnregisterServer(LPCTSTR lpszFileName);

tstring GetMimeTypeByExtension(const TCHAR* lpExtension);
GUID GetFileTypeGuidByExtension(const WCHAR* lpExtension);
CLSID GetEncoderClsidByExtension(const WCHAR* lpExtension);
CLSID GetEncoderClsidByFileType(REFGUID guidFileType);
CLSID GetEncoderClsidByMimeType(const WCHAR* lpMimeType);

time_t httpdate_parse(const TCHAR*date);

BOOL CheckOnlyOneInstance();			//检测程序运行实例

long GetFileCountOfSpecifiDirectory(PCSTR pszDirectoryName, PCTSTR pszFileExtension);	//获取指定文件中某种类型文件个数

void SplitString(const CString& strSource, PCTSTR pszDelimiter, std::vector<CString>& aryStringArray);

DWORD GetNetcard(BYTE Address[8][6]);	//获取本机网卡

HICON ExtractIcon(LPCTSTR lpszFileName, long nSize);

BOOL GetImageWidthAndHeight(LPCTSTR lpszFileName, long& nWidth, long& nHeight);

//检测当前网络状态的函数
BOOL IUIsNetworkAlive();
BOOL IUGetConnectedState();


//返回值是任务栏的高度和方向
long GetTaskbarDirectionAndRect(CRect& rtTaskbar);

//将图片转换成指定格式
int TransformImage(PCTSTR lpSrcFile, PCTSTR lpDstExt, long nDestWidth, long nDestHeight, CString& strDstFile);
BOOL GetImageCLSID(PCTSTR format, CLSID* pCLSID);

BOOL CenterWindow2(HWND hTargetWnd, HWND hWndCenter = NULL);
//将一个屏幕位置转换成窗口位置
void ScreenToClient(HWND hwnd, RECT& rt);

BOOL IsMobileNumber(PCTSTR pszNumber);
