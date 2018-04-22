package org.hootina.platform.result;

import java.io.Serializable;
import java.util.ArrayList;
import java.util.List;

public class Group implements Serializable{
	private int id;
	private int uGroupID;// 自己的ID
	private List<FriendInfo> memberList = new ArrayList<FriendInfo>();
	
	public int getId() {
		return id;
	}

	public void setId(int id) {
		this.id = id;
	}
	
	public int getGroupId() {
		return uGroupID;
	}
	
	public void setGroupId(int gid){
		uGroupID = gid;
	}
	
	public List<FriendInfo> getMemberList() {
		return memberList;
	}
	
	public void addMember(FriendInfo info) {
		memberList.add(info);
	}
}
