package org.hootina.platform.result;

import java.io.Serializable;

public class ServerCfg implements Serializable{
	private String id = "0";
	private String name;
	private String ChatIP;
	private int chatPort;// 手机号
	private String fileIP;
    private int filePort;
	public String getId() {
		return id;
	}
	public void setId(String id) {
		this.id = id;
	}
	public String getName() {
		return name;
	}
	public void setName(String name) {
		this.name = name;
	}
	public String getChatIP() {
		return ChatIP;
	}
	public void setChatIP(String chatIP) {
		ChatIP = chatIP;
	}
	public int getChatPort() {
		return chatPort;
	}
	public void setChatPort(int chatPort) {
		this.chatPort = chatPort;
	}
	public String getFileIP() {
		return fileIP;
	}
	public void setFileIP(String fileIP) {
		this.fileIP = fileIP;
	}
	public int getFilePort() {
		return filePort;
	}
	public void setFilePort(int filePort) {
		this.filePort = filePort;
	}

}
