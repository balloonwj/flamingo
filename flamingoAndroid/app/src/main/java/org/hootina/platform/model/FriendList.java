package org.hootina.platform.model;

import org.hootina.platform.userinfo.UserInfo;

import java.util.Iterator;
import java.util.List;

//TODO: 需要整合到UserSession中去
public class FriendList {
    private static FriendList mInstance;

    public static FriendList getInstance() {
        if (mInstance == null) {
            mInstance = new FriendList();
        }
        return mInstance;
    }

    public static void setFriendList(FriendList friendList) {
        mInstance = friendList;
    }

    /**
     * code : 0
     * msg : ok
     * userinfo : [{"members":[{"address":"","birthday":19900101,"clienttype":0,"customface":"","facetype":8,"gender":0,"mail":"","markname":"","nickname":"癞头僧先生","phonenumber":"","signature":"欢迎使用flamingo。","status":0,"userid":3,"username":"zhangy"},{"address":"","birthday":19900101,"clienttype":0,"customface":"","facetype":0,"gender":0,"mail":"","markname":"","nickname":"rty","phonenumber":"","signature":"","status":0,"userid":497,"username":"rty"},{"address":"","birthday":19900101,"clienttype":0,"customface":"","facetype":0,"gender":0,"mail":"","markname":"","nickname":"asd","phonenumber":"","signature":"","status":0,"userid":256,"username":"asd"},{"address":"上海市浦东新区南泉路1200号409室","birthday":20170914,"clienttype":0,"customface":"","facetype":5,"gender":0,"mail":"balloonwj@qq.com","markname":"","nickname":"qqq123","phonenumber":"","signature":"{\u201cid\u201d：12}","status":0,"userid":10,"username":"qqq"}],"teamname":"My Friends"}]
     */

    private int code;
    private String msg;
    private List<UserinfoBean> userinfo;

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

    public List<UserinfoBean> getUserinfo() {
        return userinfo;
    }

    public void setUserinfo(List<UserinfoBean> userinfo) {
        this.userinfo = userinfo;
    }

    public synchronized String getUserName(int userID) {
        if (userinfo == null)
            return "";

        for (UserinfoBean u : userinfo) {
            if (u.members == null)
                continue;

            for (UserinfoBean.MembersBean mb : u.members) {
                if (mb.getUserid() == userID)
                    if (UserInfo.isGroup(userID))
                        return mb.getNickname();        //TODO：群号的username等于groupid，需要改成等于nickname
                    else
                        return mb.getUsername();
            }
        }

        return "";
    }

    public synchronized String getNickName(int userId) {
        if (userinfo != null) {
            for (UserinfoBean userinfoBean : userinfo) {
                for (UserinfoBean.MembersBean membersBean : userinfoBean.getMembers()) {
                    if (userId == membersBean.getUserid()) {
                        return membersBean.getNickname();
                    }
                }
            }
        }
        return "";
    }

    public synchronized void deleteUser(int userId) {
        for (UserinfoBean bean : userinfo) {
            Iterator<UserinfoBean.MembersBean> iterator =  bean.getMembers().iterator();
            while (iterator.hasNext()) {
                if (iterator.next().getUserid() == userId) {
                    iterator.remove();
                }
            }
        }
    }

    public static class UserinfoBean {
        /**
         * members : [{"address":"","birthday":19900101,"clienttype":0,"customface":"","facetype":8,"gender":0,"mail":"","markname":"","nickname":"癞头僧先生","phonenumber":"","signature":"欢迎使用flamingo。","status":0,"userid":3,"username":"zhangy"},
         * {"address":"","birthday":19900101,"clienttype":0,"customface":"","facetype":0,"gender":0,"mail":"","markname":"","nickname":"rty","phonenumber":"","signature":"","status":0,"userid":497,"username":"rty"},
         * {"address":"","birthday":19900101,"clienttype":0,"customface":"","facetype":0,"gender":0,"mail":"","markname":"","nickname":"asd","phonenumber":"","signature":"","status":0,"userid":256,"username":"asd"},
         * {"address":"上海市浦东新区南泉路1200号409室","birthday":20170914,"clienttype":0,"customface":"","facetype":5,"gender":0,"mail":"balloonwj@qq.com","markname":"","nickname":"qqq123","phonenumber":"","signature":"{\u201cid\u201d：12}","status":0,"userid":10,"username":"qqq"}]
         * teamname : My Friends
         */

        private String              teamname;
        private List<MembersBean>   members;

        public String getTeamname() {
            return teamname;
        }

        public void setTeamname(String teamname) {
            this.teamname = teamname;
        }

        public List<MembersBean> getMembers() {
            return members;
        }

        public void setMembers(List<MembersBean> members) {
            this.members = members;
        }

        public static class MembersBean {
            /**
             * address :
             * birthday : 19900101
             * clienttype : 0
             * customface :
             * facetype : 8
             * gender : 0
             * mail :
             * markname :
             * nickname : 癞头僧先生
             * phonenumber :
             * signature : 欢迎使用flamingo。
             * status : 0
             * userid : 3
             * username : zhangy
             */

            private String address;
            private int birthday;
            private int clienttype;
            private String customface;
            private int facetype;
            private int gender;
            private String mail;
            private String markname;
            private String nickname;
            private String phonenumber;
            private String signature;
            private int status;
            private int userid;
            private String username;
            private String state;

            public String getState() {
                return state;
            }

            public void setState(String state) {
                this.state = state;
            }

            public String getAddress() {
                return address;
            }

            public void setAddress(String address) {
                this.address = address;
            }

            public int getBirthday() {
                return birthday;
            }

            public void setBirthday(int birthday) {
                this.birthday = birthday;
            }

            public int getClienttype() {
                return clienttype;
            }

            public void setClienttype(int clienttype) {
                this.clienttype = clienttype;
            }

            public String getCustomface() {
                return customface;
            }

            public void setCustomface(String customface) {
                this.customface = customface;
            }

            public int getFacetype() {
                return facetype;
            }

            public void setFacetype(int facetype) {
                this.facetype = facetype;
            }

            public int getGender() {
                return gender;
            }

            public void setGender(int gender) {
                this.gender = gender;
            }

            public String getMail() {
                return mail;
            }

            public void setMail(String mail) {
                this.mail = mail;
            }

            public String getMarkname() {
                return markname;
            }

            public void setMarkname(String markname) {
                this.markname = markname;
            }

            public String getNickname() {
                return nickname;
            }

            public void setNickname(String nickname) {
                this.nickname = nickname;
            }

            public String getPhonenumber() {
                return phonenumber;
            }

            public void setPhonenumber(String phonenumber) {
                this.phonenumber = phonenumber;
            }

            public String getSignature() {
                return signature;
            }

            public void setSignature(String signature) {
                this.signature = signature;
            }

            public int getStatus() {
                return status;
            }

            public void setStatus(int status) {
                this.status = status;
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
        }
    }
}
