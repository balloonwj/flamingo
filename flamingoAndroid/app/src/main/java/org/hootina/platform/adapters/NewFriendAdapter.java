package org.hootina.platform.adapters;

import java.util.List;

import org.hootina.platform.R;
import org.hootina.platform.result.NewFriendEntity;

import android.content.Context;
import android.os.Handler;
import android.os.Message;
import android.view.LayoutInflater;
import android.view.View;
import android.view.View.OnClickListener;
import android.view.ViewGroup;
import android.widget.BaseAdapter;
import android.widget.Button;
import android.widget.TextView;

public class NewFriendAdapter extends BaseAdapter {
	private List<NewFriendEntity> chatMessages;
	private Context context;
	private Handler handler;

	public NewFriendAdapter(Context context, List<NewFriendEntity> messages,
			Handler handler) {
		super();
		this.context = context;
		this.chatMessages = messages;
		this.handler = handler;
	}

	@Override
	public int getCount() {
		return chatMessages.size();
	}

	@Override
	public Object getItem(int position) {
		return chatMessages.get(position);
	}

	@Override
	public long getItemId(int position) {
		return position;
	}

	@Override
	public View getView(int position, View convertView, ViewGroup parent) {
		final ViewHolder holder = new ViewHolder();
		NewFriendEntity message = chatMessages.get(position);
		convertView = LayoutInflater.from(context).inflate(
				R.layout.item_search_user, null);

		holder.name = (TextView) convertView.findViewById(R.id.tv_window_title);
		holder.number = (TextView) convertView.findViewById(R.id.tv_nickname);
		holder.btn_add = (Button) convertView.findViewById(R.id.btn_add);
		holder.name.setText(message.getStrNickName());
		holder.number.setText(message.getStrAccountNo());
		holder.btn_add.setTag(position);
		if (message.getNcmd().equals("Apply")
				&& message.getNeedop().equals("true")) {
			holder.btn_add.setText("接受");
			holder.btn_add.setVisibility(View.VISIBLE);
			holder.btn_add.setOnClickListener(new OnClickListener() {

				@Override
				public void onClick(View v) {

					int position = (Integer) v.getTag();
					// 支付
					Message msg = Message.obtain();
					msg.what = 0;
					msg.arg1 = position;
					msg.obj = "Agree" + "|"
							+ chatMessages.get(position).getuTargetID();

					handler.sendMessage(msg);
					holder.btn_add.setText("已添加");
				}
			});

		} else if (message.getNcmd().equals("Agree")) {
			holder.btn_add.setText("已添加");
			holder.btn_add.setVisibility(View.VISIBLE);
		} else if (message.getNeedop().equals("false")) {
			holder.btn_add.setText("待添加");
			holder.btn_add.setVisibility(View.VISIBLE);
		}

		return convertView;
	}

	static class ViewHolder {
		TextView name;
		TextView number;
		Button btn_add;

	}
}
