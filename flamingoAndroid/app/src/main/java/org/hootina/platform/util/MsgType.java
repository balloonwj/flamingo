package org.hootina.platform.util;

public interface MsgType {
	public static final int Chat_Req = 1;
	public static final int	Chat_Res = 2;
	public static final int	Login_Req = 3;
	public static final int	Login_Res = 4;
	public static final int	Qurey_Msg_Req = 5;
	public static final int	Qurey_Msg_Res = 6;
	public static final int	Qurey_Index_Req = 7;
	public static final int	Qurey_Index_Res = 8;
	public static final int	Register_Req = 9;
	public static final int	Register_Ans = 10;
	public static final int	LoadUserInf_Req = 11;
	public static final int	LoadUserInf_Ans = 12;
	public static final int	LoadFriendList_Req = 13;
	public static final int	LoadFriendList_Ans = 14;
	public static final int	LoadGroupList_Req = 15;
	public static final int	LoadGroupList_Ans = 16;
    public static final int	AddFriend_Req = 17;
    public static final int	AddFriend_Ans = 18;
	public static final int	AlterFriendInf_Req = 19;
	public static final int	AlterFriendInf_Ans = 20;
	public static final int	AlterUserInf_Req = 21;
	public static final int	AlterUserInf_Ans = 22;
	public static final int	QueryUserPubInf_Req = 23;
	public static final int	QueryUserPubInf_Ans = 24;
	public static final int	Online_Notify = 25;
	public static final int	Offline_Notify = 26;
    public static final int	DownLoadFile_Req = 27;
	public static final int	DownLoadFile_Ans = 28; 
	public static final int	ReceiveFile_Ack = 29;
	public static final int	UpdateUserInf_Req = 30;
	public static final int	UpdateUserInf_Ans = 31;
	public static final int	Heartbeat_Req = 32;
	public static final int	Heartbeat_Ans = 33;
	public static final int	OfflineMsgInf_Notify=34;
	public static final int	TalkMsg_Pull=35;
	public static final int	ServerTime_Req = 36;
	public static final int ServerTime_Ans = 37;
	public static final int UpFileInfo_Req = 39;
	public static final int	UpFileInfo_Ans = 40;
	public static final int	Msg_Post = 41;
	public static final int	ConnectServer_Req = 42;
	public static final int ConnectServer_Ans = 43;
	public static final int	ServerMsg = 1000;
	public static final int MobileMsg = 1001;
	public static final int	ProtobufMsg = 1002;
			// 服务器内部消息
	public static final int	Server_Begin=30000;
	
	
	
}
