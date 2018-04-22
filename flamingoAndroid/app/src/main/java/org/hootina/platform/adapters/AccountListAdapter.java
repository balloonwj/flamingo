package org.hootina.platform.adapters;

import org.hootina.platform.R;
import org.hootina.platform.userinfo.UserInfo;

import android.content.Context;
import android.view.View;

import java.util.List;

/**
 * @desc 保存的账户列表
 */
public class AccountListAdapter extends BaseAdapter<UserInfo> {

	public AccountListAdapter(Context context, List<UserInfo> list) {
		super(context, list);

	}

	@Override
	public int getContentView() {
		return R.layout.item_account;
	}

	@Override
	public void onInitView(View view, int position) {
		//UserInfo user = getList().get(getCount() - 1 - position);
		//setText(R.id.tv_username, user.getStrAccountNo());
	}

}
