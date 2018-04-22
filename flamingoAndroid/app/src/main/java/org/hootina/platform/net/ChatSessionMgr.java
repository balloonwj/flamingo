package org.hootina.platform.net;

import org.hootina.platform.activities.BaseActivity;
import org.hootina.platform.result.ChatSession;

import java.util.List;

/**
 * @desc    会话管理器
 * @author  zhangyl
 * @date    20180403
 */

public class ChatSessionMgr {
    private List<ChatSession> mSessions = null;
    private static ChatSessionMgr mMgr = new ChatSessionMgr();

    public static ChatSessionMgr getInstance(){
        return mMgr;
    }

    public List<ChatSession> getAllSessions(){
        if (mSessions == null)
            mSessions = BaseActivity.getChatMsgDb().getChatSessions();

        return mSessions;
    }

    public boolean isEmpty(){
        synchronized (this) {
            return mSessions.isEmpty();
        }
    }

    public int getSessionCount(){
        synchronized (this) {
            return mSessions.size();
        }
    }

    public void updateSession(int senderID, String senderName, String lastMsg, String remark){
        ChatSession tmp = new ChatSession(senderID, senderName, lastMsg, remark);

        synchronized (this){
            int count = mSessions.size();
            //ChatSession tmp = null;
            for (int i = 0; i < count; ++i){
                if (mSessions.get(i).getFriendID() == tmp.getFriendID()) {
                    //tmp = mSessions.get(i);
                    mSessions.remove(i);
                    break;
                }
            }

            //新消息会话移动到顶部
            mSessions.add(0, tmp);

            BaseActivity.getChatMsgDb().updateChatSession(tmp.getFriendID(), tmp.getLastMsg(), tmp.getRemark());
        }
    }

    public void updateSession(ChatSession s){
        synchronized (this){
            int count = mSessions.size();
            //ChatSession tmp = null;
            for (int i = 0; i < count; ++i){
                if (mSessions.get(i).getFriendID() == s.getFriendID()) {
                    //tmp = mSessions.get(i);
                    mSessions.remove(i);
                    break;
                }
            }

            //新消息会话移动到顶部
            mSessions.add(0, s);

            BaseActivity.getChatMsgDb().updateChatSession(s.getFriendID(), s.getLastMsg(), s.getRemark());
        }
    }

    public synchronized void deleteSession(int sessionID){
        synchronized (this){
            int count = mSessions.size();
            //ChatSession tmp = null;
            for (int i = 0; i < count; ++i){
                if (mSessions.get(i).getFriendID() == sessionID) {
                    //tmp = mSessions.get(i);
                    mSessions.remove(i);
                    break;
                }
            }

            BaseActivity.getChatMsgDb().deleteChatSession(sessionID);
        }
    }


    private ChatSessionMgr(){
        if (mSessions == null)
            mSessions = BaseActivity.getChatMsgDb().getChatSessions();
    }

}
