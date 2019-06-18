/** 
 * Windows平台用编码格式相互转换类, EncodingUtil.h
 * zhangyl 2017.03.29
 **/
#ifndef __ENCODE_H__
#define __ENCODE_H__

//#ifdef ENCODE_EXPORTS
//#define ENCODE_API __declspec(dllexport)
//#else
//#define ENCODE_API __declspec(dllimport)
//#endif

#include <string>

#define ENCODE_API

class ENCODE_API EncodeUtil
{
public:
    //===BEGIN: 注意：以下6个函数,需要在外部释放返回的字符串指针，否则会有内存泄露
    static wchar_t* AnsiToUnicode(const char* lpszStr);
    static char* UnicodeToAnsi(const wchar_t* lpszStr);
    static char* AnsiToUtf8(const char* lpszStr);
    static char* Utf8ToAnsi(const char* lpszStr);
    static char* UnicodeToUtf8(const wchar_t* lpszStr);
    static wchar_t* Utf8ToUnicode(const char* lpszStr);
    //===END: 注意：以下6个函数,需要在外部释放返回的字符串指针，否则会有内存泄露

    //===BEGIN: 以下函数第一个参数是需要转换的源字符串指针，第二个参数是存放转换后的目标缓冲区指针，第三个参数是目标缓冲区的大小
    static bool AnsiToUnicode(const char* lpszAnsi, wchar_t* lpszUnicode, int nLen);
    static bool UnicodeToAnsi(const wchar_t* lpszUnicode, char* lpszAnsi, int nLen);
    static bool AnsiToUtf8(const char* lpszAnsi, char* lpszUtf8, int nLen);
    static bool Utf8ToAnsi(const char* lpszUtf8, char* lpszAnsi, int nLen);
    static bool UnicodeToUtf8(const wchar_t* lpszUnicode, char* lpszUtf8, int nLen);
    static bool Utf8ToUnicode(const char* lpszUtf8, wchar_t* lpszUnicode, int nLen);
    //===END: 以下函数第一个参数是需要转换的源字符串指针，第二个参数是存放转换后的目标缓冲区指针，第三个参数是目标缓冲区的大小

    static std::wstring AnsiToUnicode(const std::string& strAnsi);
    static std::string UnicodeToAnsi(const std::wstring& strUnicode);
    static std::string AnsiToUtf8(const std::string& strAnsi);
    static std::string Utf8ToAnsi(const std::string& strUtf8);
    static std::string UnicodeToUtf8(const std::wstring& strUnicode);
    static std::wstring Utf8ToUnicode(const std::string& strUtf8);

private:
    EncodeUtil() = delete;
    ~EncodeUtil() = delete;

    EncodeUtil(const EncodeUtil& rhs) = delete;
    EncodeUtil& operator=(const EncodeUtil& rhs) = delete;
};

#endif // !__ENCODE_H__
