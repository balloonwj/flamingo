#include "stdafx.h"
#include "IUSocket.h"

#include "../Common/Util/IniFile.h"
#include "../Session/UserSessionData.h"
#include "IUProtocolData.h"
#include "../Common/Util/EncodeUtil.h"
#include "../Common/Log/IULog.h"
#include "../Common/Util/ZlibUtil.h"
#include "FrameCodec.h"             // v2 帧编解码（内含 net/Protocol.h）
#include "SocketRecvDrain.h"        // 主连接收包循环 net::DrainSocketRecv
#include "FramePayload.h"           // 包体的压缩与加密管线
#include "HandshakeRunner.h"         // 三帧握手的共用流程（连到三个服务的各条链路复用同一份）
#include "TcpConnectWait.h"          // 带超时的非阻塞 connect（主连接、文件连接、图片连接共用）
#include "Sha256.h"                 // 口令 SHA-256 哈希
#include "LoginProtocolClient.h"    // v2 登录报文编解码
#include "RegisterProtocolClient.h" // v2 注册报文编解码
#include "MessageProtocolClient.h"  // v2 单聊消息报文编解码
#include "CardProtocolClient.h"     // v2 群互动卡片（接龙）报文编解码
#include "ContactsProtocolClient.h" // v2 通讯录报文编解码
#include "ConversationsProtocolClient.h" // v2 会话列表报文编解码
#include "GroupProtocolClient.h"         // v2 群组报文编解码（建群）
#include "GroupQaProtocolClient.h"       // 群问答报文编解码（1336~1339）
#include "BotProtocolClient.h"           // 机器人报文编解码（2801、2810、2811、2820）与交给 FlamingoBot.dll 的帧范围
#include "GroupLinkProtocolClient.h"     // 加群链接报文编解码（1340~1344）
#include "MediaSignalPolicy.h"           // FlamingoMedia.dll 可经主连接收发的信令白名单
#include "FolderProtocolClient.h"        // v2 单聊文件夹传输信令编解码
#include "GroupFileProtocolClient.h"     // v2 群文件库报文编解码（列表/变更推送）
#include "GlobalSearchProtocolClient.h"  // v2 全局搜索报文编解码（cmd_global_search）
#include "AccountProtocolClient.h"       // v2 账号报文编解码（修改密码）
#include "ProfileProtocolClient.h"       // v2 账号资料报文编解码（改签名、改资料）
#include "FavoriteProtocolClient.h"      // v2「我的收藏」报文编解码（cmd_set_favorite）
#include "FriendProtocolClient.h"        // v2 好友管理报文编解码（cmd_set_friend_remark 设置备注名）
#include "ContactTagProtocolClient.h"    // v2「联系人标签」报文编解码（cmd_set_contact_tags）
#include "AnnounceProtocolClient.h"      // v2 平台公告报文编解码（公告中心：2101/2102/2103）
#include "MemoProtocolClient.h"          // 备忘与提醒报文编解码（2401~2404）
#include "FileProtocolClient.h"          // v2 文件/图片信令报文编解码（申请上传/下载）
#include "ImageTransfer.h"               // 聊天图片传输管理器（发图工作线程）
#include "FileTransfer.h"                // 聊天文件传输管理器（收发文件工作线程）
#include <tchar.h>
#include <Sensapi.h>
#include <functional>
#include <objbase.h>                // CoCreateGuid

#pragma comment(lib, "Sensapi.lib")
#pragma comment(lib, "ole32.lib")   // CoCreateGuid

#include "../ClientVersion.h"
#include "../Feature/FileShare/FileShareHost.h"   // 2200 段帧转交 FlamingoFileShare.dll
#include "../Feature/FileShare/ShareChannel.h"    // 代 DLL 发送共享信令的放行规则
#include "../Feature/P2P/P2PHost.h"              // 2300 段帧转交 FlamingoP2P.dll
#include "../Feature/P2P/P2PChannel.h"           // 代 DLL 发送 P2P 信令的放行规则
#include "../Feature/SecretChat/SecretChatHost.h" // 2600 段帧转交 FlamingoSecretChat.dll
#include "../Feature/SecretChat/SecretChatGate.h" // 代 DLL 发送密聊信令的放行规则
#include "../Feature/ThreadGroup/ThreadGroupHost.h" // 2700 段帧转交 FlamingoThreadGroup.dll
#include "../Feature/ThreadGroup/ThreadGroupGate.h" // 代 DLL 发送帖子群信令的放行规则
#include "UpdateProtocolClient.h"
#include "../Feature/Update/UpdateManager.h"

//v2 客户端版本号，登录请求 cmd_login 与检查更新 cmd_check_update 都携带它。
//取自 ClientVersion.h 这一个出处 —— 此前登录上报的、关于页显示的、exe 属性里的
//三个版本号各写各的，自动更新下会直接表现为“升级完还提示有新版本”。
static const char* const kClientVersionV2 = FLAMINGO_CLIENT_VERSION;

//读 flamingo.ini 的 serverpubkey 时用的缓冲区长度（字符数）。32 字节公钥的标准
//base64 恒为 44 个字符（含一个 '=' 填充），加结束符 45 个就够；取 64 是留出余量，
//让手工在值末尾多敲了空格之类的情形也能完整读进来、再由解析函数判错，
//而不是被截断成一个看起来合法的短串。
static const int SERVER_PUBKEY_TEXT_BUF_LEN = 64;

//协议加密握手中每一次收发的超时时间，单位秒。握手就在 Connect 里同步做，超时太长
//会让"服务端在监听但不应答"这种情形把整个登录流程卡住；三帧握手只是一来一回，
//正常在毫秒级完成，给 5 秒已经很宽裕。
static const int HANDSHAKE_TIMEOUT_SEC = 5;

/**
 *  主连接的握手收发适配器：把 net::performClientHandshake 需要的字节收发
 *  转接到 CIUSocket 自己的 SendData / RecvData 上。
 *
 *  只在 Connect() 里握手那一小段时间内活着，是个栈上对象，故不必考虑并发。
 */
class CIUSocket::HandshakeChannel : public net::IHandshakeChannel
{
public:
    /**
     *  @param owner    宿主连接，生命周期覆盖本对象（本对象是各 PerformXxxHandshake 的栈上局部量）
     *  @param kind     本次握手走哪条链路的收发函数
     *  @param nTimeout 每次收发的超时时间，单位秒
     */
    HandshakeChannel(CIUSocket* owner, handshake_link_kind kind, int nTimeout)
        : m_owner(owner), m_kind(kind), m_nTimeout(nTimeout)
    {
    }

    virtual bool SendAll(const char* data, size_t len)
    {
        switch (m_kind)
        {
        //文件传输共享连接
        case handshake_link_file:
            return m_owner->SendOnFilePort(data, (int64_t)len, m_nTimeout);

        //图片传输共享连接
        case handshake_link_img:
            return m_owner->SendOnImgPort(data, (int64_t)len, m_nTimeout);

        //主连接。放在 default 而不是单列一个 case，是为了让新增链路时忘记补分支
        //的后果停留在编译告警级别之外——真要新增链路，两个 switch 都得改。
        default:
            return m_owner->SendData(data, (int)len, m_nTimeout);
        }
    }

    virtual bool RecvAll(char* buf, size_t len)
    {
        switch (m_kind)
        {
        //文件传输共享连接
        case handshake_link_file:
            return m_owner->RecvOnFilePort(buf, (int64_t)len, m_nTimeout);

        //图片传输共享连接
        case handshake_link_img:
            return m_owner->RecvOnImgPort(buf, (int64_t)len, m_nTimeout);

        //主连接，理由同 SendAll
        default:
            return m_owner->RecvData(buf, (int)len, m_nTimeout);
        }
    }

private:
    CIUSocket*          m_owner;     //宿主连接，不拥有
    handshake_link_kind m_kind;      //本次握手走哪条链路
    int                 m_nTimeout;  //每次收发的超时，单位秒
};

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

    //公钥在 LoadConfig 里读入；在那之前一律按"未配置"处理，即走明文
    memset(m_serverPubKey, 0, sizeof(m_serverPubKey));
    m_bHasServerPubKey = false;

    m_nRecordClientType = 1;
    m_nRecordOnlineStatus = 1;

    //进程启动时不处于"用户主动离线"态；由 CFlamingoClient::GoOffline / Login 维护。
    m_bUserOffline = false;

    m_seq = 0;

    m_hProxyWnd = NULL;
    m_bNetError = false;
}

CIUSocket::~CIUSocket()
{

}

CIUSocket& CIUSocket::GetInstance()
{
    static CIUSocket socketInstance;
    return socketInstance;
}

void CIUSocket::SetUIProxyWnd(HWND hWnd)
{
    m_hProxyWnd = hWnd;
}

void CIUSocket::NotifyNetError()
{
    //一次断线只上报一次：收发两个方向可能同时发现断线，重复投递会让主窗口
    //重复起自动重连。标志由每次登录成功后的 ResetNetError 复位。
    if (m_bNetError)
    {
        return;
    }
    m_bNetError = true;
    if (::IsWindow(m_hProxyWnd))
    {
        ::PostMessage(m_hProxyWnd, FMG_MSG_NET_ERROR, 0, 0);
    }
}

void CIUSocket::ResetNetError()
{
    m_bNetError = false;
}

bool CIUSocket::Init()
{
    m_bStop = false;

    if (!m_spSendThread)
        m_spSendThread.reset(new std::thread(std::bind(&CIUSocket::SendThreadProc, this)));

    if (!m_spRecvThread)
        m_spRecvThread.reset(new std::thread(std::bind(&CIUSocket::RecvThreadProc, this)));

    //启动聊天图片 / 文件传输工作线程（字节流收发在各自线程串行完成，不占主连接）
    net::ImageTransfer::GetInstance().Start();
    net::FileTransfer::GetInstance().Start();
    //启动自动更新工作线程（检查更新与下载升级包串行跑在它上）
    update::UpdateManager::GetInstance().Init();

    return true;
}

void CIUSocket::Uninit()
{
    //先停发图 / 文件工作线程（其内部会关掉对应连接以唤醒可能阻塞的字节流收发）
    net::ImageTransfer::GetInstance().Stop();
    net::FileTransfer::GetInstance().Stop();
    update::UpdateManager::GetInstance().Uninit();

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
    iniFile.ReadString(_T("server"), _T("server"), _T("127.0.0.1"), szServer, 64, strIniFilePath);
    m_strServer = EncodeUtil::UnicodeToAnsi(std::wstring(szServer));

    TCHAR szFileServer[64] = { 0 };
    iniFile.ReadString(_T("server"), _T("fileserver"), _T("127.0.0.1"), szFileServer, 64, strIniFilePath);
    m_strFileServer = EncodeUtil::UnicodeToAnsi(std::wstring(szFileServer));

    TCHAR szImgServer[64] = { 0 };
    iniFile.ReadString(_T("server"), _T("imgserver"), _T("127.0.0.1"), szImgServer, 64, strIniFilePath);
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

    //公钥是 32 字节的标准 base64，恒为 44 个字符（含一个填充符）。缓冲区给到
    //SERVER_PUBKEY_TEXT_BUF_LEN，容得下这 44 个字符加结束符，还留了余量。
    TCHAR szPubKey[SERVER_PUBKEY_TEXT_BUF_LEN] = { 0 };
    iniFile.ReadString(_T("server"), _T("serverpubkey"), _T(""),
                       szPubKey, SERVER_PUBKEY_TEXT_BUF_LEN, strIniFilePath);
    ApplyServerPublicKey(EncodeUtil::UnicodeToAnsi(std::wstring(szPubKey)));
}

void CIUSocket::ApplyServerPublicKey(const std::string& pubKeyText)
{
    memset(m_serverPubKey, 0, sizeof(m_serverPubKey));
    m_bHasServerPubKey = false;

    if (pubKeyText.empty())
    {
        //没配公钥就是不加密。客户端没有独立的加密开关，"配了公钥"本身就是"要加密"，
        //这样与服务端 .conf 的 encryption 天然配成一对，不会出现两边开关配反、
        //却要同时看两份配置才能判断原因的情况。
        LOG_INFO("Protocol encryption is off: serverpubkey is not configured in flamingo.ini.");
        return;
    }

    if (!net::parseServerPublicKey(pubKeyText, m_serverPubKey))
    {
        //配了但解析不出来（写错、被截断、混入空格），必须当作错误。这里若退回明文，
        //使用者会以为链路已加密、实则裸奔，比直接连不上危险得多。
        memset(m_serverPubKey, 0, sizeof(m_serverPubKey));
        LOG_ERROR("Invalid serverpubkey in flamingo.ini: expect base64 of %u bytes. "
                  "All connections will fail until it is fixed.",
                  (unsigned)net::IDENTITY_PUBLICKEY_BYTES);
        return;
    }

    m_bHasServerPubKey = true;
    //只打指纹不打完整公钥：公钥本身可以公开，但统一口径能避免有人照抄这段代码去打私钥
    LOG_INFO("Protocol encryption is on, server key fingerprint: %s.",
             net::publicKeyFingerprint(m_serverPubKey).c_str());
}

const unsigned char* CIUSocket::GetServerPublicKey() const
{
    return m_bHasServerPubKey ? m_serverPubKey : NULL;
}

net::FrameCipher* CIUSocket::GetFileCipher()
{
    return &m_fileHandshake.cipher();
}

net::FrameCipher* CIUSocket::GetImgCipher()
{
    return &m_imgHandshake.cipher();
}

bool CIUSocket::PerformHandshake(int nTimeout)
{
    const unsigned char* pServerPubKey = GetServerPublicKey();
    if (pServerPubKey == NULL)
    {
        //没配服务端公钥就是不加密，本连接全程明文，一个字节都不必收发
        return true;
    }

    //握手流程本身在 net::performClientHandshake 里，各条链路共用同一份；
    //本类只需提供一个把自己的收发函数包起来的适配器。
    HandshakeChannel channel(this, handshake_link_main, nTimeout);

    std::string err;
    if (!net::performClientHandshake(channel, pServerPubKey, m_handshake, err))
    {
        LOG_ERROR("Handshake failed on main connection: %s. "
                  "Server key fingerprint configured here is %s.",
                  err.c_str(), net::publicKeyFingerprint(pServerPubKey).c_str());
        return false;
    }

    LOG_INFO("Handshake done on main connection, server key fingerprint: %s.",
             net::publicKeyFingerprint(pServerPubKey).c_str());
    return true;
}

bool CIUSocket::PerformFileHandshake(int nTimeout)
{
    const unsigned char* pServerPubKey = GetServerPublicKey();
    if (pServerPubKey == NULL)
    {
        //没配服务端公钥就是不加密，本连接全程明文
        return true;
    }

    HandshakeChannel channel(this, handshake_link_file, nTimeout);

    std::string err;
    if (!net::performClientHandshake(channel, pServerPubKey, m_fileHandshake, err))
    {
        LOG_ERROR("Handshake failed on file connection: %s.", err.c_str());
        return false;
    }

    LOG_INFO("Handshake done on file connection.");
    return true;
}

bool CIUSocket::PerformImgHandshake(int nTimeout)
{
    const unsigned char* pServerPubKey = GetServerPublicKey();
    if (pServerPubKey == NULL)
    {
        //没配服务端公钥就是不加密，本连接全程明文
        return true;
    }

    HandshakeChannel channel(this, handshake_link_img, nTimeout);

    std::string err;
    if (!net::performClientHandshake(channel, pServerPubKey, m_imgHandshake, err))
    {
        LOG_ERROR("Handshake failed on image connection: %s.", err.c_str());
        return false;
    }

    LOG_INFO("Handshake done on image connection.");
    return true;
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
        } else
            addrSrv.sin_addr.s_addr = *((unsigned long*)pHostent->h_addr);
    }

    addrSrv.sin_family = AF_INET;
    addrSrv.sin_port = htons((u_short)m_nPort);
    //被拒绝、不可达时立即返回，失败原因与超时分开写进日志（见 TcpConnectWait.h）
    const int timeoutMs = timeout * net::kTcpConnectMsPerSecond;
    int connectError = 0;
    const net::tcp_connect_result connectResult =
        net::ConnectWithTimeout(m_hSocket, addrSrv, timeoutMs, connectError);
    if (connectResult != net::tcp_connect_ok)
    {
        LOG_ERROR("Could not connect to server:%s, port:%d, %s.", m_strServer.c_str(), m_nPort,
                  net::DescribeTcpConnect(connectResult, connectError, timeoutMs).c_str());
        return false;
    }

    LOG_INFO("Connect to server:%s, port:%d successfully.", m_strServer.c_str(), m_nPort);

    //握手必须在 m_bConnected 置真之前做完，理由见 PerformHandshake 的注释
    if (!PerformHandshake(HANDSHAKE_TIMEOUT_SEC))
    {
        Close();
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

bool CIUSocket::TestConnect(PCTSTR lpszHost, short nPort, int nTimeoutSec)
{
    //地址转 ANSI（与 m_strServer 同约定）；gethostbyname / inet_addr 为 ANSI 接口
    std::string strHost =
        EncodeUtil::UnicodeToAnsi(std::wstring(lpszHost != NULL ? lpszHost : _T("")));
    if (strHost.empty())
        return false;

    //临时 socket，独立于聊天主连接 m_hSocket，探测完即关闭
    SOCKET hTest = ::socket(AF_INET, SOCK_STREAM, 0);
    if (hTest == INVALID_SOCKET)
        return false;

    //设为非阻塞，以便控制连接超时
    unsigned long on = 1;
    if (::ioctlsocket(hTest, FIONBIO, &on) == SOCKET_ERROR)
    {
        ::closesocket(hTest);
        return false;
    }

    struct sockaddr_in addrSrv = { 0 };
    addrSrv.sin_family = AF_INET;
    addrSrv.sin_port = htons((u_short)nPort);
    addrSrv.sin_addr.s_addr = inet_addr(strHost.c_str());
    if (addrSrv.sin_addr.s_addr == INADDR_NONE)
    {
        //不是点分 IP，按域名解析
        struct hostent* pHostent = ::gethostbyname(strHost.c_str());
        if (pHostent == NULL)
        {
            ::closesocket(hTest);
            return false;
        }
        addrSrv.sin_addr.s_addr = *((unsigned long*)pHostent->h_addr);
    }

    //与主连接共用同一份带超时的 connect（见 TcpConnectWait.h）：被拒绝、不可达时立即返回
    int connectError = 0;
    const bool bConnected = net::ConnectWithTimeout(hTest, addrSrv, nTimeoutSec * net::kTcpConnectMsPerSecond,
                                                    connectError) == net::tcp_connect_ok;
    ::closesocket(hTest);
    return bConnected;
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
        } else
            addrSrv.sin_addr.s_addr = *((unsigned long*)pHostent->h_addr);
    }

    addrSrv.sin_family = AF_INET;
    addrSrv.sin_port = htons((u_short)m_nFilePort);
    //被拒绝、不可达时立即返回，失败原因与超时分开写进日志（见 TcpConnectWait.h）
    const int timeoutMs = timeout * net::kTcpConnectMsPerSecond;
    int connectError = 0;
    const net::tcp_connect_result connectResult =
        net::ConnectWithTimeout(m_hFileSocket, addrSrv, timeoutMs, connectError);
    if (connectResult != net::tcp_connect_ok)
    {
        LOG_ERROR("Could not connect to file server:%s, port:%d, %s.", m_strFileServer.c_str(), m_nFilePort,
                  net::DescribeTcpConnect(connectResult, connectError, timeoutMs).c_str());
        return false;
    }

    LOG_INFO("Connect to file server:%s, port:%d successfully.", m_strFileServer.c_str(), m_nFilePort);

    m_bConnectedOnFileSocket = true;

    //握手放在标志置真<u>之后</u>：SendOnFilePort / RecvOnFilePort 一进来就判
    //IsFileServerClosed，标志没置真的话它们会直接拒绝收发。这与主连接的顺序相反，
    //但没有风险——文件链路是纯同步收发，没有独立收取线程会来抢这条 socket 的数据。
    if (!PerformFileHandshake(timeout))
    {
        CloseFileServerConnection();
        return false;
    }

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
        } else
            addrSrv.sin_addr.s_addr = *((unsigned long*)pHostent->h_addr);
    }

    addrSrv.sin_family = AF_INET;
    addrSrv.sin_port = htons((u_short)m_nImgPort);
    //被拒绝、不可达时立即返回，失败原因与超时分开写进日志（见 TcpConnectWait.h）
    const int timeoutMs = timeout * net::kTcpConnectMsPerSecond;
    int connectError = 0;
    const net::tcp_connect_result connectResult =
        net::ConnectWithTimeout(m_hImgSocket, addrSrv, timeoutMs, connectError);
    if (connectResult != net::tcp_connect_ok)
    {
        LOG_ERROR("Could not connect to img server:%s, port:%d, %s.", m_strImgServer.c_str(), m_nImgPort,
                  net::DescribeTcpConnect(connectResult, connectError, timeoutMs).c_str());
        return false;
    }

    LOG_INFO("Connect to img server:%s, port:%d successfully.", m_strImgServer.c_str(), m_nImgPort);

    m_bConnectedOnImgSocket = true;

    //握手放在标志置真之后，理由同 ConnectToFileServer：SendOnImgPort /
    //RecvOnImgPort 一进来就判 IsImgServerClosed，标志没置真就会直接拒绝收发。
    if (!PerformImgHandshake(timeout))
    {
        CloseImgServerConnection();
        return false;
    }

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

    //Windows 下 select 的第一个参数 nfds 并不参与运算，仅为兼容 BSD 接口而保留；
    //SOCKET 在 64 位下是 64 位无符号句柄，这里收窄为 int 不影响 select 的行为
    long nRet = ::select(static_cast<int>(m_hSocket + 1), &readset, NULL, &exceptionset, &timeout);
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
    //用户主动离线：绝不在这里把连接重新建起来。这一句原本是"只要发送缓冲里有数据、
    //连接又断着，就自己 Connect 回去"，它绕开了上层的登录态判断，是一条隐藏的重连
    //路径 —— 用户明确选了离线，就不该被网络层自作主张地连回服务器。
    if (IsUserOffline())
    {
        //顺手丢掉排队中的数据：既然不再连服务器，这些帧发不出去，留着只会让发送线程
        //在"缓冲非空 → Send 失败 → 再试"之间空转烧 CPU。本函数由 SendThreadProc 在
        //持有 m_mtSendBuf 的情况下调用，故此处直接改缓冲是安全的（下面的 erase 同理）。
        m_strSendBuf.clear();
        LOG_INFO("Send skipped: user is offline, pending data discarded.");
        return false;
    }

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
        //send 的长度形参是 int。发送缓冲中排队的是协议帧，单帧长度受包头 4 字节长度字段
        //的约束，排队总量在实际运行中远小于 int 上限，故此处按 int 收窄是安全的
        nRet = ::send(m_hSocket, m_strSendBuf.c_str(), static_cast<int>(m_strSendBuf.length()), 0);
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
        } else if (nRet < 1)
        {
            //一旦出现错误就立刻关闭Socket
            LOG_ERROR("Send data error, disconnect server:%s, port:%d.", m_strServer.c_str(), m_nPort);
            Close();
            return false;
        }

        m_strSendBuf.erase(0, nRet);
        if (m_strSendBuf.empty())
            break;

        //只发出去一部分时立即再发，不等待。旧实现在此处 Sleep(1)，实际要睡约 16 毫秒
        //（见 net::DrainSocketRecv 内的说明），大请求因此被拖慢。系统发送缓冲写满时
        //send 会报告 WSAEWOULDBLOCK，由上面的分支退出本循环，剩余数据留在 m_strSendBuf 里，
        //由 SendThreadProc 再次调用本函数发送。注意：SendThreadProc 看到缓冲非空会立即重试，
        //发送缓冲持续写满期间是一个忙等循环；这一点与这里是否 Sleep 无关，改动前就存在，
        //见仓库根目录 bugs.md。
    }

    {
        std::lock_guard<std::mutex> guard(m_mutexLastDataTime);
        m_nLastDataTime = (long)time(NULL);
    }

    return true;
}


bool CIUSocket::Recv()
{
    //把接收缓冲里现有的数据一次读完。读取循环抽到了 net::DrainSocketRecv（便于单元测试），
    //两次 recv 之间不再 Sleep(1)，原因与实测数据见该函数内的注释。
    if (m_vecRecvChunk.empty())
        m_vecRecvChunk.resize((size_t)net::kRecvChunkSize);

    net::SocketDrainStats stats;
    const net::SocketDrainResult result =
        net::DrainSocketRecv(m_hSocket, m_strRecvBuf, &m_vecRecvChunk[0],
                             (int)m_vecRecvChunk.size(), &stats);
    //出错或对端关闭：由调用方关闭连接并上报网络错误
    if (result == net::kSocketDrainError)
    {
        LOG_ERROR("Recv data error, errorNO=%d.", stats.m_nLastError);
        return false;
    }
    if (result == net::kSocketDrainClosed)
    {
        LOG_ERROR("Recv data error, connection closed by peer, bytes read before close=%u.",
                  (unsigned)stats.m_nBytes);
        return false;
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
        } else if (nRet < 1)
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
        } else if (nRet < 1)
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

void CIUSocket::ClearPendingSendData()
{
    std::lock_guard<std::mutex> guard(m_mtSendBuf);
    if (m_strSendBuf.empty())
    {
        return;
    }
    LOG_INFO("Clear pending send data, %u bytes discarded.", (uint32_t)m_strSendBuf.size());
    m_strSendBuf.clear();
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

    //连接断了，本次协商的会话密钥就作废了。这里必须清掉：重连建的是一条全新连接、
    //会重新握手，若沿用旧密钥，流状态与对端完全对不上，表现为重连后每一帧都解密失败。
    //清理放在 m_bConnected 置假之后，此时收取线程已经会走 IsClosed 分支不再解帧。
    m_handshake.reset();
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

    //本次协商的会话密钥随连接一起作废。文件链路是一笔传输一建一断，不清的话
    //下一笔会拿着上一条连接的密钥去解新连接的帧，每一帧都会解密失败。
    m_fileHandshake.reset();
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

    //会话密钥随连接一起作废，理由同 CloseFileServerConnection
    m_imgHandshake.reset();
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

void CIUSocket::RecvThreadProc()
{
    LOG_INFO("Recv data thread start...");

    int nRet;
    //上网方式 
    DWORD   dwFlags;
    BOOL    bAlive;
    while (!m_bStop)
    {
        //主连接已被关闭（典型：用户主动下线 GoOffline 关掉了 socket）。此时 m_hSocket 为
        //INVALID_SOCKET，对它反复 select 会立即返回错误、空转把一个 CPU 核烧满。故先短睡
        //再重判，等到重新上线（Login→Connect 重建 socket、m_bConnected 置真）自然恢复正常
        //收取。不在此调 NotifyNetError：主动下线的离线态 UI 已由 GoOffline 处理过。
        if (IsClosed())
        {
            ::Sleep(200);
            continue;
        }

        //检测到数据则收数据
        nRet = CheckReceivedData();
        //出错
        if (nRet == -1)
        {
            //select 报错 = 主连接已坏（典型：服务端断开 / 网络异常）。先关掉这个死 socket
            //再上报：关闭后 IsClosed() 为真，循环顶部守卫会短睡而非空转；重连
            //（OnNetError→重连定时器→Login→Connect）会重建新 socket。与下面 DecodePackages
            //失败分支同一处理，修掉"真实掉线时一直空转到重连为止"的老问题。
            Close();
            NotifyNetError();
        }
        //无数据
        else if (nRet == 0)
        {
            bAlive = ::IsNetworkAlive(&dwFlags);		//是否在线    
            if (!bAlive && ::GetLastError() == 0)
            {
                //网络已经断开
                NotifyNetError();
                LOG_ERROR("net error, exit recv and send thread...");
                Uninit();
                break;
            }

            long nLastDataTime = 0;
            {
                std::lock_guard<std::mutex> guard(m_mutexLastDataTime);
                nLastDataTime = m_nLastDataTime;
            }

            //用户主动离线期间不再发心跳：连接已经关掉，心跳帧只会被 EnqueueFrame
            //的离线闸门丢弃，还会按心跳间隔刷一堆错误日志，不如在这里就不发。
            if (m_nHeartbeatInterval > 0 && !IsUserOffline())
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
                //收数据失败 = 连接已断。同上：先关闭死 socket 再上报，避免随后空转。
                Close();
                NotifyNetError();
                continue;
            }

            //v2 拆包；返回 false 表示帧数据已错乱，关闭连接并通知上层
            if (!DecodePackages())
            {
                Close();
                NotifyNetError();
            }
        }// end if
    }// end while-loop

    LOG_INFO("Recv data thread finish...");
}

bool CIUSocket::DecodePackages()
{
    //循环从接收缓冲中拆出完整的 v2 帧，直至剩余数据不足一帧
    while (true)
    {
        frame_header header;
        std::string body;
        size_t consumed = 0;
        net::frame_decode_result result =
            net::decodeFrame(m_strRecvBuf.data(), m_strRecvBuf.size(),
                             header, body, consumed);

        //数据不足一个完整帧，等待后续字节
        if (result == net::frame_decode_need_more)
            break;

        //帧头非法（魔数/版本/长度/CRC）——连接数据已错乱
        if (result == net::frame_decode_error)
        {
            LOG_ERROR("Decode an illegal v2 frame, clear recv buffer.");
            m_strRecvBuf.clear();
            return false;
        }

        //成功解出一帧，从接收缓冲移除该帧字节
        m_strRecvBuf.erase(0, consumed);

        //还原包体明文（解压，后续还包括解密）
        std::string payload;
        if (!RestoreFrameBody(header, body, payload))
        {
            m_strRecvBuf.clear();
            return false;
        }

        //分发本帧
        HandleV2Frame(header.cmd, header.seq, payload);
    }

    return true;
}

bool CIUSocket::IsAsyncRecvRunning() const
{
    return m_spRecvThread != nullptr;
}

bool CIUSocket::CheckUpdateSync(const std::string& reqBody, std::string& respBody,
                                int nTimeoutSec)
{
    respBody.clear();

    //与 Login() 同构。检查更新在服务端是登录前白名单命令，未登录也能调。
    if (!Connect())
    {
        LOG_ERROR("CheckUpdateSync failed: connect server failed.");
        return false;
    }

    uint32_t seq = (uint32_t)(++m_seq);
    std::string sendBuf;
    if (!BuildFrame(cmd_check_update, seq, reqBody, sendBuf))
    {
        LOG_ERROR("CheckUpdateSync failed: encode frame failed.");
        return false;
    }
    if (!SendData(sendBuf.c_str(), (int)sendBuf.length(), nTimeoutSec))
    {
        LOG_ERROR("CheckUpdateSync failed: send frame failed.");
        return false;
    }

    //RecvOneFrame 交回的已经是明文包体：解密与解压都在它内部做完了
    frame_header header;
    if (!RecvOneFrame(header, respBody, nTimeoutSec))
    {
        LOG_ERROR("CheckUpdateSync failed: recv response timeout.");
        return false;
    }

    LOG_INFO("CheckUpdateSync ok, seq=%u, body bytes=%d.", seq, (int)respBody.size());
    return true;
}

bool CIUSocket::RecvOneFrame(frame_header& header, std::string& body, int nTimeout)
{
    //先收 24 字节定长帧头
    char headerBuf[FRAME_HEADER_SIZE] = { 0 };
    if (!RecvData(headerBuf, FRAME_HEADER_SIZE, nTimeout))
        return false;

    //仅凭帧头解析：decodeFrame 会填好 header 并校验魔数/版本/CRC。
    //因包体尚未收取，合法时返回 need_more，此时 header.bodylen 已可用。
    size_t consumed = 0;
    net::frame_decode_result result =
        net::decodeFrame(headerBuf, FRAME_HEADER_SIZE, header, body, consumed);
    if (result == net::frame_decode_error)
    {
        LOG_ERROR("Recv an illegal v2 frame header.");
        Close();
        return false;
    }

    //空包体帧：headerBuf 已是完整一帧
    if (result == net::frame_decode_ok)
        return true;

    //帧头合法、需继续收包体
    if (header.bodylen == 0)
        return true;
    if (header.bodylen > MAX_BODY_CHATSERVER)
    {
        LOG_ERROR("Recv v2 frame bodylen too large: %u.", header.bodylen);
        Close();
        return false;
    }

    std::string rawBody;
    rawBody.resize(header.bodylen);
    if (!RecvData(&rawBody[0], (int)header.bodylen, nTimeout))
        return false;

    //包体还原（解压，后续还包括解密）统一在 RestoreFrameBody 里做，
    //调用方拿到的一律是明文，不必各自判断压缩标志
    if (!RestoreFrameBody(header, rawBody, body))
    {
        Close();
        return false;
    }

    return true;
}

bool CIUSocket::BuildFrame(uint16_t cmd, uint32_t seq, const std::string& body,
                           std::string& frameOut)
{
    frameOut.clear();

    //包体的压缩与加密都由 FramePayload 这一条管线负责，处理顺序也在那里定死。
    //加密器未就绪（未配置服务端公钥 / 握手未完成）时它自然产出明文。
    std::string wire;
    uint8_t     flags = 0;
    uint32_t    originlen = 0;
    if (!net::encodeFramePayload(cmd, seq, body, &m_handshake.cipher(), 0,
                                 net::payload_compress_auto, wire, flags, originlen))
    {
        LOG_ERROR("Encode v2 frame payload failed, cmd=%u, bodylen=%u.",
                  cmd, (uint32_t)body.size());
        return false;
    }

    if (!net::encodeFrame(cmd, seq, flags, wire.data(), (uint32_t)wire.size(),
                          originlen, frameOut))
    {
        LOG_ERROR("Encode v2 frame failed, cmd=%u, bodylen=%u.",
                  cmd, (uint32_t)body.size());
        frameOut.clear();
        return false;
    }

    return true;
}

bool CIUSocket::RestoreFrameBody(const frame_header& header, const std::string& rawBody,
                                 std::string& plainOut)
{
    std::string err;
    if (!net::decodeFramePayload(header.cmd, header.seq, header.flags, header.originlen,
                                 rawBody, &m_handshake.cipher(), plainOut, err))
    {
        LOG_ERROR("Restore v2 frame body failed: %s, cmd=%u, bodylen=%u, originlen=%u.",
                  err.c_str(), header.cmd, (uint32_t)rawBody.size(), header.originlen);
        return false;
    }

    return true;
}

void CIUSocket::EnqueueFrame(uint16_t cmd, uint32_t seq, const std::string& body)
{
    //离线闸门（网络层兜底）。UI 层已经在各入口拦过一道，这里再拦一次是为了防止漏网：
    //一旦有帧被排进发送缓冲，它会在下次连上时被批量补发出去，对端会突然收到一堆过期
    //消息。宁可在这里丢掉并留下日志。
    //
    //注意登录帧不受影响：CIUSocket::Login 用 SendData 直发，不经过本函数。
    if (IsUserOffline())
    {
        LOG_ERROR("EnqueueFrame dropped: user is offline, cmd=%u, bodylen=%u.",
                  cmd, (uint32_t)body.size());
        return;
    }
    if (IsClosed())
    {
        LOG_ERROR("EnqueueFrame dropped: not connected, cmd=%u, bodylen=%u.",
                  cmd, (uint32_t)body.size());
        return;
    }

    std::string frame;
    if (!BuildFrame(cmd, seq, body, frame))
        return;

    std::lock_guard<std::mutex> guard(m_mtSendBuf);
    m_strSendBuf.append(frame);
    m_cvSendBuf.notify_one();
}

void CIUSocket::HandleV2Frame(uint16_t cmd, uint32_t seq, const std::string& body)
{
    //音视频类信令（白名单见 net::IsMediaSignalCmd，目前为一对一通话 1701~1709、远程桌面 1710~1716 与屏幕共享 1720~1731）在这里不解析，
    //原样经代理窗转交主面板，再由主面板交给 FlamingoMedia.dll 的模块处理。堆对象由收件方处理后 delete
    if (net::IsMediaSignalCmd(cmd))
    {
        HWND hUI = GetUIProxyWnd();
        if (!::IsWindow(hUI))
        {
            LOG_WARNING("Drop media signal: UI proxy window not ready, cmd=%u, seq=%u.", static_cast<unsigned>(cmd), seq);
            return;
        }
        net::MediaSignalPacket* pPacket = new net::MediaSignalPacket();
        pPacket->m_cmd = cmd;
        pPacket->m_body = body;
        LOG_INFO("Recv media signal, cmd=%u, seq=%u, len=%u, forward to media module.", static_cast<unsigned>(cmd), seq,
                 static_cast<unsigned>(body.size()));
        if (!::PostMessage(hUI, FMG_MSG_MEDIA_SIGNAL, 0, (LPARAM)pPacket))
        {
            LOG_ERROR("Post media signal failed, cmd=%u, err=%lu.", static_cast<unsigned>(cmd), ::GetLastError());
            delete pPacket;
        }
        return;
    }

    //文件共享信令（2200~2299）同样不解析：就在本网络接收线程上原样交给 FlamingoFileShare.dll，DLL 拷贝包体后
    //投递到自己的消息窗口，不在这里做耗时工作（docs/文件共享方案.md 7.3）。DLL 未加载时由宿主记日志后丢弃
    if (fileshare::IsShareCommand(cmd))
    {
        balloonfileshare::CFileShareHost::GetInstance().OnServerFrame(cmd, seq, body);
        return;
    }

    //P2P 信令（2300~2399）同样不解析：就在本网络接收线程上原样交给 FlamingoP2P.dll，DLL 拷贝包体后交给自己的
    //线程处理，不在这里做耗时工作（docs/P2P通信方案.md 8.4）。DLL 未加载时由宿主记日志后丢弃
    if (p2p::IsP2PCommand(cmd))
    {
        balloonp2p::CP2PHost::GetInstance().OnServerFrame(cmd, seq, body);
        return;
    }

    //密聊信令（2600~2699）同样不解析，原样交给 FlamingoSecretChat.dll，由它拷贝后投递到自己的消息窗口
    //（docs/密聊方案.md 6.2）。包体里是密文与临时公钥，这里不记包体
    if (secretchat::IsSecretCommand(cmd))
    {
        balloonsecret::CSecretChatHost::GetInstance().OnServerFrame(cmd, seq, body);
        return;
    }

    //帖子群信令（2700~2799）同样不解析，原样交给 FlamingoThreadGroup.dll，由它拷贝后投递到自己的消息窗口
    //（docs/帖子群方案.md 7.2）。DLL 未加载时由宿主记日志后丢弃
    if (balloonthread::IsThreadFrame(cmd))
    {
        balloonthread::CThreadGroupHost::GetInstance().OnServerFrame(cmd, seq, body);
        return;
    }

    //2802～2809：机器人详情、群机器人与个人机器人的增删改、试一试，原样交给 FlamingoBot.dll 宿主登记的回调
    if (net::BotProtocolClient::isDllFrame(cmd))
    {
        DispatchBotFrame(cmd, seq, body);
        return;
    }

    switch (cmd)
    {
    case cmd_request_upload:
    {
        //申请上传应答：解析后按 seq 查归属表，转交给 ImageTransfer 或 FileTransfer
        //正在等待的任务。本应答不经 UI，直接喂给对应工作线程的等待槽。
        net::RequestUploadResult r;
        if (!net::FileProtocolClient::parseRequestUploadResponse(body, r))
        {
            LOG_ERROR("Parse v2 request-upload ack failed, body=%s.", body.c_str());
            break;
        }

        int owner = net::transfer_owner_image;
        bool hit = false;
        {
            std::lock_guard<std::mutex> guard(m_transferOwnerMtx);
            hit = m_transferOwners.takeOwner(seq, owner);
        }
        LOG_INFO("Recv v2 request-upload ack, seq=%u, code=%d, exists=%d, fileid=%s, owner=%d%s.",
                 seq, r.m_code, r.m_exists ? 1 : 0, r.m_fileid.c_str(), owner, hit ? "" : "(default)");

        //归属未命中（极端：超时被淘汰 / 早到先于登记）时缺省按图片处理，与历史行为一致。
        if (owner == net::transfer_owner_file)
            net::FileTransfer::GetInstance().OnRequestUploadResponse(seq, r);
        else
            net::ImageTransfer::GetInstance().OnRequestUploadResponse(seq, r);
        break;
    }

    case cmd_check_update:
    {
        //检查更新应答：解析后按 seq 转交 UpdateManager 的等待槽。
        //本应答不经 UI，直接喂给工作线程。
        net::CheckUpdateResult r;
        if (!net::UpdateProtocolClient::parseCheckUpdateResponse(body, r))
        {
            LOG_ERROR("Parse v2 check-update ack failed, body=%s.", body.c_str());
            break;
        }
        LOG_INFO("Recv v2 check-update ack, seq=%u, code=%d, hasupdate=%d, latest=%s.",
                 seq, r.m_code, r.m_hasUpdate ? 1 : 0, r.m_latestVersion.c_str());
        update::UpdateManager::GetInstance().OnCheckUpdateResponse(seq, r);
        break;
    }

    case cmd_request_download:
    {
        //申请下载应答：解析后按 seq 查归属表，转交给 ImageTransfer 或 FileTransfer。
        net::RequestDownloadResult r;
        if (!net::FileProtocolClient::parseRequestDownloadResponse(body, r))
        {
            LOG_ERROR("Parse v2 request-download ack failed, body=%s.", body.c_str());
            break;
        }

        int owner = net::transfer_owner_image;
        bool hit = false;
        {
            std::lock_guard<std::mutex> guard(m_transferOwnerMtx);
            hit = m_transferOwners.takeOwner(seq, owner);
        }
        LOG_INFO("Recv v2 request-download ack, seq=%u, code=%d, fileid=%s, size=%lld, owner=%d%s.",
                 seq, r.m_code, r.m_fileid.c_str(), (long long)r.m_size, owner, hit ? "" : "(default)");

        if (owner == net::transfer_owner_file)
            net::FileTransfer::GetInstance().OnRequestDownloadResponse(seq, r);
        else
            net::ImageTransfer::GetInstance().OnRequestDownloadResponse(seq, r);
        break;
    }

    case cmd_heartbeat:
        //心跳应答：连接活跃时间已在 Recv() 中更新，这里无需额外处理
        break;

    case cmd_kickout:
    {
        //被踢下线（异地登录 / 账号停用 / 服务器维护 / 密码已改 / 被本人在别的设备踢掉）。
        //包体形如 {"reason":N,"msg":"..."}。解析不出 reason 时按 0（未知）继续走 ——
        //"被踢下线"这件事本身不依赖 reason，解析失败也必须照常下线，只是提示文案走兜底。
        net::KickoutNotify notify;
        if (!net::LoginProtocolClient::parseKickoutNotify(body, notify))
        {
            LOG_ERROR("Parse v2 kickout notify failed, body=%s.", body.c_str());
        }

        LOG_INFO("Recv cmd_kickout, reason=%d, msg=%s. Go offline and close connection.",
                 notify.m_reason, notify.m_msg.c_str());

        //先立起网络层的离线闸门，再关连接，顺序不能反：闸门一旦立起，发送线程就不会
        //在 Send() 里自己把连接重新建起来（见 CIUSocket::Send 开头对 IsUserOffline 的
        //判断），业务帧也不再排进发送缓冲。与 CFlamingoClient::GoOffline 同一套顺序。
        //被踢下线与用户主动离线在网络层是同一种状态：连接断开、不自动连回去，等用户
        //自己重新选"在线"（那条路会走 Login，Login 里再把闸门撤掉）。
        SetUserOffline(true);

        //通知 UI 线程进入离线态：停掉重连定时器、状态胶囊改成离线、托盘图标转灰并弹
        //气泡告知原因。必须投递到 UI 线程做 —— 本函数跑在收数据线程上，不能直接碰窗口。
        //本消息无堆负载，UI 侧收不到也不会泄漏。
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            ::PostMessage(hUI, FMG_MSG_V2_KICKOUT, (WPARAM)notify.m_reason, 0);
        }
        else
        {
            //UI 未就绪（理论上不会发生：能收到踢下线推送说明已经登录成功了）
            LOG_ERROR("Recv cmd_kickout but UI proxy window is not ready.");
        }

        Close();
        break;
    }

    case cmd_device_changed_notify:
        //本账号其它设备上/下线：本期仅记录，不做 UI 处理
        LOG_INFO("Recv cmd_device_changed_notify, body=%s.", body.c_str());
        break;

    case cmd_send_message:
    {
        //单聊发送回执：解析后投递到 UI 线程，按 clientmsgid 把"发送中"气泡
        //回填为"已发送"并记录服务器 msgid（失败则提示）。堆对象由 UI 处理后 delete。
        net::SendMessageResponse* pResp = new net::SendMessageResponse();
        if (!net::MessageProtocolClient::parseSendResponse(body, *pResp))
        {
            LOG_ERROR("Parse v2 send-message ack failed, body=%s.", body.c_str());
            delete pResp;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 send-message ack, code=%d, clientmsgid=%s, msgid=%lld.",
                     pResp->m_code, pResp->m_clientmsgid.c_str(), (long long)pResp->m_msgid);
            ::PostMessage(hUI, FMG_MSG_V2_SEND_CHATMSG_ACK, 0, (LPARAM)pResp);
        }
        else
        {
            //UI 未就绪（理论上不会发生），丢弃以免泄漏
            delete pResp;
        }
        break;
    }

    case cmd_recall_message:
    {
        //撤回请求的应答：只用于失败时提示原因（超时限 / 无权限 / 消息不存在）。
        //成功时界面改动一律等下面的 cmd_message_recalled_notify 推送再做，
        //避免"应答改一次、推送又改一次"造成重复处理。堆对象由 UI 处理后 delete。
        net::RecallMessageResponse* pResp = new net::RecallMessageResponse();
        if (!net::MessageProtocolClient::parseRecallResponse(body, *pResp))
        {
            LOG_ERROR("Parse v2 recall-message ack failed, body=%s.", body.c_str());
            delete pResp;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 recall-message ack, code=%d, msg=%s.",
                     pResp->m_code, pResp->m_msg.c_str());
            ::PostMessage(hUI, FMG_MSG_V2_RECALL_RESULT, 0, (LPARAM)pResp);
        }
        else
        {
            //UI 未就绪（理论上不会发生），丢弃以免泄漏
            delete pResp;
        }
        break;
    }

    case cmd_message_recalled_notify:
    {
        //某条消息被撤回：单聊时服务端向双方各推一条，群聊时推给全体成员，
        //撤回者本人也会收到。解析后投递到 UI 线程统一处理（改气泡、刷会话列表
        //预览、标记本地消息库）。堆对象由 UI 处理后 delete。
        net::MessageRecalledNotify* pNotify = new net::MessageRecalledNotify();
        if (!net::MessageProtocolClient::parseMessageRecalledNotify(body, *pNotify))
        {
            LOG_ERROR("Parse v2 message-recalled notify failed, body=%s.", body.c_str());
            delete pNotify;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 message-recalled notify, ctype=%d, cid=%lld, msgid=%lld, operator=%lld.",
                     pNotify->m_ctype, (long long)pNotify->m_cid,
                     (long long)pNotify->m_msgid, (long long)pNotify->m_operator);
            ::PostMessage(hUI, FMG_MSG_V2_MESSAGE_RECALLED, 0, (LPARAM)pNotify);
        }
        else
        {
            delete pNotify;
        }
        break;
    }

    //转发消息的应答：按帧 seq 投给 UI，由主面板的转发调度器对回是哪一次转发
    case cmd_forward_message:
    {
        net::ForwardMessageResponse* pResp = new net::ForwardMessageResponse();
        if (!net::MessageProtocolClient::parseForwardMessageResponse(body, *pResp))
        {
            LOG_ERROR("Parse v2 forward-message response failed, seq=%u, body=%s.", seq, body.c_str());
            delete pResp;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 forward-message response, seq=%u, code=%d, created=%u, bodies=%u.",
                     seq, pResp->m_code, (unsigned)pResp->m_msgids.size(),
                     (unsigned)pResp->m_messages.size());
            ::PostMessage(hUI, FMG_MSG_V2_FORWARD_RESULT, (WPARAM)seq, (LPARAM)pResp);
        }
        else
        {
            delete pResp;
        }
        break;
    }

    //删除单条消息的应答：只用于失败时提示，成功后的本地删除等 cmd_messages_deleted_notify
    case cmd_delete_message:
    {
        net::DeleteMessageResponse* pResp = new net::DeleteMessageResponse();
        if (!net::MessageProtocolClient::parseDeleteMessageResponse(body, *pResp))
        {
            LOG_ERROR("Parse v2 delete-message response failed, seq=%u, body=%s.", seq, body.c_str());
            delete pResp;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 delete-message response, seq=%u, code=%d.", seq, pResp->m_code);
            ::PostMessage(hUI, FMG_MSG_V2_DELETE_MESSAGE_RESULT, 0, (LPARAM)pResp);
        }
        else
        {
            delete pResp;
        }
        break;
    }

    //按时间范围清理的应答：带回服务端截断后的实际区间，交清理对话框提示结果
    case cmd_delete_messages_by_time:
    {
        net::DeleteMessagesByTimeResponse* pResp = new net::DeleteMessagesByTimeResponse();
        if (!net::MessageProtocolClient::parseDeleteMessagesByTimeResponse(body, *pResp))
        {
            LOG_ERROR("Parse v2 delete-messages-by-time response failed, seq=%u, body=%s.",
                      seq, body.c_str());
            delete pResp;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 delete-messages-by-time response, seq=%u, code=%d, range=[%lld, %lld].",
                     seq, pResp->m_code, (long long)pResp->m_begintime, (long long)pResp->m_endtime);
            ::PostMessage(hUI, FMG_MSG_V2_DELETE_BY_TIME_RESULT, 0, (LPARAM)pResp);
        }
        else
        {
            delete pResp;
        }
        break;
    }

    //按会话清空的应答：成功时界面不做任何事（等清空形式的 1522 推送），失败时主面板按 seq
    //对回是哪个会话并提示原因
    case cmd_clear_conversation:
    {
        net::ClearConversationResponse* pResp = new net::ClearConversationResponse();
        if (!net::MessageProtocolClient::parseClearConversationResponse(body, *pResp))
        {
            LOG_ERROR("Parse v2 clear-conversation response failed, seq=%u, body=%s.", seq, body.c_str());
            delete pResp;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 clear-conversation response, seq=%u, code=%d, ctype=%d, cid=%lld, clearedmsgid=%lld.",
                     seq, pResp->m_code, pResp->m_ctype, (long long)pResp->m_cid,
                     (long long)pResp->m_clearedMsgid);
            ::PostMessage(hUI, FMG_MSG_V2_CLEAR_CONVERSATION_RESULT, (WPARAM)seq, (LPARAM)pResp);
        }
        else
        {
            delete pResp;
        }
        break;
    }

    //本人删除了消息的推送：服务端推给本人全部在线设备（含发起删除的这一台）。本地库删行、
    //移除或重新加载已打开窗口里的消息，统一在 UI 线程收到本推送时做
    case cmd_messages_deleted_notify:
    {
        net::MessagesDeletedNotify* pNotify = new net::MessagesDeletedNotify();
        if (!net::MessageProtocolClient::parseMessagesDeletedNotify(body, *pNotify))
        {
            LOG_ERROR("Parse v2 messages-deleted notify failed, body=%s.", body.c_str());
            delete pNotify;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            if (pNotify->m_isClear)
            {
                //清空形式没有 msgids，不能走下面单条形式那一句（会读 m_msgids[0]）
                LOG_INFO("Recv v2 messages-deleted notify (clear), ctype=%d, cid=%lld, clearedmsgid=%lld.",
                         pNotify->m_ctype, (long long)pNotify->m_cid, (long long)pNotify->m_clearedMsgid);
            }
            else if (pNotify->m_isRange)
            {
                LOG_INFO("Recv v2 messages-deleted notify (range), range=[%lld, %lld].",
                         (long long)pNotify->m_begintime, (long long)pNotify->m_endtime);
            }
            else
            {
                LOG_INFO("Recv v2 messages-deleted notify, ctype=%d, cid=%lld, count=%u, first msgid=%lld.",
                         pNotify->m_ctype, (long long)pNotify->m_cid,
                         (unsigned)pNotify->m_msgids.size(), (long long)pNotify->m_msgids[0]);
            }
            ::PostMessage(hUI, FMG_MSG_V2_MESSAGES_DELETED, 0, (LPARAM)pNotify);
        }
        else
        {
            delete pNotify;
        }
        break;
    }

    case cmd_features_notify:
    {
        //本人所属企业的功能开关有变化（管理员在后台改了开关）：包体与登录应答里的 features 同构。
        //解析后投递到 UI 线程，由主面板交给文件共享宿主。堆对象由 UI 处理后 delete
        net::FeaturesNotify* pNotify = new net::FeaturesNotify();
        if (!net::LoginProtocolClient::parseFeaturesNotify(body, *pNotify))
        {
            LOG_ERROR("Parse v2 features notify failed, body=%s.", body.c_str());
            delete pNotify;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 features notify, body=%s.", body.c_str());
            ::PostMessage(hUI, FMG_MSG_V2_FEATURES_NOTIFY, 0, (LPARAM)pNotify);
        }
        else
        {
            delete pNotify;
        }
        break;
    }

    case cmd_message_read_notify:
    {
        //对方已读你的消息：接收方查看会话后上报 cmd_mark_read，服务端向发送方推送本条，
        //带上对方的已读位点。只有单聊会收到。解析后投递到 UI 线程，由主面板记下位点并
        //转给已打开的单聊窗把对应气泡标为已读。堆对象由 UI 处理后 delete。
        net::MessageReadNotify* pNotify = new net::MessageReadNotify();
        if (!net::MessageProtocolClient::parseMessageReadNotify(body, *pNotify))
        {
            LOG_ERROR("Parse v2 message-read notify failed, body=%s.", body.c_str());
            delete pNotify;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 message-read notify, ctype=%d, cid=%lld, readmsgid=%lld, readcount=%d.",
                     pNotify->m_ctype, (long long)pNotify->m_cid,
                     (long long)pNotify->m_readmsgid, pNotify->m_readcount);
            ::PostMessage(hUI, FMG_MSG_V2_MESSAGE_READ_NOTIFY, 0, (LPARAM)pNotify);
        }
        else
        {
            delete pNotify;
        }
        break;
    }

    case cmd_join_relay:
    case cmd_withdraw_relay:
    case cmd_close_relay:
    {
        //接龙三种操作的应答：只用于失败时提示原因。成功时界面改动一律等下面的
        //cmd_relay_changed_notify 推送再做，与 Pin 同口径。wParam 带命令号区分是哪种操作；
        //堆对象由 UI 处理后 delete
        net::RelayOpResponse* pResp = new net::RelayOpResponse();
        if (!net::CardProtocolClient::parseRelayOpResponse(body, *pResp))
        {
            LOG_ERROR("Parse v2 relay op ack failed, cmd=%u, body=%s.", (unsigned)cmd, body.c_str());
            delete pResp;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 relay op ack, cmd=%u, code=%d, msg=%s, ctype=%d, cid=%lld, msgid=%lld.",
                     (unsigned)cmd, pResp->m_code, pResp->m_msg.c_str(), pResp->m_ctype,
                     (long long)pResp->m_cid, (long long)pResp->m_msgid);
            ::PostMessage(hUI, FMG_MSG_V2_RELAY_OP_RESULT, (WPARAM)cmd, (LPARAM)pResp);
        }
        else
        {
            //UI 未就绪（理论上不会发生），丢弃以免泄漏
            delete pResp;
        }
        break;
    }

    case cmd_relay_changed_notify:
    {
        //接龙名单或状态变化：服务端推给卡片所在会话全体，操作者本人也会收到，带的是完整名单。
        //解析后投递到 UI 线程统一处理（合并进本地库的卡片内容、刷新界面）。堆对象由 UI 处理后 delete
        net::RelayChangedNotify* pNotify = new net::RelayChangedNotify();
        if (!net::CardProtocolClient::parseRelayChangedNotify(body, *pNotify))
        {
            LOG_ERROR("Parse v2 relay changed notify failed, body=%s.", body.c_str());
            delete pNotify;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 relay changed notify, ctype=%d, cid=%lld, msgid=%lld, items=%u, state=%d.",
                     pNotify->m_ctype, (long long)pNotify->m_cid, (long long)pNotify->m_msgid,
                     (unsigned)pNotify->m_items.size(), pNotify->m_state);
            ::PostMessage(hUI, FMG_MSG_V2_RELAY_CHANGED, 0, (LPARAM)pNotify);
        }
        else
        {
            delete pNotify;
        }
        break;
    }

    case cmd_pin_message:
    {
        //Pin / 取消 Pin 请求的应答：只用于失败时提示原因（无权取消 / 消息已撤回 /
        //已不在群里）。成功时界面改动一律等下面的 cmd_message_pinned_notify 推送再做，
        //与撤回同口径 —— 这样本端与对端看到的结果必然一致。堆对象由 UI 处理后 delete。
        net::PinMessageResponse* pResp = new net::PinMessageResponse();
        if (!net::MessageProtocolClient::parsePinResponse(body, *pResp))
        {
            LOG_ERROR("Parse v2 pin-message ack failed, body=%s.", body.c_str());
            delete pResp;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 pin-message ack, code=%d, msg=%s, ctype=%d, cid=%lld, msgid=%lld, pin=%d.",
                     pResp->m_code, pResp->m_msg.c_str(), pResp->m_ctype,
                     (long long)pResp->m_cid, (long long)pResp->m_msgid, pResp->m_pin);
            ::PostMessage(hUI, FMG_MSG_V2_PIN_RESULT, 0, (LPARAM)pResp);
        }
        else
        {
            //UI 未就绪（理论上不会发生），丢弃以免泄漏
            delete pResp;
        }
        break;
    }

    case cmd_message_pinned_notify:
    {
        //某条消息被 Pin / 取消 Pin：Pin 是会话内共享的状态，单聊时服务端向双方各推
        //一条，群聊时推给全体成员，操作者本人也会收到。解析后投递到 UI 线程统一处理
        //（落本地消息库标记、刷消息记录窗）。堆对象由 UI 处理后 delete。
        net::MessagePinnedNotify* pNotify = new net::MessagePinnedNotify();
        if (!net::MessageProtocolClient::parseMessagePinnedNotify(body, *pNotify))
        {
            LOG_ERROR("Parse v2 message-pinned notify failed, body=%s.", body.c_str());
            delete pNotify;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 message-pinned notify, ctype=%d, cid=%lld, msgid=%lld, pin=%d, operator=%lld.",
                     pNotify->m_ctype, (long long)pNotify->m_cid,
                     (long long)pNotify->m_msgid, pNotify->m_pin,
                     (long long)pNotify->m_operator);
            ::PostMessage(hUI, FMG_MSG_V2_MESSAGE_PINNED, 0, (LPARAM)pNotify);
        }
        else
        {
            delete pNotify;
        }
        break;
    }

    case cmd_get_pinned_messages:
    {
        //某会话当前被 Pin 的消息全集（带完整消息体）。UI 拿到后把本地库里缺的那些
        //补齐，并与这一份对账 —— 本地库只有本机同步过的消息，别人钉了一条很老的
        //消息时本机可能压根没有那一行。堆对象由 UI 处理后 delete。
        net::PinnedMessagesResult* pResult = new net::PinnedMessagesResult();
        if (!net::MessageProtocolClient::parseGetPinnedMessagesResponse(body, *pResult))
        {
            LOG_ERROR("Parse v2 get-pinned-messages ack failed, body=%s.", body.c_str());
            delete pResult;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 get-pinned-messages ack, code=%d, ctype=%d, cid=%lld, returned=%d, total=%lld.",
                     pResult->m_code, pResult->m_ctype, (long long)pResult->m_cid,
                     (int)pResult->m_items.size(), (long long)pResult->m_total);
            ::PostMessage(hUI, FMG_MSG_V2_PINNED_MESSAGES, 0, (LPARAM)pResult);
        }
        else
        {
            delete pResult;
        }
        break;
    }

    case cmd_file_transfer_ack:
    {
        //对方把我发的某个文件收到什么程度了（开始接收 / 已接收 / 已拒绝）。
        //本端作为文件发送方收到，投到 UI 线程按 msgid 更新那条文件气泡的状态行。
        //堆对象由 UI 处理后 delete。
        net::FileTransferAckNotify* pNotify = new net::FileTransferAckNotify();
        if (!net::MessageProtocolClient::parseFileTransferAckNotify(body, *pNotify))
        {
            LOG_ERROR("Parse v2 file-transfer-ack notify failed, body=%s.", body.c_str());
            delete pNotify;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 file-transfer-ack notify, cid=%lld, msgid=%lld, state=%d.",
                     (long long)pNotify->m_cid, (long long)pNotify->m_msgid, pNotify->m_state);
            ::PostMessage(hUI, FMG_MSG_V2_FILE_TRANSFER_ACK, 0, (LPARAM)pNotify);
        }
        else
        {
            delete pNotify;
        }
        break;
    }

    case cmd_typing_notify:
    {
        //对端正在（或停止）输入：解析后投到 UI 线程，按 cid（即对端userid）路由到
        //对应单聊窗刷新头部提示。堆对象由 UI 处理后 delete。
        //本包不可靠、也不应答：解析失败只记一笔就丢，不做任何补偿。
        net::TypingNotify* pNotify = new net::TypingNotify();
        if (!net::MessageProtocolClient::parseTypingNotify(body, *pNotify))
        {
            LOG_WARNING("Parse v2 typing notify failed, body=%s.", body.c_str());
            delete pNotify;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 typing notify, cid=%lld, state=%d.",
                     (long long)pNotify->m_cid, pNotify->m_state);
            ::PostMessage(hUI, FMG_MSG_V2_TYPING_NOTIFY, 0, (LPARAM)pNotify);
        }
        else
        {
            delete pNotify;
        }
        break;
    }

    case cmd_message_notify:
    {
        //收到对端发来的新消息：解析后投递到 UI 线程，路由到对应单聊窗口渲染。
        //堆对象由 UI 处理后 delete。
        net::MessageNotify* pNotify = new net::MessageNotify();
        if (!net::MessageProtocolClient::parseMessageNotify(body, *pNotify))
        {
            LOG_ERROR("Parse v2 message-notify failed, body=%s.", body.c_str());
            delete pNotify;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 message-notify, from=%lld, msgid=%lld, msgtype=%d.",
                     (long long)pNotify->m_from, (long long)pNotify->m_msgid, pNotify->m_msgtype);
            ::PostMessage(hUI, FMG_MSG_V2_RECV_CHATMSG, 0, (LPARAM)pNotify);
        }
        else
        {
            delete pNotify;
        }
        break;
    }

    case cmd_folder_invite:
    {
        //文件夹传输邀请应答：成功带 sessionid；规模超限时另带服务端配置的上限值，
        //客户端一律用它们做提示、不硬编码。堆对象由 UI 处理后 delete。
        //<u>wParam 必须带上本帧的 seq</u>：包体里没有任何能对回发起方的字段，而本机同时
        //可以有多笔传输在等应答，管理器正是按这个 seq 找到发起它的那一笔（与
        //cmd_request_upload / cmd_request_download 按 seq 找等待槽同理）。
        net::FolderInviteResponse* pResp = new net::FolderInviteResponse();
        if (!net::FolderProtocolClient::parseInviteResponse(body, *pResp))
        {
            LOG_ERROR("Parse v2 folder invite-ack failed, seq=%u, body=%s.", seq, body.c_str());
            delete pResp;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 folder invite-ack, seq=%u, code=%d, session=%s, maxfiles=%lld, "
                     "maxsize=%lld.",
                     seq, pResp->m_code, pResp->m_sessionId.c_str(),
                     (long long)pResp->m_maxFiles, (long long)pResp->m_maxSize);
            ::PostMessage(hUI, FMG_MSG_V2_FOLDER_INVITE_ACK, (WPARAM)seq, (LPARAM)pResp);
        }
        else
        {
            delete pResp;
        }
        break;
    }

    case cmd_folder_invite_notify:
    {
        //文件夹传输邀请推送：接收端据此在会话里出一条带「接收 / 拒绝」的邀请气泡，
        //不弹独立授权窗。堆对象由 UI 处理后 delete。
        net::FolderInviteNotify* pNotify = new net::FolderInviteNotify();
        if (!net::FolderProtocolClient::parseInviteNotify(body, *pNotify))
        {
            LOG_ERROR("Parse v2 folder invite-notify failed, body=%s.", body.c_str());
            delete pNotify;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 folder invite-notify, session=%s, from=%lld, files=%lld, bytes=%lld.",
                     pNotify->m_sessionId.c_str(), (long long)pNotify->m_fromId,
                     (long long)pNotify->m_totalFiles, (long long)pNotify->m_totalSize);
            ::PostMessage(hUI, FMG_MSG_V2_FOLDER_INVITE_NOTIFY, 0, (LPARAM)pNotify);
        }
        else
        {
            delete pNotify;
        }
        break;
    }

    case cmd_folder_response:
    case cmd_folder_cancel:
    case cmd_folder_close:
    case cmd_folder_retry:
    {
        //应答邀请 / 撤回邀请 / 结束传输 / 重试续传四条命令的应答体同构（只有 code、msg
        //与原样回带的 sessionid），故共用一条窗口消息，由 wParam 带上是哪条命令的应答，
        //UI 侧据它决定失败时怎么收场。堆对象由 UI 处理后 delete。
        net::FolderSessionResponse* pResp = new net::FolderSessionResponse();
        if (!net::FolderProtocolClient::parseSessionResponse(body, *pResp))
        {
            LOG_ERROR("Parse v2 folder session-ack failed, cmd=%u, body=%s.", cmd, body.c_str());
            delete pResp;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 folder session-ack, cmd=%u, code=%d, session=%s.",
                     cmd, pResp->m_code, pResp->m_sessionId.c_str());
            ::PostMessage(hUI, FMG_MSG_V2_FOLDER_SESSION_ACK, (WPARAM)cmd, (LPARAM)pResp);
        }
        else
        {
            delete pResp;
        }
        break;
    }

    case cmd_folder_state_notify:
    {
        //文件夹传输状态推送：响铃 / 传输中 / 拒绝 / 撤回 / 对方离线 / 超时 / 失败 / 完成。
        //失败时带的错误码两端一致，界面据它给出同一条中文原因。堆对象由 UI 处理后 delete。
        net::FolderStateNotify* pState = new net::FolderStateNotify();
        if (!net::FolderProtocolClient::parseStateNotify(body, *pState))
        {
            LOG_ERROR("Parse v2 folder state-notify failed, body=%s.", body.c_str());
            delete pState;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 folder state-notify, session=%s, state=%d, errcode=%d.",
                     pState->m_sessionId.c_str(), pState->m_state, pState->m_errCode);
            ::PostMessage(hUI, FMG_MSG_V2_FOLDER_STATE_NOTIFY, 0, (LPARAM)pState);
        }
        else
        {
            delete pState;
        }
        break;
    }

    case cmd_folder_relay_info:
    {
        //文件夹传输中转参数下发：双方据此连 fileserver 建 1 条控制通道 + N 条数据通道。
        //重试续传成功后同样会再推一帧。堆对象由 UI 处理后 delete。
        net::FolderRelayInfo* pInfo = new net::FolderRelayInfo();
        if (!net::FolderProtocolClient::parseRelayInfo(body, *pInfo))
        {
            LOG_ERROR("Parse v2 folder relay-info failed, body=%s.", body.c_str());
            delete pInfo;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 folder relay-info, session=%s, role=%d, %s:%d, lanes=%d.",
                     pInfo->m_sessionId.c_str(), pInfo->m_role,
                     pInfo->m_relayIp.c_str(), pInfo->m_relayPort, pInfo->m_lanes);
            ::PostMessage(hUI, FMG_MSG_V2_FOLDER_RELAY_INFO, 0, (LPARAM)pInfo);
        }
        else
        {
            delete pInfo;
        }
        break;
    }

    case cmd_get_contacts:
    {
        //通讯录应答：解析后投递到 UI 线程，填充联系人面板。堆对象由 UI 处理后 delete。
        net::ContactsResult* pResult = new net::ContactsResult();
        if (!net::ContactsProtocolClient::parseContactsResponse(body, *pResult))
        {
            LOG_ERROR("Parse v2 contacts response failed, bodylen=%u.", (uint32_t)body.size());
            delete pResult;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 contacts, code=%d, enterprise=%lld, contacts=%u.",
                     pResult->m_code, (long long)pResult->m_enterpriseid,
                     (uint32_t)pResult->m_contacts.size());
            ::PostMessage(hUI, FMG_MSG_V2_CONTACTS, 0, (LPARAM)pResult);
        }
        else
        {
            delete pResult;
        }
        break;
    }

    case cmd_sync_conversations:
    {
        //会话列表应答：解析后投递到 UI 线程，填充"消息"页。堆对象由 UI 处理后 delete。
        net::ConversationsResult* pResult = new net::ConversationsResult();
        if (!net::ConversationsProtocolClient::parseSyncConversationsResponse(body, *pResult))
        {
            LOG_ERROR("Parse v2 sync-conversations response failed, bodylen=%u.", (uint32_t)body.size());
            delete pResult;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 conversations, code=%d, count=%u.",
                     pResult->m_code, (uint32_t)pResult->m_items.size());
            ::PostMessage(hUI, FMG_MSG_V2_CONVERSATIONS, 0, (LPARAM)pResult);
        }
        else
        {
            delete pResult;
        }
        break;
    }

    case cmd_create_group:
    {
        //建群应答：解析后投递到 UI 线程，由主面板提示成功/失败。堆对象由 UI 处理后 delete。
        net::CreateGroupResponse* pResp = new net::CreateGroupResponse();
        if (!net::GroupProtocolClient::parseCreateGroupResponse(body, *pResp))
        {
            LOG_ERROR("Parse v2 create-group response failed, bodylen=%u.", (uint32_t)body.size());
            delete pResp;
            break;
        }
        pResp->m_seq = seq;
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 create-group result, seq=%u, code=%d, groupid=%lld.", seq,
                     pResp->m_code, (long long)pResp->m_groupid);
            ::PostMessage(hUI, FMG_MSG_V2_CREATE_GROUP_RESULT, 0, (LPARAM)pResp);
        }
        else
        {
            delete pResp;
        }
        break;
    }

    case cmd_change_password:
    {
        //改密应答：解析后投递到 UI 线程，由改密对话框提示成功/失败。堆对象由 UI 处理后 delete。
        net::ChangePasswordResponse* pResp = new net::ChangePasswordResponse();
        if (!net::AccountProtocolClient::parseChangePasswordResponse(body, *pResp))
        {
            LOG_ERROR("Parse v2 change-password response failed, bodylen=%u.", (uint32_t)body.size());
            delete pResp;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 change-password result, code=%d.", pResp->code);
            ::PostMessage(hUI, FMG_MSG_V2_CHANGE_PWD_RESULT, 0, (LPARAM)pResp);
        }
        else
        {
            delete pResp;
        }
        break;
    }

    case cmd_update_profile:
    {
        //更新资料应答（当前用于个性签名）：解析后投递到 UI 线程，由主面板据 code
        //决定保留乐观更新或回退提示。堆对象由 UI 处理后 delete。
        net::UpdateProfileResponse* pResp = new net::UpdateProfileResponse();
        if (!net::ProfileProtocolClient::parseUpdateProfileResponse(body, *pResp))
        {
            LOG_ERROR("Parse v2 update-profile response failed, bodylen=%u.", (uint32_t)body.size());
            delete pResp;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 update-profile result, code=%d.", pResp->code);
            ::PostMessage(hUI, FMG_MSG_V2_UPDATE_PROFILE_RESULT, 0, (LPARAM)pResp);
        }
        else
        {
            delete pResp;
        }
        break;
    }

    case cmd_change_avatar:
    {
        //修改头像应答（通用 {code,msg}，复用 UpdateProfileResponse 承载）：解析后投递到
        //UI 线程，由修改头像对话框据 code 提示成功/失败、成功则刷新本端头像。堆对象由
        //UI 处理后 delete。
        net::UpdateProfileResponse* pResp = new net::UpdateProfileResponse();
        if (!net::ProfileProtocolClient::parseUpdateProfileResponse(body, *pResp))
        {
            LOG_ERROR("Parse v2 change-avatar response failed, bodylen=%u.", (uint32_t)body.size());
            delete pResp;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 change-avatar result, code=%d.", pResp->code);
            ::PostMessage(hUI, FMG_MSG_V2_CHANGE_AVATAR_RESULT, 0, (LPARAM)pResp);
        }
        else
        {
            delete pResp;
        }
        break;
    }

    case cmd_change_presence:
    {
        //修改在线状态应答（通用 {code,msg}）：解析后投递到 UI 线程，由主面板据 code
        //决定保留乐观更新的胶囊还是回滚 + 提示。堆对象由 UI 处理后 delete。
        net::ChangePresenceResponse* pResp = new net::ChangePresenceResponse();
        if (!net::ProfileProtocolClient::parseChangePresenceResponse(body, *pResp))
        {
            LOG_ERROR("Parse v2 change-presence response failed, bodylen=%u.", (uint32_t)body.size());
            delete pResp;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 change-presence result, code=%d.", pResp->code);
            ::PostMessage(hUI, FMG_MSG_V2_CHANGE_PRESENCE_RESULT, 0, (LPARAM)pResp);
        }
        else
        {
            delete pResp;
        }
        break;
    }

    case cmd_user_info_changed_notify:
    {
        //联系人资料变更推送（头像/昵称等）：解析后投递到 UI 线程，由主面板更新通讯录缓存里
        //该 userid 的头像字段并重画联系人/会话头像。堆对象由 UI 处理后 delete。
        net::UserInfoChangedNotify* pNotify = new net::UserInfoChangedNotify();
        if (!net::ContactsProtocolClient::parseUserInfoChangedNotify(body, *pNotify))
        {
            LOG_ERROR("Parse v2 user-info-changed notify failed, bodylen=%u.", (uint32_t)body.size());
            delete pNotify;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 user-info-changed notify, userid=%lld, facetype=%d.",
                     (long long)pNotify->m_userid, pNotify->m_facetype);
            ::PostMessage(hUI, FMG_MSG_V2_USERINFO_CHANGED, 0, (LPARAM)pNotify);
        }
        else
        {
            delete pNotify;
        }
        break;
    }

    case cmd_org_structure_notify:
    {
        //组织架构变更推送：本企业新建/改动了部门，或者有人进出部门（含新用户注册后
        //被挂进公司根部门）。本端的通讯录只在登录时拉过一次，不接这条推送的话，新
        //同事要等到下次登录才会出现，中途收到他的消息也只能显示成"用户<userid>"。
        //解析后投递到 UI 线程，由主面板重新拉一次通讯录。堆对象由 UI 处理后 delete。
        net::OrgStructureChangedNotify* pNotify = new net::OrgStructureChangedNotify();
        if (!net::ContactsProtocolClient::parseOrgStructureChangedNotify(body, *pNotify))
        {
            LOG_ERROR("Parse v2 org-structure-changed notify failed, bodylen=%u.", (uint32_t)body.size());
            delete pNotify;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 org-structure-changed notify, changetype=%d, deptid=%lld.",
                     pNotify->m_changetype, (long long)pNotify->m_deptid);
            ::PostMessage(hUI, FMG_MSG_V2_ORG_STRUCTURE_CHANGED, 0, (LPARAM)pNotify);
        }
        else
        {
            delete pNotify;
        }
        break;
    }

    case cmd_presence_changed_notify:
    {
        //好友在线状态变更推送：解析 {userid,presence,clienttype} 后投递到 UI 线程，由主面板
        //更新通讯录缓存里该 userid 的 presence 并重着色联系人树对应节点。堆对象由 UI 处理后 delete。
        net::PresenceChangedNotify* pNotify = new net::PresenceChangedNotify();
        if (!net::ContactsProtocolClient::parsePresenceChangedNotify(body, *pNotify))
        {
            LOG_ERROR("Parse v2 presence-changed notify failed, bodylen=%u.", (uint32_t)body.size());
            delete pNotify;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 presence-changed notify, userid=%lld, presence=%d.",
                     (long long)pNotify->m_userid, pNotify->m_presence);
            ::PostMessage(hUI, FMG_MSG_V2_PRESENCE_CHANGED, 0, (LPARAM)pNotify);
        }
        else
        {
            delete pNotify;
        }
        break;
    }

    //群管理的九条写操作：应答体都是通用 { code, msg }，处理方式也完全一致，
    //故合并到一个分支里，用 wParam 把"是哪条协议的应答"带给界面层。
    case cmd_invite_to_group:
    case cmd_kick_group_member:
    case cmd_set_member_role:
    case cmd_mute_member:
    case cmd_mute_all:
    case cmd_update_group_info:
    case cmd_transfer_owner:
    case cmd_dismiss_group:
    case cmd_handle_join_group:
    //发布公告的应答体是 { code, msg, id }，比通用形态多一个 id。界面上用不到它
    //（发布成功后靠 1326 推送整体刷新列表，新公告的内容推送里都有），故这里按
    //通用形态解析，多出来的 id 直接忽略。
    case cmd_publish_group_announcement:
    case cmd_delete_group_announcement:
    //入群必读的设置、确认、放行，群问答的保存与删除，作废加群链接：应答都是 { code, msg }（保存问答多一个 id，
    //界面用不到），成功后各有推送刷新界面，同样按通用形态处理（2026-10-07 起）
    case cmd_set_must_read:
    case cmd_confirm_must_read:
    case cmd_release_must_read:
    case cmd_save_group_qa:
    case cmd_delete_group_qa:
    case cmd_revoke_group_link:
    {
        net::GroupOpResult* pResult = new net::GroupOpResult();
        if (!net::GroupProtocolClient::parseCodeMsgResponse(body, pResult->m_code, pResult->m_msg))
        {
            LOG_ERROR("Parse v2 group-op response failed, cmd=%u, bodylen=%u.",
                      (uint32_t)cmd, (uint32_t)body.size());
            delete pResult;
            break;
        }
        //邀请入群的应答另带三项统计，界面据此提示「等待审批」「等对方同意」
        if (cmd == cmd_invite_to_group)
        {
            net::GroupProtocolClient::parseInviteToGroupCounts(body, *pResult);
            LOG_INFO("Recv v2 invite-to-group counts, added=%d, invited=%d, pending=%d.",
                     pResult->m_added, pResult->m_invited, pResult->m_pending);
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 group op result, cmd=%u, code=%d, msg=%s.",
                     (uint32_t)cmd, pResult->m_code, pResult->m_msg.c_str());
            ::PostMessage(hUI, FMG_MSG_V2_GROUP_OP_RESULT, (WPARAM)cmd, (LPARAM)pResult);
        }
        else
        {
            delete pResult;
        }
        break;
    }

    case cmd_accept_group_invite:
    {
        //点入群邀请卡片「加入群聊」的应答：成功时主面板打开群聊窗或提示等待审批，失败时提示原因并恢复卡片按钮；
        //卡片状态的变化另由 cmd_group_invite_changed_notify 推送。堆对象由 UI 处理后 delete
        net::AcceptGroupInviteResult* pResult = new net::AcceptGroupInviteResult();
        if (!net::GroupProtocolClient::parseAcceptGroupInviteResponse(body, *pResult))
        {
            LOG_ERROR("Parse v2 accept-group-invite response failed, body=%s.", body.c_str());
            delete pResult;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 accept-group-invite ack, code=%d, msg=%s, inviteid=%lld, groupid=%lld, joined=%d.",
                     pResult->m_code, pResult->m_msg.c_str(), (long long)pResult->m_inviteid,
                     (long long)pResult->m_groupid, pResult->m_joined ? 1 : 0);
            ::PostMessage(hUI, FMG_MSG_V2_ACCEPT_GROUP_INVITE, 0, (LPARAM)pResult);
        }
        else
        {
            delete pResult;
        }
        break;
    }

    case cmd_call_card_notify:
    {
        //一对一通话卡片已改写成结束状态：推给主叫与被叫的全部在线设备。主面板据此改本地库里的卡片、
        //刷新已打开的单聊窗与消息记录窗。堆对象由 UI 处理后 delete
        net::CallCardNotify* pNotify = new net::CallCardNotify();
        if (!net::MessageProtocolClient::parseCallCardNotify(body, *pNotify))
        {
            LOG_ERROR("Parse v2 call card notify failed, body=%s.", body.c_str());
            delete pNotify;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 call card notify, session=%s, msgid=%lld, cid=%lld.", pNotify->m_sessionId.c_str(),
                     (long long)pNotify->m_msgid, (long long)pNotify->m_cid);
            ::PostMessage(hUI, FMG_MSG_V2_CALL_CARD_CHANGED, 0, (LPARAM)pNotify);
        }
        else
        {
            delete pNotify;
        }
        break;
    }

    case cmd_group_invite_changed_notify:
    {
        //入群邀请卡片的状态变化：推给邀请人与被邀请人的全部在线设备。主面板据此改本地库里的卡片状态、
        //刷新已打开的单聊窗。堆对象由 UI 处理后 delete
        net::GroupInviteChangedNotify* pNotify = new net::GroupInviteChangedNotify();
        if (!net::GroupProtocolClient::parseGroupInviteChangedNotify(body, *pNotify))
        {
            LOG_ERROR("Parse v2 group-invite changed notify failed, body=%s.", body.c_str());
            delete pNotify;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 group-invite changed notify, inviteid=%lld, msgid=%lld, inviter=%lld, invitee=%lld, "
                     "state=%d.", (long long)pNotify->m_inviteid, (long long)pNotify->m_msgid,
                     (long long)pNotify->m_inviterid, (long long)pNotify->m_inviteeid, pNotify->m_state);
            ::PostMessage(hUI, FMG_MSG_V2_GROUP_INVITE_CHANGED, 0, (LPARAM)pNotify);
        }
        else
        {
            delete pNotify;
        }
        break;
    }

    case cmd_join_request_handled_notify:
    {
        //一条加群申请已被处理（同意或拒绝）：推给该群全体群主与管理员（含处理人本人的其他设备）。主面板转给开着的
        //群管理窗重拉待审批列表，避免再去处理一条已处理过的申请。堆对象由 UI 处理后 delete
        net::JoinRequestHandledNotify* pNotify = new net::JoinRequestHandledNotify();
        if (!net::GroupProtocolClient::parseJoinRequestHandledNotify(body, *pNotify))
        {
            LOG_ERROR("Parse v2 join-request handled notify failed, body=%s.", body.c_str());
            delete pNotify;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 join-request handled notify, groupid=%lld, requestid=%lld, status=%d, handler=%lld.",
                     (long long)pNotify->m_groupid, (long long)pNotify->m_requestid, pNotify->m_status,
                     (long long)pNotify->m_handler);
            ::PostMessage(hUI, FMG_MSG_V2_JOIN_REQUEST_HANDLED, 0, (LPARAM)pNotify);
        }
        else
        {
            delete pNotify;
        }
        break;
    }

    case cmd_get_group_join_requests:
    {
        //待审批加群申请列表：解析后投给群管理窗口的"加群审批"页
        net::GroupJoinRequestsResult* pResult = new net::GroupJoinRequestsResult();
        if (!net::GroupProtocolClient::parseJoinRequestsResponse(body, *pResult))
        {
            LOG_ERROR("Parse v2 join-requests response failed, bodylen=%u.", (uint32_t)body.size());
            delete pResult;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 join requests, code=%d, groupid=%lld, count=%u.",
                     pResult->m_code, (long long)pResult->m_groupid,
                     (uint32_t)pResult->m_requests.size());
            ::PostMessage(hUI, FMG_MSG_V2_GROUP_JOIN_REQUESTS, 0, (LPARAM)pResult);
        }
        else
        {
            delete pResult;
        }
        break;
    }

    case cmd_get_group_announcements:
    {
        net::GroupAnnouncementsResult* pResult = new net::GroupAnnouncementsResult();
        if (!net::GroupProtocolClient::parseAnnouncementsResponse(body, *pResult))
        {
            LOG_ERROR("Parse v2 group-announcements response failed, bodylen=%u.", (uint32_t)body.size());
            delete pResult;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 group announcements, code=%d, groupid=%lld, count=%u, total=%d.",
                     pResult->m_code, (long long)pResult->m_groupid,
                     (uint32_t)pResult->m_announcements.size(), pResult->m_total);
            ::PostMessage(hUI, FMG_MSG_V2_GROUP_ANNOUNCEMENTS, 0, (LPARAM)pResult);
        }
        else
        {
            delete pResult;
        }
        break;
    }

    case cmd_get_must_read:
    {
        //查询入群必读的应答：主面板按 groupid 转给打开着的群聊窗，决定是否锁住输入区并弹出必读公告
        net::MustReadResult* pResult = new net::MustReadResult();
        if (!net::GroupProtocolClient::parseGetMustReadResponse(body, *pResult))
        {
            LOG_ERROR("Parse v2 get-must-read response failed, body=%s.", body.c_str());
            delete pResult;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 must-read, code=%d, groupid=%lld, mustreadid=%lld, pending=%d.", pResult->m_code,
                     (long long)pResult->m_groupid, (long long)pResult->m_mustreadid, pResult->m_pending ? 1 : 0);
            ::PostMessage(hUI, FMG_MSG_V2_MUST_READ, 0, (LPARAM)pResult);
        }
        else
        {
            delete pResult;
        }
        break;
    }

    case cmd_get_group_qa:
    {
        //群问答列表的应答：主面板按 groupid 转给打开着的群聊窗的「问答」页签
        net::GroupQaResult* pResult = new net::GroupQaResult();
        if (!net::GroupQaProtocolClient::parseGetResponse(body, *pResult))
        {
            LOG_ERROR("Parse v2 group-qa response failed, bodylen=%u.", (uint32_t)body.size());
            delete pResult;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 group qa, code=%d, groupid=%lld, count=%u.", pResult->m_code,
                     (long long)pResult->m_groupid, (uint32_t)pResult->m_items.size());
            ::PostMessage(hUI, FMG_MSG_V2_GROUP_QA, 0, (LPARAM)pResult);
        }
        else
        {
            delete pResult;
        }
        break;
    }

    case cmd_get_group_bots:
    {
        //本群可用的机器人：主面板按 groupid 转给打开着的群聊窗
        net::GroupBotsResult* pResult = new net::GroupBotsResult();
        if (!net::BotProtocolClient::parseGetGroupBotsResponse(body, *pResult))
        {
            LOG_ERROR("Parse v2 group-bots response failed, bodylen=%u.", (uint32_t)body.size());
            delete pResult;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 group bots, code=%d, groupid=%lld, count=%u.", pResult->m_code,
                     (long long)pResult->m_groupid, (uint32_t)pResult->m_bots.size());
            ::PostMessage(hUI, FMG_MSG_V2_GROUP_BOTS, 0, (LPARAM)pResult);
        }
        else
        {
            delete pResult;
        }
        break;
    }

    case cmd_group_bots_changed_notify:
    {
        //本群可用的机器人有变化（groupid 为 0 表示本企业全部群）：打开着的群聊窗重拉
        int64_t groupId = 0;
        if (!net::BotProtocolClient::parseGroupBotsChangedNotify(body, groupId))
        {
            LOG_ERROR("Parse v2 group-bots changed notify failed, body=%s.", body.c_str());
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 group bots changed notify, groupid=%lld.", (long long)groupId);
            ::PostMessage(hUI, FMG_MSG_V2_GROUP_BOTS_CHANGED, 0, (LPARAM)new int64_t(groupId));
        }
        break;
    }

    case cmd_get_auto_reply:
    {
        //本人的「离开 / 忙碌自动回复」设置：主面板据此刷新本地设置，或把本机旧设置上传一次
        net::AutoReplyResult* pResult = new net::AutoReplyResult();
        if (!net::BotProtocolClient::parseGetAutoReplyResponse(body, *pResult))
        {
            LOG_ERROR("Parse v2 auto-reply response failed, bodylen=%u.", (uint32_t)body.size());
            delete pResult;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 auto reply, code=%d, exists=%d, allowed=%d.", pResult->m_code,
                     pResult->m_exists ? 1 : 0, pResult->m_allowed ? 1 : 0);
            ::PostMessage(hUI, FMG_MSG_V2_AUTO_REPLY, 0, (LPARAM)pResult);
        }
        else
        {
            delete pResult;
        }
        break;
    }

    case cmd_set_auto_reply:
    {
        //保存自动回复设置的结果：wParam 为结果码，失败时主面板提示原因
        int code = -1;
        std::string msg;
        net::BotProtocolClient::parseSimpleResponse(body, code, msg);
        HWND hUI = GetUIProxyWnd();
        LOG_INFO("Recv v2 set auto reply, code=%d, msg=%s.", code, msg.c_str());
        if (::IsWindow(hUI))
            ::PostMessage(hUI, FMG_MSG_V2_SET_AUTO_REPLY_RESULT, (WPARAM)code, 0);
        break;
    }

    case cmd_group_qa_changed_notify:
    {
        //本群问答有增删改：只带群ID，打开着「问答」页签的群聊窗收到后重拉
        int64_t groupId = 0;
        if (!net::GroupQaProtocolClient::parseChangedNotify(body, groupId))
        {
            LOG_ERROR("Parse v2 group-qa changed notify failed, body=%s.", body.c_str());
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 group qa changed notify, groupid=%lld.", (long long)groupId);
            ::PostMessage(hUI, FMG_MSG_V2_GROUP_QA_CHANGED, 0, (LPARAM)new int64_t(groupId));
        }
        break;
    }

    case cmd_create_group_link:
    {
        //生成加群链接的应答：主面板转给打开着的「加群链接」窗口显示链接
        net::CreateGroupLinkResult* pResult = new net::CreateGroupLinkResult();
        if (!net::GroupLinkProtocolClient::parseCreateResponse(body, *pResult))
        {
            LOG_ERROR("Parse v2 create-group-link response failed, body=%s.", body.c_str());
            delete pResult;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 create-group-link ack, code=%d, groupid=%lld, id=%lld.", pResult->m_code,
                     (long long)pResult->m_link.m_groupid, (long long)pResult->m_link.m_id);
            ::PostMessage(hUI, FMG_MSG_V2_GROUP_LINK_CREATED, 0, (LPARAM)pResult);
        }
        else
        {
            delete pResult;
        }
        break;
    }

    case cmd_get_group_links:
    {
        //本群加群链接列表的应答：主面板转给群管理窗的「加群链接」页
        net::GroupLinksResult* pResult = new net::GroupLinksResult();
        if (!net::GroupLinkProtocolClient::parseListResponse(body, *pResult))
        {
            LOG_ERROR("Parse v2 group-links response failed, bodylen=%u.", (uint32_t)body.size());
            delete pResult;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 group links, code=%d, groupid=%lld, count=%u.", pResult->m_code,
                     (long long)pResult->m_groupid, (uint32_t)pResult->m_links.size());
            ::PostMessage(hUI, FMG_MSG_V2_GROUP_LINKS, 0, (LPARAM)pResult);
        }
        else
        {
            delete pResult;
        }
        break;
    }

    case cmd_get_group_link_info:
    {
        //按随机码查群的应答：主面板据此弹出「加入群聊」确认框，或提示链接不可用
        net::GroupLinkInfoResult* pResult = new net::GroupLinkInfoResult();
        if (!net::GroupLinkProtocolClient::parseInfoResponse(body, *pResult))
        {
            LOG_ERROR("Parse v2 group-link-info response failed, body=%s.", body.c_str());
            delete pResult;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 group link info, code=%d, groupid=%lld, joinmode=%d, member=%d.", pResult->m_code,
                     (long long)pResult->m_groupid, pResult->m_joinmode, pResult->m_member ? 1 : 0);
            ::PostMessage(hUI, FMG_MSG_V2_GROUP_LINK_INFO, 0, (LPARAM)pResult);
        }
        else
        {
            delete pResult;
        }
        break;
    }

    case cmd_join_group_by_link:
    {
        //凭链接加群的应答：主面板据 joined 打开群聊窗或提示已提交加群申请
        net::JoinGroupByLinkResult* pResult = new net::JoinGroupByLinkResult();
        if (!net::GroupLinkProtocolClient::parseJoinResponse(body, *pResult))
        {
            LOG_ERROR("Parse v2 join-group-by-link response failed, body=%s.", body.c_str());
            delete pResult;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 join-group-by-link ack, code=%d, groupid=%lld, joined=%d.", pResult->m_code,
                     (long long)pResult->m_groupid, pResult->m_joined ? 1 : 0);
            ::PostMessage(hUI, FMG_MSG_V2_JOIN_GROUP_BY_LINK, 0, (LPARAM)pResult);
        }
        else
        {
            delete pResult;
        }
        break;
    }

    case cmd_group_announcement_notify:
    {
        //推送体里带完整公告，界面收到即可直接刷新列表 / 弹窗，不必再拉一次
        net::GroupAnnouncementNotify* pNotify = new net::GroupAnnouncementNotify();
        if (!net::GroupProtocolClient::parseAnnouncementNotify(body, *pNotify))
        {
            LOG_ERROR("Parse v2 group-announcement notify failed, bodylen=%u.", (uint32_t)body.size());
            delete pNotify;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 group announcement notify, groupid=%lld, changetype=%d, id=%lld.",
                     (long long)pNotify->m_groupid, pNotify->m_changetype,
                     (long long)pNotify->m_announcement.m_id);
            ::PostMessage(hUI, FMG_MSG_V2_ANNOUNCEMENT_CHANGED, 0, (LPARAM)pNotify);
        }
        else
        {
            delete pNotify;
        }
        break;
    }

    case cmd_group_member_changed_notify:
    {
        //群成员变动推送：加入 / 退出 / 被踢 / 角色变更 / 禁言变更 / 群名片变更。
        //操作者本人也会收到自己触发的那一条，界面侧按幂等处理即可。
        net::GroupMemberChangedNotify* pNotify = new net::GroupMemberChangedNotify();
        if (!net::GroupProtocolClient::parseGroupMemberChangedNotify(body, *pNotify))
        {
            LOG_ERROR("Parse v2 group-member-changed notify failed, bodylen=%u.", (uint32_t)body.size());
            delete pNotify;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 group member changed, groupid=%lld, changetype=%d, count=%u, operator=%lld.",
                     (long long)pNotify->m_groupid, pNotify->m_changetype,
                     (uint32_t)pNotify->m_userids.size(), (long long)pNotify->m_operatorid);
            ::PostMessage(hUI, FMG_MSG_V2_GROUP_MEMBER_CHANGED, 0, (LPARAM)pNotify);
        }
        else
        {
            delete pNotify;
        }
        break;
    }

    case cmd_group_info_changed_notify:
    {
        //群资料变更推送。changetype=2 表示群已被解散，界面须关掉相关窗口。
        net::GroupInfoChangedNotify* pNotify = new net::GroupInfoChangedNotify();
        if (!net::GroupProtocolClient::parseGroupInfoChangedNotify(body, *pNotify))
        {
            LOG_ERROR("Parse v2 group-info-changed notify failed, bodylen=%u.", (uint32_t)body.size());
            delete pNotify;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 group info changed, groupid=%lld, changetype=%d.",
                     (long long)pNotify->m_info.m_groupid, pNotify->m_changetype);
            ::PostMessage(hUI, FMG_MSG_V2_GROUP_INFO_CHANGED, 0, (LPARAM)pNotify);
        }
        else
        {
            delete pNotify;
        }
        break;
    }

    case cmd_join_group_request_notify:
    {
        //有人申请加群：仅群主与管理员会收到，用于让审批入口出现红点
        net::GroupJoinRequestItem* pNotify = new net::GroupJoinRequestItem();
        if (!net::GroupProtocolClient::parseJoinGroupRequestNotify(body, *pNotify))
        {
            LOG_ERROR("Parse v2 join-group-request notify failed, bodylen=%u.", (uint32_t)body.size());
            delete pNotify;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 join group request, groupid=%lld, requestid=%lld, from=%lld.",
                     (long long)pNotify->m_groupid, (long long)pNotify->m_requestid,
                     (long long)pNotify->m_userid);
            ::PostMessage(hUI, FMG_MSG_V2_JOIN_GROUP_REQUEST, 0, (LPARAM)pNotify);
        }
        else
        {
            delete pNotify;
        }
        break;
    }

    case cmd_get_group_info:
    {
        //群资料应答：解析后投递到 UI 线程，由主面板写群信息缓存、刷新群名。
        net::GroupInfoResult* pResult = new net::GroupInfoResult();
        if (!net::GroupProtocolClient::parseGroupInfoResponse(body, *pResult))
        {
            LOG_ERROR("Parse v2 group-info response failed, bodylen=%u.", (uint32_t)body.size());
            delete pResult;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 group info, code=%d, groupid=%lld, members=%d.",
                     pResult->m_code, (long long)pResult->m_groupid, pResult->m_membercount);
            ::PostMessage(hUI, FMG_MSG_V2_GROUP_INFO, 0, (LPARAM)pResult);
        }
        else
        {
            delete pResult;
        }
        break;
    }

    case cmd_get_group_members:
    {
        //群成员应答：解析后投递到 UI 线程，由群聊窗填成员侧栏、建发送人映射。
        net::GroupMembersResult* pResult = new net::GroupMembersResult();
        if (!net::GroupProtocolClient::parseGroupMembersResponse(body, *pResult))
        {
            LOG_ERROR("Parse v2 group-members response failed, bodylen=%u.", (uint32_t)body.size());
            delete pResult;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 group members, code=%d, groupid=%lld, count=%u.",
                     pResult->m_code, (long long)pResult->m_groupid,
                     (uint32_t)pResult->m_members.size());
            ::PostMessage(hUI, FMG_MSG_V2_GROUP_MEMBERS, 0, (LPARAM)pResult);
        }
        else
        {
            delete pResult;
        }
        break;
    }

    case cmd_conversation_changed_notify:
    {
        //会话属性多端同步推送：某一端改了置顶 / 免打扰 / 删除，服务端把改完后的完整状态
        //推给该用户的全部在线端（含发起端自己）。解析后投到 UI 线程，由主面板照它覆盖
        //本地会话缓存并刷新列表。堆对象由 UI 处理后 delete。
        net::ConversationChangedNotify* pNotify = new net::ConversationChangedNotify();
        if (!net::ConversationsProtocolClient::parseConversationChangedNotify(body, *pNotify))
        {
            LOG_ERROR("Parse v2 conversation-changed notify failed, bodylen=%u.", (uint32_t)body.size());
            delete pNotify;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 conversation-changed notify, ctype=%d, cid=%lld, pinned=%d, notifymode=%d, deleted=%d, unreadmark=%d.",
                     pNotify->m_ctype, (long long)pNotify->m_cid,
                     pNotify->m_pinned, pNotify->m_notifyMode, pNotify->m_deleted, (int)pNotify->m_unreadMark);
            ::PostMessage(hUI, FMG_MSG_V2_CONVERSATION_CHANGED, 0, (LPARAM)pNotify);
        }
        else
        {
            delete pNotify;
        }
        break;
    }

    case cmd_set_group_follow:
    {
        //设置群关注名单的应答：成功时界面不做任何事（名单以 1331 推送为准），失败时把结果码经
        //wParam 投到 UI，由主面板提示（docs/消息提醒模式与消息盒子方案.md §12）。没有堆负载
        int code = -1;
        std::string msg;
        if (!net::GroupProtocolClient::parseCodeMsgResponse(body, code, msg))
        {
            LOG_ERROR("Parse v2 set group follow ack failed, bodylen=%u.", (uint32_t)body.size());
            break;
        }
        if (code == 0)
        {
            LOG_INFO("Recv v2 set group follow ack ok.");
            break;
        }
        LOG_WARNING("Recv v2 set group follow ack failed, code=%d, msg=%s.", code, msg.c_str());
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            ::PostMessage(hUI, FMG_MSG_V2_SET_GROUP_FOLLOW_RESULT, (WPARAM)code, 0);
        }
        break;
    }

    case cmd_group_follow_changed_notify:
    {
        //本人在某个群里关注的成员变了（整份名单），多端同步用；发起设置的这一端也会收到。
        //解析后投到 UI 线程，由主面板覆盖缓存。堆对象由 UI 处理后 delete
        net::GroupFollowEntry* pNotify = new net::GroupFollowEntry();
        if (!net::ConversationsProtocolClient::parseGroupFollowChangedNotify(body, *pNotify))
        {
            LOG_ERROR("Parse v2 group follow changed notify failed, bodylen=%u.", (uint32_t)body.size());
            delete pNotify;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 group follow changed notify, groupid=%lld, count=%u.",
                     (long long)pNotify->m_groupid, (uint32_t)pNotify->m_targets.size());
            ::PostMessage(hUI, FMG_MSG_V2_GROUP_FOLLOW_CHANGED, 0, (LPARAM)pNotify);
        }
        else
        {
            delete pNotify;
        }
        break;
    }

    case cmd_set_notify_modes:
    {
        //批量设置提醒模式的应答：逐条结果码投到 UI 线程，由主面板对失败的条目提示原因。
        //改成功的条目另有 cmd_notify_modes_changed_notify 推送，界面改动以推送为准。
        //堆对象由 UI 处理后 delete
        net::SetNotifyModesResult* pResult = new net::SetNotifyModesResult();
        if (!net::ConversationsProtocolClient::parseSetNotifyModesResponse(body, *pResult))
        {
            LOG_ERROR("Parse v2 set notify modes ack failed, bodylen=%u.", (uint32_t)body.size());
            delete pResult;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 set notify modes ack, code=%d, msg=%s, results=%u.",
                     pResult->m_code, pResult->m_msg.c_str(), (uint32_t)pResult->m_results.size());
            ::PostMessage(hUI, FMG_MSG_V2_SET_NOTIFY_MODES_RESULT, 0, (LPARAM)pResult);
        }
        else
        {
            //UI 未就绪（理论上不会发生），丢弃以免泄漏
            delete pResult;
        }
        break;
    }

    case cmd_notify_modes_changed_notify:
    {
        //本人一批会话的提醒模式变了（批量设置成功的条目），多端同步用；发起批量设置的
        //这一端也会收到。解析后投到 UI 线程，由主面板覆盖模式表并刷新列表。
        //堆对象由 UI 处理后 delete
        net::NotifyModesChangedNotify* pNotify = new net::NotifyModesChangedNotify();
        if (!net::ConversationsProtocolClient::parseNotifyModesChangedNotify(body, *pNotify))
        {
            LOG_ERROR("Parse v2 notify modes changed notify failed, bodylen=%u.", (uint32_t)body.size());
            delete pNotify;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 notify modes changed notify, items=%u.", (uint32_t)pNotify->m_items.size());
            ::PostMessage(hUI, FMG_MSG_V2_NOTIFY_MODES_CHANGED, 0, (LPARAM)pNotify);
        }
        else
        {
            delete pNotify;
        }
        break;
    }

    case cmd_set_conversation:
    case cmd_set_member_card:
    case cmd_leave_group:
    {
        //群资料对话框三个写操作（置顶/免打扰、改群名片、退群）的通用 {code,msg} 应答。
        //解析后把 code 经 wParam、msg 经堆上 std::string 投到 UI 线程，由主面板记日志、
        //失败时给用户一个通用提示。乐观更新在发起端已生效，本路径只做结果反馈。
        int code = -1;
        std::string msg;
        if (!net::GroupProtocolClient::parseCodeMsgResponse(body, code, msg))
        {
            LOG_ERROR("Parse v2 write-op response failed, cmd=%u, bodylen=%u.",
                      (uint32_t)cmd, (uint32_t)body.size());
            break;
        }
        UINT uiMsg = FMG_MSG_V2_SET_CONVERSATION_RESULT;
        if (cmd == cmd_set_member_card)
        {
            uiMsg = FMG_MSG_V2_SET_MEMBER_CARD_RESULT;
        }
        else if (cmd == cmd_leave_group)
        {
            uiMsg = FMG_MSG_V2_LEAVE_GROUP_RESULT;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 group write-op result, cmd=%u, code=%d.",
                     (uint32_t)cmd, code);
            ::PostMessage(hUI, uiMsg, (WPARAM)code, (LPARAM)(new std::string(msg)));
        }
        break;
    }

    case cmd_set_favorite:
    {
        //「我的收藏」加入 / 取消的应答。与上面那组通用 {code,msg} 不同，本应答还原样
        //回带 ctype/cid/favorite —— 用户连点几个人时会有多条请求在途，不带这三项就
        //对不上号、失败时也没法回滚到正确的那一条，故单独走一条投递路径。
        net::SetFavoriteResult* pResult = new net::SetFavoriteResult();
        if (!net::FavoriteProtocolClient::parseSetFavoriteResponse(body, *pResult))
        {
            LOG_ERROR("Parse v2 set favorite response failed, bodylen=%u.",
                      (uint32_t)body.size());
            delete pResult;
            break;
        }

        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 set favorite result, code=%d, ctype=%d, cid=%lld, favorite=%d.",
                     pResult->m_code, pResult->m_ctype,
                     (long long)pResult->m_cid, pResult->m_favorite);
            ::PostMessage(hUI, FMG_MSG_V2_SET_FAVORITE_RESULT, 0, (LPARAM)pResult);
        }
        else
        {
            delete pResult;
        }
        break;
    }

    //设置联系人标签的应答。除 userid 外还回带服务端<u>归一后</u>的最终列表（失败时
    //回带服务端当前实际存着的那一份），主面板两种情形下都直接拿它覆盖本地缓存，
    //不必自己维护回滚快照、也不必猜服务端把哪些标签去重或去空白了。
    case cmd_set_contact_tags:
    {
        net::SetContactTagsResult* pResult = new net::SetContactTagsResult();
        if (!net::ContactTagProtocolClient::parseSetContactTagsResponse(body, *pResult))
        {
            LOG_ERROR("Parse v2 set contact tags response failed, bodylen=%u.",
                      (uint32_t)body.size());
            delete pResult;
            break;
        }

        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 set contact tags result, code=%d, userid=%lld, tagcount=%u.",
                     pResult->m_code, (long long)pResult->m_userid,
                     (uint32_t)pResult->m_tags.size());
            ::PostMessage(hUI, FMG_MSG_V2_SET_TAGS_RESULT, 0, (LPARAM)pResult);
        }
        else
        {
            delete pResult;
        }
        break;
    }

    case cmd_set_friend_remark:
    {
        //设置好友备注名的应答。与通用 {code,msg} 不同，本应答还原样回带 userid ——
        //用户连着改几个人的备注时会有多条请求在途，不带它就对不上号，失败时会把
        //别人的备注退回去，故单独走一条投递路径（口径同上面的 cmd_set_favorite）。
        net::SetFriendRemarkResult* pResult = new net::SetFriendRemarkResult();
        if (!net::FriendProtocolClient::parseSetFriendRemarkResponse(body, *pResult))
        {
            LOG_ERROR("Parse v2 set friend remark response failed, bodylen=%u.",
                      (uint32_t)body.size());
            delete pResult;
            break;
        }

        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 set friend remark result, code=%d, userid=%lld.",
                     pResult->m_code, (long long)pResult->m_userid);
            ::PostMessage(hUI, FMG_MSG_V2_SET_REMARK_RESULT, 0, (LPARAM)pResult);
        }
        else
        {
            delete pResult;
        }
        break;
    }

    case cmd_get_announcements:
    {
        //平台公告列表应答（公告中心的数据源）。解析后投递到 UI 线程，由主面板逐条
        //落本地库 announce.db、刷新铃铛角标；hasmore 为 true 时主面板会接着再拉一批。
        //注意与群公告的 cmd_get_group_announcements 不是一回事。
        net::AnnouncementsResult* pResult = new net::AnnouncementsResult();
        if (!net::AnnounceProtocolClient::parseGetAnnouncementsResponse(body, *pResult))
        {
            LOG_ERROR("Parse v2 get announcements response failed, bodylen=%u.",
                      (uint32_t)body.size());
            delete pResult;
            break;
        }

        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 announcements, code=%d, count=%u, hasmore=%d.",
                     pResult->m_code, (uint32_t)pResult->m_items.size(),
                     (int)pResult->m_hasMore);
            ::PostMessage(hUI, FMG_MSG_V2_ANNOUNCEMENTS, 0, (LPARAM)pResult);
        }
        else
        {
            //UI 未就绪（理论上不会发生），丢弃以免泄漏
            delete pResult;
        }
        break;
    }

    case cmd_mark_announcement_read:
    {
        //平台公告已读上报的应答。成功时界面不需要动（已读在发起时就乐观落了本地库），
        //应答只用于失败时留痕 / 后续重试，故仍要投给 UI。应答原样回带 announcementid，
        //用户连点几条时靠它对上号。
        net::AnnounceMarkReadResult* pResult = new net::AnnounceMarkReadResult();
        if (!net::AnnounceProtocolClient::parseMarkReadResponse(body, *pResult))
        {
            LOG_ERROR("Parse v2 mark-announcement-read response failed, bodylen=%u.",
                      (uint32_t)body.size());
            delete pResult;
            break;
        }

        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 mark announcement read result, code=%d, announcementid=%lld.",
                     pResult->m_code, (long long)pResult->m_announcementId);
            ::PostMessage(hUI, FMG_MSG_V2_ANN_MARK_READ_RESULT, 0, (LPARAM)pResult);
        }
        else
        {
            delete pResult;
        }
        break;
    }

    case cmd_announcement_notify:
    {
        //平台公告推送：changetype=1 新发布（带完整内容），=2 已撤回（只带公告ID）。
        //解析后投递到 UI 线程，由主面板落本地库、刷新铃铛角标与公告中心窗口。
        net::AnnouncementNotify* pNotify = new net::AnnouncementNotify();
        if (!net::AnnounceProtocolClient::parseAnnouncementNotify(body, *pNotify))
        {
            LOG_ERROR("Parse v2 announcement notify failed, bodylen=%u.",
                      (uint32_t)body.size());
            delete pNotify;
            break;
        }

        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 announcement notify, changetype=%d, announcementid=%lld.",
                     pNotify->m_changeType, (long long)pNotify->m_item.m_announcementId);
            ::PostMessage(hUI, FMG_MSG_V2_ANNOUNCEMENT_NOTIFY, 0, (LPARAM)pNotify);
        }
        else
        {
            delete pNotify;
        }
        break;
    }

    //备忘与提醒：拉取本人可见的全部备忘的应答
    case cmd_memo_list:
    {
        //备忘列表应答：本人可见的全部备忘。投给 UI 后由备忘中枢整体替换备忘簿、补出错过的提醒
        net::MemoListResult* pResult = new net::MemoListResult();
        if (!net::MemoProtocolClient::parseListResponse(body, *pResult))
        {
            LOG_ERROR("Parse v2 memo list response failed, bodylen=%u.", (uint32_t)body.size());
            delete pResult;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 memo list, code=%d, count=%u.", pResult->m_code, (uint32_t)pResult->m_memos.size());
            ::PostMessage(hUI, FMG_MSG_V2_MEMO_LIST, 0, (LPARAM)pResult);
        }
        else
        {
            delete pResult;
        }
        break;
    }

    //备忘与提醒：新建或修改备忘的应答
    case cmd_memo_save:
    {
        //保存备忘的应答：成功时带回服务端保存后的完整备忘
        net::MemoSaveResult* pResult = new net::MemoSaveResult();
        if (!net::MemoProtocolClient::parseSaveResponse(body, *pResult))
        {
            LOG_ERROR("Parse v2 memo save response failed, bodylen=%u.", (uint32_t)body.size());
            delete pResult;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 memo save result, code=%d, memoid=%lld.", pResult->m_code,
                     (long long)(pResult->m_hasMemo ? pResult->m_memo.m_memoid : 0));
            ::PostMessage(hUI, FMG_MSG_V2_MEMO_SAVE_RESULT, 0, (LPARAM)pResult);
        }
        else
        {
            delete pResult;
        }
        break;
    }

    //备忘与提醒：删除备忘的应答
    case cmd_memo_delete:
    {
        //删除备忘的应答，原样回带 memoid
        net::MemoDeleteResult* pResult = new net::MemoDeleteResult();
        if (!net::MemoProtocolClient::parseDeleteResponse(body, *pResult))
        {
            LOG_ERROR("Parse v2 memo delete response failed, bodylen=%u.", (uint32_t)body.size());
            delete pResult;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 memo delete result, code=%d, memoid=%lld.", pResult->m_code, (long long)pResult->m_memoid);
            ::PostMessage(hUI, FMG_MSG_V2_MEMO_DELETE_RESULT, 0, (LPARAM)pResult);
        }
        else
        {
            delete pResult;
        }
        break;
    }

    //备忘与提醒：备忘新增、修改或删除的推送
    case cmd_memo_notify:
    {
        //备忘推送：changetype=1 新增或修改（带完整备忘），=2 删除或从此看不到（只带 memoid）
        net::MemoNotify* pNotify = new net::MemoNotify();
        if (!net::MemoProtocolClient::parseNotify(body, *pNotify))
        {
            LOG_ERROR("Parse v2 memo notify failed, bodylen=%u.", (uint32_t)body.size());
            delete pNotify;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 memo notify, changetype=%d, memoid=%lld.", pNotify->m_changeType,
                     (long long)pNotify->m_memoid);
            ::PostMessage(hUI, FMG_MSG_V2_MEMO_NOTIFY, 0, (LPARAM)pNotify);
        }
        else
        {
            delete pNotify;
        }
        break;
    }

    case cmd_global_search:
    {
        //全局搜索的应答：解析后投递到 UI 线程，由主面板转给搜索下拉显示在「消息」标签末尾。
        net::GlobalSearchResult* pResult = new net::GlobalSearchResult();
        if (!net::GlobalSearchProtocolClient::parseResponse(body, *pResult))
        {
            LOG_ERROR("Parse v2 global-search response failed, bodylen=%u.", (uint32_t)body.size());
            delete pResult;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 global search, code=%d, keyword bytes=%u, conversations=%u, announcements=%u.",
                     pResult->m_code, (uint32_t)pResult->m_keyword.size(),
                     (uint32_t)pResult->m_conversations.size(), (uint32_t)pResult->m_announcements.size());
            ::PostMessage(hUI, FMG_MSG_V2_GLOBAL_SEARCH, 0, (LPARAM)pResult);
        }
        else
        {
            delete pResult;
        }
        break;
    }

    case cmd_search_group_files:
    {
        //按文件名搜索群文件的应答：解析后投递到 UI 线程，由主面板转给搜索下拉显示在「文件」标签里。
        net::SearchGroupFilesResult* pResult = new net::SearchGroupFilesResult();
        if (!net::GroupFileProtocolClient::parseSearchGroupFilesResponse(body, *pResult))
        {
            LOG_ERROR("Parse v2 search-group-files response failed, bodylen=%u.", (uint32_t)body.size());
            delete pResult;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 search group files, code=%d, keyword bytes=%u, hasmore=%d, count=%u.",
                     pResult->m_code, (uint32_t)pResult->m_keyword.size(),
                     pResult->m_hasmore ? 1 : 0, (uint32_t)pResult->m_files.size());
            ::PostMessage(hUI, FMG_MSG_V2_SEARCH_GROUP_FILES, 0, (LPARAM)pResult);
        }
        else
        {
            delete pResult;
        }
        break;
    }

    case cmd_get_group_files:
    {
        //群文件列表应答：解析后投递到 UI 线程，由群聊窗按 groupid 累积分页、拉全后渲染。
        net::GetGroupFilesResult* pResult = new net::GetGroupFilesResult();
        if (!net::GroupFileProtocolClient::parseGetGroupFilesResponse(body, *pResult))
        {
            LOG_ERROR("Parse v2 group-files response failed, bodylen=%u.", (uint32_t)body.size());
            delete pResult;
            break;
        }
        //服务端原样带回请求的序号，主面板据此把应答交给发出请求的那一方
        pResult->m_seq = seq;
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 group files, code=%d, groupid=%s, total=%d, hasmore=%d, count=%u, seq=%u.",
                     pResult->m_code, pResult->m_groupid.c_str(), pResult->m_total,
                     pResult->m_hasmore ? 1 : 0, (uint32_t)pResult->m_files.size(), seq);
            ::PostMessage(hUI, FMG_MSG_V2_GROUP_FILES, 0, (LPARAM)pResult);
        }
        else
        {
            delete pResult;
        }
        break;
    }

    case cmd_group_files_changed_notify:
    {
        //群文件变更推送：解析后投递到 UI 线程，由群聊窗按 groupid 增量增/删并重渲染。
        net::GroupFilesChangedNotify* pNotify = new net::GroupFilesChangedNotify();
        if (!net::GroupFileProtocolClient::parseGroupFilesChangedNotify(body, *pNotify))
        {
            LOG_ERROR("Parse v2 group-files-changed notify failed, bodylen=%u.", (uint32_t)body.size());
            delete pNotify;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 group-files-changed notify, groupid=%s, changetype=%d.",
                     pNotify->m_groupid.c_str(), pNotify->m_changetype);
            ::PostMessage(hUI, FMG_MSG_V2_GROUP_FILES_CHANGED, 0, (LPARAM)pNotify);
        }
        else
        {
            delete pNotify;
        }
        break;
    }

    case cmd_add_group_file:
    {
        //群文件登记应答：仅作日志。库的实际更新靠登记成功后服务端推的 1806（含本人），
        //而本应答不含 groupid、无从按群路由，故不单独投递 UI。
        net::AddGroupFileResult r;
        if (net::GroupFileProtocolClient::parseAddGroupFileResponse(body, r))
        {
            if (r.m_code == 0)
                LOG_INFO("Recv v2 add-group-file ok, entryid=%s, name=%s.",
                         r.m_file.m_entryid.c_str(), r.m_file.m_name.c_str());
            else
                LOG_ERROR("v2 add-group-file rejected, code=%d, msg=%s.",
                          r.m_code, r.m_msg.c_str());
        }
        break;
    }

    case cmd_delete_group_file:
    {
        //群文件删除应答：仅作日志。删除成功后服务端推 1806(removed)，各端据此把文件移出
        //群文件库（含本人），故本应答不单独投递 UI。客户端已按同规则做了权限门控，失败
        //一般是竞态(已删 / 角色变更)，记日志便于排查。
        net::DeleteGroupFileResult r;
        if (net::GroupFileProtocolClient::parseDeleteGroupFileResponse(body, r))
        {
            if (r.m_code == 0)
                LOG_INFO("Recv v2 delete-group-file ok, entryid=%s.", r.m_entryid.c_str());
            else
                LOG_ERROR("v2 delete-group-file rejected, code=%d, msg=%s.",
                          r.m_code, r.m_msg.c_str());
        }
        break;
    }

    case cmd_get_user_groups:
    {
        //我的群列表应答：解析后投递到 UI 线程，填"群"Tab（含无消息空群）。
        net::UserGroupsResult* pResult = new net::UserGroupsResult();
        if (!net::GroupProtocolClient::parseUserGroupsResponse(body, *pResult))
        {
            LOG_ERROR("Parse v2 user-groups response failed, bodylen=%u.", (uint32_t)body.size());
            delete pResult;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 user groups, code=%d, count=%u.",
                     pResult->m_code, (uint32_t)pResult->m_groups.size());
            ::PostMessage(hUI, FMG_MSG_V2_USER_GROUPS, 0, (LPARAM)pResult);
        }
        else
        {
            delete pResult;
        }
        break;
    }

    case cmd_sync_messages:
    {
        //历史消息应答：解析后投递到 UI 线程，由主面板路由到对应聊天窗渲染历史。
        net::SyncMessagesResult* pResult = new net::SyncMessagesResult();
        if (!net::MessageProtocolClient::parseSyncMessagesResponse(body, *pResult))
        {
            LOG_ERROR("Parse v2 sync-messages response failed, bodylen=%u.", (uint32_t)body.size());
            delete pResult;
            break;
        }
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
        {
            LOG_INFO("Recv v2 sync messages, code=%d, ctype=%d, cid=%lld, count=%u.",
                     pResult->m_code, pResult->m_ctype, (long long)pResult->m_cid,
                     (uint32_t)pResult->m_messages.size());
            ::PostMessage(hUI, FMG_MSG_V2_SYNC_MESSAGES, 0, (LPARAM)pResult);
        }
        else
        {
            delete pResult;
        }
        break;
    }

    case cmd_sync_rewritten_messages:
    {
        //按改写序号同步的应答：解析后投递到 UI 线程，由主面板覆盖本地库里已有的行、推进水位并续拉
        net::SyncRewrittenResult* pResult = new net::SyncRewrittenResult();
        if (!net::MessageProtocolClient::parseSyncRewrittenResponse(body, *pResult))
        {
            LOG_ERROR("Parse v2 sync-rewritten response failed, bodylen=%u.", (uint32_t)body.size());
            delete pResult;
            break;
        }
        LOG_INFO("Recv v2 sync rewritten messages, code=%d, items=%u, rev=%lld, more=%d, currev=%lld.",
                 pResult->m_code, (uint32_t)pResult->m_items.size(), (long long)pResult->m_rev,
                 pResult->m_more ? 1 : 0, (long long)pResult->m_currev);
        HWND hUI = GetUIProxyWnd();
        if (::IsWindow(hUI))
            ::PostMessage(hUI, FMG_MSG_V2_SYNC_REWRITTEN, 0, (LPARAM)pResult);
        else
            delete pResult;
        break;
    }

    default:
        //其余 v2 cmd 属后续阶段功能、尚未迁移，仅记录不处理
        LOG_INFO("Recv unhandled v2 frame, cmd=%u (feature not migrated yet).", cmd);
        break;
    }
}

void CIUSocket::RequestConversations()
{
    //无请求体（服务端 onSyncConversations 不读 body），发空对象占位即可。
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_sync_conversations, seq, "{}");
    LOG_INFO("Request v2 conversations (cmd_sync_conversations).");
}

void CIUSocket::ReportConversationRead(int ctype, int64_t cid, int64_t readmsgid)
{
    //已读位点须为正：readmsgid<=0（会话还没有任何已知消息）时无可上报，直接跳过，
    //避免发出服务端会判为非法（parseMarkReadRequest 对 cid/readmsgid<=0 返回 false）的包。
    if (cid <= 0 || readmsgid <= 0)
    {
        return;
    }

    std::string body = net::MessageProtocolClient::buildMarkReadRequest(ctype, cid, readmsgid);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_mark_read, seq, body);
    LOG_INFO("Report v2 conversation read (cmd_mark_read), ctype=%d, cid=%lld, readmsgid=%lld.",
             ctype, (long long)cid, (long long)readmsgid);
}

void CIUSocket::RecallMessage(int ctype, int64_t cid, int64_t msgid)
{
    //cid / msgid 非正时 buildRecallMessageRequest 返回空串，说明这个请求服务端必判非法，
    //直接放弃发送（典型：本端刚发出、msgid 还没回填就点了撤回）。
    std::string body = net::MessageProtocolClient::buildRecallMessageRequest(ctype, cid, msgid);
    if (body.empty())
    {
        LOG_ERROR("Skip v2 recall-message: invalid ctype=%d, cid=%lld, msgid=%lld.",
                  ctype, (long long)cid, (long long)msgid);
        return;
    }

    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_recall_message, seq, body);
    LOG_INFO("Request v2 recall-message (cmd_recall_message), ctype=%d, cid=%lld, msgid=%lld.",
             ctype, (long long)cid, (long long)msgid);
}

void CIUSocket::DeleteMessage(int64_t msgid)
{
    //msgid 非正时 buildDeleteMessageRequest 返回空串：服务端必判非法，直接放弃发送
    std::string body = net::MessageProtocolClient::buildDeleteMessageRequest(msgid);
    if (body.empty())
    {
        LOG_ERROR("Skip v2 delete-message: invalid msgid=%lld.", (long long)msgid);
        return;
    }

    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_delete_message, seq, body);
    LOG_INFO("Request v2 delete-message (cmd_delete_message), msgid=%lld, seq=%u.",
             (long long)msgid, seq);
}

bool CIUSocket::DeleteMessagesByTime(int64_t begintime, int64_t endtime)
{
    //开始为负或结束早于开始时返回空串：服务端必判非法，直接放弃发送
    std::string body = net::MessageProtocolClient::buildDeleteMessagesByTimeRequest(begintime, endtime);
    if (body.empty())
    {
        LOG_ERROR("Skip v2 delete-messages-by-time: invalid range [%lld, %lld].",
                  (long long)begintime, (long long)endtime);
        return false;
    }

    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_delete_messages_by_time, seq, body);
    LOG_INFO("Request v2 delete-messages-by-time (cmd_delete_messages_by_time), range=[%lld, %lld], seq=%u.",
             (long long)begintime, (long long)endtime, seq);
    return true;
}

uint32_t CIUSocket::ClearConversation(int ctype, int64_t cid)
{
    //会话类型不是 1 / 2 或会话ID非正时 buildClearConversationRequest 返回空串：服务端必判非法，直接放弃发送
    std::string body = net::MessageProtocolClient::buildClearConversationRequest(ctype, cid);
    if (body.empty())
    {
        LOG_ERROR("Skip v2 clear-conversation: invalid ctype=%d, cid=%lld.", ctype, (long long)cid);
        return 0;
    }

    //EnqueueFrame 在离线或未连接时丢弃帧且不告知调用方；这里提前判断，返回 0 让调用方
    //立即按失败处理，而不是一直等一个不会到来的应答
    if (IsUserOffline() || IsClosed())
    {
        LOG_ERROR("Skip v2 clear-conversation: not connected, ctype=%d, cid=%lld.", ctype, (long long)cid);
        return 0;
    }

    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_clear_conversation, seq, body);
    LOG_INFO("Request v2 clear-conversation (cmd_clear_conversation), ctype=%d, cid=%lld, seq=%u.",
             ctype, (long long)cid, seq);
    return seq;
}

void CIUSocket::PinMessage(int ctype, int64_t cid, int64_t msgid, bool pin)
{
    //cid / msgid 非正时 buildPinMessageRequest 返回空串，说明这个请求服务端必判非法，
    //直接放弃发送（典型：本端刚发出、msgid 还没回填就点了 Pin）。
    std::string body = net::MessageProtocolClient::buildPinMessageRequest(ctype, cid, msgid, pin);
    if (body.empty())
    {
        LOG_ERROR("Skip v2 pin-message: invalid ctype=%d, cid=%lld, msgid=%lld, pin=%d.",
                  ctype, (long long)cid, (long long)msgid, pin ? 1 : 0);
        return;
    }

    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_pin_message, seq, body);
    LOG_INFO("Request v2 pin-message (cmd_pin_message), ctype=%d, cid=%lld, msgid=%lld, pin=%d.",
             ctype, (long long)cid, (long long)msgid, pin ? 1 : 0);
}

bool CIUSocket::EnqueueRelayRequest(uint16_t cmd, const char* opName, int ctype, int64_t cid,
                                    int64_t msgid, const std::string& body)
{
    //请求拼不出来：入参不合契约，服务端必判非法，直接放弃发送
    if (body.empty())
    {
        LOG_ERROR("Skip v2 %s: invalid ctype=%d, cid=%lld, msgid=%lld.",
                  opName, ctype, (long long)cid, (long long)msgid);
        return false;
    }

    //EnqueueFrame 在离线或未连接时丢弃帧且不告知调用方；这里提前判断并返回 false，
    //让界面立即提示失败，而不是一直等一个不会到来的应答
    if (IsUserOffline() || IsClosed())
    {
        LOG_ERROR("Skip v2 %s: not connected, ctype=%d, cid=%lld, msgid=%lld.",
                  opName, ctype, (long long)cid, (long long)msgid);
        return false;
    }

    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd, seq, body);
    LOG_INFO("Request v2 %s, cmd=%u, ctype=%d, cid=%lld, msgid=%lld, seq=%u.",
             opName, (unsigned)cmd, ctype, (long long)cid, (long long)msgid, seq);
    return true;
}

bool CIUSocket::SendRelayCard(int ctype, int64_t cid, const std::string& clientMsgId,
                              const std::string& contentJson)
{
    //组 cmd_send_message 卡片请求体（msgtype=11）。回执与其它消息同路径，按 clientmsgid
    //回投到 UI（FMG_MSG_V2_SEND_CHATMSG_ACK）
    std::string body = net::MessageProtocolClient::buildSendCardRequest(
        ctype, cid, clientMsgId, message_type_relay, contentJson);
    if (body.empty())
    {
        LOG_ERROR("Skip v2 relay card: invalid ctype=%d, cid=%lld, clientmsgid=%s.",
                  ctype, (long long)cid, clientMsgId.c_str());
        return false;
    }
    if (IsUserOffline() || IsClosed())
    {
        LOG_ERROR("Skip v2 relay card: not connected, ctype=%d, cid=%lld, clientmsgid=%s.",
                  ctype, (long long)cid, clientMsgId.c_str());
        return false;
    }

    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_send_message, seq, body);
    LOG_INFO("Send v2 relay card, ctype=%d, cid=%lld, clientmsgid=%s, seq=%u.",
             ctype, (long long)cid, clientMsgId.c_str(), seq);
    return true;
}

bool CIUSocket::JoinRelay(int ctype, int64_t cid, int64_t msgid, const std::string& text)
{
    //接龙文本不写进日志：它是用户填写的内容，日志里只记定位字段
    return EnqueueRelayRequest(cmd_join_relay, "join-relay", ctype, cid, msgid,
                               net::CardProtocolClient::buildJoinRelayRequest(ctype, cid, msgid, text));
}

bool CIUSocket::WithdrawRelay(int ctype, int64_t cid, int64_t msgid)
{
    return EnqueueRelayRequest(cmd_withdraw_relay, "withdraw-relay", ctype, cid, msgid,
                               net::CardProtocolClient::buildRelayTargetRequest(ctype, cid, msgid));
}

bool CIUSocket::CloseRelay(int ctype, int64_t cid, int64_t msgid)
{
    return EnqueueRelayRequest(cmd_close_relay, "close-relay", ctype, cid, msgid,
                               net::CardProtocolClient::buildRelayTargetRequest(ctype, cid, msgid));
}

uint32_t CIUSocket::ForwardMessage(const std::string& clientMsgId, int toctype, int64_t tocid,
                                   bool merge, const std::string& title,
                                   const std::vector<int64_t>& msgids)
{
    //参数非法时 buildForwardMessageRequest 返回空串，服务端必判非法，直接放弃发送
    std::string body = net::MessageProtocolClient::buildForwardMessageRequest(
        clientMsgId, toctype, tocid, merge, title, msgids);
    if (body.empty())
    {
        LOG_ERROR("Skip v2 forward-message: invalid request, toctype=%d, tocid=%lld, merge=%d, count=%u.",
                  toctype, (long long)tocid, merge ? 1 : 0, (unsigned)msgids.size());
        return 0;
    }

    //EnqueueFrame 在离线或未连接时丢弃帧且不告知调用方；这里提前判断，返回 0 让调用方
    //立即按失败处理，而不是一直等一个不会到来的应答
    if (IsUserOffline() || IsClosed())
    {
        LOG_ERROR("Skip v2 forward-message: not connected, toctype=%d, tocid=%lld, merge=%d, count=%u.",
                  toctype, (long long)tocid, merge ? 1 : 0, (unsigned)msgids.size());
        return 0;
    }

    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_forward_message, seq, body);
    LOG_INFO("Request v2 forward-message, toctype=%d, tocid=%lld, merge=%d, count=%u, clientmsgid=%s, seq=%u.",
             toctype, (long long)tocid, merge ? 1 : 0, (unsigned)msgids.size(),
             clientMsgId.c_str(), seq);
    return seq;
}

void CIUSocket::GetPinnedMessages(int ctype, int64_t cid)
{
    std::string body = net::MessageProtocolClient::buildGetPinnedMessagesRequest(ctype, cid);
    if (body.empty())
    {
        LOG_ERROR("Skip v2 get-pinned-messages: invalid ctype=%d, cid=%lld.",
                  ctype, (long long)cid);
        return;
    }

    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_get_pinned_messages, seq, body);
    LOG_INFO("Request v2 get-pinned-messages (cmd_get_pinned_messages), ctype=%d, cid=%lld.",
             ctype, (long long)cid);
}

void CIUSocket::ReportFileTransferAck(int64_t peerUserId, int64_t msgid, int state)
{
    //参数非法时 buildFileTransferAckRequest 返回空串，说明这个上报服务端必判非法，
    //直接放弃（典型：历史里没有 msgid 的旧文件消息，本就无从对回）。
    std::string body = net::MessageProtocolClient::buildFileTransferAckRequest(peerUserId, msgid, state);
    if (body.empty())
    {
        LOG_ERROR("Skip v2 file-transfer-ack: invalid peer=%lld, msgid=%lld, state=%d.",
                  (long long)peerUserId, (long long)msgid, state);
        return;
    }

    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_file_transfer_ack, seq, body);
    LOG_INFO("Report v2 file-transfer-ack, peer=%lld, msgid=%lld, state=%d.",
             (long long)peerUserId, (long long)msgid, state);
}

void CIUSocket::ReportTyping(int64_t peerUserId, int state)
{
    //参数非法时 buildTypingNotifyRequest 返回空串。这条提示可有可无、丢了也不影响
    //聊天，故只按 WARNING 记一笔（调用方本就应保证 peerUserId 有效）。
    std::string body = net::MessageProtocolClient::buildTypingNotifyRequest(peerUserId, state);
    if (body.empty())
    {
        LOG_WARNING("Skip v2 typing-notify: invalid peer=%lld, state=%d.",
                    (long long)peerUserId, state);
        return;
    }

    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_typing_notify, seq, body);
    //调用方已按 4 秒续发 / 停手才发 0 做过节流，这里的日志量不会大，留 INFO 便于
    //联调时对照两端时序
    LOG_INFO("Report v2 typing-notify, peer=%lld, state=%d.", (long long)peerUserId, state);
}

void CIUSocket::RequestContacts()
{
    //无请求体（服务端 onGetContacts 不读 body），发空对象占位即可。
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_get_contacts, seq, "{}");
    LOG_INFO("Request v2 contacts (cmd_get_contacts).");
}

void CIUSocket::SendChatTextMessage(int64_t peerUserId, const std::string& clientMsgId,
                                    const std::string& text, const net::TextFont& font,
                                    const net::ReplyQuote& quote, int textFormat,
                                    const std::string& codeLang,
                                    const std::vector<net::CodeNoteData>& codeNotes)
{
    //组装 cmd_send_message 请求体并编码成 v2 帧发出。seq 仅作帧序号，回执
    //的对回靠 clientmsgid 而非 seq，故此处取自增值即可。font.m_valid 为 true 时
    //把字体写进 content.font，使对端与历史看到相同字体效果；Markdown 消息与代码片段另带 content.format。
    std::string body = net::MessageProtocolClient::buildSendTextRequest(
        peerUserId, clientMsgId, text, font, quote, textFormat, codeLang, codeNotes);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_send_message, seq, body);
    LOG_INFO("Send v2 chat text message, to=%lld, clientmsgid=%s, format=%d, lang=%s, notes=%u, bodylen=%u.",
             (long long)peerUserId, clientMsgId.c_str(), textFormat, codeLang.c_str(), (uint32_t)codeNotes.size(),
             (uint32_t)body.size());
}

void CIUSocket::SendAutoReplyMessage(int64_t peerUserId, const std::string& clientMsgId,
                                     const std::string& replyText)
{
    //与 SendChatTextMessage 同一条发送路径，只是请求体换成带 autoreply 标记与前缀的那份
    std::string body = net::MessageProtocolClient::buildSendAutoReplyRequest(peerUserId, clientMsgId, replyText);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_send_message, seq, body);
    LOG_INFO("Send v2 auto-reply message, to=%lld, clientmsgid=%s, bodylen=%u.",
             (long long)peerUserId, clientMsgId.c_str(), (uint32_t)body.size());
}

uint32_t CIUSocket::SendFolderInvite(int64_t peerUserId, const std::string& folderName,
                                     int64_t totalDirs, int64_t totalFiles, int64_t totalSize)
{
    //发起文件夹传输邀请：应答（带 sessionid 或错误）经 cmd_folder_invite 回投
    //FMG_MSG_V2_FOLDER_INVITE_ACK，wParam 为本次的帧 seq。规模统计由调用方扫描本地目录树
    //得出，服务端据它与配置的上限比对；客户端不硬编码上限。
    std::string body = net::FolderProtocolClient::buildInviteRequest(
        peerUserId, folderName, totalDirs, totalFiles, totalSize);
    if (body.empty())
    {
        LOG_ERROR("Build v2 folder invite failed, to=%lld, dirs=%lld, files=%lld, bytes=%lld.",
                  (long long)peerUserId, (long long)totalDirs,
                  (long long)totalFiles, (long long)totalSize);
        return 0;
    }

    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_folder_invite, seq, body);
    LOG_INFO("Send v2 folder invite, to=%lld, dirs=%lld, files=%lld, bytes=%lld, seq=%u.",
             (long long)peerUserId, (long long)totalDirs,
             (long long)totalFiles, (long long)totalSize, seq);
    return seq;
}

uint32_t CIUSocket::SendFolderFileInvite(int64_t peerUserId, const std::string& fileName, int64_t fileSize)
{
    //单聊单文件直传邀请：与文件夹传输共用 1810，带 kind=1；应答同样按帧 seq 对回发起的那一笔
    std::string body = net::FolderProtocolClient::buildFileInviteRequest(peerUserId, fileName, fileSize);
    if (body.empty())
    {
        LOG_ERROR("Build v2 direct file invite failed, to=%lld, bytes=%lld.", (long long)peerUserId,
                  (long long)fileSize);
        return 0;
    }

    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_folder_invite, seq, body);
    LOG_INFO("Send v2 direct file invite, to=%lld, bytes=%lld, seq=%u.", (long long)peerUserId, (long long)fileSize,
             seq);
    return seq;
}

void CIUSocket::SendFolderResponse(const std::string& sessionId, bool accept)
{
    //接收方应答邀请：同意则双方收 cmd_folder_relay_info，拒绝则发送方收 cmd_folder_state_notify。
    std::string body = net::FolderProtocolClient::buildResponse(sessionId, accept);
    if (body.empty())
    {
        LOG_ERROR("Build v2 folder response failed, session=%s.", sessionId.c_str());
        return;
    }

    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_folder_response, seq, body);
    LOG_INFO("Send v2 folder response, session=%s, accept=%d.", sessionId.c_str(), accept ? 1 : 0);
}

void CIUSocket::SendFolderCancel(const std::string& sessionId)
{
    //发送方在接收方应答前撤回邀请。
    std::string body = net::FolderProtocolClient::buildCancel(sessionId);
    if (body.empty())
    {
        LOG_ERROR("Build v2 folder cancel failed, session=%s.", sessionId.c_str());
        return;
    }

    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_folder_cancel, seq, body);
    LOG_INFO("Send v2 folder cancel, session=%s.", sessionId.c_str());
}

void CIUSocket::SendFolderCancel(const std::string& sessionId, bool toOffline)
{
    if (!toOffline)
    {
        SendFolderCancel(sessionId);
        return;
    }

    //单文件直传的发送方改为上传服务器：服务端向接收方推「已改为离线发送」
    std::string body = net::FolderProtocolClient::buildCancel(sessionId, true);
    if (body.empty())
    {
        LOG_ERROR("Build v2 direct file cancel (to offline) failed, session=%s.", sessionId.c_str());
        return;
    }

    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_folder_cancel, seq, body);
    LOG_INFO("Send v2 direct file cancel, session=%s, tooffline=1.", sessionId.c_str());
}

void CIUSocket::SendFolderClose(const std::string& sessionId, bool success, int errCode, const std::string& errMsg,
                                int linkPath, const std::string& md5)
{
    std::string body = net::FolderProtocolClient::buildClose(sessionId, success, errCode, errMsg, linkPath, md5);
    if (body.empty())
    {
        LOG_ERROR("Build v2 folder close failed, session=%s, success=%d, errcode=%d, linkpath=%d.",
                  sessionId.c_str(), success ? 1 : 0, errCode, linkPath);
        return;
    }

    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_folder_close, seq, body);
    LOG_INFO("Send v2 folder close, session=%s, success=%d, errcode=%d, linkpath=%d, md5=%s, errmsg=%s.",
             sessionId.c_str(), success ? 1 : 0, errCode, linkPath, md5.c_str(), errMsg.c_str());
}

void CIUSocket::SendFolderClose(const std::string& sessionId, bool success,
                                int errCode, const std::string& errMsg)
{
    //任一端结束本次传输。失败结束时错误码由服务端透传给对端，两端看到同一条原因。
    std::string body = net::FolderProtocolClient::buildClose(sessionId, success, errCode, errMsg);
    if (body.empty())
    {
        LOG_ERROR("Build v2 folder close failed, session=%s, success=%d, errcode=%d.",
                  sessionId.c_str(), success ? 1 : 0, errCode);
        return;
    }

    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_folder_close, seq, body);
    LOG_INFO("Send v2 folder close, session=%s, success=%d, errcode=%d, errmsg=%s.",
             sessionId.c_str(), success ? 1 : 0, errCode, errMsg.c_str());
}

void CIUSocket::SendFolderRetry(const std::string& sessionId)
{
    //保留期内重试续传：服务端校验通过后重签令牌并再下发一次 cmd_folder_relay_info。
    std::string body = net::FolderProtocolClient::buildRetry(sessionId);
    if (body.empty())
    {
        LOG_ERROR("Build v2 folder retry failed, session=%s.", sessionId.c_str());
        return;
    }

    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_folder_retry, seq, body);
    LOG_INFO("Send v2 folder retry, session=%s.", sessionId.c_str());
}

bool CIUSocket::SendMediaSignal(uint16_t cmd, const std::string& body)
{
    //包体由 FlamingoMedia.dll 编好，这里只校验命令号是否在白名单内，防止 DLL 借主连接发出任意命令
    if (!net::IsMediaSignalCmd(cmd))
    {
        LOG_WARNING("Reject media signal: cmd=%u is not in the media whitelist.", static_cast<unsigned>(cmd));
        return false;
    }
    if (body.empty())
    {
        LOG_WARNING("Reject media signal: cmd=%u has an empty body.", static_cast<unsigned>(cmd));
        return false;
    }
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd, seq, body);
    LOG_INFO("Send v2 media signal, cmd=%u, seq=%u, len=%u.", static_cast<unsigned>(cmd), seq,
             static_cast<unsigned>(body.size()));
    return true;
}

uint32_t CIUSocket::SendShareCommand(uint16_t cmd, const std::string& body)
{
    //包体由 FlamingoFileShare.dll 编好，这里只按放行规则校验，防止 DLL 借主连接发出任意命令
    if (!balloonfileshare::ShareChannelPolicy::CanSendShareCommand(cmd, static_cast<uint32_t>(body.size()), m_bConnected))
    {
        LOG_WARNING("Reject share command: cmd=%u, len=%u, connected=%d.", static_cast<unsigned>(cmd),
                    static_cast<unsigned>(body.size()), m_bConnected ? 1 : 0);
        return 0;
    }
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd, seq, body);
    LOG_INFO("Send v2 share command, cmd=%u, seq=%u, len=%u.", static_cast<unsigned>(cmd), seq,
             static_cast<unsigned>(body.size()));
    return seq;
}

uint32_t CIUSocket::SendSecretCommand(uint16_t cmd, const std::string& body)
{
    //包体由 FlamingoSecretChat.dll 编好，这里只按放行规则校验，防止 DLL 借主连接发出任意命令；日志不记包体
    if (!balloonsecret::CanSendSecretCommand(cmd, static_cast<uint32_t>(body.size()), m_bConnected))
    {
        LOG_WARNING("Reject secret command: cmd=%u, len=%u, connected=%d.", static_cast<unsigned>(cmd),
                    static_cast<unsigned>(body.size()), m_bConnected ? 1 : 0);
        return 0;
    }
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd, seq, body);
    return seq;
}

uint32_t CIUSocket::SendThreadCommand(uint16_t cmd, const std::string& body)
{
    //包体由 FlamingoThreadGroup.dll 编好，这里只按放行规则校验，防止 DLL 借主连接发出任意命令；包体里有帖子正文，日志不记包体
    if (!balloonthread::CanSendThreadCommand(cmd, static_cast<uint32_t>(body.size()), m_bConnected))
    {
        LOG_WARNING("Reject thread command: cmd=%u, len=%u, connected=%d.", static_cast<unsigned>(cmd),
                    static_cast<unsigned>(body.size()), m_bConnected ? 1 : 0);
        return 0;
    }
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd, seq, body);
    LOG_INFO("Send v2 thread command, cmd=%u, seq=%u, len=%u.", static_cast<unsigned>(cmd), seq,
             static_cast<unsigned>(body.size()));
    return seq;
}

uint32_t CIUSocket::SendP2PCommand(uint16_t cmd, const std::string& body)
{
    //包体由 FlamingoP2P.dll 编好，这里只按放行规则校验，防止 DLL 借主连接发出任意命令
    if (!balloonp2p::P2PChannelPolicy::CanSendP2PCommand(cmd, static_cast<uint32_t>(body.size()), m_bConnected))
    {
        LOG_WARNING("Reject p2p command: cmd=%u, len=%u, connected=%d.", static_cast<unsigned>(cmd),
                    static_cast<unsigned>(body.size()), m_bConnected ? 1 : 0);
        return 0;
    }
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd, seq, body);
    LOG_INFO("Send v2 p2p command, cmd=%u, seq=%u, len=%u.", static_cast<unsigned>(cmd), seq,
             static_cast<unsigned>(body.size()));
    return seq;
}

void CIUSocket::SendChatShakeMessage(int64_t peerUserId, const std::string& clientMsgId)
{
    //组装 cmd_send_message 的"窗口抖动"请求体（msgtype=13，content={}）并发出。
    //与文本一样，回执经 cmd_send_message 应答异步回投，靠 clientmsgid 对回。
    std::string body = net::MessageProtocolClient::buildSendShakeRequest(
        peerUserId, clientMsgId);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_send_message, seq, body);
    LOG_INFO("Send v2 chat shake message, to=%lld, clientmsgid=%s, bodylen=%u.",
             (long long)peerUserId, clientMsgId.c_str(), (uint32_t)body.size());
}

uint32_t CIUSocket::SendCreateGroup(const std::string& groupName, const std::vector<int64_t>& memberIds, int groupType)
{
    //组装 cmd_create_group 请求体并编码成 v2 帧发出。应答经 cmd_create_group
    //异步回投到 UI（FMG_MSG_V2_CREATE_GROUP_RESULT）。
    std::string body = net::GroupProtocolClient::buildCreateGroupRequest(groupName, memberIds, groupType);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_create_group, seq, body);
    LOG_INFO("Send v2 create group, seq=%u, name-bytes=%u, members=%u, grouptype=%d, bodylen=%u.", seq,
             (uint32_t)groupName.size(), (uint32_t)memberIds.size(), groupType, (uint32_t)body.size());
    return seq;
}

void CIUSocket::SendSetNotifyModes(const std::vector<net::NotifyModeEntry>& items)
{
    //超过单次上限时按顺序拆批，每批一帧；服务端逐批应答，各批互不影响
    std::vector<std::vector<net::NotifyModeEntry> > batches;
    net::ConversationsProtocolClient::splitNotifyModeBatches(items, net::kNotifyModeBatchMax, batches);
    for (size_t i = 0; i < batches.size(); ++i)
    {
        std::string body = net::ConversationsProtocolClient::buildSetNotifyModesRequest(batches[i]);
        uint32_t seq = (uint32_t)(++m_seq);
        EnqueueFrame(cmd_set_notify_modes, seq, body);
        LOG_INFO("Send v2 set notify modes, batch=%u/%u, items=%u, seq=%u.",
                 (uint32_t)(i + 1), (uint32_t)batches.size(), (uint32_t)batches[i].size(), seq);
    }
}

void CIUSocket::SendSetConversation(int ctype, int64_t cid,
                                    bool hasPinned, bool pinned,
                                    bool hasNotifyMode, int notifyMode)
{
    //组装 cmd_set_conversation 请求体（只带本次要改的 pinned / notifymode 字段）并编码成
    //v2 帧发出。应答（通用 {code,msg}）经 cmd_set_conversation 异步回投到 UI
    //（FMG_MSG_V2_SET_CONVERSATION_RESULT）。
    std::string body = net::ConversationsProtocolClient::buildSetConversationRequest(
        ctype, cid, hasPinned, pinned, hasNotifyMode, notifyMode);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_set_conversation, seq, body);
    LOG_INFO("Send v2 set conversation, ctype=%d, cid=%lld, hasPinned=%d/%d, hasNotifyMode=%d/%d.",
             ctype, (long long)cid, (int)hasPinned, (int)pinned, (int)hasNotifyMode, notifyMode);
}

void CIUSocket::SendDeleteConversation(int ctype, int64_t cid)
{
    //组装"删除会话"请求体（只带 deleted:true）并编码成 v2 帧发出。与置顶 / 免打扰
    //共用 cmd_set_conversation，故应答走同一条回投路径（FMG_MSG_V2_SET_CONVERSATION_RESULT）。
    std::string body = net::ConversationsProtocolClient::buildDeleteConversationRequest(ctype, cid);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_set_conversation, seq, body);
    LOG_INFO("Send v2 delete conversation, ctype=%d, cid=%lld.", ctype, (long long)cid);
}

void CIUSocket::SendSetUnreadMark(int ctype, int64_t cid, bool mark)
{
    //只带 unreadmark 一个字段；应答与置顶 / 删除共用 cmd_set_conversation 的回投路径
    std::string body = net::ConversationsProtocolClient::buildSetUnreadMarkRequest(ctype, cid, mark);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_set_conversation, seq, body);
    LOG_INFO("Send v2 set unread mark, ctype=%d, cid=%lld, mark=%d.", ctype, (long long)cid, (int)mark);
}

void CIUSocket::SendSetGroupFollow(int64_t groupid, const std::vector<uint32_t>& targets)
{
    std::string body = net::ConversationsProtocolClient::buildSetGroupFollowRequest(groupid, targets);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_set_group_follow, seq, body);
    LOG_INFO("Send v2 set group follow, groupid=%lld, count=%u.", (long long)groupid, (uint32_t)targets.size());
}

void CIUSocket::SendSetFavorite(int ctype, int64_t cid, bool favorite)
{
    //组装 cmd_set_favorite 请求体并编码成 v2 帧发出。应答经同一 cmd 异步回投到 UI
    //（FMG_MSG_V2_SET_FAVORITE_RESULT），主面板据其中回带的 ctype/cid/favorite 对上号。
    std::string body = net::FavoriteProtocolClient::buildSetFavoriteRequest(ctype, cid, favorite);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_set_favorite, seq, body);
    LOG_INFO("Send v2 set favorite, ctype=%d, cid=%lld, favorite=%d.",
             ctype, (long long)cid, (int)favorite);
}

void CIUSocket::SendSetFriendRemark(int64_t userid, const std::string& remarkUtf8)
{
    //组装 cmd_set_friend_remark 请求体并编码成 v2 帧发出。应答经同一 cmd 异步回投到
    //UI（FMG_MSG_V2_SET_REMARK_RESULT），主面板据其中回带的 userid 对上号。
    std::string body = net::FriendProtocolClient::buildSetFriendRemarkRequest(userid, remarkUtf8);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_set_friend_remark, seq, body);
    //备注名可能含用户隐私，日志只记长度不记内容。
    LOG_INFO("Send v2 set friend remark, userid=%lld, remarklen=%u.",
             (long long)userid, (uint32_t)remarkUtf8.size());
}

void CIUSocket::SendSetContactTags(int64_t userid, const std::vector<std::string>& tagsUtf8)
{
    //组装 cmd_set_contact_tags 请求体并编码成 v2 帧发出。应答经同一 cmd 异步回投到
    //UI（FMG_MSG_V2_SET_TAGS_RESULT），主面板据其中回带的 userid 对上号。
    std::string body = net::ContactTagProtocolClient::buildSetContactTagsRequest(userid, tagsUtf8);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_set_contact_tags, seq, body);
    //标签可能含用户隐私，日志只记条数不记内容。
    LOG_INFO("Send v2 set contact tags, userid=%lld, tagcount=%u.",
             (long long)userid, (uint32_t)tagsUtf8.size());
}

bool CIUSocket::SendMemoList()
{
    //应答经 FMG_MSG_V2_MEMO_LIST 回投，由备忘中枢整体替换备忘簿
    if (IsUserOffline() || IsClosed())
    {
        LOG_ERROR("Skip v2 memo list: not connected.");
        return false;
    }
    const std::string body = net::MemoProtocolClient::buildListRequest(0);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_memo_list, seq, body);
    LOG_INFO("Send v2 memo list, seq=%u.", seq);
    return true;
}

bool CIUSocket::SendMemoSave(const net::MemoItem& memo)
{
    //应答经 FMG_MSG_V2_MEMO_SAVE_RESULT 回投。标题与备注可能是个人隐私，日志只记 ID 与规则
    if (IsUserOffline() || IsClosed())
    {
        LOG_ERROR("Skip v2 memo save: not connected, memoid=%lld.", (long long)memo.m_memoid);
        return false;
    }
    const std::string body = net::MemoProtocolClient::buildSaveRequest(memo);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_memo_save, seq, body);
    LOG_INFO("Send v2 memo save, memoid=%lld, ctype=%d, groupid=%lld, start=%lld, repeat=%d, targets=%u, seq=%u.",
             (long long)memo.m_memoid, memo.m_ctype, (long long)memo.m_groupid, (long long)memo.m_start,
             memo.m_repeat, (uint32_t)memo.m_targets.size(), seq);
    return true;
}

bool CIUSocket::SendMemoDelete(int64_t memoid)
{
    //应答经 FMG_MSG_V2_MEMO_DELETE_RESULT 回投
    if (memoid <= 0)
    {
        return false;
    }
    if (IsUserOffline() || IsClosed())
    {
        LOG_ERROR("Skip v2 memo delete: not connected, memoid=%lld.", (long long)memoid);
        return false;
    }
    const std::string body = net::MemoProtocolClient::buildDeleteRequest(memoid);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_memo_delete, seq, body);
    LOG_INFO("Send v2 memo delete, memoid=%lld, seq=%u.", (long long)memoid, seq);
    return true;
}

void CIUSocket::SendGetAnnouncements(int64_t sinceId, int limit)
{
    //组装 cmd_get_announcements 请求体并编码成 v2 帧发出。应答经同一 cmd 异步回投到 UI
    //（FMG_MSG_V2_ANNOUNCEMENTS），由主面板落本地库 announce.db 并刷新铃铛角标。
    std::string body = net::AnnounceProtocolClient::buildGetAnnouncementsRequest(sinceId, limit);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_get_announcements, seq, body);
    LOG_INFO("Send v2 get announcements, sinceid=%lld, limit=%d.", (long long)sinceId, limit);
}

void CIUSocket::SendMarkAnnouncementRead(int64_t announcementId)
{
    //组装 cmd_mark_announcement_read 请求体并编码成 v2 帧发出。应答经同一 cmd 异步回投
    //到 UI（FMG_MSG_V2_ANN_MARK_READ_RESULT），其中原样回带 announcementid 供对上号。
    std::string body = net::AnnounceProtocolClient::buildMarkReadRequest(announcementId);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_mark_announcement_read, seq, body);
    LOG_INFO("Send v2 mark announcement read, announcementid=%lld.", (long long)announcementId);
}

void CIUSocket::SendSetMemberCard(int64_t groupId, const std::string& cardUtf8)
{
    //组装 cmd_set_member_card 请求体（{groupid,card}）并编码成 v2 帧发出。应答
    //（通用 {code,msg}）经 cmd_set_member_card 异步回投到 UI
    //（FMG_MSG_V2_SET_MEMBER_CARD_RESULT）。
    std::string body = net::GroupProtocolClient::buildSetMemberCardRequest(groupId, cardUtf8);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_set_member_card, seq, body);
    LOG_INFO("Send v2 set member card, group=%lld, card-bytes=%u.",
             (long long)groupId, (uint32_t)cardUtf8.size());
}

void CIUSocket::SendLeaveGroup(int64_t groupId)
{
    //退群请求体与 buildGroupIdRequest 同形（仅 {groupid}）。应答（通用 {code,msg}）经
    //cmd_leave_group 异步回投到 UI（FMG_MSG_V2_LEAVE_GROUP_RESULT）。
    std::string body = net::GroupProtocolClient::buildGroupIdRequest(groupId);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_leave_group, seq, body);
    LOG_INFO("Send v2 leave group, group=%lld.", (long long)groupId);
}

// ====================================================================
// 群管理（群管理窗口用）
// ====================================================================

void CIUSocket::SendGetGroupJoinRequests(int64_t groupId)
{
    //请求体与 cmd_get_group_info 同形（只有一个 groupid），复用同一个构造器
    std::string body = net::GroupProtocolClient::buildGroupIdRequest(groupId);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_get_group_join_requests, seq, body);
    LOG_INFO("Send v2 get-group-join-requests, groupid=%lld, seq=%u.", (long long)groupId, seq);
}

void CIUSocket::SendHandleJoinGroup(int64_t requestId, bool accept)
{
    std::string body = net::GroupProtocolClient::buildHandleJoinGroupRequest(requestId, accept);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_handle_join_group, seq, body);
    LOG_INFO("Send v2 handle-join-group, requestid=%lld, accept=%d, seq=%u.",
             (long long)requestId, (int)accept, seq);
}

void CIUSocket::SendInviteToGroup(int64_t groupId, const std::vector<int64_t>& userIds, int historyLimit)
{
    std::string body = net::GroupProtocolClient::buildGroupMembersOpRequest(groupId, userIds, historyLimit);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_invite_to_group, seq, body);
    LOG_INFO("Send v2 invite-to-group, groupid=%lld, count=%u, historylimit=%d, seq=%u.",
             (long long)groupId, (uint32_t)userIds.size(), historyLimit, seq);
}

void CIUSocket::SendAcceptGroupInvite(int64_t inviteId)
{
    std::string body = net::GroupProtocolClient::buildAcceptGroupInviteRequest(inviteId);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_accept_group_invite, seq, body);
    LOG_INFO("Send v2 accept-group-invite, inviteid=%lld, seq=%u.", (long long)inviteId, seq);
}

void CIUSocket::SendKickGroupMembers(int64_t groupId, const std::vector<int64_t>& userIds)
{
    //请求体与邀请入群同构（群ID + userid 列表），共用同一个构造器
    std::string body = net::GroupProtocolClient::buildGroupMembersOpRequest(groupId, userIds);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_kick_group_member, seq, body);
    LOG_INFO("Send v2 kick-group-member, groupid=%lld, count=%u, seq=%u.",
             (long long)groupId, (uint32_t)userIds.size(), seq);
}

void CIUSocket::SendSetMemberRole(int64_t groupId, int64_t userId, bool isAdmin)
{
    //角色取值与服务端 group_member_role 对齐：0 普通成员、1 管理员。
    //本接口不用于设群主 —— 转让群主走 SendTransferOwner。
    const int kRoleMember = 0;
    const int kRoleAdmin  = 1;
    std::string body = net::GroupProtocolClient::buildSetMemberRoleRequest(
        groupId, userId, isAdmin ? kRoleAdmin : kRoleMember);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_set_member_role, seq, body);
    LOG_INFO("Send v2 set-member-role, groupid=%lld, userid=%lld, admin=%d, seq=%u.",
             (long long)groupId, (long long)userId, (int)isAdmin, seq);
}

void CIUSocket::SendMuteMember(int64_t groupId, int64_t userId, bool muted,
                               int duration, const std::string& reason)
{
    std::string body = net::GroupProtocolClient::buildMuteMemberRequest(
        groupId, userId, muted ? 1 : 0, duration, reason);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_mute_member, seq, body);
    LOG_INFO("Send v2 mute-member, groupid=%lld, userid=%lld, muted=%d, duration=%d, seq=%u.",
             (long long)groupId, (long long)userId, (int)muted, duration, seq);
}

void CIUSocket::SendMuteAll(int64_t groupId, bool muteAll)
{
    std::string body = net::GroupProtocolClient::buildMuteAllRequest(groupId, muteAll ? 1 : 0);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_mute_all, seq, body);
    LOG_INFO("Send v2 mute-all, groupid=%lld, muteall=%d, seq=%u.",
             (long long)groupId, (int)muteAll, seq);
}

void CIUSocket::SendUpdateGroupInfo(const net::UpdateGroupInfoParams& params)
{
    std::string body = net::GroupProtocolClient::buildUpdateGroupInfoRequest(params);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_update_group_info, seq, body);
    LOG_INFO("Send v2 update-group-info, groupid=%lld, bodylen=%u, seq=%u.",
             (long long)params.m_groupid, (uint32_t)body.size(), seq);
}

void CIUSocket::SendChangeGroupAvatar(int64_t groupId, int facetype,
                                      const std::string& customfaceUtf8)
{
    net::UpdateGroupInfoParams params;
    params.m_groupid = groupId;

    //两个 has 都置真：恢复默认头像发的正是 facetype=0 + 空 customface，少置一个
    //服务端就不会去清库里的旧值。
    params.m_hasFacetype   = true;
    params.m_facetype      = facetype;
    params.m_hasCustomface = true;
    params.m_customface    = customfaceUtf8;

    SendUpdateGroupInfo(params);
    LOG_INFO("Send v2 change-group-avatar, groupid=%lld, facetype=%d, customfacelen=%u.",
             (long long)groupId, facetype, (uint32_t)customfaceUtf8.size());
}

void CIUSocket::SendTransferOwner(int64_t groupId, int64_t newOwner)
{
    std::string body = net::GroupProtocolClient::buildTransferOwnerRequest(groupId, newOwner);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_transfer_owner, seq, body);
    LOG_INFO("Send v2 transfer-owner, groupid=%lld, newowner=%lld, seq=%u.",
             (long long)groupId, (long long)newOwner, seq);
}

void CIUSocket::SendDismissGroup(int64_t groupId)
{
    //请求体只有 groupid，与拉群资料同形
    std::string body = net::GroupProtocolClient::buildGroupIdRequest(groupId);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_dismiss_group, seq, body);
    LOG_INFO("Send v2 dismiss-group, groupid=%lld, seq=%u.", (long long)groupId, seq);
}

void CIUSocket::SendGetGroupAnnouncements(int64_t groupId, int offset, int count)
{
    std::string body = net::GroupProtocolClient::buildGetAnnouncementsRequest(
        groupId, offset, count);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_get_group_announcements, seq, body);
    LOG_INFO("Send v2 get-group-announcements, groupid=%lld, offset=%d, count=%d, seq=%u.",
             (long long)groupId, offset, count, seq);
}

void CIUSocket::SendPublishGroupAnnouncement(int64_t groupId, const std::string& titleUtf8,
                                             const std::string& contentUtf8, int severity)
{
    std::string body = net::GroupProtocolClient::buildPublishAnnouncementRequest(
        groupId, titleUtf8, contentUtf8, severity);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_publish_group_announcement, seq, body);
    //标题与正文不进日志：公告可能含群内敏感信息，日志会被随手拷走
    LOG_INFO("Send v2 publish-group-announcement, groupid=%lld, severity=%d, bodylen=%u, seq=%u.",
             (long long)groupId, severity, (uint32_t)body.size(), seq);
}

void CIUSocket::SendRepublishGroupAnnouncement(int64_t groupId, int64_t announceId)
{
    std::string body = net::GroupProtocolClient::buildRepublishAnnouncementRequest(
        groupId, announceId);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_publish_group_announcement, seq, body);
    LOG_INFO("Send v2 republish-group-announcement, groupid=%lld, id=%lld, seq=%u.",
             (long long)groupId, (long long)announceId, seq);
}

void CIUSocket::SendDeleteGroupAnnouncement(int64_t groupId, int64_t announceId)
{
    std::string body = net::GroupProtocolClient::buildDeleteAnnouncementRequest(
        groupId, announceId);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_delete_group_announcement, seq, body);
    LOG_INFO("Send v2 delete-group-announcement, groupid=%lld, id=%lld, seq=%u.",
             (long long)groupId, (long long)announceId, seq);
}

void CIUSocket::SendSetMustRead(int64_t groupId, int64_t announceId)
{
    std::string body = net::GroupProtocolClient::buildSetMustReadRequest(groupId, announceId);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_set_must_read, seq, body);
    LOG_INFO("Send v2 set-must-read, groupid=%lld, announcement=%lld, seq=%u.",
             (long long)groupId, (long long)announceId, seq);
}

void CIUSocket::SendConfirmMustRead(int64_t groupId)
{
    std::string body = net::GroupProtocolClient::buildGroupIdRequest(groupId);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_confirm_must_read, seq, body);
    LOG_INFO("Send v2 confirm-must-read, groupid=%lld, seq=%u.", (long long)groupId, seq);
}

void CIUSocket::SendReleaseMustRead(int64_t groupId, const std::vector<int64_t>& userIds)
{
    //请求体与移出成员同构（群ID + userid 列表）
    std::string body = net::GroupProtocolClient::buildGroupMembersOpRequest(groupId, userIds);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_release_must_read, seq, body);
    LOG_INFO("Send v2 release-must-read, groupid=%lld, count=%u, seq=%u.",
             (long long)groupId, (uint32_t)userIds.size(), seq);
}

void CIUSocket::SendGetMustRead(int64_t groupId)
{
    std::string body = net::GroupProtocolClient::buildGroupIdRequest(groupId);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_get_must_read, seq, body);
    LOG_INFO("Send v2 get-must-read, groupid=%lld, seq=%u.", (long long)groupId, seq);
}

void CIUSocket::SendGetGroupBots(int64_t groupId)
{
    std::string body = net::BotProtocolClient::buildGetGroupBotsRequest(groupId);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_get_group_bots, seq, body);
    LOG_INFO("Send v2 get-group-bots, groupid=%lld, seq=%u.", (long long)groupId, seq);
}

void CIUSocket::SendGetAutoReply()
{
    std::string body = net::BotProtocolClient::buildGetAutoReplyRequest();
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_get_auto_reply, seq, body);
    LOG_INFO("Send v2 get-auto-reply, seq=%u.", seq);
}

void CIUSocket::SendSetAutoReply(const net::AutoReplyWire& s)
{
    std::string body = net::BotProtocolClient::buildSetAutoReplyRequest(s);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_set_auto_reply, seq, body);
    LOG_INFO("Send v2 set-auto-reply, away=%d, busy=%d, seq=%u.", s.m_awayOn ? 1 : 0, s.m_busyOn ? 1 : 0, seq);
}

namespace {

std::mutex                g_botSinkMutex;   //保护下面两项
CIUSocket::BotFrameSink   g_botSink = NULL; //机器人帧的接收回调；NULL 表示没有登记
void*                     g_botSinkCtx = NULL; //回调的上下文指针

} // anonymous namespace

void CIUSocket::SetBotFrameSink(BotFrameSink sink, void* ctx)
{
    std::lock_guard<std::mutex> guard(g_botSinkMutex);
    g_botSink = sink;
    g_botSinkCtx = ctx;
}

void CIUSocket::DispatchBotFrame(uint16_t cmd, uint32_t seq, const std::string& body)
{
    BotFrameSink sink = NULL;
    void* ctx = NULL;
    {
        std::lock_guard<std::mutex> guard(g_botSinkMutex);
        sink = g_botSink;
        ctx = g_botSinkCtx;
    }
    if (sink == NULL)
    {
        LOG_WARNING("Drop bot frame without sink, cmd=%u, seq=%u.", static_cast<unsigned>(cmd), seq);
        return;
    }
    sink(ctx, cmd, seq, body);
}

uint32_t CIUSocket::SendBotCommand(uint16_t cmd, const std::string& body)
{
    //包体由 FlamingoBot.dll 编好，这里只按命令范围、包体非空与连接状态放行，防止 DLL 借主连接发出任意命令
    if (!net::BotProtocolClient::isDllFrame(cmd) || body.empty() || !m_bConnected)
    {
        LOG_WARNING("Reject bot command: cmd=%u, len=%u, connected=%d.", static_cast<unsigned>(cmd),
                    static_cast<unsigned>(body.size()), m_bConnected ? 1 : 0);
        return 0;
    }
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd, seq, body);
    LOG_INFO("Send v2 bot command, cmd=%u, seq=%u, len=%u.", static_cast<unsigned>(cmd), seq,
             static_cast<unsigned>(body.size()));
    return seq;
}

void CIUSocket::SendGetGroupQa(int64_t groupId)
{
    std::string body = net::GroupQaProtocolClient::buildGetRequest(groupId);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_get_group_qa, seq, body);
    LOG_INFO("Send v2 get-group-qa, groupid=%lld, seq=%u.", (long long)groupId, seq);
}

void CIUSocket::SendSaveGroupQa(int64_t groupId, int64_t id, const std::string& question, const std::string& answer)
{
    std::string body = net::GroupQaProtocolClient::buildSaveRequest(groupId, id, question, answer);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_save_group_qa, seq, body);
    LOG_INFO("Send v2 save-group-qa, groupid=%lld, id=%lld, seq=%u.", (long long)groupId, (long long)id, seq);
}

void CIUSocket::SendDeleteGroupQa(int64_t groupId, int64_t id)
{
    std::string body = net::GroupQaProtocolClient::buildDeleteRequest(groupId, id);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_delete_group_qa, seq, body);
    LOG_INFO("Send v2 delete-group-qa, groupid=%lld, id=%lld, seq=%u.", (long long)groupId, (long long)id, seq);
}

void CIUSocket::SendCreateGroupLink(int64_t groupId, int days)
{
    std::string body = net::GroupLinkProtocolClient::buildCreateRequest(groupId, days);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_create_group_link, seq, body);
    LOG_INFO("Send v2 create-group-link, groupid=%lld, days=%d, seq=%u.", (long long)groupId, days, seq);
}

void CIUSocket::SendGetGroupLinks(int64_t groupId)
{
    std::string body = net::GroupProtocolClient::buildGroupIdRequest(groupId);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_get_group_links, seq, body);
    LOG_INFO("Send v2 get-group-links, groupid=%lld, seq=%u.", (long long)groupId, seq);
}

void CIUSocket::SendRevokeGroupLink(int64_t groupId, int64_t linkId)
{
    std::string body = net::GroupLinkProtocolClient::buildRevokeRequest(groupId, linkId);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_revoke_group_link, seq, body);
    LOG_INFO("Send v2 revoke-group-link, groupid=%lld, id=%lld, seq=%u.", (long long)groupId, (long long)linkId, seq);
}

void CIUSocket::SendGetGroupLinkInfo(const std::string& code)
{
    std::string body = net::GroupLinkProtocolClient::buildCodeRequest(code);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_get_group_link_info, seq, body);
    LOG_INFO("Send v2 get-group-link-info, seq=%u.", seq);
}

void CIUSocket::SendJoinGroupByLink(const std::string& code)
{
    std::string body = net::GroupLinkProtocolClient::buildCodeRequest(code);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_join_group_by_link, seq, body);
    LOG_INFO("Send v2 join-group-by-link, seq=%u.", seq);
}

void CIUSocket::SendChangePassword(const std::string& oldHashHex, const std::string& newHashHex)
{
    //组装 cmd_change_password 请求体并编码成 v2 帧发出。请求体里只放口令的
    //SHA-256 hex（明文不入网络/日志）；应答经 cmd_change_password 异步回投到 UI
    //（FMG_MSG_V2_CHANGE_PWD_RESULT）。
    std::string body = net::AccountProtocolClient::buildChangePasswordRequest(oldHashHex, newHashHex);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_change_password, seq, body);
    LOG_INFO("Send v2 change password, bodylen=%u.", (uint32_t)body.size());
}

void CIUSocket::SendUpdateSignature(const std::string& signatureUtf8)
{
    //组装 cmd_update_profile 请求体（仅 signature 字段）并编码成 v2 帧发出。应答经
    //cmd_update_profile 异步回投到 UI（FMG_MSG_V2_UPDATE_PROFILE_RESULT）。
    std::string body = net::ProfileProtocolClient::buildUpdateSignatureRequest(signatureUtf8);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_update_profile, seq, body);
    LOG_INFO("Send v2 update signature, bodylen=%u.", (uint32_t)body.size());
}

void CIUSocket::SendUpdateProfile(const net::ProfileUpdateFields& fields)
{
    //组装 cmd_update_profile 请求体（只带改动过的字段）并编码成 v2 帧发出。应答同样经
    //cmd_update_profile 异步回投到 UI（FMG_MSG_V2_UPDATE_PROFILE_RESULT）。
    std::string body = net::ProfileProtocolClient::buildUpdateProfileRequest(fields);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_update_profile, seq, body);
    LOG_INFO("Send v2 update profile, bodylen=%u.", (uint32_t)body.size());
}

void CIUSocket::SendChangeAvatar(int facetype, const std::string& customfaceUtf8)
{
    //组装 cmd_change_avatar 请求体并编码成 v2 帧发出。应答（通用 {code,msg}）经
    //cmd_change_avatar 异步回投到 UI（FMG_MSG_V2_CHANGE_AVATAR_RESULT）。
    std::string body = net::ProfileProtocolClient::buildChangeAvatarRequest(facetype, customfaceUtf8);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_change_avatar, seq, body);
    LOG_INFO("Send v2 change avatar, facetype=%d, customface-bytes=%u, bodylen=%u.",
             facetype, (uint32_t)customfaceUtf8.size(), (uint32_t)body.size());
}

void CIUSocket::SendChangePresence(int presence)
{
    //先把目标状态记入"期望在线状态"：既供本次发包，也供断线重连 / 重新上线时沿用。
    m_nRecordOnlineStatus = presence;

    //组装 cmd_change_presence 请求体（{"presence":N}）并编码成 v2 帧发出。应答经
    //cmd_change_presence 异步回投到 UI（FMG_MSG_V2_CHANGE_PRESENCE_RESULT）。
    std::string body = net::ProfileProtocolClient::buildChangePresenceRequest(presence);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_change_presence, seq, body);
    LOG_INFO("Send v2 change presence, presence=%d, bodylen=%u.", presence, (uint32_t)body.size());
}

void CIUSocket::SendGroupTextMessage(int64_t groupId, const std::string& clientMsgId,
                                     const std::string& text, const net::TextFont& font,
                                     const net::ReplyQuote& quote, int textFormat,
                                     const std::string& codeLang,
                                     const std::vector<net::CodeNoteData>& codeNotes)
{
    //组装群聊 cmd_send_message 请求体（ctype=2、cid=群id）并发出。回执与单聊同路径。
    //font.m_valid 为 true 时把字体写进 content.font；Markdown 消息与代码片段另带 content.format。
    std::string body = net::MessageProtocolClient::buildSendGroupTextRequest(
        groupId, clientMsgId, text, font, quote, textFormat, codeLang, codeNotes);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_send_message, seq, body);
    LOG_INFO("Send v2 group text message, group=%lld, clientmsgid=%s, format=%d, lang=%s, notes=%u, bodylen=%u.",
             (long long)groupId, clientMsgId.c_str(), textFormat, codeLang.c_str(), (uint32_t)codeNotes.size(),
             (uint32_t)body.size());
}

void CIUSocket::SendGroupTextMessageWithMentions(
    int64_t groupId, const std::string& clientMsgId,
    const std::string& text,
    const std::vector<balloonchatwnd::MentionSpan>& ats,
    bool atAll, const net::TextFont& font, const net::ReplyQuote& quote, int textFormat,
    const std::string& codeLang)
{
    //与上面的普通群文本同路径，只是 content 里多一个 at 对象（@提醒）。
    std::string body = net::MessageProtocolClient::buildSendGroupTextRequest(
        groupId, clientMsgId, text, ats, atAll, font, quote, textFormat, codeLang);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_send_message, seq, body);
    LOG_INFO("Send v2 group text message with mentions, group=%lld, clientmsgid=%s, "
             "ats=%u, atall=%d, format=%d, bodylen=%u.",
             (long long)groupId, clientMsgId.c_str(), (uint32_t)ats.size(),
             (int)atAll, textFormat, (uint32_t)body.size());
}

uint32_t CIUSocket::RequestUpload(int scene, const std::string& md5, int64_t size,
                                  const std::string& name, const std::string& ext, int owner)
{
    //组 cmd_request_upload 请求体并编码成 v2 帧发出。先按 seq 登记归属（图片 / 文件），
    //应答经 cmd_request_upload 异步回到 HandleV2Frame，再按 seq 查归属转交对应等待槽。
    std::string body = net::FileProtocolClient::buildRequestUploadRequest(scene, md5, size, name, ext);
    uint32_t seq = (uint32_t)(++m_seq);
    {
        std::lock_guard<std::mutex> guard(m_transferOwnerMtx);
        m_transferOwners.registerSeq(seq, owner);
    }
    EnqueueFrame(cmd_request_upload, seq, body);
    LOG_INFO("Send v2 request-upload, scene=%d, size=%lld, md5=%s, seq=%u, owner=%d.",
             scene, (long long)size, md5.c_str(), seq, owner);
    return seq;
}

//发 cmd_check_update(1905)。本命令<u>允许未登录时发</u>：版本过低的客户端
//会被登录拒绝，正要靠它自救，服务端已把它放进登录前白名单。
//应答异步回到 HandleV2Frame，再按 seq 转交 UpdateManager 的等待槽。
//未连上时返回 0，调用方据此当作“这次没查成”。
uint32_t CIUSocket::SendCheckUpdate(const std::string& body)
{
    if (!m_bConnected)
    {
        LOG_ERROR("Send v2 check-update failed: not connected.");
        return 0;
    }

    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_check_update, seq, body);
    LOG_INFO("Send v2 check-update, seq=%u, body=%s.", seq, body.c_str());
    return seq;
}

uint32_t CIUSocket::RequestDownload(const std::string& fileid, int owner)
{
    //组 cmd_request_download 请求体并编码成 v2 帧发出。先按 seq 登记归属，应答经
    //cmd_request_download 异步回到 HandleV2Frame，再按 seq 查归属转交对应下载等待槽。
    std::string body = net::FileProtocolClient::buildRequestDownloadRequest(fileid);
    uint32_t seq = (uint32_t)(++m_seq);
    {
        std::lock_guard<std::mutex> guard(m_transferOwnerMtx);
        m_transferOwners.registerSeq(seq, owner);
    }
    EnqueueFrame(cmd_request_download, seq, body);
    LOG_INFO("Send v2 request-download, fileid=%s, seq=%u, owner=%d.", fileid.c_str(), seq, owner);
    return seq;
}

void CIUSocket::SendChatImageMessage(int ctype, int64_t cid, const std::string& clientMsgId,
                                     const std::string& fileid, const std::string& md5,
                                     int width, int height, int64_t size)
{
    //组 cmd_send_message 图片请求体（msgtype=2）并发出。回执与文本同路径，按
    //clientmsgid 回投到 UI（FMG_MSG_V2_SEND_CHATMSG_ACK）。
    std::string body = net::MessageProtocolClient::buildSendImageRequest(
        ctype, cid, clientMsgId, fileid, md5, width, height, size);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_send_message, seq, body);
    LOG_INFO("Send v2 image message, ctype=%d, cid=%lld, clientmsgid=%s, fileid=%s, %dx%d.",
             ctype, (long long)cid, clientMsgId.c_str(), fileid.c_str(), width, height);
}

void CIUSocket::SendChatFileMessage(int ctype, int64_t cid, const std::string& clientMsgId,
                                    const std::string& fileid, const std::string& filename,
                                    int64_t size, const std::string& md5)
{
    //组 cmd_send_message 文件请求体（msgtype=3）并发出。回执与文本 / 图片同路径，按
    //clientmsgid 回投到 UI（FMG_MSG_V2_SEND_CHATMSG_ACK）。
    std::string body = net::MessageProtocolClient::buildSendFileRequest(
        ctype, cid, clientMsgId, fileid, filename, size, md5);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_send_message, seq, body);
    LOG_INFO("Send v2 file message, ctype=%d, cid=%lld, clientmsgid=%s, fileid=%s, size=%lld.",
             ctype, (long long)cid, clientMsgId.c_str(), fileid.c_str(), (long long)size);
}

void CIUSocket::SendChatVoiceMessage(int ctype, int64_t cid, const std::string& clientMsgId,
                                     const std::string& fileid, const std::string& md5,
                                     int durationSeconds, int64_t size, const std::string& waveBase64)
{
    //组 cmd_send_message 语音请求体（msgtype=4）并发出。回执与文本 / 图片 / 文件同路径，按
    //clientmsgid 回投到 UI（FMG_MSG_V2_SEND_CHATMSG_ACK）。
    std::string body = net::MessageProtocolClient::buildSendVoiceRequest(
        ctype, cid, clientMsgId, fileid, md5, durationSeconds, size, waveBase64);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_send_message, seq, body);
    LOG_INFO("Send v2 voice message, ctype=%d, cid=%lld, clientmsgid=%s, fileid=%s, duration=%d, size=%lld.",
             ctype, (long long)cid, clientMsgId.c_str(), fileid.c_str(), durationSeconds, (long long)size);
}

HWND CIUSocket::GetUIProxyWnd()
{
    return m_hProxyWnd;
}

void CIUSocket::RequestGroupInfo(int64_t groupId)
{
    std::string body = net::GroupProtocolClient::buildGroupIdRequest(groupId);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_get_group_info, seq, body);
    LOG_INFO("Request v2 group info, group=%lld.", (long long)groupId);
}

void CIUSocket::RequestGroupMembers(int64_t groupId)
{
    std::string body = net::GroupProtocolClient::buildGroupIdRequest(groupId);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_get_group_members, seq, body);
    LOG_INFO("Request v2 group members, group=%lld.", (long long)groupId);
}

uint32_t CIUSocket::RequestGroupFiles(int64_t groupId, int filetype, int offset, int count)
{
    std::string body = net::GroupFileProtocolClient::buildGetGroupFilesRequest(
        groupId, filetype, offset, count);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_get_group_files, seq, body);
    LOG_INFO("Request v2 group files, group=%lld, filetype=%d, offset=%d, count=%d, seq=%u.",
             (long long)groupId, filetype, offset, count, seq);
    return seq;
}

void CIUSocket::GlobalSearch(const std::string& keywordUtf8, int limit, int announcementLimit)
{
    std::string body = net::GlobalSearchProtocolClient::buildRequest(keywordUtf8, limit, announcementLimit);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_global_search, seq, body);
    //关键字是用户输入的内容，日志只记字节数
    LOG_INFO("Request v2 global search, keyword bytes=%u, limit=%d, announcement limit=%d, seq=%u.",
             (uint32_t)keywordUtf8.size(), limit, announcementLimit, seq);
}

void CIUSocket::SearchGroupFiles(const std::string& keywordUtf8, int limit)
{
    std::string body = net::GroupFileProtocolClient::buildSearchGroupFilesRequest(keywordUtf8, limit);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_search_group_files, seq, body);
    //关键字是用户输入的内容，日志只记字节数
    LOG_INFO("Request v2 search group files, keyword bytes=%u, limit=%d, seq=%u.",
             (uint32_t)keywordUtf8.size(), limit, seq);
}

void CIUSocket::RequestAddGroupFile(int64_t groupId, const std::string& fileid)
{
    std::string body = net::GroupFileProtocolClient::buildAddGroupFileRequest(groupId, fileid);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_add_group_file, seq, body);
    LOG_INFO("Request v2 add group file, group=%lld, fileid=%s.",
             (long long)groupId, fileid.c_str());
}

void CIUSocket::RequestDeleteGroupFile(int64_t groupId, const std::string& entryid)
{
    std::string body = net::GroupFileProtocolClient::buildDeleteGroupFileRequest(groupId, entryid);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_delete_group_file, seq, body);
    LOG_INFO("Request v2 delete group file, group=%lld, entryid=%s.",
             (long long)groupId, entryid.c_str());
}

void CIUSocket::RequestUserGroups()
{
    //无请求体（服务端 onGetUserGroups 不读 body），发空对象占位即可。
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_get_user_groups, seq, "{}");
    LOG_INFO("Request v2 user groups (cmd_get_user_groups).");
}

void CIUSocket::RequestSyncRewritten(int64_t since, int limit)
{
    std::string body = net::MessageProtocolClient::buildSyncRewrittenRequest(since, limit);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_sync_rewritten_messages, seq, body);
    LOG_INFO("Request v2 sync rewritten messages, since=%lld, limit=%d.", (long long)since, limit);
}

void CIUSocket::RequestSyncMessages(int ctype, int64_t cid, int64_t beforeMsgId, int limit)
{
    std::string body = net::MessageProtocolClient::buildSyncMessagesRequest(
        ctype, cid, beforeMsgId, limit);
    uint32_t seq = (uint32_t)(++m_seq);
    EnqueueFrame(cmd_sync_messages, seq, body);
    LOG_INFO("Request v2 sync messages, ctype=%d, cid=%lld, before=%lld, limit=%d.",
             ctype, (long long)cid, (long long)beforeMsgId, limit);
}

std::string CIUSocket::GetOrCreateDeviceId()
{
    CIniFile iniFile;
    CString strIniFilePath(g_szHomePath);
    strIniFilePath += _T("config\\flamingo.ini");

    //已有则直接读回
    TCHAR szDeviceId[64] = { 0 };
    iniFile.ReadString(_T("client"), _T("deviceid"), _T(""), szDeviceId, 64, strIniFilePath);
    if (szDeviceId[0] != _T('\0'))
        return EncodeUtil::UnicodeToAnsi(std::wstring(szDeviceId));

    //首次：生成 GUID 作为设备标识。GUID 转十六进制后为纯 ASCII，
    //写入 flamingo.ini 不破坏其 ASCII 约定。
    GUID guid = { 0 };
    TCHAR szNew[64] = { 0 };
    if (::CoCreateGuid(&guid) == S_OK)
    {
        _stprintf_s(szNew, ARRAYSIZE(szNew),
                    _T("PC-%08lX%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X"),
                    guid.Data1, guid.Data2, guid.Data3,
                    guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
                    guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    }
    else
    {
        //极少数失败时用时间戳兜底，保证设备标识非空
        _stprintf_s(szNew, ARRAYSIZE(szNew), _T("PC-%ld"), (long)time(NULL));
    }

    ::WritePrivateProfileString(_T("client"), _T("deviceid"), szNew, strIniFilePath);
    return EncodeUtil::UnicodeToAnsi(std::wstring(szNew));
}

std::string CIUSocket::GetDeviceName()
{
    wchar_t szName[MAX_COMPUTERNAME_LENGTH + 1] = { 0 };
    DWORD dwLen = MAX_COMPUTERNAME_LENGTH + 1;
    if (!::GetComputerNameW(szName, &dwLen) || szName[0] == L'\0')
        return std::string("PC");

    //计算机名可能含中文，v2 包体为 UTF-8，需转 UTF-8
    char szUtf8[256] = { 0 };
    EncodeUtil::UnicodeToUtf8(szName, szUtf8, sizeof(szUtf8));
    return std::string(szUtf8);
}

bool CIUSocket::Register(const net::RegisterRequestParams& params, int nTimeout, std::string& strReturnData)
{
    //注册是登录之前的动作，此时还没有连接，须自行建连。整体流程与 Login
    //一致：建连 → 发一帧 → 同步等一帧应答，故同样是阻塞调用。
    if (!Connect())
    {
        LOG_ERROR("Register failed: connect server failed, server: %s, port: %d.",
                  m_strServer.c_str(), m_nPort);
        return false;
    }

    //口令哈希：对明文密码的 UTF-8 字节做 SHA-256，取小写十六进制串。
    //服务端再对该哈希加盐做 MD5 入库，明文不上网 —— 与 Login 同一口径。
    net::RegisterRequestParams onWire = params;
    onWire.password = net::Sha256::hexDigest(params.password);
    if (onWire.password.empty())
    {
        LOG_ERROR("Register failed: SHA-256 of password failed.");
        return false;
    }

    std::string body = net::RegisterProtocolClient::buildRegisterRequest(onWire);

    //日志里不能出现密码，公司名则要打出来 —— 注册失败最常见的原因就是公司
    //全称填错，没有它日志根本定位不了问题。账号此刻还不存在（服务端生成后
    //随应答回传），故这里无从记录。
    LOG_INFO("Request v2 register: Nickname=%s, Phone=%s, Company=%s, Password=*****.",
             params.nickname.c_str(), params.phone.c_str(), params.company.c_str());

    //编码 v2 帧（注册请求包体小，不压缩），seq 自增
    uint32_t seq = (uint32_t)(++m_seq);
    std::string sendBuf;
    if (!BuildFrame(cmd_register, seq, body, sendBuf))
    {
        LOG_ERROR("Register failed: encode register frame failed.");
        return false;
    }
    if (!SendData(sendBuf.c_str(), (int)sendBuf.length(), nTimeout))
        return false;

    //同步收取一个完整的应答帧。RecvOneFrame 交回的已经是明文包体
    frame_header header;
    if (!RecvOneFrame(header, strReturnData, nTimeout))
        return false;

    //本连接注册阶段只应收到 cmd_register 应答；cmd 不符仅记日志，仍把包体回传
    if (header.cmd != cmd_register)
        LOG_ERROR("Register: unexpected response cmd=%u (expected cmd_register).", header.cmd);

    return true;
}

bool CIUSocket::Login(const char* pszUser, const char* pszPassword, int nClientType, int nOnlineStatus, int nTimeout, std::string& strReturnData)
{
    //记录本次登录使用的在线状态（v2 presence_type），作为"期望在线状态"的真相源：
    //断线重连 / 主动重新上线时由 CFlamingoClient::Login 读取它作为登录 presence。
    m_nRecordOnlineStatus = nOnlineStatus;

    //撤掉"用户主动离线"闸门 —— 走到这里说明上层已经决定要重新上线（用户从离线切回
    //在线，或正常登录）。必须在 Connect 之前撤，否则下面的 Connect 建好连接后，发送
    //线程仍会以为处于离线态而拒发一切业务帧。
    m_bUserOffline = false;

    if (!Connect())
        return false;

    //口令哈希：对明文密码的 UTF-8 字节做 SHA-256，取小写十六进制串。
    //服务端再对该哈希加盐做 MD5 入库，明文不上网。
    std::string passwordHash = net::Sha256::hexDigest(std::string(pszPassword));
    if (passwordHash.empty())
    {
        LOG_ERROR("Login failed: SHA-256 of password failed.");
        return false;
    }

    //组装 v2 登录请求包体（cmd_login）
    net::LoginRequestParams params;
    params.clientversion = kClientVersionV2;
    params.account       = pszUser;
    params.password      = passwordHash;
    params.clienttype    = nClientType;
    params.presence      = nOnlineStatus;
    params.deviceid      = GetOrCreateDeviceId();
    params.devicename    = GetDeviceName();
    std::string body = net::LoginProtocolClient::buildLoginRequest(params);

    LOG_INFO("Request v2 logon: Account=%s, Password=*****, ClientType=%d, Presence=%d.",
             pszUser, nClientType, nOnlineStatus);

    //编码 v2 帧，seq 自增
    uint32_t seq = (uint32_t)(++m_seq);
    std::string sendBuf;
    if (!BuildFrame(cmd_login, seq, body, sendBuf))
    {
        LOG_ERROR("Login failed: encode login frame failed.");
        return false;
    }
    if (!SendData(sendBuf.c_str(), (int)sendBuf.length(), nTimeout))
        return false;

    //同步收取一个完整的应答帧。RecvOneFrame 交回的已经是明文包体
    frame_header header;
    if (!RecvOneFrame(header, strReturnData, nTimeout))
        return false;

    //本连接登录阶段只应收到 cmd_login 应答；cmd 不符仅记日志，仍把包体回传
    if (header.cmd != cmd_login)
        LOG_ERROR("Login: unexpected response cmd=%u (expected cmd_login).", header.cmd);

    //成功收到登录应答 = 与服务端连通正常：复位收取线程的"网络已出错"标志，让后续真实
    //断线能再次上报（m_bNetError 在 v2 下已无其它复位点，详见 RecvMsgThread::ResetNetError
    //注释）。这对"主动离线→再上线"尤为关键：离线时该标志被置真，不复位则之后真掉线不再
    //触发自动重连。
    ResetNetError();

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
            } else
                return false;
        } else if (nRet < 1)
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

    //Windows 下 select 的第一个参数 nfds 并不参与运算，仅为兼容 BSD 接口而保留；
    //SOCKET 在 64 位下是 64 位无符号句柄，这里收窄为 int 不影响 select 的行为
    int nRet = ::select(static_cast<int>(m_hSocket + 1), NULL, &writeset, NULL, &timeout);
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
        } else if (nRet < 1)
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
    //v2 心跳：空包体帧，cmd 为 cmd_heartbeat，seq 自增
    ++m_nHeartbeatSeq;
    EnqueueFrame(cmd_heartbeat, (uint32_t)m_nHeartbeatSeq, std::string());
}