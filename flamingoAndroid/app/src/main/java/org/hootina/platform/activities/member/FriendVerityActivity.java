package org.hootina.platform.activities.member;

import android.os.Message;
import android.view.View;
import android.widget.TextView;

import org.hootina.platform.R;
import org.hootina.platform.activities.BaseActivity;
import org.hootina.platform.result.FriendInfo;
import org.hootina.platform.result.NewFriendEntity;

import com.lidroid.xutils.exception.DbException;

public class FriendVerityActivity extends BaseActivity {
	/*
	 * (non-Javadoc)
	 * 
	 * @see好友验证
	 */
	private String strAccountNo, strNickName, uTargetID;
	private TextView send, tv_name;
	private FriendInfo friendInfo;

	@Override
	public void onClick(View v) {
		int uTargetId = Integer.parseInt(uTargetID);
		switch (v.getId()) {
		case R.id.btn_back:
			finish();
			break;
		case R.id.send:
//			// 添加好友
//			//con.addFriend(uTargetId, tms.User.TargetsAdd.cmd.Apply);
//			//查询群成员信息
//			if (isGroup(Integer.valueOf(uTargetID))) {
//				//con.getGroup(Integer.valueOf(uTargetID));
//			}
//
//			try {
//				NewFriendEntity newfriendInfo = new NewFriendEntity();
//				newfriendInfo.setNcmd("Apply");
//				newfriendInfo.setnFace(0);
//				newfriendInfo.setStrNickName(strNickName);
//				newfriendInfo.setStrAccountNo(strAccountNo);
//				newfriendInfo.setuTargetID(Integer.valueOf(uTargetID));
//				newfriendInfo.setuAccountID(application.getMemberEntity()
//						.getuAccountID());
//				newfriendInfo.setNeedop("false");
//				BaseActivity.getDb().save(newfriendInfo);
//			} catch (DbException e) {
//				// TODO Auto-generated catch block
//				e.printStackTrace();
//			}
//
//			finish();
			break;

		default:
			break;
		}
	}

	@Override
	protected int getContentView() {
		return R.layout.activity_friendverity;
	}

	@Override
	protected void initData() {
		strAccountNo = getIntent().getStringExtra("strAccountNo");
		strNickName = getIntent().getStringExtra("strNickName");
		// 添加好友的ID
		uTargetID = getIntent().getStringExtra("uTargetID");
	}

	@Override
	protected void setData() {
		send.setOnClickListener(this);
		tv_name.setText(application.getMemberEntity().getStrAccountNo2());

	}

	@Override
	public void processMessage(Message msg) {
		super.processMessage(msg);
		// if (msg.what == MegAsnType.TargetsAddNotify) {
		// TargetsAddNotify tTargetsAddNotify = (TargetsAddNotify) msg.obj;
		// try {
		// tTargetsAddNotify.getTargetsAdds().getNcmd();
		// friendInfo = new FriendInfo();
		// friendInfo.setNcmd(tTargetsAddNotify.getTargetsAdds()
		// .getUTargetID());
		// friendInfo.setnFace(0);
		// friendInfo.setStrAccountNo(tTargetsAddNotify
		// .getTargetInfos().getStrAccountNo());
		// friendInfo.setmNickName(tTargetsAddNotify
		// .getTargetInfos().getmNickName().toStringUtf8());
		// friendInfo.setmFriendID(tTargetsAddNotify.getTargetsAdds()
		// .getUTargetID());
		// db.save(friendInfo);
		//
		// } catch (DbException e) {
		// e.printStackTrace();
		// }
		//
		// }
	}

}
