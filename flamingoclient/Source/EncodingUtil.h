/** 
* Windows平台用编码格式相互转换类, EncodingUtil.h
* zhangyl 2017.03.29
**/
#pragma once

#include <string>

wchar_t* AnsiToUnicode(const char* lpszStr);
char* UnicodeToAnsi(const wchar_t* lpszStr);
char* AnsiToUtf8(const char* lpszStr);
char* Utf8ToAnsi(const char* lpszStr);
char* UnicodeToUtf8(const wchar_t* lpszStr);
wchar_t* Utf8ToUnicode(const char* lpszStr);

bool AnsiToUnicode(const char* lpszAnsi, wchar_t* lpszUnicode, int nLen);
bool UnicodeToAnsi(const wchar_t* lpszUnicode, char* lpszAnsi, int nLen);
bool AnsiToUtf8(const char* lpszAnsi, char* lpszUtf8, int nLen);
bool Utf8ToAnsi(const char* lpszUtf8, char* lpszAnsi, int nLen);
bool UnicodeToUtf8(const wchar_t* lpszUnicode, char* lpszUtf8, int nLen);
bool Utf8ToUnicode(const char* lpszUtf8, wchar_t* lpszUnicode, int nLen);

std::wstring AnsiToUnicode(const std::string& strAnsi);
std::string UnicodeToAnsi(const std::wstring& strUnicode);
std::string AnsiToUtf8(const std::string& strAnsi);
std::string Utf8ToAnsi(const std::string& strUtf8);
std::string UnicodeToUtf8(const std::wstring& strUnicode);
std::wstring Utf8ToUnicode(const std::string& strUtf8);