package org.hootina.platform.model;

public class LoginResult {

    /**
     * code : 0
     * msg : ok
     * userid : 10
     * username : qqq
     * nickname : qqq123
     * facetype : 5
     * customface :
     * gender : 0
     * birthday : 20170914
     * signature : {“id”：12}
     * address : 上海市浦东新区南泉路1200号409室
     * phonenumber :
     * mail : balloonwj@qq.com
     */


    private static LoginResult sLoginResult;

    public static LoginResult getInstance() {
        if (sLoginResult == null) {
            return new LoginResult();
        }

        return sLoginResult;
    }

    public static void setLoginResult(LoginResult loginResult) {
        sLoginResult = loginResult;
    }

    private int code;
    private String msg;
    private int userid;
    private String username;
    private String nickname;
    private int facetype;
    private String customface;
    private int gender;
    private int birthday;
    private String signature;
    private String address;
    private String phonenumber;
    private String mail;

    public int getCode() {
        return code;
    }

    public void setCode(int code) {
        this.code = code;
    }

    public String getMsg() {
        return msg;
    }

    public void setMsg(String msg) {
        this.msg = msg;
    }

    public int getUserid() {
        return userid;
    }

    public void setUserid(int userid) {
        this.userid = userid;
    }

    public String getUsername() {
        return username;
    }

    public void setUsername(String username) {
        this.username = username;
    }

    public String getNickname() {
        return nickname;
    }

    public void setNickname(String nickname) {
        this.nickname = nickname;
    }

    public int getFacetype() {
        return facetype;
    }

    public void setFacetype(int facetype) {
        this.facetype = facetype;
    }

    public String getCustomface() {
        return customface;
    }

    public void setCustomface(String customface) {
        this.customface = customface;
    }

    public int getGender() {
        return gender;
    }

    public void setGender(int gender) {
        this.gender = gender;
    }

    public int getBirthday() {
        return birthday;
    }

    public void setBirthday(int birthday) {
        this.birthday = birthday;
    }

    public String getSignature() {
        return signature;
    }

    public void setSignature(String signature) {
        this.signature = signature;
    }

    public String getAddress() {
        return address;
    }

    public void setAddress(String address) {
        this.address = address;
    }

    public String getPhonenumber() {
        return phonenumber;
    }

    public void setPhonenumber(String phonenumber) {
        this.phonenumber = phonenumber;
    }

    public String getMail() {
        return mail;
    }

    public void setMail(String mail) {
        this.mail = mail;
    }
}
