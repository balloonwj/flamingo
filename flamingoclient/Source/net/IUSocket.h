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
private:
	CIUSocket();
    ~CIUSocket(void);

    CIUSocket(const CIUSocket& rhs) = delete;
    CIUSocket& operator = (const CIUSocket& rhs) = delete;

public:
    static CIUSocket&  GetInstance();

    void SetRecvMsgThread(CRecvMsgThread* pThread);

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

    //开启和关闭聊天服务器断线自动重连
    void    EnableReconnect(bool bEnable);

    /** 
    *@param timeout 超时时间，单位为s
    **/
	bool	Connect(int timeout = 3);
    bool    Reconnect(int timeout = 3);
    bool	ConnectToFileServer(int timeout = 3);
    bool	ConnectToImgServer(int timeout = 3);
	
	bool	IsClosed();
	bool	IsFileServerClosed();
    bool	IsImgServerClosed();
	
	void	Close();
	void	CloseFileServerConnection();
    void	CloseImgServerConnection();

    /** 判断普通Socket上是否收到数据
     * @return -1出错， 0无数据 1有数据
     */
	int    CheckReceivedData();

	//异步接口
    void    Send(const std::string& strBuffer);

   	//同步接口
    //nTimeout单位是秒
    bool    Register(const char* pszUser, const char* pszNickname, const char* pszPassword, int nTimeout, std::string& strReturnData);
    bool    Login(const char* pszUser, const char* pszPassword, int nClientType, int nOnlineStatus, int nTimeout, std::string& strReturnData);

    //超时时间,单位为秒
	bool    SendOnFilePort(const char* pBuffer, int64_t nSize, int nTimeout = 30);	
    bool	RecvOnFilePort(char* pBuffer, int64_t nSize, int nTimeout = 30);
    //bool    CheckRecvDataOnFilePort(int nTimeout = 3000);

    bool    SendOnImgPort(const char* pBuffer, int64_t nSize, int nTimeout = 30);
    bool	RecvOnImgPort(char* pBuffer, int64_t nSize, int nTimeout = 30);
    //bool    CheckRecvDataOnImgPort(int nTimeout = 3000);

private:   
    void    SendThreadProc();
    void    RecvThreadProc();

    void    SendHeartbeatPackage();

    bool	Send();
    bool	Recv();

    //发送nBuffSize长度的字节，如果发不出去，则就认为失败
    bool    SendData(const char* pBuffer, int nBuffSize, int nTimeout);
    //收取nBufferSize长度的字节，如果收不到，则认为失败
    bool    RecvData(char* pszBuff, int nBufferSize, int nTimeout);

    //解包
    bool    DecodePackages();
    
private:	
	SOCKET							m_hSocket;				//一般用途Socket（非阻塞socket）
	SOCKET							m_hFileSocket;			//传文件的Socket（阻塞socket）
    SOCKET							m_hImgSocket;			//传聊天的Socket（阻塞socket）
	short							m_nPort;
	short							m_nFilePort;
    short							m_nImgPort;
	std::string						m_strServer;			//服务器地址
    std::string						m_strFileServer;		//文件服务器地址
    std::string						m_strImgServer;		    //图片服务器地址
	
	long							m_nProxyType;			//代理类型（0不使用代理；）
    std::string						m_strProxyServer;		//代理服务器地址
	short							m_nProxyPort;			//代理服务器端口号

    long                            m_nLastDataTime;        //最近一次收发数据的时间
    std::mutex                      m_mutexLastDataTime;    //保护m_nLastDataTime的互斥体
    long                            m_nHeartbeatInterval;   //心跳包时间间隔，单位秒,默认为10秒
    int32_t                         m_nHeartbeatSeq;        //心跳包序列号
	
	bool							m_bConnected;
	bool							m_bConnectedOnFileSocket;
    bool                            m_bConnectedOnImgSocket;

    std::unique_ptr<std::thread>    m_spSendThread;
    std::unique_ptr<std::thread>    m_spRecvThread;

    std::string                     m_strSendBuf;
    std::string                     m_strRecvBuf;

    std::mutex                      m_mtSendBuf;

    std::condition_variable         m_cvSendBuf;
    std::condition_variable         m_cvRecvBuf;

    bool                            m_bStop;

    bool                            m_bEnableReconnect;     //聊天服务器断线是否重连

    std::string                     m_strRecordUser;
    std::string                     m_strRecordPassword;
    int                             m_nRecordClientType;
    int                             m_nRecordOnlineStatus;
    int                             m_seq;


	CRecvMsgThread*					m_pRecvMsgThread;
};