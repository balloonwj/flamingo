#pragma once

#include "../third-party/jsoncpp-1.9.0/json.h"
#include <string>
#include <vector>

#if defined(UNICODE) || defined(_UNICODE)
typedef std::wstring tstring;
#else
typedef std::string tstring;
#endif

extern TCHAR g_szHomePath[MAX_PATH];
extern char g_szHomePathAscii[MAX_PATH];

// 本地数据根目录，形如 "D:\FlamingoData\"（以反斜杠结尾）。按账号隔离的本地数据
// 都挂在它下面的 Users\<登录账号>\ 里：账号配置、聊天记录、公告、文件落点登记表，
// 以及文件 / 图片 / 头像缓存。
//
// 默认等于程序目录 g_szHomePath；用户在「系统设置 → 存储」里改过之后，取值来自
// config\flamingo.ini 的 [storage] datapath（归一规则见 net/StorageLocation.h）。
// 进程启动时算一次，之后不再变 —— 改动须重启才生效，因为登录流程很早就用它打开了
// 各个库与缓存目录。
//
// <u>注意与 g_szHomePath 的分工</u>：程序自身的东西（皮肤、表情、config\flamingo.ini、
// Users\LoginAccountList.dat 这份机器级的登录账号列表）仍然跟着 g_szHomePath 走，
// 不随数据根目录搬家。
extern TCHAR g_szDataRoot[MAX_PATH];
extern const UINT USER_THUMB_COUNT;

extern std::wstring g_strAppTitle;

#define     FMG_MSG_NET_ERROR               WM_USER + 50            // 网络错误

#define		FMG_MSG_LOGIN_RESULT			WM_USER + 100		    // 登录返回消息
#define		FMG_MSG_LOGOUT_RESULT			WM_USER + 101			// 注销返回消息
//WM_USER + 103 原 FMG_MSG_UPDATE_BUDDY_LIST（更新 v1 好友列表）已废弃，禁止复用。
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
//企业版说明：v1 好友体系（查找好友 / 加好友 / 删好友 / 好友ID列表）已整体删除，通讯录
//改由服务端组织架构下发。下列窗口消息编号已废弃、禁止复用 —— 留这段说明是为了
//防止后来人看到编号空档就顺手拿去当新消息用，与遗留的旧代码互发时串号：
//  WM_USER + 405  FMG_MSG_FINDFREIND           查找好友结果
//  WM_USER + 406  FMG_MSG_ADDFREIND            主动发送加好友请求的成败
//  WM_USER + 407  FMG_MSG_RECVADDFRIENDREQUSET 收到加好友请求 / 应答
//  WM_USER + 408  FMG_MSG_DELETEFRIEND         删除好友结果（曾兼作 v1 退群通知）
//  WM_USER + 413  FMG_MSG_RECVADDFRIENDANS     收到对方对加好友请求的答复
//  WM_USER + 418  FMG_MSG_UPDATE_FRIEND_IDS    更新所有好友ID
#define		FMG_MSG_SENDCHATMSG_RESULT	    WM_USER + 409			//给好友发消息的反馈结果
//#define		FMG_MSG_UPLOADFILE_RESULT		WM_USER + 410		//上传文件反馈结果
#define		FMG_MSG_RECVFILE_REQUEST		WM_USER + 411			//接受文件请求
#define		FMG_MSG_RECVFILE_RESULT		    WM_USER + 412			//下载文件反馈结果
#define		FMG_MSG_HEARTBEAT				WM_USER + 414			//心跳消息
#define		FMG_MSG_GOOFFLINE				WM_USER + 415			//下线或者掉线
#define		FMG_MSG_UPDATE_USER_BASIC_INFO  WM_USER + 416			//更新某个用户基本信息
#define		FMG_MSG_UPDATE_GROUP_BASIC_INFO WM_USER + 417		    //更新某个群组基本信息

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

//——v2 单聊消息（实时收发文本）相关，lParam 携带堆上 net:: 结构体指针，
//  由 UI 线程处理后 delete——
#define     FMG_MSG_V2_RECV_CHATMSG         WM_USER + 450			//收到 v2 单聊消息推送(cmd_message_notify 1504)，lParam=net::MessageNotify*
#define     FMG_MSG_V2_SEND_CHATMSG_ACK     WM_USER + 451			//v2 单聊发送回执(cmd_send_message 1503)，lParam=net::SendMessageResponse*
#define     FMG_MSG_V2_CONTACTS             WM_USER + 452			//v2 通讯录应答(cmd_get_contacts 1201)，lParam=net::ContactsResult*
#define     FMG_MSG_V2_ORG_STRUCTURE_CHANGED WM_USER + 555			//v2 组织架构变更推送(cmd_org_structure_notify 1404)，lParam=net::OrgStructureChangedNotify*（处理后 delete）；主面板据此重新拉一次通讯录，好让新入职/新注册的同事立刻出现。号段接在 507 之后往下取，与本行相邻的 452/455 无关
#define     FMG_MSG_V2_CONVERSATIONS        WM_USER + 455			//v2 会话列表应答(cmd_sync_conversations 1501)，lParam=net::ConversationsResult*
#define     FMG_MSG_V2_CREATE_GROUP_RESULT  WM_USER + 457			//v2 建群应答(cmd_create_group 1301)，lParam=net::CreateGroupResponse*
#define     FMG_MSG_V2_GROUP_INFO           WM_USER + 458			//v2 群资料应答(cmd_get_group_info 1303)，lParam=net::GroupInfoResult*
#define     FMG_MSG_V2_GROUP_MEMBERS        WM_USER + 459			//v2 群成员应答(cmd_get_group_members 1304)，lParam=net::GroupMembersResult*
#define     FMG_MSG_V2_USER_GROUPS          WM_USER + 460			//v2 我的群列表应答(cmd_get_user_groups 1320)，lParam=net::UserGroupsResult*
#define     FMG_MSG_V2_SYNC_MESSAGES        WM_USER + 461			//v2 历史消息应答(cmd_sync_messages 1502)，lParam=net::SyncMessagesResult*
#define     FMG_MSG_V2_CHANGE_PWD_RESULT    WM_USER + 462			//v2 修改密码应答(cmd_change_password 1105)，lParam=net::ChangePasswordResponse*（处理后 delete）
#define     FMG_MSG_V2_UPDATE_PROFILE_RESULT WM_USER + 463			//v2 更新资料应答(cmd_update_profile 1104，当前用于个性签名)，lParam=net::UpdateProfileResponse*（处理后 delete）
#define     FMG_MSG_V2_CHANGE_PRESENCE_RESULT WM_USER + 464			//v2 修改在线状态应答(cmd_change_presence 1107)，lParam=net::ChangePresenceResponse*（处理后 delete）
#define     FMG_MSG_CHANGE_MY_PRESENCE      WM_USER + 465			//主面板状态菜单 / 托盘菜单发出的"切换我的在线状态"请求，wParam=v2 presence(presence_type)，lParam=切换来源(见 PresenceChangeSource，0=手动)；由会话控制器决定 GoOffline/GoOnline/SendChangePresence
#define     FMG_MSG_V2_CHANGE_AVATAR_RESULT WM_USER + 466			//v2 修改头像应答(cmd_change_avatar 1106，应答体为通用{code,msg})，lParam=net::UpdateProfileResponse*（处理后 delete）
#define     FMG_MSG_V2_AVATAR_UPLOAD_RESULT WM_USER + 467			//v2 头像图片上传结果(走 imgserver，仅上传不发消息)，lParam=net::AvatarUploadResult*（处理后 delete）
#define     FMG_MSG_V2_AVATAR_DOWNLOAD_RESULT WM_USER + 469			//v2 头像图片下载结果(按 fileid 拉联系人头像到本地缓存)，lParam=net::AvatarDownloadResult*（处理后 delete）
#define     FMG_MSG_V2_USERINFO_CHANGED     WM_USER + 472			//v2 推送：某联系人资料(头像/昵称)变更(cmd_user_info_changed_notify 1108)，lParam=net::UserInfoChangedNotify*（处理后 delete）
#define     FMG_MSG_V2_PRESENCE_CHANGED     WM_USER + 468			//v2 推送：某好友在线状态变更(cmd_presence_changed_notify 1109)，lParam=net::PresenceChangedNotify*（处理后 delete）
// 注意：本文件的 WM_USER 偏移<u>不是一条连续序列</u> —— 下面还有好几段各自从头编号的
// 消息块（v2 图片 / 文件传输、主面板请求、托盘等），偏移区间彼此重叠。新增消息时
// 务必在<u>全文件</u>范围内查重（如 grep "WM_USER + <号>"），不能只看邻近几行顺着往下取，
// 否则会与别处同号：同号消息会被消息映射里靠前的那个处理器抢走，把别人的
// lParam 当成自己的结构体解读，既弹错提示又按错误类型 delete。
// 下面三条取 521 起（当前全文件最大号为 520），确保不与任何一段重叠。
#define     FMG_MSG_LOGOUT                  WM_USER + 521			//托盘菜单 / 主面板系统菜单发出的"登出"请求，wParam=0，lParam=0；由会话控制器二次确认后断开连接、重置主面板并回登录界面。必须 PostMessage 而非 SendMessage：登出要销毁主面板、并进入登录窗的模态循环，不能在菜单分发的调用栈上做
#define     FMG_MSG_V2_RECALL_RESULT        WM_USER + 522			//v2 撤回消息应答(cmd_recall_message 1508)，lParam=net::RecallMessageResponse*（处理后 delete）；code 非 0 时据 error_code 提示失败原因
#define     FMG_MSG_V2_MESSAGE_RECALLED     WM_USER + 523			//v2 推送：某条消息被撤回(cmd_message_recalled_notify 1509)，lParam=net::MessageRecalledNotify*（处理后 delete）；撤回者本人也会收到，据 operator 区分提示文案
#define     FMG_MSG_V2_CALL_SWITCH_MEDIA_RESULT WM_USER + 525		//v2 切换通话媒体类型应答(cmd_call_switch_media 1708)，lParam=net::CallSwitchMediaResponse*（处理后 delete）；code 非 0 时据 error_code 提示失败原因
#define     FMG_MSG_V2_CALL_MEDIA_CHANGED   WM_USER + 526		//v2 推送：通话媒体类型已变更(cmd_call_media_changed_notify 1709)，lParam=net::CallMediaChangedNotify*（处理后 delete）；双方含发起者均收到，据 operator 决定本端是否自动开摄像头
#define     FMG_MSG_CALL_STOP_MEDIA         WM_USER + 528		//通话窗发给主面板：请求停掉音视频媒体会话，wParam=0，lParam=0。用户点通话窗标题栏关闭按钮时发出——通话窗不持有 CallMediaSession（持有者是通话协调器），若只销毁窗口不停会话，采集线程会继续跑、摄像头灯一直亮着。本消息为窗口间投递，不经 ProxyWndProc
#define     FMG_MSG_V2_CONVERSATION_CHANGED WM_USER + 524			//v2 推送：会话属性多端同步(cmd_conversation_changed_notify 1514)，lParam=net::ConversationChangedNotify*（处理后 delete）；带的是改完之后的完整状态(置顶/免打扰/删除)，发起改动的那一端自己也会收到

// ---- 群管理（cmd 1305~1322）----
// 说明：这一组里的写操作（邀请/移出/设管理员/禁言/全员禁言/改群资料/转让/解散/
// 审批/敏感词增删）应答体都是通用 { code, msg }，共用同一条消息号
// FMG_MSG_V2_GROUP_OP_RESULT 回投，由 wParam 带上是哪条协议的应答，界面据它决定
// 失败时的提示文案。之所以不给每个操作各开一个消息号：它们的处理逻辑完全一致
// （成功什么都不做、等推送刷新；失败弹提示），十来个号只会把 ProxyWndProc 撑肿。
#define     FMG_MSG_V2_GROUP_OP_RESULT      WM_USER + 530			//v2 群管理写操作应答（通用 code/msg），wParam=对应的 cmd 协议号，lParam=net::GroupOpResult*（处理后 delete）
#define     FMG_MSG_V2_GROUP_JOIN_REQUESTS  WM_USER + 531			//v2 待审批加群申请列表应答(cmd_get_group_join_requests 1319)，lParam=net::GroupJoinRequestsResult*（处理后 delete）
#define     FMG_MSG_V2_SENSITIVE_WORDS      WM_USER + 532			//v2 群敏感词库应答(cmd_get_sensitive_words 1321)，lParam=net::SensitiveWordsResult*（处理后 delete）
#define     FMG_MSG_V2_GROUP_MEMBER_CHANGED WM_USER + 533			//v2 推送：群成员变动(cmd_group_member_changed_notify 1317)，lParam=net::GroupMemberChangedNotify*（处理后 delete）；加入/退出/被踢/角色变更/禁言变更/群名片变更，操作者本人也会收到
#define     FMG_MSG_V2_GROUP_INFO_CHANGED   WM_USER + 534			//v2 推送：群资料变更(cmd_group_info_changed_notify 1318)，lParam=net::GroupInfoChangedNotify*（处理后 delete）；changetype=2 表示群已解散，须关掉相关窗口
#define     FMG_MSG_V2_JOIN_GROUP_REQUEST   WM_USER + 535			//v2 推送：有人申请加群(cmd_join_group_request_notify 1307)，lParam=net::GroupJoinRequestItem*（处理后 delete）；仅群主与管理员会收到
#define     FMG_MSG_OPEN_MEMBER_PICKER      WM_USER + 536			//群管理窗→主面板：请求打开选人对话框，wParam=CCreateGroupDlg::Purpose，lParam=堆 int64_t* 群ID（主面板处理后 delete）。候选来源要用主面板手里的通讯录缓存，故由它组装
#define     FMG_MSG_V2_GROUP_ANNOUNCEMENTS  WM_USER + 537			//v2 群公告列表应答(cmd_get_group_announcements 1323)，lParam=net::GroupAnnouncementsResult*（处理后 delete）
#define     FMG_MSG_V2_ANNOUNCEMENT_CHANGED WM_USER + 538			//v2 推送：群公告发布 / 被撤回(cmd_group_announcement_notify 1326)，lParam=net::GroupAnnouncementNotify*（处理后 delete）；changetype=1 时界面还要弹一次新公告提示
#define     FMG_MSG_OPEN_GROUP_ANNOUNCE     WM_USER + 539			//新公告提示窗→主面板：点「查看全部」，请它打开该群聊天窗并切到右侧边栏「公告」Tab，wParam=群ID（int64_t）；无堆指针，无需 delete
//本条取 541（540 已被上面的 FMG_MSG_V2_FILE_TRANSFER_ACK 占用）。与选人对话框同理，
//修改头像对话框归主面板持有（头像图片上传结果固定投给主面板再转交那一个实例），
//故群管理窗只能请它代开，不能自己 new 一个。
#define     FMG_MSG_OPEN_GROUP_AVATAR_DLG   WM_USER + 541			//群管理窗→主面板：请求打开「修改群头像」对话框，lParam=堆 balloongroupmanage::GroupAvatarDlgRequest*（主面板处理后 delete）
#define     FMG_MSG_V2_SET_FAVORITE_RESULT  WM_USER + 542			//v2 收藏设置应答(cmd_set_favorite 1215，加入/取消收藏)，lParam=堆 net::SetFavoriteResult*（UI 处理后 delete）；失败时主面板据其中的 ctype/cid/favorite 回滚乐观更新
#define     FMG_MSG_V2_KICKOUT              WM_USER + 543			//v2 推送：本连接被服务器踢下线(cmd_kickout 1006)，wParam=被踢原因(见 BalloonMain/KickoutNotice.h 的 KickoutReasonValue，即协议 kickout_reason)，lParam=0，无堆负载。由会话控制器进入离线态（停自动重连）并弹托盘气泡
#define     FMG_MSG_NETWORK_RESUMED         WM_USER + 544			//本机网络由断开恢复（CCheckNetworkStatusTask 探测 → CFlamingoClient::OnNetworkStatusChange 转投），wParam=0，lParam=0，无堆负载。由会话控制器过一遍自动重连守卫再决定要不要连回服务器——主动离线 / 被踢下线时不连
#define     FMG_MSG_V2_PIN_RESULT           WM_USER + 545			//v2 Pin 消息应答(cmd_pin_message 1518)，lParam=堆 net::PinMessageResponse*（UI 处理后 delete）；只在失败时据 code 提示原因（无权取消 / 消息已撤回），成功一律等推送再改界面
#define     FMG_MSG_V2_MESSAGE_PINNED       WM_USER + 546			//v2 推送：某条消息被 Pin / 取消 Pin(cmd_message_pinned_notify 1519)，lParam=net::MessagePinnedNotify*（处理后 delete）；Pin 是会话内共享状态，单聊推双方、群聊推全体成员，操作者本人也会收到
#define     FMG_MSG_V2_PINNED_MESSAGES      WM_USER + 547			//v2 被 Pin 消息全集应答(cmd_get_pinned_messages 1520)，lParam=net::PinnedMessagesResult*（处理后 delete）；带完整消息体，UI 据它补齐本地库缺的行并对账，m_total 大于返回条数说明被截断
#define     FMG_MSG_V2_ANNOUNCEMENTS        WM_USER + 549			//v2 平台公告列表应答(cmd_get_announcements 2101)，lParam=堆 net::AnnouncementsResult*（UI 处理后 delete）；主面板落本地库 announce.db 并刷新铃铛角标。注意与群公告的 FMG_MSG_V2_GROUP_ANNOUNCEMENTS(537) 不是一回事
#define     FMG_MSG_V2_ANNOUNCEMENT_NOTIFY  WM_USER + 550			//v2 推送：平台公告新发布 / 被撤回(cmd_announcement_notify 2103)，lParam=堆 net::AnnouncementNotify*（UI 处理后 delete）；changetype=1 落库并弹角标 + 提示音，=2 只带公告ID、把本地那条标为已撤回
#define     FMG_MSG_V2_ANN_MARK_READ_RESULT WM_USER + 551			//v2 平台公告已读上报应答(cmd_mark_announcement_read 2102)，lParam=堆 net::AnnounceMarkReadResult*（UI 处理后 delete）；成功不动界面（已读在发起时已乐观落本地库），仅失败时记日志留痕
#define     FMG_MSG_V2_SET_REMARK_RESULT    WM_USER + 552			//v2 设置好友备注名应答(cmd_set_friend_remark 1209)，lParam=堆 net::SetFriendRemarkResult*（UI 处理后 delete）；成功只记日志（乐观更新在发起时已生效），失败时主面板据其中原样回带的 userid 把那一个人的备注回滚并提示。备注名改完不走推送，读回靠下次通讯录应答里的 contacts[].remark
#define     FMG_MSG_SET_CONTACT_REMARK      WM_USER + 553			//联系人资料窗→主面板：备注名编辑提交，wParam=对端 userid，lParam=堆 std::string*（用户敲进去的原始文本，UTF-8，未 trim；主面板处理后 delete）。该不该发、发不出去怎么办、失败怎么退回来全由主面板判定——通讯录缓存与 socket 都在它手上
//——客户端自动更新：UpdateManager 工作线程 → 升级窗（CBalloonUpdateWindow）——
//这两条是<u>窗口间直投</u>，不经 CFlamingoClient::ProxyWndProc，但号段仍取在
//FMG_MSG_FIRST~FMG_MSG_LAST 闸门之内，免得日后有人把它们改成走代理窗时莫名失效。
//升级窗 → 登录窗：用户关掉了升级窗（点「稍后」或标题栏关闭），登录可以继续了。
//wParam=0，lParam=0。强制升级时升级窗关不掉，这条消息也就永远不会发出——登录被
//一直挡在门外，正是强制升级该有的效果。
#define     FMG_MSG_UPDATE_LOGIN_CONTINUE   WM_USER + 500

//升级窗 → 主窗口：升级包已就绪，请<u>直接退出</u>以便更新器覆盖文件。wParam=0，lParam=0。
//不能用 WM_CLOSE 代替：那条路会先弹「退出程序还是最小化到托盘？」提示框，用户选了
//「最小化」的话程序不退出，更新器等 30 秒后放弃，升级白做一场。
#define     FMG_MSG_EXIT_FOR_UPDATE         WM_USER + 499
#define     FMG_MSG_UPDATE_PROGRESS         WM_USER + 556			//升级包下载进度，lParam=堆 update::UpdateProgressInfo*（UI 处理后 delete）；wParam=0
#define     FMG_MSG_UPDATE_RESULT           WM_USER + 557			//一次检查更新 / 下载的最终结果，lParam=堆 update::UpdateTaskResult*（UI 处理后 delete）；wParam=0
#define     FMG_MSG_V2_SET_STARRED_RESULT   WM_USER + 558			//v2 特别关注设置应答(cmd_set_starred 1216，标记/取消特别关注)，lParam=堆 net::SetStarredResult*（UI 处理后 delete）；失败时主面板据其中回带的 target/starred 回滚乐观更新（口径同 542 的收藏应答）
//本条是<u>同步查询</u>，只能用 SendMessage、不能 PostMessage —— 调用方要的是返回值，
//且 lParam 指向调用栈上的变量，异步投递时那块内存早没了。
#define     FMG_MSG_QUERY_STARRED           WM_USER + 559			//群聊窗→主面板：查某联系人是否被特别关注（弹右键菜单前取文案用），lParam=const int64_t* userid（借用，指向调用栈），返回 1 已关注 / 0 未关注。关注名单只有主面板那一份，群聊窗与气泡都不认识它，故代问
#define     FMG_MSG_TOGGLE_STARRED          WM_USER + 560			//群聊窗→主面板：请求切换某联系人的特别关注状态（群成员列表 / 气泡的 @片段与发送者头像三处右键菜单点了那一项），wParam=userid，lParam=0，无堆负载。目标状态由主面板按名单现状取反并发 cmd_set_starred，故无需传
#define     FMG_MSG_V2_TYPING_NOTIFY        WM_USER + 554		//v2 推送：对端正在 / 停止输入(cmd_typing_notify 1510)，lParam=堆 net::TypingNotify*（UI 处理后 delete）；m_cid 是"正在输入的那个人"的 userid，主面板据它找单聊窗刷新头部提示。这条提示不可靠（服务端不落库不重发），对端掉线时 state=0 收不到，故聊天窗侧另有过期兜底
#define     FMG_MSG_V2_SET_TAGS_RESULT      WM_USER + 561			//v2 设置联系人标签应答(cmd_set_contact_tags 1217)，lParam=堆 net::SetContactTagsResult*（UI 处理后 delete）；成功时用应答回带的<u>归一后</u>列表把本地缓存对齐到服务端真值（服务端会去首尾空白、丢空串、忽略大小写去重，本地乐观更新算出来的那份未必与它逐字相同），失败时用回带的<u>当前</u>列表回滚。标签改完不走推送，读回靠下次通讯录应答里的 contacts[].tags
#define     FMG_MSG_SET_CONTACT_TAGS        WM_USER + 562			//联系人资料窗→主面板：标签整份提交，wParam=对端 userid，lParam=堆 std::vector<std::string>*（该联系人在本人视角下的完整标签列表，UTF-8，未归一；主面板处理后 delete）。空列表表示清空。增、改、删三种操作都走这一条——协议本身就是整份覆盖语义。该不该发、发不出去怎么办、失败怎么退回来全由主面板判定，口径与备注名(553)一致

//——v2 单聊文件夹传输(cmd_folder_* 1810~1817)。前五条由网络层解析后回投，后五条由
//  foldertransfer::FolderTransferManager 从它自己的工作线程回投；两者都投给代理窗口，
//  走「代理窗 → 主窗口 → 主面板 → 聊天窗」同一条既有通路。lParam 一律是堆上结构体
//  指针，由 UI 线程处理后 delete。
//
//  本组取 581~590（580 已被 FMG_MSG_LAST 这道闸门占用，故不能从 580 起排；562 之后的
//  563~579 虽然空着，但留给号段更靠前的那几块继续顺排，文件夹这一整组另起一段更好找）。
//  <u>注意 FMG_MSG_LAST 已随本组一并上抬到 620</u>——不抬的话这十条会在代理窗口门口
//  被静默丢弃（见文件末尾那段闸门说明），且 lParam 上的堆对象一并泄漏。
#define     FMG_MSG_V2_FOLDER_INVITE_ACK    WM_USER + 581			//v2 文件夹传输邀请应答(cmd_folder_invite 1810)，wParam=本次邀请请求所用的帧 seq（uint32_t，应答包体里没有任何能对回发起方的字段，本机又可能有多笔传输同时在等应答，故一律按它定位是哪一笔发起的），lParam=堆 net::FolderInviteResponse*（处理后 delete）；成功带 sessionid，规模超限时带服务端配置的 maxfiles / maxsize 供提示
#define     FMG_MSG_V2_FOLDER_INVITE_NOTIFY WM_USER + 582			//v2 文件夹传输邀请推送(cmd_folder_invite_notify 1811)，lParam=堆 net::FolderInviteNotify*（处理后 delete）；接收端据此在会话里出一条带「接收 / 拒绝」的邀请气泡，不弹独立授权窗
#define     FMG_MSG_V2_FOLDER_SESSION_ACK   WM_USER + 583			//v2 文件夹传输通用应答（应答 1812 / 撤回 1813 / 结束 1814 / 重试 1817 四条共用一种应答体），wParam=发出的那条命令的协议号，lParam=堆 net::FolderSessionResponse*（处理后 delete）
#define     FMG_MSG_V2_FOLDER_STATE_NOTIFY  WM_USER + 584			//v2 文件夹传输状态推送(cmd_folder_state_notify 1815)，lParam=堆 net::FolderStateNotify*（处理后 delete）；响铃/传输中/拒绝/撤回/对方离线/超时/失败/完成，失败时带双方一致的错误码
#define     FMG_MSG_V2_FOLDER_RELAY_INFO    WM_USER + 585			//v2 文件夹传输中转参数下发(cmd_folder_relay_info 1816)，lParam=堆 net::FolderRelayInfo*（处理后 delete）；双方据此连 fileserver 建 1 条控制通道 + N 条数据通道
#define     FMG_MSG_V2_FOLDER_SCANNED       WM_USER + 586			//本地目录扫描完成（发送端，扫描跑在 FolderTransferManager 的工作线程上），lParam=堆 foldertransfer::FolderScanNotice*（处理后 delete）；界面据 localid 把文件数 / 目录数 / 总大小补进那条刚建出来的气泡，扫描失败时据 m_scanError 把它转成失败态
#define     FMG_MSG_V2_FOLDER_SEND_PROGRESS WM_USER + 587			//文件夹发送进度，lParam=堆 foldertransfer::FolderProgressNotice*（处理后 delete）；分子以接收端的回执为准，故对方回退续传位点时它会相应减少
#define     FMG_MSG_V2_FOLDER_SEND_RESULT   WM_USER + 588			//文件夹发送最终结果，lParam=堆 foldertransfer::FolderResultNotice*（处理后 delete）；UI 线程收到后须回调 FolderTransferManager::FinishSend 停线程并发 cmd_folder_close
#define     FMG_MSG_V2_FOLDER_RECV_PROGRESS WM_USER + 589			//文件夹接收进度，lParam=堆 foldertransfer::FolderProgressNotice*（处理后 delete）
#define     FMG_MSG_V2_FOLDER_RECV_RESULT   WM_USER + 590			//文件夹接收最终结果，lParam=堆 foldertransfer::FolderResultNotice*（处理后 delete）；UI 线程收到后须回调 FolderTransferManager::FinishRecv，成功时其中的 m_localRoot 即落地目录，供「打开文件夹」

// FMG_MSG_CHANGE_MY_PRESENCE 的 wParam 取值。四个值与 v2 协议 net/Protocol.h 的
// enum presence_type 逐条同值（0 离线 / 1 在线 / 2 离开 / 3 勿扰），改动那边时
// 必须同步这里。
//
// 这里另立一份命名常量而不是直接用 presence_type，原本是因为消息中枢所在的
// 编译单元包含 v1 的 net/Msg.h，而它与 net/Protocol.h 有同名枚举、不能同时包含；
// v1 协议头已于 2026-08-14 删除，那条限制不复存在。现在保留它的理由变成：本头
// 文件被大量 UI 侧文件包含，不宜为四个状态值把整个协议头拖进来。要换成直接用
// presence_type 也可以，届时把这四个常量的使用点一并改掉即可。
enum MyPresenceValue
{
    kMyPresenceOffline = 0,   // 离线 —— 真正断开连接（GoOffline）
    kMyPresenceOnline  = 1,   // 在线 —— 正常在线
    kMyPresenceAway    = 2,   // 离开 —— 暂时离开
    kMyPresenceBusy    = 3,   // 勿扰 —— 忙碌 / 请勿打扰
};

// FMG_MSG_CHANGE_MY_PRESENCE 的 lParam 取值：本次状态切换是<u>谁</u>发起的。
//
// 会话控制器据它维护"当前这个离开是不是自动切过去的"标志：
// 手动改状态一律把标志清掉，于是"自动离开期间用户自己切成忙碌"之后回到电脑前
// 不会被擅自切回在线。取值 0 表示手动是刻意的 —— 主面板状态菜单与托盘菜单两个
// 既有入口传的都是 0，语义天然正确，加这个字段时一行都不用改。
enum PresenceChangeSource
{
    kPresenceChangeManual = 0,   // 用户手动选的（状态菜单 / 托盘菜单）
    kPresenceChangeAuto   = 1,   // "无操作自动离开"功能自动切的（见 balloonmain::CAutoAwayController）
};
//——v2 聊天图片发送相关，lParam 携带堆上 net:: 结构体指针，由 UI 线程处理后 delete——
#define     FMG_MSG_V2_IMAGE_SEND_PROGRESS  WM_USER + 470			//v2 发图上传进度，lParam=net::ImageSendProgress*（按 clientmsgid 更新"发送中"气泡进度）
#define     FMG_MSG_V2_IMAGE_SEND_RESULT    WM_USER + 471			//v2 发图最终结果，lParam=net::ImageSendResult*（ok=失败则把气泡标失败；成功后发送回执仍走 FMG_MSG_V2_SEND_CHATMSG_ACK）
#define     FMG_MSG_V2_IMAGE_DOWNLOAD_RESULT WM_USER + 474			//v2 收图下载结果，lParam=net::ImageDownloadResult*（按 msgid 定位气泡，成功用 localpath 渲染；处理后 delete）
//——v2 聊天文件收发相关，lParam 携带堆上 net:: 结构体指针，由 UI 线程处理后 delete——
#define     FMG_MSG_V2_FILE_SEND_PROGRESS    WM_USER + 477			//v2 发文件上传进度，lParam=net::FileSendProgress*（按 clientmsgid 更新"发送中"文件气泡进度条）
#define     FMG_MSG_V2_FILE_SEND_RESULT      WM_USER + 478			//v2 发文件最终结果，lParam=net::FileSendResult*（失败则把气泡标失败；成功后发送回执仍走 FMG_MSG_V2_SEND_CHATMSG_ACK）
#define     FMG_MSG_V2_FILE_DOWNLOAD_PROGRESS WM_USER + 479			//v2 收文件下载进度，lParam=net::FileDownloadProgress*（按 msgid 更新接收方文件气泡进度条）
#define     FMG_MSG_V2_FILE_DOWNLOAD_RESULT  WM_USER + 480			//v2 收文件下载结果，lParam=net::FileDownloadResult*（按 msgid 定位气泡，成功用 localpath 渲染"打开/另存为"；处理后 delete）
//本条取 540（当前全文件最大号为 539）：477~480 那段已被上面四条占满，紧邻的 481 起是群文件库段，故另起号
#define     FMG_MSG_V2_FILE_TRANSFER_ACK     WM_USER + 540			//v2 推送：对方把我发的文件收到什么程度了(cmd_file_transfer_ack 1517)，lParam=net::FileTransferAckNotify*（按 msgid 定位本端那条文件气泡，按 state 显示"对方正在接收/已接收/已拒绝"；处理后 delete）
//——v2 群文件库（边栏「群文件」面板）相关，lParam 携带堆上 net:: 结构体指针，由 UI 线程处理后 delete——
#define     FMG_MSG_V2_GROUP_FILES           WM_USER + 481			//v2 群文件列表应答(cmd_get_group_files 1803)，lParam=net::GetGroupFilesResult*（按 groupid 路由群聊窗累积/渲染；处理后 delete）
#define     FMG_MSG_V2_GROUP_FILES_CHANGED   WM_USER + 482			//v2 群文件变更推送(cmd_group_files_changed_notify 1806)，lParam=net::GroupFilesChangedNotify*（按 groupid 路由群聊窗增量更新；处理后 delete）
#define     FMG_MSG_V2_GROUP_FILE_UPLOAD_RESULT   WM_USER + 483		//v2 群文件上传结果，lParam=net::GroupFileUploadResult*（按 groupid 路由群聊窗，成功后发 cmd_add_group_file 登记；处理后 delete）
#define     FMG_MSG_V2_GROUP_FILE_DOWNLOAD_RESULT WM_USER + 484		//v2 群文件下载结果，lParam=net::GroupFileDownloadResult*（按 groupid 路由群聊窗，成功后打开文件；处理后 delete）
#define     FMG_MSG_V2_GROUP_FILE_UPLOAD_PROGRESS   WM_USER + 493	//v2 群文件上传进度，lParam=net::GroupFileUploadProgress*（按 groupid 路由群聊窗，面板级进度条；处理后 delete）
#define     FMG_MSG_V2_GROUP_FILE_DOWNLOAD_PROGRESS WM_USER + 494	//v2 群文件下载进度，lParam=net::GroupFileDownloadProgress*（按 groupid 路由群聊窗，按 entryid 更新行进度；处理后 delete）
//——v2 群资料对话框写操作（置顶/免打扰/改群名片/退群）的通用 {code,msg} 应答。
//   wParam=结果码 code（0 成功），lParam=堆上 std::string* msg（UI 线程处理后 delete）——
#define     FMG_MSG_V2_SET_CONVERSATION_RESULT WM_USER + 485		//v2 会话设置应答(cmd_set_conversation 1513，置顶/免打扰)，wParam=code，lParam=std::string* msg
#define     FMG_MSG_V2_SET_MEMBER_CARD_RESULT  WM_USER + 486		//v2 改本人群名片应答(cmd_set_member_card 1315)，wParam=code，lParam=std::string* msg
#define     FMG_MSG_V2_LEAVE_GROUP_RESULT      WM_USER + 487		//v2 退出群聊应答(cmd_leave_group 1309)，wParam=code，lParam=std::string* msg
//——v2 远程桌面信令(cmd_rd_* 1710-1716)，lParam 携带堆上 net:: 结构体指针，由 UI 线程处理后 delete——
#define     FMG_MSG_V2_RD_INVITE_ACK        WM_USER + 488		//v2 远程桌面邀请应答(cmd_rd_invite 1710)，lParam=net::RDInviteResponse*（成功带 sessionid，失败提示；处理后 delete）
#define     FMG_MSG_V2_RD_INVITE_NOTIFY     WM_USER + 489		//v2 远程桌面邀请推送(cmd_rd_invite_notify 1711)，lParam=net::RDInviteNotify*（被控端弹授权窗；处理后 delete）
#define     FMG_MSG_V2_RD_RELAY_INFO        WM_USER + 490		//v2 远程桌面中转参数下发(cmd_rd_relay_info 1716)，lParam=net::RDRelayInfo*（双方据此连 imgserver 中转通道；处理后 delete）
#define     FMG_MSG_V2_RD_STATE_NOTIFY      WM_USER + 491		//v2 远程桌面状态推送(cmd_rd_state_notify 1715)，lParam=net::RDStateNotify*（拒绝/取消/对方离线/超时/断开 提示并收尾；处理后 delete）
#define     FMG_MSG_RD_START_INVITE         WM_USER + 492		//聊天窗显示器按钮→主面板远程桌面协调器:发起邀请,wParam=对端userid,lParam=堆 std::wstring* peerName(UI 线程处理后 delete)
#define     FMG_MSG_RD_CONTROLLED_END_BY_USER WM_USER + 495	//被控端受控提示条"结束控制"按钮→主面板远程桌面协调器:主动结束受控会话(发 cmd_rd_close+停推流+关受控条;无参)
//——子窗口 → 主面板 的路由消息（wParam=userid）——
#define     FMG_MSG_OPEN_CONTACT_PROFILE    WM_USER + 453			//请求主面板打开某联系人资料窗，wParam=userid
#define     FMG_MSG_OPEN_CHAT               WM_USER + 454			//请求主面板打开与某 userid 的单聊，wParam=userid
#define     FMG_MSG_CONV_SEND_BUMP          WM_USER + 456			//单聊窗发出一条消息后通知主面板冒泡该会话，wParam=对端userid，lParam=堆上 ConvSendBumpData*（处理后 delete）
#define     FMG_MSG_CONV_MARK_READ          WM_USER + 497			//聊天窗/群聊窗被切到前台（WM_ACTIVATE）后通知主面板把该会话未读清零，wParam=ctype（1单聊 2群聊），lParam=堆上 long long* cid（处理后 delete）。窗口间直投、不经 ProxyWndProc，故无须在 FlamingoClient.cpp 加转发 case
#define     FMG_MSG_OPEN_CHANGEPWD          WM_USER + 473			//请求主面板打开修改密码对话框（系统设置「账号安全」页"修改"按钮转发；无参）

//——A 段（WM_USER + 4000 ~ 4499）：网络应答与推送 → 代理窗 → 主面板 CBalloonMainPanel——
//
//2026-08-16 号段整改划出的新段，段内按功能每档 100：4000 账号 / 4100 资料 / 4200 联系人 /
//4300 群 / 4400 消息，档位顺序与 v2 协议 cmd 分组一致。本段的消息都由 IUSocket 投给代理窗，
//必须在 FlamingoClient.cpp 的 ProxyWndProc 里加转发 case，否则会被静默丢弃、堆负载泄漏。
#define     FMG_MSG_V2_MESSAGE_READ_NOTIFY  WM_USER + 4400			//v2 推送：对方已读你的消息(cmd_message_read_notify 1507)，lParam=net::MessageReadNotify*（单聊：cid=读消息的那个人的 userid，readmsgid=对方已读位点；主面板记下位点并转给已打开的单聊窗，处理后 delete）
#define     FMG_MSG_V2_DELETE_MESSAGE_RESULT WM_USER + 4401			//v2 应答：删除单条消息(cmd_delete_message 1512)，lParam=net::DeleteMessageResponse*（成功时界面不做任何事，本地删行与移除气泡等 FMG_MSG_V2_MESSAGES_DELETED；失败时主面板提示原因，处理后 delete）
#define     FMG_MSG_V2_DELETE_BY_TIME_RESULT WM_USER + 4402			//v2 应答：按时间范围清理聊天记录(cmd_delete_messages_by_time 1521)，lParam=net::DeleteMessagesByTimeResponse*（主面板转交系统设置 → 清理聊天记录对话框提示结果，处理后 delete）
#define     FMG_MSG_V2_MESSAGES_DELETED     WM_USER + 4403			//v2 推送：本人删除了消息(cmd_messages_deleted_notify 1522)，lParam=net::MessagesDeletedNotify*（单条形式或区间形式；主面板据此删本地库、更新已打开的聊天窗与消息记录窗、重拉会话列表，处理后 delete）
#define     FMG_MSG_V2_FORWARD_RESULT       WM_USER + 4404			//v2 应答：转发消息(cmd_forward_message 1511)，wParam=请求帧 seq（CIUSocket::ForwardMessage 的返回值；失败应答不带 clientmsgid，只能按它对回请求），lParam=net::ForwardMessageResponse*（主面板交给转发调度器处理，处理后 delete）

//——B 段（WM_USER + 4500 ~ 4799）：窗口 → 主面板 CBalloonMainPanel——
//
//这一段是 2026-08-16 号段整改新划出来的，段内按<u>发起窗口</u>分档：
//  4500 起  聊天窗 / 群聊窗（CBalloonChatWindow、CBalloonGroupChatWindow）
//  4520 起  个人资料、系统设置一类的对话框
//  4540 起  公告提示窗、消息盒子一类的浮窗
//下面四条原先分别取 472 / 474 / 477 / 552，逐个与 A 段的网络应答 / 推送消息撞号
//（FMG_MSG_V2_USERINFO_CHANGED、FMG_MSG_V2_IMAGE_DOWNLOAD_RESULT、
// FMG_MSG_V2_FILE_SEND_PROGRESS、FMG_MSG_V2_SET_REMARK_RESULT）。当时没出问题，
//全靠「收件窗口不同」这个没有任何机制保证的前提：这四条由某个窗口直接
//PostMessage 给主面板，那四条也经代理窗转给主面板。整改一律搬走本段这一条、
//保留网络应答那一条，因为本段不经代理窗、影响面最小。
//号段规约见开发约定文档「UI 约定」第 6 条，重号由
//Source\Tests\MessageIdRegistryTests.cpp 的守卫用例把关。
#define     FMG_MSG_OPEN_HISTORY            WM_USER + 4500			//聊天窗 / 群聊窗→主面板：打开某会话的历史消息记录窗，wParam 低字节=ctype(1单聊 2群聊)、次低字节=打开后预选的类型胶囊(balloonhistory::HistoryTypeFilter，0=全部 6=Pin；不填即 0)，lParam=堆上 int64_t* cid（处理后 delete）
#define     FMG_MSG_OPEN_GROUP_MANAGE       WM_USER + 4501			//群聊窗→主面板：打开某群的群管理对话框，lParam=堆上 int64_t* groupId（处理后 delete）
#define     FMG_MSG_OPEN_FORWARD_PICKER     WM_USER + 4502			//聊天窗 / 群聊窗→主面板：转发多选中的消息，打开目标选择窗，lParam=堆上 balloonmain::ForwardRequest*（UI/BalloonMain/ForwardRequest.h，处理后 delete）。窗口间直投、不经 ProxyWndProc
#define     FMG_MSG_QUERY_CONTACT_BRIEF     WM_USER + 4505			//群聊窗→主面板（同步 SendMessage）：按 userid 查通讯录缓存里的昵称与头像，供已退群成员的历史消息显示发送人，lParam=调用方栈上的 balloonchatwnd::ContactBriefQuery*（UI/BalloonChatWnd/GroupSenderRefresh.h，主面板就地填写、不 delete），返回 1 查到 / 0 查不到。窗口间直投、不经 ProxyWndProc
#define     FMG_MSG_TRANSFER_GROUP_OWNER    WM_USER + 4504			//群聊窗成员右键「转让群主」/ 选人对话框「转让群主并退出」→主面板：转让群主（确认框已由发起窗口弹过），lParam=堆上 balloonmain::TransferOwnerRequest*（UI/BalloonMain/GroupExitFlow.h，处理后 delete）。窗口间直投、不经 ProxyWndProc
#define     FMG_MSG_OPEN_MERGED_VIEWER      WM_USER + 4503			//聊天窗 / 群聊窗 / 聊天记录查看窗→主面板：打开一份合并转发的聊天记录，lParam=堆上 std::string*（合并消息的 content 原文，UTF-8，处理后 delete）。窗口间直投、不经 ProxyWndProc
#define     FMG_MSG_REFRESH_SELF_HEADER     WM_USER + 4520			//个人信息对话框→主面板：乐观保存后请它按 m_UserInfo 立即刷新本人 Header（昵称/签名/头像）（无参）
//下面四条是联系人资料对话框的动作出口。该窗口既没有 socket 也没有通讯录 / 收藏 /
//特别关注三份缓存，判定与执行一律交给主面板，做法与备注名(553)、标签(562)一致。
//三条状态类消息传的是<u>目标状态</u>而不是"取反"：开关拨出来的本来就是明确的开或关，
//传取反的话，用户连点两下、而第一条应答还在路上时，主面板会按同一个旧状态取反两次。
#define     FMG_MSG_SET_CONTACT_MUTE        WM_USER + 4521			//联系人资料窗→主面板：设置与该联系人会话的消息免打扰状态，wParam=对端 userid，lParam=目标状态(1 开 / 0 关)；无堆负载，无需 delete
#define     FMG_MSG_SET_CONTACT_FAVORITE    WM_USER + 4522			//联系人资料窗→主面板：设置该联系人的收藏状态，wParam=对端 userid，lParam=目标状态(1 收藏 / 0 取消)；无堆负载，无需 delete
#define     FMG_MSG_SET_CONTACT_STARRED     WM_USER + 4523			//联系人资料窗→主面板：设置该联系人的特别关注状态，wParam=对端 userid，lParam=目标状态(1 关注 / 0 取消)；无堆负载，无需 delete
#define     FMG_MSG_LOCATE_CONTACT          WM_USER + 4524			//联系人资料窗→主面板：在通讯录树里定位该联系人（切到通讯录页、展开各级部门、滚动到该行并选中），wParam=对端 userid，lParam=0；无堆负载，无需 delete
//本机历史账号列表由主面板上的会话控制器持有（内存一份、整份写回文件），系统设置窗若自行
//读写 LoginAccountList.dat，写入的内容会被会话控制器下次落盘时整份覆盖，故只能转交它处理。
//同样传目标状态而不是"取反"，理由同上。见 docs/自动登录方案.md §4。
#define     FMG_MSG_SET_AUTO_LOGIN          WM_USER + 4525			//系统设置窗→主面板（由会话控制器处理）：设置当前账号的「自动登录」，wParam=目标状态(1 开 / 0 关)，lParam=0；无堆负载，无需 delete
#define     FMG_MSG_DEFAULT_CHAT_FONT_CHANGED WM_USER + 4526		//系统设置窗→主面板：全局默认聊天字体改了（或恢复默认），请它让已打开、且没有单独设过字体的单聊 / 群聊窗改用新的默认字体（字体本身已写进账号配置，主面板从那里读），wParam=0，lParam=0；无堆负载，无需 delete
#define     FMG_MSG_OPEN_ANNOUNCE_BOX       WM_USER + 4540			//新公告提示窗→主面板：点「查看全部」，请它打开「消息盒子」窗口，wParam=0，lParam=0；无堆负载，无需 delete
//这条原先取的是 WM_USER + 497，与上面的 FMG_MSG_CONV_MARK_READ 撞了号，而两者<u>由同一个
//窗口 CBalloonMainPanel 处理</u>。ATL 消息映射先匹配先赢，靠后的这条处理器永远不会执行：
//系统设置里改「自动识别链接」开关时，消息落进了 OnConvMarkReadMsg，那个函数把 lParam 当
//堆上的 long long* 解，而本消息的 lParam 是 0，于是判空提前返回 —— 不崩，但开关对已打开的
//会话窗静默失效，要重启客户端才生效（登录时 MainDlg 会另读一次配置）。2026-08-12 改到
//空闲的 498。教训：取新号不能只看"当前最大号"，还要确认这个号没被别人占着 —— 本文件里
//号段并非按顺序排列，光看末尾很容易挑中一个中间已被用掉的号。
#define     FMG_MSG_LINK_OPTION_CHANGED     WM_USER + 498			//系统设置里改了"自动识别链接"开关，wParam=1 开 / 0 关。主面板据此更新进程级开关，并让已打开的单聊 / 群聊窗重新测量重绘（无 lParam）
//——托盘"新消息闪头像"相关（v2）。主面板投给自己，由它持有的托盘控制器处理——
#define     FMG_MSG_TRAY_FLASH              WM_USER + 475			//有应在托盘闪烁的新消息（非窗口抖动、对端发来、对应聊天窗当前未打开），lParam=堆上 TrayFlashData*（托盘控制器处理后 delete）
#define     FMG_MSG_TRAY_CLEAR              WM_USER + 476			//某会话已读：从托盘闪烁集合移除该会话，wParam=ctype（1单聊 2群聊），lParam=堆上 long long* cid（托盘控制器处理后 delete）
#define     FMG_MSG_TRAY_MINIMIZE_TIP       WM_USER + 496			//主面板刚最小化隐藏到托盘：请托盘控制器弹一次"已最小化到托盘"气泡提示（无参；"是否首次"由主面板据 CUserConfig 判定，收到就弹）


// 闸门下界。2026-08-16 由 WM_USER + 1 上抬到 WM_USER + 50：本文件里最小的一条
// FMG_MSG_* 就是 FMG_MSG_NET_ERROR（WM_USER + 50），而 CustomMsgDef.h 里那批
// v1 遗留消息取 WM_USER + 1 ~ 41，原先整段落在闸门之内。它们都是窗口之间直接
// 投递的消息，本就不该由代理窗收；下界抬到 50 之后，万一有谁误把它们投给代理窗，
// 会在门口被挡掉而不是被 switch 里号段相同的某条 case 当成别的消息处理。
#define		FMG_MSG_FIRST					WM_USER + 50
//——v2 音视频通话信令(cmd_call_* 1701-1707)，lParam 携带堆上 net:: 结构体指针，由 UI 线程处理后 delete——
#define     FMG_MSG_V2_CALL_INVITE_ACK      WM_USER + 501		//v2 通话邀请应答(cmd_call_invite 1701)，lParam=net::CallInviteResponse*（成功带 sessionid，失败如对方离线/忙提示；处理后 delete）
#define     FMG_MSG_V2_CALL_INVITE_NOTIFY   WM_USER + 502		//v2 通话邀请推送(cmd_call_invite_notify 1702)，lParam=net::CallInviteNotify*（被叫弹来电窗；处理后 delete）
#define     FMG_MSG_V2_CALL_SIGNALING       WM_USER + 503		//v2 通话中转参数下发(cmd_call_signaling 1707)，lParam=net::CallSignalingInfo*（双方据此连 imgserver 中转通道；处理后 delete）
#define     FMG_MSG_V2_CALL_STATE_NOTIFY    WM_USER + 504		//v2 通话状态推送(cmd_call_state_notify 1706)，lParam=net::CallStateNotify*（响铃/拒绝/取消/对方离线/超时/接通/结束/忙 提示并收尾；处理后 delete）
#define     FMG_MSG_CALL_START_INVITE       WM_USER + 505		//单聊窗"语音通话"按钮→主面板通话协调器:发起呼叫,wParam=对端userid,lParam=堆 std::wstring* peerName(UI 线程处理后 delete)
#define     FMG_MSG_CALL_RELAY_EVENT        WM_USER + 506		//通话中转会话事件(call::CallMediaSession→主面板):wParam=事件码 call::CallRelayEvent(connected/test_ok/failed/closed),lParam=序号(test_ok 时);无堆指针,无需 delete
#define     FMG_MSG_CALL_START_VIDEO_INVITE WM_USER + 507		//单聊窗"视频通话"按钮→主面板通话协调器:发起视频呼叫,wParam=对端userid,lParam=堆 std::wstring* peerName(UI 线程处理后 delete)
// FMG_MSG_FIRST / FMG_MSG_LAST 是 CFlamingoClient::ProxyWndProc 的收件范围闸门
//（见 FlamingoClient.cpp 开头的 message < FMG_MSG_FIRST || message > FMG_MSG_LAST
// 判断）：socket 线程投给代理窗口的消息，号段落在范围外会被直接丢弃，连 switch 都进
// 不去。所以<u>新增任何要从 socket 层投到 UI 的消息，除了在 ProxyWndProc 里加转发
// case，还必须确认号段没有越过 FMG_MSG_LAST</u>，必要时把它一起抬高。
//
// 这个坑非常隐蔽，已经踩中过：越界时代码看着处处都对（ProxyWndProc 的转发 case 写了、
// 消息映射加了、收帧日志也照常打出来），消息却在 ProxyWndProc 门口被
// DefWindowProc 静默丢掉，连 switch 都进不去，lParam 上的堆对象一并泄漏，且<u>全程
// 没有任何报错</u>——排查时极易误判成别的原因。
//
// 取新号时只查"当前最大号"是不够的：最大号本身可能就已越过闸门。窗口间直接投递的
// 消息（如 539 群公告、541 修改群头像，由某个窗口 PostMessage 给主面板）不经代理窗，
// 越界也照常工作，很容易让人误以为这个号段没问题。
//
// 上界留足余量（原为 560，2026-08-12 特别关注用掉 558-560 后上抬到 580；2026-08-14
// 单聊文件夹传输用掉 581-590 后再上抬到 620；2026-08-16 号段整改划出 A/B/C 三段，
// 上抬到 4999 一次性覆盖整个 4000~4999 规划区间），免得每加一条消息都要动它一次；
// 闸门放宽本身无风险：switch 里没有匹配 case 的消息一律走 default → DefWindowProc，
// 与被闸门挡掉的效果相同。
#define		FMG_MSG_LAST					WM_USER + 4999

//聊天窗 → 主面板 的"会话冒泡"载荷（FMG_MSG_CONV_SEND_BUMP 的 lParam，堆分配，
//主面板处理后 delete）。承载本端刚发出的一条消息，用于刷新会话列表的最后预览。
//会话用 (ctype,cid) 唯一标识：单聊 ctype=1、cid=对端userid；群聊 ctype=2、cid=群id。
struct ConvSendBumpData
{
    int          ctype;     //会话类型（1 单聊 2 群聊）
    long long    cid;       //会话ID（单聊=对端userid，群聊=群id）
    int          msgType;   //消息内容类型（见 net::message_type，如文本=1、抖动=13）
    std::string  digest;    //会话列表的"最后一条消息"预览文字（UTF-8）

    ConvSendBumpData()
        : ctype(1)
        , cid(0)
        , msgType(0)
    {
    }
};

//托盘"新消息闪头像"载荷（FMG_MSG_TRAY_FLASH 的 lParam，堆分配，托盘控制器处理后 delete）。
//由 CBalloonMainPanel::OnRecvChatMessage 在"该消息应在托盘闪烁"时填好回投：承载会话
//标识与画 monogram 头像所需的首字 / 底色（与会话列表头像同口径，保证两处颜色一致）。
//会话用 (ctype,cid) 唯一标识：单聊 ctype=1、cid=对端userid；群聊 ctype=2、cid=群id。
struct TrayFlashData
{
    int          ctype;        //会话类型（1 单聊 2 群聊）
    long long    cid;          //会话ID（单聊=对端userid，群聊=群id）
    std::wstring displayName;  //会话显示名（好友昵称 / 群名），供悬停弹窗显示；可为空
    std::wstring preview;      //最新一条消息的预览文字（与会话列表 digest 同口径），悬停弹窗副行显示；可为空
    std::wstring avatarText;   //头像首字（取显示名前 1-2 字符），托盘 monogram 图标用；可为空
    COLORREF     avatarColor;  //头像底色（按 cid 稳定取色，与会话列表头像同色）

    TrayFlashData()
        : ctype(1)
        , cid(0)
        , avatarColor(RGB(0, 0, 0))
    {
    }
};

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

class CBuddyInfo;
class CGroupInfo;
class CRecentInfo;
class CBuffer;

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

//企业版说明：v1 好友体系已整体删除 —— 通讯录改由服务端组织架构下发（v2 cmd_get_contacts），
//由 CBalloonMainPanel 自己缓存联系人。随之删掉的有：
//  struct BuddyInfo / struct BuddyTeamInfo —— 好友与好友分组落盘用的 POD（BuddyInfo.bi / BuddyTeamInfo.bti）
//  class  CBuddyTeamInfo                   —— 好友分组
//  class  COnlineBuddyInfo                 —— 在线好友精简信息（已无调用方）
//  class  CBuddyList                       —— 好友列表容器
//CBuddyInfo 保留：它同时是“本人资料”与“群成员资料”的载体（见 CGroupInfo::m_arrMember）。

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

