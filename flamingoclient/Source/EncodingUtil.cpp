/**
 * Windows平台用编码格式相互转换类, EncodingUtil.cpp
 * zhangyl 2017.03.29
 **/
#include "stdafx.h"
#include <locale.h>
//#include <iconv.h>          //linux only
#include "EncodingUtil.h"



wchar_t* EncodeUtil::AnsiToUnicode(const char* lpszStr)
{
    wchar_t* lpUnicode;
    int nLen;

    if (NULL == lpszStr)
        return NULL;

    nLen = ::MultiByteToWideChar(CP_ACP, 0, lpszStr, -1, NULL, 0);
    if (0 == nLen)
        return NULL;

    lpUnicode = new wchar_t[nLen + 1];
    if (NULL == lpUnicode)
        return NULL;

    memset(lpUnicode, 0, sizeof(wchar_t)* (nLen + 1));
    nLen = ::MultiByteToWideChar(CP_ACP, 0, lpszStr, -1, lpUnicode, nLen);
    if (0 == nLen)
    {
        delete[]lpUnicode;
        return NULL;
    }

    return lpUnicode;
}

char* EncodeUtil::UnicodeToAnsi(const wchar_t* lpszStr)
{
    char* lpAnsi;
    int nLen;

    if (NULL == lpszStr)
        return NULL;

    nLen = ::WideCharToMultiByte(CP_ACP, 0, lpszStr, -1, NULL, 0, NULL, NULL);
    if (0 == nLen)
        return NULL;

    lpAnsi = new char[nLen + 1];
    if (NULL == lpAnsi)
        return NULL;

    memset(lpAnsi, 0, nLen + 1);
    nLen = ::WideCharToMultiByte(CP_ACP, 0, lpszStr, -1, lpAnsi, nLen, NULL, NULL);
    if (0 == nLen)
    {
        delete[]lpAnsi;
        return NULL;
    }

    return lpAnsi;
}

char* EncodeUtil::AnsiToUtf8(const char* lpszStr)
{
    wchar_t* lpUnicode;
    char* lpUtf8;
    int nLen;

    if (NULL == lpszStr)
        return NULL;

    nLen = ::MultiByteToWideChar(CP_ACP, 0, lpszStr, -1, NULL, NULL);
    if (0 == nLen)
        return NULL;

    lpUnicode = new wchar_t[nLen + 1];
    if (NULL == lpUnicode)
        return NULL;

    memset(lpUnicode, 0, sizeof(wchar_t)* (nLen + 1));
    nLen = ::MultiByteToWideChar(CP_ACP, 0, lpszStr, -1, lpUnicode, nLen);
    if (0 == nLen)
    {
        delete[]lpUnicode;
        return NULL;
    }

    nLen = ::WideCharToMultiByte(CP_UTF8, 0, lpUnicode, -1, NULL, 0, NULL, NULL);
    if (0 == nLen)
    {
        delete[]lpUnicode;
        return NULL;
    }

    lpUtf8 = new char[nLen + 1];
    if (NULL == lpUtf8)
    {
        delete[]lpUnicode;
        return NULL;
    }

    memset(lpUtf8, 0, nLen + 1);
    nLen = ::WideCharToMultiByte(CP_UTF8, 0, lpUnicode, -1, lpUtf8, nLen, NULL, NULL);
    if (0 == nLen)
    {
        delete[]lpUnicode;
        delete[]lpUtf8;
        return NULL;
    }

    delete[]lpUnicode;

    return lpUtf8;
}

char* EncodeUtil::Utf8ToAnsi(const char* lpszStr)
{
    wchar_t* lpUnicode;
    char* lpAnsi;
    int nLen;

    if (NULL == lpszStr)
        return NULL;

    nLen = ::MultiByteToWideChar(CP_UTF8, 0, lpszStr, -1, NULL, NULL);
    if (0 == nLen)
        return NULL;

    lpUnicode = new wchar_t[nLen + 1];
    if (NULL == lpUnicode)
        return NULL;

    memset(lpUnicode, 0, sizeof(wchar_t)* (nLen + 1));
    nLen = ::MultiByteToWideChar(CP_UTF8, 0, lpszStr, -1, lpUnicode, nLen);
    if (0 == nLen)
    {
        delete[]lpUnicode;
        return NULL;
    }

    nLen = ::WideCharToMultiByte(CP_ACP, 0, lpUnicode, -1, NULL, 0, NULL, NULL);
    if (0 == nLen)
    {
        delete[]lpUnicode;
        return NULL;
    }

    lpAnsi = new char[nLen + 1];
    if (NULL == lpAnsi)
    {
        delete[]lpUnicode;
        return NULL;
    }

    memset(lpAnsi, 0, nLen + 1);
    nLen = ::WideCharToMultiByte(CP_ACP, 0, lpUnicode, -1, lpAnsi, nLen, NULL, NULL);
    if (0 == nLen)
    {
        delete[]lpUnicode;
        delete[]lpAnsi;
        return NULL;
    }

    delete[]lpUnicode;

    return lpAnsi;
}

char* EncodeUtil::UnicodeToUtf8(const wchar_t* lpszStr)
{
    char* lpUtf8;
    int nLen;

    if (NULL == lpszStr)
        return NULL;

    nLen = ::WideCharToMultiByte(CP_UTF8, 0, lpszStr, -1, NULL, 0, NULL, NULL);
    if (0 == nLen)
        return NULL;

    lpUtf8 = new char[nLen + 1];
    if (NULL == lpUtf8)
        return NULL;

    memset(lpUtf8, 0, nLen + 1);
    nLen = ::WideCharToMultiByte(CP_UTF8, 0, lpszStr, -1, lpUtf8, nLen, NULL, NULL);
    if (0 == nLen)
    {
        delete[]lpUtf8;
        return NULL;
    }

    return lpUtf8;
}

wchar_t* EncodeUtil::Utf8ToUnicode(const char* lpszStr)
{
    wchar_t* lpUnicode;
    int nLen;

    if (NULL == lpszStr)
        return NULL;

    nLen = ::MultiByteToWideChar(CP_UTF8, 0, lpszStr, -1, NULL, 0);
    if (0 == nLen)
        return NULL;

    lpUnicode = new wchar_t[nLen + 1];
    if (NULL == lpUnicode)
        return NULL;

    memset(lpUnicode, 0, sizeof(wchar_t)* (nLen + 1));
    nLen = ::MultiByteToWideChar(CP_UTF8, 0, lpszStr, -1, lpUnicode, nLen);
    if (0 == nLen)
    {
        delete[]lpUnicode;
        return NULL;
    }

    return lpUnicode;
}

bool EncodeUtil::AnsiToUnicode(const char* lpszAnsi, wchar_t* lpszUnicode, int nLen)
{
    int nRet = ::MultiByteToWideChar(CP_ACP, 0, lpszAnsi, -1, lpszUnicode, nLen);
    return (0 == nRet) ? FALSE : TRUE;
}

bool EncodeUtil::UnicodeToAnsi(const wchar_t* lpszUnicode, char* lpszAnsi, int nLen)
{
    int nRet = ::WideCharToMultiByte(CP_ACP, 0, lpszUnicode, -1, lpszAnsi, nLen, NULL, NULL);
    return (0 == nRet) ? FALSE : TRUE;
}

bool EncodeUtil::AnsiToUtf8(const char* lpszAnsi, char* lpszUtf8, int nLen)
{
    wchar_t* lpszUnicode = EncodeUtil::AnsiToUnicode(lpszAnsi);
    if (NULL == lpszUnicode)
        return FALSE;

    int nRet = EncodeUtil::UnicodeToUtf8(lpszUnicode, lpszUtf8, nLen);

    delete[]lpszUnicode;

    return (0 == nRet) ? FALSE : TRUE;
}

bool EncodeUtil::Utf8ToAnsi(const char* lpszUtf8, char* lpszAnsi, int nLen)
{
    wchar_t* lpszUnicode = EncodeUtil::Utf8ToUnicode(lpszUtf8);
    if (NULL == lpszUnicode)
        return FALSE;

    int nRet = UnicodeToAnsi(lpszUnicode, lpszAnsi, nLen);

    delete[]lpszUnicode;

    return (0 == nRet) ? FALSE : TRUE;
}

bool EncodeUtil::UnicodeToUtf8(const wchar_t* lpszUnicode, char* lpszUtf8, int nLen)
{
    int nRet = ::WideCharToMultiByte(CP_UTF8, 0, lpszUnicode, -1, lpszUtf8, nLen, NULL, NULL);
    return (0 == nRet) ? FALSE : TRUE;
}

bool EncodeUtil::Utf8ToUnicode(const char* lpszUtf8, wchar_t* lpszUnicode, int nLen)
{
    int nRet = ::MultiByteToWideChar(CP_UTF8, 0, lpszUtf8, -1, lpszUnicode, nLen);
    return (0 == nRet) ? FALSE : TRUE;
}

std::wstring EncodeUtil::AnsiToUnicode(const std::string& strAnsi)
{
    std::wstring strUnicode;

    wchar_t* lpszUnicode = EncodeUtil::AnsiToUnicode(strAnsi.c_str());
    if (lpszUnicode != NULL)
    {
        strUnicode = lpszUnicode;
        delete[]lpszUnicode;
    }

    return strUnicode;
}
std::string EncodeUtil::UnicodeToAnsi(const std::wstring& strUnicode)
{
    std::string strAnsi;

    char* lpszAnsi = UnicodeToAnsi(strUnicode.c_str());
    if (lpszAnsi != NULL)
    {
        strAnsi = lpszAnsi;
        delete[]lpszAnsi;
    }

    return strAnsi;
}

std::string EncodeUtil::AnsiToUtf8(const std::string& strAnsi)
{
    std::string strUtf8;

    char* lpszUtf8 = AnsiToUtf8(strAnsi.c_str());
    if (lpszUtf8 != NULL)
    {
        strUtf8 = lpszUtf8;
        delete[]lpszUtf8;
    }

    return strUtf8;
}

std::string EncodeUtil::Utf8ToAnsi(const std::string& strUtf8)
{
    std::string strAnsi;

    char* lpszAnsi = Utf8ToAnsi(strUtf8.c_str());
    if (lpszAnsi != NULL)
    {
        strAnsi = lpszAnsi;
        delete[]lpszAnsi;
    }

    return strAnsi;
}

std::string EncodeUtil::UnicodeToUtf8(const std::wstring& strUnicode)
{
    std::string strUtf8;

    char* lpszUtf8 = EncodeUtil::UnicodeToUtf8(strUnicode.c_str());
    if (lpszUtf8 != NULL)
    {
        strUtf8 = lpszUtf8;
        delete[]lpszUtf8;
    }

    return strUtf8;
}

std::wstring EncodeUtil::Utf8ToUnicode(const std::string& strUtf8)
{
    std::wstring strUnicode;

    wchar_t* lpszUnicode = EncodeUtil::Utf8ToUnicode(strUtf8.c_str());
    if (lpszUnicode != NULL)
    {
        strUnicode = lpszUnicode;
        delete[]lpszUnicode;
    }

    return strUnicode;
}

//int EncodeUtil::code_convert(char* from_charset, char* to_charset, char* inbuf, size_t inlen, char* outbuf, size_t& outlen)
//{
//    iconv_t cd;
//    char** pin = &inbuf;
//    char** pout = &outbuf;
//
//    cd = iconv_open(to_charset, from_charset);
//    if (cd == 0)
//        return false;
//
//    memset(outbuf, 0, outlen);
//
//    if (iconv(cd, pin, &inlen, pout, &outlen) == -1)
//        return false;
//
//    iconv_close(cd);
//    return true;
//}
//
//bool EncodeUtil::Utf8ToGbk(char *inbuf, size_t inlen, char *outbuf, size_t outlen)
//{
//    return code_convert("utf-8", "gbk", inbuf, inlen, outbuf, outlen);
//}
//
//bool EncodeUtil::GbkToUtf8(char* inbuf, size_t inlen, char* outbuf, size_t outlen)
//{
//    return code_convert("gbk", "utf-8", inbuf, inlen, outbuf, outlen);
//}
//
//bool EncodeUtil::Utf8ToGbk2(char* inbuf, size_t inlen, char* outbuf, size_t& outlen)
//{
//    return code_convert("gbk", "utf-8", inbuf, inlen, outbuf, outlen);
//}
//
//int EncodeUtil::GbkToUtf8(char* utfstr, const char* srcstr, int maxutfstrlen)
//{
//    if (NULL == srcstr)
//        return -1;
//
//    //首先先将gbk编码转换为unicode编码
//    if (NULL == setlocale(LC_ALL, "zh_CN.gbk"))//设置转换为unicode前的码,当前为gbk编码
//        return -1;
//
//    int unicodelen = mbstowcs(NULL, srcstr, 0);//计算转换后的长度
//    if (unicodelen <= 0)
//        return -1;
//
//    wchar_t* unicodestr = (wchar_t *)calloc(sizeof(wchar_t), unicodelen + 1);
//    mbstowcs(unicodestr, srcstr, strlen(srcstr));//将gbk转换为unicode
//
//    //将unicode编码转换为utf8编码
//    if (NULL == setlocale(LC_ALL, "zh_CN.utf8"))//设置unicode转换后的码,当前为utf8
//        return -1;
//
//    int utflen = wcstombs(NULL, unicodestr, 0);//计算转换后的长度
//    if (utflen <= 0)
//        return -1;
//    else if (utflen >= maxutfstrlen)//判断空间是否足够
//        return -1;
//
//    wcstombs(utfstr, unicodestr, utflen);
//    utfstr[utflen] = 0;//添加结束符
//    free(unicodestr);
//
//    return utflen;
//}
