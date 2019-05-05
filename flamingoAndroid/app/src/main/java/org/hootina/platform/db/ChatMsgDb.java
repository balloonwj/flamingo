package org.hootina.platform.db;

import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.database.SQLException;
import android.database.sqlite.SQLiteDatabase;
import android.database.sqlite.SQLiteException;
import android.database.sqlite.SQLiteOpenHelper;
import android.util.Log;

import org.hootina.platform.result.ChatMessage;
import org.hootina.platform.result.ChatSession;
import org.hootina.platform.result.MemberEntity;
import org.hootina.platform.result.MessageTextEntity;
import org.hootina.platform.userinfo.UserInfo;
import org.hootina.platform.userinfo.UserSession;
import org.hootina.platform.utils.LoggerFile;

import java.util.ArrayList;
import java.util.List;

/**
 * 聊天消息存储的数据库
 * Created by zhangyl on 2018/1/8.
 */

public class ChatMsgDb extends SQLiteOpenHelper {
    //每次最多取8条数据
    public static final int MAX_CHATMSG_NUM = 8;
    public static final int MAX_CHATSESSION_NUM = 100;
    public static final String CHAT_MSG_DB = "t_chatmsg";
    public static final String CHAT_SESSION_DB = "t_chatsession";

    public static final String SQL_CREATE_CHATMSGDB = "create table "
            + CHAT_MSG_DB
            + "(id integer primary key autoincrement, "
            + "msgID text, "
            + "msgTime integer, "
            + "senderID integer, "
            + "targetID integer, "
            + "msgType integer, "
            + "msgContent text, "
            + "clientType integer, "
            + "unreadFlag integer, "
            + "path text, "
            + "remark text)";

    public static final String SQL_CREATE_SESSIONDB = "create table "
            + CHAT_SESSION_DB
            + "(id integer primary key autoincrement, "
            + "sessionID integer, "
            + "lastMsg text, "
            + "remark text)";

    private Context mContext;

    private int mUnreadChatMsgCount = 0;

    public ChatMsgDb(Context context, String name, SQLiteDatabase.CursorFactory factory, int version) {
        super(context, name, factory, version);
        mContext = context;
    }

    public synchronized boolean insertChatMsg(String msgID,
                                              long msgTime,
                                              int senderID,
                                              int targetID,
                                              int msgType,
                                              String msgContent,
                                              int clientType,
                                              int unreadFlag,
                                              String remark) {
        SQLiteDatabase db = getWritableDatabase();
        db.beginTransaction(); // 开启事务
        try {
            ContentValues values = new ContentValues();
            values.put("msgID", msgID);
            values.put("msgTime", msgTime);
            values.put("senderID", senderID);
            values.put("targetID", targetID);
            values.put("msgType", msgType);
            values.put("msgContent", msgContent);
            values.put("clientType", clientType);
            values.put("unreadFlag", unreadFlag);
            values.put("remark", remark);
            db.insert(CHAT_MSG_DB, null, values);
            db.setTransactionSuccessful(); // 事务已经执行成功
        } catch (Exception e) {
            e.printStackTrace();
            return false;
        } finally {
            db.endTransaction(); // 结束事务
        }

        //mUnreadChatMsgCount++;

        return true;
    }

    public synchronized boolean insertChatMsg(String msgID,
                                              long msgTime,
                                              int senderID,
                                              int targetID,
                                              int msgType,
                                              String msgContent,
                                              int clientType,
                                              int unreadFlag,
                                              String remark,
                                              String filePath) {
        SQLiteDatabase db = getWritableDatabase();
        db.beginTransaction(); // 开启事务
        try {
            ContentValues values = new ContentValues();
            values.put("msgID", msgID);
            values.put("msgTime", msgTime);
            values.put("senderID", senderID);
            values.put("targetID", targetID);
            values.put("msgType", msgType);
            values.put("msgContent", msgContent);
            values.put("clientType", clientType);
            values.put("unreadFlag", unreadFlag);
            values.put("remark", remark);
            values.put("path", filePath);
            db.insert(CHAT_MSG_DB, null, values);
            db.setTransactionSuccessful(); // 事务已经执行成功
        } catch (Exception e) {
            //e.printStackTrace();
            LoggerFile.LogError("[insertChatMsg]db.insert error, errorMsg: " + e.getMessage());
            return false;
        } finally {
            db.endTransaction(); // 结束事务
        }

        //mUnreadChatMsgCount++;

        return true;
    }

    public synchronized List<MessageTextEntity> getChatMsgBySenderAndTargetID(int senderID, int targetID) {
        SQLiteDatabase db = getWritableDatabase();
        // 查询Book表中所有的数据
        List<MessageTextEntity> pendingMsgList = new ArrayList<>();
        Cursor cursor = null;
        try {
            cursor = db.rawQuery("SELECT msgID, msgTime, senderID, targetID, msgType, msgContent, clientType, path, unreadFlag from "
                    + CHAT_MSG_DB + " WHERE (senderID=" + senderID + " AND targetID=" + targetID
                    + ") or (senderID=" + targetID + " AND targetID=" + senderID + ") ", null);
        } catch (Exception e) {
            LoggerFile.LogError("db.rawQuery error, errorMsg: " + e.getMessage());
            return null;
        }

        if (cursor == null)
            return null;

        String msgID = "";
        long msgTime = 0;
        int msgType = 0;
        String senderName = "";
        String targetName = "";
        String msgContent = "";
        int clientType;
        int unreadFlag;
        String filePath;
        MessageTextEntity entity = null;
        if (cursor.moveToFirst()) {
            do {
                // 遍历Cursor对象，取出数据并打印
                msgID = cursor.getString(cursor.getColumnIndex("msgID"));
                try {
                    filePath = cursor.getString(cursor.getColumnIndex("path"));
                } catch (Exception e) {
                    filePath = "";
                }
                msgTime = cursor.getLong(cursor.getColumnIndex("msgTime"));
                senderID = cursor.getInt(cursor.getColumnIndex("senderID"));
                //senderName = cursor.getString(cursor.getColumnIndex("senderName"));
                targetID = cursor.getInt(cursor.getColumnIndex("targetID"));
                msgType = cursor.getInt(cursor.getColumnIndex("msgType"));
                msgContent = cursor.getString(cursor.getColumnIndex("msgContent"));
                clientType = cursor.getInt(cursor.getColumnIndex("clientType"));
                unreadFlag = cursor.getInt(cursor.getColumnIndex("unreadFlag"));

                //msgContent: [{"msgText":"gxxx"}] =>
                entity = MessageTextEntity.jsonToEntity(msgContent);
                entity.setMsgID(msgID);
                entity.setMsgTime(msgTime);
                entity.setMsgType(msgType);
                entity.setSenderID(senderID);
                entity.setTargetID(targetID);
                entity.setClientType(clientType);
                entity.setImgFile(filePath);

                pendingMsgList.add(entity);
            } while (cursor.moveToNext());
        }
        cursor.close();

        return pendingMsgList;
    }

    public synchronized List<MessageTextEntity> getChatMsgByTargetID(int targetID) {
        SQLiteDatabase db = getWritableDatabase();
        // 查询Book表中所有的数据
        List<MessageTextEntity> pendingMsgList = new ArrayList<>();
        Cursor cursor = null;
        try {
            cursor = db.rawQuery("SELECT msgID, msgTime, senderID, targetID, msgType, msgContent, clientType, path, unreadFlag from "
                    + CHAT_MSG_DB + " WHERE (" + "targetID=" + targetID
                    + ") ", null);
        } catch (SQLiteException e) {
            LoggerFile.LogError("db.rawQuery erorr, errorMsg: " + e.getMessage());
            return null;
        }

        if (cursor == null)
            return null;

        String msgID = "";
        long msgTime = 0;
        int msgType = 0;
        String senderName = "";
        String targetName = "";
        String msgContent = "";
        int clientType;
        int unreadFlag;
        int senderID;
        String filePath;
        MessageTextEntity entity = null;
        if (cursor.moveToFirst()) {
            do {
                // 遍历Cursor对象，取出数据并打印
                msgID = cursor.getString(cursor.getColumnIndex("msgID"));
                msgTime = cursor.getLong(cursor.getColumnIndex("msgTime"));
                senderID = cursor.getInt(cursor.getColumnIndex("senderID"));
                //senderName = cursor.getString(cursor.getColumnIndex("senderName"));
                targetID = cursor.getInt(cursor.getColumnIndex("targetID"));
                msgType = cursor.getInt(cursor.getColumnIndex("msgType"));
                msgContent = cursor.getString(cursor.getColumnIndex("msgContent"));
                clientType = cursor.getInt(cursor.getColumnIndex("clientType"));
                unreadFlag = cursor.getInt(cursor.getColumnIndex("unreadFlag"));
                try {
                    filePath = cursor.getString(cursor.getColumnIndex("path"));
                } catch (Exception e) {
                    filePath = "";
                }

                //msgContent: [{"msgText":"gxxx"}] =>
                entity = MessageTextEntity.jsonToEntity(msgContent);
                entity.setMsgID(msgID);
                entity.setMsgTime(msgTime);
                entity.setMsgType(msgType);
                entity.setSenderID(senderID);
                entity.setTargetID(targetID);
                entity.setClientType(clientType);
                entity.setImgFile(filePath);

                pendingMsgList.add(entity);
            } while (cursor.moveToNext());
        }
        cursor.close();

        return pendingMsgList;
    }

    //lastMsg格式：[{"faceID":7},{"faceID":7},{"msgText":"gg"}]
    public synchronized boolean updateChatSession(int sessionID, String lastMsg, String remark) {
        SQLiteDatabase db = getWritableDatabase();
        //先确定下sessionID是否存在，如果不存在则插入，存在则更新
        Cursor cursor = db.rawQuery("SELECT sessionID from " + CHAT_SESSION_DB +
                " WHERE sessionID=" + sessionID + " LIMIT 1", null);
        int existSessionID;
        boolean exsitSession = false;
        if (cursor.moveToFirst()) {
            exsitSession = true;
        }
        cursor.close();

        if (exsitSession) {
            try {
                db.execSQL("UPDATE " + CHAT_SESSION_DB + " SET lastMsg='" + lastMsg +
                        "', remark='" + remark + "' WHERE sessionID=" + sessionID);
            } catch (SQLException e) {
                e.printStackTrace();
                return false;
            }
        } else {
            db.beginTransaction(); // 开启事务
            try {
                ContentValues values = new ContentValues();
                values.put("sessionID", sessionID);
                values.put("lastMsg", lastMsg);
                values.put("remark", remark);
                db.insert(CHAT_SESSION_DB, null, values);
                db.setTransactionSuccessful(); // 事务已经执行成功
            } catch (Exception e) {
                e.printStackTrace();
                return false;
            } finally {
                db.endTransaction(); // 结束事务
            }
        }

        return true;
    }

    public synchronized boolean deleteChatSession(int sessionID) {
        SQLiteDatabase db = getWritableDatabase();
        try {
            db.execSQL("DELETE from " + CHAT_SESSION_DB + " WHERE sessionID=" + sessionID);
        } catch (SQLException e) {
            e.printStackTrace();
            return false;
        }

        return true;
    }

    public synchronized boolean deleteChatMsg(int ID) {
        SQLiteDatabase db = getWritableDatabase();
        try {
            db.execSQL("DELETE from " + CHAT_MSG_DB + " WHERE (senderID=" + ID + ") OR (targetID=" + ID + ")");
        } catch (SQLException e) {
            e.printStackTrace();
            return false;
        }

        return true;
    }

    public synchronized List<ChatSession> getChatSessions() {
        SQLiteDatabase db = getWritableDatabase();
        // 查询Book表中所有的数据
        List<ChatSession> sessionList = new ArrayList<>();
        Cursor cursor = db.rawQuery("SELECT sessionID, lastMsg, remark from " + CHAT_SESSION_DB + " LIMIT " + MAX_CHATSESSION_NUM, null);

        int sessionID;
        String senderName = "";
        String lastMsg = "";
        String remark = "";
        UserInfo userinfo = null;

        if (cursor.moveToFirst()) {
            do {
                // 遍历Cursor对象，取出数据并打印
                sessionID = cursor.getInt(cursor.getColumnIndex("sessionID"));
                lastMsg = cursor.getString(cursor.getColumnIndex("lastMsg"));
                remark = cursor.getString(cursor.getColumnIndex("remark"));

                userinfo = UserSession.getInstance().getUserInfoById(sessionID);
                if (userinfo == null)
                    continue;

                senderName = userinfo.get_nickname();

                ChatSession chatSession = new ChatSession(sessionID, senderName, lastMsg, remark);
                sessionList.add(chatSession);
            } while (cursor.moveToNext());
        }
        cursor.close();

        return sessionList;
    }

    public synchronized int getUnreadChatMsgCount() {
        return mUnreadChatMsgCount;
    }

    public synchronized void setUnreadChatMsgCount(int count) {
        mUnreadChatMsgCount = count;
    }

    @Override
    public void onCreate(SQLiteDatabase db) {
        db.execSQL(SQL_CREATE_CHATMSGDB);
        db.execSQL(SQL_CREATE_SESSIONDB);
    }

    @Override
    public void onUpgrade(SQLiteDatabase db, int oldVersion, int newVersion) {
    }
}
