#pragma once

#include "jsoncpp-1.9.0/json.h"
#include <string>
#include <vector>

#if defined(UNICODE) || defined(_UNICODE)
typedef std::wstring tstring;
#else
typedef std::string tstring;
#endif

extern TCHAR g_szHomePath[MAX_PATH];
extern char g_szHomePathAscii[MAX_PATH];
extern const UINT USER_THUMB_COUNT;

extern std::wstring g_strAppTitle;

#define     FMG_MSG_NET_ERROR               WM_USER + 50            // 网络错误

#define		FMG_MSG_LOGIN_RESULT			WM_USER + 100		    // 登录返回消息
#define		FMG_MSG_LOGOUT_RESULT			WM_USER + 101			// 注销返回消息
#define		FMG_MSG_UPDATE_BUDDY_LIST		WM_USER + 103			// 更新好友列表消息
#define		FMG_MSG_UPDATE_GROUP_LIST		WM_USER + 104			// 更新群列表消息
#define		FMG_MSG_UPDATE_RECENT_LIST		WM_USER + 105		    // 更新最近联系人列表消息
#define		FMG_MSG_BUDDY_MSG				WM_USER + 106			// 好友消息
#define		FMG_MSG_GROUP_MSG				WM_USER + 107			// 群消息
#define		FMG_MSG_SESS_MSG				WM_USER + 108		    // 临时会话消息
#define		FMG_MSG_STATUS_CHANGE_MSG		WM_USER + 109			// 好友状态改变消息
#define		FMG_MSG_KICK_MSG				WM_USER + 110		    // 被踢下线消息
#define		FMG_MSG_SYS_GROUP_MSG			WM_USER + 111			// 群系统消息
#define		FMG_MSG_UPDATE_BUDDY_NUMBER		WM_USER + 112		    // 更新好友号码
#define		FMG_MSG_UPDATE_GMEMBER_NUMBER	WM_USER + 113			// 更新群成员号码
#define		FMG_MSG_UPDATE_GROUP_NUMBER		WM_USER + 114		    // 更新群号码
#define		FMG_MSG_UPDATE_BUDDY_SIGN		WM_USER + 115			// 更新好友个性签名
#define		FMG_MSG_UPDATE_GMEMBER_SIGN		WM_USER + 116		    // 更新群成员个性签名
#define		FMG_MSG_UPDATE_BUDDY_INFO		WM_USER + 117			// 更新好友信息
#define		FMG_MSG_UPDATE_GMEMBER_INFO		WM_USER + 118		    // 更新群成员信息
#define		FMG_MSG_UPDATE_GROUP_INFO		WM_USER + 119			// 更新群信息
#define		FMG_MSG_UPDATE_C2CMSGSIG		WM_USER + 120		    // 更新临时会话信令
#define		FMG_MSG_UPDATE_GROUPFACESIG		WM_USER + 121		    // 更新群表情信令
#define		FMG_MSG_UPDATE_BUDDY_HEADPIC	WM_USER + 122		    // 更新好友头像
#define		FMG_MSG_UPDATE_GMEMBER_HEADPIC	WM_USER + 123		    // 更新群成员头像
#define		FMG_MSG_UPDATE_GROUP_HEADPIC	WM_USER + 124		    // 更新群头像
#define		FMG_MSG_CHANGE_STATUS_RESULT	WM_USER + 125		    // 改变在线状态返回消息

#define		FMG_MSG_INTERNAL_GETBUDDYDATA	WM_USER + 400
#define		FMG_MSG_INTERNAL_GETGROUPDATA	WM_USER + 401
#define		FMG_MSG_INTERNAL_GETGMEMBERDATA	WM_USER + 402
#define		FMG_MSG_INTERNAL_GROUPID2CODE	WM_USER + 403

#define		FMG_MSG_REGISTER				WM_USER + 404			//注册新用户
#define		FMG_MSG_FINDFREIND			    WM_USER + 405
#define     FMG_MSG_ADDFREIND				WM_USER + 406			//主动发送加好友请求
#define		FMG_MSG_RECVADDFRIENDREQUSET	WM_USER + 407			//收到加好友请求
#define		FMG_MSG_DELETEFRIEND			WM_USER + 408
#define		FMG_MSG_SENDCHATMSG_RESULT	    WM_USER + 409			//给好友发消息的反馈结果
//#define		FMG_MSG_UPLOADFILE_RESULT		WM_USER + 410		//上传文件反馈结果
#define		FMG_MSG_RECVFILE_REQUEST		WM_USER + 411			//接受文件请求
#define		FMG_MSG_RECVFILE_RESULT		    WM_USER + 412			//下载文件反馈结果
#define     FMG_MSG_RECVADDFRIENDANS		WM_USER + 413			//收到请求添加好友的对方反馈(拒绝或者同意)
#define		FMG_MSG_HEARTBEAT				WM_USER + 414			//心跳消息
#define		FMG_MSG_GOOFFLINE				WM_USER + 415			//下线或者掉线
#define		FMG_MSG_UPDATE_USER_BASIC_INFO  WM_USER + 416			//更新某个用户基本信息
#define		FMG_MSG_UPDATE_GROUP_BASIC_INFO WM_USER + 417		    //更新某个群组基本信息

#define		FMG_MSG_UPDATE_FRIEND_IDS		WM_USER + 418			//更新所有好友ID
#define		FMG_MSG_MODIFY_USER_INFO		WM_USER + 421			//修改用户信息成功
#define		FMG_MSG_RECV_USER_STATUS_CHANGE_DATA	WM_USER + 422	//收到用户在线状态改变数据
#define		FMG_MSG_USER_STATUS_CHANGE	    WM_USER + 423			//用户在线状态改变
#define		FMG_MSG_SELF_STATUS_CHANGE	    WM_USER + 424			//自己在另外的终端上下线通知
#define     FMG_MSG_UPDATE_USER_CHAT_MSG_ID	WM_USER + 425		    //更新用户的聊天消息ID
#define		FMG_MSG_RECV_CHAT_MSG			WM_USER + 426			//收到聊天消息
#define		FMG_MSG_UPDATE_CHATDLG_USERINFO WM_USER + 427			//更新聊天窗口中用户状态
#define		FMG_MSG_SEND_FILE_PROGRESS	    WM_USER + 428			//文件发送进度消息
#define		FMG_MSG_RECV_FILE_PROGRESS	    WM_USER + 429			//接收文件进度消息
#define		FMG_MSG_SEND_FILE_RESULT		WM_USER + 430			//发送文件结果
#define		FMG_MSG_RECV_FILE_RESULT		WM_USER + 431			//接收文件结果
#define		FMG_MSG_NETWORK_STATUS_CHANGE   WM_USER + 432			//本机网络状态发送变化
#define		FMG_MSG_CUSTOMFACE_AVAILABLE	WM_USER + 433			//自定义头像变为可用
#define		FMG_MSG_UPLOAD_USER_THUMB		WM_USER + 434			//自定义头像消息
#define		FMG_MSG_TARGET_INFO_CHANGE	    WM_USER + 435			//服务器告诉客户端用户信息发生变化，需要用户去取新的用户信息
#define		FMG_MSG_MODIFY_PASSWORD_RESULT  WM_USER + 436			//修改密码结果
#define		FMG_MSG_CREATE_NEW_GROUP_RESULT WM_USER + 437			//创建群组结果
#define     FMG_MSG_SCREENSHOT              WM_USER + 438			//屏幕截图消息


#define		FMG_MSG_FIRST					WM_USER + 001
#define		FMG_MSG_LAST					WM_USER + 500

// 消息类型字符串
#define		FMG_MSG_TYPE_STR_BUDDY			_T("message")		        // 好友消息类型
#define		FMG_MSG_TYPE_STR_GROUP			_T("group_message")	        // 群消息类型
#define		FMG_MSG_TYPE_STR_SESS			_T("sess_message")		    // 临时会话消息类型
#define		FMG_MSG_TYPE_STR_STATUSCHANGE	_T("buddies_status_change")	// 好友状态改变消息类型
#define		FMG_MSG_TYPE_STR_KICK			_T("kick_message")		    // 被踢下线消息类型
#define		FMG_MSG_TYPE_STR_SYSGROUP		_T("sys_g_msg")			    // 群系统消息类型

//typedef NS_ENUM(int, IUKMessageInsideType) {// 聊天消息类型
//    IUKMessageInsideTypeUndefined   = 0,// 未知消息类型
//    IUKMessageInsideTypeText        = 1,// 文本类型
//    IUKMessageInsideTypeImage       = 2,// 图片类型  pc专用 （pc在上传图片成功后 补发的消息 用于去下载  pc和app端都要处理）
//    IUKMessageInsideTypeFile        = 3,// 文件类型
//    IUKMessageInsideTypeShake       = 4,// 窗口抖动  pc专用
//    IUKMessageinsideTypeApp         = 5 // app端发出的图片  （app端发送的图片信息，已经上传好才会发送的这一条  pc接到这个类型可以直接显示）
//};

enum CONTENT_TYPE
{
    CONTENT_TYPE_UNKNOWN,			    // 未知消息类型
    CONTENT_TYPE_TEXT,				    // 文本类型（包括图文混排类型）
    CONTENT_TYPE_IMAGE_CONFIRM,			// 图片上传成功后的确认消息类型  pc专用 （pc在上传图片成功后 补发的消息 用于去下载  pc和app端都要处理）
    CONTENT_TYPE_FILE,				    // 收到好友发送过来的文件
    CONTENT_TYPE_SHAKE_WINDOW,			// 窗口抖动  pc专用
    CONTENT_TYPE_MOBILE_IMAGE,			// app端发出的图片  （app端发送的图片信息，已经上传好才会发送的这一条  pc接到这个类型可以直接显示）

    CONTENT_TYPE_FONT_INFO,				//pc专用，移动端忽略
    CONTENT_TYPE_FACE,					//pc专用，移动端忽略
    CONTENT_TYPE_CHAT_IMAGE,			//pc专用，移动端忽略

    CONTENT_TYPE_REMOTE_DESKTOP         //远程桌面
};

class CBuddyTeamInfo;
class CBuddyInfo;
class COnlineBuddyInfo;
class CGroupInfo;
class CRecentInfo;
class CBuffer;

struct BuddyInfo
{
    UINT	uAccountID;
    TCHAR	szAccountName[32];
    TCHAR	szMarkName[32];
    long	nTeamIndex;
    long	nGroupCount;		//群个数
    UINT	GroupID[64];		//群号ID
};

class CBuddyInfo			// 好友信息
{
public:
    CBuddyInfo(void);
    ~CBuddyInfo(void);

public:
    void Reset();
    void FillFakeData();	//TODO: 填充虚假数据，仅为了测试，实际的数据应该等服务器端完善以后从服务器获取
    void SetBuddyInfo(CBuddyInfo* lpBuddyInfo);

public:
    UINT		m_uUserID;			//用户ID，唯一标识该用户
    tstring		m_strAccount;		//用户账户名
    tstring		m_strPassword;		// 密码
    tstring		m_strNickName;		// 昵称
    tstring		m_strMarkName;		// 备注
    long		m_nTeamIndex;		// 分组索引
    UINT		m_nFace;			// 头像
    long		m_nStatus;			// 在线状态
    long        m_nClientType;      // 客户端类型
    tstring		m_strSign;			// 个性签名
    long		m_nGender;			// 性别
    long		m_nBirthday;		// 生日
    tstring		m_strMobile;		// 手机
    tstring		m_strAddress;		// 用户地址信息
    tstring		m_strEmail;
    BOOL		m_bUseCustomFace;		//是否使用自定义头像
    BOOL		m_bCustomFaceAvailable;	//自定义头像是否可用
    tstring		m_strCustomFace;	    // 自定有头像名（文件名不是路径，本地不存在需要从服务器下载）
    tstring		m_strRawCustomFace;		//原始自定义头像内容

    CStringA    m_strCustomFaceName;

    UINT		m_uMsgID;			//会话消息ID，初始值为0，只存在于内存中不存档
};

struct BuddyTeamInfo
{
    long	nTeamIndex;
    TCHAR	szTeamName[16];
};

class CBuddyTeamInfo		// 好友分组信息
{
public:
    CBuddyTeamInfo(void);
    ~CBuddyTeamInfo(void);

public:
    void Reset();
    int GetBuddyCount();
    int GetOnlineBuddyCount();
    CBuddyInfo* GetBuddy(int nIndex);
    void Sort();

public:
    int m_nIndex;			// 索引
    int m_nSort;			// 排列顺序
    tstring m_strName;		// 分组名称
    std::vector<CBuddyInfo*> m_arrBuddyInfo;
};

class COnlineBuddyInfo		// 在线好友信息
{
public:
    COnlineBuddyInfo(void);
    ~COnlineBuddyInfo(void);

public:
    void Reset();

public:
    UINT m_nUin;
    long m_nStatus;	// 在线状态
    int m_nClientType;		// 客户端类型
};

class CBuddyList	// 好友列表
{
public:
    CBuddyList(void);
    ~CBuddyList(void);

public:
    void Reset();
    int GetBuddyTeamCount();
    CBuddyTeamInfo* GetBuddyTeam(int nTeamIndex);
    CBuddyTeamInfo* GetBuddyTeamByIndex(int nIndex);
    int GetBuddyTotalCount();
    int GetBuddyCount(int nTeamIndex);
    int GetOnlineBuddyCount(int nTeamIndex);
    CBuddyInfo* GetBuddy(int nTeamIndex, int nIndex);
    CBuddyInfo* GetBuddy(UINT nUTalkUin);
    void SortBuddyTeam();
    void SortBuddy();
    BOOL AddBuddyTeam(CBuddyTeamInfo* lpBuddyTeamInfo);
    BOOL IsTeamNameExist(PCTSTR pszTeamName);

public:
    int m_nRetCode;
    std::vector<CBuddyTeamInfo*> m_arrBuddyTeamInfo;
};

class CGroupInfo			// 群信息
{
public:
    CGroupInfo(void);
    ~CGroupInfo(void);

public:
    void Reset();
    int GetMemberCount();						// 获取群成员总人数
    int GetOnlineMemberCount();					// 获取群成员在线人数
    CBuddyInfo* GetMember(int nIndex);			// 根据索引获取群成员信息
    CBuddyInfo* GetMemberByUin(UINT nUTalkUin);	// 根据内部ID获取群成员信息
    CBuddyInfo* GetMemberByAccount(PCTSTR pszAccountName);	// 根据账号获取群成员信息
    void Sort();								// 对群成员列表按在线状态进行排序
    BOOL AddMember(CBuddyInfo* lpBuddyInfo);	// 添加群成员
    BOOL DelAllMember();						// 删除所有群成员
    BOOL IsHasGroupNumber();
    BOOL IsHasGroupInfo();
    BOOL IsMember(UINT uAccountID);

public:
    UINT m_nGroupCode;		// 群账号ID
    UINT m_nGroupId;		// 群ID
    UINT m_nGroupNumber;	// 群号码
    tstring m_strAccount;	// 群账号
    tstring m_strName;		// 群名称
    tstring m_strMemo;		// 群公告
    tstring m_strFingerMemo;// 群简介
    UINT m_nOwnerUin;		// 群拥有者Uin
    UINT m_nCreateTime;		// 群创建时间
    int m_nFace;			// 群头像
    int m_nLevel;			// 群等级
    int m_nClass;			// 群分类索引
    UINT m_nOption;
    UINT m_nFlag;
    BOOL m_bHasGroupNumber;
    BOOL m_bHasGroupInfo;
    std::vector<CBuddyInfo*> m_arrMember;	// 群成员
};

class CGroupList	// 群列表
{
public:
    CGroupList(void);
    ~CGroupList(void);

public:
    void Reset();
    int GetGroupCount();							// 获取群总数
    CGroupInfo* GetGroup(int nIndex);				// 获取群信息(根据索引)
    CGroupInfo* GetGroupByCode(UINT nGroupCode);	// 获取群信息(根据群代码)
    CGroupInfo* GetGroupById(UINT nGroupId);		// 获取群信息(根据群Id)
    CBuddyInfo* GetGroupMemberByCode(UINT nGroupCode, UINT nUTalkUin);// 根据群代码和群成员UTalkUin获取群成员信息
    CBuddyInfo* GetGroupMemberById(UINT nGroupId, UINT nUTalkUin);	// 根据群Id和群成员UTalkUin获取群成员信息
    BOOL AddGroup(CGroupInfo* lpGroupInfo);		// 添加群
    UINT GetGroupCodeById(UINT nGroupId);			// 由群Id获取群代码
    UINT GetGroupIdByCode(UINT nGroupCode);			// 由群代码获取群Id

public:
    std::vector<CGroupInfo*> m_arrGroupInfo;
};

#define MAX_LAST_MSG_TEXT_LENGTH 18
#define MAX_RECENT_NAME_LENGTH	 32

struct RecentInfo
{
    int		nType;		//消息类型（好友聊天信息、群组、讨论组）
    UINT	uUserID;
    UINT	uFaceID;	//头像类型
    UINT64  uMsgTime;
    TCHAR   szName[MAX_RECENT_NAME_LENGTH];
    TCHAR   szLastMsgText[MAX_LAST_MSG_TEXT_LENGTH];
};

class CRecentInfo	// 最近联系人信息
{
public:
    CRecentInfo(void);
    ~CRecentInfo(void);

public:
    void Reset();

public:
    int		m_nType;		//消息类型（好友聊天信息、群组、讨论组）
    UINT	m_uUserID;
    UINT	m_uFaceID;		//头像类型
    UINT64  m_MsgTime;
    TCHAR   m_szName[MAX_RECENT_NAME_LENGTH];	//好友昵称或群名称
    TCHAR	m_szLastMsgText[MAX_LAST_MSG_TEXT_LENGTH];

};

class CRecentList			// 最近联系人列表
{
public:
    CRecentList(void);
    ~CRecentList(void);

public:
    void Reset();
    BOOL DeleteRecentItem(UINT uAccountID);
    BOOL AddRecent(CRecentInfo* lpRecentInfo);
    int GetRecentCount();
    CRecentInfo* GetRecent(int nIndex);

public:
    int m_nRetCode;
    std::vector<CRecentInfo*> m_arrRecentInfo;
};

class CFontInfo				// 字体信息
{
public:
    CFontInfo(void) : m_nSize(9), m_clrText(RGB(0, 0, 0)), m_strName(_T("微软雅黑")),
        m_bBold(FALSE), m_bItalic(FALSE), m_bUnderLine(FALSE){}
    ~CFontInfo(void){}

public:
    int m_nSize;			// 字体大小
    COLORREF m_clrText;		// 字体颜色
    tstring m_strName;		// 字体名称
    BOOL m_bBold;			// 是否加粗
    BOOL m_bItalic;			// 是否倾斜
    BOOL m_bUnderLine;		// 是否带下划线
};

class CCustomFaceInfo		// 自定义表情信息
{
public:
    CCustomFaceInfo(void) : m_nFileId(0), m_dwFileSize(0), m_bOnline(TRUE){}
    ~CCustomFaceInfo(void){}

public:
    tstring m_strName;			// 接收自定义表情使用参数(TODO: 这个字段先保留，不要用，留作以后扩展)
    UINT    m_nFileId;
    tstring m_strKey;
    tstring m_strServer;

    DWORD   m_dwFileSize;		// 发送自定义表情使用参数
    tstring m_strFileName;
    tstring m_strFilePath;
    BOOL	m_bOnline;			//当是文件类型时，该字段代表文件是离线文件还是在线文件
};

class CContent				// 消息内容
{
public:
    CContent(void) : m_nType(CONTENT_TYPE_UNKNOWN), m_nFaceId(0), m_nShakeTimes(0){}
    ~CContent(void){}

public:
    CONTENT_TYPE	m_nType;			// 内容类型
    CFontInfo		m_FontInfo;			// 字体信息
    tstring			m_strText;			// 文本信息
    int				m_nFaceId;			// 系统表情Id
    int				m_nShakeTimes;		// 窗口抖动次数
    CCustomFaceInfo m_CFaceInfo;		// 自定义表情信息（当发送的是m_nType，m_CFaceInfo是文件的有关信息）
};


class CBuddyMessage
{
public:
    CBuddyMessage(void);
    ~CBuddyMessage(void);

public:
    static BOOL IsType(LPCTSTR lpType);
    void		Reset();
    BOOL		Parse(Json::Value& JsonValue);
    BOOL		IsShakeWindowMsg();

public:
    CONTENT_TYPE			m_nMsgType;
    UINT					m_nMsgId;
    UINT					m_nMsgId2;
    UINT					m_nFromUin;
    UINT					m_nToUin;
    UINT					m_nReplyIp;
    UINT64					m_nTime;
    HWND					m_hwndFrom;
    tstring					m_strNickName;					//后门字段
    std::vector<CContent*> m_arrContent;
};

class CGroupMessage
{
public:
    CGroupMessage(void);
    ~CGroupMessage(void);

public:
    static BOOL IsType(LPCTSTR lpType);
    void Reset();
    BOOL Parse(Json::Value& JsonValue);

public:
    UINT	m_nMsgId;
    UINT	m_nMsgId2;
    UINT	m_nFromUin;
    UINT	m_nToUin;
    UINT	m_nMsgType;
    UINT	m_nReplyIp;
    UINT	m_nGroupCode;
    UINT	m_nSendUin;
    UINT	m_nSeq;
    UINT	m_nInfoSeq;
    UINT64	m_nTime;
    HWND	m_hwndFrom;
    std::vector<CContent*> m_arrContent;
};

class CSessMessage		// 临时消息
{
public:
    CSessMessage(void);
    ~CSessMessage(void);

public:
    static BOOL IsType(LPCTSTR lpType);
    void Reset();
    BOOL Parse(Json::Value& JsonValue);

public:
    UINT m_nMsgId;
    UINT m_nMsgId2;
    UINT m_nFromUin;
    UINT m_nToUin;
    UINT m_nMsgType;
    UINT m_nReplyIp;
    UINT m_nTime;
    UINT m_nGroupId;
    UINT m_nUTalkNum;
    UINT m_nServiceType;
    UINT m_nFlags;
    std::vector<CContent*> m_arrContent;
};

class CStatusChangeMessage	// 状态改变通知消息
{
public:
    CStatusChangeMessage(void);
    ~CStatusChangeMessage(void);

public:
    static BOOL IsType(LPCTSTR lpType);
    void Reset();
    BOOL Parse(Json::Value& JsonValue);

public:
    UINT m_nUTalkUin;
    long m_nStatus;	// 在线状态
    int m_nClientType;		// 客户端类型
};

class CKickMessage	// 被踢下线通知消息(例如号码在另一地点登录)
{
public:
    CKickMessage(void);
    ~CKickMessage(void);

public:
    static BOOL IsType(LPCTSTR lpType);
    void Reset();
    BOOL Parse(Json::Value& JsonValue);

public:
    UINT m_nMsgId;
    UINT m_nMsgId2;
    UINT m_nFromUin;
    UINT m_nToUin;
    UINT m_nMsgType;
    UINT m_nReplyIp;
    BOOL m_bShowReason;			// 是否显示被踢下线原因
    tstring m_strReason;		// 被踢下线原因
};

class CSysGroupMessage	// 群系统消息
{
public:
    CSysGroupMessage(void);
    ~CSysGroupMessage(void);

public:
    static BOOL IsType(LPCTSTR lpType);
    void Reset();
    BOOL Parse(Json::Value& JsonValue);

public:
    UINT m_nMsgId;
    UINT m_nMsgId2;
    UINT m_nFromUin;
    UINT m_nToUin;
    UINT m_nMsgType;
    UINT m_nReplyIp;
    tstring m_strSubType;
    UINT m_nGroupCode;
    UINT m_nGroupNumber;
    UINT m_nAdminUin;
    tstring m_strMsg;
    UINT m_nOpType;
    UINT m_nOldMember;
    tstring m_strOldMember;
    tstring m_strAdminUin;
    tstring m_strAdminNickName;
};

class CUpdateFriendInfoMsg	//更新好友信息
{
public:
    CUpdateFriendInfoMsg(void);
    ~CUpdateFriendInfoMsg(void);

public:
    static BOOL IsType(LPCTSTR lpType);
    void Reset();
    BOOL Parse(Json::Value& JsonValue);

public:
    UINT m_uMsgType;
    UINT m_uMsgId;
    UINT m_uMsgId2;
    UINT m_uFromUin;
    UINT m_uToUin;
    UINT m_uClientType;
    UINT64 m_uTime;
};

class CMessageSender	// 消息发送者
{
public:
    CMessageSender(long nType, UINT nSenderId, UINT nGroupCode);
    ~CMessageSender(void);

public:
    BOOL AddMsg(void* lpMsg);						// 添加消息
    BOOL DelMsg(int nIndex);						// 删除消息(根据索引)
    BOOL DelMsgById(UINT nMsgId);					// 删除消息(根据消息Id)
    BOOL DelAllMsg();								// 删除所有消息

    long GetMsgType();					// 获取消息类型
    UINT GetSenderId();								// 获取发送者Id
    int GetMsgCount();								// 获取消息总数
    int GetDisplayMsgCount();						// 除去图片上传完成的确认消息的条数
    UINT GetGroupCode();							// 获取群代码

    CBuddyMessage* GetBuddyMsg(int nIndex);		// 获取好友消息(根据索引)
    CBuddyMessage* GetGroupMsg(int nIndex);		// 获取群消息(根据索引)
    CSessMessage* GetSessMsg(int nIndex);			// 获取临时会话消息(根据索引)

    CBuddyMessage* GetBuddyMsgById(UINT nMsgId);	// 获取好友消息(根据消息Id)
    CBuddyMessage* GetGroupMsgById(UINT nMsgId);	// 获取群消息(根据消息Id)
    CSessMessage* GetSessMsgById(UINT nMsgId);		// 获取临时会话消息(根据消息Id)
    CSysGroupMessage* GetSysGroupMsgById(UINT nMsgId);// 获取群系统消息(根据消息Id)

private:
    long m_nType;
    UINT m_nSenderId;
    UINT m_nGroupCode;
    std::vector<void*> m_arrMsg;
};

class CMessageList	// 消息列表
{
public:
    CMessageList(void);
    ~CMessageList(void);

public:
    BOOL AddMsg(long nType, UINT nSenderId, UINT nGroupCode, void* lpMsg);		// 添加消息
    BOOL DelMsg(long nType, UINT nMsgId);										// 删除消息

    BOOL DelMsgSender(int nIndex);												// 删除消息发送者(根据索引)
    BOOL DelMsgSender(long nType, UINT nSenderId);								// 删除消息发送者(根据消息类型和发送者Id)
    BOOL DelAllMsgSender();									// 删除所有消息发送者

    int GetMsgSenderCount();								// 获取消息发送者总数
    CMessageSender* GetMsgSender(int nIndex);				// 获取消息发送者(根据索引)
    CMessageSender* GetMsgSender(long nType, UINT nSenderId);// 获取消息发送者(根据消息类型和发送者Id)
    CMessageSender* GetLastMsgSender();					// 获取最后一个发送消息过来的发送者

    CBuddyMessage* GetBuddyMsg(UINT nMsgId);				// 获取好友消息(根据消息Id)
    CBuddyMessage* GetGroupMsg(UINT nMsgId);				// 获取群消息(根据消息Id)
    CSessMessage* GetSessMsg(UINT nMsgId);					// 获取临时会话消息(根据消息Id)

    CBuddyMessage* GetBuddyMsg(UINT nUTalkUin, UINT nMsgId);	// 获取好友消息(根据发送者Id和消息Id)
    CBuddyMessage* GetGroupMsg(UINT nGroupCode, UINT nMsgId);	// 获取群消息(根据发送者Id和消息Id)
    CSessMessage* GetSessMsg(UINT nUTalkUin, UINT nMsgId);	// 获取临时会话消息(根据发送者Id和消息Id)

    struct MSG_IDENTITY
    {
        long nType;
        UINT nSenderId;
    };

private:
    void AddMsgIdentity(long nType, UINT nSenderId);
    void DelMsgIdentity(long nType, UINT nSenderId);

private:
    std::vector<CMessageSender*> m_arrMsgSender;
    std::vector<MSG_IDENTITY> m_arrMsgIdentity;
};

