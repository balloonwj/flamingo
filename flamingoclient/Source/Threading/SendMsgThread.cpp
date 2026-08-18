#include "stdafx.h"
#include <sstream>
#include "SendMsgThread.h"
#include "..\Session\UserSessionData.h"
#include "..\Common\Log\IULog.h"
#include "..\Common\Util\MiniBuffer.h"
#include "..\Common\Util\Path.h"
#include "..\Common\Util\EncodeUtil.h"
#include "..\App\FlamingoClient.h"
#include "..\Session\UserMgr.h"
#include "..\Common\Util\File2.h"
#include "../net/IUSocket.h"
#include "../net/ProtocolStream.h"
#include "../net/LoginProtocolClient.h"     // v2 登录应答解析（重连路径用）

CMsgItem::CMsgItem(void)
{
    m_nType = FMG_MSG_TYPE_BUDDY;
    m_lpMsg = NULL;
    m_nGroupNum = m_nUTalkNum = 0;
    m_hwndFrom = NULL;
}

CMsgItem::~CMsgItem(void)
{
    if (m_lpMsg != NULL)
    {
        switch (m_nType)
        {
        case FMG_MSG_TYPE_BUDDY:
            delete (CBuddyMessage*)m_lpMsg;
            break;
        case FMG_MSG_TYPE_GROUP:
            delete (CGroupMessage*)m_lpMsg;
            break;
        case FMG_MSG_TYPE_SESS:
            delete (CSessMessage*)m_lpMsg;
            break;
        }
    }
}

CSendMsgThread::CSendMsgThread()
{
    m_lpFMGClient = NULL;
    m_lpUserMgr = NULL;
    m_nMsgId = 0;
}

CSendMsgThread::~CSendMsgThread(void)
{
    DeleteAllItems();
}

void CSendMsgThread::Stop()
{
    m_bStop = true;
    m_cvItems.notify_one();
}

void CSendMsgThread::Run()
{
    while (!m_bStop)
    {
        CNetData* lpMsg;
        {
            std::unique_lock<std::mutex> guard(m_mtItems);
            while (m_listItems.empty())
            {
                if (m_bStop)
                    return;

                m_cvItems.wait(guard);
            }

            lpMsg = m_listItems.front();
            m_listItems.pop_front();
        }

        HandleItem(lpMsg);
    }
}

void CSendMsgThread::AddItem(CNetData* pItem)
{
    std::lock_guard<std::mutex> guard(m_mtItems);
    m_listItems.push_back(pItem);
    m_cvItems.notify_one();
}

void CSendMsgThread::DeleteAllItems()
{
    std::lock_guard<std::mutex> guard(m_mtItems);
    for (auto iter : m_listItems)
    {
        delete iter;
    }

    m_listItems.clear();
}

void CSendMsgThread::HandleItem(CNetData* pNetData)
{
    if (pNetData == NULL)
        return;

    //只剩登录这一支：本线程如今仅服务于"掉线自动重连"——CIUSocket::Login 是
    //阻塞调用（要等服务端应答帧），不能在 UI 线程上直接跑，故放到这里。
    //其余 NET_DATA_* 分支对应 v1 的注册 / 改状态 / 发消息等发送链路，其请求对象
    //已无人构造，2026-08-14 随 v1 清理一并删除。
    if (pNetData->m_uType == NET_DATA_LOGIN)
    {
        HandleLogon((const CLoginRequest*)pNetData);
    }
    else
    {
        LOG_WARNING("Unhandled data type in send queue: %u.", pNetData->m_uType);
    }

    m_seq++;

    delete pNetData;
}

// 重连登录入口：被 MainDlg 的 m_dwReconnectTimerId 触发后，CFlamingoClient::Login()
// 投递的 CLoginRequest 在本函数里处理。改造期内统一走 v2 协议 ——
//   1) 仍调 CIUSocket::Login() 走 cmd_login（其内部已 SHA-256 + encodeFrame）
//   2) 应答解析改用 net::LoginProtocolClient::parseLoginResponse
// 与 BalloonLoginDlg::LoginThreadProc 的处理保持一致，结果通过
// FMG_MSG_LOGIN_RESULT 回投给 m_hProxyWnd（FlamingoClient → MainDlg）。
//
// 历史 bug：旧实现按 v1 顶层平铺字段校验（要求 userid 为 int、username 在顶层等），
//          而 v2 应答 userid 为 string、用户资料嵌在 profile 子对象，
//          导致每次重连必判 LOGIN_FAILED，MainDlg 收到后重开登录窗
//          触发 balloonui DuiFrameWindow 悬空 m_titleBar 崩溃。
void CSendMsgThread::HandleLogon(const CLoginRequest* pLoginRequest)
{
    if (pLoginRequest == NULL)
        return;

    LOG_INFO("Reconnect logon request: Account=%s, Status=%d, LoginType=%ld.",
             pLoginRequest->m_szAccountName,
             pLoginRequest->m_nStatus,
             (long)pLoginRequest->m_nLoginType);

    //超时单位为秒（对齐 BalloonLoginDlg 的 kLoginTimeoutSeconds=3）。
    //旧实现传 3000 实际被当作 3000 秒（CIUSocket::RecvData 用 time(NULL)
    //做超时判定，单位为秒），等同于"无超时"。
    const int kReconnectTimeoutSeconds = 3;

    std::string strReturnData;
    bool bRet = CIUSocket::GetInstance().Login(
        pLoginRequest->m_szAccountName,
        pLoginRequest->m_szPassword,
        (long)pLoginRequest->m_nLoginType,
        pLoginRequest->m_nStatus,
        kReconnectTimeoutSeconds,
        strReturnData);

    CLoginResult* pLoginResult = new CLoginResult();
    pLoginResult->m_LoginResultCode = LOGIN_FAILED;
    if (bRet)
    {
        net::LoginResponse resp;
        if (net::LoginProtocolClient::parseLoginResponse(strReturnData, resp))
        {
            //成功应答：填 LoginResult，profile 字段全部从 resp.profile 取
            if (resp.code == 0)
            {
                pLoginResult->m_LoginResultCode = LOGIN_SUCCESS;
                pLoginResult->m_uAccountID = (UINT)resp.userid;
                strcpy_s(pLoginResult->m_szAccountName, ARRAYSIZE(pLoginResult->m_szAccountName),
                         resp.profile.username.c_str());
                strcpy_s(pLoginResult->m_szNickName, ARRAYSIZE(pLoginResult->m_szNickName),
                         resp.profile.nickname.c_str());
                pLoginResult->m_nFace     = resp.profile.facetype;
                pLoginResult->m_nGender   = resp.profile.gender;
                pLoginResult->m_nBirthday = resp.profile.birthday;
                strcpy_s(pLoginResult->m_szSignature, ARRAYSIZE(pLoginResult->m_szSignature),
                         resp.profile.signature.c_str());
                strcpy_s(pLoginResult->m_szAddress, ARRAYSIZE(pLoginResult->m_szAddress),
                         resp.profile.address.c_str());
                strcpy_s(pLoginResult->m_szCustomFace, ARRAYSIZE(pLoginResult->m_szCustomFace),
                         resp.profile.customface.c_str());
                strcpy_s(pLoginResult->m_szPhoneNumber, ARRAYSIZE(pLoginResult->m_szPhoneNumber),
                         resp.profile.phonenumber.c_str());
                strcpy_s(pLoginResult->m_szMail, ARRAYSIZE(pLoginResult->m_szMail),
                         resp.profile.mail.c_str());
            }
            //失败应答：把服务端错误码映射到客户端 LOGIN_RESULT_CODE
            //（错误码定义见 server chatserversrc/Protocol.h enum error_code）
            else if (resp.code == 102)
                pLoginResult->m_LoginResultCode = LOGIN_UNREGISTERED;
            else if (resp.code == 103)
                pLoginResult->m_LoginResultCode = LOGIN_PASSWORD_ERROR;
            else
            {
                LOG_ERROR("Reconnect logon failed: server returned code=%d, msg=%s.",
                          resp.code, resp.msg.c_str());
                pLoginResult->m_LoginResultCode = LOGIN_FAILED;
            }
        }
        else
        {
            LOG_ERROR("Reconnect logon failed: v2 response json invalid, json=%s",
                      strReturnData.c_str());
        }
    }
    else
    {
        LOG_ERROR("Reconnect logon failed: CIUSocket::Login returned false.");
    }

    LOG_INFO("Reconnect logon result code=%d (0=SUCCESS 1=FAILED 2=UNREG 3=PWD_ERR).",
             (int)pLoginResult->m_LoginResultCode);

    ::PostMessage(m_lpFMGClient->m_UserMgr.m_hProxyWnd, FMG_MSG_LOGIN_RESULT, 0,
                  (LPARAM)pLoginResult);
}


