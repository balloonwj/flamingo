package org.hootina.platform.userinfo;

import java.util.ArrayList;
import java.util.List;

/**
 * @desc    存储登录期间的所有用户信息
 * @author  zhangyl
 * @date    20170819
 */

public class UserSession {
    //当前用户信息
    public UserInfo              loginUser = new UserInfo();
    //用户好友信息
    public ArrayList<UserInfo>   friends = new ArrayList<UserInfo>();
    //群组信息
    public ArrayList<UserInfo>   groups  = new ArrayList<>();


    //该账户是否有群组信息
    private boolean              mExistGroup = false;

    private static UserSession   _instance = null;

    public static synchronized UserSession getInstance() {
        if (_instance == null)
            _instance = new UserSession();

        return _instance;
    }

    public synchronized int getGroupNum(){
        int groupNum = 0;
        for (UserInfo u : friends)
        {
            if (u.isGroup())
                ++groupNum;
        }

        return groupNum;
    }

    public synchronized UserInfo getGroupById(int groupid) {
        for (UserInfo g : groups)
        {
            if (g.get_userid() == groupid)
                return g;
        }

        return null;
    }

    public synchronized List<UserInfo> getGroupMembersById(int groupid) {
        for (UserInfo u : friends)
        {
            if (u.get_userid() == groupid)
                return u.groupMembers;
        }

        return null;
    }

    public synchronized UserInfo getUserInfoById(int userid) {
        for (UserInfo u : friends)
        {
            if (u.get_userid() == userid)
                return u;
        }

        if (loginUser.get_userid() == userid)
            return loginUser;

        for (UserInfo u : groups)
        {
            if (u.get_userid() == userid)
                return u;
        }

        return null;
    }

    public synchronized String getUsernameById(int userid) {
        for (UserInfo u : friends)
        {
            if (u.get_userid() == userid)
                return u.get_username();
        }

        if (loginUser.get_userid() == userid)
            return loginUser.get_username();

        for (UserInfo u : groups)
        {
            if (u.get_userid() == userid)
                return u.get_username();
        }

        return "";
    }

    public synchronized String getNicknameById(int userid) {
        for (UserInfo u : friends)
        {
            if (u.get_userid() == userid)
                return u.get_nickname();
        }

        if (loginUser.get_userid() == userid)
            return loginUser.get_nickname();

        for (UserInfo u : groups)
        {
            if (u.get_userid() == userid)
                return u.get_nickname();
        }

        return "";
    }

    public synchronized String getGroupMemberNickname(int groupid, int memberid) {
        for (UserInfo g : groups)
        {
            if (g.get_userid() == groupid) {
                for (UserInfo u : g.groupMembers) {
                    if (u.get_userid() == memberid) {
                        return u.get_nickname();
                    }
                }
            }
        }

        return "";
    }

    public synchronized boolean isFriend(int userid) {
        for (UserInfo u : friends)
        {
            if (u.get_userid() == userid)
                return true;
        }

        return false;
    }

    public synchronized boolean isSelf(int userid) {
        if (loginUser.get_userid() == userid)
            return true;

        return false;
    }

    public synchronized boolean isExistGroup() {
        return mExistGroup;
    }

    public synchronized void makeExistGroupFlag(boolean b) {
        mExistGroup = b;
    }

    public synchronized void clearFriendInfo(){
        friends.clear();
    }

    public synchronized void clearGroupInfo(){
        groups.clear();
    }

    private UserSession() {

    }
}
