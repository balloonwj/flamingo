package org.hootina.platform.result;

public class NewFriendEntity {
	private int id;
	private int uTargetID;//
	private String ncmd;
	private String strAccountNo;//
	private String strNickName;//贴加好友的
	private int nFace;
	private int uAccountID;
	private String needop;//别人加我为true
	//

 

	public int getId() {
		return id;
	}

	

	public String getNeedop() {
		return needop;
	}



	public void setNeedop(String needop) {
		this.needop = needop;
	}



	public int getuAccountID() {
		return uAccountID;
	}

	public void setuAccountID(int uAccountID) {
		this.uAccountID = uAccountID;
	}

	public void setId(int id) {
		this.id = id;
	}

	public String getStrAccountNo() {
		return strAccountNo;
	}

	public void setStrAccountNo(String strAccountNo) {
		this.strAccountNo = strAccountNo;
	}

	public String getStrNickName() {
		return strNickName;
	}

	public void setStrNickName(String strNickName) {
		this.strNickName = strNickName;
	}

	public int getnFace() {
		return nFace;
	}

	public String getNcmd() {
		return ncmd;
	}

	public void setNcmd(String ncmd) {
		this.ncmd = ncmd;
	}

	public void setnFace(int nFace) {
		this.nFace = nFace;
	}

	public int getuTargetID() {
		return uTargetID;
	}

	public void setuTargetID(int uTargetID) {
		this.uTargetID = uTargetID;
	}

	
}
