package org.hootina.platform.result;

import java.util.List;

public class FromMessageEntity {
	private int uSendID;
	private int uTargetID;
	private int uMsgID;// 消息ID
	// 消息text
	private List<MessageTextEntity> msg;
	public int getuSendID() {
		return uSendID;
	}
	public void setuSendID(int uSendID) {
		this.uSendID = uSendID;
	}
	public int getuTargetID() {
		return uTargetID;
	}
	public void setuTargetID(int uTargetID) {
		this.uTargetID = uTargetID;
	}
	public int getuMsgID() {
		return uMsgID;
	}
	public void setuMsgID(int uMsgID) {
		this.uMsgID = uMsgID;
	}
	public List<MessageTextEntity> getMsg() {
		return msg;
	}
	public void setMsg(List<MessageTextEntity> msg) {
		this.msg = msg;
	}

}
