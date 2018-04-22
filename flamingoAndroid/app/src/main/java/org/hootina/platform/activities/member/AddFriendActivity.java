package org.hootina.platform.activities.member;
import java.util.ArrayList;
import java.util.List;
import tms.Base.TargetInfo;
import tms.User.TargetsNotify;
import tms.User.TargetsQuery;
import android.content.Intent;
import android.os.Message;
import android.view.View;
import android.widget.AdapterView;
import android.widget.AdapterView.OnItemClickListener;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.hootina.platform.R;
import org.hootina.platform.activities.BaseActivity;
import org.hootina.platform.adapters.SearcnFrindAdapter;
import org.hootina.platform.widgets.x.XListView;
import org.hootina.platform.widgets.x.XListView.IXListViewListener;
import org.hootina.platform.util.MegAsnType;

/*
 * 添加好友
 */
public class AddFriendActivity extends BaseActivity implements
		IXListViewListener {
   private LinearLayout ll_person,ll_group;
   private ImageView iv_group,iv_person;
	private EditText et_addSearch;
	private TextView tv_search,tv_person,tv_group;
	private String userId;
	private TargetInfo targetinfo;
	private SearcnFrindAdapter SearcAdapter;
	private XListView xlv_searchfriend;
	private int uTargetID;
    private String number;
    private int type=0;
	@Override
	public void onClick(View v) {
	 number=	et_addSearch.getText().toString().trim();
		switch (v.getId()) {
		case R.id.btn_back:
			finish();
			break;
		case R.id.tv_search:
			if(type==0){
				//con.searchUser(number,tms.User.TargetsQuery.cmd.User);
			}else if(type==1){
				//con.searchUser(number,tms.User.TargetsQuery.cmd.Group);
			}
			
			break;
		case R.id.ll_person:
			// 好友
			type=0;
			cleanSelectedStatus();
			tv_person.setTextColor(getResources().getColor(R.color.dark_gray_text));
			iv_person.setBackgroundColor(getResources().getColor(R.color.green));

			break;
		case R.id.ll_group:
			// 群
			type=1;
			cleanSelectedStatus();
			tv_group.setTextColor(getResources().getColor(R.color.dark_gray_text));
			iv_group.setBackgroundColor(getResources().getColor(R.color.green));

			break;
		default:
			break;
		}
	}

	/**
	 * 清除选中状态
	 */
	private void cleanSelectedStatus() {
		tv_person.setTextColor(getResources().getColor(R.color.gray_text));
		tv_group.setTextColor(getResources().getColor(R.color.gray_text));
		iv_person.setBackgroundColor(getResources().getColor(R.color.white));
		iv_group.setBackgroundColor(getResources().getColor(R.color.white));
	}


	@Override
	public void processMessage(Message msg) {
		// 返回查询好友
		if (msg.what == MegAsnType.TargetsNotify) {
			TargetsNotify tTargetsNotify = (TargetsNotify) msg.obj;
			List<TargetInfo> tis = new ArrayList<TargetInfo>();
			targetinfo = tTargetsNotify.getTargetss(0).getTargetInfos();
			tis.add(targetinfo);
			uTargetID = tTargetsNotify.getTargetss(0).getUTargetID();
			SearcAdapter = new SearcnFrindAdapter(this, tis);
			xlv_searchfriend.setAdapter(SearcAdapter);

		} else if (msg.what == MegAsnType.TargetsQuery) {

			TargetsQuery tTargetsQuery = (TargetsQuery) msg.obj;
			if (tTargetsQuery.getUError() == 0) {
				List<TargetInfo> tis = new ArrayList<TargetInfo>();
				targetinfo = tTargetsQuery.getNodes(0).getTargetInfos();
				tis.add(targetinfo);
				uTargetID = tTargetsQuery.getNodes(0).getUTargetID();
				SearcAdapter = new SearcnFrindAdapter(this, tis);
				xlv_searchfriend.setAdapter(SearcAdapter);
			}
		}
	}

	@Override
	protected int getContentView() {
		return R.layout.activity_addfriends;
	}

	@Override
	protected void initData() {
		tv_search.setOnClickListener(this);
		ll_person.setOnClickListener(this);
		ll_group.setOnClickListener(this);
	}

	@Override
	protected void setData() {
		xlv_searchfriend.setXListViewListener(this);
		xlv_searchfriend.setPullRefreshEnable(true);
		xlv_searchfriend.setPullLoadEnable(false);
		xlv_searchfriend.setAutoLoadEnable(false);
		xlv_searchfriend.setOnItemClickListener(new OnItemClickListener() {

			@Override
			public void onItemClick(AdapterView<?> parent, View view,
					int position, long id) {

				Intent intent = new Intent(AddFriendActivity.this,
						DetailsFriendActivity.class);
				intent.putExtra("strAccountNo",
						SearcAdapter.getList().get(position - 1)
								.getStrAccountNo());
				intent.putExtra("strNickName",
						SearcAdapter.getList().get(position - 1)
								.getStrNickName().toStringUtf8());
				intent.putExtra("uTargetID", String.valueOf(uTargetID));
				startActivity(intent);

			}
		});

	}

	@Override
	public void onRefresh() {

	}

	@Override
	public void onLoadMore() {

	}

}
