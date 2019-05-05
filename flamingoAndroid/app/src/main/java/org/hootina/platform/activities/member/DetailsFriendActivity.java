package org.hootina.platform.activities.member;

import android.os.Message;
import android.view.View;
import android.widget.TextView;

import org.hootina.platform.R;
import org.hootina.platform.activities.BaseActivity;
import org.hootina.platform.result.FriendInfo;


/*
 * (non-Javadoc)
 * @用户详细资料
 */
public class DetailsFriendActivity extends BaseActivity {
	private String strAccountNo, strNickName, uTargetID;
	private TextView name, number;
	private FriendInfo friendInfo;
	private int uAccountID;

	@Override
	protected int getContentView() {
		return R.layout.activity_detailsfriend;
	}

	@Override
	protected void initData() {
		strAccountNo = getIntent().getStringExtra("strAccountNo");
		strNickName = getIntent().getStringExtra("strNickName");
		uTargetID = getIntent().getStringExtra("uTargetID");
	}

	@Override
	protected void setData() {
		if (application.getMemberEntity() != null) {
			uAccountID = application.getMemberEntity().getuAccountID();
		}
		setText(name, strNickName);
		setText(number, strAccountNo);
	}

	@Override
	public void onClick(View v) {
		switch (v.getId()) {
		case R.id.btn_back:
			finish();
			break;
		case R.id.btn_addfriend:
			// 添加好友
			
//			try {
//				// 添加或待添加
//				List<NewFriendEntity> lists = BaseActivity.getDb().findAll(
//						Selector.from(NewFriendEntity.class)
//								.where("uAccountID", "=", uAccountID)
//								.and(WhereBuilder
//										.b("uTargetID", "=", Integer.valueOf(uTargetID))));
//
//				List<FriendInfo> list = BaseActivity.getDb().findAll(
//						Selector.from(FriendInfo.class)
//								.where("uAccountID", "=", uAccountID)
//								.and(WhereBuilder
//										.b("uTargetID", "=", Integer.valueOf(uTargetID))));
//
//				if (Integer.parseInt(uTargetID) == uAccountID) {
//					makeTextLong("不能添加自己为好友");
//				} else {
//					if ((list == null || list.size() == 0)) {
//						if (lists != null&&lists.size()!=0) {
//							makeTextLong("已发送此用户好友请求");
//						} else {
//
//							Intent intent = new Intent(
//									DetailsFriendActivity.this,
//									FriendVerityActivity.class);
//							intent.putExtra("strAccountNo", strAccountNo);
//							intent.putExtra("strNickName", strNickName);
//							intent.putExtra("uTargetID", uTargetID);
//							startActivity(intent);
//						}
//					} else {
//						makeTextLong("此用户已经是您的好友");
//					}
//				}
//			} catch (DbException e) {
//				e.printStackTrace();
//			}

			break;

		default:
			break;
		}
	}

	@Override
	public void processMessage(Message msg) {
		super.processMessage(msg);
//		if (msg.what == MegAsnType.TargetsAddNotify) {
			// 对方方同意添加好友
//			TargetsAddNotify tTargetsAddNotify = (TargetsAddNotify) msg.obj;
//			try {
//
//				friendInfo = new FriendInfo();
//				friendInfo.setnFace(0);
//				friendInfo.setStrAccountNo(tTargetsAddNotify.getTargetInfos()
//						.getStrAccountNo());
//				friendInfo.setStrNickName(tTargetsAddNotify.getTargetInfos()
//						.getStrNickName().toStringUtf8());
//				friendInfo.setuTargetID(tTargetsAddNotify.getTargetsAdds()
//						.getUTargetID());
//				BaseActivity.getDb().save(friendInfo);
//			} catch (DbException e) {
//				e.printStackTrace();
//			}

//		}

	}

}
