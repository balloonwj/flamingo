// MainDlg.h : interface of the CMainDlg class
//
/////////////////////////////////////////////////////////////////////////////

#pragma once

#include "CustomMsgDef.h"
#include "FlamingoClient.h"
#include "TrayIcon.h"
#include "..\Common\Graphics\FaceList.h"
#include "..\Session\LoginAccountList.h"
#include "CascadeWinManager.h"
#include "../UI/BalloonAuth/BalloonLoginDlg.h"
//#include "VerifyCodeDlg.h"
#include "../UI/BalloonMain/BalloonMainPanel.h"
#include "../UI/BalloonMain/TrayFlashController.h"		// 托盘"新消息闪头像"状态机（m_trayFlash 成员需完整类型）
#include "../UI/BalloonMain/TrayMsgTipPopup.h"			// 托盘悬停未读消息提示浮窗（m_trayMsgTipPopup 成员需完整类型）
#include "../UI/BalloonChatWnd/BalloonChatWindow.h"
#include "../UI/BalloonChatWnd/BalloonGroupChatWindow.h"
#include "../UI/BalloonNetSettings/BalloonNetSettingsWindow.h"
#include "../UI/BalloonRegister/BalloonRegisterWindow.h"
#include "../UI/BalloonProfile/BalloonProfileWindow.h"
#include "../UI/BalloonContact/BalloonContactWindow.h"
#include "../UI/BalloonGroupManage/BalloonGroupManageWindow.h"
#include "../UI/BalloonHistory/BalloonHistoryWindow.h"
#include "../UI/BalloonSettings/BalloonSystemSettingDlg.h"
#include "../UI/BalloonMain/BalloonAboutDlg.h"
#include "../UI/BalloonMain/BalloonGroupInfoDlg.h"
#include "../UI/BalloonImageViewer/BalloonImageViewerWindow.h"
#include "../Feature/RemoteDesktop/RdSelfTestWindow.h"
#include "../Feature/RemoteDesktop/RdControlledStreamer.h"
#include "../UI/BalloonRemoteDesktop/BalloonRemoteDesktopDlg.h"
#include "../Feature/Call/BalloonCallDlg.h"
#include "../Feature/Call/CallMediaSession.h"
#include "../UI/BalloonRemoteDesktop/RdViewerWindow.h"
#include "../UI/BalloonRemoteDesktop/RdControlledBar.h"

//分组菜单ID基数
#define TEAM_MENU_ITEM_BASE       0xF001

//主面板状态
enum MAINPANEL_STATUS
{
    MAINPANEL_STATUS_NOTLOGIN,     //主面板未登录
    MAINPANEL_STATUS_LOGINING,     //主面板正在登录
    MAINPANEL_STATUS_LOGIN,        //主面板已经登录
    MAINPANEL_STATUS_RECONNECTING, //主面板正在重连
    MAINPANEL_STATUS_USERGOOFFLINE //主面板用户主动下线
};



// "无操作自动离开"的判定输入（定义在 BalloonMain/AutoAwayPolicy.h）。这里只按引用
// 传递，故前向声明即可，不必把该头文件拉进本头文件的所有包含方。
namespace balloonmain { struct AutoAwayInput; }

// 「禁止机器休眠」的系统电源请求持有者。本类按值持有它，需要完整类型，故直接包含
// 头文件；该头文件只依赖 <windows.h>，不会把额外依赖带给本头文件的包含方。
#include "../UI/BalloonMain/KeepAwakeSession.h"

class CMainDlg : public CDialogImpl<CMainDlg>, public CUpdateUI<CMainDlg>,
		public CMessageFilter, public CIdleHandler
{
public:
	CMainDlg(void);
	~CMainDlg(void);

	enum { IDD = IDD_MAINDLG };

	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual BOOL OnIdle();

	BEGIN_UPDATE_UI_MAP(CMainDlg)
	END_UPDATE_UI_MAP()

	BEGIN_MSG_MAP_EX(CMainDlg)
		MSG_WM_INITDIALOG(OnInitDialog)
		MSG_WM_SYSCOMMAND(OnSysCommand)
		MSG_WM_TIMER(OnTimer)
		MSG_WM_SIZE(OnSize)
		MSG_WM_HOTKEY(OnHotKey)
		MSG_WM_CLOSE(OnClose)
		MSG_WM_DESTROY(OnDestroy)
		COMMAND_ID_HANDLER_EX(ID_MENU_NETSETTINGS, OnMenu_NetSettings)			//打开网络设置窗（主面板系统菜单转发）
		

		
		COMMAND_ID_HANDLER_EX(ID_ABOUT, OnMainMenu_About)




		//显示昵称和账户、显示昵称、显示账号、显示清爽资料
        
        //网络错误
        MESSAGE_HANDLER_EX(FMG_MSG_NET_ERROR, OnNetError)

		MESSAGE_HANDLER_EX(FMG_MSG_LOGIN_RESULT, OnLoginResult)
		MESSAGE_HANDLER_EX(FMG_MSG_LOGOUT_RESULT, OnLogoutResult)

		//登录成功后那次静默检查更新的结果。只有查出可用新版本时才弹升级窗，
		//"已是最新"与"检查失败"一律不打扰用户。
		MESSAGE_HANDLER_EX(FMG_MSG_UPDATE_RESULT, OnSilentUpdateResult)

		//升级窗请求直接退出程序，好让更新器覆盖文件。不走 WM_CLOSE，见处理函数说明。
		MESSAGE_HANDLER_EX(FMG_MSG_EXIT_FOR_UPDATE, OnExitForUpdate)

		MESSAGE_HANDLER_EX(FMG_MSG_UPDATE_BUDDY_INFO, OnUpdateBuddyInfo)		 //更新用户信息
		MESSAGE_HANDLER_EX(FMG_MSG_UPDATE_BUDDY_NUMBER, OnUpdateBuddyNumber)	 //更新用户昵称(TODO: 名字要改)
		MESSAGE_HANDLER_EX(FMG_MSG_UPDATE_BUDDY_HEADPIC, OnUpdateBuddyHeadPic)
		MESSAGE_HANDLER_EX(FMG_MSG_UPDATE_BUDDY_SIGN, OnUpdateBuddySign)

		MESSAGE_HANDLER_EX(FMG_MSG_UPDATE_GROUP_LIST, OnUpdateGroupList)
		MESSAGE_HANDLER_EX(FMG_MSG_UPDATE_RECENT_LIST, OnUpdateRecentList)	 //更新最近联系人列表
		MESSAGE_HANDLER_EX(FMG_MSG_V2_CONTACTS, OnV2Contacts)					//v2 通讯录应答：填充联系人面板
		MESSAGE_HANDLER_EX(FMG_MSG_V2_CONVERSATIONS, OnV2Conversations)			//v2 会话列表应答：填充消息页
		MESSAGE_HANDLER_EX(FMG_MSG_V2_CREATE_GROUP_RESULT, OnV2CreateGroupResult)	//v2 建群应答：提示建群结果
		MESSAGE_HANDLER_EX(FMG_MSG_V2_GROUP_INFO, OnV2GroupInfo)				//v2 群资料应答：缓存群名/刷新群列表
		MESSAGE_HANDLER_EX(FMG_MSG_V2_GROUP_MEMBERS, OnV2GroupMembers)			//v2 群成员应答：路由到群聊窗
		MESSAGE_HANDLER_EX(FMG_MSG_V2_SET_CONVERSATION_RESULT, OnV2GroupWriteResult)	//v2 会话设置应答：日志/失败提示
		MESSAGE_HANDLER_EX(FMG_MSG_V2_SET_MEMBER_CARD_RESULT, OnV2GroupWriteResult)	//v2 改群名片应答：日志/失败提示
		MESSAGE_HANDLER_EX(FMG_MSG_V2_LEAVE_GROUP_RESULT, OnV2GroupWriteResult)		//v2 退群应答：日志/失败提示
		MESSAGE_HANDLER_EX(FMG_MSG_V2_SET_FAVORITE_RESULT, OnV2SetFavoriteResult)	//v2 收藏设置应答：失败时让主面板回滚乐观更新
		MESSAGE_HANDLER_EX(FMG_MSG_V2_SET_STARRED_RESULT, OnV2SetStarredResult)		//v2 特别关注设置应答：失败时让主面板按回带的 target 回滚那一个人的关注状态
		MESSAGE_HANDLER_EX(FMG_MSG_V2_SET_REMARK_RESULT, OnV2SetRemarkResult)		//v2 设置好友备注名应答：失败时让主面板按回带的 userid 回滚那一个人的备注
		MESSAGE_HANDLER_EX(FMG_MSG_V2_SET_TAGS_RESULT, OnV2SetTagsResult)		//v2 设置联系人标签应答：主面板拿回带的列表覆盖本地缓存（成功对齐归一结果、失败退回服务端真值）
		MESSAGE_HANDLER_EX(FMG_MSG_V2_GROUP_FILES, OnV2GroupFiles)				//v2 群文件列表应答：路由到群聊窗
		MESSAGE_HANDLER_EX(FMG_MSG_V2_GROUP_FILES_CHANGED, OnV2GroupFilesChanged)	//v2 群文件变更推送：路由到群聊窗
		MESSAGE_HANDLER_EX(FMG_MSG_V2_GROUP_FILE_UPLOAD_RESULT, OnV2GroupFileUploadResult)		//v2 群文件上传结果：路由到群聊窗
		MESSAGE_HANDLER_EX(FMG_MSG_V2_GROUP_FILE_DOWNLOAD_RESULT, OnV2GroupFileDownloadResult)	//v2 群文件下载结果：路由到群聊窗
		MESSAGE_HANDLER_EX(FMG_MSG_V2_GROUP_FILE_UPLOAD_PROGRESS, OnV2GroupFileUploadProgress)	//v2 群文件上传进度：路由到群聊窗
		MESSAGE_HANDLER_EX(FMG_MSG_V2_GROUP_FILE_DOWNLOAD_PROGRESS, OnV2GroupFileDownloadProgress)	//v2 群文件下载进度：路由到群聊窗
		MESSAGE_HANDLER_EX(FMG_MSG_V2_USER_GROUPS, OnV2UserGroups)				//v2 我的群列表应答：填群Tab
		MESSAGE_HANDLER_EX(FMG_MSG_V2_SYNC_MESSAGES, OnV2SyncMessages)			//v2 历史消息应答：路由到聊天窗渲染
		MESSAGE_HANDLER_EX(FMG_MSG_V2_CHANGE_PWD_RESULT, OnV2ChangePwdResult)	//v2 修改密码应答：转发到改密对话框
		MESSAGE_HANDLER_EX(FMG_MSG_V2_UPDATE_PROFILE_RESULT, OnV2UpdateProfileResult)	//v2 更新资料应答（个性签名）：转发到主面板
		MESSAGE_HANDLER_EX(FMG_MSG_V2_CHANGE_AVATAR_RESULT, OnV2ChangeAvatarResult)	//v2 修改头像应答：转发到修改头像对话框
		MESSAGE_HANDLER_EX(FMG_MSG_V2_AVATAR_UPLOAD_RESULT, OnV2AvatarUploadResult)	//v2 头像图片上传结果：转发到修改头像对话框
		MESSAGE_HANDLER_EX(FMG_MSG_V2_AVATAR_DOWNLOAD_RESULT, OnV2AvatarDownloadResult)	//v2 头像图片下载结果：转发到主面板刷新节点头像
		MESSAGE_HANDLER_EX(FMG_MSG_V2_USERINFO_CHANGED, OnV2UserInfoChanged)		//v2 联系人资料变更推送：转发到主面板实时刷新头像
		MESSAGE_HANDLER_EX(FMG_MSG_V2_ORG_STRUCTURE_CHANGED, OnV2OrgStructureChanged)	//v2 组织架构变更推送：转发到主面板重新拉通讯录
		MESSAGE_HANDLER_EX(FMG_MSG_CHANGE_MY_PRESENCE, OnChangeMyPresence)		//主面板状态菜单切换在线状态：据连接态走 GoOffline/GoOnline/SendChangePresence
		MESSAGE_HANDLER_EX(WM_WTSSESSION_CHANGE, OnSessionChange)			//锁屏/解锁等会话事件：锁屏立刻切离开、解锁切回在线（"无操作自动离开"的第二条触发路径）
		MESSAGE_HANDLER_EX(FMG_MSG_LOGOUT, OnLogout)					//托盘菜单/系统菜单登出：二次确认后断开连接、销毁主面板并回登录界面
		MESSAGE_HANDLER_EX(FMG_MSG_V2_RECALL_RESULT, OnV2RecallResult)			//v2 撤回消息应答：失败时提示原因，成功不动界面（等推送）
		MESSAGE_HANDLER_EX(FMG_MSG_V2_MESSAGE_RECALLED, OnV2MessageRecalled)		//v2 消息被撤回推送：转发到主面板改气泡/刷会话列表/标记本地库
		MESSAGE_HANDLER_EX(FMG_MSG_V2_PIN_RESULT, OnV2PinResult)				//v2 Pin 消息应答：失败时提示原因，成功不动界面（等推送）
		MESSAGE_HANDLER_EX(FMG_MSG_V2_MESSAGE_PINNED, OnV2MessagePinned)		//v2 消息被 Pin / 取消 Pin 推送：转发到主面板落本地标记并刷新消息记录窗
		MESSAGE_HANDLER_EX(FMG_MSG_V2_PINNED_MESSAGES, OnV2PinnedMessages)		//v2 被 Pin 消息全集应答：转发到主面板补齐本地库缺行并对账
		MESSAGE_HANDLER_EX(FMG_MSG_V2_ANNOUNCEMENTS, OnV2Announcements)			//v2 平台公告列表应答：转发到主面板落本地库 announce.db 并刷新铃铛角标
		MESSAGE_HANDLER_EX(FMG_MSG_V2_ANNOUNCEMENT_NOTIFY, OnV2AnnouncementNotify)	//v2 平台公告新发布/被撤回推送：转发到主面板落库、刷角标、刷消息盒子窗
		MESSAGE_HANDLER_EX(FMG_MSG_V2_ANN_MARK_READ_RESULT, OnV2AnnMarkReadResult)	//v2 平台公告已读上报应答：成功不动界面，失败时留痕
		MESSAGE_HANDLER_EX(FMG_MSG_V2_CHANGE_PRESENCE_RESULT, OnV2ChangePresenceResult)	//v2 修改在线状态应答：转发到主面板
		MESSAGE_HANDLER_EX(FMG_MSG_V2_PRESENCE_CHANGED, OnV2PresenceChanged)		//v2 好友在线状态变更推送：刷新联系人树状态点
		MESSAGE_HANDLER_EX(FMG_MSG_V2_KICKOUT, OnV2Kickout)				//v2 被踢下线推送：进入离线态、停自动重连并弹托盘气泡
		MESSAGE_HANDLER_EX(FMG_MSG_NETWORK_RESUMED, OnNetworkResumed)			//本机网络恢复：过自动重连守卫，非离线态才连回服务器
		MESSAGE_HANDLER_EX(FMG_MSG_V2_CONVERSATION_CHANGED, OnV2ConversationChanged)	//v2 会话属性多端同步推送：按新状态刷新会话列表
		MESSAGE_HANDLER_EX(FMG_MSG_V2_GROUP_OP_RESULT, OnV2GroupOpResult)				//v2 群管理写操作应答：失败时提示，成功不动界面（等推送）
		MESSAGE_HANDLER_EX(FMG_MSG_V2_GROUP_JOIN_REQUESTS, OnV2GroupJoinRequests)		//v2 待审批加群申请列表应答：填进群管理窗口的审批页
		MESSAGE_HANDLER_EX(FMG_MSG_V2_SENSITIVE_WORDS, OnV2SensitiveWords)				//v2 群敏感词库应答：填进群权限页的词库管理
		MESSAGE_HANDLER_EX(FMG_MSG_V2_GROUP_ANNOUNCEMENTS, OnV2GroupAnnouncements)		//v2 群公告列表应答：填进群管理窗公告页与群聊窗公告边栏
		MESSAGE_HANDLER_EX(FMG_MSG_V2_ANNOUNCEMENT_CHANGED, OnV2AnnouncementChanged)	//v2 群公告变更推送：刷新公告界面；新发布时还要弹一次提示
		MESSAGE_HANDLER_EX(FMG_MSG_V2_GROUP_MEMBER_CHANGED, OnV2GroupMemberChanged)		//v2 群成员变动推送：刷新群管理窗口与群聊窗成员边栏
		MESSAGE_HANDLER_EX(FMG_MSG_V2_GROUP_INFO_CHANGED, OnV2GroupInfoChanged)			//v2 群资料变更推送：刷新群名与各页；已解散时关窗
		MESSAGE_HANDLER_EX(FMG_MSG_V2_JOIN_GROUP_REQUEST, OnV2JoinGroupRequest)			//v2 有人申请加群推送：让审批入口出现待处理红点
		MESSAGE_HANDLER_EX(FMG_MSG_V2_RECV_CHATMSG, OnV2RecvChatMsg)			//v2 单聊收到新消息：路由到对应聊天窗
		MESSAGE_HANDLER_EX(FMG_MSG_V2_SEND_CHATMSG_ACK, OnV2SendChatAck)		//v2 单聊发送回执
		MESSAGE_HANDLER_EX(FMG_MSG_V2_RD_INVITE_ACK, OnV2RdInviteAck)			//v2 远程桌面邀请应答
		MESSAGE_HANDLER_EX(FMG_MSG_V2_RD_INVITE_NOTIFY, OnV2RdInviteNotify)		//v2 远程桌面邀请推送：被控端弹授权窗
		MESSAGE_HANDLER_EX(FMG_MSG_V2_RD_RELAY_INFO, OnV2RdRelayInfo)			//v2 远程桌面中转参数：连 imgserver 中转
		MESSAGE_HANDLER_EX(FMG_MSG_V2_RD_STATE_NOTIFY, OnV2RdStateNotify)		//v2 远程桌面状态推送
		MESSAGE_HANDLER_EX(FMG_MSG_RD_START_INVITE, OnRdStartInvite)			//聊天窗显示器按钮→协调器发起远程桌面
		MESSAGE_HANDLER_EX(FMG_MSG_RD_CONTROLLED_END_BY_USER, OnRdControlledEndByUser)	//被控端受控提示条"结束控制"→协调器收尾
		MESSAGE_HANDLER_EX(FMG_MSG_V2_CALL_INVITE_ACK, OnV2CallInviteAck)			//v2 音视频通话邀请应答
		MESSAGE_HANDLER_EX(FMG_MSG_V2_CALL_INVITE_NOTIFY, OnV2CallInviteNotify)		//v2 音视频通话邀请推送：被叫弹来电窗
		MESSAGE_HANDLER_EX(FMG_MSG_V2_CALL_SIGNALING, OnV2CallSignaling)			//v2 音视频通话中转参数：连 imgserver 中转
		MESSAGE_HANDLER_EX(FMG_MSG_V2_CALL_STATE_NOTIFY, OnV2CallStateNotify)		//v2 音视频通话状态推送
		MESSAGE_HANDLER_EX(FMG_MSG_V2_CALL_SWITCH_MEDIA_RESULT, OnV2CallSwitchMediaResult)	//v2 切换通话媒体应答：失败时提示原因
		MESSAGE_HANDLER_EX(FMG_MSG_V2_CALL_MEDIA_CHANGED, OnV2CallMediaChanged)		//v2 通话媒体已变更推送：重建通话窗媒体层 + 启停视频引擎
		MESSAGE_HANDLER_EX(FMG_MSG_CALL_STOP_MEDIA, OnCallStopMedia)			//通话窗关窗：停掉媒体会话，释放摄像头 / 麦克风
		MESSAGE_HANDLER_EX(FMG_MSG_CALL_START_INVITE, OnCallStartInvite)			//单聊窗"语音通话"按钮→协调器发起呼叫
		MESSAGE_HANDLER_EX(FMG_MSG_CALL_START_VIDEO_INVITE, OnCallStartVideoInvite)	//单聊窗"视频通话"按钮→协调器发起视频呼叫
		MESSAGE_HANDLER_EX(FMG_MSG_CALL_RELAY_EVENT, OnCallRelayEvent)			//通话中转会话事件(连通/透传校验/失败/断开)
		MESSAGE_HANDLER_EX(FMG_MSG_V2_IMAGE_SEND_PROGRESS, OnV2ImageSendProgress)	//v2 发图上传进度：路由到聊天窗更新气泡进度
		MESSAGE_HANDLER_EX(FMG_MSG_V2_IMAGE_SEND_RESULT, OnV2ImageSendResult)		//v2 发图最终结果：路由到聊天窗收尾气泡
		MESSAGE_HANDLER_EX(FMG_MSG_V2_IMAGE_DOWNLOAD_RESULT, OnV2ImageDownloadResult)	//v2 收图下载结果：路由到聊天窗回填渲染
		MESSAGE_HANDLER_EX(FMG_MSG_V2_FILE_SEND_PROGRESS, OnV2FileSendProgress)		//v2 发文件上传进度：路由到聊天窗更新文件气泡进度
		MESSAGE_HANDLER_EX(FMG_MSG_V2_FILE_SEND_RESULT, OnV2FileSendResult)			//v2 发文件最终结果：路由到聊天窗收尾文件气泡
		MESSAGE_HANDLER_EX(FMG_MSG_V2_FILE_DOWNLOAD_PROGRESS, OnV2FileDownloadProgress)	//v2 收文件下载进度：路由到聊天窗更新进度
		MESSAGE_HANDLER_EX(FMG_MSG_V2_FILE_DOWNLOAD_RESULT, OnV2FileDownloadResult)	//v2 收文件下载结果：路由到聊天窗回填渲染
		MESSAGE_HANDLER_EX(FMG_MSG_V2_FILE_TRANSFER_ACK, OnV2FileTransferAck)		//v2 对方收取本端所发文件的状态：路由到聊天窗更新文件气泡状态行
		//——v2 单聊文件夹传输(cmd_folder_* 1810~1817)：每条都先交 FolderTransferManager
		//  处理会话生命周期，再路由到聊天窗更新那条文件夹气泡——
		MESSAGE_HANDLER_EX(FMG_MSG_V2_FOLDER_INVITE_ACK, OnV2FolderInviteAck)		//v2 文件夹邀请应答：绑定服务端会话 id，或按 code 把气泡标失败
		MESSAGE_HANDLER_EX(FMG_MSG_V2_FOLDER_INVITE_NOTIFY, OnV2FolderInviteNotify)	//v2 文件夹邀请推送：接收端会话里出一条带「接收 / 拒绝」的邀请气泡
		MESSAGE_HANDLER_EX(FMG_MSG_V2_FOLDER_SESSION_ACK, OnV2FolderSessionAck)		//v2 文件夹通用应答（应答 / 撤回 / 结束 / 重试四条共用）
		MESSAGE_HANDLER_EX(FMG_MSG_V2_FOLDER_STATE_NOTIFY, OnV2FolderStateNotify)	//v2 文件夹状态推送：按 state 把气泡切成拒绝 / 取消 / 失败 / 完成等
		MESSAGE_HANDLER_EX(FMG_MSG_V2_FOLDER_RELAY_INFO, OnV2FolderRelayInfo)		//v2 文件夹中转参数：起收发状态机，气泡转传输中
		MESSAGE_HANDLER_EX(FMG_MSG_V2_FOLDER_SCANNED, OnV2FolderScanned)			//本地目录扫描完成：把文件数 / 目录数 / 总大小补进气泡
		MESSAGE_HANDLER_EX(FMG_MSG_V2_FOLDER_SEND_PROGRESS, OnV2FolderSendProgress)	//文件夹发送进度：更新气泡进度条与状态行
		MESSAGE_HANDLER_EX(FMG_MSG_V2_FOLDER_SEND_RESULT, OnV2FolderSendResult)		//文件夹发送结果：收尾气泡并回调管理器停线程、发 cmd_folder_close
		MESSAGE_HANDLER_EX(FMG_MSG_V2_FOLDER_RECV_PROGRESS, OnV2FolderRecvProgress)	//文件夹接收进度：同上
		MESSAGE_HANDLER_EX(FMG_MSG_V2_FOLDER_RECV_RESULT, OnV2FolderRecvResult)		//文件夹接收结果：同上，成功时把落地目录写回气泡供「打开文件夹」
		MESSAGE_HANDLER_EX(FMG_MSG_V2_TYPING_NOTIFY, OnV2TypingNotify)				//v2 对端正在/停止输入：路由到对应单聊窗刷新头部提示
		MESSAGE_HANDLER_EX(FMG_MSG_TRAY_FLASH, OnV2TrayFlash)					//v2 有新消息：托盘闪发送方头像
		MESSAGE_HANDLER_EX(FMG_MSG_TRAY_CLEAR, OnV2TrayClear)					//v2 某会话已读：从托盘闪烁集合移除
		MESSAGE_HANDLER_EX(FMG_MSG_TRAY_MINIMIZE_TIP, OnTrayMinimizeTip)		//主面板首次最小化隐藏到托盘：弹一次托盘气泡提示
		MESSAGE_HANDLER_EX(FMG_MSG_STATUS_CHANGE_MSG, OnStatusChangeMsg)
		MESSAGE_HANDLER_EX(FMG_MSG_KICK_MSG, OnKickMsg)
        
		MESSAGE_HANDLER_EX(FMG_MSG_SYS_GROUP_MSG, OnSysGroupMsg)
		
		MESSAGE_HANDLER_EX(FMG_MSG_UPDATE_GMEMBER_NUMBER, OnUpdateGMemberNumber)
		MESSAGE_HANDLER_EX(FMG_MSG_UPDATE_GROUP_NUMBER, OnUpdateGroupNumber)
		
		MESSAGE_HANDLER_EX(FMG_MSG_UPDATE_GMEMBER_SIGN, OnUpdateGMemberSign)
		MESSAGE_HANDLER_EX(FMG_MSG_UPDATE_GMEMBER_INFO, OnUpdateGMemberInfo)
		MESSAGE_HANDLER_EX(FMG_MSG_UPDATE_GROUP_INFO, OnUpdateGroupInfo)				//更新群信息
		MESSAGE_HANDLER_EX(FMG_MSG_UPDATE_GMEMBER_HEADPIC, OnUpdateGMemberHeadPic)
		MESSAGE_HANDLER_EX(FMG_MSG_UPDATE_GROUP_HEADPIC, OnUpdateGroupHeadPic)
		MESSAGE_HANDLER_EX(FMG_MSG_CHANGE_STATUS_RESULT, OnChangeStatusResult)

		MESSAGE_HANDLER_EX(WM_TRAYICON_NOTIFY, OnTrayIconNotify)
		MESSAGE_HANDLER_EX(WM_SHOW_BUDDYCHATDLG, OnShowOrCloseDlg)				//显示或关闭与好友聊天对话框
		MESSAGE_HANDLER_EX(WM_SHOW_GROUPCHATDLG, OnShowOrCloseDlg)
		MESSAGE_HANDLER_EX(WM_SHOW_SESSCHATDLG, OnShowOrCloseDlg)
		MESSAGE_HANDLER_EX(WM_SHOW_SYSGROUPCHATDLG, OnShowOrCloseDlg)
		MESSAGE_HANDLER_EX(WM_SHOW_BUDDYINFODLG, OnShowOrCloseDlg)
		MESSAGE_HANDLER_EX(WM_SHOW_GMEMBERINFODLG, OnShowOrCloseDlg)
		MESSAGE_HANDLER_EX(WM_SHOW_GROUPINFODLG, OnShowOrCloseDlg)
		MESSAGE_HANDLER_EX(WM_CLOSE_BUDDYCHATDLG, OnShowOrCloseDlg)
		MESSAGE_HANDLER_EX(WM_CLOSE_GROUPCHATDLG, OnShowOrCloseDlg)
		MESSAGE_HANDLER_EX(WM_CLOSE_SESSCHATDLG, OnShowOrCloseDlg)
		MESSAGE_HANDLER_EX(WM_CLOSE_SYSGROUPCHATDLG, OnShowOrCloseDlg)
		MESSAGE_HANDLER_EX(WM_CLOSE_BUDDYINFODLG, OnShowOrCloseDlg)
		MESSAGE_HANDLER_EX(WM_CLOSE_GMEMBERINFODLG, OnShowOrCloseDlg)
		MESSAGE_HANDLER_EX(WM_CLOSE_GROUPINFODLG, OnShowOrCloseDlg)
		MESSAGE_HANDLER_EX(WM_CLOSE_MAINDLG, OnCloseDlg)						//关闭主对话框
		MESSAGE_HANDLER_EX(WM_CANCEL_FLASH, OnCancelFlash)

		REFLECT_NOTIFICATIONS()													//消息反射通知宏
	END_MSG_MAP()

private:
	BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
	void OnSysCommand(UINT nID, CPoint pt);
	void OnTimer(UINT_PTR nIDEvent);
	void OnSize(UINT nType, CSize size);
	void OnHotKey(int nHotKeyID, UINT uModifiers, UINT uVirtKey);
	void OnClose();
	void OnDestroy();
	LRESULT OnTrayIconNotify(UINT uMsg, WPARAM wParam, LPARAM lParam);
	void OnMenu_ShowMainPanel(UINT uNotifyCode, int nID, CWindow wndCtl);
	void OnMenu_Exit(UINT uNotifyCode, int nID, CWindow wndCtl);
	void OnMenu_Mute(UINT uNotifyCode, int nID, CWindow wndCtl);
	// 打开新版网络设置窗 —— 主面板系统菜单"网络设置"项经 WM_COMMAND 转发到此。
	void OnMenu_NetSettings(UINT uNotifyCode, int nID, CWindow wndCtl);
	
	void OnMainMenu_About(UINT uNotifyCode, int nID, CWindow wndCtl);			// 主菜单关于菜单点击
	void OnMenu_Status(UINT uNotifyCode, int nID, CWindow wndCtl);				// “用户在线状态”菜单






	void CloseDialog(int nVal);
	void OnFindBtn(UINT uNotifyCode, int nID, CWindow wndCtl);



    //返回true,表示登录对话框走的是正常登录流程;返回false,表示直接关闭了登录对话框
	bool StartLogin(BOOL bAutoLogin = FALSE);

	// 断开连接、关掉所有窗口、清空登录态并重新弹出登录窗 —— 用户主动登出(OnLogout)与
	// 账号在别处登录被顶下线(OnV2Kickout)共用的收尾序列。调用方须跑在由 PostMessage
	// 投递而来的干净调用栈上：本函数要销毁主面板、并进入登录窗的模态循环。
	//   exitKind：      本次退出登录的方式，取值见 balloonmain::LoginExitKind。
	//   kickoutReason： 被踢原因，取值见 balloonmain::KickoutReasonValue；exitKind 为
	//                   主动登出时本参数被忽略，传 kKickoutReasonUnknown 即可。
	// 这两个参数只用于判定要不要顺手清掉本机的"自动登录"标志(见
	// balloonmain::ShouldClearAutoLoginOnExit)，不影响其余任何收尾动作。
	void ReturnToLoginWindow(int exitKind, int kickoutReason);

	/**
	 *  清掉某账号在本机存下的"自动登录"标志并立即落盘。
	 *
	 *  收口在一处而不是各调用点各写一遍：登出、被踢下线(密码已修改)两条路径要做的
	 *  是同一件事，散着写迟早漏掉一条。
	 *    lpszAccount：账号名；NULL、空串、或该账号不在历史列表里时只记日志、不做事。
	 *  无返回值；本就没勾自动登录时不写盘。落盘失败由 SaveLoginAccountListFile 记日志。
	 */
	void ClearAutoLoginFlag(LPCTSTR lpszAccount);

	/**
	 *  忘掉当前账号在本机存下的密码 —— 清空密码，并把"记住密码"与"自动登录"一并
	 *  取消，随后落盘。
	 *
	 *  用在服务端明确回了"密码错误"之后：存下的密码既然已经不对，留着只会让勾了
	 *  自动登录的用户每次开机被自动登一次、再被弹一次"密码错误"，而在那之前他根本
	 *  没有界面可以取消勾选，等于卡死在这个循环里。"记住密码"一并取消，是因为密码
	 *  框已经没有可回填的内容，勾着一个空密码没有意义。
	 *  当前账号为空或不在历史列表里时只记日志、不做事。
	 */
	void ForgetSavedPasswordForCurrentAccount();

	/**
	 *  把历史登录账号列表写回 Bin\Users\LoginAccountList.dat。
	 *  登录成功记下账号 / 姓名后、以及用户改了昵称后调用，使登录窗下次
	 *  打开时能按账号显示对应的姓名。无返回值，写失败只记日志。
	 */
	void SaveLoginAccountListFile();

	/**
	 *  把当前登录账号的昵称同步进历史登录账号列表 —— 与列表里存的姓名
	 *  不一致时才写入并落盘（用户刚改过昵称的情形）。当前账号不在列表里
	 *  （尚未登录成功过）时什么都不做。
	 */
	void SyncNickNameToLoginAccountList();

    LRESULT OnNetError(UINT uMsg, WPARAM wParam, LPARAM lParam);    
	LRESULT OnLoginResult(UINT uMsg, WPARAM wParam, LPARAM lParam);			// 登录返回消息

	// 登录成功后那次静默检查更新的结果。lParam 是堆上的 update::UpdateTaskResult*，
	// 本函数处理完后 delete。只有查出可用新版本时才拉起升级窗；已是最新或检查失败
	// 都只记日志，不打扰用户——他并没有主动要求检查。
	LRESULT OnSilentUpdateResult(UINT uMsg, WPARAM wParam, LPARAM lParam);

	// 升级窗请求退出程序（升级包已就绪，更新器正等着覆盖文件）。直接走 CloseDialog，
	// 不弹「退出还是最小化」那个提示框——那个框在这里是有害的：用户选「最小化」的话
	// 程序不退出，更新器等 30 秒后放弃，升级白做一场，而用户还以为在升级。
	LRESULT OnExitForUpdate(UINT uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT OnLogoutResult(UINT uMsg, WPARAM wParam, LPARAM lParam);		// 注销返回消息
	LRESULT OnGoOffline(UINT uMsg, WPARAM wParam, LPARAM lParam);			// 下线
	LRESULT OnUpdateUserInfo(UINT uMsg, WPARAM wParam, LPARAM lParam);		// 更新用户信息
	LRESULT OnUpdateGroupList(UINT uMsg, WPARAM wParam, LPARAM lParam);		// 更新群列表消息
	LRESULT OnUpdateRecentList(UINT uMsg, WPARAM wParam, LPARAM lParam);	// 更新最近联系人列表消息
	LRESULT OnV2Contacts(UINT uMsg, WPARAM wParam, LPARAM lParam);		// v2 通讯录应答(lParam=net::ContactsResult*，处理后 delete)
	LRESULT OnV2Conversations(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 会话列表应答(lParam=net::ConversationsResult*，处理后 delete)
	LRESULT OnV2CreateGroupResult(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 建群应答(lParam=net::CreateGroupResponse*，处理后 delete)
	LRESULT OnV2GroupInfo(UINT uMsg, WPARAM wParam, LPARAM lParam);			// v2 群资料应答(lParam=net::GroupInfoResult*，处理后 delete)
	LRESULT OnV2GroupMembers(UINT uMsg, WPARAM wParam, LPARAM lParam);		// v2 群成员应答(lParam=net::GroupMembersResult*，处理后 delete)
	LRESULT OnV2GroupWriteResult(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 群写操作通用应答(置顶/免打扰/改群名片/退群)：wParam=code，lParam=std::string* msg(处理后 delete)
	// v2 收藏设置应答（cmd_set_favorite）。lParam 为堆上的 net::SetFavoriteResult*，
	// 本方法负责 delete。转交主面板：成功只记日志，失败要按应答里回带的
	// ctype/cid/favorite 把那一条的乐观更新回滚掉。
	LRESULT OnV2SetFavoriteResult(UINT uMsg, WPARAM wParam, LPARAM lParam);
	// v2 特别关注设置应答（cmd_set_starred）。lParam 为堆上的 net::SetStarredResult*，
	// 本方法负责 delete。转交主面板：成功只记日志（乐观更新在发起时已生效），失败要按
	// 应答里回带的 target/starred 把那一个人的关注状态回滚，并按 code 提示原因
	// （典型是关注人数已达上限）。
	LRESULT OnV2SetStarredResult(UINT uMsg, WPARAM wParam, LPARAM lParam);
	// v2 设置好友备注名应答（cmd_set_friend_remark）。lParam 为堆上的
	// net::SetFriendRemarkResult*，本方法负责 delete。转交主面板：成功只记日志
	// （乐观更新在发起时已生效），失败要按应答里回带的 userid 把那一个人的备注退回去。
	LRESULT OnV2SetRemarkResult(UINT uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT OnV2SetTagsResult(UINT uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT OnV2GroupFiles(UINT uMsg, WPARAM wParam, LPARAM lParam);			// v2 群文件列表应答(lParam=net::GetGroupFilesResult*，处理后 delete)
	LRESULT OnV2GroupFilesChanged(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 群文件变更推送(lParam=net::GroupFilesChangedNotify*，处理后 delete)
	LRESULT OnV2GroupFileUploadResult(UINT uMsg, WPARAM wParam, LPARAM lParam);		// v2 群文件上传结果(lParam=net::GroupFileUploadResult*，处理后 delete)
	LRESULT OnV2GroupFileDownloadResult(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 群文件下载结果(lParam=net::GroupFileDownloadResult*，处理后 delete)
	LRESULT OnV2GroupFileUploadProgress(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 群文件上传进度(lParam=net::GroupFileUploadProgress*，处理后 delete)
	LRESULT OnV2GroupFileDownloadProgress(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 群文件下载进度(lParam=net::GroupFileDownloadProgress*，处理后 delete)
	LRESULT OnV2UserGroups(UINT uMsg, WPARAM wParam, LPARAM lParam);			// v2 我的群列表应答(lParam=net::UserGroupsResult*，处理后 delete)
	LRESULT OnV2SyncMessages(UINT uMsg, WPARAM wParam, LPARAM lParam);		// v2 历史消息应答(lParam=net::SyncMessagesResult*，处理后 delete)
	LRESULT OnV2ChangePwdResult(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 修改密码应答(lParam=net::ChangePasswordResponse*，处理后 delete)
	LRESULT OnV2UpdateProfileResult(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 更新资料应答-个性签名(lParam=net::UpdateProfileResponse*，处理后 delete)
	LRESULT OnV2ChangeAvatarResult(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 修改头像应答(lParam=net::UpdateProfileResponse*，处理后 delete)
	LRESULT OnV2AvatarUploadResult(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 头像图片上传结果(lParam=net::AvatarUploadResult*，处理后 delete)
	LRESULT OnV2AvatarDownloadResult(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 头像图片下载结果(lParam=net::AvatarDownloadResult*，处理后 delete)
	LRESULT OnV2UserInfoChanged(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 联系人资料变更推送(lParam=net::UserInfoChangedNotify*，处理后 delete)
	LRESULT OnV2OrgStructureChanged(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 组织架构变更推送(lParam=net::OrgStructureChangedNotify*，处理后 delete)
	LRESULT OnChangeMyPresence(UINT uMsg, WPARAM wParam, LPARAM lParam);		// 切换我的在线状态(wParam=v2 presence，lParam=切换来源见 PresenceChangeSource)：据连接态走 GoOffline/GoOnline/SendChangePresence
	LRESULT OnSessionChange(UINT uMsg, WPARAM wParam, LPARAM lParam);		// 锁屏/解锁等会话事件(wParam=WTS_* 状态码)：锁屏立刻切离开、解锁切回在线
	LRESULT OnLogout(UINT uMsg, WPARAM wParam, LPARAM lParam);			// 登出(wParam/lParam 均为 0)：二次确认后断开三条连接、关掉所有子窗口、销毁主面板并重新弹出登录窗
	LRESULT OnV2RecallResult(UINT uMsg, WPARAM wParam, LPARAM lParam);		// v2 撤回消息应答(lParam=net::RecallMessageResponse*，处理后 delete)
	LRESULT OnV2MessageRecalled(UINT uMsg, WPARAM wParam, LPARAM lParam);		// v2 消息被撤回推送(lParam=net::MessageRecalledNotify*，处理后 delete)
	LRESULT OnV2PinResult(UINT uMsg, WPARAM wParam, LPARAM lParam);				// v2 Pin 消息应答(lParam=net::PinMessageResponse*，处理后 delete)：仅失败时据 code 提示原因
	LRESULT OnV2MessagePinned(UINT uMsg, WPARAM wParam, LPARAM lParam);			// v2 消息被 Pin / 取消 Pin 推送(lParam=net::MessagePinnedNotify*，处理后 delete)
	LRESULT OnV2Announcements(UINT uMsg, WPARAM wParam, LPARAM lParam);			// v2 平台公告列表应答(lParam=net::AnnouncementsResult*，处理后 delete)：转给主面板落本地库并刷角标
	LRESULT OnV2AnnouncementNotify(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 平台公告新发布 / 被撤回推送(lParam=net::AnnouncementNotify*，处理后 delete)
	LRESULT OnV2AnnMarkReadResult(UINT uMsg, WPARAM wParam, LPARAM lParam);		// v2 平台公告已读上报应答(lParam=net::AnnounceMarkReadResult*，处理后 delete)：成功不动界面
	LRESULT OnV2PinnedMessages(UINT uMsg, WPARAM wParam, LPARAM lParam);		// v2 被 Pin 消息全集应答(lParam=net::PinnedMessagesResult*，处理后 delete)
	LRESULT OnV2ChangePresenceResult(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 修改在线状态应答(lParam=net::ChangePresenceResponse*，处理后 delete)
	LRESULT OnV2PresenceChanged(UINT uMsg, WPARAM wParam, LPARAM lParam);		// v2 好友在线状态变更推送(lParam=net::PresenceChangedNotify*，处理后 delete)
	// v2 被踢下线推送(cmd_kickout)。wParam=被踢原因(balloonmain::KickoutReasonValue)，lParam=0，无堆负载。
	// 典型场景：同一账号在另一台 PC 上登录，本端被服务端顶下线。处理动作与用户主动选"离线"
	// 完全一致(停重连定时器、断连接、状态胶囊与托盘转离线)，额外多弹一条托盘气泡说明原因。
	LRESULT OnV2Kickout(UINT uMsg, WPARAM wParam, LPARAM lParam);
	// 本机网络由断开恢复（wParam/lParam 均为 0，无堆负载）。要不要趁机连回服务器须过
	// balloonmain::ShouldAutoReconnect 这道守卫：用户主动离线、被踢下线时一律不连。
	LRESULT OnNetworkResumed(UINT uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT OnV2ConversationChanged(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 会话属性多端同步推送(lParam=net::ConversationChangedNotify*，处理后 delete)
	LRESULT OnV2GroupOpResult(UINT uMsg, WPARAM wParam, LPARAM lParam);			// v2 群管理写操作应答(wParam=cmd 协议号, lParam=net::GroupOpResult*，处理后 delete)
	LRESULT OnV2GroupJoinRequests(UINT uMsg, WPARAM wParam, LPARAM lParam);		// v2 待审批加群申请列表应答(lParam=net::GroupJoinRequestsResult*，处理后 delete)
	LRESULT OnV2SensitiveWords(UINT uMsg, WPARAM wParam, LPARAM lParam);		// v2 群敏感词库应答(lParam=net::SensitiveWordsResult*，处理后 delete)
	LRESULT OnV2GroupAnnouncements(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 群公告列表应答(lParam=net::GroupAnnouncementsResult*，处理后 delete)
	LRESULT OnV2AnnouncementChanged(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 群公告变更推送(lParam=net::GroupAnnouncementNotify*，处理后 delete)
	LRESULT OnV2GroupMemberChanged(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 群成员变动推送(lParam=net::GroupMemberChangedNotify*，处理后 delete)
	LRESULT OnV2GroupInfoChanged(UINT uMsg, WPARAM wParam, LPARAM lParam);		// v2 群资料变更推送(lParam=net::GroupInfoChangedNotify*，处理后 delete)
	LRESULT OnV2JoinGroupRequest(UINT uMsg, WPARAM wParam, LPARAM lParam);		// v2 有人申请加群推送(lParam=net::GroupJoinRequestItem*，处理后 delete)
	LRESULT OnV2RecvChatMsg(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 单聊收到新消息(lParam=net::MessageNotify*，处理后 delete)
	LRESULT OnV2SendChatAck(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 单聊发送回执(lParam=net::SendMessageResponse*，处理后 delete)
	LRESULT OnV2RdInviteAck(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 远程桌面邀请应答(lParam=net::RDInviteResponse*，处理后 delete)
	LRESULT OnV2RdInviteNotify(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 远程桌面邀请推送(lParam=net::RDInviteNotify*，处理后 delete)
	LRESULT OnV2RdRelayInfo(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 远程桌面中转参数(lParam=net::RDRelayInfo*，处理后 delete)
	LRESULT OnV2RdStateNotify(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 远程桌面状态推送(lParam=net::RDStateNotify*，处理后 delete)
	LRESULT OnV2CallInviteAck(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 音视频通话邀请应答(lParam=net::CallInviteResponse*，处理后 delete)
	LRESULT OnV2CallInviteNotify(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 音视频通话邀请推送(lParam=net::CallInviteNotify*，处理后 delete)
	LRESULT OnV2CallSignaling(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 音视频通话中转参数(lParam=net::CallSignalingInfo*，处理后 delete)
	LRESULT OnV2CallStateNotify(UINT uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT OnV2CallSwitchMediaResult(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 切换通话媒体应答(lParam=net::CallSwitchMediaResponse*，处理后 delete)
	LRESULT OnV2CallMediaChanged(UINT uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT OnCallStopMedia(UINT uMsg, WPARAM wParam, LPARAM lParam);		// 通话窗关窗请求停媒体会话(wParam/lParam 均为 0)		// v2 通话媒体已变更推送(lParam=net::CallMediaChangedNotify*，处理后 delete)	// v2 音视频通话状态推送(lParam=net::CallStateNotify*，处理后 delete)
	LRESULT OnCallStartInvite(UINT uMsg, WPARAM wParam, LPARAM lParam);	// 单聊窗发起语音通话(wParam=对端userid，lParam=堆 std::wstring* peerName，处理后 delete)
	LRESULT OnCallStartVideoInvite(UINT uMsg, WPARAM wParam, LPARAM lParam);	// 单聊窗发起视频通话(wParam=对端userid，lParam=堆 std::wstring* peerName，处理后 delete)
	LRESULT OnCallRelayEvent(UINT uMsg, WPARAM wParam, LPARAM lParam);	// 通话中转会话事件(wParam=call::CallRelayEvent 事件码，lParam=序号；无堆指针)
	LRESULT OnRdStartInvite(UINT uMsg, WPARAM wParam, LPARAM lParam);	// 聊天窗发起远程桌面(wParam=对端userid，lParam=堆 std::wstring* peerName，处理后 delete)
	LRESULT OnRdControlledEndByUser(UINT uMsg, WPARAM wParam, LPARAM lParam);	// 被控端用户点受控条"结束控制"：发 cmd_rd_close + 停推流 + 关受控条(无参)
	LRESULT OnV2ImageSendProgress(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 发图上传进度(lParam=net::ImageSendProgress*，处理后 delete)
	LRESULT OnV2ImageSendResult(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 发图最终结果(lParam=net::ImageSendResult*，处理后 delete)
	LRESULT OnV2ImageDownloadResult(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 收图下载结果(lParam=net::ImageDownloadResult*，处理后 delete)
	LRESULT OnV2FileSendProgress(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 发文件上传进度(lParam=net::FileSendProgress*，处理后 delete)
	LRESULT OnV2FileSendResult(UINT uMsg, WPARAM wParam, LPARAM lParam);		// v2 发文件最终结果(lParam=net::FileSendResult*，处理后 delete)
	LRESULT OnV2FileDownloadProgress(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 收文件下载进度(lParam=net::FileDownloadProgress*，处理后 delete)
	LRESULT OnV2FileDownloadResult(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 收文件下载结果(lParam=net::FileDownloadResult*，处理后 delete)
	LRESULT OnV2FileTransferAck(UINT uMsg, WPARAM wParam, LPARAM lParam);		// v2 对方收取本端所发文件的状态(lParam=net::FileTransferAckNotify*，处理后 delete)
	// ---- v2 单聊文件夹传输：会话生命周期交 foldertransfer::FolderTransferManager，
	//      界面更新交主面板路由到对应聊天窗；每个处理器都负责 delete 自己的堆负载 ----
	LRESULT OnV2FolderInviteAck(UINT uMsg, WPARAM wParam, LPARAM lParam);		// v2 文件夹邀请应答(wParam=本次邀请请求的帧 seq，管理器按它对回是哪一笔发起的；lParam=net::FolderInviteResponse*，处理后 delete)
	LRESULT OnV2FolderInviteNotify(UINT uMsg, WPARAM wParam, LPARAM lParam);	// v2 文件夹邀请推送(lParam=net::FolderInviteNotify*，处理后 delete)
	LRESULT OnV2FolderSessionAck(UINT uMsg, WPARAM wParam, LPARAM lParam);		// v2 文件夹通用应答(wParam=命令协议号，lParam=net::FolderSessionResponse*，处理后 delete)
	LRESULT OnV2FolderStateNotify(UINT uMsg, WPARAM wParam, LPARAM lParam);		// v2 文件夹状态推送(lParam=net::FolderStateNotify*，处理后 delete)
	LRESULT OnV2FolderRelayInfo(UINT uMsg, WPARAM wParam, LPARAM lParam);		// v2 文件夹中转参数(lParam=net::FolderRelayInfo*，处理后 delete)
	LRESULT OnV2FolderScanned(UINT uMsg, WPARAM wParam, LPARAM lParam);			// 本地目录扫描完成(lParam=foldertransfer::FolderScanNotice*，处理后 delete)
	LRESULT OnV2FolderSendProgress(UINT uMsg, WPARAM wParam, LPARAM lParam);	// 文件夹发送进度(lParam=foldertransfer::FolderProgressNotice*，处理后 delete)
	LRESULT OnV2FolderSendResult(UINT uMsg, WPARAM wParam, LPARAM lParam);		// 文件夹发送结果(lParam=foldertransfer::FolderResultNotice*，处理后 delete)
	LRESULT OnV2FolderRecvProgress(UINT uMsg, WPARAM wParam, LPARAM lParam);	// 文件夹接收进度(lParam=foldertransfer::FolderProgressNotice*，处理后 delete)
	LRESULT OnV2FolderRecvResult(UINT uMsg, WPARAM wParam, LPARAM lParam);		// 文件夹接收结果(lParam=foldertransfer::FolderResultNotice*，处理后 delete)
	LRESULT OnV2TypingNotify(UINT uMsg, WPARAM wParam, LPARAM lParam);			// v2 对端正在/停止输入(lParam=net::TypingNotify*，处理后 delete)
	// v2 有应在托盘闪烁的新消息：把发送方记入 m_trayFlash 并启动托盘头像闪烁。
	// lParam=堆上 TrayFlashData*（处理后 delete）。
	LRESULT OnV2TrayFlash(UINT uMsg, WPARAM wParam, LPARAM lParam);
	// v2 某会话已读：从 m_trayFlash 移除该会话；若再无未读则停闪并恢复默认托盘图标。
	// wParam=ctype（1单聊 2群聊），lParam=堆上 long long* cid（处理后 delete）。
	LRESULT OnV2TrayClear(UINT uMsg, WPARAM wParam, LPARAM lParam);
	// 主面板最小化隐藏到托盘后弹一次气泡提示，告诉用户此后从托盘图标唤回。
	// 由主面板判定"该账号是否已提示过"，本处收到即弹；无参。
	LRESULT OnTrayMinimizeTip(UINT uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT OnStatusChangeMsg(UINT uMsg, WPARAM wParam, LPARAM lParam);		// 好友状态改变消息
	LRESULT OnKickMsg(UINT uMsg, WPARAM wParam, LPARAM lParam);				// 被踢下线消息
	LRESULT OnSysGroupMsg(UINT uMsg, WPARAM wParam, LPARAM lParam);			// 群系统消息
	LRESULT OnUpdateBuddyNumber(UINT uMsg, WPARAM wParam, LPARAM lParam);	// 更新好友号码
	LRESULT OnUpdateGMemberNumber(UINT uMsg, WPARAM wParam, LPARAM lParam);	// 更新群成员号码_
	LRESULT OnUpdateGroupNumber(UINT uMsg, WPARAM wParam, LPARAM lParam);	// 更新群号码
	LRESULT OnUpdateBuddySign(UINT uMsg, WPARAM wParam, LPARAM lParam);		// 更新好友个性签名
	LRESULT OnUpdateGMemberSign(UINT uMsg, WPARAM wParam, LPARAM lParam);	// 更新群成员个性签名
	LRESULT OnUpdateBuddyInfo(UINT uMsg, WPARAM wParam, LPARAM lParam);		// 更新好友信息
	LRESULT OnUpdateGMemberInfo(UINT uMsg, WPARAM wParam, LPARAM lParam);	// 更新群成员信息
	LRESULT OnUpdateGroupInfo(UINT uMsg, WPARAM wParam, LPARAM lParam);		// 更新群信息
	LRESULT OnUpdateBuddyHeadPic(UINT uMsg, WPARAM wParam, LPARAM lParam);	// 更新好友头像图片
	LRESULT OnUpdateGMemberHeadPic(UINT uMsg, WPARAM wParam, LPARAM lParam);// 更新群成员头像图片
	LRESULT OnUpdateGroupHeadPic(UINT uMsg, WPARAM wParam, LPARAM lParam);	// 更新群头像图片
	LRESULT OnChangeStatusResult(UINT uMsg, WPARAM wParam, LPARAM lParam);	// 改变在线状态返回消息
	
	LRESULT OnCloseDlg(UINT uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT OnShowOrCloseDlg(UINT uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT OnCancelFlash(UINT uMsg, WPARAM wParam, LPARAM lParam);


	void ShowSysGroupChatDlg(UINT nGroupCode, BOOL bShow);
																	 


	void OnTrayIcon_LButtunUp();
	void OnTrayIcon_RButtunUp();
	void OnTrayIcon_MouseHover();
	void OnTrayIcon_MouseLeave();

	// 取"我当前的在线状态"（v2 presence_type，取值见 UserSessionData.h 的 MyPresenceValue）。
	// 托盘右键菜单据它标出当前所处的那一项，托盘悬停提示据它显示状态文字。
	//
	// 主面板在时<u>以状态胶囊为准</u>：胶囊是当前状态在界面上的唯一呈现，托盘跟它取同一个
	// 值，两处才不会一个说在线、一个说离线。
	//
	// 主面板不在（未登录 / 已销毁）时才退回按登录态判断。这里<u>不能</u>直接用
	// CIUSocket::GetRecordOnlineStatus()：那个字段记的是"期望在线状态"，用户点"离线"走的
	// 是 GoOffline()，它并不会把该字段改成 0（见 OnChangeMyPresence 与
	// CFlamingoClient::GoOffline），所以单看它永远取不到"离线"。
	//
	// 返回：kMyPresenceOffline / kMyPresenceOnline / kMyPresenceAway / kMyPresenceBusy 之一。
	int  GetCurrentPresence();

	// 拼托盘图标的鼠标悬停提示文字："昵称 / 账号 / 当前在线状态"三行，状态取
	// GetCurrentPresence()。返回值为拼好的字符串（值语义）。
	CString BuildTrayTipText();

	// 把托盘图标恢复成默认应用图标，并把它的悬停提示刷成 BuildTrayTipText() 的内容。
	// 供"新消息闪烁结束"等几处恢复默认图标的地方复用（原先各处各写一份 Format，且都把
	// 状态写死成"在线"）。无参数、无返回值；副作用是调用 m_TrayIcon.ModifyIcon。
	//
	// 注意：本方法<u>会覆盖当前托盘图标</u>。只想刷新提示文字（例如用户切换了在线状态）
	// 时改用 m_TrayIcon.ModifyTip(BuildTrayTipText())，那条路不动图标，托盘正闪新消息
	// 头像时也不会把闪烁打断。
	void UpdateTrayIconTip();

	// 与聊天服务器的连接是否已就绪（已连上且完成登录）。离线判定的另一半输入 ——
	// 只看在线状态不够：用户选的是"在线"但连接还没建好（正从离线恢复 / 正在断线重连）
	// 时，需要服务器的操作同样必须拦住。
	bool IsServerConnected() const;

	// 取当前界面连接态（在线 / 正在连接 / 离线），由在线状态与真实连接状态共同推导。
	// 托盘图标、聊天窗提示条、各处功能门禁都据它决定表现。
	// 返回值取值见 OnlineCapability.h 的 ConnectionUiState。
	int  GetConnectionUiState();

	// 把当前连接态（在线 / 正在连接 / 离线）广播给主面板与所有已打开的聊天窗，
	// 由它们决定输入区是否置灰、顶部提示条显示什么。
	// 状态发生任何变化时都要调用：主动离线、从离线恢复在线、登录成功、登录失败、掉线。
	void BroadcastConnectionState();

	// "从离线恢复在线"失败时的收尾：把上层状态、状态胶囊、托盘图标、界面门禁全部拨回
	// 离线态，等同于用户从没点过恢复在线，然后按需提示。
	//
	// 与首次登录失败的区别在于<u>不弹登录窗</u>：用户只是想从离线切回在线，主面板还开着、
	// 账号也还登录着，失败就该原地留在离线态让他稍后再试，而不是被踢回登录界面。
	//   lpszReason：给用户看的失败原因；传 NULL 表示不弹提示（例如用户自己取消的）。
	void ResumeOnlineFailed(LPCTSTR lpszReason);

	// 把"当前应可见的主界面"唤到前台：登录前是登录窗 m_LoginDlg、登录后是
	// 主面板 m_BalloonMainPanel。供托盘单击 / 双击、托盘菜单"打开主面板"复用。
	// 关键点：窗口被最小化(iconic)时必须用 SW_RESTORE 还原，仅 SW_SHOW 还原不了。
	void ShowLoginOrMainPanel();

	// 按当前连接态加载托盘图标（真正在线才彩色，主动离线与连接未就绪都用灰色图标；
	// 文件名由 OnlineCapability::GetTrayIconFileName 决定）。先销毁旧图标再加载，
	// 成功返回 TRUE。
	//   nConnUiState：界面连接态，取值见 OnlineCapability.h 的 ConnectionUiState。
	BOOL LoadAppIcon(int nConnUiState);
	void DestroyAppIcon();

	// 按当前在线状态刷新系统托盘图标与提示文字（提示为"昵称（状态）"）。
	// 登录成功、在线状态变化时调用，让托盘随状态显示对应图标。
	void RefreshTrayIconForStatus();

	// "无操作自动离开"的一跳：读全系统空闲时长与设置，交给 AutoAwayPolicy 判定，
	// 该切就走 OnChangeMyPresence（标明来源为自动）。由 AUTOAWAY_TIMER_ID 定时器每
	// 30 秒调一次；未登录 / 掉线 / 功能关闭等情形由判定函数内部挡掉，本方法不另加守卫。
	//
	// 副作用：可能改变自己的在线状态并向服务端发 cmd_change_presence，同时刷新状态
	// 胶囊与托盘图标；绝大多数跳什么都不做。
	void CheckAutoAway();

	// 装配一次"自动离开"判定所需的输入（当前状态、空闲时长、阈值、连接态、开关）。
	// 空闲轮询与锁屏事件两条触发路径共用它，保证两边看到的是同一份口径。
	//   input：出参，装配结果。
	// 返回：取不到系统空闲时长时返回 false（此时 input 内容无意义，调用方应直接放弃本次判定）。
	bool BuildAutoAwayInput(balloonmain::AutoAwayInput& input);

	// 执行一次"自动离开"判定的结果：该切就打日志并走 OnChangeMyPresence（来源标为自动），
	// kAutoAwayNone 时什么都不做。
	//   action：    判定结果，取值见 AutoAwayPolicy.h 的 AutoAwayAction。
	//   input：     本次判定的输入，仅用于打日志（空闲时长 / 阈值 / 原状态）。
	//   pszTrigger：触发来源的中文短标签，只进日志，便于事后区分是空闲轮询还是锁屏引起的。
	void ApplyAutoAwayAction(int action, const balloonmain::AutoAwayInput& input,
	                         const char* pszTrigger);

	// 「禁止机器休眠」的一次处理：读取设置与当前持有状态，交给 KeepAwakePolicy 判定，
	// 按判定结果获取或释放系统电源请求、或向系统合成一次输入事件。与 CheckAutoAway
	// 复用同一个 AUTOAWAY_TIMER_ID 定时器（30 秒），两者互斥，同一次触发中至多一个
	// 会真正执行动作。
	//
	// 副作用：可能获取或释放系统电源请求（本机随之不再休眠 / 恢复可休眠），并合成一次
	// 位移量为 0 的鼠标移动事件；每个动作都会输出日志（日志约定见
	// BalloonMain/KeepAwakeSession.h）。本功能默认关闭，多数情况下不执行任何动作。
	void CheckKeepAwake();

	BOOL LoadLoginIcon();
	void DestroyLoginIcon();

	// 按 m_trayFlash 当前"最近一条"未读会话重建托盘闪烁图标 m_hMsgIcon —— 用
	// monogram（头像首字 + 底色）按小图标尺寸生成 HICON。先销毁旧 m_hMsgIcon 再
	// 重建；集合为空时把 m_hMsgIcon 置空。每次 PushUnread 或目标切换后调用，OnTimer
	// 的闪烁分支据此在该头像与空白间切换。
	void UpdateTrayFlashIcon();
	// 停止托盘新消息闪烁：杀掉闪烁定时器、销毁 m_hMsgIcon、把托盘图标恢复成默认应用
	// 图标（带"昵称 / 账号 / 在线"提示）。最后一条未读被读完、或注销时调用。
	void StopTrayFlash();

	// 托盘悬停未读提示浮窗的"选中会话行"回调跳板（userdata=CMainDlg this）。
	static void OnTrayPopupPickConv(void* userdata, int ctype, long long cid);
	// 打开托盘悬停提示里被点的会话：转交主面板 OpenConversation（其内部清未读 +
	// 回投 FMG_MSG_TRAY_CLEAR 使托盘停闪）。
	void OpenConvFromTrayPopup(int ctype, long long cid);

	void GetNumber(UINT nGroupCode, UINT nUTalkUin, UINT& nGroupNum, UINT& nUTalkNum);
	HICON ExtractIcon(LPCTSTR lpszFileName);


	long GetStatusFromMenuID(int nMenuID);									// 从菜单ID获取对应的UTalk_STATUS

	void CreateEssentialDirectories();										//登录成功以后创建必需文件夹

	void SaveCurrentLogonUserToFile();										//将当前登录的账户保存到文件中
	void DeleteCurrentUserFromFile();										//从文件中删除当前登录的账户

public:

private:
	CFlamingoClient			m_FMGClient;
			
	CLoginDlg				m_LoginDlg;
	CBalloonMainPanel		m_BalloonMainPanel;								//登录后的新版 balloonui 主面板
	CBalloonChatWindow		m_BalloonChatWindow;							//新版气泡式单聊窗口(调试入口预览用)
	CBalloonGroupChatWindow	m_BalloonGroupChatWindow;						//新版气泡式群聊窗口(调试入口预览用)
	CBalloonNetSettingsWindow	m_BalloonNetSettingsWindow;					//新版网络设置窗口(调试入口预览用)
	CBalloonRegisterWindow	m_BalloonRegisterWindow;						//新版注册对话框(调试入口预览用)
	CBalloonProfileWindow	m_BalloonProfileWindow;							//新版个人信息对话框(调试入口预览用)
	CBalloonContactWindow	m_BalloonContactWindow;							//新版联系人资料对话框(调试入口预览用)
	CBalloonGroupManageWindow	m_BalloonGroupManageWindow;					//新版群管理窗口(调试入口预览用)
	CBalloonHistoryWindow		m_BalloonHistoryWindow;						//新版历史消息记录窗口(调试入口预览用)
	CBalloonSystemSettingDlg	m_BalloonSystemSettingDlg;					//新版系统设置对话框(调试入口预览用)
	CBalloonAboutDlg			m_BalloonAboutDlg;							//新版关于对话框(调试入口预览用)
	balloonmain::CBalloonGroupInfoDlg	m_BalloonGroupInfoDlg;					//新版群资料对话框(调试入口 --groupinfo 预览用)
	CBalloonImageViewerWindow	m_BalloonImageViewerWindow;					//新版聊天图片放大看图窗(调试入口预览用)
	CRdSelfTestWindow			m_RdSelfTestWindow;							//远程桌面引擎自环测试窗(调试入口 --rdpselftest)
	CBalloonRemoteDesktopDlg	m_rdDlg;									//远程桌面会话对话框(呼叫中/授权两用途,协调器持有)
	CBalloonCallDlg				m_callDlg;									//音视频通话会话对话框(呼叫中/来电/通话中三态,协调器持有)
	call::CallMediaSession		m_callMedia;								//通话中转会话(接通后连 imgserver 中转收发;第一期收发测试帧验证透传,第二期接音频管线)
	int							m_callMediaType;							//当前/最近一次通话的媒体类型(net::call_media_audio/video);发起或接听时记下,signaling 回来时据此决定中转会话是否起视频
	remotedesktop::RdControlledStreamer	m_rdStreamer;					//被控端推流器(本端=被控端时连中转抓屏推流)
	CRdViewerWindow				m_rdViewer;									//控制端观看窗(本端=控制端时连中转收屏幕帧显示)
	CRdControlledBar			m_rdControlledBar;							//被控端受控提示条(本端=被控端时屏幕顶部置顶提示"正在被控制"+结束按钮)
	std::wstring				m_rdControlledPeerName;						//被控端记录的控制方昵称(授权时由 cmd_rd_invite_notify 带来,显示在受控条文案)
	std::string					m_rdControlledSessionId;					//被控端记录的会话 id(用户主动结束时发 cmd_rd_close 用)
	// 托盘"新消息闪头像"状态机：记录哪些会话未读、最近一条（单击打开它）、闪烁状态。
	// 由 v2 收消息回投的 FMG_MSG_TRAY_FLASH 填入、会话已读时移除；驱动 m_TrayIcon 的
	// 头像闪烁与单击开窗。生命周期与本对话框一致。
	balloonmain::CTrayFlashController	m_trayFlash;
	// 本轮闪烁是否已经打过"首次 tick"那条日志。每次 FMG_MSG_TRAY_FLASH 到达时复位为
	// false，OnTimer 的闪烁分支打完一条后置 true —— 定时器每几百毫秒触发一次，不做
	// 这个限制会把日志刷满。纯诊断用途，不参与任何闪烁逻辑。
	bool						m_bTrayFlashTickLogged = false;
	// 托盘悬停弹出的未读消息提示浮窗（balloonui 重写，取代旧 SkinLib 版 m_MsgTipDlg）；
	// 数据来自 m_trayFlash，点击行经 OpenConvFromTrayPopup 打开会话。
	CTrayMsgTipPopup		m_trayMsgTipPopup;

	

	

	




	Gdiplus::GdiplusStartupInput	m_gdiplusStartupInput;
	ULONG_PTR						m_gdiplusToken;
	CFaceList						m_FaceList;
	CTrayIcon						m_TrayIcon;

	long							m_nLastMsgType;
	UINT							m_nLastSenderId;

	HICON							m_hAppIcon;
	HICON							m_hLoginIcon[6];
	int								m_nCurLoginIcon;
	HICON							m_hMsgIcon;
	DWORD							m_dwLoginTimerId;
	DWORD							m_dwMsgTimerId;
    DWORD                           m_dwReconnectTimerId;                   //断线重连定时器ID
    DWORD                           m_dwAutoAwayTimerId;                    //"无操作自动离开"轮询定时器ID；0=未启动
	//DWORD							m_dwExitAppTimerId;						//检测程序退出计时器

    // 当前这个"离开"是否为"无操作自动离开"功能切过去的。
    //
    // 只有它为 true 时，检测到键鼠操作才会把用户自动切回在线；用户<u>自己手动</u>选的
    // 离开不会被动。任何一次手动改状态都会把它清掉（见 OnChangeMyPresence 的来源标志），
    // 这样"自动离开期间用户手动切成忙碌"之后回到电脑前也不会被擅自切回在线。
    // 不持久化：每次启动都由用户在登录窗选定初始状态，自动标记从头算起。
    bool                            m_bAutoAway;

    // 「禁止机器休眠」的系统电源请求持有者。功能关闭时它不持有任何资源，析构时会
    // 自动释放。只在主线程访问（定时器与消息处理），见 KeepAwakeSession.h 的说明。
    balloonmain::CKeepAwakeSession   m_KeepAwakeSession;

    // 上次向系统合成输入事件时的 tick（::GetTickCount()）；0 表示本次持有期间尚未
    // 合成过。与 m_KeepAwakeSession 一同构成 KeepAwakePolicy 判定所需的状态。
    DWORD                           m_dwLastKeepAwakeNudgeTick;

    bool                            m_bEnableReconnect;                     //是否断线重连
    UINT                            m_uReconnectInterval;                   //重连时间间隔，必须大于0，单位毫秒

    // 被踢下线提示框是否正在显示。它是模态框、跑嵌套消息循环，期间托盘点击与重复的被踢
    // 下线通知都要挡掉，否则会重入"回到登录界面"的收尾序列（见 OnV2Kickout）。
    bool                            m_bKickoutNoticeShowing;

    bool                            m_bAlreadySendReloginRequest;           //重连过程中是否已经发送过登录请求包
    //本次登录是否由"从离线恢复在线"发起（用户在离线态下选了在线/离开/忙碌）。
    //登录失败时 OnLoginResult 据它决定：恢复在线失败就退回离线态并提示，而不是像
    //首次登录失败那样弹出登录窗把用户踢回登录界面。登录有结果（成功或失败）后复位。
    //生命周期同本对话框。
    bool                            m_bResumingOnline;

	HICON							m_hDlgIcon, m_hDlgSmallIcon;
	CLoginAccountList				m_LoginAccountList;
	LOGIN_ACCOUNT_INFO				m_stAccountInfo;						//当前即将要登录的账户信息
	CCascadeWinManager				m_CascadeWinManager;
		

	BOOL							m_bAlreadyLogin;				        //是否已经登录过



	CRect							m_rcTrayIconRect;


    MAINPANEL_STATUS                m_nMainPanelStatus;             //主面板状态
};
