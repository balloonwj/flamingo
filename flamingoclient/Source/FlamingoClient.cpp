#include "stdafx.h"
#include "FlamingoClient.h"
#include "IniFile.h"
#include "BalloonAuth/BalloonLoginDlg.h"
#include "net/IUProtocolData.h"
#include "net/MessageProtocolClient.h"  // net::MessageNotify / net::SendMessageResponse（v2 单聊消息投递载体）
#include "net/ContactsProtocolClient.h" // net::ContactsResult（v2 通讯录投递载体）
#include "net/ConversationsProtocolClient.h" // net::ConversationsResult（v2 会话列表投递载体）
#include "net/GroupProtocolClient.h"        // net::CreateGroupResponse（v2 建群应答投递载体）
#include "net/GroupFileProtocolClient.h"    // net::GetGroupFilesResult / GroupFilesChangedNotify（v2 群文件库投递载体）
#include "net/ProfileProtocolClient.h"      // net::UpdateProfileResponse（v2 改头像应答投递载体）
#include "net/ImageTransfer.h"              // net::ImageSendProgress / ImageSendResult / ImageDownloadResult（v2 图片收发投递载体）
#include "net/FileTransfer.h"               // net::FileSendProgress / FileSendResult / FileDownload*（v2 文件收发投递载体）
#include "net/RemoteDesktopProtocolClient.h" // net::RDInviteResponse / RDInviteNotify / RDRelayInfo / RDStateNotify（v2 远程桌面投递载体）
#include "net/CallProtocolClient.h"          // net::CallInviteResponse / CallInviteNotify / CallSignalingInfo / CallStateNotify（v2 音视频通话投递载体）
#include "net/AnnounceProtocolClient.h"      // net::AnnouncementsResult / AnnouncementNotify / AnnounceMarkReadResult（v2 平台公告投递载体）
#include "net/FriendProtocolClient.h"        // net::SetFriendRemarkResult（v2 设置好友备注名应答投递载体）
#include "IULog.h"
#include "UpdateDlg.h"
#include "EncodeUtil.h"
#include "UserSessionData.h"
#include "CustomMsgDef.h"
#include "Startup.h"
#include "File2.h"
#include "Path.h"
#include "../net/Msg.h"
#include <sstream>

CFlamingoClient& CFlamingoClient::GetInstance()
{
    static CFlamingoClient client;
    return client;
}


CFlamingoClient::CFlamingoClient(void)
{
	m_ServerTime = 0;

	m_bNetworkAvailable = TRUE;

	m_hwndRegister = NULL;
	m_hwndModifyPassword = NULL;
	
	m_bBuddyIDsAvailable = FALSE;
	m_bBuddyListAvailable = FALSE;

	m_nGroupCount = 0;
	m_bGroupInfoAvailable = FALSE;
	m_bGroupMemberInfoAvailable = FALSE;

    m_SendMsgThread.m_lpUserMgr = &m_UserMgr;
    m_RecvMsgThread.m_lpUserMgr = &m_UserMgr;
    m_RecvMsgThread.m_pFMGClient = this;
    m_FileTask.m_lpFMGClient = this;
    m_ImageTask.m_lpFMGClient = this;
}

CFlamingoClient::~CFlamingoClient(void)
{

}

// 初始化客户端
bool CFlamingoClient::InitProxyWnd()
{
	BOOL bRet = CreateProxyWnd();	// 创建代理窗口
	if (!bRet)
		return false;

    CIUSocket::GetInstance().LoadConfig();

	return true;
}

bool CFlamingoClient::InitNetThreads()
{
    CIUSocket::GetInstance().SetRecvMsgThread(&m_RecvMsgThread);
    CIUSocket::GetInstance().Init();

    m_SendMsgThread.Start();
    m_RecvMsgThread.Start();

    m_SendMsgThread.m_lpFMGClient = this;

    m_FileTask.Start();
    m_ImageTask.Start();

    //CSendFileThread::GetInstance().AttachSocketClient(&m_SocketClient);
    //CSendFileThread::GetInstance().Start();

    return true;
}

// 反初始化客户端
void CFlamingoClient::Uninit()
{
	DestroyProxyWnd();				// 销毁代理窗口

    CIUSocket::GetInstance().Uninit();
    m_SendMsgThread.Stop();
    m_RecvMsgThread.Stop();
	m_FileTask.Stop();
    m_ImageTask.Stop();

    CIUSocket::GetInstance().Join();
    m_SendMsgThread.Join();
    m_RecvMsgThread.Join();
	m_FileTask.Join();
    m_ImageTask.Join();
}

void CFlamingoClient::SetServer(PCTSTR pszServer)
{
    CIUSocket::GetInstance().SetServer(pszServer);
}

void CFlamingoClient::SetFileServer(PCTSTR pszServer)
{
    CIUSocket::GetInstance().SetFileServer(pszServer);
}

void CFlamingoClient::SetImgServer(PCTSTR pszServer)
{
    CIUSocket::GetInstance().SetImgServer(pszServer);
}

void CFlamingoClient::SetPort(short port)
{
    CIUSocket::GetInstance().SetPort(port);
}

void CFlamingoClient::SetFilePort(short port)
{
    CIUSocket::GetInstance().SetFilePort(port);
}

void CFlamingoClient::SetImgPort(short port)
{
    CIUSocket::GetInstance().SetImgPort(port);
}

// 设置UTalk号码和密码
void CFlamingoClient::SetUser(LPCTSTR lpUserAccount, LPCTSTR lpUserPwd)
{
	if (NULL == lpUserAccount || NULL == lpUserPwd || m_UserMgr.m_UserInfo.m_nStatus != STATUS_OFFLINE)
		return;

	m_UserMgr.m_UserInfo.m_strAccount = lpUserAccount;
	m_UserMgr.m_UserInfo.m_strPassword = lpUserPwd;
	//m_UserMgr.m_nUTalkUin = _tcstoul(lpUTalkNum, NULL, 10);
	//m_UserMgr.m_UserInfo.m_nUTalkUin = m_UserMgr.m_nUTalkUin;
	//m_UserMgr.m_UserInfo.m_nUTalkNum = m_UserMgr.m_nUTalkUin;
	//m_UserMgr.m_UserInfo.m_bHasUTalkNum = TRUE;
}

// 设置登录状态
void CFlamingoClient::SetLoginStatus(long nStatus)
{
	m_UserMgr.m_UserInfo.m_nStatus = nStatus;
}

// 设置回调窗口句柄
void CFlamingoClient::SetCallBackWnd(HWND hCallBackWnd)
{
	m_UserMgr.m_hCallBackWnd = hCallBackWnd;
}

void CFlamingoClient::SetRegisterWindow(HWND hwndRegister)
{
	m_hwndRegister = hwndRegister;
}

void CFlamingoClient::SetModifyPasswordWindow(HWND hwndModifyPassword)
{
	m_hwndModifyPassword = hwndModifyPassword;
}

void CFlamingoClient::SetCreateNewGroupWindow(HWND hwndCreateNewGroup)
{
	m_hwndCreateNewGroup = hwndCreateNewGroup;
}

void CFlamingoClient::Register(PCTSTR pszAccountName, PCTSTR pszNickName, PCTSTR pszPassword)
{
	//TODO: 如果此时断网，则直接返回
	CRegisterRequest* pRequest = new CRegisterRequest();
	char szData[64] = {0};
    EncodeUtil::UnicodeToUtf8(pszAccountName, szData, ARRAYSIZE(szData));
	strcpy_s(pRequest->m_szAccountName, ARRAYSIZE(pRequest->m_szAccountName), szData);

	memset(szData, 0, sizeof(szData));
    EncodeUtil::UnicodeToUtf8(pszNickName, szData, ARRAYSIZE(szData));
	strcpy_s(pRequest->m_szNickName, ARRAYSIZE(pRequest->m_szNickName), szData);

	memset(szData, 0, sizeof(szData));
    EncodeUtil::UnicodeToUtf8(pszPassword, szData, ARRAYSIZE(szData));
	strcpy_s(pRequest->m_szPassword, ARRAYSIZE(pRequest->m_szPassword), szData);

	m_SendMsgThread.AddItem(pRequest);
}

BOOL CFlamingoClient::UpdateLogonUserInfo(  PCTSTR pszNickName, 
										 PCTSTR pszSignature, 
										 UINT uGender, 
										 long nBirthday, 
										 PCTSTR pszAddress, 
										 PCTSTR pszPhone, 
										 PCTSTR pszMail, 
										 UINT uSysFaceID, 
										 PCTSTR pszCustomFacePath,
										 BOOL bUseCustomThumb)
{
	if(!m_bNetworkAvailable)
		return FALSE;
	
	CUpdateLogonUserInfoRequest* pRequest = new CUpdateLogonUserInfoRequest();
	char szData[512] = {0};
	if(pszNickName!=NULL &&*pszNickName!=NULL)
	{
        EncodeUtil::UnicodeToUtf8(pszNickName, szData, ARRAYSIZE(szData));
		strcpy_s(pRequest->m_szNickName, ARRAYSIZE(pRequest->m_szNickName), szData);
	}
	
	if(pszSignature!=NULL &&*pszSignature!=NULL)
	{
		memset(szData, 0, sizeof(szData));
        EncodeUtil::UnicodeToUtf8(pszSignature, szData, ARRAYSIZE(szData));
		strcpy_s(pRequest->m_szSignature, ARRAYSIZE(pRequest->m_szSignature), szData);
	}

	if(pszAddress!=NULL &&*pszAddress!=NULL)
	{
		memset(szData, 0, sizeof(szData));
        EncodeUtil::UnicodeToUtf8(pszAddress, szData, ARRAYSIZE(szData));
		strcpy_s(pRequest->m_szAddress, ARRAYSIZE(pRequest->m_szAddress), szData);
	}

	if(pszPhone!=NULL &&*pszPhone!=NULL)
	{
		memset(szData, 0, sizeof(szData));
        EncodeUtil::UnicodeToUtf8(pszPhone, szData, ARRAYSIZE(szData));
		strcpy_s(pRequest->m_szPhone, ARRAYSIZE(pRequest->m_szPhone), szData);
	}
	
	if(pszMail!=NULL &&*pszMail!=NULL)
	{
		memset(szData, 0, sizeof(szData));
        EncodeUtil::UnicodeToUtf8(pszMail, szData, ARRAYSIZE(szData));
		strcpy_s(pRequest->m_szMail, ARRAYSIZE(pRequest->m_szMail), szData);
	}

	pRequest->m_uGender = uGender;
	pRequest->m_nBirthday = nBirthday;
	pRequest->m_uFaceID = uSysFaceID;
	
	if(!bUseCustomThumb)
	{
		pRequest->m_bUseCustomThumb = FALSE;
		m_UserMgr.m_UserInfo.m_strCustomFace = _T("");
		m_UserMgr.m_UserInfo.m_nFace = uSysFaceID;
		m_UserMgr.m_UserInfo.m_bUseCustomFace = FALSE;
	}
	else
	{
		pRequest->m_bUseCustomThumb = TRUE;
		if(pszCustomFacePath!=NULL && &pszCustomFacePath!=NULL)
		{
			_tcscpy_s(pRequest->m_szCustomFace, ARRAYSIZE(pRequest->m_szCustomFace), pszCustomFacePath);
		}
		else
		{
			pRequest->m_bUseCustomThumb = FALSE;
			m_UserMgr.m_UserInfo.m_strCustomFace = _T("");
			m_UserMgr.m_UserInfo.m_bUseCustomFace = FALSE;
		}
	}

	m_SendMsgThread.AddItem(pRequest);

	return TRUE;
}

void CFlamingoClient::StartCheckNetworkStatusTask()
{
	//m_CheckNetworkStatusTask.m_pProtocol = &m_IUProtocol;
	m_CheckNetworkStatusTask.m_pTalkClient = this;
	//m_ThreadPool.AddTask(&m_CheckNetworkStatusTask);
}

void CFlamingoClient::StartHeartbeatTask()
{
	//m_HeartbeatTask.m_pTalkClient = this;
	//m_ThreadPool.AddTask(&m_HeartbeatTask);
}


void CFlamingoClient::SendHeartbeatMessage()
{
	if(!m_bNetworkAvailable)
		return;
	
	CHeartbeatMessageRequest* pRequest = new CHeartbeatMessageRequest();
	//m_SendMsgTask.AddItem(pRequest);
}

void CFlamingoClient::ModifyPassword(PCTSTR pszOldPassword, PCTSTR pszNewPassword)
{
	if(!m_bNetworkAvailable)
		return;
	
	char szData[64] = {0};
	CModifyPasswordRequest* pRequest = new CModifyPasswordRequest();
    EncodeUtil::UnicodeToUtf8(pszOldPassword, szData, ARRAYSIZE(szData));
	strcpy_s(pRequest->m_szOldPassword, ARRAYSIZE(pRequest->m_szOldPassword), szData);
	memset(szData, 0, sizeof(szData));
    EncodeUtil::UnicodeToUtf8(pszNewPassword, szData, ARRAYSIZE(szData));
	strcpy_s(pRequest->m_szNewPassword, ARRAYSIZE(pRequest->m_szNewPassword), szData);
	m_SendMsgThread.AddItem(pRequest);
}

void CFlamingoClient::CreateNewGroup(PCTSTR pszGroupName)
{
	if(!m_bNetworkAvailable)
		return;
	
	char szData[64] = {0};
	CCreateNewGroupRequest* pRequest = new CCreateNewGroupRequest();
    EncodeUtil::UnicodeToUtf8(pszGroupName, szData, ARRAYSIZE(szData));
	strcpy_s(pRequest->m_szGroupName, ARRAYSIZE(pRequest->m_szGroupName), szData);
    m_SendMsgThread.AddItem(pRequest);
}

// 登录
void CFlamingoClient::Login(int nStatus/* = STATUS_ONLINE*/)
{
	if (!IsOffline() || m_UserMgr.m_UserInfo.m_strAccount.empty() || m_UserMgr.m_UserInfo.m_strPassword.empty())
		return;

	CLoginRequest* pLoginRequest = new CLoginRequest();
	if(pLoginRequest == NULL)
		return;
	
	char szData[64] = {0};
    EncodeUtil::UnicodeToUtf8(m_UserMgr.m_UserInfo.m_strAccount.c_str(), szData, ARRAYSIZE(szData));
	strcpy_s(pLoginRequest->m_szAccountName, ARRAYSIZE(pLoginRequest->m_szAccountName), szData);

	memset(szData, 0, sizeof(szData));
    EncodeUtil::UnicodeToUtf8(m_UserMgr.m_UserInfo.m_strPassword.c_str(), szData, ARRAYSIZE(szData));
	strcpy_s(pLoginRequest->m_szPassword, ARRAYSIZE(pLoginRequest->m_szPassword), szData);

    //登录使用的在线状态取"期望在线状态"（v2 presence_type，见 net/Protocol.h），
    //而非写死值。该字段由 CIUSocket 维护：初次登录写入登录 presence、用户在状态菜单里
    //改状态时写入新值，故断线重连 / 主动重新上线会沿用上次选择的状态。
    //注意此处承载的是 v2 presence（0 离线/1 在线/2 离开/3 勿扰），不是 v1 ONLINE_STATUS——
    //两套枚举的 away/invisible 值是反的；HandleLogon 会把它原样传给 CIUSocket::Login 的
    //nOnlineStatus（即 v2 presence）。旧实现写死 STATUS_AWAY(v1=4)，等于把自己登成 v2 隐身(4)。
    //入参 nStatus 是历史遗留的 v1 形参，唯一调用方（重连定时器）按默认值传入，不再使用。
    (void)nStatus;
    pLoginRequest->m_nStatus = CIUSocket::GetInstance().GetRecordOnlineStatus();
	
	if(IsMobileNumber(m_UserMgr.m_UserInfo.m_strAccount.c_str()))
		pLoginRequest->m_nLoginType = LOGIN_USE_MOBILE_NUMBER;
	else
		pLoginRequest->m_nLoginType = LOGIN_USE_ACCOUNT;
	

	m_SendMsgThread.AddItem(pLoginRequest);
}

void CFlamingoClient::GetGroupMembers(int32_t groupid)
{
    CGroupBasicInfoRequest* pGroupInfoRequest = new CGroupBasicInfoRequest();
    pGroupInfoRequest->m_groupid = groupid;
    m_SendMsgThread.AddItem(pGroupInfoRequest);
}

void CFlamingoClient::ChangeStatus(int32_t nNewStatus)
{
    CChangeUserStatusRequest* pChangeStatusRequest = new CChangeUserStatusRequest();
    pChangeStatusRequest->m_nNewStatus = nNewStatus;

    m_SendMsgThread.AddItem(pChangeStatusRequest);
}

// 注销
BOOL CFlamingoClient::Logout()
{
	if (IsOffline())
		return FALSE;

	return FALSE;
}

// 取消登录
void CFlamingoClient::CancelLogin()
{
	//m_ThreadPool.RemoveAllTask();
}

// 改变在线状态
void CFlamingoClient::ChangeStatus(long nStatus)
{
	if (IsOffline())
		return;
}

// 更新好友列表
void CFlamingoClient::UpdateBuddyList()
{
	if (IsOffline())
		return;
}

// 更新群列表
void CFlamingoClient::UpdateGroupList()
{
	if (IsOffline())
		return;
}

// 更新最近联系人列表
void CFlamingoClient::UpdateRecentList()
{
	if (IsOffline())
		return;
}

// 更新好友信息
void CFlamingoClient::UpdateBuddyInfo(UINT nUTalkUin)
{	
	if (IsOffline())
		return;
}

// 更新群成员信息
void CFlamingoClient::UpdateGroupMemberInfo(UINT nGroupCode, UINT nUTalkUin)
{
	if (IsOffline())
		return;
}

// 更新群信息
void CFlamingoClient::UpdateGroupInfo(UINT nGroupCode)
{
	if (IsOffline())
		return;

}

// 更新好友号码
void CFlamingoClient::UpdateBuddyNum(UINT nUTalkUin)
{
	//if (IsOffline())
	//	return;

	//CUTalkNumTask* lpUTalkNumTask = new CUTalkNumTask;
	//if (lpUTalkNumTask != NULL)
	//{
	//	lpUTalkNumTask->m_lpUserMgr = &m_UserMgr;
	//	lpUTalkNumTask->m_lpUTalkProtocol = &m_UTalkProtocol;
	//	lpUTalkNumTask->AddParam(OP_TYPE_GETBUDDYNUM, 0, nUTalkUin);
	//	m_ThreadPool.AddTask(lpUTalkNumTask);
	//}
}

// 更新群成员号码
void CFlamingoClient::UpdateGroupMemberNum(UINT nGroupCode, UINT nUTalkUin)
{
	//if (IsOffline())
	//	return;

	//CUTalkNumTask* lpUTalkNumTask = new CUTalkNumTask;
	//if (lpUTalkNumTask != NULL)
	//{
	//	lpUTalkNumTask->m_lpUserMgr = &m_UserMgr;
	//	lpUTalkNumTask->m_lpUTalkProtocol = &m_UTalkProtocol;
	//	lpUTalkNumTask->AddParam(OP_TYPE_GETGMEMBERNUM, nGroupCode, nUTalkUin);
	//	m_ThreadPool.AddTask(lpUTalkNumTask);
	//}
}

// 更新群成员号码
void CFlamingoClient::UpdateGroupMemberNum(UINT nGroupCode, std::vector<UINT>* arrUTalkUin)
{
	//if (UTalk_STATUS_OFFLINE == m_UserMgr.m_nStatus
	//	|| NULL == arrUTalkUin || (int)arrUTalkUin->size() <= 0)
	//	return;

	//CUTalkNumTask* lpUTalkNumTask = new CUTalkNumTask;
	//if (lpUTalkNumTask != NULL)
	//{
	//	lpUTalkNumTask->m_lpUserMgr = &m_UserMgr;
	//	lpUTalkNumTask->m_lpUTalkProtocol = &m_UTalkProtocol;
	//	for (int i = 0; i < (int)arrUTalkUin->size(); i++)
	//	{
	//		lpUTalkNumTask->AddParam(OP_TYPE_GETGMEMBERNUM, nGroupCode, arrUTalkUin->at(i));
	//	}
	//	m_ThreadPool.AddTask(lpUTalkNumTask);
	//}
}

// 更新群号码
void CFlamingoClient::UpdateGroupNum(UINT nGroupCode)
{
	//if (IsOffline())
	//	return;

	//CUTalkNumTask* lpUTalkNumTask = new CUTalkNumTask;
	//if (lpUTalkNumTask != NULL)
	//{
	//	lpUTalkNumTask->m_lpUserMgr = &m_UserMgr;
	//	lpUTalkNumTask->m_lpUTalkProtocol = &m_UTalkProtocol;
	//	lpUTalkNumTask->AddParam(OP_TYPE_GETGROUPNUM, nGroupCode, 0);
	//	m_ThreadPool.AddTask(lpUTalkNumTask);
	//}
}

// 更新好友个性签名
void CFlamingoClient::UpdateBuddySign(UINT nUTalkUin)
{
	//if (IsOffline())
	//	return;

	//CUTalkSignTask* lpUTalkSignTask = new CUTalkSignTask;
	//if (NULL == lpUTalkSignTask)
	//	return;

	//lpUTalkSignTask->m_lpUserMgr = &m_UserMgr;
	//lpUTalkSignTask->m_lpUTalkProtocol = &m_UTalkProtocol;
	//lpUTalkSignTask->m_nType = UTalkSIGN_OP_TYPE_GET;
	//lpUTalkSignTask->AddGetParam(FALSE, 0, nUTalkUin);
	//m_ThreadPool.AddTask(lpUTalkSignTask);
}

// 更新群成员个性签名
void CFlamingoClient::UpdateGroupMemberSign(UINT nGroupCode, UINT nUTalkUin)
{
	//if (IsOffline())
	//	return;

	//CUTalkSignTask* lpUTalkSignTask = new CUTalkSignTask;
	//if (NULL == lpUTalkSignTask)
	//	return;

	//lpUTalkSignTask->m_lpUserMgr = &m_UserMgr;
	//lpUTalkSignTask->m_lpUTalkProtocol = &m_UTalkProtocol;
	//lpUTalkSignTask->m_nType = UTalkSIGN_OP_TYPE_GET;
	//lpUTalkSignTask->AddGetParam(TRUE, nGroupCode, nUTalkUin);
	//m_ThreadPool.AddTask(lpUTalkSignTask);
}

// 修改UTalk个性签名
void CFlamingoClient::ModifyUTalkSign(LPCTSTR lpSign)
{
	const CBuddyInfo& currentLogonUser = m_UserMgr.m_UserInfo;

	UpdateLogonUserInfo(currentLogonUser.m_strNickName.c_str(), 
						lpSign, 
						currentLogonUser.m_nGender,
						currentLogonUser.m_nBirthday,
						currentLogonUser.m_strAddress.c_str(),
						currentLogonUser.m_strMobile.c_str(),
						currentLogonUser.m_strEmail.c_str(),
						currentLogonUser.m_nFace,
						currentLogonUser.m_strRawCustomFace.c_str(),
						currentLogonUser.m_bUseCustomFace);
}

// 更新好友头像
void CFlamingoClient::UpdateBuddyHeadPic(UINT nUTalkUin, UINT nUTalkNum)
{
	//if (IsOffline())
	//	return;

	//CGetHeadPicTask* lpGetHeadPicTask = new CGetHeadPicTask;
	//if (NULL == lpGetHeadPicTask)
	//	return;

	//lpGetHeadPicTask->m_lpUserMgr = &m_UserMgr;
	//lpGetHeadPicTask->m_lpUTalkProtocol = &m_UTalkProtocol;
	//lpGetHeadPicTask->AddParam(GHPT_OP_TYPE_BUDDY_PIC, 0, 0, nUTalkUin, nUTalkNum);
	//m_ThreadPool.AddTask(lpGetHeadPicTask);
}

// 更新群成员头像
void CFlamingoClient::UpdateGroupMemberHeadPic(UINT nGroupCode, UINT nUTalkUin, UINT nUTalkNum)
{
	//if (IsOffline())
	//	return;

	//CGetHeadPicTask* lpGetHeadPicTask = new CGetHeadPicTask;
	//if (NULL == lpGetHeadPicTask)
	//	return;

	//lpGetHeadPicTask->m_lpUserMgr = &m_UserMgr;
	//lpGetHeadPicTask->m_lpUTalkProtocol = &m_UTalkProtocol;
	//lpGetHeadPicTask->AddParam(GHPT_OP_TYPE_SESS_PIC, nGroupCode, 0, nUTalkUin, nUTalkNum);
	//m_ThreadPool.AddTask(lpGetHeadPicTask);
}

// 更新群头像
void CFlamingoClient::UpdateGroupHeadPic(UINT nGroupCode, UINT nGroupNum)
{
	//if (IsOffline())
	//	return;

	//CGetHeadPicTask* lpGetHeadPicTask = new CGetHeadPicTask;
	//if (NULL == lpGetHeadPicTask)
	//	return;

	//lpGetHeadPicTask->m_lpUserMgr = &m_UserMgr;
	//lpGetHeadPicTask->m_lpUTalkProtocol = &m_UTalkProtocol;
	//lpGetHeadPicTask->AddParam(GHPT_OP_TYPE_GROUP_PIC, nGroupCode, nGroupNum, 0,  0);
	//m_ThreadPool.AddTask(lpGetHeadPicTask);
}

// 更新群表情信令
void CFlamingoClient::UpdateGroupFaceSignal()
{
	if (IsOffline())
		return;
}

// 发送好友消息
BOOL CFlamingoClient::SendBuddyMsg(UINT nFromUin, const tstring& strFromNickName, UINT nToUin, const tstring& strToNickName, time_t nTime, const tstring& strChatMsg, HWND hwndFrom/* = NULL*/)
{
    return m_SendMsgThread.AddBuddyMsg(nFromUin, strFromNickName, nToUin, strToNickName, nTime, strChatMsg, hwndFrom);
}

// 发送群消息
BOOL CFlamingoClient::SendGroupMsg(UINT nGroupId, time_t nTime, LPCTSTR lpMsg, HWND hwndFrom)
{
	if (IsOffline())
		return FALSE;

    //TODO: 原来的
    //return m_SendMsgThread.AddBuddyMsg(nGroupId, nTime, lpMsg, hwndFrom);
    //return m_SendMsgThread.AddGroupMsg(nGroupId, nTime, lpMsg, hwndFrom);
    return m_SendMsgThread.AddBuddyMsg(m_UserMgr.m_UserInfo.m_uUserID, m_UserMgr.m_UserInfo.m_strAccount, nGroupId, _T(""), nTime, lpMsg, hwndFrom);
}

// 发送临时会话消息
BOOL CFlamingoClient::SendSessMsg(UINT nGroupId, UINT nToUin, time_t nTime, LPCTSTR lpMsg)
{
	if (IsOffline())
		return FALSE;

	//return m_SendMsgTask.AddSessMsg(nGroupId, nToUin, nTime, lpMsg);
    return FALSE;
}


BOOL CFlamingoClient::SendMultiChatMsg(const std::set<UINT> setAccountID, time_t nTime, LPCTSTR lpMsg, HWND hwndFrom/*=NULL*/)
{
	if (IsOffline())
		return FALSE;
	//群发消息
	return m_SendMsgThread.AddMultiMsg(setAccountID, nTime, lpMsg, hwndFrom);
}

// 是否离线状态
BOOL CFlamingoClient::IsOffline()
{
	return (STATUS_OFFLINE == m_UserMgr.m_UserInfo.m_nStatus) ? TRUE : FALSE;
}

// 获取在线状态
long CFlamingoClient::GetStatus()
{
	return m_UserMgr.m_UserInfo.m_nStatus;
}

// 获取验证码图片
BOOL CFlamingoClient::GetVerifyCodePic(const BYTE*& lpData, DWORD& dwSize)
{
	//lpData = (const BYTE*)m_UserMgr.m_VerifyCodePic.GetData();
	//dwSize = m_UserMgr.m_VerifyCodePic.GetSize();

	//if (lpData != NULL && dwSize > 0)
	//{
	//	return TRUE;
	//}
	//else
	//{
	//	lpData = NULL;
	//	dwSize = 0;
	//	return FALSE;
	//}
	return FALSE;
}

void CFlamingoClient::SetBuddyListAvailable(BOOL bAvailable)
{
	m_bBuddyListAvailable = bAvailable;
}
	
BOOL CFlamingoClient::IsBuddyListAvailable()
{
	return m_bBuddyListAvailable;
}

// 获取用户信息
CBuddyInfo* CFlamingoClient::GetUserInfo(UINT uAccountID/*=0*/)
{
	if(uAccountID==0 || uAccountID==m_UserMgr.m_UserInfo.m_uUserID)
		return &m_UserMgr.m_UserInfo;

	//企业版说明：v1 好友列表已删除，这里只剩"在群成员里找"这一条路径。
	CBuddyInfo* pBuddyInfo = NULL;
	CGroupInfo* pGroupInfo = NULL;
	for(size_t i=0; i<m_UserMgr.m_GroupList.m_arrGroupInfo.size(); ++i)
	{
		pGroupInfo = m_UserMgr.m_GroupList.m_arrGroupInfo[i];
		for(size_t j=0; j<pGroupInfo->m_arrMember.size(); ++j)
		{
			pBuddyInfo = pGroupInfo->m_arrMember[j];
			if(uAccountID == pBuddyInfo->m_uUserID)
				return pBuddyInfo;
		}
	}

	return NULL;
}

// 获取群列表
CGroupList* CFlamingoClient::GetGroupList()
{
	return &m_UserMgr.m_GroupList;
}

// 获取最近联系人列表
CRecentList* CFlamingoClient::GetRecentList()
{
	return &m_UserMgr.m_RecentList;
}

// 获取消息列表
CMessageList* CFlamingoClient::GetMessageList()
{
	return &m_UserMgr.m_MsgList;
}

// 获取消息记录管理器
CMessageLogger* CFlamingoClient::GetMsgLogger()
{
	return &m_UserMgr.m_MsgLogger;
}

// 获取用户文件夹存放路径
tstring CFlamingoClient::GetUserFolder()
{
	return m_UserMgr.GetUserFolder();
}

// 获取个人文件夹存放路径
tstring CFlamingoClient::GetPersonalFolder(UINT nUserNum/* = 0*/)
{
	return m_UserMgr.GetPersonalFolder();
}

// 获取聊天图片存放路径
tstring CFlamingoClient::GetChatPicFolder(UINT nUserNum/* = 0*/)
{
	return m_UserMgr.GetChatPicFolder();
}

// 获取用户头像图片全路径文件名
tstring CFlamingoClient::GetUserHeadPicFullName(UINT nUserNum/* = 0*/)
{
	return m_UserMgr.GetUserHeadPicFullName(nUserNum);
}

// 获取好友头像图片全路径文件名
tstring CFlamingoClient::GetBuddyHeadPicFullName(UINT nUTalkNum)
{
	return m_UserMgr.GetBuddyHeadPicFullName(nUTalkNum);
}

// 获取群头像图片全路径文件名
tstring CFlamingoClient::GetGroupHeadPicFullName(UINT nGroupNum)
{
	return m_UserMgr.GetGroupHeadPicFullName(nGroupNum);
}

// 获取群成员头像图片全路径文件名
tstring CFlamingoClient::GetSessHeadPicFullName(UINT nUTalkNum)
{
	return m_UserMgr.GetSessHeadPicFullName(nUTalkNum);
}

// 获取聊天图片全路径文件名
tstring CFlamingoClient::GetChatPicFullName(LPCTSTR lpszFileName)
{
	return m_UserMgr.GetChatPicFullName(lpszFileName);
}

// 获取消息记录全路径文件名
tstring CFlamingoClient::GetMsgLogFullName(UINT nUserNum/* = 0*/)
{
	return m_UserMgr.GetMsgLogFullName();
}

// 判断是否需要更新好友头像
BOOL CFlamingoClient::IsNeedUpdateBuddyHeadPic(UINT nUTalkNum)
{
	return m_UserMgr.IsNeedUpdateBuddyHeadPic(nUTalkNum);
}

// 判断是否需要更新群头像
BOOL CFlamingoClient::IsNeedUpdateGroupHeadPic(UINT nGroupNum)
{
	return m_UserMgr.IsNeedUpdateGroupHeadPic(nGroupNum);
}

// 判断是否需要更新群成员头像
BOOL CFlamingoClient::IsNeedUpdateSessHeadPic(UINT nUTalkNum)
{
	return m_UserMgr.IsNeedUpdateSessHeadPic(nUTalkNum);
}

// 获取服务器时间
void CFlamingoClient::RequestServerTime()
{
	//m_IUProtocol.RequestServerTime();
}

time_t CFlamingoClient::GetCurrentTime()
{
	//return m_IUProtocol.GetServerTime();

    return 0;
}

void CFlamingoClient::OnNetworkStatusChange(UINT message, WPARAM wParam, LPARAM lParam)
{
	BOOL bAvailable = (wParam>0 ? TRUE : FALSE);
	
	//新状态和当前的一致，保持不变
	if(m_bNetworkAvailable == bAvailable)
		return;

	m_bNetworkAvailable = bAvailable;

	if(m_bNetworkAvailable)
	{
		//本机网络恢复了。要不要趁机连回服务器，<u>不由这里决定</u>：用户主动选了离线、
		//或者账号在别处登录被顶下线之后，语义都是"别再连了"，此刻自己连回去就绕开了
		//那个决定（被踢的那一路还会把刚在别处登上去的那一端又顶下来，两端来回顶号）。
		//故把决定权交给 CMainDlg —— 登录态在它手里，它用与掉线事件、重连定时器同一个
		//守卫函数(balloonmain::ShouldAutoReconnect)判断，三处共用一个判定，不再各判各的。
		LOG_WARNING("Local connection is on service, ask main window whether to relogon.");
		if (m_UserMgr.m_hCallBackWnd != NULL)
			::PostMessage(m_UserMgr.m_hCallBackWnd, FMG_MSG_NETWORK_RESUMED, 0, 0);
	}
	else
	{
		LOG_WARNING("Local connection is out of service, make the user offline.");
		GoOffline();
	}
	
}

void CFlamingoClient::OnHeartbeatResult(UINT message, WPARAM wParam, LPARAM lParam)
{
	if(wParam == HEARTBEAT_DEAD)
	{
		GoOffline();
	}
}

void CFlamingoClient::OnRegisterResult(UINT message, WPARAM wParam, LPARAM lParam)
{
    ::SendMessage(m_hwndRegister, message, wParam, lParam);	
}

void CFlamingoClient::OnLoginResult(UINT message, WPARAM wParam, LPARAM lParam)
{
	CLoginResult* pLoginResult = (CLoginResult*)lParam;
	if(pLoginResult == NULL)
		return;

	long nLoginResultCode = pLoginResult->m_LoginResultCode;
	UINT uAccountID = pLoginResult->m_uAccountID;

	LOG_INFO("FlamingoClient::OnLoginResult: code=%d (0=SUCCESS 1=FAILED 2=UNREG "
	         "3=PWD_ERR), userid=%u, account=%s.",
	         (int)nLoginResultCode, (unsigned int)uAccountID,
	         pLoginResult->m_szAccountName);
	
	if(uAccountID > 0)
		m_UserMgr.m_UserInfo.m_uUserID = uAccountID;
	if(nLoginResultCode == LOGIN_SUCCESS)
	{
		m_UserMgr.m_UserInfo.m_nStatus = STATUS_ONLINE;
		TCHAR szAccountName[64] = {0};
        EncodeUtil::Utf8ToUnicode(pLoginResult->m_szAccountName, szAccountName, ARRAYSIZE(szAccountName));
		m_UserMgr.m_UserInfo.m_strAccount = szAccountName;
        TCHAR szNickName[64] = { 0 };
        EncodeUtil::Utf8ToUnicode(pLoginResult->m_szNickName, szNickName, ARRAYSIZE(szNickName));
        m_UserMgr.m_UserInfo.m_strNickName = szNickName;
        m_UserMgr.m_UserInfo.m_nFace = pLoginResult->m_nFace;
        m_UserMgr.m_UserInfo.m_nGender = pLoginResult->m_nGender;
        m_UserMgr.m_UserInfo.m_nBirthday = pLoginResult->m_nBirthday;

        TCHAR szSignature[512] = { 0 };
        EncodeUtil::Utf8ToUnicode(pLoginResult->m_szSignature, szSignature, ARRAYSIZE(szSignature));
        m_UserMgr.m_UserInfo.m_strSign = szSignature;

        TCHAR szAddress[512] = { 0 };
        EncodeUtil::Utf8ToUnicode(pLoginResult->m_szAddress, szAddress, ARRAYSIZE(szAddress));
        m_UserMgr.m_UserInfo.m_strAddress = szAddress;

        TCHAR szPhoneNumber[64] = { 0 };
        EncodeUtil::Utf8ToUnicode(pLoginResult->m_szPhoneNumber, szPhoneNumber, ARRAYSIZE(szPhoneNumber));
        m_UserMgr.m_UserInfo.m_strMobile = szPhoneNumber;

        TCHAR szMail[512] = { 0 };
        EncodeUtil::Utf8ToUnicode(pLoginResult->m_szMail, szMail, ARRAYSIZE(szMail));
        m_UserMgr.m_UserInfo.m_strEmail = szMail;

        m_UserMgr.m_UserInfo.m_strCustomFaceName = pLoginResult->m_szCustomFace;
        if (!m_UserMgr.m_UserInfo.m_strCustomFaceName.IsEmpty())
        {
			m_UserMgr.m_UserInfo.m_bUseCustomFace = TRUE;
			CString cachedUserThumb;
            cachedUserThumb.Format(_T("%s%d.png"), m_UserMgr.GetCustomUserThumbFolder().c_str(), uAccountID);
            if (Hootina::CPath::IsFileExist(cachedUserThumb))
            {
                TCHAR szCachedThumbMd5[64] = { 0 };
                GetFileMd5ValueW(cachedUserThumb, szCachedThumbMd5, ARRAYSIZE(szCachedThumbMd5));

                TCHAR szThumbMd5Unicode[64] = { 0 };
                EncodeUtil::Utf8ToUnicode(pLoginResult->m_szCustomFace, szThumbMd5Unicode, ARRAYSIZE(szThumbMd5Unicode));
                if (_tcsncmp(szCachedThumbMd5, szThumbMd5Unicode, 32) == 0)
                {
                    m_UserMgr.m_UserInfo.m_bUseCustomFace = TRUE;
                    m_UserMgr.m_UserInfo.m_bCustomFaceAvailable = TRUE;
                }
                //头像不存在，下载该头像
                else
                {					
					CFileItemRequest* pFileItem = new CFileItemRequest();
					pFileItem->m_nFileType = FILE_ITEM_DOWNLOAD_USER_THUMB;
					pFileItem->m_uAccountID = uAccountID;
					strcpy_s(pFileItem->m_szUtfFilePath, ARRAYSIZE(pFileItem->m_szUtfFilePath), pLoginResult->m_szCustomFace);
					m_FileTask.AddItem(pFileItem);
                }

            }
			else
			{
				//下载头像
				CFileItemRequest* pFileItem = new CFileItemRequest();
				pFileItem->m_nFileType = FILE_ITEM_DOWNLOAD_USER_THUMB;
				pFileItem->m_uAccountID = uAccountID;
				strcpy_s(pFileItem->m_szUtfFilePath, ARRAYSIZE(pFileItem->m_szUtfFilePath), pLoginResult->m_szCustomFace);
				m_FileTask.AddItem(pFileItem);
			}
                
        }

	}
	
    ::SendMessage(m_UserMgr.m_hCallBackWnd, FMG_MSG_LOGIN_RESULT, 0, nLoginResultCode);
    ::SendMessage(m_UserMgr.m_hCallBackWnd, FMG_MSG_UPDATE_BUDDY_INFO, 0, (LPARAM)m_UserMgr.m_UserInfo.m_uUserID);

	delete pLoginResult;
}

void CFlamingoClient::OnUpdateGroupBasicInfo(UINT message, WPARAM wParam, LPARAM lParam)
{
    CGroupBasicInfoResult* pResult = (CGroupBasicInfoResult*)lParam;
    if (pResult == NULL)
        return;

    CBuddyInfo* pBuddyInfo = NULL;
    TCHAR szAccountName[32] = { 0 };
    TCHAR szNickName[32] = { 0 };
    TCHAR szSignature[256] = { 0 };
    TCHAR szPhoneNumber[32] = { 0 };
    TCHAR szMail[32] = { 0 };

    //TCHAR szGroupAccount[32];

    CGroupInfo* pGroupInfo = m_UserMgr.m_GroupList.GetGroupById(pResult->m_groupid);
    
    for (auto& iter : pResult->m_listUserBasicInfo)
    {
        EncodeUtil::Utf8ToUnicode(iter->szAccountName, szAccountName, ARRAYSIZE(szAccountName));
        EncodeUtil::Utf8ToUnicode(iter->szNickName, szNickName, ARRAYSIZE(szNickName));
        EncodeUtil::Utf8ToUnicode(iter->szSignature, szSignature, ARRAYSIZE(szSignature));
        EncodeUtil::Utf8ToUnicode(iter->szPhoneNumber, szPhoneNumber, ARRAYSIZE(szPhoneNumber));
        EncodeUtil::Utf8ToUnicode(iter->szMail, szMail, ARRAYSIZE(szMail));


        pBuddyInfo = new CBuddyInfo();
        pBuddyInfo->m_uUserID = iter->uAccountID;

        pBuddyInfo->m_strAccount = szAccountName;

        pBuddyInfo->m_strNickName = szNickName;

        pBuddyInfo->m_strSign = szSignature;

        pBuddyInfo->m_strMobile = szPhoneNumber;

        pBuddyInfo->m_strEmail = szMail;
        pBuddyInfo->m_nStatus = iter->nStatus;
        pBuddyInfo->m_nClientType = iter->clientType;
        pBuddyInfo->m_nFace = iter->uFaceID;
        pBuddyInfo->m_nBirthday = iter->nBirthday;
        pBuddyInfo->m_nGender = iter->nGender;

        pBuddyInfo->m_strCustomFaceName = iter->customFace;
        if (!pBuddyInfo->m_strCustomFaceName.IsEmpty())
        {
            pBuddyInfo->m_bUseCustomFace = TRUE;
            CString cachedUserThumb;
            cachedUserThumb.Format(_T("%s%d.png"), m_UserMgr.GetCustomUserThumbFolder().c_str(), iter->uAccountID);
            if (Hootina::CPath::IsFileExist(cachedUserThumb))
            {
                TCHAR szCachedThumbMd5[64] = { 0 };
                GetFileMd5ValueW(cachedUserThumb, szCachedThumbMd5, ARRAYSIZE(szCachedThumbMd5));

                TCHAR szThumbMd5Unicode[64] = { 0 };
                EncodeUtil::Utf8ToUnicode(iter->customFace, szThumbMd5Unicode, ARRAYSIZE(szThumbMd5Unicode));
                if (_tcsncmp(szCachedThumbMd5, szThumbMd5Unicode, 32) == 0)
                {
                    pBuddyInfo->m_bUseCustomFace = TRUE;
                    pBuddyInfo->m_bCustomFaceAvailable = TRUE;
                }
                //头像不存在，下载该头像
                else
                {
                    //下载头像
                    CFileItemRequest* pFileItem = new CFileItemRequest();
                    pFileItem->m_nFileType = FILE_ITEM_DOWNLOAD_USER_THUMB;
                    pFileItem->m_uAccountID = iter->uAccountID;
                    strcpy_s(pFileItem->m_szUtfFilePath, ARRAYSIZE(pFileItem->m_szUtfFilePath), iter->customFace);
                    m_ImageTask.AddItem(pFileItem);
                }
            }
            else
            {
                //下载头像
                CFileItemRequest* pFileItem = new CFileItemRequest();
                pFileItem->m_nFileType = FILE_ITEM_DOWNLOAD_USER_THUMB;
                pFileItem->m_uAccountID = iter->uAccountID;
                strcpy_s(pFileItem->m_szUtfFilePath, ARRAYSIZE(pFileItem->m_szUtfFilePath), iter->customFace);
                m_ImageTask.AddItem(pFileItem);
            }
        }

        pBuddyInfo->m_nTeamIndex = 0;

        if (pGroupInfo != NULL)
        {
            if (!pGroupInfo->IsMember(pBuddyInfo->m_uUserID))
                pGroupInfo->AddMember(pBuddyInfo);
        }

       
        DEL(iter);
    }

    ::SendMessage(m_UserMgr.m_hCallBackWnd, FMG_MSG_UPDATE_GROUP_INFO, pResult->m_groupid, 0);

    delete pResult;

    //::SendMessage(m_UserMgr.m_hCallBackWnd, FMG_MSG_UPDATE_BUDDY_INFO, 0, (LPARAM)m_UserMgr.m_UserInfo.m_uUserID);
    ::SendMessage(m_UserMgr.m_hCallBackWnd, FMG_MSG_UPDATE_RECENT_LIST, 0, 0);
    ::SendMessage(m_UserMgr.m_hCallBackWnd, FMG_MSG_UPDATE_GROUP_LIST, 0, 0);
}

void CFlamingoClient::OnModifyInfoResult(UINT message, WPARAM wParam, LPARAM lParam)
{
	CUpdateLogonUserInfoResult* pResult = (CUpdateLogonUserInfoResult*)lParam;
	if(pResult == NULL)
		delete pResult;

    TCHAR szNickName[64] = { 0 };
    EncodeUtil::Utf8ToUnicode(pResult->m_szNickName, szNickName, ARRAYSIZE(szNickName));
    m_UserMgr.m_UserInfo.m_strNickName = szNickName;
    m_UserMgr.m_UserInfo.m_nFace = pResult->m_uFaceID;
    m_UserMgr.m_UserInfo.m_nGender = pResult->m_uGender;
    m_UserMgr.m_UserInfo.m_nBirthday = pResult->m_nBirthday;

    TCHAR szSignature[512] = { 0 };
    EncodeUtil::Utf8ToUnicode(pResult->m_szSignature, szSignature, ARRAYSIZE(szSignature));
    m_UserMgr.m_UserInfo.m_strSign = szSignature;

    TCHAR szAddress[512] = { 0 };
    EncodeUtil::Utf8ToUnicode(pResult->m_szAddress, szAddress, ARRAYSIZE(szAddress));
    m_UserMgr.m_UserInfo.m_strAddress = szAddress;

    TCHAR szPhoneNumber[64] = { 0 };
    EncodeUtil::Utf8ToUnicode(pResult->m_szPhone, szPhoneNumber, ARRAYSIZE(szPhoneNumber));
    m_UserMgr.m_UserInfo.m_strMobile = szPhoneNumber;

    TCHAR szMail[512] = { 0 };
    EncodeUtil::Utf8ToUnicode(pResult->m_szMail, szMail, ARRAYSIZE(szMail));
    m_UserMgr.m_UserInfo.m_strEmail = szMail;

	m_UserMgr.m_UserInfo.m_strCustomFaceName = pResult->m_szCustomFace;
	if (!m_UserMgr.m_UserInfo.m_strCustomFaceName.IsEmpty())
	{
		m_UserMgr.m_UserInfo.m_bUseCustomFace = TRUE;
		CString cachedUserThumb;
		cachedUserThumb.Format(_T("%s%d.png"), m_UserMgr.GetCustomUserThumbFolder().c_str(), m_UserMgr.m_UserInfo.m_uUserID);
		if (Hootina::CPath::IsFileExist(cachedUserThumb))
		{
			TCHAR szCachedThumbMd5[64] = { 0 };
			GetFileMd5ValueW(cachedUserThumb, szCachedThumbMd5, ARRAYSIZE(szCachedThumbMd5));

			TCHAR szThumbMd5Unicode[64] = { 0 };
            EncodeUtil::Utf8ToUnicode(pResult->m_szCustomFace, szThumbMd5Unicode, ARRAYSIZE(szThumbMd5Unicode));
			if (_tcsncmp(szCachedThumbMd5, szThumbMd5Unicode, 32) == 0)
			{
				m_UserMgr.m_UserInfo.m_bUseCustomFace = TRUE;
				m_UserMgr.m_UserInfo.m_bCustomFaceAvailable = TRUE;
			}
			//头像不存在，下载该头像
			else
			{
				CFileItemRequest* pFileItem = new CFileItemRequest();
				pFileItem->m_nFileType = FILE_ITEM_DOWNLOAD_USER_THUMB;
				pFileItem->m_uAccountID = m_UserMgr.m_UserInfo.m_uUserID;
				strcpy_s(pFileItem->m_szUtfFilePath, ARRAYSIZE(pFileItem->m_szUtfFilePath), pResult->m_szCustomFace);
				m_FileTask.AddItem(pFileItem);
			}

		}
		//下载头像
		else
		{
			CFileItemRequest* pFileItem = new CFileItemRequest();
			pFileItem->m_nFileType = FILE_ITEM_DOWNLOAD_USER_THUMB;
			pFileItem->m_uAccountID = m_UserMgr.m_UserInfo.m_uUserID;
			strcpy_s(pFileItem->m_szUtfFilePath, ARRAYSIZE(pFileItem->m_szUtfFilePath), pResult->m_szCustomFace);
			m_FileTask.AddItem(pFileItem);
		}

	}

    ::SendMessage(m_UserMgr.m_hCallBackWnd, FMG_MSG_UPDATE_BUDDY_INFO, 0, (LPARAM)m_UserMgr.m_UserInfo.m_uUserID);
    //::SendMessage(m_UserMgr.m_hCallBackWnd, FMG_MSG_UPDATE_GROUP_INFO, (WPARAM)uAccountID, 0);
	
    delete pResult;
}

void CFlamingoClient::OnRecvUserStatusChangeData(UINT message, WPARAM wParam, LPARAM lParam)
{
	CFriendStatus* pFriendStatus = (CFriendStatus*)lParam;
	if(pFriendStatus == NULL)
		return;

	//m_RecvMsgTask.AddMsgData(pFriendStatus);
}

void CFlamingoClient::OnUserStatusChange(UINT message, WPARAM wParam, LPARAM lParam)
{
	CFriendStatus* pFriendStatus = (CFriendStatus*)lParam;
	if(pFriendStatus == NULL)
		return;
	
	UINT uAccountID = pFriendStatus->m_uAccountID;
	long nFlag = pFriendStatus->m_nStatus;
	//long nStatus = ParseBuddyStatus(nFlag);

	

	//AtlTrace(_T("AccountID=%u, Status=%d\n"), uAccountID, nStatus);
	//如果用户信息暂时不可用，先缓存起来
	//if(!m_bBuddyListAvailable || !m_bGroupMemberInfoAvailable)
	//{
	//	std::map<UINT, long>::iterator iter = m_mapUserStatusCache.find(uAccountID);
	//	if(iter != m_mapUserStatusCache.end())
	//		iter->second = nStatus;
	//	else
	//		m_mapUserStatusCache.insert(std::make_pair(uAccountID, nStatus));

	//	return;
	//}
	
    if (pFriendStatus->m_type == 1 || pFriendStatus->m_type == 2)
    {
        SetBuddyStatus(uAccountID, nFlag);
        SetBuddyClientType(uAccountID, pFriendStatus->m_nClientType);

        //自己
        //if(uAccountID == m_UserMgr.m_UserInfo.m_uUserID)
        //{
        //	::SendMessage(m_UserMgr.m_hCallBackWnd, FMG_MSG_SELF_STATUS_CHANGE, (WPARAM)uAccountID, (LPARAM)nStatus);
        //}
        //else
        {
            ::SendMessage(m_UserMgr.m_hCallBackWnd, FMG_MSG_STATUS_CHANGE_MSG, 0, (LPARAM)uAccountID);
            ::SendMessage(m_UserMgr.m_hCallBackWnd, FMG_MSG_UPDATE_RECENT_LIST, 0, 0);
            ::SendMessage(m_UserMgr.m_hCallBackWnd, FMG_MSG_UPDATE_CHATDLG_USERINFO, (WPARAM)uAccountID, 0);
            ::SendMessage(m_UserMgr.m_hCallBackWnd, FMG_MSG_UPDATE_GROUP_INFO, (WPARAM)uAccountID, 0);
        }
    }

    delete pFriendStatus;
}

void CFlamingoClient::OnSendConfirmMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    CUploadFileResult* pUploadFileResult = (CUploadFileResult*)lParam;
    if (pUploadFileResult == NULL)
        return;

    if (wParam != 0)
    {
        //图片上传失败
        delete pUploadFileResult;
        return;
    }

    //上传图片结果
    if (pUploadFileResult->m_nFileType == FILE_ITEM_UPLOAD_CHAT_IMAGE)
    {
        time_t nTime = time(NULL);
        TCHAR szMd5[64] = { 0 };
        EncodeUtil::AnsiToUnicode(pUploadFileResult->m_szMd5, szMd5, ARRAYSIZE(szMd5));
        CString strImageName;
        strImageName.Format(_T("%s.%s"), szMd5, Hootina::CPath::GetExtension(pUploadFileResult->m_szLocalName).c_str());
        long nWidth = 0;
        long nHeight = 0;
        GetImageWidthAndHeight(pUploadFileResult->m_szLocalName, nWidth, nHeight);
        char szUtf8FileName[MAX_PATH] = { 0 };
        EncodeUtil::UnicodeToUtf8(strImageName, szUtf8FileName, ARRAYSIZE(szUtf8FileName));
        CStringA strImageAcquireMsg;
        //if (pUploadFileResult->m_bSuccessful)
        //    strImageAcquireMsg.Format("{\"msgType\":2,\"time\":%llu,\"clientType\":1,\"content\":[{\"pic\":[\"%s\",\"%s\",%u,%d,%d]}]}", nTime, szUtf8FileName, pUploadFileResult->m_szRemoteName, pUploadFileResult->m_dwFileSize, nWidth, nHeight);
        //else
        //    strImageAcquireMsg.Format("{\"msgType\":2,\"time\":%llu,\"clientType\":1,\"content\":[{\"pic\":[\"%s\",\"\",%u,%d,%d]}]}", nTime, szUtf8FileName, pUploadFileResult->m_dwFileSize, nWidth, nHeight);

        if (pUploadFileResult->m_bSuccessful)
            strImageAcquireMsg.Format("{\"msgType\":2,\"time\":%lld,\"clientType\":1,\"content\":[{\"pic\":[\"%s\",\"%s\",%u,%d,%d]}]}", nTime, szUtf8FileName, pUploadFileResult->m_szRemoteName, pUploadFileResult->m_nFileSize, nWidth, nHeight);
        else
            strImageAcquireMsg.Format("{\"msgType\":2,\"time\":%lld,\"clientType\":1,\"content\":[{\"pic\":[\"%s\",\"\",%u,%d,%d]}]}", nTime, szUtf8FileName, pUploadFileResult->m_nFileSize, nWidth, nHeight);

        long nBodyLength = strImageAcquireMsg.GetLength() + 1;
        char* pszMsgBody = new char[nBodyLength];
        memset(pszMsgBody, 0, nBodyLength);
        strcpy_s(pszMsgBody, nBodyLength, strImageAcquireMsg);
        CSentChatConfirmImageMessage* pConfirm = new CSentChatConfirmImageMessage();
        pConfirm->m_hwndChat = pUploadFileResult->m_hwndReflection;
        pConfirm->m_pszConfirmBody = pszMsgBody;
        pConfirm->m_uConfirmBodySize = nBodyLength - 1;
        pConfirm->m_uSenderID = pUploadFileResult->m_uSenderID;
        pConfirm->m_setTargetIDs = pUploadFileResult->m_setTargetIDs;
        if (pConfirm->m_setTargetIDs.size() > 1)
            pConfirm->m_nType = CHAT_CONFIRM_TYPE_MULTI;
        else
            pConfirm->m_nType = CHAT_CONFIRM_TYPE_SINGLE;

        //SendBuddyMsg(UINT nFromUin, const tstring& strFromNickName, UINT nToUin, const tstring& strToNickName, time_t nTime, const tstring& strChatMsg, HWND hwndFrom/* = NULL*/)

        m_SendMsgThread.AddItem(pConfirm);
    }

    delete pUploadFileResult;
}

void CFlamingoClient::OnUpdateChatMsgID(UINT message, WPARAM wParam, LPARAM lParam)
{
	UINT uAccountID = (UINT)wParam;
	UINT uMsgID = (UINT)lParam;
	m_UserMgr.SetMsgID(uAccountID, uMsgID);
}

void CFlamingoClient::OnBuddyCustomFaceAvailable(UINT message, WPARAM wParam, LPARAM lParam)
{
	UINT uAccountID = (UINT)wParam;
	if(uAccountID == 0)
		return;

	//企业版说明：v1 好友列表已删除，这里只剩"本人自定义头像可用"这一条；
	//联系人头像走 v2（cmd_user_info_changed_notify + AvatarDownloadResult），不经此处。
	if(uAccountID != m_UserMgr.m_UserInfo.m_uUserID)
		return;

	m_UserMgr.m_UserInfo.m_bCustomFaceAvailable = TRUE;

	::PostMessage(m_UserMgr.m_hCallBackWnd, FMG_MSG_UPDATE_BUDDY_INFO, 0, (LPARAM)uAccountID);
	::PostMessage(m_UserMgr.m_hCallBackWnd, FMG_MSG_UPDATE_RECENT_LIST, 0, 0);
}

void CFlamingoClient::OnModifyPasswordResult(UINT message, WPARAM wParam, LPARAM lParam)
{
    ::PostMessage(m_hwndModifyPassword, message, wParam, lParam);
}

void CFlamingoClient::OnCreateNewGroupResult(UINT message, WPARAM wParam, LPARAM lParam)
{
	::PostMessage(m_hwndCreateNewGroup, message, wParam, lParam);
}

void CFlamingoClient::OnUpdateGroupList(UINT message, WPARAM wParam, LPARAM lParam)
{
	//BOOL bSuccess = FALSE;
	//CGroupListResult* lpGroupListResult = (CGroupListResult*)lParam;
	//if (lpGroupListResult != NULL)
	//{
	//	for (int i = 0; i < (int)lpGroupListResult->m_arrGroupInfo.size(); i++)
	//	{
	//		CGroupInfo* lpGroupInfo = lpGroupListResult->m_arrGroupInfo[i];
	//		if (lpGroupInfo != NULL)
	//			m_UserMgr.m_GroupList.AddGroup(lpGroupInfo);
	//	}
	//	lpGroupListResult->m_arrGroupInfo.clear();
	//	delete lpGroupListResult;
	//	bSuccess = TRUE;
	//}
	//::SendMessage(m_UserMgr.m_hCallBackWnd, message, NULL, bSuccess);
}

void CFlamingoClient::OnUpdateRecentList(UINT message, WPARAM wParam, LPARAM lParam)
{
	::SendMessage(m_UserMgr.m_hCallBackWnd, message, 0, 0);
}

void CFlamingoClient::OnBuddyMsg(UINT message, WPARAM wParam, LPARAM lParam)
{
	CBuddyMessage* lpBuddyMsg = (CBuddyMessage*)lParam;
	if (NULL == lpBuddyMsg)
		return;
	
	UINT nUTalkUin = lpBuddyMsg->m_nFromUin;
	UINT nMsgId = lpBuddyMsg->m_nMsgId;

	m_UserMgr.m_MsgList.AddMsg(FMG_MSG_TYPE_BUDDY, lpBuddyMsg->m_nFromUin, 0, (void*)lpBuddyMsg);
	
	//更新最近会话列表以在最近会话列表中显示消息的个数
	::SendMessage(m_UserMgr.m_hCallBackWnd, FMG_MSG_UPDATE_RECENT_LIST, 0, 0);
	::SendMessage(m_UserMgr.m_hCallBackWnd, message, nUTalkUin, lParam);
}

void CFlamingoClient::OnGroupMsg(UINT message, WPARAM wParam, LPARAM lParam)
{
	CBuddyMessage* lpGroupMsg = (CBuddyMessage*)lParam;
	if (NULL == lpGroupMsg)
		return;
	
	UINT nGroupCode = lpGroupMsg->m_nToUin;
	UINT nMsgId = lpGroupMsg->m_nMsgId;

	m_UserMgr.m_MsgList.AddMsg(FMG_MSG_TYPE_GROUP, 
		lpGroupMsg->m_nToUin, lpGroupMsg->m_nToUin, (void*)lpGroupMsg);
	
	//更新最近会话列表以在最近会话列表中显示消息的个数
	::SendMessage(m_UserMgr.m_hCallBackWnd, FMG_MSG_UPDATE_RECENT_LIST, 0, 0);
	::SendMessage(m_UserMgr.m_hCallBackWnd, message, nGroupCode, nMsgId);
}

void CFlamingoClient::OnSessMsg(UINT message, WPARAM wParam, LPARAM lParam)
{
	CSessMessage* lpSessMsg = (CSessMessage*)lParam;
	if (NULL == lpSessMsg)
		return;
	
	UINT nUTalkUin = lpSessMsg->m_nFromUin;
	UINT nMsgId = lpSessMsg->m_nMsgId;
	UINT nGroupCode = 0;

	CGroupInfo* lpGroupInfo = m_UserMgr.m_GroupList.GetGroupById(lpSessMsg->m_nGroupId);
	if (lpGroupInfo != NULL)
	{
		nGroupCode = lpGroupInfo->m_nGroupCode;
		CBuddyInfo* lpBuddyInfo = lpGroupInfo->GetMemberByUin(lpSessMsg->m_nFromUin);
		if (NULL == lpBuddyInfo)
		{
			lpBuddyInfo = new CBuddyInfo;
			if (lpBuddyInfo != NULL)
			{
				lpBuddyInfo->Reset();
				lpBuddyInfo->m_uUserID = lpSessMsg->m_nFromUin;
				//lpBuddyInfo->m_nUTalkNum = lpSessMsg->m_nUTalkNum;
				lpGroupInfo->m_arrMember.push_back(lpBuddyInfo);
			}
			UpdateGroupMemberInfo(nGroupCode, lpSessMsg->m_nFromUin);
		}
	}

	m_UserMgr.m_MsgList.AddMsg(FMG_MSG_TYPE_SESS, 
		lpSessMsg->m_nFromUin, nGroupCode, (void*)lpSessMsg);

	::SendMessage(m_UserMgr.m_hCallBackWnd, message, nUTalkUin, nMsgId);
}

void CFlamingoClient::OnSysGroupMsg(UINT message, WPARAM wParam, LPARAM lParam)
{
	//CSysGroupMessage* lpSysGroupMsg = (CSysGroupMessage*)lParam;
	//if (NULL == lpSysGroupMsg)
	//	return;

	//UINT nGroupCode = lpSysGroupMsg->m_nGroupCode;

	//m_UserMgr.m_MsgList.AddMsg(FMG_MSG_TYPE_SYSGROUP, lpSysGroupMsg->m_nGroupCode, 
	//	lpSysGroupMsg->m_nGroupCode, (void*)lpSysGroupMsg);

	//::SendMessage(m_UserMgr.m_hCallBackWnd, message, 0, nGroupCode);
}

void CFlamingoClient::OnStatusChangeMsg(UINT message, WPARAM wParam, LPARAM lParam)
{
	//UINT nUTalkUin = 0;
	//CStatusChangeMessage* lpStatusChangeMsg = (CStatusChangeMessage*)lParam;
	//if (NULL == lpStatusChangeMsg)
	//	return;
	//
	//nUTalkUin = lpStatusChangeMsg->m_nUTalkUin;
	//CBuddyInfo* lpBuddyInfo = m_UserMgr.m_BuddyList.GetBuddy(nUTalkUin);
	//if (lpBuddyInfo != NULL)
	//{
	//	lpBuddyInfo->m_nStatus = lpStatusChangeMsg->m_nStatus;
	//	lpBuddyInfo->m_nClientType = lpStatusChangeMsg->m_nClientType;
	//	CBuddyTeamInfo* lpBuddyTeamInfo = m_UserMgr.m_BuddyList.GetBuddyTeam(lpBuddyInfo->m_nTeamIndex);
	//	if (lpBuddyTeamInfo != NULL)
	//		lpBuddyTeamInfo->Sort();
	//}
	//delete lpStatusChangeMsg;
	//::SendMessage(m_UserMgr.m_hCallBackWnd, message, 0, nUTalkUin);
}

void CFlamingoClient::OnKickMsg(UINT message, WPARAM wParam, LPARAM lParam)
{
    ::PostMessage(m_UserMgr.m_hCallBackWnd, FMG_MSG_SELF_STATUS_CHANGE, 0, (LPARAM)STATUS_ONLINE);
}

void CFlamingoClient::OnScreenshotMsg(UINT message, WPARAM wParam, LPARAM lParam)
{
    ::PostMessage(m_UserMgr.m_hCallBackWnd, FMG_MSG_SCREENSHOT, wParam, lParam);
}

void CFlamingoClient::OnUpdateBuddyNumber(UINT message, WPARAM wParam, LPARAM lParam)
{
	//UINT nUTalkUin = 0;
	//CGetUTalkNumResult* lpGetUTalkNumResult = (CGetUTalkNumResult*)lParam;
	//if (lpGetUTalkNumResult != NULL)
	//{
	//	nUTalkUin = lpGetUTalkNumResult->m_nUTalkUin;
	//	CBuddyInfo* lpBuddyInfo = m_UserMgr.m_BuddyList.GetBuddy(nUTalkUin);
	//	if (lpBuddyInfo != NULL)
	//		lpBuddyInfo->SetUTalkNum(lpGetUTalkNumResult);
	//	delete lpGetUTalkNumResult;
	//}
	//::SendMessage(m_UserMgr.m_hCallBackWnd, message, 0, nUTalkUin);
}

void CFlamingoClient::OnUpdateGMemberNumber(UINT message, WPARAM wParam, LPARAM lParam)
{
	//UINT nGroupCode = (UINT)wParam;
	//UINT nUTalkUin = 0;
	//CGetUTalkNumResult* lpGetUTalkNumResult = (CGetUTalkNumResult*)lParam;
	//if (nGroupCode != 0 && lpGetUTalkNumResult != NULL)
	//{
	//	nUTalkUin = lpGetUTalkNumResult->m_nUTalkUin;
	//	CBuddyInfo* lpBuddyInfo = m_UserMgr.m_GroupList.GetGroupMemberByCode(nGroupCode, nUTalkUin);
	//	if (lpBuddyInfo != NULL)
	//		lpBuddyInfo->SetUTalkNum(lpGetUTalkNumResult);
	//	delete lpGetUTalkNumResult;
	//}
	//::SendMessage(m_UserMgr.m_hCallBackWnd, message, nGroupCode, nUTalkUin);
}

void CFlamingoClient::OnUpdateGroupNumber(UINT message, WPARAM wParam, LPARAM lParam)
{
	//UINT nGroupCode = (UINT)wParam;
	//CGetUTalkNumResult* lpGetUTalkNumResult = (CGetUTalkNumResult*)lParam;
	//if (nGroupCode != 0 && lpGetUTalkNumResult != NULL)
	//{
	//	CGroupInfo* lpGroupInfo = m_UserMgr.m_GroupList.GetGroupByCode(nGroupCode);
	//	if (lpGroupInfo != NULL)
	//		lpGroupInfo->SetGroupNumber(lpGetUTalkNumResult);
	//	delete lpGetUTalkNumResult;
	//}
	//::SendMessage(m_UserMgr.m_hCallBackWnd, message, 0, nGroupCode);
}

void CFlamingoClient::OnUpdateBuddySign(UINT message, WPARAM wParam, LPARAM lParam)
{
	//UINT nUTalkUin = 0;
	//CGetSignResult* lpGetSignResult = (CGetSignResult*)lParam;
	//if (lpGetSignResult != NULL)
	//{
	//	nUTalkUin = lpGetSignResult->m_nUTalkUin;
	//	if (m_UserMgr.m_UserInfo.m_nUTalkUin == nUTalkUin)		// 更新用户个性签名
	//	{
	//		m_UserMgr.m_UserInfo.SetUTalkSign(lpGetSignResult);
	//	}
	//	else											// 更新好友个性签名
	//	{
	//		CBuddyInfo* lpBuddyInfo = m_UserMgr.m_BuddyList.GetBuddy(nUTalkUin);
	//		if (lpBuddyInfo != NULL)
	//			lpBuddyInfo->SetUTalkSign(lpGetSignResult);
	//	}
	//	delete lpGetSignResult;
	//}
	//::SendMessage(m_UserMgr.m_hCallBackWnd, message, NULL, nUTalkUin);
}

void CFlamingoClient::OnUpdateGMemberSign(UINT message, WPARAM wParam, LPARAM lParam)
{
	//UINT nGroupCode = (UINT)wParam;
	//UINT nUTalkUin = 0;
	//CGetSignResult* lpGetSignResult = (CGetSignResult*)lParam;

	//if (nGroupCode != 0 && lpGetSignResult != NULL)
	//{
	//	nUTalkUin = lpGetSignResult->m_nUTalkUin;
	//	CBuddyInfo* lpBuddyInfo = m_UserMgr.m_GroupList.GetGroupMemberByCode(nGroupCode, nUTalkUin);
	//	if (lpBuddyInfo != NULL)
	//		lpBuddyInfo->SetUTalkSign(lpGetSignResult);
	//	delete lpGetSignResult;
	//}
	//::SendMessage(m_UserMgr.m_hCallBackWnd, message, nGroupCode, nUTalkUin);
}

void CFlamingoClient::OnUpdateBuddyInfo(UINT message, WPARAM wParam, LPARAM lParam)
{
	//企业版说明：原先这里从 v1 好友列表里取自己那条记录再自我赋值（本身就是空操作），
	//随 m_BuddyList 一并删除，只保留向主窗口转发刷新通知。
	::SendMessage(m_UserMgr.m_hCallBackWnd, message, NULL, 0);
}

void CFlamingoClient::OnUpdateGMemberInfo(UINT message, WPARAM wParam, LPARAM lParam)
{
	//UINT nGroupCode = (UINT)wParam;
	//UINT nUTalkUin = 0;
	//CBuddyInfoResult* lpBuddyInfoResult = (CBuddyInfoResult*)lParam;

	//if (nGroupCode != 0 && lpBuddyInfoResult != NULL)
	//{
	//	//nUTalkUin = lpBuddyInfoResult->m_nUTalkUin;
	//	CBuddyInfo* lpBuddyInfo = m_UserMgr.m_GroupList.GetGroupMemberByCode(nGroupCode, nUTalkUin);
	//	if (lpBuddyInfo != NULL)
	//		lpBuddyInfo->SetBuddyInfo(lpBuddyInfoResult);
	//	delete lpBuddyInfoResult;
	//}
	//::SendMessage(m_UserMgr.m_hCallBackWnd, message, nGroupCode, nUTalkUin);
}

void CFlamingoClient::OnUpdateGroupInfo(UINT message, WPARAM wParam, LPARAM lParam)
{
	//UINT nGroupCode = 0;
	//CGroupInfoResult* lpGroupInfoResult = (CGroupInfoResult*)lParam;
	//if (lpGroupInfoResult != NULL)
	//{
	//	nGroupCode = lpGroupInfoResult->m_nGroupCode;
	//	CGroupInfo* lpGroupInfo = m_UserMgr.m_GroupList.GetGroupByCode(nGroupCode);
	//	if (lpGroupInfo != NULL)
	//		lpGroupInfo->SetGroupInfo(lpGroupInfoResult);
	//	delete lpGroupInfoResult;
	//}

	UINT nGroupCode = (UINT)lParam;
	::SendMessage(m_UserMgr.m_hCallBackWnd, message, 0, nGroupCode);
}

void CFlamingoClient::OnChangeStatusResult(UINT message, WPARAM wParam, LPARAM lParam)
{
	BOOL bSuccess = (BOOL)wParam;
	long nNewStatus = lParam;

	if (bSuccess)
		m_UserMgr.m_UserInfo.m_nStatus = nNewStatus;

	::SendMessage(m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
}

void CFlamingoClient::OnTargetInfoChange(UINT message, WPARAM wParam, LPARAM lParam)
{
	CTargetInfoChangeResult* pResult = (CTargetInfoChangeResult*)lParam;
	if(pResult == NULL)
		return;

	UINT uAccountID = pResult->m_uAccountID;
	delete pResult;

	if(uAccountID==0 || !IsGroupTarget(uAccountID))
		return;

	//群组账号只有基本信息和ID列表，没有扩展信息
	//CUserBasicInfoRequest* pBasicInfoRequest = new CUserBasicInfoRequest();
	//pBasicInfoRequest->m_setAccountID.insert(uAccountID);
	//m_SendMsgTask.AddItem(pBasicInfoRequest);
	
	m_bGroupMemberInfoAvailable = FALSE;
}

void CFlamingoClient::OnInternal_GetBuddyData(UINT message, WPARAM wParam, LPARAM lParam)
{
	UINT nUTalkUin = (UINT)wParam;
	RMT_BUDDY_DATA* lpBuddyData = (RMT_BUDDY_DATA*)lParam;
	if (NULL == lpBuddyData)
		return;

	//企业版说明：数据源 v1 好友列表已删除，本查询恒等于"查不到"。调用方
	//（CRecvMsgThread::GetBuddyData）本来就按"取不到就退回用账号ID显示"处理，
	//故这里直接返回，保持同一语义。
	(void)nUTalkUin;
}

void CFlamingoClient::OnInternal_GetGroupData(UINT message, WPARAM wParam, LPARAM lParam)
{
	UINT nGroupCode = (UINT)wParam;
	RMT_GROUP_DATA* lpGroupData = (RMT_GROUP_DATA*)lParam;
	if (NULL == lpGroupData)
		return;

	CGroupInfo* lpGroupInfo = m_UserMgr.m_GroupList.GetGroupByCode(nGroupCode);
	if (NULL == lpGroupInfo)
		return;

	lpGroupData->bHasGroupInfo = lpGroupInfo->m_bHasGroupInfo;
	lpGroupData->nGroupNum = lpGroupInfo->m_nGroupNumber;
}

void CFlamingoClient::OnInternal_GetGMemberData(UINT message, WPARAM wParam, LPARAM lParam)
{
	RMT_GMEMBER_REQ* lpGMemberReq = (RMT_GMEMBER_REQ*)wParam;
	RMT_BUDDY_DATA* lpGMemberData = (RMT_BUDDY_DATA*)lParam;
	if (NULL == lpGMemberReq || NULL == lpGMemberData)
		return;

	CGroupInfo* lpGroupInfo = m_UserMgr.m_GroupList.GetGroupByCode(lpGMemberReq->nGroupCode);
	if (NULL == lpGroupInfo)
		return;

	CBuddyInfo* lpBuddyInfo = lpGroupInfo->GetMemberByUin(lpGMemberReq->nUTalkUin);
	if (NULL == lpBuddyInfo)
		return;

	lpGMemberData->nUTalkNum = lpBuddyInfo->m_uUserID;

	int nMaxCnt = sizeof(lpGMemberData->szNickName) / sizeof(TCHAR);
	_tcsncpy(lpGMemberData->szNickName, lpBuddyInfo->m_strNickName.c_str(), nMaxCnt);
	lpGMemberData->szNickName[nMaxCnt-1] = _T('\0');
}

UINT CFlamingoClient::OnInternal_GroupId2Code(UINT message, WPARAM wParam, LPARAM lParam)
{
	CGroupInfo* lpGroupInfo = m_UserMgr.m_GroupList.GetGroupById(lParam);
	return ((lpGroupInfo != NULL) ? lpGroupInfo->m_nGroupCode : 0);
}
// 加载用户设置信息
void CFlamingoClient::LoadUserConfig()
{
	if(m_UserMgr.m_UserInfo.m_strAccount.empty())
		return;

	PCTSTR pszAccount = m_UserMgr.m_UserInfo.m_strAccount.c_str();
	
	TCHAR szConfigPath[MAX_PATH] = {0};
	_stprintf_s(szConfigPath, MAX_PATH, _T("%sUsers\\%s\\%s.cfg"), g_szHomePath, pszAccount, pszAccount);

	m_UserConfig.LoadConfig(szConfigPath);

	// 把配置里的自定义接收目录回填给 UserMgr，使本次会话的文件接收按它生效；
	// 空串时 UserMgr 自动退回默认推导路径。
	m_UserMgr.SetCustomRecvFilePath(m_UserConfig.GetRecvFilePath());
}

void CFlamingoClient::SaveUserConfig()
{
	if(m_UserMgr.m_UserInfo.m_strAccount.empty())
		return;

	PCTSTR pszAccount = m_UserMgr.m_UserInfo.m_strAccount.c_str();
	
	TCHAR szConfigPath[MAX_PATH] = {0};
	_stprintf_s(szConfigPath, MAX_PATH,_T("%sUsers\\%s\\%s.cfg"),  g_szHomePath, pszAccount, pszAccount);

	m_UserConfig.SaveConfig(szConfigPath);
}

BOOL CFlamingoClient::SetBuddyStatus(UINT uAccountID, long nStatus)
{
	//好友上线
	if(uAccountID!=m_UserMgr.m_UserInfo.m_uUserID)
	{
		m_UserMgr.SetStatus(uAccountID, nStatus);
		return TRUE;	
	}

	return FALSE;
}

BOOL CFlamingoClient::SetBuddyClientType(UINT uAccountID, long nNewClientType)
{
    if (uAccountID != m_UserMgr.m_UserInfo.m_uUserID)
    {
        m_UserMgr.SetClientType(uAccountID, nNewClientType);
        return TRUE;
    }

    return FALSE;
}


long CFlamingoClient::ParseBuddyStatus(long nFlag)
{
	long nOnlineClientType = OFFLINE_CLIENT_BOTH;

	if(nFlag & 0x40)
		nOnlineClientType = ONLINE_CLIENT_PC;
	else if(nFlag & 0x80)
		nOnlineClientType = ONLINE_CLIENT_MOBILE;
	
	long nStatus = STATUS_OFFLINE;
	if(nOnlineClientType == ONLINE_CLIENT_PC)
	{
		if(nFlag & 0x01)
			nStatus = STATUS_ONLINE;
		else if(nFlag & 0x02)
			nStatus = STATUS_INVISIBLE;
		else if(nFlag & 0x03)
			nStatus = STATUS_BUSY;
		else if(nFlag & 0x04)
			nStatus = STATUS_AWAY;
		
	}
	else if(nOnlineClientType == ONLINE_CLIENT_MOBILE)
	{	
		nStatus = STATUS_MOBILE_ONLINE;	
	}
		
	return nStatus;
}



void CFlamingoClient::GoOnline()
{
	if(m_bNetworkAvailable)
		Login();
}

void CFlamingoClient::GoOffline()
{
    //先立起"用户主动离线"闸门，再关连接。顺序不能反：闸门一旦立起，网络层就不再
    //自发重连、也不再往发送缓冲排帧（见 CIUSocket::SetUserOffline 的说明）；若先关
    //连接再立闸门，中间那一小段里发送线程有机会自己把连接重新建起来。
    CIUSocket::GetInstance().SetUserOffline(true);

    //丢弃排队中尚未发出的业务帧，否则下次连上会被批量补发，对端突然收到一堆过期消息。
    CIUSocket::GetInstance().ClearPendingSendData();

    CIUSocket::GetInstance().Close();
    CIUSocket::GetInstance().CloseFileServerConnection();
    CIUSocket::GetInstance().CloseImgServerConnection();

	m_UserMgr.ResetToOfflineStatus();
	//m_HeartbeatTask.Stop();

	//m_mapUserStatusCache.clear();
	
	::SendMessage(m_UserMgr.m_hCallBackWnd, FMG_MSG_UPDATE_BUDDY_INFO, 0, (LPARAM)m_UserMgr.m_UserInfo.m_uUserID);
	::SendMessage(m_UserMgr.m_hCallBackWnd, FMG_MSG_UPDATE_RECENT_LIST, 0, 0);
	::SendMessage(m_UserMgr.m_hCallBackWnd, FMG_MSG_UPDATE_GROUP_LIST, 0, 0);
}

void CFlamingoClient::CacheBuddyStatus()
{
	//群成员的在线状态也要缓存
	CGroupInfo* pGroupInfo = NULL;
	for(std::vector<CGroupInfo*>::iterator iter=m_UserMgr.m_GroupList.m_arrGroupInfo.begin(); 
		iter!=m_UserMgr.m_GroupList.m_arrGroupInfo.end();
		++iter)
	{
		pGroupInfo =*iter;
		if(pGroupInfo == NULL)
			continue;;

		for(size_t j=0; j<pGroupInfo->m_arrMember.size(); ++j)
		{
			// pBuddyInfo 原先在函数开头声明，供"遍历 v1 好友列表"与"遍历群成员"
			// 两个循环共用。好友列表随好友子系统删除后，只剩群成员这一个循环，
			// 变量就地声明即可。
			CBuddyInfo* pBuddyInfo = pGroupInfo->m_arrMember[j];
			if(pBuddyInfo == NULL)
				continue;

			if(pBuddyInfo->m_nStatus != STATUS_OFFLINE)
				m_mapUserStatusCache.insert(std::pair<UINT, long>(pBuddyInfo->m_uUserID, pBuddyInfo->m_nStatus));
		}
	}
}

// 创建代理窗口
BOOL CFlamingoClient::CreateProxyWnd()
{
	WNDCLASSEX wcex;
	LPCTSTR szWindowClass = _T("FLAMINGO_PROXY_WND");
	HWND hWnd;

	DestroyProxyWnd();	// 销毁代理窗口

	HINSTANCE hInstance = ::GetModuleHandle(NULL);

	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = ProxyWndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = NULL;
	wcex.hCursor = NULL;
	wcex.hbrBackground = NULL;
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = szWindowClass;
	wcex.hIconSm = NULL;

	if (!::RegisterClassEx(&wcex))
		return FALSE;

	hWnd = ::CreateWindow(szWindowClass, NULL, WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, hInstance, NULL);
	if (NULL == hWnd)
		return FALSE;

	::SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)this);
	
    m_RecvMsgThread.SetProxyWnd(hWnd);
    m_UserMgr.m_hProxyWnd = hWnd;

	return TRUE;
}

// 销毁代理窗口
BOOL CFlamingoClient::DestroyProxyWnd()
{
	if (m_UserMgr.m_hProxyWnd != NULL)
	{
		::DestroyWindow(m_UserMgr.m_hProxyWnd);
		m_UserMgr.m_hProxyWnd = NULL;
	}
	return TRUE;
}

LRESULT CALLBACK CFlamingoClient::ProxyWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	CFlamingoClient* lpFMGClient = (CFlamingoClient*)::GetWindowLongPtr(hWnd, GWLP_USERDATA);
	if (NULL == lpFMGClient)
		return ::DefWindowProc(hWnd, message, wParam, lParam);

	if (message < FMG_MSG_FIRST || message > FMG_MSG_LAST)
		return ::DefWindowProc(hWnd, message, wParam, lParam);

	switch (message)
	{
    //网络错误
    case FMG_MSG_NET_ERROR:
        ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, FMG_MSG_NET_ERROR, 0, 0);
        break;

	case FMG_MSG_HEARTBEAT:
		lpFMGClient->OnHeartbeatResult(message, wParam, lParam);
		break;

	case FMG_MSG_NETWORK_STATUS_CHANGE:
		lpFMGClient->OnNetworkStatusChange(message, wParam, lParam);
		break;
	
	case FMG_MSG_REGISTER:				// 注册结果
		lpFMGClient->OnRegisterResult(message, wParam, lParam);
		break;
	case FMG_MSG_LOGIN_RESULT:			// 登录返回消息
		lpFMGClient->OnLoginResult(message, wParam, lParam);
		break;
	case FMG_MSG_LOGOUT_RESULT:			// 注销返回消息
	case FMG_MSG_UPDATE_BUDDY_HEADPIC:	// 更新好友头像
		//::MessageBox(NULL, _T("Change headpic"), _T("Change head"), MB_OK);
	case FMG_MSG_UPDATE_GMEMBER_HEADPIC:	// 更新群成员头像
	case FMG_MSG_UPDATE_GROUP_HEADPIC:	// 更新群头像
		::SendMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
		break;
    case FMG_MSG_UPDATE_GROUP_BASIC_INFO:
        lpFMGClient->OnUpdateGroupBasicInfo(message, wParam, lParam);
        break;

	case FMG_MSG_MODIFY_USER_INFO:				//修改个人信息结果
		lpFMGClient->OnModifyInfoResult(message, wParam, lParam);
		break;
	case FMG_MSG_RECV_USER_STATUS_CHANGE_DATA:
		lpFMGClient->OnRecvUserStatusChangeData(message, wParam, lParam);
		break;
	
	case FMG_MSG_USER_STATUS_CHANGE:
		lpFMGClient->OnUserStatusChange(message, wParam, lParam);
		break;

    case FMG_MSG_UPLOAD_USER_THUMB:
        lpFMGClient->OnSendConfirmMessage(message, wParam, lParam);
        break;

	case FMG_MSG_UPDATE_USER_CHAT_MSG_ID:
		lpFMGClient->OnUpdateChatMsgID(message, wParam, lParam);
		break;	
	case FMG_MSG_CUSTOMFACE_AVAILABLE:
		lpFMGClient->OnBuddyCustomFaceAvailable(message, wParam, lParam);
		break;

	case FMG_MSG_MODIFY_PASSWORD_RESULT:
		lpFMGClient->OnModifyPasswordResult(message, wParam, lParam);
		break;

	case FMG_MSG_CREATE_NEW_GROUP_RESULT:
		lpFMGClient->OnCreateNewGroupResult(message, wParam, lParam);
		break;

	case FMG_MSG_UPDATE_GROUP_LIST:		// 更新群列表消息
		lpFMGClient->OnUpdateGroupList(message, wParam, lParam);
		break;
	case FMG_MSG_UPDATE_RECENT_LIST:		// 更新最近联系人列表消息
		lpFMGClient->OnUpdateRecentList(message, wParam, lParam);
		break;
	case FMG_MSG_BUDDY_MSG:				// 好友消息
		lpFMGClient->OnBuddyMsg(message, wParam, lParam);
		break;
	case FMG_MSG_GROUP_MSG:				// 群消息
		lpFMGClient->OnGroupMsg(message, wParam, lParam);
		break;
	case FMG_MSG_SESS_MSG:				// 临时会话消息
		lpFMGClient->OnSessMsg(message, wParam, lParam);
		break;
	case FMG_MSG_STATUS_CHANGE_MSG:		// 好友状态改变消息
		lpFMGClient->OnStatusChangeMsg(message, wParam, lParam);
		break;
    case FMG_MSG_SELF_STATUS_CHANGE:	//自己的状态发生改变，例如被踢下线消息
		lpFMGClient->OnKickMsg(message, wParam, lParam);
		break;
    case FMG_MSG_SCREENSHOT:    //截屏消息
        lpFMGClient->OnScreenshotMsg(message, wParam, lParam);
        break;

    //v2 单聊消息（收到推送 / 发送回执）：转发给主窗口处理并路由到对应聊天窗。
    //lParam 为堆上 net:: 结构体指针，主窗口处理后负责 delete；若主窗口未就绪
    //则在此就地释放，避免泄漏。
    case FMG_MSG_V2_RECV_CHATMSG:
    case FMG_MSG_V2_SEND_CHATMSG_ACK:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else if (message == FMG_MSG_V2_RECV_CHATMSG)
            delete (net::MessageNotify*)lParam;
        else
            delete (net::SendMessageResponse*)lParam;
        break;

    //v2 通讯录应答：转发给主窗口填充联系人面板；主窗口未就绪则就地释放。
    case FMG_MSG_V2_CONTACTS:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::ContactsResult*)lParam;
        break;

    //v2 会话列表应答：转发给主窗口填充"消息"页；主窗口未就绪则就地释放。
    case FMG_MSG_V2_CONVERSATIONS:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::ConversationsResult*)lParam;
        break;

    //v2 建群应答：转发给主窗口提示建群结果；主窗口未就绪则就地释放。
    case FMG_MSG_V2_CREATE_GROUP_RESULT:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::CreateGroupResponse*)lParam;
        break;

    //v2 修改头像应答：转发给主窗口（→修改头像对话框提示/关闭、本端头像刷新）；未就绪则就地释放。
    case FMG_MSG_V2_CHANGE_AVATAR_RESULT:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::UpdateProfileResponse*)lParam;
        break;

    //v2 头像图片上传结果：转发给主窗口（→修改头像对话框，成功后发 cmd_change_avatar）；未就绪则就地释放。
    case FMG_MSG_V2_AVATAR_UPLOAD_RESULT:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::AvatarUploadResult*)lParam;
        break;

    //v2 头像图片下载结果：转发给主窗口（→主面板按 fileid 刷新联系人/会话节点头像）；未就绪则就地释放。
    case FMG_MSG_V2_AVATAR_DOWNLOAD_RESULT:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::AvatarDownloadResult*)lParam;
        break;

    //v2 联系人资料变更推送（头像/昵称）：转发给主窗口实时刷新联系人/会话头像；未就绪则就地释放。
    case FMG_MSG_V2_USERINFO_CHANGED:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::UserInfoChangedNotify*)lParam;
        break;

    //v2 会话属性多端同步推送：转发给主窗口按新状态刷新会话列表（置顶 / 免打扰 / 删除）；
    //未就绪则就地释放。
    case FMG_MSG_V2_CONVERSATION_CHANGED:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::ConversationChangedNotify*)lParam;
        break;

    //v2 群管理写操作应答（邀请/移出/设管理员/禁言/全员禁言/改群资料/转让/解散/
    //审批/敏感词增删共用）：转发给主窗口，成功不动界面（等 1317/1318 推送统一刷新），
    //失败据 code 提示。wParam 带的是具体哪条协议，须原样传下去。
    case FMG_MSG_V2_GROUP_OP_RESULT:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::GroupOpResult*)lParam;
        break;

    //v2 待审批加群申请列表应答：转发给群管理窗口的"加群审批"页
    case FMG_MSG_V2_GROUP_JOIN_REQUESTS:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::GroupJoinRequestsResult*)lParam;
        break;

    //v2 群敏感词库应答：转发给群权限页的"管理词库"
    case FMG_MSG_V2_SENSITIVE_WORDS:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::SensitiveWordsResult*)lParam;
        break;

    //v2 群成员变动推送：转发给主窗口，刷新群管理窗口、群聊窗成员边栏与标题
    case FMG_MSG_V2_GROUP_MEMBER_CHANGED:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::GroupMemberChangedNotify*)lParam;
        break;

    //v2 群资料变更推送：转发给主窗口，刷新群名与各页；changetype=2 时关窗
    case FMG_MSG_V2_GROUP_INFO_CHANGED:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::GroupInfoChangedNotify*)lParam;
        break;

    //v2 群公告列表应答：转发给主窗口，再由它分给群管理窗的公告页或群聊窗的公告边栏
    case FMG_MSG_V2_GROUP_ANNOUNCEMENTS:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::GroupAnnouncementsResult*)lParam;
        break;

    //v2 群公告变更推送：转发给主窗口刷新公告界面；新发布时还要弹一次提示
    case FMG_MSG_V2_ANNOUNCEMENT_CHANGED:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::GroupAnnouncementNotify*)lParam;
        break;

    //v2 有人申请加群推送：转发给主窗口，让审批入口出现待处理红点
    case FMG_MSG_V2_JOIN_GROUP_REQUEST:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::GroupJoinRequestItem*)lParam;
        break;

    //v2 切换通话媒体类型应答：转发给主窗口按 code 提示失败原因（成功不动界面，等推送）；
    //未就绪则就地释放，避免堆对象泄漏。
    case FMG_MSG_V2_CALL_SWITCH_MEDIA_RESULT:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::CallSwitchMediaResponse*)lParam;
        break;

    //v2 通话媒体类型已变更推送：转发给主窗口重建通话窗媒体层、启停视频引擎；
    //未就绪则就地释放。
    case FMG_MSG_V2_CALL_MEDIA_CHANGED:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::CallMediaChangedNotify*)lParam;
        break;

    //v2 撤回消息应答：转发给主窗口按 code 提示失败原因（成功不动界面，等下面的推送）；
    //未就绪则就地释放，避免堆对象泄漏。
    case FMG_MSG_V2_RECALL_RESULT:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::RecallMessageResponse*)lParam;
        break;

    //v2 某条消息被撤回的推送：转发给主窗口把气泡改成提示行 / 刷会话列表预览 /
    //标记本地消息库；未就绪则就地释放。
    case FMG_MSG_V2_MESSAGE_RECALLED:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::MessageRecalledNotify*)lParam;
        break;

    //v2 平台公告列表应答：转发给主窗口落本地库 announce.db 并刷新铃铛角标；
    //未就绪则就地释放，避免堆对象泄漏。
    case FMG_MSG_V2_ANNOUNCEMENTS:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::AnnouncementsResult*)lParam;
        break;

    //v2 平台公告新发布 / 被撤回的推送：转发给主窗口落本地库、刷角标、刷消息盒子窗；
    //未就绪则就地释放。
    case FMG_MSG_V2_ANNOUNCEMENT_NOTIFY:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::AnnouncementNotify*)lParam;
        break;

    //v2 平台公告已读上报的应答：转发给主窗口（成功不动界面，仅失败时留痕）；
    //未就绪则就地释放。
    case FMG_MSG_V2_ANN_MARK_READ_RESULT:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::AnnounceMarkReadResult*)lParam;
        break;

    //v2 设置好友备注名的应答：转发给主窗口（成功只记日志，失败时主面板据回带的
    //userid 把那一个人的备注回滚）；未就绪则就地释放。
    case FMG_MSG_V2_SET_REMARK_RESULT:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::SetFriendRemarkResult*)lParam;
        break;

    //v2 Pin 消息应答：转发给主窗口按 code 提示失败原因（成功不动界面，等下面的推送）；
    //未就绪则就地释放，避免堆对象泄漏。
    case FMG_MSG_V2_PIN_RESULT:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::PinMessageResponse*)lParam;
        break;

    //v2 某条消息被 Pin / 取消 Pin 的推送：转发给主窗口落本地消息库标记 / 刷气泡上的
    //图钉 / 刷消息记录窗；未就绪则就地释放。
    case FMG_MSG_V2_MESSAGE_PINNED:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::MessagePinnedNotify*)lParam;
        break;

    //v2 被 Pin 消息全集应答：转发给主窗口补齐本地库缺的消息体并与服务端对账；
    //未就绪则就地释放。
    case FMG_MSG_V2_PINNED_MESSAGES:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::PinnedMessagesResult*)lParam;
        break;

    //v2 被踢下线推送：转发给主窗口进入离线态（停自动重连）并弹托盘气泡。
    //wParam 是被踢原因，无堆负载，主窗口未就绪时直接丢掉即可（此时也没有界面可提示）。
    case FMG_MSG_V2_KICKOUT:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        break;

    //v2 联系人在线状态变更推送：转发给主窗口重着联系人/会话状态点；未就绪则就地释放。
    case FMG_MSG_V2_PRESENCE_CHANGED:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::PresenceChangedNotify*)lParam;
        break;

    //v2 群资料应答：转发给主窗口写群信息缓存 / 刷新群名；未就绪则就地释放。
    case FMG_MSG_V2_GROUP_INFO:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::GroupInfoResult*)lParam;
        break;

    //v2 群成员应答：转发给主窗口路由到对应群聊窗；未就绪则就地释放。
    case FMG_MSG_V2_GROUP_MEMBERS:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::GroupMembersResult*)lParam;
        break;

    //v2 群文件列表应答：转发给主窗口路由到对应群聊窗；未就绪则就地释放。
    case FMG_MSG_V2_GROUP_FILES:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::GetGroupFilesResult*)lParam;
        break;

    //v2 群文件变更推送：转发给主窗口路由到对应群聊窗；未就绪则就地释放。
    case FMG_MSG_V2_GROUP_FILES_CHANGED:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::GroupFilesChangedNotify*)lParam;
        break;

    //v2 群文件上传结果：转发给主窗口路由到对应群聊窗；未就绪则就地释放。
    case FMG_MSG_V2_GROUP_FILE_UPLOAD_RESULT:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::GroupFileUploadResult*)lParam;
        break;

    //v2 群文件下载结果：转发给主窗口路由到对应群聊窗；未就绪则就地释放。
    case FMG_MSG_V2_GROUP_FILE_DOWNLOAD_RESULT:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::GroupFileDownloadResult*)lParam;
        break;

    //v2 群文件上传进度：转发给主窗口路由到对应群聊窗；未就绪则就地释放。
    case FMG_MSG_V2_GROUP_FILE_UPLOAD_PROGRESS:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::GroupFileUploadProgress*)lParam;
        break;

    //v2 群文件下载进度：转发给主窗口路由到对应群聊窗；未就绪则就地释放。
    case FMG_MSG_V2_GROUP_FILE_DOWNLOAD_PROGRESS:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::GroupFileDownloadProgress*)lParam;
        break;

    //v2 我的群列表应答：转发给主窗口填"群"Tab；未就绪则就地释放。
    case FMG_MSG_V2_USER_GROUPS:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::UserGroupsResult*)lParam;
        break;

    //v2 历史消息应答：转发给主窗口路由到对应聊天窗渲染历史；未就绪则就地释放。
    case FMG_MSG_V2_SYNC_MESSAGES:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::SyncMessagesResult*)lParam;
        break;

    //v2 远程桌面邀请应答：转发给主窗口（→控制端呼叫窗按 code 置会话 / 提示失败）；未就绪则就地释放。
    case FMG_MSG_V2_RD_INVITE_ACK:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::RDInviteResponse*)lParam;
        break;

    //v2 远程桌面邀请推送：转发给主窗口（→被控端弹授权窗）；未就绪则就地释放。
    case FMG_MSG_V2_RD_INVITE_NOTIFY:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::RDInviteNotify*)lParam;
        break;

    //v2 远程桌面中转参数下发：转发给主窗口（→双方接通，连 imgserver 中转通道）；未就绪则就地释放。
    case FMG_MSG_V2_RD_RELAY_INFO:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::RDRelayInfo*)lParam;
        break;

    //v2 远程桌面状态推送：转发给主窗口（→关对话框并按 state 提示拒绝 / 取消 / 离线 / 超时 / 断开）；未就绪则就地释放。
    case FMG_MSG_V2_RD_STATE_NOTIFY:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::RDStateNotify*)lParam;
        break;

    //v2 音视频通话邀请应答：转发给主窗口（→主叫呼叫窗按 code 置会话 / 提示失败）；未就绪则就地释放。
    case FMG_MSG_V2_CALL_INVITE_ACK:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::CallInviteResponse*)lParam;
        break;

    //v2 音视频通话邀请推送：转发给主窗口（→被叫弹来电窗）；未就绪则就地释放。
    case FMG_MSG_V2_CALL_INVITE_NOTIFY:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::CallInviteNotify*)lParam;
        break;

    //v2 音视频通话中转参数下发：转发给主窗口（→双方接通，连 imgserver 中转通道）；未就绪则就地释放。
    case FMG_MSG_V2_CALL_SIGNALING:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::CallSignalingInfo*)lParam;
        break;

    //v2 音视频通话状态推送：转发给主窗口（→关对话框并按 state 提示 / 收尾）；未就绪则就地释放。
    case FMG_MSG_V2_CALL_STATE_NOTIFY:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::CallStateNotify*)lParam;
        break;

    //v2 发图上传进度：转发给主窗口按 clientmsgid 更新"发送中"气泡进度；未就绪则就地释放。
    case FMG_MSG_V2_IMAGE_SEND_PROGRESS:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::ImageSendProgress*)lParam;
        break;

    //v2 发图最终结果：转发给主窗口收尾"发送中"气泡（成功转正常 / 失败标红）；未就绪则就地释放。
    case FMG_MSG_V2_IMAGE_SEND_RESULT:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::ImageSendResult*)lParam;
        break;

    //v2 收图下载结果：转发给主窗口按 msgid 把"下载中"气泡回填渲染 / 标失败；未就绪则就地释放。
    case FMG_MSG_V2_IMAGE_DOWNLOAD_RESULT:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::ImageDownloadResult*)lParam;
        break;

    //v2 发文件上传进度：转发给主窗口更新"发送中"文件气泡进度；未就绪则就地释放。
    case FMG_MSG_V2_FILE_SEND_PROGRESS:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::FileSendProgress*)lParam;
        break;

    //v2 发文件最终结果：转发给主窗口收尾"发送中"文件气泡（成功转完成 / 失败标红）；未就绪则就地释放。
    case FMG_MSG_V2_FILE_SEND_RESULT:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::FileSendResult*)lParam;
        break;

    //v2 收文件下载进度：转发给主窗口按 msgid 更新"下载中"文件气泡进度；未就绪则就地释放。
    case FMG_MSG_V2_FILE_DOWNLOAD_PROGRESS:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::FileDownloadProgress*)lParam;
        break;

    //v2 收文件下载结果：转发给主窗口按 msgid 把"下载中"文件气泡回填渲染 / 标失败；未就绪则就地释放。
    case FMG_MSG_V2_FILE_DOWNLOAD_RESULT:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::FileDownloadResult*)lParam;
        break;

    //v2 对方收取本端所发文件的状态：转发给主窗口按 msgid 更新那条文件气泡的状态行；未就绪则就地释放。
    case FMG_MSG_V2_FILE_TRANSFER_ACK:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::FileTransferAckNotify*)lParam;
        break;

    //v2 对端正在 / 停止输入：转发给主窗口按 cid 路由到单聊窗刷新头部提示；未就绪则就地释放。
    case FMG_MSG_V2_TYPING_NOTIFY:
        if (lpFMGClient->m_UserMgr.m_hCallBackWnd != NULL)
            ::PostMessage(lpFMGClient->m_UserMgr.m_hCallBackWnd, message, wParam, lParam);
        else
            delete (net::TypingNotify*)lParam;
        break;
	case FMG_MSG_SYS_GROUP_MSG:			// 群系统消息
		lpFMGClient->OnSysGroupMsg(message, wParam, lParam);
		break;
	case FMG_MSG_UPDATE_BUDDY_NUMBER:	// 更新好友号码
		lpFMGClient->OnUpdateBuddyNumber(message, wParam, lParam);
		break;
	case FMG_MSG_UPDATE_GMEMBER_NUMBER:	// 更新群成员号码
		lpFMGClient->OnUpdateGMemberNumber(message, wParam, lParam);
		break;
	case FMG_MSG_UPDATE_GROUP_NUMBER:	// 更新群号码
		lpFMGClient->OnUpdateGroupNumber(message, wParam, lParam);
		break;
	case FMG_MSG_UPDATE_BUDDY_SIGN:		// 更新好友个性签名
		lpFMGClient->OnUpdateBuddySign(message, wParam, lParam);
		break;
	case FMG_MSG_UPDATE_GMEMBER_SIGN:	// 更新群成员个性签名
		lpFMGClient->OnUpdateGMemberSign(message, wParam, lParam);
		break;
	case FMG_MSG_UPDATE_BUDDY_INFO:		// 更新用户信息
		lpFMGClient->OnUpdateBuddyInfo(message, wParam, lParam);
		break;
	case FMG_MSG_UPDATE_GMEMBER_INFO:	// 更新群成员信息
		lpFMGClient->OnUpdateGMemberInfo(message, wParam, lParam);
		break;
	case FMG_MSG_UPDATE_GROUP_INFO:		// 更新群信息
		lpFMGClient->OnUpdateGroupInfo(message, wParam, lParam);
		break;
	case FMG_MSG_UPDATE_C2CMSGSIG:		// 更新临时会话信令
		//lpFMGClient->OnUpdateC2CMsgSig(message, wParam, lParam);
		break;
	case FMG_MSG_CHANGE_STATUS_RESULT:	// 改变在线状态返回消息
		lpFMGClient->OnChangeStatusResult(message, wParam, lParam);
		break;
	case FMG_MSG_TARGET_INFO_CHANGE:		//有用户信息发生改变：
		lpFMGClient->OnTargetInfoChange(message, wParam, lParam);
		break;

	case FMG_MSG_INTERNAL_GETBUDDYDATA:
		lpFMGClient->OnInternal_GetBuddyData(message, wParam, lParam);
		break;
	case FMG_MSG_INTERNAL_GETGROUPDATA:
		lpFMGClient->OnInternal_GetGroupData(message, wParam, lParam);
		break;
	case FMG_MSG_INTERNAL_GETGMEMBERDATA:
		lpFMGClient->OnInternal_GetGMemberData(message, wParam, lParam);
		break;
	case FMG_MSG_INTERNAL_GROUPID2CODE:
		return lpFMGClient->OnInternal_GroupId2Code(message, wParam, lParam);
		break;

	default:
		return ::DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}
