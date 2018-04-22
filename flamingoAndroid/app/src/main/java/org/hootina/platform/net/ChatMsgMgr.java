package org.hootina.platform.net;

import org.hootina.platform.result.MessageTextEntity;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * @desc   内存中管理所有聊天消息的类
 * @author zhangyl
 * @date:  2018.04.01
 */

public final class ChatMsgMgr {
    //key是friendID，value是消息实体集合
    private HashMap<Integer, List<MessageTextEntity>> mMessages = new HashMap<>();

    //List<MessageTextEntity> mMessages = new ArrayList<>();

    private static ChatMsgMgr mInstance = new ChatMsgMgr();
    //未读聊天消息个数，key：senderID， value：数目
    private HashMap<Integer, Integer>   mUnreadChatMsgCounts = new HashMap<>();


    public static ChatMsgMgr getInstance(){
        return mInstance;
    }

    public void addMessageTextEntity(int friendID, MessageTextEntity t){
        Integer wrapperFriendID = friendID;
        List<MessageTextEntity> arr;
        if (!mMessages.containsKey(wrapperFriendID))
        {
            arr = new ArrayList<>();
            mMessages.put(wrapperFriendID, arr);
        }
        else
            arr = mMessages.get(wrapperFriendID);

        arr.add(t);
    }

    public void removeMessageTextEntity(MessageTextEntity t){
//        int count = mMessages.size();
//        for(int i = 0; i < count; ++i){
//            if (mMessages.get(i).getmMsgID() == t.getmMsgID()){
//                mMessages.remove(i);
//                break;
//            }
//        }
    }

    public void increaseUnreadChatMsgCount(int senderID){
        synchronized (this) {
            Integer keyWrapper = Integer.valueOf(senderID);
            Integer valueWrapper = mUnreadChatMsgCounts.get(keyWrapper);
            int old = 0;
            if (valueWrapper != null) {
                old  = valueWrapper.intValue();
                old++;
                valueWrapper = old;
            } else {
                old = 1;
            }

            mUnreadChatMsgCounts.put(keyWrapper, Integer.valueOf(old));
        }
    }

    public void decreaseUnreadChatMsgCount(int senderID){
        synchronized (this) {
            Integer keyWrapper = Integer.valueOf(senderID);
            Integer valueWrapper = mUnreadChatMsgCounts.get(keyWrapper);
            if (valueWrapper != null) {
                int old = valueWrapper.intValue();
                old--;
                if (old <= 0)
                    old = 0;
                valueWrapper = old;
            }
        }
    }

    public int getTotalUnreadChatMsgCount(){
        int total = 0;
        synchronized (this){
            Set<Map.Entry<Integer, Integer>> sets = mUnreadChatMsgCounts.entrySet();
            for (Map.Entry<Integer, Integer> entry : sets){
                total += entry.getValue().intValue();
            }

            return total;
        }

    }

    public void clearTotalUnreadChatMsgCount(){
        synchronized (this){
            Set<Map.Entry<Integer, Integer>> sets = mUnreadChatMsgCounts.entrySet();
            for (Map.Entry<Integer, Integer> entry : sets){
                mUnreadChatMsgCounts.put(entry.getKey(), Integer.valueOf(0));
            }
        }
    }

    public int getUnreadChatMsgCountBySenderID(int senderID){
        synchronized (this){
            Set<Map.Entry<Integer, Integer>> sets = mUnreadChatMsgCounts.entrySet();
            for (Map.Entry<Integer, Integer> entry : sets){
                if (entry.getKey().intValue() == senderID){
                    return entry.getValue().intValue();
                }
            }
        }

        return 0;
    }

    public void clearUnreadChatMsgCountBySenderID(int senderID){
        synchronized (this){
            Set<Map.Entry<Integer, Integer>> sets = mUnreadChatMsgCounts.entrySet();
            for (Map.Entry<Integer, Integer> entry : sets){
                if (entry.getKey().intValue() == senderID){
                    mUnreadChatMsgCounts.put(entry.getKey(), Integer.valueOf(0));
                    break;
                }
            }
        }
    }



    private ChatMsgMgr(){

    }

}
