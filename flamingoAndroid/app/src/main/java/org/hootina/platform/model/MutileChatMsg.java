package org.hootina.platform.model;

import java.util.ArrayList;
import java.util.List;

public class MutileChatMsg {

    private static List<MutileChatMsg> sMutileChatMsgs = new ArrayList<>();

    private int groupid; // 群id

    private int senderid; //发言人

    private String msg; //消息内容

    public MutileChatMsg(int groupid, int senderid, String msg) {
        this.groupid = groupid;
        this.senderid = senderid;
        this.msg = msg;
    }

    public static List<MutileChatMsg> getMutileChatMsgs() {
        return sMutileChatMsgs;
    }


    public int getGroupid() {
        return groupid;
    }

    public void setGroupid(int groupid) {
        this.groupid = groupid;
    }

    public int getSenderid() {
        return senderid;
    }

    public void setSenderid(int senderid) {
        this.senderid = senderid;
    }

    public String getMsg() {
        return msg;
    }

    public void setMsg(String msg) {
        this.msg = msg;
    }

    public static List<MutileChatMsg> get(int groupid) {
        List<MutileChatMsg> tmp = new ArrayList<>();
        for (MutileChatMsg msg : sMutileChatMsgs) {
            if (msg.getGroupid() == groupid) {
                tmp.add(msg);
            }
        }

        return tmp;
    }

    public static void add( MutileChatMsg msg) {
        sMutileChatMsgs.add(msg);
    }
}
