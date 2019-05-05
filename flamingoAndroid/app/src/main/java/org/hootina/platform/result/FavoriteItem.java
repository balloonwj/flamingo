package org.hootina.platform.result;

import java.io.Serializable;

/*
 * 收藏
 */
public class FavoriteItem implements Serializable {
	private int id;
	private int uAccountID;// 自己的ID
	private int uTargetID;// 好友ID
	private String name;
	private String type;// 2图片，null是文本
	private String msgtext;// 收藏的内容
	private int nFace;
	private String headpath;
	private String time;

	public String getTime() {
		return time;
	}

	public void setTime(String time) {
		this.time = time;
	}

	public int getnFace() {
		return nFace;
	}

	public String getHeadpath() {
		return headpath;
	}

	public void setHeadpath(String headpath) {
		this.headpath = headpath;
	}

	public void setnFace(int nFace) {
		this.nFace = nFace;
	}

	public int getId() {
		return id;
	}

	public void setId(int id) {
		this.id = id;
	}

	public int getuAccountID() {
		return uAccountID;
	}

	public void setuAccountID(int uAccountID) {
		this.uAccountID = uAccountID;
	}

	public int getuTargetID() {
		return uTargetID;
	}

	public void setuTargetID(int uTargetID) {
		this.uTargetID = uTargetID;
	}

	public String getName() {
		return name;
	}

	public void setName(String name) {
		this.name = name;
	}

	public String getType() {
		return type;
	}

	public void setType(String type) {
		this.type = type;
	}

	public String getMsgtext() {
		return msgtext;
	}

	public void setMsgtext(String msgtext) {
		this.msgtext = msgtext;
	}

}
