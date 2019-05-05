package org.hootina.platform.activities.details;

import android.app.ActionBar;
import android.app.Dialog;
import android.content.Intent;
import android.os.Message;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.RelativeLayout;
import android.widget.TextView;

import org.hootina.platform.R;
import org.hootina.platform.activities.BaseActivity;
import org.hootina.platform.activities.member.ChattingActivity;
import org.hootina.platform.enums.MsgType;
import org.hootina.platform.model.FriendList;
import org.hootina.platform.model.GroupInfos;
import org.hootina.platform.net.ChatSessionMgr;
import org.hootina.platform.net.NetWorker;
import org.hootina.platform.userinfo.UserInfo;
import org.hootina.platform.userinfo.UserSession;

/*
 * 群组详细信息
 */
public class GroupDetailActivity extends BaseActivity {
	private RelativeLayout 	rl_group;
	private TextView 		tv_number;
	private TextView 		tv_window_title;
	private TextView 		tv_group_name;
	private Button 			btn_sendmsg;
	private String 			_groupName = "";
	private int 			_groupid = 0;
	private int 			_groupMemberNumber = 0;
	private TextView 		txt_num;
	private ImageView 		mImgMore;

	@Override
	public void onClick(View v) {
		switch (v.getId()) {
		case R.id.btn_back:
			finish();
			break;

		case R.id.btn_sendmsg:
			Intent intent = new Intent(this, ChattingActivity.class);
			intent.putExtra("nickname", _groupName);
			intent.putExtra("friendid", _groupid);
			intent.putExtra("msgtexts", "");
			intent.putExtra("type", "");
			startActivity(intent);
			break;

		case R.id.rl_group:
			//群成员
			Intent intent1 = new Intent(this, GroupMemberActivity.class);
			intent1.putExtra(GroupMemberActivity.GROUP_ID, _groupid);
			intent1.putExtra("groupName", _groupName);
			startActivity(intent1);
			break;

		default:
			break;
		}
	}

	@Override
	protected int getContentView() {
		return R.layout.activity_group_detail;
	}

	@Override
	protected void initData() {
		//strNickName = getIntent().getStringExtra("groupname");
		_groupid = getIntent().getIntExtra("groupID", 0);
		//_groupMemberNumber = getIntent().getIntExtra("groupmembernum", 0);
        _groupName = getIntent().getStringExtra("groupName");
		tv_window_title = (TextView)findViewById(R.id.tv_window_title);
        tv_window_title.setText(_groupName);
        tv_group_name = (TextView)findViewById(R.id.tv_group_name);
        tv_group_name.setText(_groupName);
		tv_number.setText("共" + _groupMemberNumber + "个成员");
		txt_num.setText(_groupid + "");
		NetWorker.getGroupMember(_groupid);

		mImgMore = (ImageView) findViewById(R.id.img_more);
		mImgMore.setOnClickListener(new View.OnClickListener() {
			@Override
			public void onClick(View item) {
				View view = getLayoutInflater().inflate(R.layout.dialog_delete_group,
						null);
				final Dialog dialog = new Dialog(GroupDetailActivity.this, R.style.ActionSheetDialogAnimation);
				dialog.setContentView(view, new ActionBar.LayoutParams(ActionBar.LayoutParams.FILL_PARENT,
						ActionBar.LayoutParams.WRAP_CONTENT));
				Window window = dialog.getWindow();
				// 设置显示动画
				window.setWindowAnimations(R.style.popwin_down_up);
				WindowManager.LayoutParams wl = window.getAttributes();
				wl.x = 0;
				wl.y = getWindowManager().getDefaultDisplay().getHeight();
				// 以下这两句是为了保证按钮可以水平满屏
				wl.width = ViewGroup.LayoutParams.MATCH_PARENT;
				wl.height = ViewGroup.LayoutParams.WRAP_CONTENT;
				// 设置显示位置
				dialog.onWindowAttributesChanged(wl);
				// 设置点击外围解散
				dialog.setCanceledOnTouchOutside(true);
				dialog.show();
				View btn_sesseion = view.findViewById(R.id.btn_delete_session);
				btn_sesseion.setOnClickListener(new View.OnClickListener() {
					@Override
					public void onClick(View view) {
						ChatSessionMgr.getInstance().deleteSession(_groupid);
						BaseActivity.getChatMsgDb().deleteChatMsg(_groupid);
						dialog.dismiss();
						finish();
					}
				});

				View btn_cancel = view.findViewById(R.id.btn_cancel);
				btn_cancel.setOnClickListener(new View.OnClickListener() {
					@Override
					public void onClick(View view) {
						dialog.dismiss();

					}
				});
				View btn_quit_group = view.findViewById(R.id.btn_quit_group);
				btn_quit_group.setOnClickListener(new View.OnClickListener() {
					@Override
					public void onClick(View view) {
						NetWorker.deleteFriend(_groupid);
						//GroupInfos.getInstance().deleteGroup(_groupid);
						//FriendList.getInstance().deleteUser(_groupid);
						ChatSessionMgr.getInstance().deleteSession(_groupid);
						BaseActivity.getChatMsgDb().deleteChatMsg(_groupid);

						dialog.dismiss();

						NetWorker.getFriendList();
						//finish();
					}
				});
			}
		});
	}

    @Override
    protected void onResume() {
        super.onResume();
        NetWorker.getGroupMember(_groupid);
    }

    @Override
	public void processMessage(Message msg) {
		super.processMessage(msg);
		if (msg.what == MsgType.msg_type_operatefriend) {
			//删除好友成功, 重新拉取下好友列表
			if (msg.arg1 == 5) {
				NetWorker.getFriendList();
			}
		} else if (msg.what == MsgType.msg_type_getfriendlist) {
			if (msg.arg1 != MsgType.ERROR_CODE_SUCCESS)
				return;

			//FriendList.setFriendList((FriendList) msg.obj);
			//Toast.makeText(this, "成功退群", Toast.LENGTH_SHORT);
			finish();
		} else if (msg.what == MsgType.msg_type_getgroupmembers) {
            UserInfo currentGroup = UserSession.getInstance().getGroupById(_groupid);
            if (currentGroup != null) {
                _groupMemberNumber = currentGroup.groupMembers.size();

                tv_number.setText("共" + _groupMemberNumber + "个成员");
            }
        }
	}

	@Override
	protected void setData() {
		rl_group.setOnClickListener(this);
	}

}
