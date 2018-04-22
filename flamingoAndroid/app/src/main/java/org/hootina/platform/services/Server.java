//TODO: 这个类要整理一下

package org.hootina.platform.services;

public class Server {
	private int id;
	private String name;
	private String ChatIP;
	private String chatPort;
	private String fileIP;
	private String filePort;
	public int getId() {
		return id;
	}
	public void setId(int id) {
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
	public String getChatPort() {
		return chatPort;
	}
	public void setChatPort(String chatPort) {
		this.chatPort = chatPort;
	}
	public String getFileIP() {
		return fileIP;
	}
	public void setFileIP(String fileIP) {
		this.fileIP = fileIP;
	}
	public String getFilePort() {
		return filePort;
	}
	public void setFilePort(String filePort) {
		this.filePort = filePort;
	}
	
}
