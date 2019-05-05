package org.hootina.platform.model;

public class CreateGroup {

    public CreateGroup(String groupname) {
        this.groupname = groupname;
        this.type = 0;
    }

    private String groupname;
    private int type;

    public String getGroupname() {
        return groupname;
    }

    public void setGroupname(String groupname) {
        this.groupname = groupname;
    }

    public int getType() {
        return type;
    }

    public void setType(int type) {
        this.type = type;
    }
}
