package org.hootina.platform.util;

public interface MegAsnType {
	public static final int Login = 1;//鐧诲綍
	public static final int LoginError = 10;//鐧诲綍閿欒杩斿洖
	public static final int	Regiest = 2;
	public static final int	RegiestError = 11;//閿欒杩斿洖淇℃伅
	public static final int	TargetsNotify = 3;//鏌ヨ娣诲姞濂藉弸鏃惰繑鍥炵殑濂藉弸
	public static final int TargetsQuery=12;//鏌ヨ濂藉弸
	public static final int	TargetsAddNotify = 4;//娣诲姞濂藉弸
	public static final int UserInfoReq=5;//好友列表
	public static final int ToTalkingmsg=6;//鍙戦�佸ソ鍙嬫秷鎭�
	public static final int FromTalkingmsg=7;//鎺ュ彈濂藉弸娑堟伅
	public static final int Topicturemsg=8;//鍙戦�佸浘鐗�
	public static final int FileLoadData=9;
	public static final int UserPrivateInfo=14;//鍚屾鐢ㄦ埛绉佹湁淇℃伅
	public static final int UserPrivateInfoList=11;//同步好友信息
	public static final int FileUpData = 50;//上传图片成功
	public static final int TargetInfo = 101; // 基本信息
	//public static final int NewFriendAgree = 102; // 添加好友成功
	public static final int ReqDownLoadFile = 1000;
	public static final int FriendChange = 1001; // 添加好友成功
	public static final int MyTimer = 1002; // 定时器
	public static final int ReLogin = 1003; // 断线重新登录
	public static final int UpdateUserInfo = 15;//修改个人信息成功
	public static final int Refresh = 16;//刷新消息
	public static final int ChagePwd = 17;//修改密码
	
}
