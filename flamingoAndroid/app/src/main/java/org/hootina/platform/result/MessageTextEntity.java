package org.hootina.platform.result;

import android.util.JsonReader;

import org.hootina.platform.services.Face;
import org.hootina.platform.utils.LoggerFile;

import java.io.IOException;
import java.io.StringReader;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

public class MessageTextEntity {
	public static final int CONTENT_TYPE_UNKNOWN 		= 0;	// 未知消息类型
	public static final int CONTENT_TYPE_TEXT    		= 1;	// 文本类型（包括图文混排类型）
	public static final int CONTENT_TYPE_IMAGE_CONFIRM 	= 2;	// 图片上传成功后的确认消息类型  pc专用 （pc在上传图片成功后 补发的消息 用于去下载  pc和app端都要处理）
	public static final int CONTENT_TYPE_FILE           = 3;	// 收到好友发送过来的文件
	public static final int CONTENT_TYPE_SHAKE_WINDOW   = 4;	// 窗口抖动  pc专用
	public static final int CONTENT_TYPE_MOBILE_IMAGE   = 5;	// app端发出的图片  （app端发送的图片信息，已经上传好才会发送的这一条  pc接到这个类型可以直接显示）

	private String 				mMsgID; 		// 消息ID
	private long 				mMsgTime;
	private int 				mMsgType;		// 消息类型
	private int					mSenderID;
	private String				mSenderNickname;
	private int					mTargetID;
	private String				mTargetNickname;
	private int 				mClientType; 	// 1.pc端 2.IOS 3.Android
	private List<ContentText> 	mContent;
	private String mImgFile;

	public String getImgFile() {
		return mImgFile;
	}

	public void setImgFile(String imgFile) {
		mImgFile = imgFile;
	}

	public String getMsgID() {
		return mMsgID;
	}

	public void setMsgID(String msgID) {
		mMsgID = msgID;
	}

	public long getMsgTime() {
		return mMsgTime;
	}

	public void setMsgTime(long msgTime) {
		mMsgTime = msgTime;
	}

	public int getMsgType() {
		return mMsgType;
	}

	public void setMsgType(int msgType) {
		mMsgType = msgType;
	}

	public int getSenderID(){
		return mSenderID;
	}

	public void setSenderID(int senderID){
		mSenderID = senderID;
	}

	public String getSenderNickname(){
		return mSenderNickname;
	}

	public void setSenderNickname(String senderNickname){
		mSenderNickname = senderNickname;
	}

	public int getTargetID(){
		return mTargetID;
	}

	public void setTargetID(int targetID){
		mTargetID = targetID;
	}

	public String getTargetNickname(){
		return mTargetNickname;
	}

	public void setTargetNickname(String targetNickname){
		mTargetNickname = targetNickname;
	}

	public int getClientType() {
		return mClientType;
	}

	public void setClientType(int clientType) {
		mClientType = clientType;
	}

	public List<ContentText> getContent() {
		return mContent;
	}

	public void setContent(List<ContentText> content) {
		mContent = content;
	}

	//将|[1]||[2]|uu转换成[{"faceID":1},{"faceID":2},{"faceID":-1,"msgText":"uu"}]
	public String contentToJson(){
		ContentText c = null;
		int size = mContent.size();
		String str = "[";
		//TODO: 效率太低，改成用StringBuilder重写
		for (int i = 0; i < size; ++i){
			c = mContent.get(i);
			if (c.getFaceID() != Face.DEFAULT_NULL_FACEID) {
				str += "{\"faceID\":";
				str += c.getFaceID();
				str += "}";
			} else {
				str += "{\"msgText\":\"";
				str += c.getMsgText();
				str += "\"}";
			}

			str += ",";
		}

		str = str.substring(0, str.length() - 1);
		str += "]";

		return str;
	}

	//strJson: [{"faceID":1},{"faceID":2},{"faceID":-1,"msgText":"uu"}]
	public static MessageTextEntity jsonToEntity(String strJson){
		MessageTextEntity entity = new MessageTextEntity();
		List<ContentText> obj = new ArrayList<ContentText>();
		entity.setContent(obj);
		ContentText c = null;
		int    faceId;
		String text = "";

		try {
			JsonReader reader = new JsonReader(new StringReader(strJson));

			reader.beginArray();
			while (reader.hasNext()) {
				reader.beginObject();
				while(reader.hasNext()){
					String name = reader.nextName();
					//注意：这里不能使用==，必须使用equals
				    if (name.equals("faceID")){
						faceId = reader.nextInt();
						c = new ContentText();
						c.setFaceID(faceId);
                        obj.add(c);
					} else if (name.equals("msgText")){
						text = reader.nextString();
						c = new ContentText();
						c.setMsgText(text);
                        obj.add(c);
					} else {
                        reader.skipValue();
                    }
				}

				reader.endObject();
			}// end while-loop
			reader.endArray();

			reader.close();

		} catch (NumberFormatException e) {
            e.printStackTrace();
            LoggerFile.LogError("parse chat content json error, json=", strJson);
        } catch (IllegalStateException e){
            LoggerFile.LogError("parse chat content json error, json=", strJson);
            e.printStackTrace();
        } catch (IOException e){
			LoggerFile.LogError("parse chat content json error, json=", strJson);
            e.printStackTrace();
		}

		return entity;
	}

	//可以解析文本："rt"
	//或"||1||||2||rt"
	public static MessageTextEntity rawStringToEntity(String raw){
		MessageTextEntity entity = new MessageTextEntity();
		List<ContentText> obj = new ArrayList<ContentText>();
		entity.setContent(obj);
		ContentText c = null;

		String rawChatMsg2 = raw.replace("||", "|");
		String[] ss = rawChatMsg2.split("\\|");

		String element = "";
		String faceId = "";
		int faceIdNum;

		for (int i = 0; i < ss.length; i++) {
			//Map<String, Object> map = new HashMap<String, Object>();
			element = ss[i];
			if (element.isEmpty())
				continue;

			if (element.startsWith("[") && element.endsWith("]")) {
				faceId = element.substring(1, element.length() - 1);
				//map.put("faceID", Integer.valueOf(faceId));
				if (faceId.isEmpty())
					continue;

				try {
					faceIdNum = Integer.parseInt(faceId);
				} catch (NumberFormatException e) {
					e.printStackTrace();
					continue;
				}

				c = new ContentText();
				c.setFaceID(faceIdNum);
			} else {
				c = new ContentText();
				c.setMsgText(element);
				//map.put("msgText", id);
			}

			obj.add(c);
			//obj.add(map);
		}
		entity.setContent(obj);


		return entity;
	}

	public static String generateMsgID(){
		return UUID.randomUUID().toString();
	}

}
