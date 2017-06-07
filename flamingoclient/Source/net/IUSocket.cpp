#include "stdafx.h"
#include "IUSocket.h"
#include <assert.h>
#include "../IniFile.h"
#include "../UserSessionData.h"
#include "../IULog.h"
#include "../MiniBuffer.h"
#include "../EncodingUtil.h"
#include "Msg.h"
#include "IUProtocolData.h"
#include "../RecvMsgThread.h"
#include "protocolstream.h"

using namespace balloon;

CIUSocket::CIUSocket(CRecvMsgThread* pThread)
: m_strServer(_T("iu.Hootina.com")),
  m_bStop(false),
  m_pRecvMsgThread(pThread)
{
	m_hSocket = INVALID_SOCKET;
	m_hFileSocket = INVALID_SOCKET;
	m_nPort = 25251;
	m_nFilePort = 25261;

	m_nProxyType = 0;
	m_nProxyPort = 0;

	m_bConnected = FALSE;
	m_bConnectedOnFileSocket = FALSE;

    m_nHeartbeatInterval = 10;
    m_nLastDataTime = (long)time(NULL);

    m_nHeartbeatSeq = 0;
}

CIUSocket::~CIUSocket()
{

}

bool CIUSocket::Init()
{
    LoadConfig();

	if (!Connect())
		return false;

    m_spSendThread.reset(new std::thread(std::bind(&CIUSocket::SendThreadProc, this)));
    m_spRecvThread.reset(new std::thread(std::bind(&CIUSocket::RecvThreadProc, this)));

	return true;
}

void CIUSocket::Uninit()
{
    m_bStop = true;
	m_cvSendBuf.notify_one();
	m_cvRecvBuf.notify_one();
}

void CIUSocket::Join()
{
    m_spSendThread->join();
    m_spRecvThread->join();
}

void CIUSocket::LoadConfig()
{
	CIniFile iniFile;
	CString strIniFilePath(g_szHomePath);
	strIniFilePath += _T("config\\flamingo.ini");
	
	iniFile.ReadString(_T("server"), _T("server"), _T("iu.Hootina.com"), m_strServer.GetBuffer(64), 64, strIniFilePath);
	m_strServer.ReleaseBuffer();

	iniFile.ReadString(_T("server"), _T("fileserver"), _T("iu.Hootina.com"), m_strFileServer.GetBuffer(64), 64, strIniFilePath);
	m_strFileServer.ReleaseBuffer();

	m_nPort = iniFile.ReadInt(_T("server"), _T("port"), 8000, strIniFilePath);

	m_nFilePort = iniFile.ReadInt(_T("server"), _T("filePort"), 8001, strIniFilePath);

	m_nProxyType = iniFile.ReadInt(_T("server"), _T("proxyType"), 0, strIniFilePath);
	iniFile.ReadString(_T("server"), _T("proxyServer"), _T("xxx.com"), m_strProxyServer.GetBuffer(64), 64, strIniFilePath);
	m_strProxyServer.ReleaseBuffer();
	m_nProxyPort = iniFile.ReadInt(_T("server"), _T("proxyPort"), 4000, strIniFilePath);

    m_nHeartbeatInterval = iniFile.ReadInt(_T("server"), _T("heartbeatinterval"), 10, strIniFilePath);
}

void CIUSocket::SetServer(PCTSTR lpszServer)
{
	m_strServer = lpszServer;
	Close();
}

void CIUSocket::SetFileServer(PCTSTR lpszFileServer)
{
	m_strFileServer = lpszFileServer;
	CloseFileServerConnection();
}

//暂且没用到
void CIUSocket::SetProxyServer(PCTSTR lpszProxyServer)
{
	m_strProxyServer = lpszProxyServer;
}

void CIUSocket::SetPort(short nPort)
{
	m_nPort = nPort;
	//Close();
	CloseFileServerConnection();
}

void CIUSocket:: SetFilePort(short nFilePort)
{
	m_nFilePort = nFilePort;
	//Close();
	CloseFileServerConnection();
}

//暂且没用到
void CIUSocket::SetProxyPort(short nProxyPort)
{
	m_nProxyPort = nProxyPort;
}

//暂且没用到
void CIUSocket::SetProxyType(long nProxyType)
{
	m_nProxyType = nProxyType;
}

PCTSTR CIUSocket::GetServer() const
{
	return m_strServer.GetString();
}

PCTSTR CIUSocket::GetFileServer() const
{
	return m_strFileServer.GetString();
}

short CIUSocket::GetPort() const
{
	return m_nPort;
}

short CIUSocket::GetFilePort() const
{
	return m_nFilePort;
}

BOOL CIUSocket::Connect(int timeout /*= 3*/)
{
	if(!IsClosed())
		return TRUE;

	m_hSocket = ::socket(AF_INET, SOCK_STREAM, 0);
	if(m_hSocket == INVALID_SOCKET)
		return FALSE;

	long tmSend = 3*1000L;
	long tmRecv = 3*1000L;
	long noDelay = 1;
	setsockopt(m_hSocket, IPPROTO_TCP, TCP_NODELAY,(LPSTR)&noDelay, sizeof(long));
	setsockopt(m_hSocket, SOL_SOCKET,  SO_SNDTIMEO,(LPSTR)&tmSend, sizeof(long));
	setsockopt(m_hSocket, SOL_SOCKET,  SO_RCVTIMEO,(LPSTR)&tmRecv, sizeof(long));

    //将socket设置成非阻塞的
    unsigned long on = 1;
    if (::ioctlsocket(m_hSocket, FIONBIO, &on) == SOCKET_ERROR)
        return FALSE;

	struct sockaddr_in addrSrv = {0}; 
	struct hostent* pHostent = NULL;
	unsigned int addr = 0;
	
	CStringA strServer;
	UnicodeToAnsi(m_strServer, strServer.GetBuffer(64), 64);
	strServer.ReleaseBuffer();
	if((addrSrv.sin_addr.s_addr=inet_addr(strServer)) == INADDR_NONE)
	{
		pHostent = ::gethostbyname(strServer) ;
		if (!pHostent) 
		{
			CIULog::Log(LOG_ERROR, __FUNCSIG__, _T("Could not connect server:%s, port:%d."), m_strServer, m_nPort);
			return FALSE;
		}
		else
			addrSrv.sin_addr.s_addr =*((unsigned long*)pHostent->h_addr);
	}

	addrSrv.sin_family = AF_INET;
	addrSrv.sin_port = htons((u_short)m_nPort); 
    int ret = ::connect(m_hSocket, (struct sockaddr*)&addrSrv, sizeof(addrSrv));
    if (ret == 0)
	{
        CIULog::Log(LOG_NORMAL, __FUNCSIG__, _T("Connect server:%s, port:%d successfully."), m_strServer, m_nPort);
        m_bConnected = TRUE;
        return TRUE;
	}
    
    if (ret == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
    {
        CIULog::Log(LOG_ERROR, __FUNCSIG__, _T("Could not connect server:%s, port:%d."), m_strServer, m_nPort);
        return FALSE;
    }

    fd_set writeset;
    FD_ZERO(&writeset);
    FD_SET(m_hSocket, &writeset);
    struct timeval tv = { timeout, 0 };
    if (::select(m_hSocket + 1, NULL, &writeset, NULL, &tv) != 1)
    {
        CIULog::Log(LOG_ERROR, __FUNCSIG__, _T("Could not connect server:%s, port:%d."), m_strServer, m_nPort);
        return FALSE;
    }
    
    m_bConnected = TRUE;
	
	return TRUE;
}

BOOL CIUSocket::ConnectToFileServer()
{
	if(!IsFileServerClosed())
		return TRUE;

	m_hFileSocket = ::socket(AF_INET, SOCK_STREAM, 0);
	if(m_hFileSocket == INVALID_SOCKET)
		return FALSE;
	
	long tmSend = 3*1000L;
	long tmRecv = 3*1000L;
	long noDelay = 1;
	setsockopt(m_hFileSocket, IPPROTO_TCP, TCP_NODELAY,(LPSTR)&noDelay, sizeof(long));
	setsockopt(m_hFileSocket, SOL_SOCKET,  SO_SNDTIMEO,(LPSTR)&tmSend, sizeof(long));
	setsockopt(m_hFileSocket, SOL_SOCKET,  SO_RCVTIMEO,(LPSTR)&tmRecv, sizeof(long));
	
	struct sockaddr_in addrSrv = {0}; 
	struct hostent* pHostent = NULL;
	unsigned int addr = 0;
	
	CStringA strServer;
	UnicodeToAnsi(m_strFileServer, strServer.GetBuffer(64), 64);
	strServer.ReleaseBuffer();
	if((addrSrv.sin_addr.s_addr=inet_addr(strServer)) == INADDR_NONE)
	{
		pHostent = ::gethostbyname(strServer) ;
		if (!pHostent)
		{
			CIULog::Log(LOG_ERROR, __FUNCSIG__, _T("Could not connect file server:%s, port:%d."), m_strFileServer, m_nFilePort);
			return FALSE;
		}
		else
			addrSrv.sin_addr.s_addr =*((unsigned long*)pHostent->h_addr);
	}

	addrSrv.sin_family = AF_INET;
	addrSrv.sin_port = htons((u_short)m_nFilePort); 
	//TODO: 如果错误码是WSAEWOULDBLOCK，该怎么办？
	if(::connect(m_hFileSocket, (struct sockaddr*)&addrSrv, sizeof(addrSrv))==SOCKET_ERROR && ::WSAGetLastError()!=WSAEWOULDBLOCK)
	{
		CIULog::Log(LOG_ERROR, __FUNCSIG__, _T("Could not connect file server:%s, port:%d."), m_strFileServer, m_nFilePort);
		return FALSE;
	}
	
	CIULog::Log(LOG_ERROR, __FUNCSIG__, _T("Connect file server:%s, port:%d successfully."), m_strFileServer, m_nFilePort);
	m_bConnectedOnFileSocket = TRUE;

	return TRUE;
}

bool CIUSocket::CheckReceivedData()
{
	fd_set readset;
	FD_ZERO(&readset);
	FD_SET(m_hSocket, &readset);

	timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 500;
	
    long nRet = ::select(m_hSocket + 1, &readset, NULL, NULL, &timeout);
	if(nRet == 1)
		return true;
	else if(nRet == SOCKET_ERROR)
	{
		CIULog::Log(LOG_WARNING, __FUNCSIG__, _T("Check socket data error, disconnect server:%s, port:%d."), m_strServer, m_nPort);
		//Close();
	}

	//超时nRet=0，也返回FALSE，因为在超时的这段时间内也没有数据

	return false;
}

bool CIUSocket::Send()
{
	//如果未连接则重连，重连也失败则返回FALSE
    //TODO: 在发送数据的过程中重连没什么意义，因为与服务的Session已经无效了，换个地方重连
    if (IsClosed() && !Connect())
    {
        CIULog::Log(LOG_WARNING, __FUNCSIG__, _T("connect server:%s:%d error."), m_strServer, m_nPort);
        return false;
    }
	
	int nSentBytes = 0;
	int nRet = 0;
	while(true)
    {
        nRet = ::send(m_hSocket, m_strSendBuf.c_str(), m_strSendBuf.length(), 0);
		if(nRet == SOCKET_ERROR)				
		{
            if (::WSAGetLastError() == WSAEWOULDBLOCK)
                break;
            else
            {
                CIULog::Log(LOG_WARNING, __FUNCSIG__, _T("Send data error, disconnect server:%s, port:%d."), m_strServer, m_nPort);
                Close();
                return false;
            }
		}
		else if (nRet < 1)
		{
			//一旦出现错误就立刻关闭Socket
			CIULog::Log(LOG_WARNING, __FUNCSIG__, _T("Send data error, disconnect server:%s, port:%d."), m_strServer, m_nPort);
			Close();
			return false;
		}
		
        m_strSendBuf.erase(0, nRet);		
        if (m_strSendBuf.empty())
			break;

		::Sleep(1);
	}

    {
        std::lock_guard<std::mutex> guard(m_mutexLastDataTime);
        m_nLastDataTime = (long)time(NULL);
    }

	return true;
}


bool CIUSocket::Recv()
{
	int nRet = 0;
	
    while(true)
    {
        char buff[512];
        nRet = ::recv(m_hSocket, buff, 512, 0);
		if(nRet == SOCKET_ERROR)				//一旦出现错误就立刻关闭Socket
		{
            if (::WSAGetLastError() == WSAEWOULDBLOCK)
                break;
            else
            {
                CIULog::Log(LOG_WARNING, __FUNCSIG__, _T("Recv data error, disconnect server:%s, port:%d."), m_strServer, m_nPort);
                //Close();
                return false;
            }
		}
		else if(nRet < 1)
		{
			CIULog::Log(LOG_WARNING, __FUNCSIG__, _T("Recv data error, disconnect server:%s, port:%d."), m_strServer, m_nPort);
			//Close();
            return false;
		}
		
        m_strRecvBuf.append(buff, nRet);
		
		::Sleep(1);
	} 

    {
        std::lock_guard<std::mutex> guard(m_mutexLastDataTime);
        m_nLastDataTime = (long)time(NULL);
    }

	return true;
}

BOOL CIUSocket::SendOnFilePort(const char* pBuffer, long nSize)
{
	assert(pBuffer!=NULL && nSize>0);
	//如果未连接则重连，重连也失败则返回FALSE
	if(IsFileServerClosed() && !ConnectToFileServer())
		return FALSE;
	
	int nSentBytes = 0;
	int nRet = 0;
	do 
	{
		nRet = ::send(m_hFileSocket, pBuffer+nSentBytes, nSize-nSentBytes, 0);
		if(nRet==SOCKET_ERROR && ::WSAGetLastError()==WSAEWOULDBLOCK)				
		{
			::Sleep(1);
			continue;
		}
		else if(nRet < 1)
		{
			//一旦出现错误就立刻关闭Socket
			CIULog::Log(LOG_WARNING, __FUNCSIG__, _T("Send data error, disconnect server:%s, port:%d."), m_strServer, m_nFilePort);
			CloseFileServerConnection();
			return FALSE;
		}
		
		nSentBytes += nRet;
		
		if(nSentBytes>=nSize)
			break;

		::Sleep(1);

	} while (TRUE);

	return TRUE;
}

BOOL CIUSocket::RecvOnFilePort(char* pBuffer, long nSize)
{
	assert(pBuffer!=NULL && nSize>0);

	if(IsFileServerClosed() && !ConnectToFileServer())
		return FALSE;

	int nRet = 0;
	long nRecvBytes = 0;
	do 
	{
		nRet = ::recv(m_hFileSocket, pBuffer+nRecvBytes, nSize-nRecvBytes, 0);
		if(nRet==SOCKET_ERROR && ::WSAGetLastError()==WSAEWOULDBLOCK)				//一旦出现错误就立刻关闭Socket
		{
			::Sleep(1);
			continue;
		}
		else if(nRet < 1)
		{
			CIULog::Log(LOG_WARNING, __FUNCSIG__, _T("Recv data error, disconnect server:%s, port:%d."), m_strServer, m_nFilePort);
			CloseFileServerConnection();
			return FALSE;
		}
		
		nRecvBytes += nRet;
		if(nRecvBytes >= nSize)
			break;

		::Sleep(1);

	} while (TRUE);


	return TRUE;
}

BOOL CIUSocket::IsClosed()
{
	if(m_hSocket == INVALID_SOCKET) 
		return TRUE;

	fd_set fd = {0};
	FD_ZERO(&fd);
	FD_SET(m_hSocket, &fd);
	timeval tmout={0, 1};
	int rtCode = select(m_hSocket+1, &fd, NULL, NULL, &tmout);
	
	if(!rtCode || rtCode==1)
		return FALSE;
	
	//只要有异常就关闭，下次重连
	Close();

	return TRUE;
}

BOOL CIUSocket::IsFileServerClosed()
{
	if(m_hFileSocket == INVALID_SOCKET) 
		return TRUE;

	fd_set fd = {0};
	FD_ZERO(&fd);
	FD_SET(m_hFileSocket, &fd);
	timeval tmout={0, 1};
	int rtCode = select(m_hFileSocket+1, &fd, NULL, NULL, &tmout);
	
	if(!rtCode || rtCode==1)
		return FALSE;
	
	//只要有异常就关闭，下次重连
	CloseFileServerConnection();
	return TRUE;
}

void CIUSocket::Close()
{
	//FIXME: 这个函数会被数据发送线程和收取线程同时调用，不安全
    if(m_hSocket == INVALID_SOCKET)
		return;
	
	::shutdown(m_hSocket, SD_BOTH);
	::closesocket(m_hSocket);
	m_hSocket = INVALID_SOCKET;

	m_bConnected = FALSE;
}

void CIUSocket::CloseFileServerConnection()
{
	if(m_hFileSocket == INVALID_SOCKET)
		return;

	::shutdown(m_hFileSocket, SD_BOTH);
	::closesocket(m_hFileSocket);
	m_hFileSocket= INVALID_SOCKET;

	CIULog::Log(LOG_WARNING, __FUNCSIG__, _T("Disconnect file server:%s, port:%d."), m_strServer, m_nFilePort);
	
	m_bConnectedOnFileSocket = FALSE;
}

void CIUSocket::SendThreadProc()
{
    while (!m_bStop)
    {
        std::unique_lock<std::mutex> guard(m_mtSendBuf);
        while (m_strSendBuf.empty())
        {
            if (m_bStop)
                return;

            m_cvSendBuf.wait(guard);
        }

        //TODO: 进行重连，如果连接不上，则向客户报告错误
        if (!Send())
        {

        }
    }
}

void CIUSocket::Send(const std::string& strBuffer)
{ 
    std::lock_guard<std::mutex> guard(m_mtSendBuf);
    //插入包头
    int32_t length = (int32_t)strBuffer.length();
    msg header = { length };
    m_strSendBuf.append((const char*)&header, sizeof(header));
    m_strSendBuf.append(strBuffer.c_str(), length);
    m_cvSendBuf.notify_one();
}

void CIUSocket::RecvThreadProc()
{
    while (!m_bStop)
    {
        //检测到数据则收数据
        if (CheckReceivedData())
        {
            //TODO: 进行重连，如果连接不上，则向客户报告错误
            if (!Recv())
            {
                continue;
            }

            //一定要放在一个循环里面解包，因为可能一片数据中有多个包，
            //对于数据收不全，这个地方我纠结了好久T_T
            while (true)
            {
                if (m_strRecvBuf.length() <= sizeof(msg))
                    break;

                msg header;
                memcpy_s(&header, sizeof(msg), m_strRecvBuf.data(), sizeof(msg));
                if (m_strRecvBuf.length() < sizeof(msg))
                    break;

                //去除包头信息
                m_strRecvBuf.erase(0, sizeof(msg));
                std::string strBody;
                strBody.append(m_strRecvBuf.c_str(), header.packagesize);
                //去除包体信息
                m_strRecvBuf.erase(0, header.packagesize);

                m_pRecvMsgThread->AddMsgData(strBody);
            }
        }
        else
        {
            long nLastDataTime = 0;
            {
                std::lock_guard<std::mutex> guard(m_mutexLastDataTime);
                nLastDataTime = m_nLastDataTime;
            }
//调试版本就不要发心跳包了，影响调试
#ifndef _DEBUG
            if (time(NULL) - nLastDataTime >= m_nHeartbeatInterval)
                SendHeartbeatPackage();
#endif
        }// end if
    }// end while-loop
}


void CIUSocket::SendHeartbeatPackage()
{
    std::string outbuf;
    BinaryWriteStream writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_heartbeart);
    writeStream.WriteInt32(m_nHeartbeatSeq);
    std::string dummy;
    writeStream.WriteString(dummy);
    writeStream.Flush();

    ++m_nHeartbeatSeq;

    Send(outbuf);
}
