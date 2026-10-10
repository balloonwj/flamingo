#include "stdafx.h"
#include "IUProtocolData.h"
#include "Protocol.h"   // enum profile_visibility

//class NetData
CNetData::CNetData()
{
	m_uType = 0;
	m_nRetryTimes = 0;
}

CNetData::~CNetData()
{
}

CRegisterRequest::CRegisterRequest()
{
	m_uType = NET_DATA_REGISTER;
	memset(m_szAccountName, 0, sizeof(m_szAccountName));
	memset(m_szNickName, 0, sizeof(m_szNickName));
	memset(m_szPassword, 0, sizeof(m_szPassword));
}

CRegisterRequest::~CRegisterRequest()
{

}

//class CRegisterResult
CRegisterResult::CRegisterResult()
{
	m_uType = NET_DATA_REGISTER;
	m_nResultCode = REGISTER_FAILED;
	memset(m_szMobile, 0, sizeof(m_szMobile));
	memset(m_szAccount, 0, sizeof(m_szAccount));
}

CRegisterResult::~CRegisterResult()
{

}

CLoginRequest::CLoginRequest()
{
	m_uType = NET_DATA_LOGIN;
	memset(m_szAccountName, 0, sizeof(m_szAccountName));
	memset(m_szPassword, 0, sizeof(m_szPassword));
	m_nStatus = STATUS_ONLINE;
	m_nLoginType = LOGIN_USE_MOBILE_NUMBER;
}

CLoginRequest::~CLoginRequest()
{
}

//class CLoginResult
CLoginResult::CLoginResult()
{
	m_uType = NET_DATA_LOGIN;
	m_LoginResultCode = LOGIN_FAILED;
	m_uAccountID = 0;
	//默认不带轮次号：自动登录这类不经登录窗的路径就用这个值
	m_uLoginGeneration = 0;
	//默认关闭：失败应答与老服务端都没有 features
	m_nFileShareFeature = 0;
	m_nInviteConfirmFeature = 0;
	m_nGroupCallFeature = 0;
	m_nCaptionFeature = 0;
	m_nCloudRecordFeature = 0;
	m_nSecretChatFeature = 0;
	m_nThreadGroupFeature = 0;
	m_nBotFeature = 0;
	m_nBotGroupFeature = 0;
	m_nBotPersonalFeature = 0;
	m_nBotGroupExternalFeature = 0;
	m_nFace = 0;
    m_nGender = 0;
    m_nBirthday = 19900101;
    //公开范围默认值与服务端两列的默认值一致：手机号仅本人可见、座机对同事可见
    m_nPhoneVisible = profile_visibility_self_only;
    m_nLandlineVisible = profile_visibility_colleagues;
	m_nStatus = STATUS_OFFLINE;
}

CLoginResult::~CLoginResult()
{

}

CUserBasicInfoRequest::CUserBasicInfoRequest()
{
	m_uType = NET_DATA_USER_BASIC_INFO;
}

CUserBasicInfoRequest::~CUserBasicInfoRequest()
{
	
}

//class CUserBasicInfoResult
CUserBasicInfoResult::CUserBasicInfoResult()
{
	m_uType = NET_DATA_USER_BASIC_INFO;
}

CUserBasicInfoResult::~CUserBasicInfoResult()
{

}

CChangeUserStatusRequest::CChangeUserStatusRequest()
{
    m_uType = NET_DATA_CHANGE_STATUS;
    m_nNewStatus = 0;
}

CChangeUserStatusRequest::~CChangeUserStatusRequest()
{
    
}

//class CGroupBasicInfoRequest
CGroupBasicInfoRequest::CGroupBasicInfoRequest()
{
    m_uType = NET_DATA_GROUP_BASIC_INFO;
}

CGroupBasicInfoRequest::~CGroupBasicInfoRequest()
{

}

//class CGroupBasicInfoResult
CGroupBasicInfoResult::CGroupBasicInfoResult()
{
    m_uType = NET_DATA_GROUP_BASIC_INFO;
}

CGroupBasicInfoResult::~CGroupBasicInfoResult()
{

}

//class CUserExtendInfoRequest
CUserExtendInfoRequest::CUserExtendInfoRequest()
{
	m_uType = NET_DATA_USER_EXTEND_INFO;
}

CUserExtendInfoRequest::~CUserExtendInfoRequest()
{
	
}

//class CLoginUserExtendInfo
CUserExtendInfoResult::CUserExtendInfoResult()
{
	m_uType = NET_DATA_USER_EXTEND_INFO;
}

CUserExtendInfoResult::~CUserExtendInfoResult()
{

}

//class CFriendStatus
CFriendStatus::CFriendStatus()
{
	m_uType = NET_DATA_USER_STATUS;
	m_uAccountID = 0;
	m_nStatus = 0;
    m_nClientType = 0;
    m_type = 0;
}

CFriendStatus::~CFriendStatus()
{
}

//class CSentChatMessage
CSentChatMessage::CSentChatMessage()
{
	m_uType = NET_DATA_CHAT_MSG;
	m_pMsgItem = NULL;
	m_hwndChat = NULL;
}

CSentChatMessage::~CSentChatMessage()
{

}

//class CSentChatConfirmImageMessage
CSentChatConfirmImageMessage::CSentChatConfirmImageMessage()
{
	m_uType = NET_DATA_CHAT_CONFIRM_IMAGE_MSG;
	m_nType = CHAT_CONFIRM_TYPE_SINGLE;
	m_hwndChat = NULL;
	m_pszConfirmBody = NULL;
	m_uConfirmBodySize = 0;
	m_uSenderID = 0;
	//m_uTargetID = 0;
}

CSentChatConfirmImageMessage::~CSentChatConfirmImageMessage()
{

}

//class CRecvChatMessage
CRecvChatMessage::CRecvChatMessage()
{
	m_uType = NET_DATA_CHAT_MSG;
	m_uMsgID = 0;
	m_uSenderID = 0;
	m_uTargetID = 0;
	m_uMsgBodySize = 0;
	m_pszMsgBody = NULL;
}

CRecvChatMessage::~CRecvChatMessage()
{
	if(m_pszMsgBody != NULL)
		delete m_pszMsgBody;
}

//class CModifyPassword
CModifyPasswordRequest::CModifyPasswordRequest()
{
	m_uType = NET_DATA_MODIFY_PASSWORD;
	memset(m_szOldPassword, 0, sizeof(m_szOldPassword));
	memset(m_szNewPassword, 0, sizeof(m_szNewPassword));
}

CModifyPasswordRequest::~CModifyPasswordRequest()
{
}

//class CModifyPasswordResult
CModifyPasswordResult::CModifyPasswordResult()
{
	m_uType = NET_DATA_MODIFY_PASSWORD;
	m_nResultCode = 0;
}

CModifyPasswordResult::~CModifyPasswordResult()
{

}

//class CCreateNewGroupRequest
CCreateNewGroupRequest::CCreateNewGroupRequest()
{
	m_uType = NET_DATA_CREATE_NEW_GROUP;
	memset(m_szGroupName, 0, sizeof(m_szGroupName));
}

CCreateNewGroupRequest::~CCreateNewGroupRequest()
{

}

//class CCreateGroupResult
CCreateNewGroupResult::CCreateNewGroupResult()
{
	m_uType = NET_DATA_CREATE_NEW_GROUP;
	m_uError = 0;
	m_uAccountID = 0;
	memset(m_szGroupName, 0, sizeof(m_szGroupName));
	memset(m_szAccount, 0, sizeof(m_szAccount));
}

CCreateNewGroupResult::~CCreateNewGroupResult()
{
}

//class CHeartbeatMessageRequest
CHeartbeatMessageRequest::CHeartbeatMessageRequest()
{
	m_uType = NET_DATA_HEARTBEAT;
}

CHeartbeatMessageRequest::~CHeartbeatMessageRequest()
{
	
}

//class CHeartbeatMessageResult
CHeartbeatMessageResult::CHeartbeatMessageResult()
{
	m_uType = NET_DATA_HEARTBEAT;
}

CHeartbeatMessageResult::~CHeartbeatMessageResult()
{

}

//class CUpdateLogonUserInfoRequest
CUpdateLogonUserInfoRequest::CUpdateLogonUserInfoRequest()
{
	m_uType = NET_DATA_UPDATE_LOGON_USER_INFO;
	memset(m_szNickName, 0, sizeof(m_szNickName));
	memset(m_szSignature, 0, sizeof(m_szSignature));
	m_uGender = 0;
	m_nBirthday = 0;
	memset(m_szAddress, 0, sizeof(m_szAddress));
	memset(m_szPhone, 0, sizeof(m_szPhone));
	memset(m_szMail, 0, sizeof(m_szMail));
	m_uFaceID = 0;
	memset(m_szCustomFace, 0, sizeof(m_szCustomFace));
	m_bUseCustomThumb = FALSE;
}

CUpdateLogonUserInfoRequest::~CUpdateLogonUserInfoRequest()
{
}

//class CUpdateLogonUserInfoResult
CUpdateLogonUserInfoResult::CUpdateLogonUserInfoResult()
{
	m_uType = NET_DATA_UPDATE_LOGON_USER_INFO;
    m_uType = NET_DATA_UPDATE_LOGON_USER_INFO;
    memset(m_szNickName, 0, sizeof(m_szNickName));
    memset(m_szSignature, 0, sizeof(m_szSignature));
    m_uGender = 0;
    m_nBirthday = 0;
    memset(m_szAddress, 0, sizeof(m_szAddress));
    memset(m_szPhone, 0, sizeof(m_szPhone));
    memset(m_szMail, 0, sizeof(m_szMail));
    m_uFaceID = 0;
    memset(m_szCustomFace, 0, sizeof(m_szCustomFace));
    m_bUseCustomThumb = FALSE;
}

CUpdateLogonUserInfoResult::~CUpdateLogonUserInfoResult()
{

}

//class CTargetInfoChangeResult
CTargetInfoChangeResult::CTargetInfoChangeResult()
{
	m_uType = NET_DATA_TARGET_INFO_CHANGE;
	m_uAccountID = 0;
}

CTargetInfoChangeResult::~CTargetInfoChangeResult()
{

}

//class CScreenshot
CScreenshotInfo::CScreenshotInfo() : m_targetId(0)
{

}

CScreenshotInfo::~CScreenshotInfo()
{
    
}

//class CFileItemRequest
CFileItemRequest::CFileItemRequest()
{
	m_uType = NET_DATA_FILE;

	m_nID = 0;
	memset(m_szUtfFilePath, 0, sizeof(m_szUtfFilePath));
	memset(m_szFilePath, 0, sizeof(m_szFilePath));

	m_hwndReflection = NULL;
	m_hCancelEvent = NULL;

	m_uSenderID = 0;

	m_nFileType = FILE_ITEM_UNKNOWN;

	m_pBuddyMsg = NULL;

	m_bPending = TRUE;

	m_uAccountID = 0;
}

CFileItemRequest::~CFileItemRequest()
{
	if(m_hCancelEvent != NULL)
		::CloseHandle(m_hCancelEvent);
}

// class CUploadFileResult
CUploadFileResult::CUploadFileResult()
{
	m_nFileType = FILE_ITEM_UNKNOWN;
	m_bSuccessful = FALSE;
    m_nFileSize = 0;
	memset(m_szLocalName, 0, sizeof(m_szLocalName));

	memset(m_szMd5, 0, sizeof(m_szMd5));
	memset(m_szRemoteName, 0, sizeof(m_szRemoteName));

	m_uSenderID = 0;

	m_hwndReflection = NULL;
}

CUploadFileResult::~CUploadFileResult()
{

}

void CUploadFileResult::Clone(const CUploadFileResult* pSource)
{
	if(pSource == NULL)
		return;

	m_nFileType = pSource->m_nFileType;
	m_bSuccessful = pSource->m_bSuccessful;
	m_nFileSize = pSource->m_nFileSize;

	_tcscpy_s(m_szLocalName, ARRAYSIZE(m_szLocalName), pSource->m_szLocalName);

	strcpy_s(m_szMd5, ARRAYSIZE(m_szMd5), pSource->m_szMd5);
	strcpy_s(m_szRemoteName, ARRAYSIZE(m_szRemoteName), pSource->m_szRemoteName);

	m_uSenderID = pSource->m_uSenderID;
	m_setTargetIDs = pSource->m_setTargetIDs;

	m_hwndReflection = pSource->m_hwndReflection;
}







