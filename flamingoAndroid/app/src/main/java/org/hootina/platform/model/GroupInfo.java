package org.hootina.platform.model;

import java.util.List;

public class GroupInfo {

    /**
     * code : 0
     * msg : ok
     * groupid : 268435457
     * members : [{"userid":3,"username":"zhangy","nickname":"癞头僧先生","facetype":8,"customface":"","gender":0,"birthday":19900101,"signature":"欢迎使用flamingo。","address":"","phonenumber":"","mail":"","clienttype":1,"status":3},{"userid":19,"username":"13570330825","nickname":"liyongc","facetype":0,"customface":"","gender":0,"birthday":19900101,"signature":"","address":"","phonenumber":"","mail":"","clienttype":1,"status":0},{"userid":34,"username":"15150217213","nickname":"xiaoqiang","facetype":18,"customface":"","gender":0,"birthday":19900101,"signature":"","address":"","phonenumber":"","mail":"","clienttype":1,"status":0},{"userid":43,"username":"158","nickname":"如果如是","facetype":0,"customface":"","gender":0,"birthday":19900101,"signature":"","address":"","phonenumber":"","mail":"","clienttype":1,"status":0},{"userid":85,"username":"13322888888","nickname":"apple","facetype":0,"customface":"","gender":0,"birthday":19900101,"signature":"","address":"","phonenumber":"","mail":"","clienttype":1,"status":0},{"userid":105,"username":"18893479251","nickname":"榴莲宅","facetype":0,"customface":"","gender":0,"birthday":19900101,"signature":"","address":"","phonenumber":"","mail":"","clienttype":1,"status":0},{"userid":104,"username":"15082671007","nickname":"小白A","facetype":0,"customface":"","gender":0,"birthday":19900101,"signature":"","address":"","phonenumber":"","mail":"","clienttype":1,"status":0},{"userid":16,"username":"17091203068","nickname":"Barry","facetype":0,"customface":"","gender":0,"birthday":19900101,"signature":"","address":"","phonenumber":"","mail":"","clienttype":1,"status":0},{"userid":106,"username":"18299081534","nickname":"扎西。","facetype":25,"customface":"","gender":0,"birthday":19900101,"signature":"","address":"","phonenumber":"","mail":"wuboui","clienttype":1,"status":0},{"userid":33,"username":"15827490321","nickname":"未央","facetype":0,"customface":"","gender":0,"birthday":19900101,"signature":"","address":"","phonenumber":"","mail":"","clienttype":1,"status":0},{"userid":107,"username":"18611547622","nickname":"123木头人","facetype":0,"customface":"","gender":0,"birthday":19900101,"signature":"","address":"","phonenumber":"","mail":"","clienttype":1,"status":0},{"userid":108,"username":"五彩缤纷","nickname":"五彩缤纷","facetype":0,"customface":"","gender":0,"birthday":19900101,"signature":"","address":"","phonenumber":"","mail":"","clienttype":1,"status":0},{"userid":110,"username":"13268285402","nickname":"Yan1","facetype":0,"customface":"","gender":0,"birthday":19900101,"signature":"","address":"","phonenumber":"","mail":"","clienttype":1,"status":0},{"userid":109,"username":"13080786315","nickname":"xtcow","facetype":0,"customface":"","gender":0,"birthday":19900101,"signature":"","address":"","phonenumber":"","mail":"","clienttype":1,"status":0},{"userid":111,"username":"15073315025","nickname":"Fagan","facetype":0,"customface":"","gender":0,"birthday":19900101,"signature":"","address":"","phonenumber":"","mail":"","clienttype":1,"status":0},{"userid":112,"username":"15989113989","nickname":"西瓜瓜","facetype":0,"customface":"","gender":0,"birthday":19900101,"signature":"","address":"","phonenumber":"","mail":"","clienttype":1,"status":0},{"userid":10,"username":"qqq","nickname":"qqq123","facetype":5,"customface":"","gender":0,"birthday":20170914,"signature":"{\u201cid\u201d：12}","address":"上海市浦东新区南泉路1200号409室","phonenumber":"","mail":"balloonwj@qq.com","clienttype":1,"status":0},{"userid":114,"username":"13261861976","nickname":"BrianYi","facetype":13,"customface":"","gender":0,"birthday":19900101,"signature":"","address":"","phonenumber":"","mail":"","clienttype":1,"status":0},{"userid":10,"username":"qqq","nickname":"qqq123","facetype":5,"customface":"","gender":0,"birthday":20170914,"signature":"{\u201cid\u201d：12}","address":"上海市浦东新区南泉路1200号409室","phonenumber":"","mail":"balloonwj@qq.com","clienttype":1,"status":0}]
     */

    private int                 code;
    private String              msg;
    private int                 groupid;
    private String              groupName;
    private List<MembersBean>   members;

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

    public int getGroupid() {
        return groupid;
    }

    public void setGroupid(int groupid) {
        this.groupid = groupid;
    }

    public String getGroupName() {
        return groupName;
    }

    public void setGroupName(String groupName) {
        this.groupName = groupName;
    }

    public List<MembersBean> getMembers() {
        return members;
    }

    public void setMembers(List<MembersBean> members) {
        this.members = members;
    }

    public static class MembersBean {
        /**
         * userid : 3
         * username : zhangy
         * nickname : 癞头僧先生
         * facetype : 8
         * customface :
         * gender : 0
         * birthday : 19900101
         * signature : 欢迎使用flamingo。
         * address :
         * phonenumber :
         * mail :
         * clienttype : 1
         * status : 3
         */

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
        private int clienttype;
        private int status;

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

        public int getClienttype() {
            return clienttype;
        }

        public void setClienttype(int clienttype) {
            this.clienttype = clienttype;
        }

        public int getStatus() {
            return status;
        }

        public void setStatus(int status) {
            this.status = status;
        }
    }
}
