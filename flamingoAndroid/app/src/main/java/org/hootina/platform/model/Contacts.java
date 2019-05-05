package org.hootina.platform.model;

import org.greenrobot.greendao.annotation.Entity;
import org.greenrobot.greendao.annotation.Id;
import org.greenrobot.greendao.annotation.Keep;
import org.greenrobot.greendao.annotation.Generated;

@Entity
public class Contacts {


   @Keep
    public Contacts(int userid, int type) {
        this.userid = userid;
        this.type = type;
    }

    @Keep
    public Contacts(int userid, int type, String username, int accept) {
        this.userid = userid;
        this.type = type;
        this.username = username;
        this.accept = accept;
    }

    @Generated(hash = 1287593394)
    public Contacts(long id, int userid, int type, String username, int accept) {
        this.id = id;
        this.userid = userid;
        this.type = type;
        this.username = username;
        this.accept = accept;
    }

    @Generated(hash = 1804918957)
    public Contacts() {
    }




//    @Override
//    public boolean equals(Object obj) {
//        if (obj instanceof DealUser) {
//            DealUser dealUser = (DealUser) obj;
//
//        }
//
//    }

    /**
     * userid : 9
     * type : 3
     * username : xxx
     * accept : 1
     */



    @Id
    private long id;
    private int userid;
    private int type;
    private String username;
    private int accept;

    public long getId() {
        return id;
    }

    public void setId(long id) {
        this.id = id;
    }

    public int getUserid() {
        return userid;
    }

    public void setUserid(int userid) {
        this.userid = userid;
    }

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

    public int getAccept() {
        return accept;
    }

    public void setAccept(int accept) {
        this.accept = accept;
    }


}
