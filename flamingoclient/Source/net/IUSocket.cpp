#include "stdafx.h"
#include "IUSocket.h"

#include "../IniFile.h"
#include "../UserSessionData.h"
#include "Msg.h"
#include "IUProtocolData.h"
#include "../RecvMsgThread.h"
#include "../EncodeUtil.h"
#include "../IULog.h"
#include "../MiniBuffer.h"
#include "protocolstream.h"
#include "../ZlibUtil.h"
#include <tchar.h>
#include <Sensapi.h>
#include <functional>

#pragma comment(lib, "Sensapi.lib")

//包最大字节数限制为10M
#define MAX_PACKAGE_SIZE    10 * 1024 * 1024

CIUSocket::CIUSocket()
{
    m_hSocket = INVALID_SOCKET;
    m_hFileSocket = INVALID_SOCKET;
    m_hImgSocket = INVALID_SOCKET;
    m_nPort = 20000;
    m_nFilePort = 20001;
    m_nImgPort = 20002;

    m_nProxyType = 0;
    m_nProxyPort = 0;

    m_bConnected = false;
    m_bConnectedOnFileSocket = false;
    m_bConnectedOnImgSocket = false;

    m_nHeartbeatInterval = 0;
    m_nLastDataTime = (long)time(NULL);

    m_nHeartbeatSeq = 0;

    m_bStop = false;

    m_bEnableReconnect = true;

    m_nRecordClientType = 1;
    m_nRecordOnlineStatus = 1;

    m_seq = 0;

    m_pRecvMsgThread = NULL;
}

CIUSocket::~CIUSocket()
{

}

CIUSocket&  CIUSocket::GetInstance()
{
    static CIUSocket socketInstance;
    return socketInstance;
}

void CIUSocket::SetRecvMsgThread(CRecvMsgThread* pThread)
{
    m_pRecvMsgThread = pThread;
}

bool CIUSocket::Init()
{
    m_bStop = false;

    if (!m_spSendThread)
        m_spSendThread.reset(new std::thread(std::bind(&CIUSocket::SendThreadProc, this)));

    if (!m_spRecvThread)
        m_spRecvThread.reset(new std::thread(std::bind(&CIUSocket::RecvThreadProc, this)));

    return true;
}

void CIUSocket::Uninit()
{
    m_bStop = true;
    if (m_spSendThread)
        m_spSendThread->detach();
    if (m_spRecvThread)
        m_spRecvThread->detach();

    m_cvSendBuf.notify_one();
    m_cvRecvBuf.notify_one();

    //如果线程退掉了，这里指针会自动为空，所以再次reset()时先判断一下
    if (m_spSendThread)
        m_spSendThread.reset();
    if (m_spRecvThread)
        m_spRecvThread.reset();

    CloseFileServerConnection();
    CloseImgServerConnection();
}

void CIUSocket::Join()
{
    if (m_spSendThread && m_spSendThread->joinable())
        m_spSendThread->join();
    if (m_spRecvThread && m_spRecvThread->joinable())
        m_spRecvThread->join();
}

void CIUSocket::LoadConfig()
{
    CIniFile iniFile;
    CString        strIniFilePath(g_szHomePath);
    strIniFilePath += _T("config\\flamingo.ini");

    TCHAR szServer[64] = { 0 };
    iniFile.ReadString(_T("server"), _T("server"), _T("flamingo.hootina.org"), szServer, 64, strIniFilePath);
    m_strServer = EncodeUtil::UnicodeToAnsi(std::wstring(szServer));

    TCHAR szFileServer[64] = { 0 };
    iniFile.ReadString(_T("server"), _T("fileserver"), _T("flamingo.hootina.org"), szFileServer, 64, strIniFilePath);
    m_strFileServer = EncodeUtil::UnicodeToAnsi(std::wstring(szFileServer));

    TCHAR szImgServer[64] = { 0 };
    iniFile.ReadString(_T("server"), _T("imgserver"), _T("flamingo.hootina.org"), szImgServer, 64, strIniFilePath);
    m_strImgServer = EncodeUtil::UnicodeToAnsi(std::wstring(szImgServer));

    m_nPort = iniFile.ReadInt(_T("server"), _T("port"), 20000, strIniFilePath);
    m_nFilePort = iniFile.ReadInt(_T("server"), _T("fileport"), 20001, strIniFilePath);
    m_nImgPort = iniFile.ReadInt(_T("server"), _T("imgport"), 20002, strIniFilePath);

    m_nProxyType = iniFile.ReadInt(_T("server"), _T("proxytype"), 0, strIniFilePath);
    TCHAR szProxyServer[64] = { 0 };
    iniFile.ReadString(_T("server"), _T("proxyServer"), _T("xxx.com"), szProxyServer, 64, strIniFilePath);
    m_strProxyServer = EncodeUtil::UnicodeToAnsi(std::wstring(szProxyServer));
    m_nProxyPort = iniFile.ReadInt(_T("server"), _T("proxyport"), 4000, strIniFilePath);

    m_nHeartbeatInterval = iniFile.ReadInt(_T("server"), _T("heartbeatinterval"), 0, strIniFilePath);

    if (iniFile.ReadInt(_T("server"), _T("enablereconnect"), 1, strIniFilePath) != 0)
        m_bEnableReconnect = true;
    else
        m_bEnableReconnect = false;
}

void CIUSocket::SetServer(PCTSTR lpszServer)
{
    m_strServer = EncodeUtil::UnicodeToAnsi(std::wstring(lpszServer));
    Close();
}

void CIUSocket::SetFileServer(PCTSTR lpszFileServer)
{
    m_strFileServer = EncodeUtil::UnicodeToAnsi(std::wstring(lpszFileServer));
    CloseFileServerConnection();
}

void CIUSocket::SetImgServer(PCTSTR lpszImgServer)
{
    m_strImgServer = EncodeUtil::UnicodeToAnsi(std::wstring(lpszImgServer));
    CloseImgServerConnection();
}

//暂且没用到
void CIUSocket::SetProxyServer(PCTSTR lpszProxyServer)
{
    m_strProxyServer = EncodeUtil::UnicodeToAnsi(std::wstring(lpszProxyServer));
}

void CIUSocket::SetPort(short nPort)
{
    m_nPort = nPort;
    CloseFileServerConnection();
}

void CIUSocket::SetFilePort(short nFilePort)
{
    m_nFilePort = nFilePort;
    CloseFileServerConnection();
}

void CIUSocket::SetImgPort(short nImgPort)
{
    m_nImgPort = nImgPort;
    CloseImgServerConnection();
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

void CIUSocket::EnableReconnect(bool bEnable)
{
    m_bEnableReconnect = bEnable;
}

bool CIUSocket::Connect(int timeout /*= 3*/)
{
    Close();

    m_hSocket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_hSocket == INVALID_SOCKET)
        return false;

    long tmSend = 3 * 1000L;
    long tmRecv = 3 * 1000L;
    long noDelay = 1;
    setsockopt(m_hSocket, IPPROTO_TCP, TCP_NODELAY, (LPSTR)&noDelay, sizeof(long));
    setsockopt(m_hSocket, SOL_SOCKET, SO_SNDTIMEO, (LPSTR)&tmSend, sizeof(long));
    setsockopt(m_hSocket, SOL_SOCKET, SO_RCVTIMEO, (LPSTR)&tmRecv, sizeof(long));

    //将socket设置成非阻塞的
    unsigned long on = 1;
    if (::ioctlsocket(m_hSocket, FIONBIO, &on) == SOCKET_ERROR)
        return false;

    struct sockaddr_in addrSrv = { 0 };
    struct hostent* pHostent = NULL;
    unsigned int addr = 0;

    if ((addrSrv.sin_addr.s_addr = inet_addr(m_strServer.c_str())) == INADDR_NONE)
    {
        pHostent = ::gethostbyname(m_strServer.c_str());
        if (!pHostent)
        {
            LOG_ERROR("Could not connect server:%s, port:%d.", m_strServer.c_str(), m_nPort);
            return false;
        }
        else
            addrSrv.sin_addr.s_addr = *((unsigned long*)pHostent->h_addr);
    }

    addrSrv.sin_family = AF_INET;
    addrSrv.sin_port = htons((u_short)m_nPort);
    int ret = ::connect(m_hSocket, (struct sockaddr*)&addrSrv, sizeof(addrSrv));
    if (ret == 0)
    {
        LOG_INFO("Connect to server:%s, port:%d successfully.", m_strServer.c_str(), m_nPort);
        m_bConnected = true;
        return true;
    }
    else if (ret == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
    {
        LOG_ERROR("Could not connect to server:%s, port:%d.", m_strServer.c_str(), m_nPort);
        return false;
    }

    fd_set writeset;
    FD_ZERO(&writeset);
    FD_SET(m_hSocket, &writeset);
    struct timeval tv = { timeout, 0 };
    if (::select(m_hSocket + 1, NULL, &writeset, NULL, &tv) != 1)
    {
        LOG_ERROR("Could not connect to server:%s, port:%d.", m_strServer.c_str(), m_nPort);
        return false;
    }

    m_bConnected = true;

    return true;
}

bool CIUSocket::Reconnect(int timeout/* = 3*/)
{
    if (m_strRecordUser.empty() || m_strRecordPassword.empty())
    {
        LOG_ERROR("Failed to reconnect to chat server, RecordUser or RecordPassword is empty, RecordUser=%s, RecordPassword=%s", m_strRecordUser.c_str(), m_strRecordPassword.c_str());
        return false;
    }


    if (!Connect(timeout))
    {
        LOG_ERROR("Failed to reconnect to chat server");
        return false;
    }


    //if (Login(m_strRecordUser.c_str(), m_strRecordPassword.c_str(), m_nRecordClientType, m_nRecordOnlineStatus, ))

    return false;
}

bool CIUSocket::ConnectToFileServer(int timeout/* = 3*/)
{
    if (!IsFileServerClosed())
        return true;

    m_hFileSocket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_hFileSocket == INVALID_SOCKET)
        return false;

    long tmSend = 3 * 1000L;
    long tmRecv = 3 * 1000L;
    long noDelay = 1;
    setsockopt(m_hFileSocket, IPPROTO_TCP, TCP_NODELAY, (LPSTR)&noDelay, sizeof(long));
    setsockopt(m_hFileSocket, SOL_SOCKET, SO_SNDTIMEO, (LPSTR)&tmSend, sizeof(long));
    setsockopt(m_hFileSocket, SOL_SOCKET, SO_RCVTIMEO, (LPSTR)&tmRecv, sizeof(long));

    //将socket设置成非阻塞的
    unsigned long on = 1;
    if (::ioctlsocket(m_hFileSocket, FIONBIO, &on) == SOCKET_ERROR)
        return false;

    struct sockaddr_in addrSrv = { 0 };
    struct hostent* pHostent = NULL;
    unsigned int addr = 0;

    if ((addrSrv.sin_addr.s_addr = inet_addr(m_strFileServer.c_str())) == INADDR_NONE)
    {
        pHostent = ::gethostbyname(m_strFileServer.c_str());
        if (!pHostent)
        {
            LOG_ERROR("Could not connect file server:%s, port:%d.", m_strFileServer.c_str(), m_nFilePort);
            return false;
        }
        else
            addrSrv.sin_addr.s_addr = *((unsigned long*)pHostent->h_addr);
    }

    addrSrv.sin_family = AF_INET;
    addrSrv.sin_port = htons((u_short)m_nFilePort);
    int ret = ::connect(m_hFileSocket, (struct sockaddr*)&addrSrv, sizeof(addrSrv));
    if (ret == 0)
    {
        LOG_INFO("Connect to file server:%s, port:%d successfully.", m_strFileServer.c_str(), m_nFilePort);
        m_bConnectedOnFileSocket = true;
        return true;
    }
    else if (ret == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
    {
        LOG_ERROR("Could not connect to file server:%s, port:%d.", m_strFileServer.c_str(), m_nFilePort);
        return false;
    }

    fd_set writeset;
    FD_ZERO(&writeset);
    FD_SET(m_hSocket, &writeset);
    struct timeval tv = { timeout, 0 };
    if (::select(m_hFileSocket + 1, NULL, &writeset, NULL, &tv) != 1)
    {
        LOG_ERROR("Could not connect to file server:%s, port:%d.", m_strFileServer.c_str(), m_nFilePort);
        return false;
    }

    LOG_INFO("Connect to file server:%s, port:%d successfully.", m_strFileServer.c_str(), m_nFilePort);

    m_bConnectedOnFileSocket = true;

    return true;
}

bool CIUSocket::ConnectToImgServer(int timeout/* = 3*/)
{
    if (!IsImgServerClosed())
        return true;

    m_hImgSocket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_hImgSocket == INVALID_SOCKET)
        return FALSE;

    long tmSend = 3 * 1000L;
    long tmRecv = 3 * 1000L;
    long noDelay = 1;
    setsockopt(m_hImgSocket, IPPROTO_TCP, TCP_NODELAY, (LPSTR)&noDelay, sizeof(long));
    setsockopt(m_hImgSocket, SOL_SOCKET, SO_SNDTIMEO, (LPSTR)&tmSend, sizeof(long));
    setsockopt(m_hImgSocket, SOL_SOCKET, SO_RCVTIMEO, (LPSTR)&tmRecv, sizeof(long));

    //将socket设置成非阻塞的
    unsigned long on = 1;
    if (::ioctlsocket(m_hImgSocket, FIONBIO, &on) == SOCKET_ERROR)
        return false;

    struct sockaddr_in addrSrv = { 0 };
    struct hostent* pHostent = NULL;
    unsigned int addr = 0;

    if ((addrSrv.sin_addr.s_addr = inet_addr(m_strImgServer.c_str())) == INADDR_NONE)
    {
        pHostent = ::gethostbyname(m_strImgServer.c_str());
        if (!pHostent)
        {
            LOG_ERROR("Could not connect to img server:%s, port:%d.", m_strImgServer.c_str(), m_nImgPort);
            return FALSE;
        }
        else
            addrSrv.sin_addr.s_addr = *((unsigned long*)pHostent->h_addr);
    }

    addrSrv.sin_family = AF_INET;
    addrSrv.sin_port = htons((u_short)m_nImgPort);
    int ret = ::connect(m_hImgSocket, (struct sockaddr*)&addrSrv, sizeof(addrSrv));
    if (ret == 0)
    {
        LOG_INFO("Connect to img server:%s, port:%d successfully.", m_strImgServer.c_str(), m_nImgPort);
        m_bConnectedOnImgSocket = true;
        return true;
    }
    else if (ret == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
    {
        LOG_ERROR("Could not connect to img server:%s, port:%d.", m_strImgServer.c_str(), m_nImgPort);
        return false;
    }

    fd_set writeset;
    FD_ZERO(&writeset);
    FD_SET(m_hSocket, &writeset);
    struct timeval tv = { timeout, 0 };
    if (::select(m_hImgSocket + 1, NULL, &writeset, NULL, &tv) != 1)
    {
        LOG_ERROR("Could not connect to img server:%s, port:%d.", m_strImgServer.c_str(), m_nImgPort);
        return false;
    }

    LOG_INFO("Connect to img server:%s, port:%d successfully.", m_strImgServer.c_str(), m_nImgPort);

    m_bConnectedOnImgSocket = true;

    return true;
}

int CIUSocket::CheckReceivedData()
{
    fd_set readset;
    FD_ZERO(&readset);
    FD_SET(m_hSocket, &readset);

    fd_set exceptionset;
    FD_ZERO(&exceptionset);
    FD_SET(m_hSocket, &exceptionset);

    timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 500;

    long nRet = ::select(m_hSocket + 1, &readset, NULL, &exceptionset, &timeout);
    if (nRet >= 1)
    {
        if (FD_ISSET(m_hSocket, &exceptionset))
            return -1;

        if (FD_ISSET(m_hSocket, &readset))
            return 1;
    }
    //出错
    else if (nRet == SOCKET_ERROR)
        return -1;

    //超时nRet=0，在超时的这段时间内没有数据
    return 0;
}

bool CIUSocket::Send()
{
    //如果未连接则重连，重连也失败则返回FALSE
    //TODO: 在发送数据的过程中重连没什么意义，因为与服务的Session已经无效了，换个地方重连
    if (IsClosed() && !Connect())
    {
        LOG_ERROR("connect server:%s:%d error.", m_strServer.c_str(), m_nPort);
        return false;
    }

    int nSentBytes = 0;
    int nRet = 0;
    while (true)
    {
        nRet = ::send(m_hSocket, m_strSendBuf.c_str(), m_strSendBuf.length(), 0);
        if (nRet == SOCKET_ERROR)
        {
            if (::WSAGetLastError() == WSAEWOULDBLOCK)
                break;
            else
            {
                LOG_ERROR("Send data error, disconnect server:%s, port:%d.", m_strServer.c_str(), m_nPort);
                Close();
                return false;
            }
        }
        else if (nRet < 1)
        {
            //一旦出现错误就立刻关闭Socket
            LOG_ERROR("Send data error, disconnect server:%s, port:%d.", m_strServer.c_str(), m_nPort);
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
    char buff[10 * 1024];
    while (true)
    {

        nRet = ::recv(m_hSocket, buff, 10 * 1024, 0);
        if (nRet == SOCKET_ERROR)				//一旦出现错误就立刻关闭Socket
        {
            if (::WSAGetLastError() == WSAEWOULDBLOCK)
                break;
            else
            {
                LOG_ERROR("Recv data error, errorNO=%d.", ::WSAGetLastError());
                //Close();
                return false;
            }
        }
        else if (nRet < 1)
        {
            LOG_ERROR("Recv data error, errorNO=%d.", ::WSAGetLastError());
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

bool CIUSocket::SendOnFilePort(const char* pBuffer, int64_t nSize, int nTimeout/* = 3*/)
{
    //如果未连接则重连，重连也失败则返回FALSE
    if (IsFileServerClosed())
        return false;
    
    int64_t nStartTime = time(NULL);
   
    int64_t nSentBytes = 0;
    int nRet = 0;
    int64_t now;
    do
    {
        //FIXME: 将int64_t强制转换成int32可能会有问题
        nRet = ::send(m_hFileSocket, pBuffer + nSentBytes, (int)(nSize - nSentBytes), 0);
        if (nRet == SOCKET_ERROR && ::WSAGetLastError() == WSAEWOULDBLOCK)
        {
            ::Sleep(1);
            now = (int64_t)time(NULL);
            if (now - nStartTime < (int64_t)nTimeout)
                continue;
            else
            {
                //超时了,关闭socket,并返回false
                CloseFileServerConnection();
                LOG_ERROR("Send data timeout, now: %lld, nStartTime: %lld, nTimeout: %d, disconnect file server:%s, port:%d.", now, nStartTime, nTimeout, m_strFileServer.c_str(), m_nFilePort);
                return false;
            }
        }
        else if (nRet < 1)
        {
            //一旦出现错误就立刻关闭Socket
            LOG_ERROR("Send data error, nRet: %d, disconnect file server: %s, port: %d, socket errorCode: %d", nRet, m_strFileServer.c_str(), m_nFilePort, ::WSAGetLastError());
            CloseFileServerConnection();
            return false;
        }

        nSentBytes += (int64_t)nRet;

        if (nSentBytes >= nSize)
            break;

        ::Sleep(1);

    } while (true);

    return true;
}

bool CIUSocket::RecvOnFilePort(char* pBuffer, int64_t nSize, int nTimeout/* = 3*/)
{
    if (IsFileServerClosed())
        return false;
    
    int64_t nStartTime = time(NULL);

    int nRet = 0;
    int64_t nRecvBytes = 0;
    int64_t now;

    do
    {
        nRet = ::recv(m_hFileSocket, pBuffer + nRecvBytes, (int)(nSize - nRecvBytes), 0);
        if (nRet == SOCKET_ERROR && ::WSAGetLastError() == WSAEWOULDBLOCK)
        {
            ::Sleep(1);
            now = time(NULL);
            if (now - nStartTime < (int64_t)nTimeout)
                continue;
            else
            {
                //超时了,关闭socket,并返回false
                CloseFileServerConnection();                
                LOG_ERROR("Recv data timeout, now: %lld, nStartTime: %lld, nTimeout: %d, disconnect file server:%s, port:%d.", now, nStartTime, nTimeout, m_strFileServer.c_str(), m_nFilePort);
                return false;
            }
        }
        //一旦出现错误就立刻关闭Socket
        else if (nRet < 1)
        {
            LOG_ERROR("Recv data error, nRet: %d, disconnect file server: %s, port: %d, socket errorCode: %d.", nRet, m_strFileServer.c_str(), m_nFilePort, ::WSAGetLastError());
            CloseFileServerConnection();
            return false;
        }

        nRecvBytes += (int64_t)nRet;
        if (nRecvBytes >= nSize)
            break;

        ::Sleep(1);

    } while (true);


    return true;
}

bool CIUSocket::SendOnImgPort(const char* pBuffer, int64_t nSize, int nTimeout/* = 3*/)
{
    //如果未连接则返回false
    if (IsImgServerClosed())
        return false;

    int64_t nStartTime = time(NULL);

    int64_t nSentBytes = 0;
    int nRet = 0;
    int64_t now;
    do
    {
        nRet = ::send(m_hImgSocket, pBuffer + nSentBytes, (int)(nSize - nSentBytes), 0);
        if (nRet == SOCKET_ERROR && ::WSAGetLastError() == WSAEWOULDBLOCK)
        {
            ::Sleep(1);
            now = time(NULL);
            if (now - nStartTime < (int64_t)nTimeout)
                continue;
            else
            {
                //超时了,关闭socket,并返回false
                CloseImgServerConnection();
                LOG_ERROR("Send data timeout, now: %lld, nStartTime: %lld, nTimeout: %d, disconnect img server: %s, port: %d.", now, nStartTime, nTimeout, m_strImgServer.c_str(), m_nImgPort);
                return false;
            }
        }
        else if (nRet < 1)
        {
            //一旦出现错误就立刻关闭Socket
            LOG_ERROR("Send data error, nRet:%d, disconnect img server:%s, port:%d, socket errorCode: %d.", nRet, m_strImgServer.c_str(), m_nImgPort, ::WSAGetLastError());
            CloseImgServerConnection();
            return false;
        }

        nSentBytes += (int64_t)nRet;

        if (nSentBytes >= nSize)
            break;

        ::Sleep(1);

    } while (true);

    return true;
}

bool CIUSocket::RecvOnImgPort(char* pBuffer, int64_t nSize, int nTimeout/* = 3*/)
{
    if (IsImgServerClosed())
        return false;

    int64_t nStartTime = time(NULL);

    int nRet = 0;
    int64_t nRecvBytes = 0;
    int64_t now;
    do
    {       
        //FIXME: 将int64_t强制转换成int32可能会有问题
        nRet = ::recv(m_hImgSocket, pBuffer + nRecvBytes, (int)(nSize - nRecvBytes), 0);
        if (nRet == SOCKET_ERROR && ::WSAGetLastError() == WSAEWOULDBLOCK)
        {
            ::Sleep(1);
            now = time(NULL);
            if (now - nStartTime < (int64_t)nTimeout)
                continue;
            else
            {
                //超时了,关闭socket,并返回false
                CloseImgServerConnection();
                LOG_ERROR("Recv data timeout, now: %lld, nStartTime: %lld, nTimeout: %d, disconnect img server: %s, port: %d.", now, nStartTime, nTimeout, m_strImgServer.c_str(), m_nImgPort);
                return false;
            }
        }
        //一旦出现错误就立刻关闭Socket
        else if (nRet < 1)
        {
            LOG_ERROR("Recv data error, nRet: %d, disconnect img server:%s, port:%d, socket errorCode: %d.", nRet, m_strImgServer.c_str(), m_nImgPort, ::WSAGetLastError());
            CloseImgServerConnection();
            return false;
        }

        nRecvBytes += (int64_t)nRet;
        if (nRecvBytes >= nSize)
            break;

        ::Sleep(1);

    } while (true);


    return true;
}

bool CIUSocket::IsClosed()
{
    return !m_bConnected;
}

bool CIUSocket::IsFileServerClosed()
{
    return !m_bConnectedOnFileSocket;
}

bool CIUSocket::IsImgServerClosed()
{
    return !m_bConnectedOnImgSocket;
}

void CIUSocket::Close()
{
    //FIXME: 这个函数会被数据发送线程和收取线程同时调用，不安全
    if (m_hSocket == INVALID_SOCKET)
        return;

    ::shutdown(m_hSocket, SD_BOTH);
    ::closesocket(m_hSocket);
    m_hSocket = INVALID_SOCKET;

    m_bConnected = false;
}

void CIUSocket::CloseFileServerConnection()
{
    if (m_hFileSocket == INVALID_SOCKET)
        return;

    ::shutdown(m_hFileSocket, SD_BOTH);
    ::closesocket(m_hFileSocket);
    m_hFileSocket = INVALID_SOCKET;

    LOG_ERROR("Disconnect file server:%s, port:%d.", m_strFileServer.c_str(), m_nFilePort);

    m_bConnectedOnFileSocket = false;
}

void CIUSocket::CloseImgServerConnection()
{
    if (m_hImgSocket == INVALID_SOCKET)
        return;

    ::shutdown(m_hImgSocket, SD_BOTH);
    ::closesocket(m_hImgSocket);
    m_hImgSocket = INVALID_SOCKET;

    LOG_ERROR("Disconnect img server:%s, port:%d.", m_strImgServer.c_str(), m_nImgPort);

    m_bConnectedOnImgSocket = false;
}

void CIUSocket::SendThreadProc()
{
    LOG_INFO("Recv data thread start...");

    while (!m_bStop)
    {
        std::unique_lock<std::mutex> guard(m_mtSendBuf);
        while (m_strSendBuf.empty())
        {
            if (m_bStop)
                return;

            m_cvSendBuf.wait(guard);
        }

        if (!Send())
        {
            //进行重连，如果连接不上，则向客户报告错误
        }
    }

    LOG_INFO("Recv data thread finish...");
}

void CIUSocket::Send(const std::string& strBuffer)
{
    //size_t nDestLength;
    std::string strDestBuf;
    if (!ZlibUtil::CompressBuf(strBuffer, strDestBuf))
    {
        LOG_ERROR("Compress error.");
        return;
    }

    //插入包头
    int32_t length = (int32_t)strBuffer.length();
    msg header;
    header.compressflag = 1;
    header.originsize = length;
    header.compresssize = (int32_t)strDestBuf.length();
	
	std::lock_guard<std::mutex> guard(m_mtSendBuf);
    m_strSendBuf.append((const char*)&header, sizeof(header));

    m_strSendBuf.append(strDestBuf);
    m_cvSendBuf.notify_one();
}

void CIUSocket::RecvThreadProc()
{
    LOG_INFO("Recv data thread start...");

    int nRet;
    //上网方式 
    DWORD   dwFlags;
    BOOL    bAlive;
    while (!m_bStop)
    {
        //检测到数据则收数据
        nRet = CheckReceivedData();
        //出错
        if (nRet == -1)
        {
            m_pRecvMsgThread->NotifyNetError();
        }
        //无数据
        else if (nRet == 0)
        {
            bAlive = ::IsNetworkAlive(&dwFlags);		//是否在线    
            if (!bAlive && ::GetLastError() == 0)
            {
                //网络已经断开
                m_pRecvMsgThread->NotifyNetError();
                LOG_ERROR("net error, exit recv and send thread...");
                Uninit();
                break;
            }

            long nLastDataTime = 0;
            {
                std::lock_guard<std::mutex> guard(m_mutexLastDataTime);
                nLastDataTime = m_nLastDataTime;
            }

            if (m_nHeartbeatInterval > 0)
            {
                if (time(NULL) - nLastDataTime >= m_nHeartbeatInterval)
                    SendHeartbeatPackage();
            }
        }
        //有数据
        else if (nRet == 1)
        {
            if (!Recv())
            {
                m_pRecvMsgThread->NotifyNetError();
                continue;
            }

            DecodePackages();
        }// end if
    }// end while-loop

    LOG_INFO("Recv data thread finish...");
}

bool CIUSocket::DecodePackages()
{
    //一定要放在一个循环里面解包，因为可能一片数据中有多个包，
    //对于数据收不全，这个地方我纠结了好久T_T
    while (true)
    {
        //接收缓冲区不够一个包头大小
        if (m_strRecvBuf.length() <= sizeof(msg))
            break;

        msg header;
        memcpy_s(&header, sizeof(msg), m_strRecvBuf.data(), sizeof(msg));
        //数据压缩过
        if (header.compressflag == PACKAGE_COMPRESSED)
        {
            //防止包头定义的数据是一些错乱的数据，这里最大限制每个包大小为10M
            if (header.compresssize >= MAX_PACKAGE_SIZE || header.compresssize <= 0 ||
                header.originsize >= MAX_PACKAGE_SIZE || header.originsize <= 0)
            {
                LOG_ERROR("Recv a illegal package, compresssize: %d, originsize=%d.", header.compresssize, header.originsize);
                m_strRecvBuf.clear();
                return false;
            }

            //接收缓冲区不够一个整包大小（包头+包体）
            if (m_strRecvBuf.length() < sizeof(msg) + header.compresssize)
                break;

            //去除包头信息
            m_strRecvBuf.erase(0, sizeof(msg));
            //拿到包体
            std::string strBody;
            strBody.append(m_strRecvBuf.c_str(), header.compresssize);
            //去除包体信息
            m_strRecvBuf.erase(0, header.compresssize);

            //解压
            std::string strUncompressBody;
            if (!ZlibUtil::UncompressBuf(strBody, strUncompressBody, header.originsize))
            {
                LOG_ERROR("uncompress buf error, compresssize: %d, originsize: %d", header.compresssize, header.originsize);
                m_strRecvBuf.clear();
                return false;
            }

            m_pRecvMsgThread->AddMsgData(strUncompressBody);
        }
        //数据未压缩过
        else
        {
            //防止包头定义的数据是一些错乱的数据，这里最大限制每个包大小为10M
            if (header.originsize >= MAX_PACKAGE_SIZE || header.originsize <= 0)
            {
                LOG_ERROR("Recv a illegal package, originsize=%d.", header.originsize);
                m_strRecvBuf.clear();
                return false;
            }

            //接收缓冲区不够一个整包大小（包头+包体）
            if (m_strRecvBuf.length() < sizeof(msg) + header.originsize)
                break;

            //去除包头信息
            m_strRecvBuf.erase(0, sizeof(msg));
            //拿到包体
            std::string strBody;
            strBody.append(m_strRecvBuf.c_str(), header.originsize);
            //去除包体信息
            m_strRecvBuf.erase(0, header.originsize);
            m_pRecvMsgThread->AddMsgData(strBody);
        }
    }// end while

    return true;
}

bool CIUSocket::Register(const char* pszUser, const char* pszNickname, const char* pszPassword, int nTimeout, std::string& strReturnData)
{
    if (!Connect())
        return false;

    char szRegisterInfo[256] = { 0 };
    sprintf_s(szRegisterInfo,
        256,
        "{\"username\": \"%s\", \"nickname\": \"%s\", \"password\": \"%s\"}",
        pszUser,
        pszNickname,
        pszPassword);

    std::string outbuf;
    net::BinaryStreamWriter writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_register);
    writeStream.WriteInt32(0);
    std::string data = szRegisterInfo;
    writeStream.WriteString(data);
    writeStream.Flush();

    LOG_INFO("Request register: Account=%s, Password=*****, nickname=%s.", pszUser, pszNickname, pszPassword);

    //int32_t length = (int32_t)outbuf.length();

    std::string strDestBuf;
    if (!ZlibUtil::CompressBuf(outbuf, strDestBuf))
    {
        LOG_ERROR("compress error");
        return false;
    }
    msg header;
    header.compressflag = 1;
    header.originsize = outbuf.length();
    header.compresssize = strDestBuf.length();
    std::string strSendBuf;
    strSendBuf.append((const char*)&header, sizeof(header));
    strSendBuf.append(strDestBuf);

    //超时时间设置为3秒
    if (!SendData(strSendBuf.c_str(), strSendBuf.length(), nTimeout))
        return false;

    memset(&header, 0, sizeof(header));
    if (!RecvData((char*)&header, sizeof(header), nTimeout))
        return false;

    std::string strData;
    if (header.compressflag == PACKAGE_COMPRESSED)
    {
        if (header.compresssize >= MAX_PACKAGE_SIZE || header.compresssize <= 0 ||
            header.originsize >= MAX_PACKAGE_SIZE || header.originsize <= 0)
        {
            Close();
            LOG_ERROR("Recv a illegal package, compresssize: %d, originsize=%d.", header.compresssize, header.originsize);
            return false;
        }

        CMiniBuffer minBuff(header.compresssize);
        if (!RecvData(minBuff, header.compresssize, nTimeout))
        {
            return false;
        }

        std::string strOrigin(minBuff, header.compresssize);

        std::string strUncompressBuf;
        if (!ZlibUtil::UncompressBuf(strOrigin, strUncompressBuf, header.originsize))
        {
            Close();
            LOG_ERROR("uncompress error");
            return false;
        }

        strData = strUncompressBuf;
    }
    else
    {
        if (header.originsize >= MAX_PACKAGE_SIZE || header.originsize <= 0)
        {
            Close();
            LOG_ERROR("Recv a illegal package, originsize=%d.", header.originsize);
            return false;
        }

        CMiniBuffer minBuff(header.originsize);
        if (!RecvData(minBuff, header.originsize, nTimeout))
        {
            return false;
        }
        strData.append(minBuff, header.originsize);
    }

    net::BinaryStreamReader readStream(strData.c_str(), strData.length());
    int32_t cmd;
    if (!readStream.ReadInt32(cmd))
        return false;

    int32_t seq;
    if (!readStream.ReadInt32(seq))
        return false;

    size_t datalength;
    if (!readStream.ReadString(&strReturnData, 0, datalength))
    {
        return false;
    }

    return true;
}

bool CIUSocket::Login(const char* pszUser, const char* pszPassword, int nClientType, int nOnlineStatus, int nTimeout, std::string& strReturnData)
{
    if (!Connect())
        return false;

    char szLoginInfo[256] = { 0 };
    sprintf_s(szLoginInfo,
        ARRAYSIZE(szLoginInfo),
        "{\"username\": \"%s\", \"password\": \"%s\", \"clienttype\": %d, \"status\": %d}",
        pszUser,
        pszPassword,
        nClientType,
        nOnlineStatus);

    std::string outbuf;
    net::BinaryStreamWriter writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_login);
    writeStream.WriteInt32(0);
    //std::string data = szLoginInfo;
    writeStream.WriteCString(szLoginInfo, strlen(szLoginInfo));
    writeStream.Flush();

    LOG_INFO("Request logon: Account=%s, Password=*****, Status=%d, LoginType=%d.", pszUser, pszPassword, nOnlineStatus, nClientType);

    std::string strDestBuf;
    if (!ZlibUtil::CompressBuf(outbuf, strDestBuf))
    {
        LOG_ERROR("compress error");
        return false;
    }
    msg header;
    memset(&header, 0, sizeof(header));
    header.compressflag = 1;
    header.originsize = outbuf.length();
    header.compresssize = strDestBuf.length();

    //std::string strX;
    //if (!::UncompressBuf(strDestBuf, strX, header.originsize))
    //{
    //    int x = 0;
    //}

    std::string strSendBuf;
    strSendBuf.append((const char*)&header, sizeof(header));
    strSendBuf.append(strDestBuf);

    //超时时间设置为3秒
    if (!SendData(strSendBuf.c_str(), strSendBuf.length(), nTimeout))
        return false;

    memset(&header, 0, sizeof(header));
    if (!RecvData((char*)&header, sizeof(header), nTimeout))
        return false;

    std::string strData;
    if (header.compressflag == PACKAGE_COMPRESSED)
    {
        if (header.compresssize >= MAX_PACKAGE_SIZE || header.compresssize <= 0 ||
            header.originsize >= MAX_PACKAGE_SIZE || header.originsize <= 0)
        {
            Close();
            LOG_ERROR("Recv a illegal package, compresssize: %d, originsize=%d.", header.compresssize, header.originsize);
            return false;
        }

        CMiniBuffer minBuff(header.compresssize);
        if (!RecvData(minBuff, header.compresssize, nTimeout))
        {
            return false;
        }

        std::string strOrigin(minBuff, header.compresssize);

        std::string strUncompressBuf;
        if (!ZlibUtil::UncompressBuf(strOrigin, strUncompressBuf, header.originsize))
        {
            Close();
            LOG_ERROR("uncompress error");
            return false;
        }

        strData = strUncompressBuf;
    }
    else
    {
        if (header.originsize >= MAX_PACKAGE_SIZE || header.originsize <= 0)
        {
            Close();
            LOG_ERROR("Recv a illegal package, originsize=%d.", header.originsize);
            return false;
        }

        CMiniBuffer minBuff(header.originsize);
        if (!RecvData(minBuff, header.originsize, nTimeout))
        {
            return false;
        }
        strData.append(minBuff, header.originsize);
    }

    net::BinaryStreamReader readStream(strData.c_str(), strData.length());
    int32_t cmd;
    if (!readStream.ReadInt32(cmd))
        return false;

    int32_t seq;
    if (!readStream.ReadInt32(seq))
        return false;

    size_t datalength;
    if (!readStream.ReadString(&strReturnData, 0, datalength))
    {
        return false;
    }

    return true;
}

bool CIUSocket::SendData(const char* pBuffer, int nBuffSize, int nTimeout)
{
    //TODO：这个地方可以先加个select判断下socket是否可写

    int64_t nStartTime = time(NULL);

    int nSentBytes = 0;
    int nRet = 0;
    while (true)
    {
        nRet = ::send(m_hSocket, pBuffer, nBuffSize, 0);
        if (nRet == SOCKET_ERROR)
        {
            //对方tcp窗口太小暂时发布出去，同时没有超时，则继续等待
            if (::WSAGetLastError() == WSAEWOULDBLOCK && time(NULL) - nStartTime < nTimeout)
            {
                continue;
            }
            else
                return false;
        }
        else if (nRet < 1)
        {
            //一旦出现错误就立刻关闭Socket
            LOG_ERROR("Send data error, disconnect server:%s, port:%d.", m_strServer.c_str(), m_nPort);
            Close();
            return false;
        }

        nSentBytes += nRet;
        if (nSentBytes >= nBuffSize)
            break;

        pBuffer += nRet;
        nBuffSize -= nRet;

        ::Sleep(1);
    }

    return true;
}

bool CIUSocket::RecvData(char* pszBuff, int nBufferSize, int nTimeout)
{
    int64_t nStartTime = time(NULL);

    fd_set writeset;
    FD_ZERO(&writeset);
    FD_SET(m_hSocket, &writeset);

    timeval timeout;
    timeout.tv_sec = nTimeout;
    timeout.tv_usec = 0;

    int nRet = ::select(m_hSocket + 1, NULL, &writeset, NULL, &timeout);
    if (nRet != 1)
    {
        Close();
        return false;
    }

    int nRecvBytes = 0;
    int nBytesToRecv = nBufferSize;
    while (true)
    {
        nRet = ::recv(m_hSocket, pszBuff, nBytesToRecv, 0);
        if (nRet == SOCKET_ERROR)				//一旦出现错误就立刻关闭Socket
        {
            if (::WSAGetLastError() == WSAEWOULDBLOCK && time(NULL) - nStartTime < nTimeout)
                continue;
            else
            {
                LOG_ERROR("Recv data error, disconnect server:%s, port:%d.", m_strServer.c_str(), m_nPort);
                Close();
                return false;
            }
        }
        else if (nRet < 1)
        {
            LOG_ERROR("Recv data error, disconnect server:%s, port:%d.", m_strServer.c_str(), m_nPort);
            Close();
            return false;
        }

        nRecvBytes += nRet;
        if (nRecvBytes >= nBufferSize)
            break;

        pszBuff += nRet;
        nBytesToRecv -= nRet;

        ::Sleep(1);
    }

    return true;
}

void CIUSocket::SendHeartbeatPackage()
{
    std::string outbuf;
    net::BinaryStreamWriter writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_heartbeat);
    writeStream.WriteInt32(m_nHeartbeatSeq);
    std::string dummy;
    writeStream.WriteString(dummy);
    writeStream.Flush();

    ++m_nHeartbeatSeq;

    Send(outbuf);
}
