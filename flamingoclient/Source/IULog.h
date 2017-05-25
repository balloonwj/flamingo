#pragma once

enum LOG_LEVEL
{
	LOG_NORMAL,
	LOG_WARNING,
	LOG_ERROR
};

class CIULog
{
public:
	static BOOL Init(BOOL bToFile, PCTSTR pszLogFileName);
	static void Unit();
	
	//不输出线程ID号和所在函数签名
	static BOOL Log(long nLevel, PCTSTR pszFmt, ...);
	//输出线程ID号和所在函数签名
	static BOOL Log(long nLevel, PCSTR pszFunctionSig, PCTSTR pszFmt, ...);		//注意:pszFunctionSig参数为Ansic版本
	static BOOL Log(long nLevel, PCSTR pszFunctionSig, PCSTR pszFmt, ...);

private:
	static CString GetCurrentTime();
	
private:
	static BOOL		m_bToFile;				//日志写入文件还是写到控制台
	static CString  m_strLogFileName;
	static HANDLE	m_hLogFile;
};