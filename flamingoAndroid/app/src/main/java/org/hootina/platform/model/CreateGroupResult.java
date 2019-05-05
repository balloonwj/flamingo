package org.hootina.platform.model;

public class CreateGroupResult {


    /**
     * code : 0
     * msg : ok
     * groupid : 12345678
     * groupname : 我的群名称
     */

    private int code;
    private String msg;
    private int groupid;
    private String groupname;

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

    public String getGroupname() {
        return groupname;
    }

    public void setGroupname(String groupname) {
        this.groupname = groupname;
    }
}
