package org.hootina.platform.net;

import android.util.Log;

import org.hootina.platform.activities.BaseActivity;
import org.hootina.platform.db.ChatSessionDao;
import org.hootina.platform.db.MyDbUtil;
import org.hootina.platform.result.ChatSession;

import java.util.ArrayList;
import java.util.Date;
import java.util.List;

/**
 * @author zhangyl
 * @desc 会话管理器
 * @date 20180403
 */

public class ChatSessionMgr {

    private List<ChatSession> mSessions = new ArrayList<>();
    private static ChatSessionMgr mMgr = new ChatSessionMgr();

    public static ChatSessionMgr getInstance() {
        return mMgr;
    }

    public List<ChatSession> getAllSessions() {
        if (MyDbUtil.getChatSessionDao() != null) {
            mSessions = MyDbUtil.getChatSessionDao().loadAll();
        }

        return mSessions;
    }

    public boolean isEmpty() {
        synchronized (this) {
            return mSessions.isEmpty();
        }
    }

    public void clear() {
        if (mSessions != null) {
            mSessions.clear();
        }
    }

    public int getSessionCount() {
        synchronized (this) {
            return mSessions.size();
        }
    }

    public void updateSession(int targetID, String senderName, String lastMsg, String remark, Date date) {
        ChatSession tmp = new ChatSession(targetID, senderName, lastMsg, remark);
        tmp.setTime(date);

        boolean isNewSession = true;

        ChatSessionDao chatSessionDao = MyDbUtil.getChatSessionDao();
        if (chatSessionDao == null) {
            Log.e("ChatSessionMgr", "getChatSessionDao error");
            return;
        }

        mSessions = chatSessionDao.loadAll();

        //群消息
        for (ChatSession chatSession : mSessions) {
            if (chatSession.getFriendID() == targetID) {
                tmp.setId(chatSession.getId());
                isNewSession = false;
            }
        }

        if (isNewSession) {
            tmp.setId(targetID);

            if (MyDbUtil.getChatSessionDao() != null) {
                MyDbUtil.getChatSessionDao().insertOrReplace(tmp);
            }
        } else {
            if (MyDbUtil.getChatSessionDao() != null) {
                MyDbUtil.getChatSessionDao().update(tmp);
            }
        }


    }
    // ChatSession lastsession = null;
//        if (mSessions != null && mSessions.size() > 0) {
//            lastsession = mSessions.get(mSessions.size() - 1);
//        }


//        if (lastsession != null) {
//            boolean hasChatted = false;
//            long chatid = 0;
//            for (ChatSession item : mSessions) {
//                if (item.getFriendID() == senderID) {
//                    hasChatted = true;
//                    chatid = item.getId();
//                }
//            }
//            if (!hasChatted) {
//                tmp.setId(lastsession.getId() + 1);
//            } else {
//                tmp.setId(chatid);
//            }
//
//        } else {
//            tmp.setId(0);
//        }
//        synchronized (this) {
//            int count = mSessions.size();
//            //ChatSession tmp = null;
//            for (int i = 0; i < count; ++i) {
//                if (mSessions.get(i).getFriendID() == tmp.getFriendID()) {
//                    //tmp = mSessions.get(i);
//                    mSessions.remove(i);
//                    break;
//                }
//            }
//
//            //新消息会话移动到顶部
//            mSessions.add(0, tmp);

    //BaseActivity.getChatMsgDb().updateChatSession(tmp.getFriendID(), tmp.getLastMsg(), tmp.getRemark());
    //}
    //   }

    public void updateSession(ChatSession s) {
        synchronized (this) {
            int count = mSessions.size();
            //ChatSession tmp = null;
            for (int i = 0; i < count; ++i) {
                if (mSessions.get(i).getFriendID() == s.getFriendID()) {
                    //tmp = mSessions.get(i);
                    mSessions.remove(i);
                    break;
                }
            }

            //新消息会话移动到顶部
            mSessions.add(0, s);
            if (MyDbUtil.getChatSessionDao() != null) {
                MyDbUtil.getChatSessionDao().update(s);
            }

            //BaseActivity.getChatMsgDb().updateChatSession(s.getFriendID(), s.getLastMsg(), s.getRemark());
        }
    }

    public synchronized void deleteSession(int sessionID) {
        synchronized (this) {
            int count = mSessions.size();
            //ChatSession tmp = null;
            for (int i = 0; i < count; ++i) {
                if (mSessions.get(i).getFriendID() == sessionID) {
                    //tmp = mSessions.get(i);
                    mSessions.remove(i);
                    break;
                }
            }

            if (MyDbUtil.getChatSessionDao() != null) {
                MyDbUtil.getChatSessionDao().deleteByKey((long) sessionID);
            }

            //BaseActivity.getChatMsgDb().deleteChatSession(sessionID);
        }
    }


    private ChatSessionMgr() {
        if (mSessions == null && MyDbUtil.getChatSessionDao() != null) {
            mSessions = MyDbUtil.getChatSessionDao().loadAll();
        }
        //mSessions = BaseActivity.getChatMsgDb().getChatSessions();

    }

}
