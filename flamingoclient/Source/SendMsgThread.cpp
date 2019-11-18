#include "stdafx.h"
#include <sstream>
#include "SendMsgThread.h"
#include "IULog.h"
#include "MiniBuffer.h"
#include "Path.h"
#include "EncodeUtil.h"
#include "FlamingoClient.h"
#include "UserMgr.h"
#include "File2.h"
#include "net/Msg.h"
#include "net/IUSocket.h"
#include "net/protocolstream.h"

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
	if(pNetData == NULL)
		return;

	switch(pNetData->m_uType)
	{
	case NET_DATA_REGISTER:
		HandleRegister((const CRegisterRequest*)pNetData);
		break;

	case NET_DATA_LOGIN:
		HandleLogon((const CLoginRequest*)pNetData);
		break;

	case NET_DATA_USER_BASIC_INFO:
		HandleUserBasicInfo((const CUserBasicInfoRequest*)pNetData);
		break;

    case NET_DATA_CHANGE_STATUS:
        HandleChangeUserStatus((const CChangeUserStatusRequest*)pNetData);
        break;

    case NET_DATA_GROUP_BASIC_INFO:
        HandleGroupBasicInfo((const CGroupBasicInfoRequest*)pNetData);
        break;
	
	case NET_DATA_FIND_FRIEND:
		HandleFindFriendMessage((const CFindFriendRequest*)pNetData);
		break;

	case NET_DATA_OPERATE_FRIEND:
		HandleOperateFriendMessage((const COperateFriendRequest*)pNetData);
		break;

	case NET_DATA_CHAT_MSG:
		HandleSentChatMessage((const CSentChatMessage*)pNetData);
		break;

	case NET_DATA_CHAT_CONFIRM_IMAGE_MSG:
		HandleSentConfirmImageMessage((const CSentChatConfirmImageMessage*)pNetData);
		break;

	case NET_DATA_HEARTBEAT:
		HandleHeartbeatMessage((const CHeartbeatMessageRequest*)pNetData);
		break;

	case NET_DATA_UPDATE_LOGON_USER_INFO:
		HandleUpdateLogonUserInfoMessage((const CUpdateLogonUserInfoRequest*)pNetData);
		break;

	case NET_DATA_MODIFY_PASSWORD:
		HandleModifyPassword((const CModifyPasswordRequest*)pNetData);
		break;

	case NET_DATA_CREATE_NEW_GROUP:
		HandleCreateNewGroup((const CCreateNewGroupRequest*)pNetData);
		break;

    case NET_DATA_OPERATE_TEAM:
        HandleOperateTeam((const CAddTeamInfoRequest*)pNetData);
        break;

    case NET_DATA_MODIFY_FRIEND_MARKNAME:
        HandleModifyFriendMarkName((const CModifyFriendMakeNameRequest*)pNetData);
        break;

    case NET_DATA_MOVE_FRIEND:
        HandleMoveFriendMessage((const CMoveFriendRequest*)pNetData);
        break;

	default:
#ifdef _DEBUG
		::MessageBox(::GetForegroundWindow(), _T("Be cautious! Unhandled data type in send queen."), _T("Warning"), MB_OK|MB_ICONERROR);
#else
		LOG_WARNING("Be cautious! Unhandled data type in send queen.");
#endif
	}

    m_seq++;
	
	delete pNetData;	
}

void CSendMsgThread::HandleRegister(const CRegisterRequest* pRegisterRequest)
{
	if (pRegisterRequest == NULL)
		return;

    char szRegisterInfo[256] = { 0 };
    sprintf_s(szRegisterInfo,
                256,
                "{\"username\": \"%s\", \"nickname\": \"%s\", \"password\": \"%s\"}",
                pRegisterRequest->m_szAccountName,
                pRegisterRequest->m_szNickName,
                pRegisterRequest->m_szPassword);

    std::string outbuf;
    net::BinaryStreamWriter writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_register);
    writeStream.WriteInt32(m_seq);
    std::string data = szRegisterInfo;
    writeStream.WriteString(data);
    writeStream.Flush();

    LOG_INFO("Request register: Account=%s, Password=*****, nickname=%s.", pRegisterRequest->m_szAccountName, pRegisterRequest->m_szPassword, pRegisterRequest->m_szNickName);

    CIUSocket::GetInstance().Send(outbuf);
}

void CSendMsgThread::HandleLogon(const CLoginRequest* pLoginRequest)
{ 
    if(pLoginRequest == NULL)
		return;

    char szLoginInfo[256] = { 0 };
    sprintf_s(szLoginInfo, 
             ARRAYSIZE(szLoginInfo), 
             "{\"username\": \"%s\", \"password\": \"%s\", \"clienttype\": %d, \"status\": %d}", 
             pLoginRequest->m_szAccountName,
             pLoginRequest->m_szPassword,
             (long)pLoginRequest->m_nLoginType,
             pLoginRequest->m_nStatus);

    
    std::string strReturnData;
    //超时时间设置为3秒
    bool bRet = CIUSocket::GetInstance().Login(pLoginRequest->m_szAccountName, pLoginRequest->m_szPassword, (long)pLoginRequest->m_nLoginType, pLoginRequest->m_nStatus, 3000, strReturnData);
    LOG_INFO("Request logon: Account=%s, Password=*****, Status=%d, LoginType=%d.", pLoginRequest->m_szAccountName, pLoginRequest->m_szPassword, pLoginRequest->m_nStatus, (long)pLoginRequest->m_nLoginType);
    
    int nRet = LOGIN_FAILED;
    CLoginResult* pLoginResult = new CLoginResult();
    pLoginResult->m_LoginResultCode = LOGIN_FAILED;
    if (bRet)
    {
        //{"code": 0, "msg": "ok", "userid": 8}
        Json::Reader JsonReader;
        Json::Value JsonRoot;
        if (JsonReader.parse(strReturnData, JsonRoot) && !JsonRoot["code"].isNull() && JsonRoot["code"].isInt())
        {
            int nRetCode = JsonRoot["code"].asInt();

            if (nRetCode == 0)
            {
                if (!JsonRoot["userid"].isInt() || !JsonRoot["username"].isString() || !JsonRoot["nickname"].isString() ||
                    !JsonRoot["facetype"].isInt() || !JsonRoot["gender"].isInt() || !JsonRoot["birthday"].isInt() ||
                    !JsonRoot["signature"].isString() || !JsonRoot["address"].isString() ||
                    !JsonRoot["customface"].isString() || !JsonRoot["phonenumber"].isString() ||
                    !JsonRoot["mail"].isString())
                {
                    LOG_ERROR(_T("login failed, login response json is invalid, json=%s"), strReturnData.c_str());
                    pLoginResult->m_LoginResultCode = LOGIN_FAILED;
                }
                else
                {
                    pLoginResult->m_LoginResultCode = 0;
                    pLoginResult->m_uAccountID = JsonRoot["userid"].asInt();
                    strcpy_s(pLoginResult->m_szAccountName, ARRAYSIZE(pLoginResult->m_szAccountName), JsonRoot["username"].asCString());
                    strcpy_s(pLoginResult->m_szNickName, ARRAYSIZE(pLoginResult->m_szNickName), JsonRoot["nickname"].asCString());
                    //pLoginResult->m_nStatus = JsonRoot["status"].asInt();
                    pLoginResult->m_nFace = JsonRoot["facetype"].asInt();
                    pLoginResult->m_nGender = JsonRoot["gender"].asInt();
                    pLoginResult->m_nBirthday = JsonRoot["birthday"].asInt();
                    strcpy_s(pLoginResult->m_szSignature, ARRAYSIZE(pLoginResult->m_szSignature), JsonRoot["signature"].asCString());
                    strcpy_s(pLoginResult->m_szAddress, ARRAYSIZE(pLoginResult->m_szAddress), JsonRoot["address"].asCString());
                    strcpy_s(pLoginResult->m_szCustomFace, ARRAYSIZE(pLoginResult->m_szCustomFace), JsonRoot["customface"].asCString());
                    strcpy_s(pLoginResult->m_szPhoneNumber, ARRAYSIZE(pLoginResult->m_szPhoneNumber), JsonRoot["phonenumber"].asCString());
                    strcpy_s(pLoginResult->m_szMail, ARRAYSIZE(pLoginResult->m_szMail), JsonRoot["mail"].asCString());
                }
            }
            else if (nRetCode == 102)
                pLoginResult->m_LoginResultCode = LOGIN_UNREGISTERED;
            else if (nRetCode == 103)
                pLoginResult->m_LoginResultCode = LOGIN_PASSWORD_ERROR;
            else
                pLoginResult->m_LoginResultCode = LOGIN_FAILED;
        }
    }

    ::PostMessage(m_lpFMGClient->m_UserMgr.m_hProxyWnd, FMG_MSG_LOGIN_RESULT, 0, (LPARAM)pLoginResult);
}

void CSendMsgThread::HandleUserBasicInfo(const CUserBasicInfoRequest* pUserBasicInfo)
{
	if(pUserBasicInfo == NULL)
		return;
	
	std::string outbuf;
	net::BinaryStreamWriter writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_getofriendlist);
    writeStream.WriteInt32(m_seq);
	std::string dummy;
	writeStream.WriteString(dummy);
	writeStream.Flush();

	LOG_INFO("Request to get userinfo.");

    CIUSocket::GetInstance().Send(outbuf);
}

void CSendMsgThread::HandleChangeUserStatus(const CChangeUserStatusRequest* pChangeUserStatusRequest)
{
    if (pChangeUserStatusRequest == NULL)
        return;

    std::string outbuf;
    net::BinaryStreamWriter writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_userstatuschange);
    writeStream.WriteInt32(m_seq);
    char szData[32] = { 0 };
    sprintf_s(szData, ARRAYSIZE(szData), "{\"type\": 1, \"onlinestatus\": %d}", pChangeUserStatusRequest->m_nNewStatus);
    writeStream.WriteCString(szData, strlen(szData));
    writeStream.Flush();

    LOG_INFO("Request to change user status, newstatus=%d.", pChangeUserStatusRequest->m_nNewStatus);

    CIUSocket::GetInstance().Send(outbuf);
}

void CSendMsgThread::HandleGroupBasicInfo(const CGroupBasicInfoRequest* pGroupBasicInfo)
{
    if (pGroupBasicInfo == NULL)
        return;

    std::string outbuf;
    net::BinaryStreamWriter writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_getgroupmembers);
    writeStream.WriteInt32(m_seq);
    char szData[32] = {0};
    sprintf_s(szData, ARRAYSIZE(szData), "{\"groupid\": %d}", pGroupBasicInfo->m_groupid);
    writeStream.WriteCString(szData, strlen(szData));
    writeStream.Flush();

    LOG_INFO("Request to get group members, groupid=%d.", pGroupBasicInfo->m_groupid);

    CIUSocket::GetInstance().Send(outbuf);
}

void CSendMsgThread::HandleFindFriendMessage(const CFindFriendRequest* pFindFriendRequest)
{
	if(pFindFriendRequest == NULL)
		return;

    char szData[64] = { 0 };
    sprintf_s(szData, 64, "{\"type\": %d, \"username\": \"%s\"}", pFindFriendRequest->m_nType, pFindFriendRequest->m_szAccountName);

    std::string outbuf;
    net::BinaryStreamWriter writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_finduser);
    writeStream.WriteInt32(m_seq);
    //std::string data = szData;
    writeStream.WriteCString(szData, strlen(szData));
    writeStream.Flush();

    LOG_INFO("Request to find friend, type=%d, accountName=%s", pFindFriendRequest->m_nType, pFindFriendRequest->m_szAccountName);

    CIUSocket::GetInstance().Send(outbuf);
}

void CSendMsgThread::HandleOperateFriendMessage(const COperateFriendRequest* pOperateFriendRequest)
{
	if(pOperateFriendRequest == NULL)
		return;

    char szData[64] = { 0 };
    if (pOperateFriendRequest->m_uCmd == Apply)
    {
        sprintf_s(szData, 64, "{\"userid\": %d, \"type\": 1}", pOperateFriendRequest->m_uAccountID);
    }
    else if (pOperateFriendRequest->m_uCmd == Agree || pOperateFriendRequest->m_uCmd == Refuse)
    {
        sprintf_s(szData, 64, "{\"userid\": %d, \"type\": 3, \"accept\": %d}", pOperateFriendRequest->m_uAccountID, pOperateFriendRequest->m_uCmd != Agree?0:1);
    }
    else if (pOperateFriendRequest->m_uCmd == Delete)
    {
        sprintf_s(szData, 64, "{\"userid\": %d, \"type\": 4}", pOperateFriendRequest->m_uAccountID);
    }

    std::string outbuf;
    net::BinaryStreamWriter writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_operatefriend);
    writeStream.WriteInt32(m_seq);
    std::string data = szData;
    writeStream.WriteCString(szData, strlen(szData));
    writeStream.Flush();

    LOG_INFO("Request to operate friend, type=%d, accountId=%u", pOperateFriendRequest->m_uCmd, pOperateFriendRequest->m_uAccountID);

    CIUSocket::GetInstance().Send(outbuf);
}

BOOL CSendMsgThread::HandleHeartbeatMessage(const CHeartbeatMessageRequest* pHeartbeatRequest)
{
	if(pHeartbeatRequest == NULL)
		return FALSE;

	//return m_pProtocol->RequestHeartbeat();
    return FALSE;
}

void CSendMsgThread::HandleUpdateLogonUserInfoMessage(const CUpdateLogonUserInfoRequest* pRequest)
{
	if(pRequest == NULL)
		return;

	char szCustomFace[MAX_PATH] = {0};
	EncodeUtil::UnicodeToUtf8(pRequest->m_szCustomFace, szCustomFace, ARRAYSIZE(szCustomFace));
	
    std::ostringstream os;
    os << "{\"nickname\": \"" << pRequest->m_szNickName << "\", \"facetype\":" << pRequest->m_uFaceID
        << ", \"customface\": \"" << szCustomFace << "\", \"gender\":" << pRequest->m_uGender
        << ", \"birthday\":" << pRequest->m_nBirthday << ", \"signature\": \"" << pRequest->m_szSignature 
        << "\",\"address\": \"" << pRequest->m_szAddress << "\", \"phonenumber\": \""
        << pRequest->m_szPhone << "\", \"mail\": \"" << pRequest->m_szMail << "\"}";


    std::string outbuf;
    net::BinaryStreamWriter writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_updateuserinfo);
    writeStream.WriteInt32(m_seq);
    writeStream.WriteString(os.str());
    writeStream.Flush();

    LOG_INFO("Request to Update User Info, data=%s", os.str().c_str());

    CIUSocket::GetInstance().Send(outbuf);
}

void CSendMsgThread::HandleModifyPassword(const CModifyPasswordRequest* pModifyPassword)
{
	if(pModifyPassword == NULL)
		return;

    char szData[256] = { 0 };
    sprintf_s(szData, ARRAYSIZE(szData), "{\"oldpassword\": \"%s\", \"newpassword\": \"%s\"}", pModifyPassword->m_szOldPassword, pModifyPassword->m_szNewPassword);
    std::string outbuf;
    net::BinaryStreamWriter writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_modifypassword);
    writeStream.WriteInt32(m_seq);
    writeStream.WriteCString(szData, strlen(szData));
    writeStream.Flush();

    LOG_INFO("Request to modify password, data=%s", szData);

    CIUSocket::GetInstance().Send(outbuf);
}

void CSendMsgThread::HandleCreateNewGroup(const CCreateNewGroupRequest* pCreateNewGroup)
{
	if(pCreateNewGroup == NULL)
		return;

    char szData[256] = { 0 };
    sprintf_s(szData, ARRAYSIZE(szData), "{\"groupname\": \"%s\", \"type\": 0}", pCreateNewGroup->m_szGroupName);
    std::string outbuf;
    net::BinaryStreamWriter writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_creategroup);
    writeStream.WriteInt32(m_seq);
    writeStream.WriteCString(szData, strlen(szData));
    writeStream.Flush();

    LOG_INFO("Request to create new group, data=%s", szData);

    CIUSocket::GetInstance().Send(outbuf);
}

void CSendMsgThread::HandleOperateTeam(const CAddTeamInfoRequest* pAddNewTeam)
{
    if (pAddNewTeam == NULL)
        return;
   
    std::string outbuf;
    net::BinaryStreamWriter writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_updateteaminfo);
    writeStream.WriteInt32(m_seq);
    std::string dummy;
    writeStream.WriteString(dummy);
    writeStream.WriteInt32(pAddNewTeam->m_opType);
    std::string strUtf8NewTeamName = EncodeUtil::UnicodeToUtf8(pAddNewTeam->m_strNewTeamName);
    writeStream.WriteString(strUtf8NewTeamName);
    std::string strUtf8OldTeamName = EncodeUtil::UnicodeToUtf8(pAddNewTeam->m_strOldTeamName);
    writeStream.WriteString(strUtf8OldTeamName);
    writeStream.Flush();

    //LOG_INFO(_T("Request to update teamname, NewTeamName=%s, OldTeamName=%s."), pAddNewTeam->m_strNewTeamName, pAddNewTeam->m_strOldTeamName);

    CIUSocket::GetInstance().Send(outbuf);
}

void CSendMsgThread::HandleModifyFriendMarkName(const CModifyFriendMakeNameRequest* pModifyFriendMakeNameRequest)
{
    if (pModifyFriendMakeNameRequest == NULL)
        return;

    std::string outbuf;
    net::BinaryStreamWriter writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_modifyfriendmarkname);
    writeStream.WriteInt32(m_seq);
    std::string dummyData;
    writeStream.WriteString(dummyData);
    writeStream.WriteInt32((int32_t)(pModifyFriendMakeNameRequest->m_uFriendID));
    char szData[64] = { 0 };
    EncodeUtil::UnicodeToUtf8(pModifyFriendMakeNameRequest->m_szNewMarkName, szData, ARRAYSIZE(szData));
    std::string newMarkName = szData;
    writeStream.WriteString(newMarkName);
    writeStream.Flush();

    LOG_INFO(_T("Request to update friend markname, friendid=%d, NewMarkName=%s."), pModifyFriendMakeNameRequest->m_uFriendID, pModifyFriendMakeNameRequest->m_szNewMarkName);

    CIUSocket::GetInstance().Send(outbuf);
}

void CSendMsgThread::HandleMoveFriendMessage(const CMoveFriendRequest* pMoveFriendRequest)
{
    if (pMoveFriendRequest == NULL)
        return;

    std::string outbuf;
    net::BinaryStreamWriter writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_movefriendtootherteam);
    writeStream.WriteInt32(m_seq);
    std::string dummy;
    writeStream.WriteString(dummy);
    writeStream.WriteInt32(pMoveFriendRequest->m_nFriendID);
    std::string strUtf8NewTeamName = EncodeUtil::UnicodeToUtf8(pMoveFriendRequest->m_strNewTeamName);
    writeStream.WriteString(strUtf8NewTeamName);
    std::string strUtf8OldTeamName = EncodeUtil::UnicodeToUtf8(pMoveFriendRequest->m_strOldTeamName);
    writeStream.WriteString(strUtf8OldTeamName);
    writeStream.Flush();

    //LOG_INFO(_T("Request to move friend, NewTeamName=%s, OldTeamName=%s."), pAddNewTeam->m_strNewTeamName, pAddNewTeam->m_strOldTeamName);

    CIUSocket::GetInstance().Send(outbuf);
}

BOOL CSendMsgThread::HandleSentChatMessage(const CSentChatMessage* pSentChatMessage)
{
	if(pSentChatMessage == NULL)
		return FALSE;

	CMsgItem* lpMsgItem = pSentChatMessage->m_pMsgItem;
	if (lpMsgItem == NULL)
	{
		return FALSE;
	}
	
	BOOL bRet = FALSE;
	switch (lpMsgItem->m_nType)
	{
	case FMG_MSG_TYPE_BUDDY:		// 好友消息
		{
			bRet = SendBuddyMsg(lpMsgItem);
			if (!bRet)
				::OutputDebugString(_T("发送好友消息失败\n"));
		}
		break;
	case FMG_MSG_TYPE_GROUP:		// 群消息
		{
			bRet = SendBuddyMsg(lpMsgItem);
			if (!bRet)
				::OutputDebugString(_T("发送群消息失败\n"));
		}
		break;
	case FMG_MSG_TYPE_MULTI:		//群发消息
		{
			bRet = SendMultiMsg(lpMsgItem);
			if (!bRet)
				::OutputDebugString(_T("群发消息失败\n"));
		}
		break;

	case FMG_MSG_TYPE_SESS:		// 群成员消息
		{
			bRet = SendSessMsg(lpMsgItem);
			if (!bRet)
				::OutputDebugString(_T("发送群成员消息失败\n"));
		}
		break;
	}
	
	if(bRet)
		delete lpMsgItem;
	
	return bRet;
}

BOOL CSendMsgThread::HandleSentConfirmImageMessage(const CSentChatConfirmImageMessage* pConfirmImageMessage)
{
	if(pConfirmImageMessage==NULL || pConfirmImageMessage->m_pszConfirmBody==NULL)
		return FALSE;

	BOOL bRet = FALSE;
	size_t nTargetCount = pConfirmImageMessage->m_setTargetIDs.size();
	std::set<UINT>::const_iterator iter = pConfirmImageMessage->m_setTargetIDs.begin();
	if(pConfirmImageMessage->m_nType == CHAT_CONFIRM_TYPE_SINGLE)
	{
		UINT uTargetID =*iter;
		//bRet = m_pProtocol->SendChatMessage(pConfirmImageMessage->m_pszConfirmBody, pConfirmImageMessage->m_uConfirmBodySize, FALSE, uTargetID, 0);
        long nChatMsgLength = pConfirmImageMessage->m_uConfirmBodySize;
        std::string outbuf;
        net::BinaryStreamWriter writeStream(&outbuf);
        writeStream.WriteInt32(msg_type_chat);
        writeStream.WriteInt32(m_seq);
        //senderId
        //writeStream.Write((int32_t)lpBuddyMsg->m_nFromUin);
        //消息内容
        writeStream.WriteCString(pConfirmImageMessage->m_pszConfirmBody, nChatMsgLength);
        //targetId
        writeStream.WriteInt32((int32_t)uTargetID);
        writeStream.Flush();

        LOG_INFO("Send chat msg: msgID=%u, senderId=%u, targetId=%u.", m_nMsgId, pConfirmImageMessage->m_uSenderID, uTargetID);

        CIUSocket::GetInstance().Send(outbuf);        
    }
	else if(pConfirmImageMessage->m_nType == CHAT_CONFIRM_TYPE_MULTI)
	{
		CMiniBuffer miniBuffer(nTargetCount*sizeof(UINT));
		UINT* pAccountIDs = (UINT*)miniBuffer.GetBuffer();
		long j = 0;
		for(; iter!=pConfirmImageMessage->m_setTargetIDs.end(); ++iter)
		{
			pAccountIDs[j] =*iter;
			++j;
		}

		bRet = SendMultiChatMessage(pConfirmImageMessage->m_pszConfirmBody, pConfirmImageMessage->m_uConfirmBodySize, pAccountIDs, nTargetCount);
	}

	if(bRet)
	{
		delete[] pConfirmImageMessage->m_pszConfirmBody;
	}

	return bRet;
}

BOOL CSendMsgThread::AddBuddyMsg(UINT nFromUin, const tstring& strFromNickName, UINT nToUin, const tstring& strToNickName, time_t nTime, const tstring& strChatMsg, HWND hwndFrom /*= NULL*/)
{
	//TODO: 还是在主线程
	CMsgItem* lpMsgItem = new CMsgItem();
	CBuddyMessage* lpBuddyMsg = new CBuddyMessage();
	
	//m_nMsgId = m_lpUserMgr->GetMsgID(nToUin);
	lpBuddyMsg->m_nMsgId = m_nMsgId;
	lpBuddyMsg->m_nTime = nTime;
    lpBuddyMsg->m_nFromUin = nFromUin;
	lpBuddyMsg->m_nToUin = nToUin;
	lpBuddyMsg->m_hwndFrom = hwndFrom;

    CreateMsgContent(strChatMsg, lpBuddyMsg->m_arrContent);

	lpMsgItem->m_nType = FMG_MSG_TYPE_BUDDY;
	lpMsgItem->m_lpMsg = (void*)lpBuddyMsg;
	lpMsgItem->m_hwndFrom = hwndFrom;
	
    //lpMsgItem->m_nUTalkNum = lpBuddyInfo->m_uUserID;
    lpMsgItem->m_strNickName = strFromNickName;
	
	CSentChatMessage* pSentChatMessage = new CSentChatMessage();
	pSentChatMessage->m_pMsgItem = lpMsgItem;
	pSentChatMessage->m_hwndChat = hwndFrom;

    //加入队列，离开主线程
	std::lock_guard<std::mutex> guard(m_mtItems);
	m_listItems.push_back(pSentChatMessage);
	m_cvItems.notify_one();

	return TRUE;
}

BOOL CSendMsgThread::AddGroupMsg(UINT nGroupId, time_t nTime, LPCTSTR lpMsg, HWND hwndFrom)
{
	if (0 == nGroupId || NULL == lpMsg || NULL ==*lpMsg)
		return FALSE;

	CMsgItem* lpMsgItem = new CMsgItem();
	CGroupMessage* lpGroupMsg = new CGroupMessage;
	
	m_nMsgId++;
	lpGroupMsg->m_nMsgId = m_nMsgId;
	lpGroupMsg->m_nTime = (UINT)nTime;
	lpGroupMsg->m_nToUin = nGroupId;
	lpGroupMsg->m_nGroupCode = nGroupId;
	lpGroupMsg->m_nSendUin = m_lpUserMgr->m_UserInfo.m_uUserID;
	lpGroupMsg->m_hwndFrom = hwndFrom;
	//if (m_lpUserMgr != NULL)
	//	lpGroupMsg->m_nGroupCode = m_lpUserMgr->m_GroupList.GetGroupCodeById(nGroupId);

	CreateMsgContent(lpMsg, lpGroupMsg->m_arrContent);

	lpMsgItem->m_nType = FMG_MSG_TYPE_GROUP;
	lpMsgItem->m_lpMsg = (void*)lpGroupMsg;
	// 使用到未加锁的主线程数据，不要在主线程之外的线程调用此函数
	CGroupInfo* lpGroupInfo = m_lpUserMgr->m_GroupList.GetGroupByCode(nGroupId);
	if (lpGroupInfo != NULL)
	{
		lpMsgItem->m_nGroupNum = nGroupId;
		lpMsgItem->m_nUTalkNum = m_lpUserMgr->m_UserInfo.m_uUserID;
		CBuddyInfo* lpBuddyInfo = lpGroupInfo->GetMemberByUin(m_lpUserMgr->m_UserInfo.m_uUserID);
		if (lpBuddyInfo != NULL)
		{
			lpMsgItem->m_strNickName = lpBuddyInfo->m_strNickName;
		}
	}

	CSentChatMessage* pSentChatMessage = new CSentChatMessage();
	pSentChatMessage->m_pMsgItem = lpMsgItem;
	pSentChatMessage->m_hwndChat = hwndFrom;
	
	
	std::lock_guard<std::mutex> guard(m_mtItems);
	m_listItems.push_back(pSentChatMessage);
	m_cvItems.notify_one();

	return TRUE;
}

// 使用到未加锁的主线程数据，不要在主线程之外的线程调用此函数
BOOL CSendMsgThread::AddMultiMsg(const std::set<UINT> setAccountID, time_t nTime, LPCTSTR lpMsg, HWND hwndFrom/*=NULL*/)
{
	if (setAccountID.empty() || NULL == lpMsg || NULL ==*lpMsg)
		return FALSE;

	CMsgItem* lpMsgItem = new CMsgItem();

	CBuddyMessage* lpBuddyMsg = new CBuddyMessage();

	//m_nMsgId = m_lpUserMgr->GetMsgID(nToUin);
	lpBuddyMsg->m_nMsgId = m_nMsgId;
	lpBuddyMsg->m_nTime = nTime;
	lpBuddyMsg->m_nFromUin = m_lpUserMgr->m_UserInfo.m_uUserID;
	lpBuddyMsg->m_nToUin = -1;
	lpBuddyMsg->m_hwndFrom = hwndFrom;
	lpBuddyMsg->m_nMsgType = (CONTENT_TYPE)FMG_MSG_TYPE_MULTI;
	for(std::set<UINT>::const_iterator iter=setAccountID.begin(); iter!=setAccountID.end(); ++iter)
	{
		lpMsgItem->m_arrTargetIDs.push_back(*iter);
	}

	CreateMsgContent(lpMsg, lpBuddyMsg->m_arrContent);

	lpMsgItem->m_nType = FMG_MSG_TYPE_MULTI;
	lpMsgItem->m_lpMsg = (void*)lpBuddyMsg;
	lpMsgItem->m_hwndFrom = hwndFrom;
	lpMsgItem->m_nUTalkNum = -1;
		
	CSentChatMessage* pSentChatMessage = new CSentChatMessage();
	pSentChatMessage->m_pMsgItem = lpMsgItem;
	pSentChatMessage->m_hwndChat = hwndFrom;

	std::lock_guard<std::mutex> guard(m_mtItems);
	m_listItems.push_back(pSentChatMessage);
	m_cvItems.notify_one();

	return TRUE;
}

BOOL CSendMsgThread::AddSessMsg(UINT nGroupId, UINT nToUin, time_t nTime, LPCTSTR lpMsg)
{
	if (0 == nGroupId || 0 == nToUin || NULL == lpMsg || NULL ==*lpMsg)
		return FALSE;

	CMsgItem* lpMsgItem = new CMsgItem;
	if (NULL == lpMsgItem)
		return FALSE;

	CSessMessage* lpSessMsg = new CSessMessage;
	if (NULL == lpSessMsg)
	{
		delete lpMsgItem;
		return FALSE;
	}

	m_nMsgId++;
	lpSessMsg->m_nMsgId = m_nMsgId;
	lpSessMsg->m_nTime = (UINT)nTime;
	lpSessMsg->m_nToUin = nToUin;
	lpSessMsg->m_nGroupId = nGroupId;
	
	CreateMsgContent(lpMsg, lpSessMsg->m_arrContent);

	lpMsgItem->m_nType = FMG_MSG_TYPE_SESS;
	lpMsgItem->m_lpMsg = (void*)lpSessMsg;
	if (m_lpUserMgr != NULL)	// 使用到未加锁的主线程数据，不要在主线程之外的线程调用此函数
	{
		lpMsgItem->m_strNickName = m_lpUserMgr->m_UserInfo.m_strNickName;

		CBuddyInfo* lpBuddyInfo = m_lpUserMgr->m_GroupList.GetGroupMemberById(nGroupId, nToUin);
		if (lpBuddyInfo != NULL)
		{
			lpMsgItem->m_nUTalkNum = lpBuddyInfo->m_uUserID;
		}
	}

	//::EnterCriticalSection(&m_csItem);
	//m_arrItem.push_back(lpMsgItem);
	//::LeaveCriticalSection(&m_csItem);

	//::ReleaseSemaphore(m_hSemaphore, 1, NULL);


	return TRUE;
}

BOOL CSendMsgThread::HandleFontInfo(LPCTSTR& p, tstring& strText, std::vector<CContent*>& arrContent)
{
	tstring strTemp = GetBetweenString(p+2, _T("[\""), _T("\"]")).c_str();
	if (!strTemp.empty())
	{
		LPCTSTR lpFontFmt = _T("%[^,],%d,%[^,],%d,%d,%d");
		TCHAR szName[32] = _T("宋体");
		TCHAR szColor[32] = _T("000000");
		int nSize = 9;
		BOOL bBold = FALSE, bItalic = FALSE, bUnderLine = FALSE;

		int nCount = _stscanf(strTemp.c_str(), lpFontFmt, szName, 
			&nSize, &szColor, &bBold, &bItalic, &bUnderLine);
		if (nCount != 6)
			return FALSE;

		if (!strText.empty())
		{
			CContent* lpContent = new CContent;
			if (lpContent != NULL)
			{
				lpContent->m_nType = CONTENT_TYPE_TEXT;
				lpContent->m_strText = strText;
				arrContent.push_back(lpContent);
			}
			strText = _T("");
		}

		CContent* lpContent = new CContent;
		if (lpContent != NULL)
		{
			lpContent->m_nType = CONTENT_TYPE_FONT_INFO;
			lpContent->m_FontInfo.m_nSize = nSize;
			lpContent->m_FontInfo.m_clrText = HexStrToRGB(szColor);
			lpContent->m_FontInfo.m_strName = szName;
			lpContent->m_FontInfo.m_bBold = bBold;
			lpContent->m_FontInfo.m_bItalic = bItalic;				
			lpContent->m_FontInfo.m_bUnderLine = bUnderLine;
			arrContent.push_back(lpContent);
		}

		p = _tcsstr(p+2, _T("\"]"));
		p++;
		return TRUE;
	}
	return FALSE;
}

BOOL CSendMsgThread::HandleSysFaceId(LPCTSTR& p, tstring& strText, std::vector<CContent*>& arrContent)
{
	int nFaceId = GetBetweenInt(p+2, _T("[\""), _T("\"]"), -1);
	if (nFaceId != -1)
	{
		if (!strText.empty())
		{
			CContent* lpContent = new CContent;
			if (lpContent != NULL)
			{
				lpContent->m_nType = CONTENT_TYPE_TEXT;
				lpContent->m_strText = strText;
				arrContent.push_back(lpContent);
			}
			strText = _T("");
		}

		CContent* lpContent = new CContent;
		if (lpContent != NULL)
		{
			lpContent->m_nType = CONTENT_TYPE_FACE;
			lpContent->m_nFaceId = nFaceId;
			arrContent.push_back(lpContent);
		}

		p = _tcsstr(p+2, _T("\"]"));
		p++;
		return TRUE;
	}
	return FALSE;
}

BOOL CSendMsgThread::HandleShakeWindowMsg(LPCTSTR& p, tstring& strText, std::vector<CContent*>& arrContent)
{
	int nShakeTimes = GetBetweenInt(p+2, _T("[\""), _T("\"]"), -1);
	if (nShakeTimes > 0)
	{
		if (!strText.empty())
		{
			CContent* lpContent = new CContent;
			if (lpContent != NULL)
			{
				lpContent->m_nType = CONTENT_TYPE_TEXT;
				lpContent->m_strText = strText;
				arrContent.push_back(lpContent);
			}
			strText = _T("");
		}

		CContent* lpContent = new CContent;
		if (lpContent != NULL)
		{
			lpContent->m_nType = CONTENT_TYPE_SHAKE_WINDOW;
			lpContent->m_nShakeTimes = nShakeTimes;
			arrContent.push_back(lpContent);
		}

		p = _tcsstr(p+2, _T("\"]"));
		p++;
		return TRUE;
	}
	return FALSE;
}

BOOL CSendMsgThread::HandleCustomPic(LPCTSTR& p, tstring& strText, std::vector<CContent*>& arrContent)
{
	tstring strFileName = GetBetweenString(p+2, _T("[\""), _T("\"]"));
	if (!strFileName.empty())
	{
		if (!strText.empty())
		{
			CContent* lpContent = new CContent;
			if (lpContent != NULL)
			{
				lpContent->m_nType = CONTENT_TYPE_TEXT;
				lpContent->m_strText = strText;
				arrContent.push_back(lpContent);
			}
			strText = _T("");
		}

		CContent* lpContent = new CContent();
		if (lpContent != NULL)
		{
			lpContent->m_nType = CONTENT_TYPE_CHAT_IMAGE;
			lpContent->m_CFaceInfo.m_strFileName = Hootina::CPath::GetFileName(strFileName.c_str());
			lpContent->m_CFaceInfo.m_strFilePath = strFileName;
			arrContent.push_back(lpContent);
		}

		p = _tcsstr(p+2, _T("\"]"));
		p++;
		return TRUE;
	}
	return FALSE;
}

BOOL CSendMsgThread::HandleFile(LPCTSTR& p, tstring& strText, std::vector<CContent*>& arrContent)
{
	tstring strFileName = GetBetweenString(p+2, _T("[\""), _T("\"]"));
	if (!strFileName.empty())
	{
		LPCTSTR lpFileFmt = _T("%[^,],%[^,],%d,%d");
		TCHAR szFilePath[MAX_PATH] = {0};
		TCHAR szFileName[MAX_PATH] = {0};
		long nFileSize = 0;
		BOOL bOnline = TRUE;

		int nCount = _stscanf(strFileName.c_str(), lpFileFmt, szFileName, szFilePath, &nFileSize, &bOnline);
		if (nCount != 4)
			return FALSE;
		
		
		if (!strText.empty())
		{
			CContent* lpContent = new CContent;
			if (lpContent != NULL)
			{
				lpContent->m_nType = CONTENT_TYPE_TEXT;
				lpContent->m_strText = strText;
				arrContent.push_back(lpContent);
			}
			strText = _T("");
		}

		CContent* lpContent = new CContent;
		if (lpContent != NULL)
		{
			lpContent->m_nType = CONTENT_TYPE_FILE;
			lpContent->m_CFaceInfo.m_strFilePath = szFilePath;
			lpContent->m_CFaceInfo.m_strName = szFileName;
			lpContent->m_CFaceInfo.m_strFileName = szFileName;
			lpContent->m_CFaceInfo.m_dwFileSize = (DWORD)nFileSize;
			lpContent->m_CFaceInfo.m_bOnline = bOnline;

			arrContent.push_back(lpContent);
		}

		p = _tcsstr(p+2, _T("\"]"));
		p++;
		return TRUE;
	}
	return FALSE;
}

BOOL CSendMsgThread::HandleRemoteDesktop(LPCTSTR& p, tstring& strText, std::vector<CContent*>& arrContent)
{
    int nPlaceholder = GetBetweenInt(p + 2, _T("[\""), _T("\"]"), -1);
    if (nPlaceholder > 0)
    {
        if (!strText.empty())
        {
            CContent* lpContent = new CContent;
            if (lpContent != NULL)
            {
                lpContent->m_nType = CONTENT_TYPE_TEXT;
                lpContent->m_strText = strText;
                arrContent.push_back(lpContent);
            }
            strText = _T("");
        }

        CContent* lpContent = new CContent;
        if (lpContent != NULL)
        {
            lpContent->m_nType = CONTENT_TYPE_REMOTE_DESKTOP;
            //lpContent->m_nShakeTimes = nPlaceholder;
            arrContent.push_back(lpContent);
        }

        p = _tcsstr(p + 2, _T("\"]"));
        p++;
        return TRUE;
    }
    return FALSE;
}


BOOL CSendMsgThread::CreateMsgContent(const tstring& strChatMsg, std::vector<CContent*>& arrContent)
{
	tstring strText;
	CContent* lpContent;
    for (LPCTSTR p = strChatMsg.c_str();*p != _T('\0'); p++)
	{
		if (*p == _T('/'))
		{
			if (*(p+1) == _T('/'))
			{
				strText +=*p;
				p++;
				continue;
			}
			else if (*(p+1) == _T('o'))						//字体信息
			{
				if (HandleFontInfo(p, strText, arrContent))
					continue;
			}
			else if (*(p+1) == _T('f'))						//表情信息
			{
				if (HandleSysFaceId(p, strText, arrContent))
					continue;
			}
			else if(*(p+1) == _T('s'))						//窗口抖动
			{
				if (HandleShakeWindowMsg(p, strText, arrContent))
					continue;
			}
			else if (*(p+1) == _T('c'))						//自定义图片
			{
				if (HandleCustomPic(p, strText, arrContent))
					continue;
			}
			else if (*(p+1) == _T('i'))						//文件
			{
				if (HandleFile(p, strText, arrContent))
					continue;
			}
            else if (*(p + 1) == _T('r'))
            {
                if (HandleRemoteDesktop(p, strText, arrContent))     //远程桌面
                    continue;
            }
		}
		strText +=*p;
	}

	if (!strText.empty())
	{
		lpContent = new CContent;
		if (lpContent != NULL)
		{
			lpContent->m_nType = CONTENT_TYPE_TEXT;
			lpContent->m_strText = strText;
			arrContent.push_back(lpContent);
		}
		strText = _T("");
	}

	return TRUE;
}


// 发送好友消息
BOOL CSendMsgThread::SendBuddyMsg(CMsgItem* lpMsgItem)
{
	if (NULL == lpMsgItem || NULL == lpMsgItem->m_lpMsg)
		return FALSE;

	CBuddyMessage* lpMsg = (CBuddyMessage*)lpMsgItem->m_lpMsg;
	std::vector<CContent*>& arrContent = lpMsg->m_arrContent;
	HWND hWndFrom = lpMsg->m_hwndFrom;
	//CUploadBuddyChatPicResult uploadPicResult;
	BOOL bRet;
	CContent* lpContent = NULL;
	CFileItemRequest* pFileItemRequest = NULL;
	
	// 上传自定义图片
	CString strDestPath;
	TCHAR szMd5[64];
	size_t nSize = arrContent.size();
	for (size_t i = 0; i < nSize; ++i)	
	{
		lpContent = arrContent[i];
		if(lpContent==NULL || CONTENT_TYPE_CHAT_IMAGE!=lpContent->m_nType)
			continue;
		//将文件拷贝至聊天记录文件夹
		memset(szMd5, 0, sizeof(szMd5));
		//TODO: 获取文件md5值失败怎么办？
		if(!GetFileMd5ValueW(lpContent->m_CFaceInfo.m_strFilePath.c_str(), szMd5, ARRAYSIZE(szMd5)))
			continue;
		strDestPath.Format(_T("%s%s.%s"), m_lpUserMgr->GetChatPicFolder().c_str(), szMd5, Hootina::CPath::GetExtension(lpContent->m_CFaceInfo.m_strFilePath.c_str()).c_str());
		::CopyFile(lpContent->m_CFaceInfo.m_strFilePath.c_str(), strDestPath, FALSE);

		pFileItemRequest = new CFileItemRequest();
		pFileItemRequest->m_hwndReflection = hWndFrom;
		_tcscpy_s(pFileItemRequest->m_szFilePath, ARRAYSIZE(pFileItemRequest->m_szFilePath), lpContent->m_CFaceInfo.m_strFilePath.c_str());
		pFileItemRequest->m_uSenderID = lpMsg->m_nFromUin;
		pFileItemRequest->m_setTargetIDs.insert(lpMsg->m_nToUin);
		pFileItemRequest->m_nFileType = FILE_ITEM_UPLOAD_CHAT_IMAGE;

        m_lpFMGClient->m_ImageTask.AddItem(pFileItemRequest);

		lpContent->m_CFaceInfo.m_strFileName = Hootina::CPath::GetFileName(strDestPath);
	}
	
    
	//加工消息内容
	bRet = ProcessBuddyMsg(lpMsg);
	if (!bRet)
	{
		::PostMessage(hWndFrom, FMG_MSG_SENDCHATMSG_RESULT, SEND_WHOLE_MSG_FAILED, 0);
		return FALSE;
	}
	
	//群信息
	if(IsGroupTarget(lpMsg->m_nToUin))
		WriteGroupMsgLog(m_lpUserMgr, lpMsg->m_nToUin, m_lpUserMgr->m_UserInfo.m_uUserID, m_lpUserMgr->m_UserInfo.m_strNickName.c_str(), lpMsg);
	else
        WriteBuddyMsgLog(m_lpUserMgr, lpMsg->m_nToUin, lpMsgItem->m_strNickName.c_str(), TRUE, lpMsg);
		//WriteBuddyMsgLog(m_lpUserMgr, lpMsgItem->m_nUTalkNum, lpMsgItem->m_strNickName.c_str(), TRUE, lpMsg);

	return TRUE;
}

BOOL CSendMsgThread::SendMultiMsg(CMsgItem* lpMsgItem)
{
	if (NULL == lpMsgItem || NULL == lpMsgItem->m_lpMsg)
		return FALSE;

	CBuddyMessage* lpMsg = (CBuddyMessage*)lpMsgItem->m_lpMsg;
	std::vector<CContent*>& arrContent = lpMsg->m_arrContent;
	HWND hWndFrom = lpMsg->m_hwndFrom;
	//CUploadBuddyChatPicResult uploadPicResult;
	BOOL bRet;
	CContent* lpContent = NULL;
	CFileItemRequest* pFileItemRequest = NULL;
	
	// 上传自定义图片
	CString strDestPath;
	TCHAR szMd5[64];
	size_t nSize = arrContent.size();
	for (size_t i = 0; i < nSize; ++i)	
	{
		lpContent = arrContent[i];
		if(lpContent==NULL || CONTENT_TYPE_CHAT_IMAGE!=lpContent->m_nType)
			continue;
		//将文件拷贝至聊天记录文件夹
		memset(szMd5, 0, sizeof(szMd5));
		//TODO: 获取文件md5值失败怎么办？
		if(!GetFileMd5ValueW(lpContent->m_CFaceInfo.m_strFilePath.c_str(), szMd5, ARRAYSIZE(szMd5)))
			continue;
		strDestPath.Format(_T("%s%s.%s"), m_lpUserMgr->GetChatPicFolder().c_str(), szMd5, Hootina::CPath::GetExtension(lpContent->m_CFaceInfo.m_strFilePath.c_str()).c_str());
		::CopyFile(lpContent->m_CFaceInfo.m_strFilePath.c_str(), strDestPath, FALSE);

		pFileItemRequest = new CFileItemRequest();
		pFileItemRequest->m_hwndReflection = hWndFrom;
		_tcscpy_s(pFileItemRequest->m_szFilePath, ARRAYSIZE(pFileItemRequest->m_szFilePath), lpContent->m_CFaceInfo.m_strFilePath.c_str());
		pFileItemRequest->m_uSenderID = lpMsg->m_nFromUin;
		pFileItemRequest->m_nFileType = FILE_ITEM_UPLOAD_CHAT_IMAGE;
		pFileItemRequest->m_setTargetIDs.insert(lpMsgItem->m_arrTargetIDs.begin(), lpMsgItem->m_arrTargetIDs.end());

        m_lpFMGClient->m_ImageTask.AddItem(pFileItemRequest);

		lpContent->m_CFaceInfo.m_strFileName = Hootina::CPath::GetFileName(strDestPath);
	}
	

	//加工消息内容
	bRet = ProcessMultiMsg(lpMsgItem);
	if (!bRet)
	{
		::PostMessage(hWndFrom, FMG_MSG_SENDCHATMSG_RESULT, SEND_WHOLE_MSG_FAILED, 0);
		return FALSE;
	}
	
	//群信息
	if(IsGroupTarget(lpMsg->m_nToUin))
		WriteGroupMsgLog(m_lpUserMgr, lpMsg->m_nToUin, m_lpUserMgr->m_UserInfo.m_uUserID, m_lpUserMgr->m_UserInfo.m_strNickName.c_str(), lpMsg);
	else
		WriteBuddyMsgLog(m_lpUserMgr, lpMsgItem->m_nUTalkNum, lpMsgItem->m_strNickName.c_str(), TRUE, lpMsg);

	return TRUE;
}

BOOL CSendMsgThread::SendMultiChatMessage(const char* pszChatMsg, int nChatMsgLength, UINT* pAccountList, int nAccountNum)
{
    std::string outbuf;
    net::BinaryStreamWriter writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_multichat);
    writeStream.WriteInt32(m_seq);
    //senderId
    //writeStream.Write((int32_t)lpBuddyMsg->m_nFromUin);
    //消息内容
    writeStream.WriteCString(pszChatMsg, nChatMsgLength);
    //targetId

    std::ostringstream osTargets;
    osTargets << "{\"targets\":[";
    for (int i = 0; i < nAccountNum; ++i)
    {
        osTargets << pAccountList[i] << ",";
    }

    //去除最后一个多余的逗号
    std::string strTarget = osTargets.str().substr(0, osTargets.str().length() - 1);
    strTarget += "]}";

    writeStream.WriteString(strTarget);
    writeStream.Flush();

    LOG_INFO("Send multi chat msg: nAccountNum=%d.", nAccountNum);

    CIUSocket::GetInstance().Send(outbuf);
    
    //TODO: 不需要返回值的
    return TRUE;
}

// 发送群消息
BOOL CSendMsgThread::SendGroupMsg(CMsgItem* lpMsgItem)
{
	return SendBuddyMsg(lpMsgItem);
	
	//if (NULL == lpMsgItem || NULL == lpMsgItem->m_lpMsg)
	//	return FALSE;

	//CGroupMessage* lpMsg = (CGroupMessage*)lpMsgItem->m_lpMsg;
	//std::vector<CContent*>& arrContent = lpMsg->m_arrContent;
	//CUploadGroupChatPicResult uploadPicResult;
	//CGetGroupFaceSigResult sigResult;
	//CSendGroupMsgResult sendMsgResult;
	//BOOL bHasCustomFace = FALSE;
	//int nRetry = 3;		//重试次数
	//BOOL bRet;

	//for (int i = 0; i < (int)arrContent.size(); i++)	// 上传自定义表情
	//{
	//	CContent* lpContent = arrContent[i];
	//	if (lpContent != NULL && CONTENT_TYPE_CUSTOM_FACE == lpContent->m_nType)
	//	{
	//		bHasCustomFace = TRUE;

	//		for (int j = 0; j < nRetry; j++)
	//		{
	//			bRet = UploadGroupChatPic(lpContent->m_CFaceInfo.m_strName.c_str(), uploadPicResult);
	//			if (bRet)
	//				break;
	//		}

	//		if (!bRet)
	//			return FALSE;

	//		lpContent->m_CFaceInfo.m_strFilePath = uploadPicResult.m_strFilePath;
	//	}
	//}

	//if (bHasCustomFace && (m_strGFaceKey.empty() || m_strGFaceSig.empty()))
	//{
	//	//bRet = m_lpUTalkProtocol->GetGroupFaceSignal(m_HttpClient, WEBUTalk_CLIENT_ID, 
	//	//	m_lpUserMgr->m_LoginResult2.m_strPSessionId.c_str(), &sigResult);
	//	//if (!bRet || (sigResult.m_nRetCode != 0))
	//	//	return FALSE;

	//	//m_strGFaceKey = sigResult.m_strGFaceKey;
	//	//m_strGFaceSig = sigResult.m_strGFaceSig;
	//}

	//bRet = m_lpUTalkProtocol->SendGroupMsg(m_HttpClient, lpMsg, 
	//	WEBUTalk_CLIENT_ID, m_lpUserMgr->m_LoginResult2.m_strPSessionId.c_str(), 
	//	m_strGFaceKey.c_str(), m_strGFaceSig.c_str(), &sendMsgResult);
	//if (!bRet || (sendMsgResult.m_nRetCode != 0))
	//	return FALSE;

	//WriteGroupMsgLog(m_lpUserMgr, lpMsgItem->m_nGroupNum, 
	//	lpMsgItem->m_nUTalkNum, lpMsgItem->m_strNickName.c_str(), lpMsg);

	//return TRUE;
}

// 发送群成员消息
BOOL CSendMsgThread::SendSessMsg(CMsgItem* lpMsgItem)
{
	/*if (NULL == lpMsgItem || NULL == lpMsgItem->m_lpMsg)
		return FALSE;

	CSessMessage* lpMsg = (CSessMessage*)lpMsgItem->m_lpMsg;
	CSendSessMsgResult sendMsgResult;
	BOOL bRet;

	if (lpMsgItem->m_strGroupSig.empty())
	{
		CGetC2CMsgSigResult* lpGetC2CMsgSigResult = new CGetC2CMsgSigResult;
		if (NULL == lpGetC2CMsgSigResult)
			return FALSE;
		
		bRet = m_lpUTalkProtocol->GetC2CMsgSignal(m_HttpClient, 
			lpMsg->m_nGroupId, lpMsg->m_nToUin, WEBUTalk_CLIENT_ID, 
			m_lpUserMgr->m_LoginResult2.m_strPSessionId.c_str(), lpGetC2CMsgSigResult);
		if (!bRet || lpGetC2CMsgSigResult->m_nRetCode != 0)
		{
			delete lpGetC2CMsgSigResult;
			lpGetC2CMsgSigResult = NULL;
			return FALSE;
		}
		lpMsgItem->m_strGroupSig = lpGetC2CMsgSigResult->m_strValue;
		lpGetC2CMsgSigResult->m_nGroupId = lpMsg->m_nGroupId;
		lpGetC2CMsgSigResult->m_nUTalkUin = lpMsg->m_nToUin;
		::PostMessage(m_lpUserMgr->m_hProxyWnd, 
			FMG_MSG_UPDATE_C2CMSGSIG, 0, (LPARAM)lpGetC2CMsgSigResult);
	}

	bRet = m_lpUTalkProtocol->SendSessMsg(m_HttpClient, lpMsg, lpMsgItem->m_strGroupSig.c_str(), 
		WEBUTalk_CLIENT_ID, m_lpUserMgr->m_LoginResult2.m_strPSessionId.c_str(), &sendMsgResult);
	if (!bRet || (sendMsgResult.m_nRetCode != 0))
		return FALSE;

	WriteSessMsgLog(m_lpUserMgr, lpMsgItem->m_nUTalkNum, lpMsgItem->m_strNickName.c_str(), TRUE, lpMsg);*/

	return TRUE;
}

BOOL CSendMsgThread::ProcessBuddyMsg(CBuddyMessage* lpBuddyMsg)
{
	if (NULL==lpBuddyMsg)
 		return FALSE;
	
	TCHAR cBuf[512] = {0};
 
	CString strChatContent;
	CString strEscape;
	CString strFont;
	const long MAX_RECENT_MSG_LENGTH = 16;
	CString strRecentMsg;
	long nImageCount = 0;
	std::vector<CString> aryImageInfo;
	long nImageWidth = 0;
	long nImageHeight = 0;
	
	//群组前面加上具体的"某某人说："
    if (IsGroupTarget(lpBuddyMsg->m_nToUin))
	{
		strRecentMsg.Format(_T("%s:"), m_lpUserMgr->GetNickName(lpBuddyMsg->m_nFromUin).c_str());
	}
	for (int i = 0; i < (int)lpBuddyMsg->m_arrContent.size(); i++)
	{
		CContent* lpContent = lpBuddyMsg->m_arrContent[i];
		if(lpContent == NULL)
			continue;

		if (lpContent->m_nType == CONTENT_TYPE_TEXT)							//文本
		{
			//strContent += _T("\\\"");
			//strContent += UnicodeToHexStr(lpContent->m_strText.c_str(), TRUE);
			//strContent += _T("\\\",");

			strChatContent += _T("{\"msgText\":\"");
			strEscape = lpContent->m_strText.data();
			//对双引号和反斜杠进行转义
			strEscape.Replace(_T("\\"), _T("\\\\"));
			strEscape.Replace(_T("\""), _T("\\\""));
			strChatContent += strEscape;
			strChatContent += _T("\"},");

			strRecentMsg += lpContent->m_strText.data();
		}
		else if (lpContent->m_nType == CONTENT_TYPE_FONT_INFO)			//字体
		{
			strFont.Format(_T("\"font\":[\"%s\",%d,%d,%d,%d,%d],"),
							lpContent->m_FontInfo.m_strName.data(),
							lpContent->m_FontInfo.m_nSize,
							lpContent->m_FontInfo.m_clrText,
							lpContent->m_FontInfo.m_bBold,
							lpContent->m_FontInfo.m_bItalic,
							lpContent->m_FontInfo.m_bUnderLine);
		}
		else if (lpContent->m_nType == CONTENT_TYPE_FACE)				//表情
		{
			strChatContent += _T("{\"faceID\":");
			memset(cBuf, 0, sizeof(cBuf));
			wsprintf(cBuf, _T("%d"), lpContent->m_nFaceId);
			strChatContent += cBuf;
			strChatContent += _T("},");

			if(strRecentMsg.GetLength()<MAX_RECENT_MSG_LENGTH && strRecentMsg.GetLength()+4<=MAX_RECENT_MSG_LENGTH)
				strRecentMsg += _T("[表情]");
		}
		else if (lpContent->m_nType == CONTENT_TYPE_SHAKE_WINDOW)			//窗口抖动
		{
			strChatContent += _T("{\"shake\":1},");
			if(strRecentMsg.GetLength()<MAX_RECENT_MSG_LENGTH && strRecentMsg.GetLength()+6<=MAX_RECENT_MSG_LENGTH)
				strRecentMsg += _T("[窗口抖动]");
		}
		else if (lpContent->m_nType == CONTENT_TYPE_CHAT_IMAGE)			//图片
		{
			GetImageWidthAndHeight(lpContent->m_CFaceInfo.m_strFilePath.c_str(), nImageWidth, nImageHeight);
			memset(cBuf, 0, sizeof(cBuf));
			wsprintf(cBuf, _T("{\"pic\":[\"%s\",\"%s\",%u,%d,%d]},"), 
					lpContent->m_CFaceInfo.m_strFileName.c_str(),
					lpContent->m_CFaceInfo.m_strFileName.c_str(),
					lpContent->m_CFaceInfo.m_dwFileSize,
					nImageWidth,
					nImageHeight);
			strChatContent += cBuf;
			++nImageCount;
			aryImageInfo.push_back(cBuf);

			if(strRecentMsg.GetLength()<MAX_RECENT_MSG_LENGTH && strRecentMsg.GetLength()+4<=MAX_RECENT_MSG_LENGTH)
				strRecentMsg += _T("[图片]");
		}
		else if (lpContent->m_nType == CONTENT_TYPE_FILE)			//文件
		{
			memset(cBuf, 0, sizeof(cBuf));
			wsprintf(cBuf, _T("{\"file\":[\"%s\",\"%s\",%u,%d]},"), 
					lpContent->m_CFaceInfo.m_strFileName.c_str(),
					lpContent->m_CFaceInfo.m_strFilePath.c_str(),
					lpContent->m_CFaceInfo.m_dwFileSize,
					lpContent->m_CFaceInfo.m_bOnline);
			strChatContent += cBuf;

			if(strRecentMsg.GetLength()<MAX_RECENT_MSG_LENGTH && strRecentMsg.GetLength()+4<=MAX_RECENT_MSG_LENGTH)
				strRecentMsg += _T("[文件]");

		}
        else if (lpContent->m_nType == CONTENT_TYPE_REMOTE_DESKTOP)
        {
            strChatContent += _T("{\"remotedesktop\":1},");
            if (strRecentMsg.GetLength()<MAX_RECENT_MSG_LENGTH && strRecentMsg.GetLength() + 6 <= MAX_RECENT_MSG_LENGTH)
                strRecentMsg += _T("[远程桌面]");
        }
		
	}

	strRecentMsg = strRecentMsg.Left(MAX_RECENT_MSG_LENGTH);

	strChatContent.TrimRight(_T(","));

	CString strContent;
	strContent.Format(_T("{\"msgType\":1,\"time\":%llu,\"clientType\":1,"), lpBuddyMsg->m_nTime);
	//strContent.Format(_T("{\"time\":%llu,\"clientType\":1,"), lpBuddyMsg->m_nTime);
	
	//加上字体信息
	strContent += strFont;

	//加上聊天正文内容
	strContent += _T("\"content\":[");
	strContent += strChatContent;

	strContent += _T("]}");

	//发送前先转换成utf8格式
	//utf8存储中文时，对应2～4个字节
	long nLength = strContent.GetLength()*4;
	CStringA strUtf8Msg;
	EncodeUtil::UnicodeToUtf8(strContent, strUtf8Msg.GetBuffer(nLength), nLength);
	strUtf8Msg.ReleaseBuffer();

    long nChatMsgLength = strUtf8Msg.GetLength();
    std::string outbuf;
    net::BinaryStreamWriter writeStream(&outbuf);
    writeStream.WriteInt32(msg_type_chat);
    writeStream.WriteInt32(m_seq);
    //senderId
    //writeStream.Write((int32_t)lpBuddyMsg->m_nFromUin);
    //消息内容
    writeStream.WriteCString(strUtf8Msg, nChatMsgLength);
    //targetId
    writeStream.WriteInt32((int32_t)lpBuddyMsg->m_nToUin);
    writeStream.Flush();

    CIUSocket::GetInstance().Send(outbuf);
    LOG_INFO("Send chat msg: msgID=%u, senderId=%u, targetId=%u.", m_nMsgId, lpBuddyMsg->m_nFromUin, lpBuddyMsg->m_nToUin);
		
	//TODO: 这个大的个人信息类需要整改
    //只有发送成功的消息才会被添加到最近聊天列表中
	UINT uFaceID = m_lpUserMgr->GetFaceID(lpBuddyMsg->m_nToUin);
	tstring strNickName;
    if (IsGroupTarget(lpBuddyMsg->m_nToUin))
		strNickName = m_lpUserMgr->GetGroupName(lpBuddyMsg->m_nToUin);
	else
		strNickName = m_lpUserMgr->GetNickName(lpBuddyMsg->m_nToUin);
		
	AddItemToRecentSessionList(lpBuddyMsg->m_nToUin, 0/*uFaceID*/, strNickName.c_str(), strRecentMsg, lpBuddyMsg->m_nTime);

	
	return TRUE;
}

BOOL CSendMsgThread::ProcessMultiMsg(CMsgItem* pMsgItem)
{
	if(pMsgItem == NULL)
		return FALSE;

	CBuddyMessage* lpBuddyMsg = (CBuddyMessage*)pMsgItem->m_lpMsg;
	if (NULL == lpBuddyMsg)
 		return FALSE;
	
	TCHAR cBuf[512] = {0};
 
	CString strChatContent;
	CString strEscape;
	CString strFont;
	const long MAX_RECENT_MSG_LENGTH = 16;
	CString strRecentMsg;
	long nImageCount = 0;
	std::vector<CString> aryImageInfo;
	long nImageWidth = 0;
	long nImageHeight = 0;
	
	//群组前面加上具体的"某某人说："
	if(IsGroupTarget(lpBuddyMsg->m_nToUin))
	{
		strRecentMsg.Format(_T("%s:"), m_lpUserMgr->GetNickName(lpBuddyMsg->m_nFromUin).c_str());
	}
	for (int i = 0; i < (int)lpBuddyMsg->m_arrContent.size(); i++)
	{
		CContent* lpContent = lpBuddyMsg->m_arrContent[i];
		if(lpContent == NULL)
			continue;

		if (lpContent->m_nType == CONTENT_TYPE_TEXT)							//文本
		{
			//strContent += _T("\\\"");
			//strContent += UnicodeToHexStr(lpContent->m_strText.c_str(), TRUE);
			//strContent += _T("\\\",");

			strChatContent += _T("{\"msgText\":\"");
			strEscape = lpContent->m_strText.data();
			//对双引号和反斜杠进行转义
			strEscape.Replace(_T("\\"), _T("\\\\"));
			strEscape.Replace(_T("\""), _T("\\\""));
			strChatContent += strEscape;
			strChatContent += _T("\"},");

			strRecentMsg += lpContent->m_strText.data();
		}
		else if (lpContent->m_nType == CONTENT_TYPE_FONT_INFO)			//字体
		{
			strFont.Format(_T("\"font\":[\"%s\",%d,%d,%d,%d,%d],"),
							lpContent->m_FontInfo.m_strName.data(),
							lpContent->m_FontInfo.m_nSize,
							lpContent->m_FontInfo.m_clrText,
							lpContent->m_FontInfo.m_bBold,
							lpContent->m_FontInfo.m_bItalic,
							lpContent->m_FontInfo.m_bUnderLine);
		}
		else if (lpContent->m_nType == CONTENT_TYPE_FACE)				//表情
		{
			strChatContent += _T("{\"faceID\":");
			memset(cBuf, 0, sizeof(cBuf));
			wsprintf(cBuf, _T("%d"), lpContent->m_nFaceId);
			strChatContent += cBuf;
			strChatContent += _T("},");

			if(strRecentMsg.GetLength()<MAX_RECENT_MSG_LENGTH && strRecentMsg.GetLength()+4<=MAX_RECENT_MSG_LENGTH)
				strRecentMsg += _T("[表情]");
		}
		else if (lpContent->m_nType == CONTENT_TYPE_SHAKE_WINDOW)			//窗口抖动
		{
			strChatContent += _T("{\"shake\":1},");
			if(strRecentMsg.GetLength()<MAX_RECENT_MSG_LENGTH && strRecentMsg.GetLength()+6<=MAX_RECENT_MSG_LENGTH)
				strRecentMsg += _T("[窗口抖动]");
		}
		else if (lpContent->m_nType == CONTENT_TYPE_CHAT_IMAGE)			//图片
		{
			GetImageWidthAndHeight(lpContent->m_CFaceInfo.m_strFilePath.c_str(), nImageWidth, nImageHeight);
			memset(cBuf, 0, sizeof(cBuf));
			wsprintf(cBuf, _T("{\"pic\":[\"%s\",\"%s\",%u,%d,%d]},"), 
					lpContent->m_CFaceInfo.m_strFileName.c_str(),
					lpContent->m_CFaceInfo.m_strFileName.c_str(),
					lpContent->m_CFaceInfo.m_dwFileSize,
					nImageWidth,
					nImageHeight);
			strChatContent += cBuf;
			++nImageCount;
			aryImageInfo.push_back(cBuf);

			if(strRecentMsg.GetLength()<MAX_RECENT_MSG_LENGTH && strRecentMsg.GetLength()+4<=MAX_RECENT_MSG_LENGTH)
				strRecentMsg += _T("[图片]");
		}
		else if (lpContent->m_nType == CONTENT_TYPE_FILE)			//文件
		{
			memset(cBuf, 0, sizeof(cBuf));
			wsprintf(cBuf, _T("{\"file\":[\"%s\",\"%s\",%u,%d]},"), 
					lpContent->m_CFaceInfo.m_strFileName.c_str(),
					lpContent->m_CFaceInfo.m_strFilePath.c_str(),
					lpContent->m_CFaceInfo.m_dwFileSize,
					lpContent->m_CFaceInfo.m_bOnline);
			strChatContent += cBuf;

			if(strRecentMsg.GetLength()<MAX_RECENT_MSG_LENGTH && strRecentMsg.GetLength()+4<=MAX_RECENT_MSG_LENGTH)
				strRecentMsg += _T("[文件]");

		}
		
	}

	strRecentMsg = strRecentMsg.Left(MAX_RECENT_MSG_LENGTH);

	strChatContent.TrimRight(_T(","));

	CString strContent;
	strContent.Format(_T("{\"msgType\":1,\"time\":%llu,\"clientType\":1,"), lpBuddyMsg->m_nTime);
	//strContent.Format(_T("{\"time\":%llu,\"clientType\":1,"), lpBuddyMsg->m_nTime);
	
	//加上字体信息
	strContent += strFont;

	//加上聊天正文内容
	strContent += _T("\"content\":[");
	strContent += strChatContent;

	strContent += _T("]}");

	//发送前先转换成utf8格式
	//utf8存储中文时，对应2～4个字节
	long nLength = strContent.GetLength()*4;
	CStringA strUtf8Msg;
	EncodeUtil::UnicodeToUtf8(strContent, strUtf8Msg.GetBuffer(nLength), nLength);
	strUtf8Msg.ReleaseBuffer();

	size_t nSize = pMsgItem->m_arrTargetIDs.size();
	CMiniBuffer miniBuffer(nSize* sizeof(UINT));
	UINT* pAccountIDs = (UINT*)miniBuffer.GetBuffer();
	for(size_t i=0; i<nSize; ++i)
	{
		pAccountIDs[i] = pMsgItem->m_arrTargetIDs[i];
	}

	BOOL bRet = FALSE;
	long nRetry = 3;
	while(nRetry > 0)
	{
		bRet = SendMultiChatMessage(strUtf8Msg.GetString(), strUtf8Msg.GetLength(), pAccountIDs, (long)nSize);

		if(bRet)
			break;
		else
		{
			::Sleep(1000);
			--nRetry;
		}

	}
	
	
	//只有发送成功的消息才会被添加到最近聊天列表中
	if(bRet)
	{
		UINT uFaceID = m_lpUserMgr->GetFaceID(lpBuddyMsg->m_nToUin);
		tstring strNickName;
		if(IsGroupTarget(lpBuddyMsg->m_nToUin))
			strNickName = m_lpUserMgr->GetGroupName(lpBuddyMsg->m_nToUin);
		else
			strNickName = m_lpUserMgr->GetNickName(lpBuddyMsg->m_nToUin);
		
		AddItemToRecentSessionList(lpBuddyMsg->m_nToUin, uFaceID, strNickName.c_str(), strRecentMsg, lpBuddyMsg->m_nTime);
	}
	
	return bRet;
}

void CSendMsgThread::AddItemToRecentSessionList(UINT uUserID, UINT uFaceID, PCTSTR pszNickName, PCTSTR pszText, time_t nMsgTime)
{
	CRecentInfo* pRecentInfo = new CRecentInfo();
	pRecentInfo->m_nType = IsGroupTarget(uUserID) ? FMG_MSG_TYPE_GROUP : FMG_MSG_TYPE_BUDDY;
	pRecentInfo->m_uUserID = uUserID;
	pRecentInfo->m_uFaceID = uFaceID;
	pRecentInfo->m_MsgTime = nMsgTime;
	_tcscpy_s(pRecentInfo->m_szName, ARRAYSIZE(pRecentInfo->m_szName), pszNickName);
	_tcscpy_s(pRecentInfo->m_szLastMsgText, ARRAYSIZE(pRecentInfo->m_szLastMsgText), pszText);
	m_lpUserMgr->m_RecentList.AddRecent(pRecentInfo);

	::PostMessage(m_lpUserMgr->m_hProxyWnd, FMG_MSG_UPDATE_RECENT_LIST, 0, 0);
}

std::wstring CSendMsgThread::UnicodeToHexStr(const WCHAR* lpStr, BOOL bDblSlash)
{
	static const WCHAR* lpHexStr = _T("0123456789abcdef");
	std::wstring strRet = _T("");
	WCHAR* lpSlash;

	if (NULL == lpStr || NULL ==*lpStr)
		return strRet;

	lpSlash = (bDblSlash ? _T("\\\\u") : _T("\\u"));

	for (int i = 0; i < (int)wcslen(lpStr); i++)
	{
		if (my_isalnum((WCHAR)lpStr[i]))	// 检测指定字符是否是字母(A-Z，a-z)或数字(0-9)
		{
			strRet += lpStr[i];
		}
		else
		{
			CHAR* lpChar = (CHAR*)&lpStr[i];

			strRet += lpSlash;
			strRet += lpHexStr[((unsigned char)(*(lpChar+1)) >> 4) & 0x0f];
			strRet += lpHexStr[(unsigned char)(*(lpChar+1)) & 0x0f];
			strRet += lpHexStr[((unsigned char)(*lpChar) >> 4) & 0x0f];
			strRet += lpHexStr[(unsigned char)(*lpChar) & 0x0f];
		}
	}

	return strRet;
}