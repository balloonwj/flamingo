package org.hootina.platform.result;

public class ChatSession {
	private int 			id;

	private int 			mFriendID;		// 好友ID
	private int 			mSelfID;
	private int 			nUnreadCount; 	// 未读消息数量

	private String 			mFriendNickName;// 好友姓名
	private String 			mLastMsg;		// 好友最后一条消息
	private String			mRemark;


	public ChatSession(int senderID, String senderName, String lastMsg, String remark){
		mFriendID = senderID;
		mFriendNickName = senderName;
		mLastMsg = lastMsg;
		mRemark = remark;
	}

	public int getmSelfID() {
		return mSelfID;
	}
	public void setmSelfID(int mSelfID) {
		this.mSelfID = mSelfID;
	}
	public int getId() {
		return id;
	}
	public void setId(int id) {
		this.id = id;
	}
	public String getFriendNickName() {
		return mFriendNickName;
	}
	public void setFriendNickName(String nickName) {
		mFriendNickName = nickName;
	}
	public int getFriendID() {
		return mFriendID;
	}
	public void setFriendID(int mFriendID) {
		this.mFriendID = mFriendID;
	}
	public String getLastMsg() {
		return mLastMsg;
	}
	public void setLastMsg(String lastMsg) {
		mLastMsg = lastMsg;
	}

	public void setmRemark(String remark){
		mRemark = remark;
	}

	public String getRemark(){
		return mRemark;
	}

	public void addUnreadCount()
	{
		nUnreadCount++;
	}
	
	public void setUnreadCount(int n)
	{
		nUnreadCount = n;
	}
	
	public int getUnreadCount()
	{
		return nUnreadCount;
	}
}
