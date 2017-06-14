#pragma once

#include <thread>
#include <mutex>
#include <string>
#include <memory>
#include <condition_variable>
#include <stdint.h>

class CRecvMsgThread;

//网络通信层只负责数据传输和接收
class CIUSocket
{
public:
	CIUSocket(CRecvMsgThread* pThread);
    ~CIUSocket(void);

    CIUSocket(const CIUSocket& rhs) = delete;
    CIUSocket& operator = (const CIUSocket& rhs) = delete;

    bool Init();
    void Uninit();

    void Join();
	
	void	LoadConfig();
	void	SetServer(PCTSTR lpszServer);
	void	SetFileServer(PCTSTR lpszFileServer);
    void	SetImgServer(PCTSTR lpszImgServer);
	void	SetProxyServer(PCTSTR lpszProxyServer);
	void	SetPort(short nPort);
	void    SetFilePort(short nFilePort);
    void    SetImgPort(short nImgPort);
	void	SetProxyPort(short nProxyPort);
	void    SetProxyType(long nProxyType);

	PCTSTR  GetServer() const;
	PCTSTR	GetFileServer() const;
    PCTSTR	GetImgServer() const;
	short   GetPort() const;
    short	GetFilePort() const;
    short	GetImgPort() const;

    /** 
    *@param timeout 超时时间，单位为s
    **/
	BOOL	Connect(int timeout = 3);
	BOOL	ConnectToFileServer();
    BOOL	ConnectToImgServer();
	
	BOOL	IsClosed();
	BOOL	IsFileServerClosed();
    BOOL	IsImgServerClosed();
	
	void	Close();
	void	CloseFileServerConnection();
    void	CloseImgServerConnection();

	bool    CheckReceivedData();							//判断普通Socket上是否收到数据，有返回true，没有返回false

	//异步接口
    void    Send(const std::string& strBuffer);
	
	//同步接口
	BOOL    SendOnFilePort(const char* pBuffer, int64_t nSize);	
    BOOL	RecvOnFilePort(char* pBuffer, int64_t nSize);
    BOOL    SendOnImgPort(const char* pBuffer, int64_t nSize);
    BOOL	RecvOnImgPort(char* pBuffer, int64_t nSize);

private:   
    void    SendThreadProc();
    void    RecvThreadProc();

    void    SendHeartbeatPackage();

    bool	Send();
    bool	Recv();
    

private:	
	SOCKET							m_hSocket;				//一般用途Socket（非阻塞socket）
	SOCKET							m_hFileSocket;			//传文件的Socket（阻塞socket）
    SOCKET							m_hImgSocket;			//传聊天的Socket（阻塞socket）
	short							m_nPort;
	short							m_nFilePort;
    short							m_nImgPort;
	CString							m_strServer;			//服务器地址
	CString							m_strFileServer;		//文件服务器地址
    CString							m_strImgServer;		    //图片服务器地址
	
	long							m_nProxyType;			//代理类型（0不使用代理；）
	CString							m_strProxyServer;		//代理服务器地址
	short							m_nProxyPort;			//代理服务器端口号

    long                            m_nLastDataTime;        //最近一次收发数据的时间
    std::mutex                      m_mutexLastDataTime;    //保护m_nLastDataTime的互斥体
    long                            m_nHeartbeatInterval;   //心跳包时间间隔，单位秒
    int32_t                         m_nHeartbeatSeq;        //心跳包序列号
	
	BOOL							m_bConnected;
	BOOL							m_bConnectedOnFileSocket;
    BOOL                            m_bConnectedOnImgSocket;

    std::shared_ptr<std::thread>    m_spSendThread;
    std::shared_ptr<std::thread>    m_spRecvThread;

    std::string                     m_strSendBuf;
    std::string                     m_strRecvBuf;

    std::mutex                      m_mtSendBuf;

    std::condition_variable         m_cvSendBuf;
    std::condition_variable         m_cvRecvBuf;

    bool                            m_bStop;

	CRecvMsgThread*					m_pRecvMsgThread;
};