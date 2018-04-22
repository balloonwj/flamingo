package org.hootina.platform.fragments;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

import org.hootina.platform.R;
import org.hootina.platform.activities.BaseActivity;
import org.hootina.platform.activities.details.FriendDetailInfoActivity;
import org.hootina.platform.activities.member.AddFriendActivity;
import org.hootina.platform.activities.member.GroupActivity;
import org.hootina.platform.activities.member.NewFriendActivity;
import org.hootina.platform.adapters.FriendsAdapter;
import org.hootina.platform.enums.MsgType;
import org.hootina.platform.result.AppData;
import org.hootina.platform.result.FileInfo;
import org.hootina.platform.result.FriendInfo;
import org.hootina.platform.result.NewFriendEntity;
import org.hootina.platform.userinfo.UserInfo;
import org.hootina.platform.userinfo.UserSession;
import org.hootina.platform.util.MegAsnType;
import com.lidroid.xutils.DbUtils;
import com.lidroid.xutils.db.sqlite.Selector;
import com.lidroid.xutils.db.sqlite.WhereBuilder;
import com.lidroid.xutils.exception.DbException;

import android.content.Intent;
import android.os.Handler;
import android.os.Message;
import android.util.Log;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ImageButton;
import android.widget.ListView;
import android.widget.RelativeLayout;
import android.widget.TextView;
import android.widget.AdapterView.OnItemClickListener;

/*
 * 联系人
 */
public class FriendFragment extends BaseFragment {
	private ImageButton 		btn_add;
	private RelativeLayout 		rel_newfrd;
	private RelativeLayout 		rel_group;
	private ListView 			lv_friends;
	private FriendsAdapter 		friendsAdapter;
	private DbUtils 			db;
	private FriendInfo 			friendInfo;
	private String 				picname;
	private File 				file;
	private byte[] 				contentIntleng;
	private int 				uAccountID;
	private List<FileInfo> 		m_downloadingFiles;
	private int 				mnLoad = 0;
	private TextView 			tv_friend_num;
	private RelativeLayout 		rl_newfriend;

	public int getAccountID() {
		return uAccountID;
	}

	private MysHandler handler = new MysHandler();

	public class MysHandler extends Handler {
		@Override
		public void dispatchMessage(Message msg) {
			super.dispatchMessage(msg);
			switch (msg.what) {
			case 0:
				break;
			case 1:
				break;
			}
		}
	}

	@Override
	public void onClick(View v) {
		switch (v.getId()) {
		case R.id.btn_add:
			Intent intent = new Intent(getActivity(), AddFriendActivity.class);
			startActivityForResult(intent, 0);
			break;

		case R.id.rel_newfrd:
			Intent intent1 = new Intent(getActivity(), NewFriendActivity.class);
			// startActivityForResult(NewFriendActivity.class, 0);
			startActivityForResult(intent1, 0);
			break;

		case R.id.rel_group:
			startActivity(GroupActivity.class);

		default:
			break;
		}
	}

	@Override
	public void onActivityResult(int requestCode, int resultCode, Intent data) {
		super.onActivityResult(requestCode, resultCode, data);
		// con.getFriends(1);
		loadFriendList();
	}

	public void setFrirndlistCount() {
		if (mnLoad == 0) {
			// con.getFriends(1);
		}
	}

	public boolean isGroup(int nID) {
		int nMask = nID & 0xE0000000;
		if (nMask == 0xC0000000) {
			return true;
		}
		return false;
	}

	private void loadFriendList() {
		List<UserInfo> list = UserSession.getInstance().friends;
		if (list != null && lv_friends != null) {
			List<UserInfo> friends = new ArrayList<UserInfo>();
			for (UserInfo u : list)
			{
				if (u.get_userid() < 0x0FFFFFFF)
					friends.add(u);
			}

			friendsAdapter = new FriendsAdapter(getActivity(), friends, handler);
			lv_friends.setAdapter(friendsAdapter);
		}

		if (tv_friend_num != null) {
			int nWaitFriend = AppData.getFriendApplyWaiting(BaseActivity
					.getMyAccountID());
			if (nWaitFriend == 0) {
				tv_friend_num.setVisibility(View.GONE);
			} else {
				rl_newfriend.setVisibility(View.VISIBLE);
				// tv_friend_num.setVisibility(View.VISIBLE);
				String strText = "..";
				if (nWaitFriend < 100) {
					strText = String.valueOf(nWaitFriend);
				}
				tv_friend_num.setText(strText);
			}
		}
	}

	private void refreshFriend(UserInfo friendInfo) {
		List<UserInfo> list;
		if (friendsAdapter == null) {
			list = new ArrayList<UserInfo>();
			list.add(friendInfo);
			friendsAdapter = new FriendsAdapter(getActivity(), list, handler);
		} else {
			list = friendsAdapter.getList();
			int nFlag = 0;
			for (int i = 0; i < list.size(); ++i) {
				if (list.get(i).get_userid() == friendInfo.get_userid()) {
					friendsAdapter.getList().set(i, friendInfo);
					nFlag = 1;
					break;
				}
			}

			if (nFlag == 0) {
				friendsAdapter.getList().add(friendInfo);
			}
		}

		lv_friends.setAdapter(friendsAdapter);
	}

	@Override
	protected int getContentView() {
		return R.layout.activity_friend;
	}

	@Override
	protected void initData() {
			loadFriendList();
	}

	@Override
	protected void setData() {
		btn_add.setOnClickListener(this);
		rel_group.setOnClickListener(this);
		rel_newfrd.setOnClickListener(this);
		lv_friends.setOnItemClickListener(new OnItemClickListener() {
			@Override
			public void onItemClick(AdapterView<?> parent, View view, int position, long id) {
				UserInfo currentUser = friendsAdapter.getList().get(position);
				if (currentUser == null)
					return;
				Intent intent = new Intent(getActivity(), FriendDetailInfoActivity.class);
				intent.putExtra("userid", currentUser.get_userid());
				intent.putExtra("nickname", currentUser.get_nickname());
				intent.putExtra("username", currentUser.get_username());
				intent.putExtra("signature", currentUser.get_signature());
//				startActivity(intent);
				startActivityForResult(intent, 0);
			}
		});
	}

	@Override
	protected void processLogic() {
		Log.i("zhangyl test", "aaa");
	}

	@Override
	public void processMessage(Message msg) {
		if (msg.what == MsgType.msg_type_getfriendlist) {
			if (msg.arg1 == MsgType.ERROR_CODE_SUCCESS)
				loadFriendList();
		}



		else if (msg.what == MegAsnType.UserPrivateInfoList) {

			loadFriendList();
		} else if (msg.what == MegAsnType.FileLoadData) {
			loadFriendList();
		} else if (msg.what == MegAsnType.FriendChange) {
			loadFriendList();
		} else if (msg.what == MegAsnType.TargetInfo) {
			mnLoad = 1;
		}

	}

	public void OnFriendChange(tms.User.TargetsAdd tTargetsAdd) {
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
//					FriendInfo friend = null; //AppData.findFriend(uAccountID, uTargetID);
//					// 可能是pc添加的好友
//					if (friend != null) {
//					} else {
//						FriendInfo info = new FriendInfo();
//						info.setnFace(tTargetsAdd.getObjs().getNFace());
//						info.setStrAccountNo(tTargetsAdd.getObjs()
//								.getStrAccountNo());
//						info.setStrNickName(tTargetsAdd.getObjs()
//								.getStrNickName().toStringUtf8());
//						info.setuAccountID(uAccountID);
//						info.setuTargetID(uTargetID);
//						//AppData.updateFriendInfo(info);
//						tms.User.SysUserPrivateInfoList.Builder sysUserPrivateInfolist = tms.User.SysUserPrivateInfoList
//								.newBuilder();
//						tms.User.SysUserPrivateInfo.Builder sysUserPrivateInfo = tms.User.SysUserPrivateInfo
//								.newBuilder();
//						sysUserPrivateInfo.setUVersion(0);
//						sysUserPrivateInfo.setUTargetID(uTargetID);
//						sysUserPrivateInfolist.addNodes(sysUserPrivateInfo);
////						con.sendProto(
////								tms.Base.cmd.n_SysUserPrivateInfoList_VALUE,
////								sysUserPrivateInfolist.build());
//					}
//
//				} catch (DbException e) {
//
//					e.printStackTrace();
//				}
//				break;
//			case Apply:
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
//
//				try {
//					List<FriendInfo> list = BaseActivity.getDb().findAll(
//							Selector.from(FriendInfo.class)
//									.where("uAccountID", "=", uAccountID)
//									.and(WhereBuilder.b("uTargetID", "=",
//											uTargetID)));
//					if (list != null && list.size() > 0) {
//						// delete list
//						BaseActivity.getDb().delete(list.get(0));
//					}
//				} catch (DbException e) {
//					// TODO Auto-generated catch block
//					e.printStackTrace();
//				}
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
