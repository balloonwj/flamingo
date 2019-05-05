package org.hootina.platform.model;


import java.util.List;

public class GetMessage {

    /**
     * clientType : 2
     * content : [{"faceID":-1,"msgText":"jvc"}]
     * msgID : 221a0c9f-e902-40e6-ac7e-2e7c5b32baa8
     * msgTime : 1538284181
     * msgType : 1
     * senderID : 10
     * targetID : 102
     */

    private int clientType;
    private String msgID;
    private int msgTime;
    private int msgType;
    private int senderID;
    private int targetID;
    private List<ContentBean> content;

    public int getClientType() {
        return clientType;
    }

    public void setClientType(int clientType) {
        this.clientType = clientType;
    }

    public String getMsgID() {
        return msgID;
    }

    public void setMsgID(String msgID) {
        this.msgID = msgID;
    }

    public int getMsgTime() {
        return msgTime;
    }

    public void setMsgTime(int msgTime) {
        this.msgTime = msgTime;
    }

    public int getMsgType() {
        return msgType;
    }

    public void setMsgType(int msgType) {
        this.msgType = msgType;
    }

    public int getSenderID() {
        return senderID;
    }

    public void setSenderID(int senderID) {
        this.senderID = senderID;
    }

    public int getTargetID() {
        return targetID;
    }

    public void setTargetID(int targetID) {
        this.targetID = targetID;
    }

    public List<ContentBean> getContent() {
        return content;
    }

    public void setContent(List<ContentBean> content) {
        this.content = content;
    }

    public static class ContentBean {
        /**
         * faceID : -1
         * msgText : jvc
         */

        private int faceID;
        private String msgText;

        public int getFaceID() {
            return faceID;
        }

        public void setFaceID(int faceID) {
            this.faceID = faceID;
        }

        public String getMsgText() {
            return msgText;
        }

        public void setMsgText(String msgText) {
            this.msgText = msgText;
        }
    }
}
