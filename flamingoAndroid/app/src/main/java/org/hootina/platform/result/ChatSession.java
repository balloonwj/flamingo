package org.hootina.platform.result;

import org.greenrobot.greendao.annotation.Entity;
import org.greenrobot.greendao.annotation.Generated;
import org.greenrobot.greendao.annotation.Id;

import java.util.Date;

@Entity
public class ChatSession {
    @Id
    private long id;

    private int mFriendID;        // 好友ID
    private int mSelfID;
    private int nUnreadCount;    // 未读消息数量

    private String mFriendNickName;// 好友姓名
    private String mLastMsg;        // 好友最后一条消息
    private String mRemark;
    private Date time; //时间


    public ChatSession(int targetID, String senderName, String lastMsg, String remark) {
        mFriendID = targetID;
        mFriendNickName = senderName;
        mLastMsg = lastMsg;
        mRemark = remark;
    }

    public ChatSession(int senderID,int targetID, String senderName, String lastMsg, String remark) {
        mFriendID = targetID;
        mSelfID = senderID;
        mFriendNickName = senderName;
        mLastMsg = lastMsg;
        mRemark = remark;
    }

    @Generated(hash = 323097250)
    public ChatSession(long id, int mFriendID, int mSelfID, int nUnreadCount, String mFriendNickName,
            String mLastMsg, String mRemark, Date time) {
        this.id = id;
        this.mFriendID = mFriendID;
        this.mSelfID = mSelfID;
        this.nUnreadCount = nUnreadCount;
        this.mFriendNickName = mFriendNickName;
        this.mLastMsg = mLastMsg;
        this.mRemark = mRemark;
        this.time = time;
    }

    @Generated(hash = 1350292942)
    public ChatSession() {
    }

    public int getmSelfID() {
        return mSelfID;
    }

    public void setmSelfID(int mSelfID) {
        this.mSelfID = mSelfID;
    }

    public Date getTime() {
        return time;
    }

    public void setTime(Date time) {
        this.time = time;
    }

    public String getFriendNickName() {
        return mFriendNickName;
    }

    public void setFriendNickName(String nickName) {
        mFriendNickName = nickName;
    }

    public int getFriendID() {
        return mFriendID;
    }

    public void setFriendID(int mFriendID) {
        this.mFriendID = mFriendID;
    }

    public String getLastMsg() {
        return mLastMsg;
    }

    public void setLastMsg(String lastMsg) {
        mLastMsg = lastMsg;
    }

    public void setmRemark(String remark) {
        mRemark = remark;
    }

    public String getRemark() {
        return mRemark;
    }

    public void addUnreadCount() {
        nUnreadCount++;
    }

    public void setUnreadCount(int n) {
        nUnreadCount = n;
    }

    public int getUnreadCount() {
        return nUnreadCount;
    }

    public long getId() {
        return id;
    }

    public void setId(long id) {
        this.id = id;
    }

    public int getSelfID() {
        return mSelfID;
    }

    public void setSelfID(int selfID) {
        mSelfID = selfID;
    }

    public int getnUnreadCount() {
        return nUnreadCount;
    }

    public void setnUnreadCount(int nUnreadCount) {
        this.nUnreadCount = nUnreadCount;
    }

    public void setRemark(String remark) {
        mRemark = remark;
    }

    public int getMFriendID() {
        return this.mFriendID;
    }

    public void setMFriendID(int mFriendID) {
        this.mFriendID = mFriendID;
    }

    public int getMSelfID() {
        return this.mSelfID;
    }

    public void setMSelfID(int mSelfID) {
        this.mSelfID = mSelfID;
    }

    public int getNUnreadCount() {
        return this.nUnreadCount;
    }

    public void setNUnreadCount(int nUnreadCount) {
        this.nUnreadCount = nUnreadCount;
    }

    public String getMFriendNickName() {
        return this.mFriendNickName;
    }

    public void setMFriendNickName(String mFriendNickName) {
        this.mFriendNickName = mFriendNickName;
    }

    public String getMLastMsg() {
        return this.mLastMsg;
    }

    public void setMLastMsg(String mLastMsg) {
        this.mLastMsg = mLastMsg;
    }

    public String getMRemark() {
        return this.mRemark;
    }

    public void setMRemark(String mRemark) {
        this.mRemark = mRemark;
    }


}
