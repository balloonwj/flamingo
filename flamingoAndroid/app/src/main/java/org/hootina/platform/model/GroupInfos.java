package org.hootina.platform.model;

import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;

public class GroupInfos {

    private static GroupInfos   sInstance;
    private List<GroupInfo>     mGroupInfos = new ArrayList<>();

    public static GroupInfos getInstance() {
        if (sInstance == null) {
            sInstance = new GroupInfos();
        }

        return sInstance;
    }

    public synchronized void setGroupInfos(List<GroupInfo> groupInfos) {
        mGroupInfos = groupInfos;
    }

    public synchronized List<GroupInfo> getGroupInfos() {
        return mGroupInfos;
    }

    public boolean hasLoaded(int userId) {
        for (GroupInfo groupInfo : mGroupInfos) {
            for (GroupInfo.MembersBean bean : groupInfo.getMembers()) {
                if (bean.getUserid() == userId) {
                    return true;
                }
            }
        }
        return false;
    }

    public synchronized String getGroupName(int groupID) {
        for (GroupInfo groupInfo : mGroupInfos) {
            if (groupInfo.getGroupid() == groupID) {
                return groupInfo.getGroupName();
            }
        }

        return "";
    }

    public synchronized String getGroupMemberNickName(int groupID, int memberUserID) {
        for (GroupInfo groupInfo : mGroupInfos) {
            if (groupInfo.getGroupid() != groupID)
                continue;

            for (GroupInfo.MembersBean m : groupInfo.getMembers()) {
                if (m.getUserid() == memberUserID) {
                    return m.getNickname();
                }
            }
        }

        return "";
    }


    public synchronized String getNickName(int userid) {
        for (GroupInfo groupInfo : mGroupInfos) {
            for (GroupInfo.MembersBean membersBean : groupInfo.getMembers()) {
                if (membersBean.getUserid() == userid) {
                    return membersBean.getNickname();
                }
            }
        }

        return "";
    }

    public synchronized void deleteGroup(int groupId) {
        Iterator<GroupInfo> iterator = mGroupInfos.iterator();

        while (iterator.hasNext()) {
            if (iterator.next().getGroupid() == groupId) {
                iterator.remove();
                break;
            }
        }
    }

}
