package org.hootina.platform.activities.member;

import java.util.List;

import android.os.Handler;
import android.os.Message;
import android.view.View;
import android.widget.TextView;

import org.hootina.platform.R;
import org.hootina.platform.activities.BaseActivity;
import org.hootina.platform.adapters.NewFriendAdapter;
import org.hootina.platform.result.NewFriendEntity;
import org.hootina.platform.widgets.x.XListView;
import org.hootina.platform.widgets.x.XListView.IXListViewListener;

import com.lidroid.xutils.db.sqlite.Selector;
import com.lidroid.xutils.exception.DbException;

public class NewFriendActivity extends BaseActivity implements
		IXListViewListener {
	/*
	 * (non-Javadoc)
	 * 
	 * @新的朋友
	 */
	private TextView addsend;
	private NewFriendAdapter newFriendAdapter;
	private XListView xlv_newfriend;
	private List<NewFriendEntity> list = null;
	private MyHandler handler = new MyHandler();
	private int uAccountID;

	@Override
	public void onClick(View v) {
		switch (v.getId()) {
		case R.id.btn_back:
			//con.getFriends(1);
			this.setResult(0);
			finish();
			break;
		case R.id.addsend:
			startActivity(AddFriendActivity.class);
			break;

		default:
			break;
		}
	}

	@Override
	protected int getContentView() {
		return R.layout.activity_newfriend;
	}

	@Override
	protected void initData() {
		addsend.setOnClickListener(this);
	}

	@Override
	protected void setData() {
		if(application.getMemberEntity()!=null){
			uAccountID=application.getMemberEntity().getuAccountID();
		}
//		try {
//			list = BaseActivity.getDb().findAll(Selector.from(NewFriendEntity.class).where(
//					"uAccountID", "=",
//					uAccountID));
//			if (list != null) {
//				newFriendAdapter = new NewFriendAdapter(this, list, handler);
//				xlv_newfriend.setAdapter(newFriendAdapter);
//			}
//
//		} catch (DbException e) {
//
//			e.printStackTrace();
//		}

	}

	public class MyHandler extends Handler {
		@Override
		public void dispatchMessage(Message msg) {
			super.dispatchMessage(msg);
			switch (msg.what) {
			case 0:
				String text = (String) (msg.obj);
				String[] ss = text.split("\\|");
				String cmdtype = ss[0];
				String TargetID = ss[1];
				int uTargetId = Integer.parseInt(TargetID);
				if (cmdtype.equals("Agree")) {
					//con.addFriend(uTargetId, tms.User.TargetsAdd.cmd.Agree);
				} else if (cmdtype.equals("Apply")){
					//con.addFriend(uTargetId, tms.User.TargetsAdd.cmd.Apply);
				}

				break;

			default:
				break;
			}
		}
	}

	@Override
	public void onRefresh() {

	}

	@Override
	public void onLoadMore() {

	}

}
