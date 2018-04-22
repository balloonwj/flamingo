package org.hootina.platform.util;

public interface CmdType {
	// Heartbeat = 0;
	public static final int n_AnsInfo = 0;
	public static final int n_TalkMsg = 1;
	public static final int n_TalkMsgAns = 2;
	public static final int n_PullTalkMsgReq = 3;
	public static final int n_MsgInfoNotify = 11;
	// user
	public static final int n_TargetInfo = 34;
	public static final int n_FriendInfo = 35;
	public static final int n_UserTargets = 36;
	public static final int n_UserPrivateInfo = 37;
	public static final int n_UserInfo = 38;
	public static final int n_UserOnlineInfo = 39;
	public static final int n_LoginInfo = 40;
	public static final int n_RegisterInfo = 41;
	public static final int n_UserInfoReq = 42;
	public static final int n_TargetsQuery = 43;
	public static final int n_UpdateUserInfo = 44;
	public static final int n_TargetsAdd = 45;
	public static final int n_UpdateTargetInfo = 46;
	public static final int n_StateServerReq = 47;
	public static final int n_UserInfo_Old = 48;
	// old// notify
	public static final int n_UserInfoNotify = 101;
	public static final int n_UserBaseInfoNotify = 102;
	public static final int n_TargetsAddNotify = 103;
	public static final int n_TargetsNotify = 104;
	public static final int n_UserOnlineNotify = 105;
	// server
	public static final int n_ReqHeartbeat = 1001;
	public static final int n_AnsHeartbeat = 1002;
	public static final int n_ServerTime = 1003;
	public static final int n_FileLoadData = 1007;
	public static final int n_FileLoadInfo = 1008;
	public static final int n_UserOnlineNotifyList = 1020;
	public static final int n_TalkMsgBaseList = 1021;
	public static final int n_ChatMsgBaseList = 1022;
	public static final int n_UserMsgIDList = 1023;
	public static final int n_ServerInfo = 1024;
	public static final int n_ServerInfoList = 1025;
	public static final int n_ServerOut = 1026;
	public static final int n_TargtServerIDList = 1027;
	public static final int n_UserLoginFlagList = 1028;
	public static final int n_TargetOnlineInfo = 1029;

}
