package org.hootina.platform.activities.details;

import android.content.Intent;
import android.view.View;
import android.widget.Button;
import android.widget.RelativeLayout;
import android.widget.TextView;

import org.hootina.platform.R;
import org.hootina.platform.activities.BaseActivity;
import org.hootina.platform.activities.member.ChattingActivity;

/*
 * 群组详细信息
 */
public class GroupDetailActivity extends BaseActivity {
	private RelativeLayout 	rl_group;
	private TextView 		tv_number;
	private TextView 		tv_name;
	private TextView 		tv_names;
	private Button 			btn_sendmsg;
	private String 			strNickName;
	private int 			_groupid = 0;
	private int 			_groupMemberNumber = 0;

	@Override
	public void onClick(View v) {
		switch (v.getId()) {
		case R.id.btn_back:
			finish();
			break;

		case R.id.btn_sendmsg:
			Intent intent = new Intent(this, ChattingActivity.class);
			intent.putExtra("nickname", strNickName);
			intent.putExtra("userid", _groupid);
			intent.putExtra("msgtexts", "");
			intent.putExtra("type", "");
			startActivity(intent);
			break;

		case R.id.rl_group:
			//群成员
			Intent intent1 = new Intent(this, GroupMemberActivity.class);
			intent1.putExtra("groupid", _groupid);
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
		strNickName = getIntent().getStringExtra("groupname");
		_groupid = getIntent().getIntExtra("groupid", 0);
		_groupMemberNumber = getIntent().getIntExtra("groupmembernum", 0);
		tv_name.setText(strNickName);
		tv_names.setText(strNickName);
		tv_number.setText("共" + _groupMemberNumber + "个成员");
	}

	@Override
	protected void setData() {
		rl_group.setOnClickListener(this);
	}

}
