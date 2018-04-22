package org.hootina.platform.result;

import java.util.List;

import android.graphics.Bitmap;

public class ChatMessage {

	public static final int MSG_STATE_SENDING			= 0;	//发送中
	public static final int MSG_STATE_SENT				= 1;	//发送成功
	public static final int MSG_STATE_SENDFAILED		= 2;	//发送失败
	public static final int MSG_STATE_UNREAD			= 3;	//消息未读
	public static final int MSG_STATE_READ				= 4;	//消息已读

	private int 		id;

	private int 		mSenderID;
	private int 		mTargetID;
	private String 		mMsgID;			// 消息ID
	private String		mSenderName;
	private String      mTargetName;
	private long 		mMsgTime;		// 消息时间
	private int 		mMsgType;		// 消息类型 1.文本 2.图片
	private int 		mMsgSenderClientType;
	private int 		mMsgState;
	private String 		mMsgText;
	private Bitmap 		mMsgBitmap;
	private String      mRemark;		//消息备注

	// private int faceID;
	private List<ContentText> content;

	public ChatMessage(){

	}

	public ChatMessage( String msgID, int msgType, int senderID, String senderName, int targetID,
						String targetName, String msgContent, int unreadFlag, String remark){
		mMsgID = msgID;
		mMsgType = msgType;
		mSenderID = senderID;
		mSenderName = senderName;
		mTargetID = targetID;
		mTargetName = targetName;
		mMsgText = msgContent;
		mMsgState = unreadFlag;
		mRemark = remark;
	}

	public String getmMsgText() {
		return mMsgText;
	}

	public void setTextid() {
		mMsgText ="";
		for (int i = 0; i < getContent().size(); i++) {

			if (i > 0) {
				mMsgText += "|";
			}
			String faceid = String.valueOf(getContent().get(i).getFaceID());
			String text = getContent().get(i).getMsgText();
			String picture = getContent().get(i).getPic();
			if (text != null) {
				mMsgText += text;

			} else if (faceid != null) {
				mMsgText += "[";
				mMsgText += faceid;
				mMsgText += "]";
			} else if (picture != null) {
				mMsgText += "(";
				mMsgText += picture;
				mMsgText += ")";
			}

		}
	}

	public int getTargetID() {
		return mTargetID;
	}

	public void setTargetID(int targetID) {
		mTargetID = targetID;
	}

	public List<ContentText> getContent() {
		return content;
	}

	public void setContent(List<ContentText> content) {
		this.content = content;
		setTextid();
	}

	public int getSenderID() {
		return mSenderID;
	}

	public void setSenderID(int sendID) {
		this.mSenderID = sendID;
	}

	public String getMsgID() {
		return mMsgID;
	}

	public void setMsgID(String mMsgID) {
		this.mMsgID = mMsgID;
	}

	public int getMsgSenderClientType() {
		return mMsgSenderClientType;
	}

	public long getMsgTime() {
		return mMsgTime;
	}

	public void setMsgTime(long mMsgTime) {
		this.mMsgTime = mMsgTime;
	}

	public int getMsgType() {
		return mMsgType;
	}

	public void setMsgType(int mMsgType) {
		this.mMsgType = mMsgType;
	}

	public void setMsgSenderClientType(int mMsgSenderClientType) {
		this.mMsgSenderClientType = mMsgSenderClientType;
	}

	public int getId() {
		return id;
	}

	public void setId(int id) {
		this.id = id;
	}
	
	public Bitmap getMsgBitmap(){
		return mMsgBitmap;
	}
	
	public void setMsgBitmap(Bitmap bm){
		mMsgBitmap = bm;
	}
	
	public void setMsgState(int flag)
	{
		mMsgState = flag;
	}
	
	public int getMsgState()
	{
		return mMsgState;
	}

}
