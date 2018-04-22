package org.hootina.platform.adapters;

import java.util.List;

import android.content.Context;
import android.view.View;
import android.widget.ImageView;

import org.hootina.platform.R;
import org.hootina.platform.FlamingoApplication;
import org.hootina.platform.services.Server;

public class ServerAdapter extends BaseAdapter<Server> {
	private FlamingoApplication application;

	public ServerAdapter(Context context, List<Server> list) {
		super(context, list);
	}

	@Override
	public int getContentView() {
		return R.layout.item_server;
	}

	@Override
	public void onInitView(View view, int position) {
		Server server = getList().get(position);
		setText(R.id.tv_name, server.getName());
		ImageView iv_chek = (ImageView) (view.findViewById(R.id.iv_chek));
	}

}
