package org.hootina.platform.result;

public class UserServer {
	private String id = "0";
	private String name;// 名称
	private String ChatIP;// IP地址
	private int chatPort;
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
