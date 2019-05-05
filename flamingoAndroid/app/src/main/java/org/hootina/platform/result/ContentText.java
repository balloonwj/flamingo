package org.hootina.platform.result;

import org.hootina.platform.model.ChatContent;
import org.hootina.platform.services.Face;

import java.util.Arrays;

public class ContentText {
    private String msgText;
    private int faceID = Face.DEFAULT_NULL_FACEID;
    private String pic;

    public String getMsgText() {
        return msgText;
    }

    public String getPic() {
        return pic;
    }

    public void setPic(String pic) {
        this.pic = pic;
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

    public ChatContent.ContentBean toContentBean() {
        ChatContent.ContentBean contentBean = new ChatContent.ContentBean();
        contentBean.setMsgText(msgText);
        contentBean.setFaceID(faceID);
        contentBean.setPic(Arrays.asList(pic));
        return contentBean;
    }
}
