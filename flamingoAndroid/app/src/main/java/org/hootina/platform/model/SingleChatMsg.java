package org.hootina.platform.model;

import java.util.ArrayList;
import java.util.List;

public class SingleChatMsg {

    private static List<SingleChatMsg> SingleMsgs = new ArrayList<>();

    private int senderId;

    private int tartgetid;

    private String msg;

    public SingleChatMsg(int senderId, int tartgetid, String msg) {
        this.senderId = senderId;
        this.tartgetid = tartgetid;
        this.msg = msg;
    }

    public static List<SingleChatMsg> getSingleMsgs() {
        return SingleMsgs;
    }

    public static void setSingleMsgs(List<SingleChatMsg> singleMsgs) {
        SingleMsgs = singleMsgs;
    }

    public int getSenderId() {
        return senderId;
    }

    public void setSenderId(int senderId) {
        this.senderId = senderId;
    }

    public int getTartgetid() {
        return tartgetid;
    }

    public void setTartgetid(int tartgetid) {
        this.tartgetid = tartgetid;
    }

    public String getMsg() {
        return msg;
    }

    public void setMsg(String msg) {
        this.msg = msg;
    }


    public static List<SingleChatMsg> get(int senderId, int targetid) {

        List<SingleChatMsg> msgs = new ArrayList<>();

        for (SingleChatMsg msg : SingleMsgs) {
            if (msg.getSenderId() == senderId && msg.getTartgetid() == targetid) {
                msgs.add(msg);
            }
        }

        return msgs;
    }

    public static void add(SingleChatMsg msg) {
        SingleMsgs.add(msg);
    }
}
