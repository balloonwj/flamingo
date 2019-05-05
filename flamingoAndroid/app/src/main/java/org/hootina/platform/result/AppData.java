package org.hootina.platform.result;

import android.annotation.SuppressLint;
import android.os.Message;

import com.alibaba.fastjson.JSONObject;

import org.hootina.platform.userinfo.UserInfo;
import org.hootina.platform.userinfo.UserSession;
import com.lidroid.xutils.DbUtils;
import com.lidroid.xutils.db.sqlite.Selector;
import com.lidroid.xutils.db.sqlite.WhereBuilder;
import com.lidroid.xutils.exception.DbException;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

@SuppressLint("UseSparseArrays")
public class AppData {

	private static List<ChatSession> 			mChatSessions;
	private static Map<Integer, List<UserInfo>> mMapFriendList;
	private static Map<Integer, UserInfo> 		mMapTempFriend; // 未加载完成的临时对象,
																// 因为需要多个协议加载
	protected static DbUtils 					mDB;
	private static int 							mSelfID;

	public static void clears() {
		mChatSessions = null;
		mMapFriendList = null;
		mMapTempFriend = null;
		mDB = null;
		mSelfID = 0;
		Init(1, null);
	}

	public static void clearsession(){
		mChatSessions = null;
	}
	
	
	public static void clear() {
		mChatSessions = null;
		mMapFriendList = null;
	}

	public static void Init(int accountID, DbUtils db) {
		if (accountID == 0 || db == null)
			return;

		if (mSelfID == accountID && mDB == db)
			return;

		clear();

		mSelfID = accountID;
		mDB = db;
		mMapFriendList = new HashMap<Integer, List<UserInfo>>();
		mMapTempFriend = new HashMap<Integer, UserInfo>();
		mChatSessions = new ArrayList<ChatSession>();

		//getFriendList(accountID);
		loadChatSessions();
		//setSendingMsgFail();
	}

	public static boolean loadChatSessions() {
		if (mDB == null)
			return false;

		try {
			mChatSessions = mDB.findAll(Selector.from(
					ChatSession.class).where("uAccountID", "=", mSelfID));
		} catch (DbException e) {
			e.printStackTrace();
			return false;
		}

		if (mChatSessions == null)
			return false;

		int chatSessionNum = mChatSessions.size();
		ChatSession chatSession = null;
		int userid = 0;
		UserInfo userinfo = null;
		String nickname = "";
		for (int i = 0; i < chatSessionNum; ++i) {
			chatSession = mChatSessions.get(i);
			if (chatSession == null)
				continue;

			userid = chatSession.getFriendID();
			userinfo = findFriend(mSelfID, userid);
			if (userinfo != null) {
				nickname = userinfo.get_nickname();
				chatSession.setFriendNickName(nickname);
			}
		}

		return true;
	}

	public static List<ChatSession> getChatSessions() {
		return mChatSessions;
	}

	public synchronized static ChatSession updateChatSessionByFriendID(int friendID) {
		ChatSession newSession  = null;

		//未初始化
		if (mChatSessions == null) {
			mChatSessions = new ArrayList<ChatSession>();
			//newSession = new ChatSession();
			mChatSessions.add(newSession);
			return newSession;
		}

		//找到已经存在的，返回它
		for (int i = 0; i < mChatSessions.size(); ++i) {
			newSession = mChatSessions.get(i);
			if (newSession == null)
				continue;

//			if (newSession.getmFriendID() == friendID)
//			{
//				return newSession;
//			}
		}

		//找不到
//		newSession = new ChatSession();
//		mChatSessions.add(newSession);
		return newSession;
	}

	public static void setRead(int nAccountID, int nTargetID) {
		if (mChatSessions == null) {
			return;
		}

		for (int i = 0; i < mChatSessions.size(); ++i) {
			ChatSession session = mChatSessions.get(i);
			if (session != null) {
//				if (session.getmFriendID() == nTargetID
//						&& session.getmSelfID() == nAccountID) {
//					session.setNotRead(0);
//					try {
//						mDB.update(session);
//					} catch (DbException e) {
//
//						e.printStackTrace();
//					}
//					break;
//				}
			}
		}
	}

	public static int getUnReadCount() {
		if (mChatSessions == null) {
			return 0;
		}
		int count = 0;
		for (int i = 0; i < mChatSessions.size(); ++i) {
			//count += mChatSessions.get(i).getNotRead();
		}
		return count;
	}

	public static List<UserInfo> getFriendList(int nAccountID) {
//		if (mMapFriendList == null) {
//			return null;
//		}
//		List<FriendInfo> list = mMapFriendList.get(nAccountID);
//		if (list != null && list.size() != 0) {
//			return list;
//		}
//
//		// 从数据库中读取
//		try {
//			list = mDB.findAll(Selector.from(FriendInfo.class).where(
//					"uAccountID", "=", nAccountID));
//		} catch (DbException e) {
//
//			e.printStackTrace();
//		}
//		if (list != null) {
//			mMapFriendList.put(nAccountID, list);
//
//			// 读取群数据
//			for (int i = 0; i < list.size(); ++i) {
//				int nFriendID = list.get(i).getmFriendID();
//				if (isGroup(nFriendID)) {
//					getFriendList(nFriendID);
//				}
//			}
//		}
//
//		return list;
		return null;
	}

	public static List<FriendInfo> getFriend(int nAccountID) {
		// List<FriendInfo> list=new ArrayList<FriendInfo>();
//		List<FriendInfo> list = new ArrayList<FriendInfo>();
//		list = getFriendList(nAccountID);
//		//去除无效数据和群组数据之后的干净数据
//		List<FriendInfo> listDest = new ArrayList<FriendInfo>();
//		// 读取群数据
//		int nFriendID;
//		String strAccountNO;
//		if (list != null) {
//			for (int i = 0; i < list.size(); ++i) {
//				nFriendID = list.get(i).getmFriendID();
//				strAccountNO = list.get(i).getStrAccountNo();
//				if (strAccountNO.isEmpty())
//					continue;
//				if (isGroup(nFriendID))
//					continue;
//
//				listDest.add(list.get(i));
//			}
//		}
/*		if (list != null) {	
			for (int i = 0; i < list.size(); ) {
				int nFriendID = list.get(i).getmFriendID();
				String strAccountNO = list.get(i).getStrAccountNo();
				if (strAccountNO.IsEmpty())
				if (isGroup(nFriendID)) {
					list.remove(i);

				} else {
					++i;
				}
			}
		}*/
		
		//Collections.sort(listDest);
		//return listDest;
		return null;
	}

	public static List<UserInfo> getGroupList() {
		//TODO: 先从UserSession对象中取，没有则从本地数据库中取
		//List<FriendInfo> list = getFriendList(nAccountID);
//		// 从数据库中读取
//		try {
//			list = mDB.findAll(Selector.from(FriendInfo.class).where(
//					"uAccountID", "=", nAccountID));
//		} catch (DbException e) {
//
//			e.printStackTrace();
//		}
//		// 读取群数据
//		if (list != null) {
//
//			for (int i = 0; i < list.size();) {
//				int nFriendID = list.get(i).getmFriendID();
//				if (isGroup(nFriendID)) {
//					++i;
//				} else {
//					list.remove(i);
//				}
//			}
//		}
		List<UserInfo> localList = new ArrayList<UserInfo>();
		List<UserInfo> list = UserSession.getInstance().friends;
		for (UserInfo u : list)
		{
			if (UserInfo.isGroup(u.get_userid()))
				localList.add(u);
		}
		Collections.sort(localList);
		return localList;
	}

	public static void reloadFriendList(int nAccountID) {
		if (mMapFriendList == null) {
			return;
		}
		mMapFriendList.remove(nAccountID);
		getFriendList(nAccountID);
	}

	//因为本地可能存储了多个登录用户的账号信息，所以nAccount是登录用户的账户id，nFriendID是查找的好友或者群id
	public static UserInfo findFriend(int nAccountID, int nFriendID) {
		if (nAccountID <= 0 || nFriendID <= 0)
			return null;

		UserInfo userInfo = UserSession.getInstance().getUserInfoById(nFriendID);
		return userInfo;

//		if (mMapFriendList == null) {
//			return null;
//		}
//		List<UserInfo> list = mMapFriendList.get(nAccountID);
//		if (list == null) {
//			return null;
//		}
//
//		for (int i = 0; i < list.size(); ++i) {
//			if (list.get(i).get_userid() == nFriendID) {
//				return list.get(i);
//			}
//		}
		//return null;
	}

	public static void deleteFriend(int nAccountID, int nFriendID) {
		if (mMapFriendList == null) {
			return;
		}
		List<UserInfo> list = mMapFriendList.get(nAccountID);
		if (list == null) {
			return;
		}

		for (int i = 0; i < list.size(); ++i) {
			if (list.get(i).get_userid() == nFriendID) {
				if (mDB != null) {
					try {
						mDB.delete(list.get(i));
					} catch (DbException e) {

						e.printStackTrace();
					}
				}
				list.remove(i);
				return;
			}
		}
	}

	public static void updateFriendInfo(UserInfo info) {
		boolean bFound = false;

		if (mMapFriendList == null) {
			return;
		}

		UserInfo temp = mMapTempFriend.get(info.get_userid());
//		if (temp != null) {
//			if (info.getnFace() == 0) {
//				info.setnFace(temp.getnFace());
//			}
//
//			if (info.getStrAccountNo() == null
//					|| info.getStrAccountNo().equals("")) {
//				info.setStrAccountNo(temp.getStrAccountNo());
//			}
//
//			if (info.getmNickName() == null
//					|| info.getmNickName().equals("")) {
//				info.setmNickName(temp.getmNickName());
//			}
//		}

		List<UserInfo> list = mMapFriendList.get(info.get_userid());
		if (list == null) {
			list = new ArrayList<UserInfo>();
			list.add(info);
			mMapFriendList.put(info.get_userid(), list);
		} else {
			for (int i = 0; i < list.size(); ++i) {
				UserInfo friend = list.get(i);
				if (friend != null
						&& friend.get_userid() == info.get_userid()) {
					bFound = true;
					list.set(i, info);
				}
			}
			if (bFound == false) {
				list.add(info);
			}
			mMapFriendList.put(info.get_userid(), list);
		}

		try {
			if (bFound == false) {
				if (mDB != null) {
					mDB.save(info);
				}
			} else {
				if (mDB != null) {
					mDB.update(info);
				}
			}
		} catch (DbException e) {

			e.printStackTrace();
		}

	}

	public static void updateFriendInfo(int nTargetID, int nFace,
			String strName, String strAccountID) {
		if (mMapFriendList == null) {
			return;
		}
		UserInfo info = new UserInfo();
		info.set_userid(nTargetID);
		info.set_faceType(nFace);
		info.set_nickname(strName);
		info.set_username(strAccountID);
		mMapTempFriend.put(nTargetID, info);

		for (Map.Entry<Integer, List<UserInfo>> entry : mMapFriendList
				.entrySet()) {
			List<UserInfo> list = entry.getValue();
			int nAccountID = entry.getKey();

			for (int i = 0; i < list.size(); ++i) {
				UserInfo friend = list.get(i);
				if (friend == null) {
					continue;
				}
				if (friend.get_userid() == nTargetID) {
					if (friend.get_faceType() != nFace
							|| (friend.get_nickname() == null || (strName != null && !friend
									.get_nickname().equals(strName)))
							|| (friend.get_username() == null && (strAccountID != null && !friend
									.get_username().equals(strAccountID)))) {
						friend.set_faceType(nFace);
						friend.set_nickname(strName);
						friend.set_username(strAccountID);

						try {
							if (mDB != null) {
								mDB.update(friend);
							}
						} catch (DbException e) {

							e.printStackTrace();
						}
					}

					list.set(i, friend);
					mMapFriendList.put(nAccountID, list);
					break;
				}
			}
		}
	}

	public static int getFriendApplyWaiting(int nAccountID) {
//		try {
//			List<NewFriendEntity> list = BaseActivity.getDb().findAll(
//					Selector.from(NewFriendEntity.class)
//							.where("uAccountID", "=", nAccountID)
//							.and(WhereBuilder.b("ncmd", "=", "Apply")));
//			if (list != null) {
//				return list.size();
//			}
//		} catch (DbException e) {
//			// TODO Auto-generated catch block
//			e.printStackTrace();
//		}
		return 0;
	}

	// 消息
	public static void onRecvNewChatMsg(Message msg) {
		MessageTextEntity messageTextEntity = JSONObject.parseObject((String)msg.obj, MessageTextEntity.class);
		int msgType = messageTextEntity.getMsgType();
		// 只处理1， 2， 5
		if (msgType != MessageTextEntity.CONTENT_TYPE_TEXT
				&& msgType != MessageTextEntity.CONTENT_TYPE_IMAGE_CONFIRM
				&& msgType != MessageTextEntity.CONTENT_TYPE_MOBILE_IMAGE) {
			return;
		}

		int senderId = msg.arg1;
		int targetId = msg.arg2;
		ChatMessage messages = new ChatMessage();
		messages.setSenderID(senderId);
		messages.setTargetID(targetId);
		messages.setMsgID("");
		messages.setMsgType(msgType);
		messages.setMsgState(ChatMessage.MSG_STATE_UNREAD);

//		if (isGroup(targetId)) {
//			messages.setTargetID(targetId);
//		} else if (senderId == mSelfID) // 本人 pc 发送
//		{
//			messages.setTargetID(targetId);
//		}
//		messages.setMsgSenderClientType(messageTextEntity.getmClientType());
//		messages.setMsgTime(messageTextEntity.getmMsgTime());
//		messages.setmContent(messageTextEntity.getmContent());
//		int size = messageTextEntity.getmContent().size();
//		for (int i = 0; i < size; i++) {
//			try {
//				//INSERT INTO org_hootina_platform_result_ChatMessage (uMsgID,time,textid,
//				// mTargetID,state,msgType,msgID,mSenderID,clientType) VALUES (?,?,?,?,?,?,?,?,?)
//				BaseActivity.getDb().save(messages);
//
//			} catch (DbException e) {
//				e.printStackTrace();
//				return;
//			}
//		}


//		String msgText = null;
//		String faceID = null;
//		Object pictrue = null;
//
//
//		int type = 0;
//		if (msgType == ChatMessage.CONTENT_TYPE_TEXT) {
//			for (int i = 0; i < size; i++) {
//				faceID = messageTextEntity.getmContent().get(i).getFaceID();
//				msgText = messageTextEntity.getmContent().get(i).getMsgText();
//
//				if (faceID != null || msgText != null) {
//					type = 1;
//
//					try {
//						//INSERT INTO org_hootina_platform_result_ChatMessage (uMsgID,time,textid,
//						// mTargetID,state,msgType,msgID,mSenderID,clientType) VALUES (?,?,?,?,?,?,?,?,?)
//						BaseActivity.getDb().save(messages);
//
//					} catch (DbException e) {
//						e.printStackTrace();
//					}
//				}
//
//			}
//		} else if (messageTextEntity.getmMsgType() == ChatMessage.CONTENT_TYPE_IMAGE_CONFIRM
//				|| messageTextEntity.getmMsgType() == ChatMessage.CONTENT_TYPE_MOBILE_IMAGE) {
//			try {
//				//INSERT INTO org_hootina_platform_result_ChatMessage
//				// (uMsgID,time,textid,mTargetID,state,msgType,msgID,mSenderID,clientType)
//				// VALUES (?,?,?,?,?,?,?,?,?)
//				BaseActivity.getDb().save(messages);
//
//			} catch (DbException e) {
//				e.printStackTrace();
//			}
//		}

		if (mChatSessions == null)
			mChatSessions = new ArrayList<>();


		UserInfo userinfo = findFriend(mSelfID, senderId);
		if (userinfo == null)
			return;

		ChatSession chatSession = null;
//		for (int i = 0; i < mChatSessions.size(); ++i) {
//			if (mChatSessions.get(i).getmFriendID() == senderId) {
//				chatSession = mChatSessions.get(i);
//				break;
//			}
//		}

		if (chatSession == null)
		{
			//chatSession = new ChatSession();
			//mChatSessions.add(0, chatSession);
		}

//		chatSession.addNotRead();
//		if (!isGroup(senderId))
//		{
//			chatSession.setmNickName(userinfo.get_nickname());// haoyou姓名
//			chatSession.setmLastMsgText(messages.getmMsgText());
//			chatSession.setmFriendID(senderId);
//			chatSession.setmSelfID(mSelfID);
//		}

		try {
			mDB.update(chatSession);
		}catch (DbException e)
		{

		}



//		int nPreSessionCount = mChatSessions.size();
//		ChatSession friendSessionList = new ChatSession();
//
//		try {
//			// 群
//			if (isGroup(targetId)) {
//				// friend = findFriend(tTalkMsg.getUTargetID(),
//				// tTalkMsg.getUSendID());
//				UserInfo group = UserSession.getInstance().getUserInfoById(targetId);
//				String strGroupName = null;
//				String strMemberName = null;
//				if (group != null && group.get_nickname() != null && !group.get_nickname().equals("")) {
//					strGroupName = group.get_nickname();
//				}
//				if (friend != null && friend.get_nickname() != null
//						&& !friend.get_nickname().equals("")) {
//					strMemberName = friend.get_nickname();
//				}
//
//				String strText = messages.getmMsgText();
//				if (strMemberName != null) {
//					strText = strMemberName + " : |" + strText;
//				}
//
//				ChatSession chatSession = null;
//				for (int i = 0; i < mChatSessions.size(); ++i) {
//					if (mChatSessions.get(i).getmFriendID() == targetId) {
//						chatSession = mChatSessions.get(i);
//						chatSession.setmLastMsgText(strText);
//
//						if (friend != null && strGroupName != null) {
//							chatSession.setmNickName(strGroupName);
//						}
//
//						if (messageTextEntity.getmMsgType() == 2
//								|| messageTextEntity.getmMsgType() == 5) {
//							if (senderId == mSelfID) {
//								chatSession.setNotRead(0);
//							} else {
//								chatSession.addNotRead();
//							}
//
//							mDB.update(chatSession);
//							mChatSessions.set(i, chatSession);
//						} else if (messageTextEntity.getmMsgType() == 1) {
//							if (msgText != null || faceID != null) {
//								if (senderId == mSelfID) {
//									chatSession.setNotRead(0);
//								} else {
//									chatSession.addNotRead();
//								}
//								//UPDATE org_hootina_platform_result_FriendSessionList SET textid=?,
//								// uAccountID=?,uTargetID=?,nNotReadCount=?,strNickName=?
//								// WHERE id = 1
//								mDB.update(chatSession);
//								mChatSessions.set(i, chatSession);
//							}
//						}
//
//						break;
//					}
//				}
//				if (chatSession != null) {
//
//				} else {
//					// String textid = messages.getmMsgText();
//					if (friend != null) {
//						friendSessionList.setmNickName(strGroupName);// haoyou姓名
//					}
//					friendSessionList.setmLastMsgText(strText);
//					friendSessionList.setmFriendID(targetId);
//					friendSessionList.setmSelfID(mSelfID);
//
//					if (messageTextEntity.getmMsgType() == 2
//							|| messageTextEntity.getmMsgType() == 5) {
//						if (senderId == mSelfID) {
//							friendSessionList.setNotRead(0);
//						} else {
//							friendSessionList.addNotRead();
//						}
//
//						mChatSessions.add(friendSessionList);
//						mDB.save(friendSessionList);
//					} else if (messageTextEntity.getmMsgType() == 1) {
//						if (msgText != null || faceID != null) {
//							if (senderId == mSelfID) {
//								friendSessionList.setNotRead(0);
//							} else {
//								friendSessionList.addNotRead();
//							}
//
//							mChatSessions.add(friendSessionList);
//							mDB.save(friendSessionList);
//						}
//					}
//
//				}
//			} else {
//				if (senderId != mSelfID
//						&& targetId != mSelfID) {
//					Log.i("chat error ",
//							"not my message, send : " + senderId
//									+ ", target : " + targetId
//									+ ", my id : " + mSelfID);
//					return;
//				}
//				// 判断是否是本人发送
//				int nSessionAccountID = mSelfID;
//				int nSessionTargetID = senderId;
//				if (senderId == mSelfID) {
//					friend = findFriend(mSelfID, targetId);
//					nSessionTargetID = targetId;
//				}
//
//				// 普通好友
//				// friend = findFriend(tTalkMsg.getUTargetID(),
//				// tTalkMsg.getUSendID());
//				ChatSession chatSession = null;
//				for (int i = 0; i < mChatSessions.size(); ++i) {
//					if (mChatSessions.get(i).getmFriendID() == nSessionTargetID
//							&& mChatSessions.get(i).getmSelfID() == nSessionAccountID) {
//						chatSession = mChatSessions.get(i);
//						chatSession.setmLastMsgText(messages.getmMsgText());
//
//						if (friend != null && friend.get_nickname() != null
//								&& !friend.get_nickname().equals("")) {
//							chatSession.setmNickName(friend.get_nickname());
//						}
//
//						if (messageTextEntity.getmMsgType() == 2
//								|| messageTextEntity.getmMsgType() == 5) {
//							if (senderId == mSelfID) {
//								chatSession.setNotRead(0);
//							} else {
//								chatSession.addNotRead();
//							}
//
//							mDB.update(chatSession);
//							mChatSessions.set(i, chatSession);
//						} else if (messageTextEntity.getmMsgType() == 1) {
//							if (msgText != null || faceID != null) {
//								if (senderId == mSelfID) {
//									chatSession.setNotRead(0);
//								} else {
//									chatSession.addNotRead();
//								}
//
//								//UPDATE org_hootina_platform_result_FriendSessionList
//								// SET textid=?,uAccountID=?,uTargetID=?,nNotReadCount=?,strNickName=?
//								// WHERE id = 1
//								mDB.update(chatSession);
//								mChatSessions.set(i, chatSession);
//							}
//						}
//
//						break;
//					}
//				}
//				if (chatSession != null) {
//
//				} else {
//					String textid = messages.getmMsgText();
//					if (friend != null) {
//						friendSessionList.setmNickName(friend.get_nickname());// haoyou姓名
//					}
//					friendSessionList.setmLastMsgText(textid);
//					friendSessionList.setmFriendID(nSessionTargetID);// haoyou
//					friendSessionList.setmSelfID(nSessionAccountID);
//
//					if (messageTextEntity.getmMsgType() == 2
//							|| messageTextEntity.getmMsgType() == 5) {
//						if (senderId == mSelfID) {
//							friendSessionList.setNotRead(0);
//						} else {
//							friendSessionList.addNotRead();
//						}
//						mChatSessions.add(friendSessionList);
//						mDB.save(friendSessionList);
//					} else if (messageTextEntity.getmMsgType() == 1) {
//						if (msgText != null || faceID != null) {
//							if (senderId == mSelfID) {
//								friendSessionList.setNotRead(0);
//							} else {
//								friendSessionList.addNotRead();
//							}
//							mChatSessions.add(friendSessionList);
//							mDB.save(friendSessionList);
//						}
//					}
//				}
//
//			}
//
//			// 对于第一个消息， 需要重新读库， id不同
//			if (nPreSessionCount == 0 && mChatSessions.size() == 1) {
//				mChatSessions.remove(0);
//				loadChatSessions();
//			}
//		} catch (DbException e) {
//			e.printStackTrace();
//		}
	}

	public static void setSendingMsgFail() {
		try {
			List<ChatMessage> list = mDB.findAll(Selector
					.from(ChatMessage.class).where("state", "=", 0)
					.and(WhereBuilder.b("mSenderID", "=", mSelfID)));

			if (list == null) {
				return;
			}

			for (int i = 0; i < list.size(); ++i) {
				ChatMessage msg = list.get(i);
				if (msg != null) {
					msg.setMsgState(2);
					mDB.update(msg);
				}
			}
		} catch (DbException e) {
			e.printStackTrace();
		}

	}
}
