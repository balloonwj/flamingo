package org.hootina.platform.activities;

import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Message;
import android.support.v4.app.FragmentActivity;
import android.view.View.OnClickListener;
import android.view.ViewGroup;
import android.view.Window;
import android.widget.ImageView;
import android.widget.TextView;
import android.widget.Toast;

import org.hootina.platform.FlamingoApplication;
import org.hootina.platform.R;
import org.hootina.platform.db.ChatMsgDb;
import org.hootina.platform.enums.MsgType;
import org.hootina.platform.net.NetWorker;
import org.hootina.platform.result.FriendInfo;
import org.hootina.platform.result.MemberEntity;
import org.hootina.platform.result.NewFriendEntity;
import org.hootina.platform.result.AppData;
import org.hootina.platform.userinfo.UserInfo;
import org.hootina.platform.utils.LoggerFile;
import org.hootina.platform.util.MegAsnType;

import com.lidroid.xutils.DbUtils;
import com.lidroid.xutils.db.sqlite.Selector;
import com.lidroid.xutils.db.sqlite.WhereBuilder;
import com.lidroid.xutils.exception.DbException;
import com.nostra13.universalimageloader.core.DisplayImageOptions;
import com.nostra13.universalimageloader.core.ImageLoader;

import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;

//import org.json.JSONObject;

/**
 * 所有activity类的父类
 */
public abstract class BaseActivity extends FragmentActivity implements OnClickListener {
    //注册Activity返回的结果
	public static final int REGISTER_RESULT		   = 0;
	public static final int REGISTER_RESULT_CANCEL = 1;
	public static final int REGISTER_RESULT_OK 	   = 2;

    protected static FlamingoApplication 	application;
	private   static ChatMsgDb				msgDb;

	public static boolean 					bLogin = false;

	@Override
	protected void onCreate(Bundle bundle) {
		super.onCreate(bundle);

		// 设置隐藏标题栏
		requestWindowFeature(Window.FEATURE_NO_TITLE);

		// 初始化application
		application = (FlamingoApplication) getApplication();
		// 添加Activity到堆栈
		application.getAppManager().addActivity(this);

		msgDb = application.getChatMsgDb();
		//application.getMemberEntity();

		setContentView(getContentView());
		autoInitAllWidgets();

		initData();
		setData();
	}
	
	@Override
	protected void onRestoreInstanceState(Bundle savedInstanceState) {
		super.onRestoreInstanceState(savedInstanceState);
		application = (FlamingoApplication) getApplication();
		// 添加Activity到堆栈
		application.getAppManager().addActivity(this);
		application.getMemberEntity();
	}

	public static ChatMsgDb getChatMsgDb(){
        return msgDb;
    }

	@Override
	protected void onDestroy() {
		super.onDestroy();
		// 结束Activity&从堆栈中移除
		application.getAppManager().finishActivity(this);
	}

	public void processMessage(Message msg) {
	}

	public static void sendMessage(int cmd, String text) {
		Message msg = new Message();
		msg.what = cmd;
		msg.obj = text;
		sendMessage(msg);
	}

	public static void sendMessage(Message msg) {
		handler.sendMessage(msg);
		if (msg.what == MegAsnType.FromTalkingmsg) {
		}

	}

	public static void sendEmptyMessage(int what) {
		handler.sendEmptyMessage(what);
	}

	// Handler对象是静态的，则所有继承BaseActivity的子类都是共用同一个消息队列
	private static Handler handler = new Handler() {

		@Override
		public void handleMessage(Message msg) {
			//ProcessNetMsg(msg);

			BaseActivity acitivity = ((BaseActivity) application.getAppManager().currentActivity());
			if (acitivity != null) {
				if (msg.what == MsgType.msg_networker_disconnect)
					Toast.makeText(acitivity, "网络已经断开", Toast.LENGTH_SHORT).show();
				else
					acitivity.processMessage(msg);
			}
		}
	};

	/**
	 * 获取布局文件ID
	 *
	 * @return
	 */
	protected abstract int getContentView();

	/**
	 * 初始化控件
	 */
	// protected abstract void initView();

	/**
	 * 初始化数据
	 */
	protected abstract void initData();

	/**
	 * 设置
	 */
	protected abstract void setData();

	/**
	 * 调用findViewById,避免强制类型转换
	 *
	 * @param id
	 * @return
	 */
	// protected <T extends View> T getView(int id) {
	// return (T) findViewById(id);
	// }

	/**
	 * 自动初始化所有控件
	 */
	private void autoInitAllWidgets() {
		// 得到该Activity的所有字段
		Field[] fields = this.getClass().getDeclaredFields();

		for (Field field : fields) {

			// 反射访问私有成员，必须加上这句
			field.setAccessible(true);

			try {
				int id = R.id.class.getField(field.getName())
						.getInt(new R.id());
				// 然后对这个属性赋值
				field.set(this, this.findViewById(id));

			} catch (Exception e) {
			}

		}
	}

	// ----------------------------------------------
	// --------------------跳转函数--------------------------
	// ----------------------------------------------

	protected void startActivity(Class<?> cls) {
		Intent intent = new Intent(this, cls);
		startActivity(intent);
	}

	// ----------------------------------------------
	// --------------------基本控件设置--------------------------
	// ----------------------------------------------

	/**
	 * 设置文字
	 *
	 * @param tv
	 *            TextView
	 * @param str
	 *            要设置的字符串
	 */
	protected void setText(TextView tv, String str) {
		if (str == null || tv == null) {
			return;
		}
		tv.setText(str);
	}

	/**
	 * 显示网络图片, 可重新配置options
	 */
	protected void displayImage(ImageView iv, String imageUrl,
			float screenWidthScale, float heightWidthScale,
			DisplayImageOptions options) {
		if (imageUrl == null) {
			return;
		}

		ViewGroup.LayoutParams params = iv.getLayoutParams();
		params.width = (int) (screenWidthScale * application.getScreenWidth());
		params.height = (int) (params.width * heightWidthScale);
		iv.setLayoutParams(params);

		ImageLoader.getInstance().displayImage(imageUrl, iv, options);
	}

	public static int getMyAccountID() {
		if (application != null) {
			return application.getMemberEntity().getuAccountID();
		} else {
			return 0;
		}
	}

	public static MemberEntity getMyMemberEntity() {
		if (application != null) {
			return application.getMemberEntity();
		} else {
			return null;
		}
	}

	public static boolean isGroup(int nID) {
		int nMask = nID & 0xE0000000;
		if (nMask == 0xC0000000) {
			return true;
		}
		return false;
	}

	public static void ProcessNetMsg(Message msg) {
		//if (application.getMemberEntity() == null) {
		//	return;
		//}

		int uAccountID = application.getMemberEntity().getuAccountID();
		if (msg.what == MegAsnType.Login) {
			int Version = 0;
			int uTargetID = application.getMemberEntity().getuAccountID();

			// 初始化数据
			// 加载好友
			//con.getFriends(1);
		} else if (msg.what == MegAsnType.ReLogin) {
			String mobilenumber = application.getMemberEntity()
					.getStrAccountNo();
			String password = application.getMemberEntity().getPassword();

			if (mobilenumber != null && !mobilenumber.equals("")
					&& password != null && !password.equals("")) {
//				con.login(mobilenumber, password,
//						tms.User.LoginInfo.type.n_AccountNO);
			}
		} else if (msg.what == MegAsnType.UserInfoReq) {
			tms.User.SysMemberInfoList sysMemberList = (tms.User.SysMemberInfoList) msg.obj;
			List<Integer> friendlist = new ArrayList<Integer>();
			for (int i = 0; i < sysMemberList.getNodesCount(); ++i) {
				int uFriendID = sysMemberList.getNodes(i).getUTargetID();
				friendlist.add(uFriendID);

				if (isGroup(uFriendID)) {
					//con.getGroup(uFriendID);
				}
			}

			if (friendlist.size() > 0) {
				//con.getUserInfo(friendlist);
			}

			int nAccount = sysMemberList.getUTargetID();
			if (sysMemberList.getNodesCount() > 0) {
				for (int i = 0; i < sysMemberList.getNodesCount(); i++) {
					tms.User.SysMemberInfo tTargets = sysMemberList.getNodes(i);
					int uTargetID = tTargets.getUTargetID();
					UserInfo friendInfo = new UserInfo();
					//friendInfo.setmFriendID(tTargets.getUTargetID());
					//friendInfo.setmSelfID(nAccount);

					AppData.updateFriendInfo(friendInfo);

				}
			}

//			// pc端可能已经删除好友, 更新本地数据库
//			List<FriendInfo> localFriends = AppData.getFriendList(nAccount);
//			if (localFriends != null && sysMemberList.getNodesCount() > 0) {
//				for (int i = 0; i < localFriends.size(); ++i) {
//					FriendInfo friend = localFriends.get(i);
//					if (friend == null) {
//						continue;
//					}
//					boolean bFound = false;
//					for (int k = 0; k < sysMemberList.getNodesCount(); ++k) {
//						tms.User.SysMemberInfo tTargets = sysMemberList
//								.getNodes(k);
//						if (friend.getmFriendID() == tTargets.getUTargetID()) {
//							bFound = true;
//						}
//					}
//					if (bFound == false) {
//						AppData.deleteFriend(nAccount,
//								friend.getmFriendID());
//					}
//				}
//			}
//
//			AppData.reloadFriendList(nAccount);

		} else if (msg.what == MegAsnType.UserPrivateInfoList) {

//			SysUserPrivateInfoList sSysUserPrivateInfolist = (SysUserPrivateInfoList) msg.obj;
//			String sign = sSysUserPrivateInfolist.getNodes(0).getObjs()
//					.getStrSigature().toStringUtf8();
//			String picname = sSysUserPrivateInfolist.getNodes(0).getObjs()
//					.getStrCustomFace().toStringUtf8();
//			int uTargetID = sSysUserPrivateInfolist.getNodes(0).getUTargetID();
//			try {
//				List<FriendInfo> list = BaseActivity.getDb().findAll(
//						Selector.from(FriendInfo.class)
//								.where("uAccountID", "=", uAccountID)
//								.and(WhereBuilder
//										.b("uTargetID", "=", uTargetID)));
//				if (list != null && list.size() > 0) {
//					UserInfo friendInfo = list.get(0);
//					if (picname.equals("")) {
//
//					} else {
//						if (friendInfo.getHeadpath() != picname) {
//							Bitmap bm = PictureUtil.decodeHeadFromFile(picname);
//
//						}
//					}
//
//					friendInfo.setHeadpath(picname);
//					friendInfo.setSign(sign);
//
//					AppData.updateFriendInfo(friendInfo);
//				}
//
//			} catch (DbException e) {
//				e.printStackTrace();
//			}

		} else if (msg.what == MegAsnType.FriendChange) {
			if (msg.obj == null) {
				return;
			}
			tms.User.TargetsAdd tTargetsAdd = (tms.User.TargetsAdd) msg.obj;

			OnFriendChange(tTargetsAdd);

		} else if (msg.what == MegAsnType.TargetInfo) {
			tms.User.SysTargetInfoList info = (tms.User.SysTargetInfoList) msg.obj;

			tms.User.SysUserPrivateInfoList.Builder sysUserPrivateInfolist = tms.User.SysUserPrivateInfoList
					.newBuilder();
			for (int i = 0; i < info.getNodesCount(); ++i) {
				tms.User.SysUserPrivateInfo.Builder sysUserPrivateInfo = tms.User.SysUserPrivateInfo
						.newBuilder();
				sysUserPrivateInfo.setUVersion(0);
				sysUserPrivateInfo
						.setUTargetID(info.getNodes(i).getUTargetID());
				sysUserPrivateInfolist.addNodes(sysUserPrivateInfo);
			}
//			con.sendProto(tms.Base.cmd.n_SysUserPrivateInfoList_VALUE,
//					sysUserPrivateInfolist.build());

			for (int k = 0; k < info.getNodesCount(); ++k) {
				tms.User.SysTargetInfo target = info.getNodes(k);

				int nFace = target.getObjs().getNFace();
				String strName = target.getObjs().getStrNickName()
						.toStringUtf8();
				String strAccountNo = target.getObjs().getStrAccountNo();

				AppData.updateFriendInfo(target.getUTargetID(), nFace,
						strName, strAccountNo);
			}
		} else if (msg.what == MsgType.msg_type_chat) {
			//AppData.onRecvNewChatMsg(msg);
			if (msgDb != null) {
				int unreadFlag = 1;
				//msgDb.insertChatMsg("xyz", msg.arg1, "", msg.arg2, "", (String)msg.obj, unreadFlag, "");
			}
			else
				LoggerFile.LogError("uninitialized msgdb");
		}
	}

	public static void OnFriendChange(tms.User.TargetsAdd tTargetsAdd) {
//		int uAccountID = application.getMemberEntity().getuAccountID();
//		int uTargetID = tTargetsAdd.getUTargetID();
//		if ((tTargetsAdd.getUError() == 0) && (tTargetsAdd.getUTargetID() != 0)) {
//			switch (tTargetsAdd.getNcmd()) {
//			case Agree:
//				try {
//					List<NewFriendEntity> list = BaseActivity.getDb().findAll(
//							Selector.from(NewFriendEntity.class)
//									.where("uAccountID", "=", uAccountID)
//									.and(WhereBuilder.b("uTargetID", "=",
//											uTargetID)));
//					if (list == null) {
//
//						// sava
//
//						NewFriendEntity newfriendInfo = new NewFriendEntity();
//						newfriendInfo.setNcmd(tTargetsAdd.getNcmd().name());
//						newfriendInfo
//								.setnFace(tTargetsAdd.getObjs().getNFace());
//						newfriendInfo.setStrAccountNo(tTargetsAdd.getObjs()
//								.getStrAccountNo());
//						newfriendInfo.setStrNickName(tTargetsAdd.getObjs()
//								.getStrNickName().toStringUtf8());
//						newfriendInfo.setuTargetID(uTargetID);
//						newfriendInfo.setuAccountID(uAccountID);
//						newfriendInfo.setNeedop("true");
//						BaseActivity.getDb().save(newfriendInfo);
//
//					} else {
//						// //////
//						if (list.size() > 0) {
//							BaseActivity.getDb().delete(list.get(0));
//						}
//						NewFriendEntity newfriendInfo = new NewFriendEntity();
//						newfriendInfo.setNcmd(tTargetsAdd.getNcmd().name());
//						newfriendInfo
//								.setnFace(tTargetsAdd.getObjs().getNFace());
//						newfriendInfo.setStrAccountNo(tTargetsAdd.getObjs()
//								.getStrAccountNo());
//						newfriendInfo.setStrNickName(tTargetsAdd.getObjs()
//								.getStrNickName().toStringUtf8());
//						newfriendInfo.setuTargetID(uTargetID);
//						newfriendInfo.setuAccountID(uAccountID);
//						newfriendInfo.setNeedop("true");
//						BaseActivity.getDb().save(newfriendInfo);
//					}
//
//					List<FriendInfo> friends = BaseActivity.getDb().findAll(
//							Selector.from(FriendInfo.class)
//									.where("uAccountID", "=", uAccountID)
//									.and(WhereBuilder.b("uTargetID", "=",
//											uTargetID)));
//					// 可能是pc添加的好友
//					if (friends != null && friends.size() > 0) {
//
//					} else {
//						FriendInfo info = new FriendInfo();
//						info.setnFace(tTargetsAdd.getObjs().getNFace());
//						info.setStrAccountNo(tTargetsAdd.getObjs()
//								.getStrAccountNo());
//						info.setStrNickName(tTargetsAdd.getObjs()
//								.getStrNickName().toStringUtf8());
//						info.setuAccountID(uAccountID);
//						info.setuTargetID(uTargetID);
//
//						//AppData.updateFriendInfo(info);
//
//						tms.User.SysUserPrivateInfoList.Builder sysUserPrivateInfolist = tms.User.SysUserPrivateInfoList
//								.newBuilder();
//						tms.User.SysUserPrivateInfo.Builder sysUserPrivateInfo = tms.User.SysUserPrivateInfo
//								.newBuilder();
//						sysUserPrivateInfo.setUVersion(0);
//						sysUserPrivateInfo.setUTargetID(uTargetID);
//						sysUserPrivateInfolist.addNodes(sysUserPrivateInfo);
//
//						//con.sendProto(
//						//		tms.Base.cmd.n_SysUserPrivateInfoList_VALUE,
//						//		sysUserPrivateInfolist.build());
//					}
//
//				} catch (DbException e) {
//
//					e.printStackTrace();
//				}
//
//				break;
//			case Apply:
//
//				try {
//					List<NewFriendEntity> list = BaseActivity.getDb()
//							.findAll(
//									Selector.from(NewFriendEntity.class)
//											.where("uAccountID", "=",
//													uAccountID)
//											.and(WhereBuilder.b("uTargetID",
//													"=", uTargetID))
//											.and(WhereBuilder.b("ncmd", "=",
//													"Apply"))
//											.and(WhereBuilder.b("needop", "=",
//													"false")));
//					if (list != null && list.size() != 0) {
//
//					} else {
//						// sava
//						NewFriendEntity newfriendInfo = new NewFriendEntity();
//						newfriendInfo.setNcmd(tTargetsAdd.getNcmd().name());
//						newfriendInfo
//								.setnFace(tTargetsAdd.getObjs().getNFace());
//						newfriendInfo.setStrAccountNo(tTargetsAdd.getObjs()
//								.getStrAccountNo());
//						newfriendInfo.setStrNickName(tTargetsAdd.getObjs()
//								.getStrNickName().toStringUtf8());
//						newfriendInfo.setuTargetID(uTargetID);
//						newfriendInfo.setuAccountID(uAccountID);
//						newfriendInfo.setNeedop("true");
//						BaseActivity.getDb().save(newfriendInfo);
//
//					}
//				} catch (DbException e) {
//					// TODO Auto-generated catch block
//					e.printStackTrace();
//				}
//
//				break;
//			case Delete:
//				AppData.deleteFriend(uAccountID, uTargetID);
//				// try {
//				// List<FriendInfo> list = BaseActivity.getDb().findAll(
//				// Selector.from(FriendInfo.class)
//				// .where("uAccountID", "=", uAccountID)
//				// .and(WhereBuilder.b("uTargetID", "=",
//				// uTargetID)));
//				// if (list != null && list.size() > 0) {
//				// // delete list
//				// BaseActivity.getDb().delete(list.get(0));
//				// }
//				// } catch (DbException e) {
//				// // TODO Auto-generated catch block
//				// e.printStackTrace();
//				// }
//
//				break;
//			case Refuse:
//				// delete 添加某人的记录
//
//				break;
//			default:
//				break;
//			}
//		}
	}

}
