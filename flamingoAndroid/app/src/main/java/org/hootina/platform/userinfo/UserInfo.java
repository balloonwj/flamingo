package org.hootina.platform.userinfo;

import java.text.Collator;
import java.util.List;
import java.util.ArrayList;

/**
 * @desc    用户信息类
 * @author  zhangyl
 * @date    2017.08.18
 */

/*
        {"code": 0, "msg": "ok", "userid": 8, "username": "13917043320", "nickname": "zhangyl",
        "facetype": 0, "customface":"文件md5", "gender":0, "birthday":19891208, "signature":"哈哈，终于成功了",
        "address":"上海市东方路3261号", "phonenumber":"021-389456", "mail":"balloonwj@qq.org"}
*/
public class UserInfo implements  Comparable {
    //群组id基数
    private static final int GROUP_ID_BOUNDARY = 0x0FFFFFFF;

    //如果这是一个群id，则该成员存放该群的群成员
    public List<UserInfo> groupMembers = new ArrayList<UserInfo>();

    private int         id;
    private int         _userid;
    private String      _username;
    private String      _nickname;
    private int         _faceType;
    private String      _customFacePath;
    private int         _gender;
    private int         _birthday;
    private String      _signature;
    private String      _address;
    private String      _phoneNumber;
    private String      _mail;

    //实时数据, 在线的类型
    private int         _onlineType;

    //客户端类型
    private int _clientType;

    public UserInfo()
    {

    }

    public boolean isGroup()
    {
        return _userid >= GROUP_ID_BOUNDARY;
    }

    public static boolean isGroup(int userid) {
        return userid >= GROUP_ID_BOUNDARY;
    }

    public void setId(int id) {
        this.id = id;
    }

    public synchronized int get_userid() {
        return _userid;
    }

    public synchronized void set_userid(int _userid) {
        this._userid = _userid;
    }

    public synchronized String get_username() {
        return _username;
    }

    public synchronized void set_username(String _username) {
        this._username = _username;
    }

    public synchronized String get_nickname() {
        return _nickname;
    }

    public synchronized void set_nickname(String _nickname) {
        this._nickname = _nickname;
    }

    public synchronized int get_faceType() {
        return _faceType;
    }

    public synchronized void set_faceType(int _faceType) {
        this._faceType = _faceType;
    }

    public synchronized int get_birthday() {
        return _birthday;
    }

    public synchronized void set_birthday(int _birthday) {
        this._birthday = _birthday;
    }

    public synchronized int get_gender() {
        return _gender;
    }

    public synchronized void set_gender(int _gender) {
        this._gender = _gender;
    }

    public synchronized String get_customFacePath() {
        return _customFacePath;
    }

    public synchronized void set_customFacePath(String _customFacePath) {
        this._customFacePath = _customFacePath;
    }

    public synchronized String get_signature() {
        return _signature;
    }

    public synchronized void set_signature(String _signature) {
        this._signature = _signature;
    }

    public synchronized String get_address() {
        return _address;
    }

    public synchronized void set_address(String _address) {
        this._address = _address;
    }

    public synchronized String get_phoneNumber() {
        return _phoneNumber;
    }

    public synchronized void set_phoneNumber(String _phoneNumber) {
        this._phoneNumber = _phoneNumber;
    }

    public synchronized String get_mail() {
        return _mail;
    }

    public synchronized void set_mail(String _mail) {
        this._mail = _mail;
    }

    public synchronized int get_onlinetype() {
        return _onlineType;
    }

    public synchronized void set_onlinetype(int onlinetype) {
        this._onlineType = onlinetype;
    }

    public synchronized int get_clientType() {
        return _clientType;
    }

    public synchronized void set_clientType(int _clientType) {
        this._clientType = _clientType;
    }

    @Override
    public int compareTo(Object o){
        //字母和数字排在前面，然后根据汉语拼音字母排序
        UserInfo other = (UserInfo)o;
        return Collator.getInstance(java.util.Locale.CHINESE).compare(this._nickname, other.get_nickname());
    }
}
