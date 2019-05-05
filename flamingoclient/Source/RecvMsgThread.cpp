#include "stdafx.h"
#include "RecvMsgThread.h"
#include <assert.h>
#include "net/IUProtocolData.h"
#include "net/protocolstream.h"
#include "net/Msg.h"
#include "File.h"
#include "EncodingUtil.h"
#include "Path.h"
#include "FlamingoClient.h"
#include "IULog.h"

using namespace balloon;

// "/f["系统表情id"] /c["自定义表情文件名"] /o[字体名称，大小，颜色，加粗，倾斜，下划线]"
tstring FormatContent(std::vector<CContent*>& arrContent)
{
	LPCTSTR lpFontFmt = _T("/o[\"%s,%d,%s,%d,%d,%d\"]");
	LPCTSTR lpFaceFmt = _T("/f[\"%d\"]");
	LPCTSTR lpCFaceFmt = _T("/c[\"%s\"]");
	LPCTSTR lpShakeWindowFmt = _T("/s[\"1\"]");
	//LPCTSTR lpCTextFmt = _T("/t[\"%s\"]");
	LPCTSTR lpFileFmt = _T("/i[\"%s\"]");
	TCHAR cBuf[1024];
	TCHAR cColor[32];
	tstring strMsg, strText;

	for (int i = 0; i < (int)arrContent.size(); i++)
	{
		CContent* lpContent = arrContent[i];
		if (NULL == lpContent)
			continue;

		switch (lpContent->m_nType)
		{
		case CONTENT_TYPE_FONT_INFO:
			{
				memset(cBuf, 0, sizeof(cBuf));
				memset(cColor, 0, sizeof(cColor));

				RGBToHexStr(lpContent->m_FontInfo.m_clrText, cColor, sizeof(cColor)/sizeof(TCHAR));

				wsprintf(cBuf, lpFontFmt, lpContent->m_FontInfo.m_strName.c_str(),
					lpContent->m_FontInfo.m_nSize, cColor,
					lpContent->m_FontInfo.m_bBold, lpContent->m_FontInfo.m_bItalic,
					lpContent->m_FontInfo.m_bUnderLine);
				strMsg += cBuf;
			}
			break;

		case CONTENT_TYPE_TEXT:
			{
				strText = lpContent->m_strText;
				Replace(strText, _T("/"), _T("//"));
				//wsprintf(cBuf, lpCTextFmt, strText.c_str());
				//strMsg += cBuf;
				strMsg += strText;
			}
			break;

		case CONTENT_TYPE_FACE:
			{
				memset(cBuf, 0, sizeof(cBuf));
				wsprintf(cBuf, lpFaceFmt, lpContent->m_nFaceId);
				strMsg += cBuf;
			}
			break;

		case CONTENT_TYPE_CHAT_IMAGE:
			{
				if (!lpContent->m_CFaceInfo.m_strFilePath.empty())
				{
					memset(cBuf, 0, sizeof(cBuf));
					wsprintf(cBuf, lpCFaceFmt, lpContent->m_CFaceInfo.m_strFileName.c_str());
					strMsg += cBuf;
				}
			}
			break;

		case CONTENT_TYPE_SHAKE_WINDOW:
			{
				strMsg += lpShakeWindowFmt;
			}
			break;

		case CONTENT_TYPE_FILE:
			{
				strMsg += _T("发送文件：[");
				memset(cBuf, 0, sizeof(cBuf));
				wsprintf(cBuf, lpFileFmt, lpContent->m_CFaceInfo.m_strFileName.c_str());
				strMsg += cBuf;
				strMsg += _T("]");
			}
			break;
		}
	}

	return strMsg;
}

BOOL CreateMsgLogFile(CMessageLogger& msgLogger, const tstring& strFileName)
{
	tstring strPath = Hootina::CPath::GetDirectoryName(strFileName.c_str());
	if (!Hootina::CPath::IsDirectoryExist(strPath.c_str()))
		Hootina::CPath::CreateDirectory(strPath.c_str(), NULL);

	msgLogger.SetMsgLogFileName(strFileName.c_str());
	return msgLogger.CreateMsgLogFile();
}

// 写入一条好友消息记录
void WriteBuddyMsgLog(CUserMgr* lpUserMgr, UINT nUTalkNum, 
					  LPCTSTR lpNickName, BOOL bSelf, CBuddyMessage* lpMsg)
{
	if (NULL == lpUserMgr || 0 == nUTalkNum || NULL == lpMsg)
		return;

	TCHAR szNickName[32] = {0};
	if (NULL == lpNickName || NULL ==*lpNickName)
	{
		wsprintf(szNickName, _T("%u"), nUTalkNum);
		lpNickName = szNickName;
	}

	tstring strContent = FormatContent(lpMsg->m_arrContent);

	if (!lpUserMgr->m_MsgLogger.IsMsgLogFileValid())
		CreateMsgLogFile(lpUserMgr->m_MsgLogger, lpUserMgr->GetMsgLogFullName());

	lpUserMgr->m_MsgLogger.Lock();
	lpUserMgr->m_MsgLogger.WriteBuddyMsgLog(nUTalkNum, lpNickName, 
		lpMsg->m_nTime, bSelf, strContent.c_str());
	lpUserMgr->m_MsgLogger.UnLock();
}

// 写入一条群消息记录
void WriteGroupMsgLog(CUserMgr* lpUserMgr, UINT nGroupNum, UINT nUTalkNum, 
					  LPCTSTR lpNickName, CBuddyMessage* lpMsg)
{
	if (NULL == lpUserMgr || 0 == nGroupNum || NULL == lpMsg)
		return;

	TCHAR szNickName[32] = {0};
	if (NULL == lpNickName || NULL ==*lpNickName)
	{
		wsprintf(szNickName, _T("%u"), nUTalkNum);
		lpNickName = szNickName;
	}

	tstring strContent = FormatContent(lpMsg->m_arrContent);

	if (!lpUserMgr->m_MsgLogger.IsMsgLogFileValid())
		CreateMsgLogFile(lpUserMgr->m_MsgLogger, lpUserMgr->GetMsgLogFullName());

	lpUserMgr->m_MsgLogger.Lock();
	lpUserMgr->m_MsgLogger.WriteGroupMsgLog(nGroupNum, nUTalkNum, 
		lpNickName, lpMsg->m_nTime, strContent.c_str());
	lpUserMgr->m_MsgLogger.UnLock();
}

// 写入一条临时会话(群成员)消息记录
void WriteSessMsgLog(CUserMgr* lpUserMgr, UINT nUTalkNum, 
					 LPCTSTR lpNickName, BOOL bSelf, CSessMessage* lpMsg)
{
	if (NULL == lpUserMgr || 0 == nUTalkNum || NULL == lpMsg)
		return;

	TCHAR szNickName[32] = {0};
	if (NULL == lpNickName || NULL ==*lpNickName)
	{
		wsprintf(szNickName, _T("%u"), nUTalkNum);
		lpNickName = szNickName;
	}

	tstring strContent = FormatContent(lpMsg->m_arrContent);

	if (!lpUserMgr->m_MsgLogger.IsMsgLogFileValid())
		CreateMsgLogFile(lpUserMgr->m_MsgLogger, lpUserMgr->GetMsgLogFullName());

	lpUserMgr->m_MsgLogger.Lock();
	lpUserMgr->m_MsgLogger.WriteSessMsgLog(nUTalkNum, lpNickName, 
		lpMsg->m_nTime, bSelf, strContent.c_str());
	lpUserMgr->m_MsgLogger.UnLock();
}

CRecvMsg::CRecvMsg(void)
{
	m_lpMsg = NULL;
}

CRecvMsg::~CRecvMsg(void)
{
	if(m_lpMsg != NULL)
		delete m_lpMsg;
}

// class CRecvMsgThread
CRecvMsgThread::CRecvMsgThread() : 
m_bUIEnable(false),
m_seq(0), 
m_hProxyWnd(NULL),
m_bNetError(false)
{
	
}

CRecvMsgThread::~CRecvMsgThread(void)
{
	DelAllMsgData();

}

void CRecvMsgThread::Stop()
{
	m_bStop = true;
	m_cvItems.notify_one();
}

void CRecvMsgThread::Run()
{
    while (!m_bStop)
    {
        std::string strMsg;
        {
            std::unique_lock<std::mutex> guard(m_mtItems);
            while (m_listItems.empty())
            {
                if (m_bStop)
                    return;

                m_cvItems.wait(guard);
            }

            
            strMsg = m_listItems.front();
            m_listItems.pop_front();
            
        }

        HandleMessage(strMsg);
    }
	
}

//void CRecvMsgThread::EnableBuddyListAvailable(BOOL bEnable)
//{
//	//::EnterCriticalSection(&m_csBuddyListAvailable);
//	//m_bBuddyListAvailable = TRUE;
//	//::LeaveCriticalSection(&m_csBuddyListAvailable);
//
//	//CRecvChatMessage* pCurrentChatMessage = NULL;
//
//	////先处理缓存的消息
//	//::EnterCriticalSection(&m_csCacheChatMsg);
//	//while(m_listCacheChatMsg.size() > 0)
//	//{
//	//	pCurrentChatMessage = m_listCacheChatMsg.front();
//	//	m_listCacheChatMsg.pop_front();
//	//	HandleChatMessage(pCurrentChatMessage);
//	//}
//	//
//	//::LeaveCriticalSection(&m_csCacheChatMsg);
//
//}

void CRecvMsgThread::AddMsgData(const std::string& pMsgData)
{
	if (pMsgData.empty())
		return;

    //收到数据就认为网络没有错误
    m_bNetError = false;

    std::lock_guard<std::mutex> guard(m_mtItems);
    m_listItems.push_back(pMsgData);
    m_cvItems.notify_one();
  	
	return;
}

void CRecvMsgThread::NotifyNetError()
{
    //已经有错误了，就不要发送重复消息了
    if (!m_bNetError)
    {
        m_bNetError = true;
        ::PostMessage(m_hProxyWnd, FMG_MSG_NET_ERROR, 0, 0);
    }       
}

void CRecvMsgThread::DelAllMsgData()
{
    std::lock_guard<std::mutex> guard(m_mtItems);
	CNetData* pNetData = NULL;
    m_listItems.clear();
}

//这个函数在主线程里面调用，所以需要利用互斥体保护起来
void CRecvMsgThread::EnableUI(bool bEnable)
{
    std::lock_guard<std::mutex> guard(m_mtUIEnable);
    m_bUIEnable = bEnable;

    //先处理缓存的聊天消息
    for (const auto& iter : m_listCachedChatMsg)
    {
        HandleChatMessage(iter.senderid, iter.targetid, iter.strChatMsg);
    }

    m_listCachedChatMsg.clear();

    for (const auto& iter : m_listCachedNotifyMsg)
    {
        HandleOperateFriendMessage(iter.strNotifyMsg);
    }

    m_listCachedNotifyMsg.clear();
}

BOOL CRecvMsgThread::HandleMessage(const std::string& strMsg)
{
    BinaryReadStream readStream(strMsg.c_str(), strMsg.length());
    int32_t cmd;
    if (!readStream.ReadInt32(cmd))
    {
        return false;
    }

    //int seq;
    if (!readStream.ReadInt32(m_seq))
    {
        return false;
    }

    std::string data;
    size_t datalength;
    if (!readStream.ReadString(&data, 0, datalength))
    {
        return false;
    }

    switch (cmd)
    {
        //心跳包不处理
        case msg_type_heartbeat:
            break;
        //注册
        case msg_type_register:
            HandleRegisterMessage(data);
            break;
        //登录
        case msg_type_login:
            HandleLoginMessage(data);
            break;
        //用户信息
        case msg_type_getofriendlist:
            HandleFriendListInfo(data);
            break;
        //获取群成员信息
        case msg_type_getgroupmembers:
            HandleGroupBasicInfo(data);
            break;
        //查找用户结果
        case msg_type_finduser:
            HandleFindFriendMessage(data);
            break;
        //加好友申请或删除好友或加群或退群
        case msg_type_operatefriend:
            CacheNotifyMsg(data);
            break;
        //用户信息发生变更
        case msg_type_userstatuschange:
        {
            int32_t targetId;
            //int seq;
            if (!readStream.ReadInt32(targetId))
            {
                break;
            }
            HandleUserStatusNotifyMessage(targetId, data);
        }
    
		    break;
        
        //更新个人信息
        case msg_type_updateuserinfo:
            HandleUpdateLogonUserInfoMessage(data);
            break;

        //修改密码
        case msg_type_modifypassword:
            HandleModifyPasswordResult(data);
            break;

        //创建群
        case msg_type_creategroup:
            HandleCreateNewGroupResult(data);
            break;

            //聊天消息
        case msg_type_chat:
        {
            int32_t senderId;
            //int seq;
            if (!readStream.ReadInt32(senderId))
            {
                break;
            }

            int32_t targetid;
            if (!readStream.ReadInt32(targetid))
            {
                break;
            }

            CacheChatMessage(senderId, targetid, data);
        }
            break;

            //被踢下线
        case msg_type_kickuser:
            ::PostMessage(m_hProxyWnd, FMG_MSG_SELF_STATUS_CHANGE, 0, 0);
            break;
            
            //截屏数据
        case msg_type_remotedesktop:
        {
            string bmpHeader;
            size_t bmpHeaderlength;
            if (!readStream.ReadString(&bmpHeader, 0, bmpHeaderlength))
                break;

            string bmpData;
            size_t bmpDatalength;
            if (!readStream.ReadString(&bmpData, 0, bmpDatalength))
                break;

            int32_t target;
            if (!readStream.ReadInt32(target))
                break;

            HandleScreenshotMessage(target, bmpHeader, bmpData);
         }
            break;


	    case NET_DATA_TARGET_INFO_CHANGE:
		    //HandleTargetInfoChangeMessage((CTargetInfoChangeResult*)pNetData);
		    break;

	    

	    

	    default:
		    break;

	}

	return TRUE;
}

BOOL CRecvMsgThread::HandleRegisterMessage(const std::string& strMsg)
{
    //{"code": 0, "msg": "ok", "userid": 8}
    Json::Reader JsonReader;
    Json::Value JsonRoot;
    if (!JsonReader.parse(strMsg, JsonRoot))
    {
        return FALSE;
    }

    if (JsonRoot["code"].isNull())
        return FALSE;

    int nRetCode = JsonRoot["code"].asInt();

    CRegisterResult* pRegisterResult = new CRegisterResult();
    if (nRetCode == 0)
        pRegisterResult->m_nResultCode = REGISTER_SUCCESS;
    else if (nRetCode == 101)
        pRegisterResult->m_nResultCode = REGISTER_EXIST;
    else 
        pRegisterResult->m_nResultCode = REGISTER_FAILED;

	::PostMessage(m_hProxyWnd, FMG_MSG_REGISTER, 0, (LPARAM)pRegisterResult);	
	return TRUE;
}

BOOL CRecvMsgThread::HandleLoginMessage(const std::string& strMsg)
{
    //{"code": 0, "msg": "ok", "userid": 8}
    Json::Reader JsonReader;
    Json::Value JsonRoot;
    if (!JsonReader.parse(strMsg, JsonRoot))
    {
        return FALSE;
    }

    if (JsonRoot["code"].isNull())
        return FALSE;

    int nRetCode = JsonRoot["code"].asInt();

    CLoginResult* pLoginResult = new CLoginResult();
    if (nRetCode == 0)
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
        strcpy_s(pLoginResult->m_szCustomFace, ARRAYSIZE(pLoginResult->m_szCustomFace), JsonRoot["customface"].asCString());
        strcpy_s(pLoginResult->m_szAddress, ARRAYSIZE(pLoginResult->m_szAddress), JsonRoot["address"].asCString());
        strcpy_s(pLoginResult->m_szCustomFace, ARRAYSIZE(pLoginResult->m_szCustomFace), JsonRoot["customface"].asCString());
        strcpy_s(pLoginResult->m_szPhoneNumber, ARRAYSIZE(pLoginResult->m_szPhoneNumber), JsonRoot["phonenumber"].asCString());
        strcpy_s(pLoginResult->m_szMail, ARRAYSIZE(pLoginResult->m_szMail), JsonRoot["mail"].asCString());
        
    }
    else if (nRetCode == 102)
        pLoginResult->m_LoginResultCode = LOGIN_UNREGISTERED;
    else if (nRetCode == 103)
        pLoginResult->m_LoginResultCode = LOGIN_PASSWORD_ERROR;
    else
        pLoginResult->m_LoginResultCode = LOGIN_FAILED;
    //m_lpUserMgr为野指针
    ::PostMessage(m_hProxyWnd, FMG_MSG_LOGIN_RESULT, 0, (LPARAM)pLoginResult);
	
    return TRUE;
}

BOOL CRecvMsgThread::HandleFriendListInfo(const std::string& strMsg)
{
    /*
    {
    "code": 0,
    "msg": "ok",
    "userinfo": [
        {
            "teamindex": 0,
            "teamname": "My Friends",
            "members": [
                {
                    "userid": 4,
                    "username": "13811411052",
                    "nickname": "bj_man",
                    "facetype": 2,
                    "customface": "",
                    "gender": 0,
                    "birthday": 19900101,
                    "signature": "",
                    "address": "",
                    "phonenumber": "",
                    "mail": "",
                    "clienttype": 0,
                    "status": 0
                },
                {
                    "userid": 5,
                    "username": "15618326596",
                    "nickname": "Half",
                    "facetype": 0,
                    "customface": "",
                    "gender": 0,
                    "birthday": 19900101,
                    "signature": "",
                    "address": "",
                    "phonenumber": "",
                    "mail": "",
                    "clienttype": 0,
                    "status": 0
                }
            ]
        }
    ]
}
    */
    
    Json::Reader JsonReader;
	Json::Value JsonRoot;
    if (!JsonReader.parse(strMsg, JsonRoot))
	{
		return FALSE;
	}

	if (JsonRoot["code"].isNull() || JsonRoot["code"].asInt() != 0 || !JsonRoot["userinfo"].isArray())
		return FALSE;

	LOG_INFO("Recv user basic info, info count=%u:", JsonRoot["userinfo"].size());

	CUserBasicInfoResult* pUserBasicInfoResult = new CUserBasicInfoResult();
	UserBasicInfo* pUserBasicInfo = NULL;
    UINT nTeamCount = (UINT)JsonRoot["userinfo"].size();

    std::string strTeamName;
    for (UINT i = 0; i < nTeamCount; ++i)
	{
        strTeamName = JsonRoot["userinfo"][(UINT)i]["teamname"].asString();

        std::list<UserBasicInfo*> friendList;        

        if (!JsonRoot["userinfo"][(UINT)i]["members"].isArray())
            continue;

        for (UINT j = 0; j < JsonRoot["userinfo"][(UINT)i]["members"].size(); ++j)
        {
            pUserBasicInfo = new UserBasicInfo();
            //memset(pUserBasicInfo, 0, sizeof(UserBasicInfo));

            pUserBasicInfo->uAccountID = JsonRoot["userinfo"][(UINT)i]["members"][(UINT)j]["userid"].asUInt();

#ifdef _DEBUG
            //为了调试方便加上userid
            sprintf_s(pUserBasicInfo->szAccountName, 32, "%s - %d", JsonRoot["userinfo"][(UINT)i]["members"][(UINT)j]["username"].asString().c_str(), pUserBasicInfo->uAccountID);
#else
            sprintf_s(pUserBasicInfo->szAccountName, 32, "%s", JsonRoot["userinfo"][(UINT)i]["members"][(UINT)j]["username"].asString().c_str(), pUserBasicInfo->uAccountID);
#endif
            //账户名
            //strcpy_s(pUserBasicInfo->szAccountName, ARRAYSIZE(pUserBasicInfo->szAccountName), pUserInfo[i].user);
            //昵称
            strcpy_s(pUserBasicInfo->szNickName, ARRAYSIZE(pUserBasicInfo->szNickName), JsonRoot["userinfo"][(UINT)i]["members"][(UINT)j]["nickname"].asString().c_str());
            //备注名
            strcpy_s(pUserBasicInfo->szMarkName, ARRAYSIZE(pUserBasicInfo->szMarkName), JsonRoot["userinfo"][(UINT)i]["members"][(UINT)j]["markname"].asString().c_str());
            //签名
            strcpy_s(pUserBasicInfo->szSignature, ARRAYSIZE(pUserBasicInfo->szSignature), JsonRoot["userinfo"][(UINT)i]["members"][(UINT)j]["signature"].asString().c_str());
            //地址
            strcpy_s(pUserBasicInfo->szAddress, ARRAYSIZE(pUserBasicInfo->szAddress), JsonRoot["userinfo"][(UINT)i]["members"][(UINT)j]["address"].asString().c_str());
            //自定义头像
            strcpy_s(pUserBasicInfo->customFace, ARRAYSIZE(pUserBasicInfo->customFace), JsonRoot["userinfo"][(UINT)i]["members"][(UINT)j]["customface"].asString().c_str());
            //电话
            strcpy_s(pUserBasicInfo->szPhoneNumber, ARRAYSIZE(pUserBasicInfo->szPhoneNumber), JsonRoot["userinfo"][(UINT)i]["members"][(UINT)j]["phonenumber"].asString().c_str());
            //邮箱
            strcpy_s(pUserBasicInfo->szMail, ARRAYSIZE(pUserBasicInfo->szMail), JsonRoot["userinfo"][(UINT)i]["members"][(UINT)j]["mail"].asString().c_str());

            pUserBasicInfo->nStatus = JsonRoot["userinfo"][(UINT)i]["members"][(UINT)j]["status"].asInt();
            pUserBasicInfo->clientType = JsonRoot["userinfo"][(UINT)i]["members"][(UINT)j]["clienttype"].asInt();
            //头像ID
            pUserBasicInfo->uFaceID = JsonRoot["userinfo"][(UINT)i]["members"][(UINT)j]["facetype"].asInt();
            //性别
            pUserBasicInfo->nGender = JsonRoot["userinfo"][(UINT)i]["members"][(UINT)j]["gender"].asInt();
            //生日
            pUserBasicInfo->nBirthday = JsonRoot["userinfo"][(UINT)i]["members"][(UINT)j]["birthday"].asInt();

            friendList.push_back(pUserBasicInfo);
        }

        pUserBasicInfoResult->m_mapUserBasicInfo[strTeamName] = friendList;
	}

    ::PostMessage(m_hProxyWnd, FMG_MSG_UPDATE_USER_BASIC_INFO, 0, (LPARAM)pUserBasicInfoResult);
	
	return TRUE;
}

BOOL CRecvMsgThread::HandleGroupBasicInfo(const std::string& strMsg)
{
    Json::Reader JsonReader;
    Json::Value JsonRoot;
    if (!JsonReader.parse(strMsg, JsonRoot))
    {
        return FALSE;
    }

    if (JsonRoot["code"].isNull() || JsonRoot["code"].asInt() != 0 || !JsonRoot["groupid"].isInt() || !JsonRoot["members"].isArray())
        return FALSE;

    LOG_INFO("Recv group member info, groupid=%d, info count=%u:", JsonRoot["groupid"].asInt(), JsonRoot["members"].size());

    CGroupBasicInfoResult* pGroupBasicInfoResult = new CGroupBasicInfoResult();
    pGroupBasicInfoResult->m_groupid = JsonRoot["groupid"].asInt();
    UserBasicInfo* pUserBasicInfo = NULL;
    for (UINT i = 0; i < JsonRoot["members"].size(); ++i)
    {
        pUserBasicInfo = new UserBasicInfo();
        //memset(pUserBasicInfo, 0, sizeof(UserBasicInfo));

        pUserBasicInfo->uAccountID = JsonRoot["members"][(UINT)i]["userid"].asUInt();

        //FIXME: 为了调试方便加上userid
        sprintf_s(pUserBasicInfo->szAccountName, 32, "%s - %d", JsonRoot["members"][(UINT)i]["username"].asString().c_str(), pUserBasicInfo->uAccountID);
        //账户名
        //strcpy_s(pUserBasicInfo->szAccountName, ARRAYSIZE(pUserBasicInfo->szAccountName), pUserInfo[i].user);
        //昵称
        strcpy_s(pUserBasicInfo->szNickName, ARRAYSIZE(pUserBasicInfo->szNickName), JsonRoot["members"][(UINT)i]["nickname"].asString().c_str());
        //签名
        strcpy_s(pUserBasicInfo->szSignature, ARRAYSIZE(pUserBasicInfo->szSignature), JsonRoot["members"][(UINT)i]["signature"].asString().c_str());
        //地址
        strcpy_s(pUserBasicInfo->szAddress, ARRAYSIZE(pUserBasicInfo->szAddress), JsonRoot["members"][(UINT)i]["address"].asString().c_str());
        //自定义头像
        strcpy_s(pUserBasicInfo->customFace, ARRAYSIZE(pUserBasicInfo->customFace), JsonRoot["members"][(UINT)i]["customface"].asString().c_str());
        //电话
        strcpy_s(pUserBasicInfo->szPhoneNumber, ARRAYSIZE(pUserBasicInfo->szPhoneNumber), JsonRoot["members"][(UINT)i]["phonenumber"].asString().c_str());
        //邮箱
        strcpy_s(pUserBasicInfo->szMail, ARRAYSIZE(pUserBasicInfo->szMail), JsonRoot["members"][(UINT)i]["mail"].asString().c_str());

        pUserBasicInfo->nStatus = JsonRoot["members"][(UINT)i]["status"].asInt();
        pUserBasicInfo->clientType = JsonRoot["members"][(UINT)i]["clienttype"].asInt();
        //头像ID
        pUserBasicInfo->uFaceID = JsonRoot["members"][(UINT)i]["facetype"].asInt();
        //性别
        pUserBasicInfo->nGender = JsonRoot["members"][(UINT)i]["gender"].asInt();
        //生日
        pUserBasicInfo->nBirthday = JsonRoot["members"][(UINT)i]["birthday"].asInt();

        pGroupBasicInfoResult->m_listUserBasicInfo.push_back(pUserBasicInfo);

    }

    ::PostMessage(m_hProxyWnd, FMG_MSG_UPDATE_GROUP_BASIC_INFO, 0, (LPARAM)pGroupBasicInfoResult);

    return TRUE;
}



BOOL CRecvMsgThread::HandleFindFriendMessage(const std::string& strMsg)
{
    Json::Reader JsonReader;
    Json::Value JsonRoot;
    if (!JsonReader.parse(strMsg, JsonRoot))
    {
        return FALSE;
    }

    if (JsonRoot["code"].isNull() || JsonRoot["code"].asInt() != 0 || !JsonRoot["userinfo"].isArray())
        return FALSE;

    CFindFriendResult* pFindFriendResult = new CFindFriendResult();
    if (JsonRoot["userinfo"].size() == 0)
        pFindFriendResult->m_nResultCode = FIND_FRIEND_NOT_FOUND;
    else
    {
        pFindFriendResult->m_nResultCode = FIND_FRIEND_FOUND;
        pFindFriendResult->m_uAccountID = JsonRoot["userinfo"][(UINT)0]["userid"].asInt();
        strcpy_s(pFindFriendResult->m_szAccountName, ARRAYSIZE(pFindFriendResult->m_szAccountName), JsonRoot["userinfo"][(UINT)0]["username"].asCString());
        strcpy_s(pFindFriendResult->m_szNickName, ARRAYSIZE(pFindFriendResult->m_szNickName), JsonRoot["userinfo"][(UINT)0]["nickname"].asCString());
    }


    ::PostMessage(m_hProxyWnd, FMG_MSG_FINDFREIND, 0, (LPARAM)pFindFriendResult);
    return TRUE;
}

BOOL CRecvMsgThread::HandleUserStatusNotifyMessage(int targetId, const std::string& strMsg)
{
    Json::Reader JsonReader;
    Json::Value JsonRoot;
    if (!JsonReader.parse(strMsg, JsonRoot))
    {
        return FALSE;
    }

    if (JsonRoot["type"].isNull())
        return FALSE;

    int type = JsonRoot["type"].asInt();
    CFriendStatus* pFriendStatus = new CFriendStatus();
    pFriendStatus->m_uAccountID = targetId;
    //上线 { "type": 1, "onlinestatus": 1/2/3/4.., "clienttype": 0/1/2}
    if (type == 1)
    {
        pFriendStatus->m_type = 1;
        
        if (!JsonRoot["onlinestatus"].isNull() && JsonRoot["onlinestatus"].isInt())
            pFriendStatus->m_nStatus = JsonRoot["onlinestatus"].asInt();
        else
            pFriendStatus->m_nStatus = 1;       

        if (!JsonRoot["clienttype"].isNull() && JsonRoot["clienttype"].isInt())
            pFriendStatus->m_nClientType = JsonRoot["clienttype"].asInt();
        else
            pFriendStatus->m_nClientType = 0;

    }
    //下线
    else if (type == 2)
    {
        pFriendStatus->m_nStatus = 0;
        pFriendStatus->m_type = 2;
    }
    //个人昵称、头像、签名等信息更改
    else if (type == 3)
    {
        pFriendStatus->m_type = 3;
    }
    
    
	::PostMessage(m_hProxyWnd, FMG_MSG_USER_STATUS_CHANGE, 0, (LPARAM)pFriendStatus);

	return TRUE;
}

BOOL CRecvMsgThread::HandleOperateFriendMessage(const std::string& strMsg)
{
    //{"userid": 9, "type": 2, "username": "xxx"}
    Json::Reader JsonReader;
    Json::Value JsonRoot;
    if (!JsonReader.parse(strMsg, JsonRoot))
    {
        return FALSE;
    }

    if (!JsonRoot["userid"].isInt() || !JsonRoot["type"].isInt() != 0 || !JsonRoot["username"].isString())
        return FALSE;

    LOG_INFO("Recv operate friend request");
    int userid = JsonRoot["userid"].asInt();
    int type = JsonRoot["type"].asInt();
    string username = JsonRoot["username"].asString();
    //收到加好友请求
    if (type == 2)
    {
        COperateFriendResult* pOperateFriendResult = new COperateFriendResult();
        pOperateFriendResult->m_uCmd = Apply;
        pOperateFriendResult->m_uAccountID = userid;
        strcpy_s(pOperateFriendResult->m_szAccountName, ARRAYSIZE(pOperateFriendResult->m_szAccountName), username.c_str());

        ::PostMessage(m_hProxyWnd, FMG_MSG_RECVADDFRIENDREQUSET, 0, (LPARAM)pOperateFriendResult);
        return TRUE;
    }
    //收到加好友结果
    else if (type == 3)
    {
        if (!JsonRoot["accept"].isInt())
            return FALSE;

        int accept = JsonRoot["accept"].asInt();
        
        COperateFriendResult* pOperateFriendResult = new COperateFriendResult();
        pOperateFriendResult->m_uCmd = (accept != 0 ? Agree : Refuse);
        pOperateFriendResult->m_uAccountID = userid;
        strcpy_s(pOperateFriendResult->m_szAccountName, ARRAYSIZE(pOperateFriendResult->m_szAccountName), username.c_str());

        ::PostMessage(m_hProxyWnd, FMG_MSG_RECVADDFRIENDREQUSET, 0, (LPARAM)pOperateFriendResult);
        return TRUE;
    }
    //删除好友或者退群
    else if (type == 5)
	{
        COperateFriendResult* pOperateFriendResult = new COperateFriendResult();
        pOperateFriendResult->m_uCmd = Delete;
        pOperateFriendResult->m_uAccountID = userid;
        strcpy_s(pOperateFriendResult->m_szAccountName, ARRAYSIZE(pOperateFriendResult->m_szAccountName), username.c_str());
        ::PostMessage(m_lpUserMgr->m_hProxyWnd, FMG_MSG_DELETEFRIEND, DELETE_FRIEND_SUCCESS, pOperateFriendResult->m_uAccountID);
		return TRUE;
	}
	//else if(pOperateFriendResult->m_uCmd==Agree || pOperateFriendResult->m_uCmd==Refuse || pOperateFriendResult->m_uCmd==Apply)
	//{
	//	::PostMessage(m_lpUserMgr->m_hProxyWnd,  FMG_MSG_RECVADDFRIENDREQUSET, 0, (LPARAM)pOperateFriendResult);
	//	return TRUE;
	//}

	return FALSE;
}

BOOL CRecvMsgThread::HandleChatMessage(int32_t senderId, int32_t targetId, const std::string& strMsg)
{         
    std::vector<CRecvMsg*> arrMsg;
    BOOL bRet = ParseChatMessage(senderId, targetId, strMsg, arrMsg);
    if (!bRet || arrMsg.empty())
    {
        return FALSE;
    }

    size_t nSize = arrMsg.size();
    CRecvMsg* lpMsg = NULL;
    for (size_t i = 0; i < nSize; ++i)
    {
        lpMsg = arrMsg[i];
        if (NULL == lpMsg)
            continue;
        HandleBuddyMsg(lpMsg);
    }

    return TRUE;
    
}

BOOL CRecvMsgThread::HandleUpdateLogonUserInfoMessage(const std::string& strMsg)
{
    Json::Reader JsonReader;
    Json::Value JsonRoot;
    if (!JsonReader.parse(strMsg, JsonRoot))
        return FALSE;

    if (!JsonRoot["code"].isInt() || JsonRoot["code"].asInt() != 0)
        return FALSE;


    CUpdateLogonUserInfoResult* pResult = new CUpdateLogonUserInfoResult();
    strcpy_s(pResult->m_szNickName, ARRAYSIZE(pResult->m_szNickName), JsonRoot["nickname"].asCString());
    strcpy_s(pResult->m_szSignature, ARRAYSIZE(pResult->m_szSignature), JsonRoot["signature"].asCString());
    strcpy_s(pResult->m_szAddress, ARRAYSIZE(pResult->m_szAddress), JsonRoot["address"].asCString());
    strcpy_s(pResult->m_szPhone, ARRAYSIZE(pResult->m_szPhone), JsonRoot["phonenumber"].asCString());
    strcpy_s(pResult->m_szMail, ARRAYSIZE(pResult->m_szMail), JsonRoot["mail"].asCString());
    strcpy_s(pResult->m_szCustomFace, ARRAYSIZE(pResult->m_szCustomFace), JsonRoot["customface"].asCString());

    pResult->m_uGender = JsonRoot["gender"].asInt();
    pResult->m_nBirthday = JsonRoot["birthday"].asInt();
    pResult->m_uFaceID = JsonRoot["facetype"].asInt();

    //std::string strCustomFace = JsonRoot["customface"].asString();
    //if (!strCustomFace.empty())
    //{

    //}

	::PostMessage(m_lpUserMgr->m_hProxyWnd, FMG_MSG_MODIFY_USER_INFO, 0, (LPARAM)pResult);
	return TRUE;
}


BOOL CRecvMsgThread::HandleModifyPasswordResult(const std::string& strMsg)
{
    Json::Reader JsonReader;
    Json::Value JsonRoot;
    if (!JsonReader.parse(strMsg, JsonRoot))
        return FALSE;

    if (!JsonRoot["code"].isInt())
        return FALSE;

    int code = JsonRoot["code"].asInt();

    ::PostMessage(m_lpUserMgr->m_hProxyWnd, FMG_MSG_MODIFY_PASSWORD_RESULT, (WPARAM)code, 0);

    return TRUE;
}

BOOL CRecvMsgThread::HandleTargetInfoChangeMessage(CTargetInfoChangeResult* pResult)
{
	if(pResult == NULL)
		return FALSE;

	::PostMessage(m_lpUserMgr->m_hProxyWnd, FMG_MSG_TARGET_INFO_CHANGE, 0, (LPARAM)pResult);
	
	return TRUE;
}

BOOL CRecvMsgThread::HandleCreateNewGroupResult(const std::string& strMsg)
{
    //{"code":0, "msg": "ok", "groupid": 12345678, "groupname": "我的群名称"}
    Json::Reader JsonReader;
    Json::Value JsonRoot;
    if (!JsonReader.parse(strMsg, JsonRoot))
        return FALSE;

    if (!JsonRoot["code"].isInt() || !JsonRoot["groupid"].isInt() || !JsonRoot["groupname"].isString())
        return FALSE;

    CCreateNewGroupResult* pResult = new CCreateNewGroupResult();
    pResult->m_uAccountID = JsonRoot["groupid"].asInt();
    strcpy_s(pResult->m_szGroupName, ARRAYSIZE(pResult->m_szGroupName), JsonRoot["groupname"].asCString());

   
    //发给主线程
    ::PostMessage(m_lpUserMgr->m_hProxyWnd, FMG_MSG_CREATE_NEW_GROUP_RESULT, 0, (LPARAM)pResult);

    return TRUE;
}

BOOL CRecvMsgThread::HandleScreenshotMessage(int32_t targetId, const std::string& strBmpHeader, const std::string& strBmpData)
{
    CScreenshotInfo* pSscreenshotInfo = new CScreenshotInfo();
    pSscreenshotInfo->m_targetId = targetId;
    pSscreenshotInfo->m_strBmpHeader = strBmpHeader;
    pSscreenshotInfo->m_strBmpData = strBmpData;

    //发给主线程
    ::PostMessage(m_lpUserMgr->m_hProxyWnd, FMG_MSG_SCREENSHOT, 0, (LPARAM)pSscreenshotInfo);
    return TRUE;
}

BOOL CRecvMsgThread::CacheChatMessage(int32_t senderId, int32_t targetId, const std::string& strMsg)
{   
    std::lock_guard<std::mutex> guard(m_mtUIEnable);    
    CachedChatMsg c;
    c.senderid = senderId;
    c.targetid = targetId;
    c.strChatMsg = strMsg;
    m_listCachedChatMsg.push_back(c);

    if (!m_bUIEnable)
    {
        return FALSE;
    }
    
    for (const auto& iter : m_listCachedChatMsg)
    {
        HandleChatMessage(iter.senderid, iter.targetid, iter.strChatMsg);
    }

    m_listCachedChatMsg.clear();
	
	return TRUE;	
}

BOOL CRecvMsgThread::CacheNotifyMsg(const std::string& strMsg)
{
    std::lock_guard<std::mutex> guard(m_mtUIEnable);
    CachedNotifyMsg c;
    c.strNotifyMsg = strMsg;
    m_listCachedNotifyMsg.push_back(c);

    if (!m_bUIEnable)
    {
        return FALSE;
    }

    for (const auto& iter : m_listCachedNotifyMsg)
    {
        HandleOperateFriendMessage(iter.strNotifyMsg);
    }

    m_listCachedNotifyMsg.clear();

    return TRUE;
}

BOOL CRecvMsgThread::ParseChatMessage(int32_t senderId, int32_t targetId, const std::string& strMsg, std::vector<CRecvMsg*>& arrMsg)
{
    const char* lpData = strMsg.data();
    UINT dwSize = strMsg.length();
	if (NULL==lpData || dwSize==0)
		return FALSE;

	Json::Reader JsonReader;
	Json::Value JsonRoot;
	std::string strText(lpData);

	if (!JsonReader.parse(strText, JsonRoot))
		return FALSE;

	if(!JsonRoot["content"].isArray())
		return FALSE;

	CRecvMsg* lpMsg = new CRecvMsg;
    BOOL bRet = ParseChatMessage(senderId, targetId, strMsg, JsonRoot,*lpMsg);
	if (!bRet)
	{
		delete lpMsg;
        LOG_ERROR("Unknown message, invalid json: %s", strMsg.c_str());
        ::OutputDebugStringA("Unknown message：");
		::OutputDebugStringA(strText.c_str());
		::OutputDebugStringA("\r\n");
	}
	else
	{
		arrMsg.push_back(lpMsg);
	}
	
	return TRUE;
}

BOOL CRecvMsgThread::ParseChatMessage(int32_t senderId, int32_t targetId, const std::string& strRawMsg, Json::Value& JsonValue, CRecvMsg& msg)
{
	if(JsonValue.isNull())
		return FALSE;
	
	CBuddyMessage* lpBuddyMsg = new CBuddyMessage();
	BOOL bRet = lpBuddyMsg->Parse(JsonValue);
	if (!bRet)
	{
		delete lpBuddyMsg;
		return FALSE;
	}

	lpBuddyMsg->m_nMsgId = 0;
	lpBuddyMsg->m_nMsgId2 = 0;
    lpBuddyMsg->m_nFromUin = senderId;
    lpBuddyMsg->m_nToUin = targetId;
	msg.m_lpMsg = lpBuddyMsg;
	

	return TRUE;
}

BOOL CRecvMsgThread::HandleBuddyMsg(CRecvMsg* lpRecvMsg)
{
	if (NULL==lpRecvMsg || NULL==lpRecvMsg->m_lpMsg)
		return FALSE;

	CBuddyMessage* lpMsg = (CBuddyMessage*)lpRecvMsg->m_lpMsg;

	UINT nSenderID = lpMsg->m_nFromUin;
	UINT nTargetID = lpMsg->m_nToUin;
	
	tstring strSenderNickName(m_lpUserMgr->GetNickName(nSenderID));
	tstring strTargetNickName(m_lpUserMgr->GetNickName(nTargetID));
    //TODO: 临时会话会发送者昵称会为空，后面加上临时会话的功能会加上
    if (strSenderNickName.empty() || strTargetNickName.empty())
    {
        LOG_ERROR(_T("Recv a chat msg, but sender or target nickname is empty, senderID: %u, targetID: %u, senderName: %s, targetName: %s"), nSenderID, nTargetID, strSenderNickName.c_str(), strTargetNickName.c_str());
        return FALSE;
    }

	BOOL bGroup = IsGroupTarget(nTargetID);
	
	//单聊消息
	if(!bGroup)
	{
		//如果目标ID是自己，则是正常的好友发来的消息
		if(m_lpUserMgr->m_UserInfo.m_uUserID == nTargetID)
		{
			//消息类型
			if(lpMsg->m_nMsgType != CONTENT_TYPE_IMAGE_CONFIRM)
			{
				//TODO: 为什么要写两条？直接写一条好友的消息记录不行吗？
				WriteBuddyMsgLog(m_lpUserMgr, nSenderID, strSenderNickName.c_str(), FALSE, lpMsg);	// 写入好友发送来的消息记录
				//WriteBuddyMsgLog(m_lpUserMgr, nTargetID, strTargetNickName.c_str(), FALSE, lpMsg);	// 写入消息记录
			}
		}
		//如果目标ID不是自己，发送人ID是自己则是其它平台同步的聊天消息
		else if(nSenderID == m_lpUserMgr->m_UserInfo.m_uUserID)
		{
			if(lpMsg->m_nMsgType != CONTENT_TYPE_IMAGE_CONFIRM)
			{
				WriteBuddyMsgLog(m_lpUserMgr, nTargetID, strSenderNickName.c_str(), TRUE, lpMsg);
				//WriteBuddyMsgLog(m_lpUserMgr, nSenderID, strSenderNickName.c_str(), FALSE, lpMsg);		// 写入消息记录
				//WriteBuddyMsgLog(m_lpUserMgr, nTargetID, strTargetNickName.c_str(), TRUE, lpMsg);		// 写入消息记录
			}
		}
	}
	else
	{
		//发送人不是自己，目标也不是自己，则是群消息
		if(lpMsg->m_nMsgType != CONTENT_TYPE_IMAGE_CONFIRM)
		{
			WriteGroupMsgLog(m_lpUserMgr, nTargetID, nSenderID, strSenderNickName.c_str(), lpMsg);
		}	
	}
	
	//TODO: 这个函数的实际作用是将消息写入最近联系人列表中，函数名要改！！
	ProcessBuddyMsg(lpMsg);
	
	// 需要下载图片
	if (IsNeedDownloadPic(lpMsg))
	{
		GetChatPic(lpMsg);
		return TRUE;
	}

	if(!bGroup)
	{
		//其它平台同步的消息
		if(m_lpUserMgr->m_UserInfo.m_uUserID == nSenderID)
		{
			::PostMessage(m_lpUserMgr->m_hProxyWnd, FMG_MSG_BUDDY_MSG, 0, (LPARAM)lpMsg);
		}	
		else
		{
			//正常好友发来的消息
			if(nTargetID == m_lpUserMgr->m_UserInfo.m_uUserID)
				::PostMessage(m_hProxyWnd, FMG_MSG_BUDDY_MSG, 0, (LPARAM)lpMsg);
			else
			{
				lpMsg->m_nFromUin = nTargetID;
				::PostMessage(m_lpUserMgr->m_hProxyWnd, FMG_MSG_BUDDY_MSG, 0, (LPARAM)lpMsg);
			}
		}
	}
	else
	{
		//非其他平台同步的消息，提示用户
		if(nSenderID != m_lpUserMgr->m_UserInfo.m_uUserID)
			::PostMessage(m_lpUserMgr->m_hProxyWnd, FMG_MSG_GROUP_MSG, 0, (LPARAM)lpMsg);
	}

	return TRUE;
}

BOOL CRecvMsgThread::HandleGroupMsg(CRecvMsg* lpRecvMsg)
{
	if (NULL == lpRecvMsg || NULL == lpRecvMsg->m_lpMsg)
		return FALSE;

	CBuddyMessage* lpMsg = (CBuddyMessage*)lpRecvMsg->m_lpMsg;
	
	//写入群消息记录
	UINT nGroupCode = lpMsg->m_nToUin;
	UINT nSenderID = lpMsg->m_nFromUin;
	tstring strNickName = m_lpUserMgr->GetNickName(nSenderID);
	
	if(lpMsg->m_nMsgType != CONTENT_TYPE_IMAGE_CONFIRM)
	{
		WriteGroupMsgLog(m_lpUserMgr, nGroupCode, nSenderID, strNickName.c_str(), lpMsg);
	}
	
	//TODO: 这个函数的实际作用是将消息写入最近联系人列表中，函数名要改！！
	ProcessBuddyMsg(lpMsg);

	::PostMessage(m_lpUserMgr->m_hProxyWnd, FMG_MSG_GROUP_MSG, 0, (LPARAM)lpMsg);

	return TRUE;
}

BOOL CRecvMsgThread::HandleSessMsg(CRecvMsg* lpRecvMsg)
{
	if (NULL == lpRecvMsg || NULL == lpRecvMsg->m_lpMsg)
		return FALSE;

	CSessMessage* lpMsg = (CSessMessage*)lpRecvMsg->m_lpMsg;

	UINT nUTalkNum = 0;
	tstring strNickName;

	UINT nGroupCode = GroupId2Code(lpMsg->m_nGroupId);				// 群标识转换到群代码
	if (nGroupCode != 0)
	{
		RMT_GROUP_DATA* lpGroupData = GetGroupData(nGroupCode);	// 确保群信息已获取

		RMT_BUDDY_DATA* lpBuddyData = GetGMemberData(nGroupCode, lpMsg->m_nFromUin);
		if (lpBuddyData != NULL)
		{
			nUTalkNum = lpBuddyData->nUTalkNum;
			strNickName = lpBuddyData->szNickName;
		}
	}

	WriteSessMsgLog(m_lpUserMgr, nUTalkNum, strNickName.c_str(), FALSE, lpMsg);		// 写入消息记录

	//if (IsNeedDownloadPic(lpMsg->m_arrContent))	// 需要下载图片
	//	StartGetChatPicTask(OP_TYPE_SESS_PIC, lpMsg);
	//else	
		::PostMessage(m_lpUserMgr->m_hProxyWnd, FMG_MSG_SESS_MSG, 0, (LPARAM)lpMsg);

	return TRUE;
}

RMT_BUDDY_DATA* CRecvMsgThread::GetBuddyData(UINT nUTalkUin)
{
	std::map<UINT,RMT_BUDDY_DATA*>::iterator iter;
	std::pair<std::map<UINT, RMT_BUDDY_DATA*>::iterator, bool> pairInsert;
	RMT_BUDDY_DATA* lpBuddyData = NULL;
	BOOL bRet = FALSE;
	
	iter = m_mapBuddyData.find(nUTalkUin);
	if (iter != m_mapBuddyData.end())
	{
		lpBuddyData = iter->second;
		lpBuddyData->nUTalkNum = nUTalkUin;
		if (lpBuddyData != NULL)
		{
			if (lpBuddyData->nUTalkNum != 0 && 
				lpBuddyData->szNickName[0] != _T('\0'))
				return lpBuddyData;
		}
		else
			m_mapBuddyData.erase(iter);
	}

	if (NULL == lpBuddyData)
	{
		lpBuddyData = new RMT_BUDDY_DATA;
		if (NULL == lpBuddyData)
			return NULL;
		memset(lpBuddyData, 0, sizeof(RMT_BUDDY_DATA));

		pairInsert = m_mapBuddyData.insert(std::pair<UINT, RMT_BUDDY_DATA*>(nUTalkUin, lpBuddyData));
		if (!pairInsert.second)
		{
			delete lpBuddyData;
			return NULL;
		}
	}

	::SendMessageTimeout(m_lpUserMgr->m_hProxyWnd, FMG_MSG_INTERNAL_GETBUDDYDATA, 
		nUTalkUin, (LPARAM)lpBuddyData, SMTO_BLOCK, 8000, 0);

	//::SendMessage(m_lpUserMgr->m_hProxyWnd, FMG_MSG_INTERNAL_GETBUDDYDATA, nUTalkUin, (LPARAM)lpBuddyData);
	if (lpBuddyData->nUTalkNum != 0 && lpBuddyData->szNickName[0] != _T('\0'))
	{
		return lpBuddyData;
	}

	if (0 == lpBuddyData->nUTalkNum)
	{
	//	CGetUTalkNumResult* lpGetUTalkNumResult = new CGetUTalkNumResult;
	//	if (NULL == lpGetUTalkNumResult)
	//		return NULL;

	//	int nRetry = 3;
	//	for (int i = 0; i < nRetry; i++)
	//	{
	///*		bRet = m_lpUTalkProtocol->GetUTalkNum(m_HttpClient, TRUE, nUTalkUin, 
	//			m_lpUserMgr->m_LoginResult2.m_strVfWebUTalk.c_str(), lpGetUTalkNumResult);
	//		if (bRet && 0 == lpGetUTalkNumResult->m_nRetCode)
	//			break;*/
	//	}

	//	if (bRet && 0 == lpGetUTalkNumResult->m_nRetCode)
	//	{
	//		lpBuddyData->nUTalkNum = lpGetUTalkNumResult->m_nUTalkNum;
	//		::PostMessage(m_lpUserMgr->m_hProxyWnd, 
	//			FMG_MSG_UPDATE_BUDDY_NUMBER, 0, (LPARAM)lpGetUTalkNumResult);
	//	}
	//	else
	//	{
	//		delete lpGetUTalkNumResult;
	//		return NULL;
	//	}
	}

	if (_T('\0') == lpBuddyData->szNickName[0])
	{
		// 假定昵称一定能够从好友列表获取到，这里不处理
	}

	return lpBuddyData;
}

RMT_GROUP_DATA* CRecvMsgThread::GetGroupData(UINT nGroupCode)
{
	std::map<UINT,RMT_GROUP_DATA*>::iterator iter;
	std::pair<std::map<UINT, RMT_GROUP_DATA*>::iterator, bool> pairInsert;
	RMT_GROUP_DATA* lpGroupData = NULL;
	BOOL bRet = FALSE;

	iter = m_mapGroupData.find(nGroupCode);
	if (iter != m_mapGroupData.end())
	{
		lpGroupData = iter->second;
		if (lpGroupData != NULL)
		{
			if (lpGroupData->bHasGroupInfo && 
				lpGroupData->nGroupNum != 0)
				return lpGroupData;
		}
		else
			m_mapGroupData.erase(iter);
	}

	if (NULL == lpGroupData)
	{
		lpGroupData = new RMT_GROUP_DATA;
		if (NULL == lpGroupData)
			return NULL;
		memset(lpGroupData, 0, sizeof(RMT_GROUP_DATA));

		pairInsert = m_mapGroupData.insert(std::pair<UINT, RMT_GROUP_DATA*>(nGroupCode, lpGroupData));
		if (!pairInsert.second)
		{
			delete lpGroupData;
			return NULL;
		}
	}

	::SendMessageTimeout(m_lpUserMgr->m_hProxyWnd, FMG_MSG_INTERNAL_GETGROUPDATA, 
		nGroupCode, (LPARAM)lpGroupData, SMTO_BLOCK, 8000, 0);

	//::SendMessage(m_lpUserMgr->m_hProxyWnd, FMG_MSG_INTERNAL_GETGROUPDATA, nGroupCode, (LPARAM)lpGroupData);
	if (lpGroupData->bHasGroupInfo && lpGroupData->nGroupNum != 0)
	{
		return lpGroupData;
	}

	if (!lpGroupData->bHasGroupInfo)
	{
		//CGroupInfoResult* lpGroupInfoResult = new CGroupInfoResult;
		//if (NULL == lpGroupInfoResult)
		//	return NULL;

		//int nRetry = 3;
		//for (int i = 0; i < nRetry; i++)
		//{
		//	//bRet = m_lpUTalkProtocol->GetGroupInfo(m_HttpClient, nGroupCode, 
		//	//	m_lpUserMgr->m_LoginResult2.m_strVfWebUTalk.c_str(), lpGroupInfoResult);
		//	//if (bRet && 0 == lpGroupInfoResult->m_nRetCode)
		//	//	break;
		//}

		//if (bRet && 0 == lpGroupInfoResult->m_nRetCode)
		//{
		//	lpGroupData->bHasGroupInfo = TRUE;

		//	::SendMessageTimeout(m_lpUserMgr->m_hProxyWnd, FMG_MSG_UPDATE_GROUP_INFO, 
		//		0, (LPARAM)lpGroupInfoResult, SMTO_BLOCK, 8000, 0);

		//	//::SendMessage(m_lpUserMgr->m_hProxyWnd, 
		//	//	FMG_MSG_UPDATE_GROUP_INFO, 0, (LPARAM)lpGroupInfoResult);
		//}
		//else
		//{
		//	delete lpGroupInfoResult;
		//	return NULL;
		//}
	}

	if (0 == lpGroupData->nGroupNum)
	{
		//CGetUTalkNumResult* lpGetUTalkNumResult = new CGetUTalkNumResult;
		//if (NULL == lpGetUTalkNumResult)
		//	return NULL;

		//int nRetry = 3;
		//for (int i = 0; i < nRetry; i++)
		//{
		//	//bRet = m_lpUTalkProtocol->GetUTalkNum(m_HttpClient, FALSE, nGroupCode, 
		//	//	m_lpUserMgr->m_LoginResult2.m_strVfWebUTalk.c_str(), lpGetUTalkNumResult);
		//	//if (bRet && 0 == lpGetUTalkNumResult->m_nRetCode)
		//	//	break;
		//}

		//if (bRet && 0 == lpGetUTalkNumResult->m_nRetCode)
		//{
		//	lpGroupData->nGroupNum = lpGetUTalkNumResult->m_nUTalkNum;
		//	::PostMessage(m_lpUserMgr->m_hProxyWnd, 
		//		FMG_MSG_UPDATE_GROUP_NUMBER, nGroupCode, (LPARAM)lpGetUTalkNumResult);
		//}
		//else
		//{
		//	delete lpGetUTalkNumResult;
		//	return NULL;
		//}
	}

	return lpGroupData;
}

RMT_BUDDY_DATA* CRecvMsgThread::GetGMemberData(UINT nGroupCode, UINT nUTalkUin)
{
	std::map<CGMemberMapKey,RMT_BUDDY_DATA*>::iterator iter;
	std::pair<std::map<CGMemberMapKey, RMT_BUDDY_DATA*>::iterator, bool> pairInsert;
	RMT_BUDDY_DATA* lpGMemberData = NULL;
	BOOL bRet = FALSE;
	
	CGMemberMapKey key;
	key.m_nGroupCode = nGroupCode;
	key.m_nUTalkUin = nUTalkUin;

	iter = m_mapGMemberData.find(key);
	if (iter != m_mapGMemberData.end())
	{
		lpGMemberData = iter->second;
		if (lpGMemberData != NULL)
		{
			if (lpGMemberData->nUTalkNum != 0 && 
				lpGMemberData->szNickName[0] != _T('\0'))
				return lpGMemberData;
		}
		else
			m_mapGMemberData.erase(iter);
	}

	if (NULL == lpGMemberData)
	{
		lpGMemberData = new RMT_BUDDY_DATA;
		if (NULL == lpGMemberData)
			return NULL;
		memset(lpGMemberData, 0, sizeof(RMT_BUDDY_DATA));

		pairInsert = m_mapGMemberData.insert(std::pair<CGMemberMapKey, RMT_BUDDY_DATA*>(key, lpGMemberData));
		if (!pairInsert.second)
		{
			delete lpGMemberData;
			return NULL;
		}
	}

	RMT_GMEMBER_REQ stGMemberReq = {nGroupCode, nUTalkUin};

	::SendMessageTimeout(m_lpUserMgr->m_hProxyWnd, FMG_MSG_INTERNAL_GETGMEMBERDATA, 
		(WPARAM)&stGMemberReq, (LPARAM)lpGMemberData, SMTO_BLOCK, 8000, 0);

	//::SendMessage(m_lpUserMgr->m_hProxyWnd, 
	//	FMG_MSG_INTERNAL_GETGMEMBERDATA, (WPARAM)&stGMemberReq, (LPARAM)lpGMemberData);
	if (lpGMemberData->nUTalkNum != 0 && lpGMemberData->szNickName[0] != _T('\0'))
	{
		return lpGMemberData;
	}

	if (0 == lpGMemberData->nUTalkNum)
	{
		//CGetUTalkNumResult* lpGetUTalkNumResult = new CGetUTalkNumResult;
		//if (NULL == lpGetUTalkNumResult)
		//	return NULL;

		//int nRetry = 3;
		//for (int i = 0; i < nRetry; i++)
		//{
		//	//bRet = m_lpUTalkProtocol->GetUTalkNum(m_HttpClient, TRUE, nUTalkUin, 
		//	//	m_lpUserMgr->m_LoginResult2.m_strVfWebUTalk.c_str(), lpGetUTalkNumResult);
		//	//if (bRet && 0 == lpGetUTalkNumResult->m_nRetCode)
		//	//	break;
		//}

		//if (bRet && 0 == lpGetUTalkNumResult->m_nRetCode)
		//{
		//	lpGMemberData->nUTalkNum = lpGetUTalkNumResult->m_nUTalkNum;
		//	::PostMessage(m_lpUserMgr->m_hProxyWnd, 
		//		FMG_MSG_UPDATE_GMEMBER_NUMBER, nGroupCode, (LPARAM)lpGetUTalkNumResult);
		//}
		//else
		//{
		//	delete lpGetUTalkNumResult;
		//	return NULL;
		//}
	}

	if (_T('\0') == lpGMemberData->szNickName[0])
	{
		// 假定昵称一定能够从好友列表获取到，这里不处理
	}

	return lpGMemberData;
}

UINT CRecvMsgThread::GroupId2Code(UINT nGroupId)
{
	std::map<UINT,UINT>::iterator iter;
	std::pair<std::map<UINT,UINT>::iterator, bool> pairInsert;
	UINT nGroupCode = 0;

	iter = m_mapGroupId2Code.find(nGroupId);
	if (iter != m_mapGroupId2Code.end())
	{
		nGroupCode = iter->second;
		if (nGroupCode != 0)
			return nGroupCode;
		else
			m_mapGroupId2Code.erase(iter);
	}

	::SendMessageTimeout(m_lpUserMgr->m_hProxyWnd, FMG_MSG_INTERNAL_GROUPID2CODE, 
		0, nGroupId, SMTO_BLOCK, 8000, (PDWORD_PTR)&nGroupCode);

	//nGroupCode = ::SendMessage(m_lpUserMgr->m_hProxyWnd, FMG_MSG_INTERNAL_GROUPID2CODE, 0, nGroupId);
	if (nGroupCode != 0)
	{
		pairInsert = m_mapGroupId2Code.insert(std::pair<UINT,UINT>(nGroupId, nGroupCode));
		if (pairInsert.second)
			return nGroupCode;
	}
	
	return 0;
}

BOOL CRecvMsgThread::IsNeedDownloadPic(const CBuddyMessage* pBuddyMessage)
{
	if(pBuddyMessage->m_nMsgType!=CONTENT_TYPE_IMAGE_CONFIRM && pBuddyMessage->m_nMsgType!=CONTENT_TYPE_MOBILE_IMAGE)
		return FALSE;
	
	const std::vector<CContent*>& arrContent = pBuddyMessage->m_arrContent;
	size_t nSize = arrContent.size();
	CContent* lpContent = NULL;
	//TODO: 需要下载图片的情况只有手机平台和电脑确认图片两种类型，所以这里的字体信息可以简化掉！
	for(size_t i=0; i<nSize; ++i)
	{
		lpContent = arrContent[i];
		if (NULL == lpContent)
			continue;

		if(CONTENT_TYPE_CHAT_IMAGE == lpContent->m_nType)
			return TRUE;	
	}

	return FALSE;
}

void CRecvMsgThread::GetChatPic(CBuddyMessage* pBuddyMessage)
{
	const std::vector<CContent*>& arrContent = pBuddyMessage->m_arrContent;
	size_t nSize = arrContent.size();
	CContent* lpContent = NULL;
	CFileItemRequest* pRequest = NULL;
	char szUtf8Name[MAX_PATH];
	TCHAR szDestPath[MAX_PATH];
						
	//TODO: 需要下载图片的情况只有手机平台和电脑确认图片两种类型，所以这里的字体信息可以简化掉！
	for(size_t i=0; i<nSize; ++i)
	{
		lpContent = arrContent[i];
		if (NULL == lpContent)
			continue;

		if(CONTENT_TYPE_CHAT_IMAGE==lpContent->m_nType)
		{
			memset(szUtf8Name, 0, sizeof(szUtf8Name));
			memset(szDestPath, 0, sizeof(szDestPath));
            //聊天图片的格式为jpg
			_stprintf_s(szDestPath, ARRAYSIZE(szDestPath), _T("%s"), m_pFMGClient->GetChatPicFullName(lpContent->m_CFaceInfo.m_strFileName.c_str()).c_str());
			if(!Hootina::CPath::IsFileExist(szDestPath))
			{
				pRequest = new CFileItemRequest();
				EncodeUtil::UnicodeToUtf8(lpContent->m_CFaceInfo.m_strFilePath.c_str(), szUtf8Name, ARRAYSIZE(szUtf8Name));
				
				strcpy_s(pRequest->m_szUtfFilePath, ARRAYSIZE(pRequest->m_szUtfFilePath), szUtf8Name);
				_tcscpy_s(pRequest->m_szFilePath, ARRAYSIZE(pRequest->m_szFilePath), szDestPath);
				pRequest->m_nFileType = FILE_ITEM_DOWNLOAD_CHAT_IMAGE;
				pRequest->m_pBuddyMsg = pBuddyMessage;
				m_pFMGClient->m_ImageTask.AddItem(pRequest);
			}
		}
	}

}

BOOL CRecvMsgThread::ProcessBuddyMsg(CBuddyMessage* lpBuddyMsg)
{
	if (NULL==lpBuddyMsg)
 		return FALSE;

	if(lpBuddyMsg->m_nMsgType == CONTENT_TYPE_IMAGE_CONFIRM)
		return TRUE;
	
	const long MAX_RECENT_MSG_LENGTH = 16;
	CString strRecentMsg;
	UINT uFaceID = 0;
	tstring strNickName;
	CContent* lpContent = NULL;
	size_t nSize = lpBuddyMsg->m_arrContent.size();
	if(IsGroupTarget(lpBuddyMsg->m_nToUin))
		strRecentMsg.Format(_T("%s:"), m_lpUserMgr->GetNickName(lpBuddyMsg->m_nFromUin).c_str());
	for (size_t i = 0; i < nSize; ++i)
	{
		lpContent = lpBuddyMsg->m_arrContent[i];
		if(lpContent == NULL)
			continue;

		if (lpContent->m_nType == CONTENT_TYPE_TEXT)							//文本
		{
			strRecentMsg += lpContent->m_strText.data();
		}
		else if (lpContent->m_nType == CONTENT_TYPE_FACE)				//表情
		{
			if(strRecentMsg.GetLength()<MAX_RECENT_MSG_LENGTH && strRecentMsg.GetLength()+4<=MAX_RECENT_MSG_LENGTH)
				strRecentMsg += _T("[表情]");
		}
		else if (lpContent->m_nType == CONTENT_TYPE_SHAKE_WINDOW)			//窗口抖动
		{
			if(strRecentMsg.GetLength()<MAX_RECENT_MSG_LENGTH && strRecentMsg.GetLength()+6<=MAX_RECENT_MSG_LENGTH)
				strRecentMsg += _T("[窗口抖动]");
		}
		else if (lpContent->m_nType == CONTENT_TYPE_CHAT_IMAGE)			//图片
		{
			if(strRecentMsg.GetLength()<MAX_RECENT_MSG_LENGTH && strRecentMsg.GetLength()+4<=MAX_RECENT_MSG_LENGTH)
				strRecentMsg += _T("[图片]");
		}
		else if (lpContent->m_nType == CONTENT_TYPE_FILE)			//文件
		{
			if(strRecentMsg.GetLength()<MAX_RECENT_MSG_LENGTH && strRecentMsg.GetLength()+4<=MAX_RECENT_MSG_LENGTH)
				strRecentMsg += _T("[文件]");
		}
		
	}

	strRecentMsg = strRecentMsg.Left(MAX_RECENT_MSG_LENGTH);
	
	if(lpBuddyMsg->m_nToUin == m_lpUserMgr->m_UserInfo.m_uUserID)
	{
		uFaceID = m_lpUserMgr->GetFaceID(lpBuddyMsg->m_nFromUin);
		strNickName = m_lpUserMgr->GetNickName(lpBuddyMsg->m_nFromUin);
		strRecentMsg = strRecentMsg.Left(MAX_RECENT_MSG_LENGTH);
		AddItemToRecentSessionList(lpBuddyMsg->m_nFromUin, uFaceID, strNickName.c_str(), strRecentMsg, lpBuddyMsg->m_nTime);
	}
	else if(lpBuddyMsg->m_nFromUin == m_lpUserMgr->m_UserInfo.m_uUserID)
	{
		uFaceID = m_lpUserMgr->GetFaceID(lpBuddyMsg->m_nToUin);
		strNickName = m_lpUserMgr->GetNickName(lpBuddyMsg->m_nToUin);
		strRecentMsg = strRecentMsg.Left(MAX_RECENT_MSG_LENGTH);
		AddItemToRecentSessionList(lpBuddyMsg->m_nToUin, uFaceID, strNickName.c_str(), strRecentMsg, lpBuddyMsg->m_nTime);
	}
	
	//群消息
	if(IsGroupTarget(lpBuddyMsg->m_nToUin))
	{
		strNickName = m_lpUserMgr->GetGroupName(lpBuddyMsg->m_nToUin);
		AddItemToRecentSessionList(lpBuddyMsg->m_nToUin, uFaceID, strNickName.c_str(), strRecentMsg, lpBuddyMsg->m_nTime);
	}

	
	return TRUE;
}

void CRecvMsgThread::AddItemToRecentSessionList(UINT uUserID, UINT uFaceID, PCTSTR pszNickName, PCTSTR pszText, time_t nMsgTime)
{
	assert(m_lpUserMgr != NULL);

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