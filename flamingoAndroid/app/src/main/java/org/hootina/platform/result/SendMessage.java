package org.hootina.platform.result;

import java.util.List;

public class SendMessage {
	private int 			mMsgType;
	private String 			mMsgID;
	private int 			mSenderID;
	private int 			mTargetID;// 消息ID
	private long 			mMsgTime;
	private int 			mSenderClientType;// 消息类型1.文本   2.窗口抖动   3.文件
	private List<Object> 	content;


	public long getmMsgTime() {
		return mMsgTime;
	}
	public void setmMsgTime(long mMsgTime) {
		this.mMsgTime = mMsgTime;
	}
	private String textid;//消息和表情
	public String getTextid() {
		return textid;
	}

	public int getMsgType() {
		return mMsgType;
	}
	public void setMsgType(int msgType) {
		this.mMsgType = msgType;
	}
	public String getmMsgID() {
		return mMsgID;
	}
	public void setmMsgID(String msgID) {
		this.mMsgID = msgID;
	}
	public int getmSenderID() {
		return mSenderID;
	}
	public void setmSenderID(int mSenderID) {
		this.mSenderID = mSenderID;
	}
	
	public int getmTargetID() {
		return mTargetID;
	}
	public void setmTargetID(int mTargetID) {
		this.mTargetID = mTargetID;
	}
	public int getmSenderClientType() {
		return mSenderClientType;
	}
	public void setmSenderClientType(int mSenderClientType) {
		this.mSenderClientType = mSenderClientType;
	}
	public List<Object> getContent() {
		return content;
	}
	public void setContent(List<Object> content) {
		this.content = content;
//		setmLastMsgText();
	}

	public String getMsgID() {
		return mMsgID;
	}

	public void setMsgID(String msgID) {
		mMsgID = msgID;
	}

	public int getSenderID() {
		return mSenderID;
	}

	public void setSenderID(int senderID) {
		mSenderID = senderID;
	}

	public int getTargetID() {
		return mTargetID;
	}

	public void setTargetID(int targetID) {
		mTargetID = targetID;
	}

	public long getMsgTime() {
		return mMsgTime;
	}

	public void setMsgTime(long msgTime) {
		mMsgTime = msgTime;
	}

	public int getSenderClientType() {
		return mSenderClientType;
	}

	public void setSenderClientType(int senderClientType) {
		mSenderClientType = senderClientType;
	}

	public void setTextid(String textid) {
		this.textid = textid;
	}
}
