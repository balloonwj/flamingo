package org.hootina.platform.model;

import org.hootina.platform.utils.StringUtil;

import java.util.List;

public class ChatContent {


    /**
     * senderId : 1
     * targetId : 2
     * msgType : 1
     * time : 2434167
     * clientType : 0
     * font : ["fontname","fontSize","fontColor","fontBold","fontItalic","fontUnderline"]
     * content : [{"msgText":"text1"},{"faceID":102},{"pic":["name","server_path",400,50,60]},{"remotedesktop":1},{"shake":1},{"file":["name","server_path",400,0]}]
     */

    private long id;

    private int senderId;
    private int targetId;
    private int msgType;
    private long time;
    private int clientType;
    private List<String> font;
    private List<ContentBean> content;

    public long getId() {
        return id;
    }

    public void setId(long id) {
        this.id = id;
    }

    public int getSenderId() {
        return senderId;
    }

    public void setSenderId(int senderId) {
        this.senderId = senderId;
    }

    public int getTargetId() {
        return targetId;
    }

    public void setTargetId(int targetId) {
        this.targetId = targetId;
    }

    public int getMsgType() {
        return msgType;
    }

    public void setMsgType(int msgType) {
        this.msgType = msgType;
    }

    public long getTime() {
        return time;
    }

    public void setTime(long time) {
        this.time = time;
    }

    public int getClientType() {
        return clientType;
    }

    public void setClientType(int clientType) {
        this.clientType = clientType;
    }

    public List<String> getFont() {
        return font;
    }

    public void setFont(List<String> font) {
        this.font = font;
    }

    public List<ContentBean> getContent() {
        return content;
    }

    public void setContent(List<ContentBean> content) {
        this.content = content;
    }

    public static class ContentBean {
        /**
         * msgText : text1
         * faceID : 102
         * pic : ["name","server_path",400,50,60]
         * remotedesktop : 1
         * shake : 1
         * file : ["name","server_path",400,0]
         */

        public static final int ERROR_FACEID = -1000;

        private String msgText;
        private int faceID = ERROR_FACEID;
        private int remotedesktop;
        private int shake;
        private List<String> pic;
        private List<String> file;

        public String getMsgText() {
            return msgText;
        }

        public void setMsgText(String msgText) {
            this.msgText = msgText;
        }

        public int getFaceID() {
            return faceID;
        }

        public void setFaceID(int faceID) {
            this.faceID = faceID;
        }

        public int getRemotedesktop() {
            return remotedesktop;
        }

        public void setRemotedesktop(int remotedesktop) {
            this.remotedesktop = remotedesktop;
        }

        public int getShake() {
            return shake;
        }

        public void setShake(int shake) {
            this.shake = shake;
        }

        public List<String> getPic() {
            return pic;
        }

        public void setPic(List<String> pic) {
            this.pic = pic;
        }

        public List<String> getFile() {
            return file;
        }

        public void setFile(List<String> file) {
            this.file = file;
        }
    }


    public String toJson() {
        StringBuilder stringBuilder = new StringBuilder();
        stringBuilder.append("{");
        stringBuilder.append("\"msgType\":" + getMsgType() + ", ");
        stringBuilder.append("\"time\":" + getTime() + ", ");
        stringBuilder.append("\"clientType\":" + getClientType() + ", ");
        stringBuilder.append("\"content\":");
        stringBuilder.append("[");

        //先计算一下faceID的数目
        int itemNum = 0;
        for (ContentBean bean : content) {
            if (!StringUtil.isEmpty(bean.getMsgText())) {
                ++ itemNum;
            }

            if (bean.getFaceID() != ContentBean.ERROR_FACEID) {
                ++ itemNum;
            }
        }

        int index = 0;
        //{"clientType":2,"content":[{"faceID":"53"},"faceID","53",{"msgText":"helloworld"}],"msgType":1,"time":1539258336}
        for (ContentBean bean : content) {
            if (!StringUtil.isEmpty(bean.getMsgText())) {
                stringBuilder.append("{\"msgText\":" + "\"" + bean.getMsgText() + "\"" + "}");
                //每个节点之间用逗号分隔，最后一个不加逗号
                if (index != itemNum - 1)
                    stringBuilder.append(", ");
            }

            if (bean.getFaceID() != ContentBean.ERROR_FACEID) {
                stringBuilder.append("{\"faceID\":" + bean.getFaceID() + "}");
                //每个节点之间用逗号分隔，最后一个不加逗号
                if (index != itemNum - 1)
                    stringBuilder.append(", ");
            }

            ++ index;
        }
        stringBuilder.append("]");
        stringBuilder.append("}");
        return stringBuilder.toString();
    }
}
