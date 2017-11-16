/**
 * 性能计数器，统计一段代码执行的耗时，并打印出来,PerformanceCounter.h
 * zhangyl 2017.07.27
 */
#ifndef __PERMANCE_COUNTER_H__
#define __PERMANCE_COUNTER_H__

//#ifdef PERMANCECOUNTER_EXPORTS
//#define PERMANCECOUNTER_API __declspec(dllexport)
//#else
//#define PERMANCECOUNTER_API __declspec(dllimport)
//#endif

#define PERMANCECOUNTER_API

#define BEGIN_PERFORMANCECOUNTER    CPerformanceCounter::Begin(__FUNCSIG__,__LINE__);
#define END_PERFORMANCECOUNTER      CPerformanceCounter::End(__FUNCSIG__, __LINE__);

class PERMANCECOUNTER_API CPerformanceCounter
{
public:
    static bool Init(bool bToFile, PCTSTR pszLogFileName);
    static void Uninit();

    //输出线程ID号和所在函数签名、行号
    static bool Begin(PCSTR pszFunctionSig, int nLineNo);		//注意:pszFunctionSig参数为Ansic版本
    static bool End(PCSTR pszFunctionSig, int nLineNo);

private:
    CPerformanceCounter() = delete;
    ~CPerformanceCounter() = delete;

    CPerformanceCounter(const CPerformanceCounter& rhs) = delete;
    CPerformanceCounter& operator=(const CPerformanceCounter& rhs) = delete;

    static void GetTime(char* pszTime, int nTimeStrLength);

private:
    static bool		        m_bToFile;				//日志写入文件还是写到控制台  
    static HANDLE	        m_hPerformanceFile;
    static LARGE_INTEGER    m_liFreq;
    static LARGE_INTEGER    m_liBegin;
    static LARGE_INTEGER    m_liEnd;
};

#endif // !__PERMANCE_COUNTER_H__