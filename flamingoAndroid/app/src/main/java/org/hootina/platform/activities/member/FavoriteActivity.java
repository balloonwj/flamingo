package org.hootina.platform.activities.member;

import java.util.List;

import android.view.View;
import android.widget.ListView;

import org.hootina.platform.R;
import org.hootina.platform.activities.BaseActivity;
import org.hootina.platform.adapters.FavoriteAdapter;
import org.hootina.platform.result.FavoriteItem;

/**
 * 收藏夹
 */
public class FavoriteActivity extends BaseActivity {
	private List<FavoriteItem> 	list;
	private int 				uAccountID;
	private ListView 			lv_shoucang;
	private FavoriteAdapter 	favoriteAdapter;
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
		return R.layout.activity_favorite;
	}

	@Override
	protected void initData() {
		if (application.getMemberEntity() != null) {
			uAccountID = application.getMemberEntity().getuAccountID();
		}
	}

	@Override
	protected void setData() {
		loadFavoriteItemList();
	}
	private void loadFavoriteItemList() {
//		try {
//			list = BaseActivity.getDb().findAll(
//					Selector.from(FavoriteItem.class).where("uAccountID", "=",
//							uAccountID));
//		} catch (DbException e) {
//			e.printStackTrace();
//		}
//
//		if(list!=null&&list.size()!=0){
//
//			favoriteAdapter=new FavoriteAdapter(this,list);
//
//			lv_shoucang.setAdapter(favoriteAdapter);
//
//		}
		
		
		
	}
}
