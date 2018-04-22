package org.hootina.platform.activities.member;

import java.util.List;

import android.view.View;
import android.widget.ListView;

import org.hootina.platform.R;
import org.hootina.platform.activities.BaseActivity;
import org.hootina.platform.adapters.ShouCangAdapter;
import org.hootina.platform.result.ShouCangInfo;
import com.lidroid.xutils.db.sqlite.Selector;
import com.lidroid.xutils.exception.DbException;

public class ShoucangActivity extends BaseActivity {
	private List<ShouCangInfo> list;
	private int uAccountID;
	private ListView lv_shoucang;
	private ShouCangAdapter shouCangAdapter;
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
		return R.layout.activity_shoucang;
	}

	@Override
	protected void initData() {
		if (application.getMemberEntity() != null) {
			uAccountID = application.getMemberEntity().getuAccountID();
		}
	}

	@Override
	protected void setData() {
		shoucanginfolist();


	}
	private void shoucanginfolist() {
//		try {
//			list = BaseActivity.getDb().findAll(
//					Selector.from(ShouCangInfo.class).where("uAccountID", "=",
//							uAccountID));
//		} catch (DbException e) {
//			e.printStackTrace();
//		}
//
//		if(list!=null&&list.size()!=0){
//
//			shouCangAdapter=new ShouCangAdapter(this,list);
//
//			lv_shoucang.setAdapter(shouCangAdapter);
//
//		}
		
		
		
	}
}
