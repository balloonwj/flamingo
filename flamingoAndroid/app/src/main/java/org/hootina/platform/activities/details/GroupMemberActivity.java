package org.hootina.platform.activities.details;

import android.os.Handler;
import android.os.Message;
import android.view.View;
import android.widget.ListView;

import org.hootina.platform.R;
import org.hootina.platform.activities.BaseActivity;
import org.hootina.platform.adapters.FriendsAdapter;
import org.hootina.platform.userinfo.UserInfo;
import org.hootina.platform.userinfo.UserSession;

import java.util.List;

public class GroupMemberActivity extends BaseActivity {
	private ListView 		lv_groupmember;
	private int 			_groupid;
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
		return R.layout.activity_group_member;
	}

	@Override
	protected void initData() {
		_groupid = getIntent().getIntExtra("groupid", 0);
	}

	@Override
	protected void setData() {
		loadGroupMembers();
	}

	private void loadGroupMembers() {
		if (_groupid == 0)
			return;

		List<UserInfo> list = UserSession.getInstance().getGroupMembersById(_groupid);
		if (list == null || lv_groupmember == null)
			return;

		friendsAdapter = new FriendsAdapter(this, list, handler);
		lv_groupmember.setAdapter(friendsAdapter);
	}
}
