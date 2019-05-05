package org.hootina.platform.result;

import java.io.Serializable;
import java.text.Collator;

import org.hootina.platform.utils.PictureUtil;

public class FriendInfo implements Serializable, Comparable {
    private int id;
    private int uAccountID;// 自己的ID
    private int uTargetID;// 好友ID
    private String strAccountNo;// 好友账号
    private String strNickName;//贴加好友的
    private int nFace;
    private String ncmd;
    private String headpath;//头像路劲
    private String sign;//个性签名
    private boolean isSelected = false;

    public boolean isSelected() {
        return isSelected;
    }

    public void setSelected(boolean isSelected) {
        this.isSelected = isSelected;
    }

    public int getuAccountID() {
        return uAccountID;
    }

    public String getHeadpath() {
        return headpath;
    }

    public void setHeadpath(String headpath) {
        this.headpath = headpath;
        PictureUtil.SetHeadPath(uTargetID, headpath);
    }

    public String getSign() {
        return sign;
    }

    public void setSign(String sign) {
        this.sign = sign;
    }

    public void setuAccountID(int uAccountID) {
        this.uAccountID = uAccountID;
    }

    public String getNcmd() {
        return ncmd;
    }

    public void setNcmd(String ncmd) {
        this.ncmd = ncmd;
    }

    public int getuTargetID() {
        return uTargetID;
    }

    public void setuTargetID(int uTargetID) {
        this.uTargetID = uTargetID;
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

    public void setnFace(int nFace) {
        this.nFace = nFace;
    }

    public int getId() {
        return id;
    }

    public void setId(int id) {
        this.id = id;
    }

    @Override
    public int compareTo(Object o) {
        //字母和数字排在前面，然后根据汉语拼音字母排序
        FriendInfo other = (FriendInfo) o;
        return Collator.getInstance(java.util.Locale.CHINESE).compare(this.strNickName, other.getStrNickName());
    }
}
