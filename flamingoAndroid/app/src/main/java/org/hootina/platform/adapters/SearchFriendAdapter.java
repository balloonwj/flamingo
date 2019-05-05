package org.hootina.platform.adapters;

import android.content.Context;
import android.view.View;

import org.hootina.platform.R;
import org.hootina.platform.model.TargetInfo;

import java.util.List;

public class SearchFriendAdapter extends BaseAdapter<TargetInfo> {
	public SearchFriendAdapter(Context context, List<TargetInfo> list) {
		super(context, list);
	}

	@Override
	public int getContentView() {
		return R.layout.item_search_user;
	}

	@Override
	public void onInitView(View view, int position) {
		//TargetInfo targetInfo = getList().get(position);
		//setText(R.id.txt_name, targetInfo.getStrNickName().toStringUtf8());
		//setText(R.id.txt_info, targetInfo.getStrAccountNo());
//		setVisibility(R.id.txt_add, View.VISIBLE);
		
	}
}
