//协议及状态常量

#pragma once
#include <stdint.h>
#include <set>
#include <list>
#include <map>
#include <string>

#define DEFAULT_TEAMNAME _T("My Friends")

//客户端类型
enum CLIENT_TYPE
{
	CLIENT_TYPE_UNKNOWN = 0,
	CLIENT_TYPE_PC      = 1,
	CLIENT_TYPE_IOS		= 2,
	CLIENT_TYPE_ANDROID = 3
};

//在线状态
enum ONLINE_STATUS
{
	STATUS_OFFLINE		  = 0,			//离线
	STATUS_ONLINE		  = 1,			//在线
	STATUS_INVISIBLE	  = 2,			//隐身
	STATUS_BUSY			  = 3,			//忙碌
	STATUS_AWAY			  = 4,			//离开
	STATUS_MOBILE_ONLINE  = 5,			//移动版在线
	STATUS_MOBILE_OFFLINE = 6,			//移动版下线
	STATUS_BOTH			  = 7			//手机和电脑同时在线
};

enum ONLINE_CLIENT_TYPE
{	
    ONLINE_CLIENT_UNKONWN,
    ONLINE_CLIENT_PC,
	ONLINE_CLIENT_MOBILE,
	ONLINE_CLIENT_BOTH,
	OFFLINE_CLIENT_BOTH
};

enum LOGIN_TYPE
{
	LOGIN_USE_MOBILE_NUMBER = 0,	//使用手机号登录
	LOGIN_USE_ACCOUNT		= 1,	//使用账号登录
};

enum LOGIN_RESULT_CODE	// 登录结果代码
{
	LOGIN_SUCCESS,				// 登录成功
	LOGIN_FAILED,				// 登录失败
	LOGIN_UNREGISTERED,			// 用户未注册
	LOGIN_PASSWORD_ERROR,		// 密码错误
	LOGIN_USER_CANCEL_LOGIN,	// 用户取消登录
	LOGIN_SERVER_REFUSED,		// 服务器拒绝
	LOGIN_SERVER_NOT_RESPONSE	// 服务器未响应
};

enum REGISTER_RESULT_CODE	//注册结果代码
{
	REGISTER_SUCCESS,				//注册成功
	REGISTER_EXIST,					//账号已经存在
	REGISTER_FAILED,				//注册失败
	REGISTER_SERVER_NOT_RESPONSE	//服务器未响应
};

enum HEARTBEAT_RESULT_CODE
{
	HEARTBEAT_ALIVE,				//仍然保持连接
	HEARTBEAT_DEAD,					//已经断开连接
};

//加好友状态
enum OPERATE_FRIEND
{
    Apply,
    Agree,
    Refuse,
    Delete
};

enum ADD_FRIEND_RESULT	//添加好友结果
{
	FIND_FRIEND_NOT_FOUND,
	FIND_FRIEND_FOUND,
	FIND_FRIEND_FAILED,
	
	ADD_FRIEND_SUCCESS,
	ADD_FRIEND_FAILED
};

enum DELETE_FRIEND_RESULT
{
	DELETE_FRIEND_SUCCESS,
	DELETE_FRIEND_FAILED
};

enum SEND_CHAT_MSG_RESULT		//发送聊天消息结果
{
	SEND_MSG_SUCESS,
	SEND_WHOLE_MSG_FAILED,		//发送整个消息失败
	SEND_IMGAE_FAILED,			//发送图片失败
	//SEND_FILE_FAILED			//发送文件失败
};

enum UPLOAD_USER_THUMB_RESULT
{
	UPLOAD_USER_THUMB_RESULT_SUCCESS,
	UPLOAD_USER_THUMB_RESULT_FAILED
};

enum SEND_FILE_RESULT
{
	SEND_FILE_FAILED,
	SEND_FILE_SUCCESS
};

enum RECV_FILE_RESULT
{
	RECV_FILE_FAILED,
	RECV_FILE_SUCCESS
};

enum GET_USER_INFO_TYPE
{
	USER_INFO_TYPE_SELF,
	USER_INFO_TYPE_FRIENDS,
	USER_INFO_TYPE_GROUP,
	USER_INFO_TYPE_GROUP_MEMBER
};

struct FindUserOrGroupResult
{
	long		nType;			//个人还是群组
	UINT		uAccountID;
	TCHAR		szAccountName[32];
	TCHAR		szNickName[32];
};

//记录当前添加好友的状态信息
struct AddFriendInfo
{
	UCHAR	nCmd;				//操作类型（Apply/Agree/Refuse）
	UINT	uAccountID;			//账户ID
	TCHAR	szAccountName[32];	//账户名
	TCHAR	szNickName[32];		//昵称
};

enum CHAT_MSG_TYPE
{
	FMG_MSG_TYPE_UNKNOWN,
	FMG_MSG_TYPE_BUDDY,
	FMG_MSG_TYPE_GROUP,
	FMG_MSG_TYPE_MULTI,		//群发消息
	FMG_MSG_TYPE_SESS
};

struct UserBasicInfo
{
	UINT  uAccountID;
	char  szAccountName[32];
	char  szNickName[64];
    char  szMarkName[64];
    char  szSignature[256];
    char  szAddress[51225];
	UINT  uFaceID;
    int   nStatus;              //在线状态
    int   clientType;           //客户端类型
    char  customFace[64];
    int   nGender;
    int   nBirthday;
    char  szPhoneNumber[64];
    char  szMail[64];
};

struct UserExtendInfo
{
	UINT	uAccountID;
	UINT	uGender;
	UINT	uShengXiao;
	UINT	uConstel;
	UINT    uBlood;
	UINT	uBirthday;

	char	szCountry[32];			// 国家
	char	szProvince[32];			// 省份
	char	szCity[32];				// 城市
	char	szPhone[32];			// 电话
	char	szMobile[32];			// 手机
	char	szOccupation[32];		// 职业
	char	szCollege[32];			// 毕业院校
	char	szEmail[48];			// 邮箱
	char	szHomepage[64];			// 个人主页
	char	szSignature[256];		// 个性签名
	char	szPersonal[512];		// 个人说明	
	char    szCustomFace[MAX_PATH];	// 自定义头像名
};

//文件上传下载类型
enum FILE_ITEM_TYPE
{
	FILE_ITEM_UNKNOWN,
	FILE_ITEM_UPLOAD_CHAT_IMAGE,
	FILE_ITEM_UPLOAD_CHAT_OFFLINE_FILE,
	FILE_ITEM_UPLOAD_USER_THUMB,

	FILE_ITEM_DOWNLOAD_CHAT_IMAGE,
	FILE_ITEM_DOWNLOAD_CHAT_OFFLINE_FILE,
	FILE_ITEM_DOWNLOAD_USER_THUMB,
};

//文件下载返回结果码
enum FILE_DOWNLOAD_RETCODE
{
	FILE_DOWNLOAD_FAILED,
	FILE_DOWNLOAD_SUCCESS,
	FILE_DOWNLOAD_USERCANCEL	//用户取消下载
};

//文件上传返回结果码
enum FILE_UPLOAD_RETCODE
{
	FILE_UPLOAD_FAILED,
	FILE_UPLOAD_SUCCESS,
	FILE_UPLOAD_USERCANCEL		//用户取消上传
};

//获取文件md5值结果码
enum GET_FILE_MD5_RETCODE
{
	GET_FILE_MD5_FAILED,
	GET_FILE_MD5_SUCESS,
	GET_FILE_MD5_USERCANCEL
};

//修改个人信息详情
enum MODIFY_USER_INFO_TYPE
{
	MODIFY_NICKNAME = 0x0001,
	MODIFY_GENDER   = 0x0002,
	MODIFY_BIRTHDAY = 0x0004,
	MODIFY_SIGNATRUE= 0x0008,
	MODIFY_ADDRESS  = 0x0010,
	MODIFY_PHONE    = 0x0020,
	MODIFY_EMAIL    = 0x0040,
	MODIFY_THUMB    = 0x0080
};

enum CHAT_CONFIRM_TYPE
{
	CHAT_CONFIRM_TYPE_SINGLE,		//单聊（包括群组）确认消息类型
	CHAT_CONFIRM_TYPE_MULTI			//群发确认消息类型
};

enum NET_DATA_TYPE
{
	NET_DATA_UNKNOWN,
	NET_DATA_REGISTER,
	NET_DATA_LOGIN,
	NET_DATA_USER_BASIC_INFO,
    NET_DATA_CHANGE_STATUS,
    NET_DATA_GROUP_BASIC_INFO,
	NET_DATA_USER_EXTEND_INFO,
	NET_DATA_FRIENDS_ID,
	NET_DATA_FRIENDS_STATUS,
	NET_DATA_CHAT_MSG,
	NET_DATA_CHAT_CONFIRM_IMAGE_MSG,	//聊天发送完图片后追加的确认信息
	NET_DATA_FIND_FRIEND,
	NET_DATA_OPERATE_FRIEND,
	NET_DATA_HEARTBEAT,
	NET_DATA_UPDATE_LOGON_USER_INFO,
	NET_DATA_TARGET_INFO_CHANGE,
	NET_DATA_MODIFY_PASSWORD,
	NET_DATA_CREATE_NEW_GROUP,
    NET_DATA_OPERATE_TEAM,              //添加新的好友分组
    NET_DATA_MODIFY_FRIEND_MARKNAME,    //修改好友备注名
    NET_DATA_MOVE_FRIEND,               //移动好友至其他分组

	NET_DATA_FILE
};

class CNetData
{
public:
	CNetData();
	virtual ~CNetData();
public:
	UINT		m_uType;		//数据类型
	long		m_nRetryTimes;	//当作为发送数据项时重试次数。
};

class CRegisterRequest : public CNetData
{
public:
	CRegisterRequest();
	~CRegisterRequest();

public:
	char m_szAccountName[64];
	char m_szNickName[64];
	char m_szPassword[64];
	
};

class CRegisterResult : public CNetData
{
public:
	CRegisterResult();
	~CRegisterResult();

public:
	long m_nResultCode;
	char m_szMobile[64];
	char m_szAccount[64];
};

class CLoginRequest : public CNetData
{
public:
	CLoginRequest();
	~CLoginRequest();
public:
	char		m_szAccountName[64];
	char		m_szPassword[64];
	long		m_nStatus;
	LOGIN_TYPE	m_nLoginType;	//登录类型：0表示使用手机号登入，1表示使用账号登入
};

class CLoginResult : public CNetData
{
public:
	CLoginResult();
	~CLoginResult();
public:
	long m_LoginResultCode;
	UINT m_uAccountID;
	char m_szAccountName[32];
	char m_szNickName[32];
	char m_szSignature[256];
	long m_nFace;
    char m_szCustomFace[64];
    int  m_nGender;
    int  m_nBirthday;
    char m_szAddress[512];
    char m_szPhoneNumber[64];
    char m_szMail[256];
	long m_nStatus;
};

class CUserBasicInfoRequest : public CNetData
{
public:
	CUserBasicInfoRequest();
	~CUserBasicInfoRequest();

public:
	std::set<UINT> m_setAccountID;
};

class CUserBasicInfoResult : public CNetData
{
public:
	CUserBasicInfoResult();
	~CUserBasicInfoResult();

public:
    //key是分组的名字，value是该组好友的集合
	std::map<std::string, std::list<UserBasicInfo*>> m_mapUserBasicInfo;
};

class CChangeUserStatusRequest : public CNetData
{
public:
    CChangeUserStatusRequest();
    ~CChangeUserStatusRequest();

public:
    int32_t m_nNewStatus;
};

class CGroupBasicInfoRequest : public CNetData
{
public:
    CGroupBasicInfoRequest();
    ~CGroupBasicInfoRequest();

public:
    int  m_groupid;
};

class CGroupBasicInfoResult : public CNetData
{
public:
    CGroupBasicInfoResult();
    ~CGroupBasicInfoResult();

public:
    int                       m_groupid;
    std::list<UserBasicInfo*> m_listUserBasicInfo;
};

class CUserExtendInfoRequest : public CNetData
{
public:
	CUserExtendInfoRequest();
	~CUserExtendInfoRequest();

public:
	std::set<UINT> m_setAccountID;
};

class CUserExtendInfoResult : public CNetData
{
public:
	CUserExtendInfoResult();
	~CUserExtendInfoResult();
public:
	std::list<UserExtendInfo*> m_listUserExtendInfo;
};

class CLoginUserFriendsIDRequest : public CNetData
{
public:
	CLoginUserFriendsIDRequest();
	~CLoginUserFriendsIDRequest();

public:
	UINT m_uAccountID;
};

class CLoginUserFriendsIDResult : public CNetData
{
public:
	CLoginUserFriendsIDResult();
	~CLoginUserFriendsIDResult();

public:
	UINT				m_uAccountID;
	std::set<UINT>		m_setFriendIDs;
	std::set<UINT>		m_setGroupIDs;
};

class CFriendStatus : public CNetData
{
public:
	CFriendStatus();
	~CFriendStatus();

public:
	UINT	m_uAccountID;
	long	m_nStatus;
    long    m_nClientType;
    int     m_type;
};

class CFindFriendRequest : public CNetData
{
public:
	CFindFriendRequest();
	~CFindFriendRequest();

public:
	char m_szAccountName[64];
	long m_nType;				//查找类型
};

class CFindFriendResult : public CNetData
{
public:
	CFindFriendResult();
	~CFindFriendResult();

public:
	long m_nResultCode;
	UINT m_uAccountID;
	char m_szAccountName[64];
	char m_szNickName[64];
};

class COperateFriendRequest : public CNetData
{
public:
	COperateFriendRequest();
	~COperateFriendRequest();

public:
	UINT m_uAccountID;
	UINT m_uCmd;
};

class COperateFriendResult : public CNetData
{
public:
	COperateFriendResult();
	~COperateFriendResult();

public:
	UINT m_uAccountID;
	UINT m_uCmd;
	char m_szAccountName[64];
	char m_szNickName[64];
};

class CAddTeamInfoRequest : public CNetData
{
public:
    CAddTeamInfoRequest();
    ~CAddTeamInfoRequest();

public:
    int          m_opType;          //操作类型
    std::wstring m_strNewTeamName;
    std::wstring m_strOldTeamName;  
};

class CAddTeamInfoResult : public CNetData
{
public:
    CAddTeamInfoResult();
    ~CAddTeamInfoResult();

public:
    int          m_opType;          //操作类型  
};

class CMoveFriendRequest : public CNetData
{
public:
    CMoveFriendRequest();
    ~CMoveFriendRequest();

public:
    int          m_nFriendID;          //操作类型
    std::wstring m_strNewTeamName;
    std::wstring m_strOldTeamName;
};

class CMsgItem;

class CSentChatMessage : public CNetData
{
public:
	CSentChatMessage();
	~CSentChatMessage();

public:
	CMsgItem* m_pMsgItem;
	HWND	  m_hwndChat;
};

class CSentChatConfirmImageMessage : public CNetData
{
public:
	CSentChatConfirmImageMessage();
	~CSentChatConfirmImageMessage();

public:
	CHAT_CONFIRM_TYPE	m_nType;
	HWND				m_hwndChat;
	char*				m_pszConfirmBody;
	UINT				m_uConfirmBodySize;		//消息体的长度，包括最后末尾结束的\0
	UINT				m_uSenderID;
	std::set<UINT>		m_setTargetIDs;
	//UINT	  m_uTargetID;

};

class CRecvChatMessage : public CNetData
{
public:
	CRecvChatMessage();
	~CRecvChatMessage();

public:
	UINT  m_uMsgID;			//消息ID
	UINT  m_uSenderID;		//发送人ID
	UINT  m_uTargetID;		//接收者ID
	UINT  m_uMsgBodySize;	//消息正文内容长度
	char* m_pszMsgBody;		//消息正文内容
};

class CModifyPasswordRequest : public CNetData
{
public:
	CModifyPasswordRequest();
	~CModifyPasswordRequest();

public:
	char m_szOldPassword[64];
	char m_szNewPassword[64];
};

class CModifyPasswordResult : public CNetData
{
public:
	CModifyPasswordResult();
	~CModifyPasswordResult();

public:
	long	m_nResultCode;
};

//class CCreateNewGroupRequest
class CCreateNewGroupRequest : public CNetData
{
public:
	CCreateNewGroupRequest();
	~CCreateNewGroupRequest();

public:
	char m_szGroupName[64];
};

//class CCreateGroupResult
class CCreateNewGroupResult : public CNetData
{
public:
	CCreateNewGroupResult();
	~CCreateNewGroupResult();

public:
	UINT	m_uError;
	UINT	m_uAccountID;
	char	m_szGroupName[64];
	char	m_szAccount[32];
};

//class CModifyFriendMakeNameRequest
class CModifyFriendMakeNameRequest : public CNetData
{
public:
    CModifyFriendMakeNameRequest();
    ~CModifyFriendMakeNameRequest();

public:
    UINT	m_uFriendID;
    TCHAR   m_szNewMarkName[64];
};

//class CModifyFriendMakeNameResult
class CModifyFriendMakeNameResult : public CNetData
{
public:
    CModifyFriendMakeNameResult();
    ~CModifyFriendMakeNameResult();

public:
    UINT	m_uFriendID;
    //char	m_szNewMarkName[64];
};

class CHeartbeatMessageRequest : public CNetData
{
public:
	CHeartbeatMessageRequest();
	~CHeartbeatMessageRequest();
};

class CHeartbeatMessageResult : public CNetData
{
public:
	~CHeartbeatMessageResult();
	CHeartbeatMessageResult();
};

//class CUpdateLogonUserInfoRequest
class CUpdateLogonUserInfoRequest : public CNetData
{
public:
	CUpdateLogonUserInfoRequest();
	~CUpdateLogonUserInfoRequest();

public:
	char	m_szNickName[64];
	char    m_szSignature[512];
	UINT	m_uGender;
	long	m_nBirthday;
	char    m_szAddress[64];
	char    m_szPhone[32];
	char    m_szMail[48];
	UINT	m_uFaceID;
	TCHAR   m_szCustomFace[MAX_PATH];
	BOOL    m_bUseCustomThumb;				//是否使用自定义头像
};

//class CUpdateLogonUserInfoResult
class CUpdateLogonUserInfoResult : public CNetData
{
public:
	CUpdateLogonUserInfoResult();
	~CUpdateLogonUserInfoResult();

public:
    char	m_szNickName[64];
    char    m_szSignature[512];
    UINT	m_uGender;
    long	m_nBirthday;
    char    m_szAddress[64];
    char    m_szPhone[32];
    char    m_szMail[48];
    UINT	m_uFaceID;
    char    m_szCustomFace[MAX_PATH];
    BOOL    m_bUseCustomThumb;
};

//class CTargetInfoChangeResult
class CTargetInfoChangeResult : public CNetData
{
public:
	CTargetInfoChangeResult();
	~CTargetInfoChangeResult();

public:
	UINT m_uAccountID;

};

class CScreenshotInfo
{
public:
    CScreenshotInfo();
    ~CScreenshotInfo();

public:
    std::string m_strBmpHeader;
    std::string m_strBmpData;
    UINT        m_targetId;
};


/////////////////////////////////////////////////////
/////////////////////////////////////////////////////
// 文件传输内容
/////////////////////////////////////////////////////
class CBuddyMessage;
class CFileItemRequest : public CNetData
{
public:
	CFileItemRequest();
	~CFileItemRequest();

public:
	long			m_nID;
	char			m_szUtfFilePath[MAX_PATH];	//文件utf8路径（用于下载，下载时必填）
	TCHAR			m_szFilePath[MAX_PATH];		//文件全饰路径（用于上传，上传时必填, 下载时作为目标文件路径）
	HWND			m_hwndReflection;			//							 (非必须字段)
	HANDLE			m_hCancelEvent;				// 取消事件					 (非必须字段)
	UINT			m_uSenderID;
	std::set<UINT>	m_setTargetIDs;
	UINT			m_uAccountID;				//账户id，用于下载头像
	long			m_nFileType;				//目前有聊天图片、离线文件和自定义头像三种类型
	CBuddyMessage*  m_pBuddyMsg;

	BOOL			m_bPending;					//当该项已经在下载或者上传为FALSE，反之为TRUE
};

class CUploadFileResult
{
public:
	CUploadFileResult();
	~CUploadFileResult();

public:
	void Clone(const CUploadFileResult* pSource);

public:
	long			m_nFileType;
	BOOL			m_bSuccessful;					//是否上传成功
	int64_t			m_nFileSize;					//文件大小
	TCHAR			m_szLocalName[MAX_PATH];		//本地文件名
						
	char			m_szMd5[64];					//文件的md5值
	char			m_szRemoteName[MAX_PATH];		//上传成功以后文件在服务器上的路径名

	UINT			m_uSenderID;
	std::set<UINT>	m_setTargetIDs;
	HWND			m_hwndReflection;
};

struct FileProgress
{
	TCHAR szDestPath[MAX_PATH];
	long  nPercent;				//百分比，0～100之间(值为-1时，为获取文件md5值)
	long  nVerificationPercent;	//获取md5值进度（0～100）
};






