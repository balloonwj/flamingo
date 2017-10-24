#ifndef __LOG_H__
#define __LOG_H__

//#ifdef LOG_EXPORTS
//#define LOG_API __declspec(dllexport)
//#else
//#define LOG_API __declspec(dllimport)
//#endif

#define LOG_API

enum LOG_LEVEL
{
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_ERROR
};

//注意：如果打印的日志信息中有中文，则格式化字符串要用_T()宏包裹起来，
//e.g. LOG_INFO(_T("GroupID=%u, GroupName=%s, GroupName=%s."), lpGroupInfo->m_nGroupCode, lpGroupInfo->m_strAccount.c_str(), lpGroupInfo->m_strName.c_str());
#define LOG_INFO(...)     CIULog::Log(LOG_LEVEL_INFO, __FUNCSIG__,__LINE__, __VA_ARGS__)
#define LOG_WARNING(...)    CIULog::Log(LOG_LEVEL_WARNING, __FUNCSIG__, __LINE__,__VA_ARGS__)
#define LOG_ERROR(...)      CIULog::Log(LOG_LEVEL_ERROR, __FUNCSIG__,__LINE__, __VA_ARGS__)

class LOG_API CIULog
{
public:
    static bool Init(bool bToFile, bool bTruncateLongLog, PCTSTR pszLogFileName);
	static void Uninit();

    static void SetLevel(LOG_LEVEL nLevel);
	
	//不输出线程ID号和所在函数签名、行号
	static bool Log(long nLevel, PCTSTR pszFmt, ...);
	//输出线程ID号和所在函数签名、行号
	static bool Log(long nLevel, PCSTR pszFunctionSig, int nLineNo, PCTSTR pszFmt, ...);		//注意:pszFunctionSig参数为Ansic版本
    static bool Log(long nLevel, PCSTR pszFunctionSig, int nLineNo, PCSTR pszFmt, ...);
private:
    CIULog() = delete;
    ~CIULog() = delete;

    CIULog(const CIULog& rhs) = delete;
    CIULog& operator=(const CIULog& rhs) = delete;

    static void GetTime(char* pszTime, int nTimeStrLength);
	
private:
	static bool		        m_bToFile;			    //日志写入文件还是写到控制台  
	static HANDLE	        m_hLogFile;
    static bool             m_bTruncateLongLog;     //长日志是否截断
    static LOG_LEVEL        m_nLogLevel;            //日志级别
};

#endif // !__LOG_H__