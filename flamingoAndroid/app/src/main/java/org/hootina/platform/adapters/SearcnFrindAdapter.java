package org.hootina.platform.adapters;

import java.util.List;
import tms.Base.TargetInfo;
import android.content.Context;
import android.view.View;
import org.hootina.platform.R;

public class SearcnFrindAdapter extends BaseAdapter<TargetInfo> {
	public SearcnFrindAdapter(Context context, List<TargetInfo> list) {
		super(context, list);
	}

	@Override
	public int getContentView() {
		return R.layout.searchfriend_adapter;
	}

	@Override
	public void onInitView(View view, int position) {
		TargetInfo targetInfo = getList().get(position);
		setText(R.id.name, targetInfo.getStrNickName().toStringUtf8());
		setText(R.id.number, targetInfo.getStrAccountNo());
//		setVisibility(R.id.txt_add, View.VISIBLE);
		
	}

}
