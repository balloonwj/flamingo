package org.hootina.platform.model;

public class SearchUser {

    public SearchUser(int type, String username) {
        this.type = type;
        this.username = username;
    }

    /**
     * type : 1
     * username : zhangyl
     */

    private int type;
    private String username;

    public int getType() {
        return type;
    }

    public void setType(int type) {
        this.type = type;
    }

    public String getUsername() {
        return username;
    }

    public void setUsername(String username) {
        this.username = username;
    }
}
