package org.hootina.platform.result;

import java.io.Serializable;

import org.hootina.platform.utils.PictureUtil;

/*
 * 用户登录信息
 */
public class MemberEntity implements Serializable {
	private String id = "0";
	private String strAccountNo;
	private String strAccountNo2;
	private String mobile;// 手机号
	private int uAccountID;
    private String password;
    private String uBirthday ;//生日
    private String strCustomFace;//头像
    private String strSigature;//个性签名
    private String nickname;//昵称
    private int nFace;
	private String headpath;

	public String getMobile() {
		return mobile;
	}

	public void setMobile(String mobile) {
		this.mobile = mobile;
	}

	public String getStrAccountNo2() {
		return strAccountNo2;
	}

	public void setStrAccountNo2(String strAccountNo2) {
		this.strAccountNo2 = strAccountNo2;
	}

	public int getnFace() {
		return nFace;
	}

	public void setnFace(int nFace) {
		this.nFace = nFace;
	}

	public String getNickname() {
		return nickname;
	}

	public void setNickname(String nickname) {
		this.nickname = nickname;
	}

	public String getStrSigature() {
		return strSigature;
	}

	public void setStrSigature(String strSigature) {
		this.strSigature = strSigature;
	}

	public String getuBirthday() {
		return uBirthday;
	}

	public void setuBirthday(String uBirthday) {
		this.uBirthday = uBirthday;
	}

	public String getStrCustomFace() {
		return strCustomFace;
	}

	public void setStrCustomFace(String strCustomFace) {
		this.strCustomFace = strCustomFace;
	}

	public String getHeadpath() {
		return headpath;
	}

	public void setHeadpath(String headpath) {
		this.headpath = headpath;
		
		PictureUtil.SetHeadPath(uAccountID, headpath);
	}

	public String getPassword() {
		return password;
	}

	public void setPassword(String password) {
		this.password = password;
	}

	public String getStrAccountNo() {
		return strAccountNo;
	}

	public void setStrAccountNo(String strAccountNo) {
		this.strAccountNo = strAccountNo;
	}

	public int getuAccountID() {
		return uAccountID;
	}

	public void setuAccountID(int uAccountID) {
		this.uAccountID = uAccountID;
	}

	public String getId() {
		return id;
	}

	public void setId(String id) {
		this.id = id;
	}



}
