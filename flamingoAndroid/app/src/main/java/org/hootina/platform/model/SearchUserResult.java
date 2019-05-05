package org.hootina.platform.model;

import java.util.List;

public class SearchUserResult {

    /**
     * code : 0
     * msg : ok
     * userinfo : [{"userid":10,"username":"qqq","nickname":"qqq123","facetype":5}]
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

    public static class UserinfoBean {
        /**
         * userid : 10
         * username : qqq
         * nickname : qqq123
         * facetype : 5
         */

        private int userid;
        private String username;
        private String nickname;
        private int facetype;

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
    }
}
