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

        return null;
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

    private UserSession() {

    }
}
