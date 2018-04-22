package org.hootina.platform.activities.member;

import android.content.Intent;
import android.os.Handler;
import android.os.Message;
import android.view.View;
import android.widget.AdapterView;
import android.widget.AdapterView.OnItemClickListener;
import android.widget.ListView;

import org.hootina.platform.R;
import org.hootina.platform.activities.BaseActivity;
import org.hootina.platform.activities.details.GroupDetailActivity;
import org.hootina.platform.adapters.FriendsAdapter;
import org.hootina.platform.result.AppData;
import org.hootina.platform.userinfo.UserInfo;

import java.util.List;

public class GroupActivity extends BaseActivity {
	/*
	 * (non-Javadoc)
	 * 
	 * @see 群组
	 */
	private ListView 		lv_groups;
	private 				int uAccountID;
	private FriendsAdapter 	friendsAdapter;
	private MysHandler 		handler = new MysHandler();

	public class MysHandler extends Handler {
		@Override
		public void dispatchMessage(Message msg) {
			super.dispatchMessage(msg);
			switch (msg.what) {
			case 0:

				break;
			case 1:

				break;
			case 2:

				break;

			default:
				break;
			}
		}
	}
	@Override
	public void onClick(View v) {
		switch (v.getId()) {
		case R.id.btn_back:
			finish();
			break;

		default:
			break;
		}
	}

	@Override
	protected int getContentView() {
		return R.layout.activity_group;
	}

	@Override
	protected void initData() {
		loadGrouplist();
	}

	@Override
	protected void setData() {
		lv_groups.setOnItemClickListener(new OnItemClickListener() {

			@Override
			public void onItemClick(AdapterView<?> parent, View view, int position, long id) {
				Intent intent = new Intent(GroupActivity.this, GroupDetailActivity.class);
				UserInfo groupInfo = friendsAdapter.getList().get(position);
				if (groupInfo == null)
					return;
				intent.putExtra("groupname", groupInfo.get_nickname());
				intent.putExtra("groupid", groupInfo.get_userid());
				intent.putExtra("groupmembernum", groupInfo.groupMembers.size());

				startActivity(intent);
			}
		});

	}

	private void loadGrouplist() {
		List<UserInfo> list = AppData.getGroupList();
		if (list == null || lv_groups == null)
			return;

		friendsAdapter = new FriendsAdapter(this, list, handler);
		lv_groups.setAdapter(friendsAdapter);
	}
}
