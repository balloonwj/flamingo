package org.hootina.platform.adapters;

import java.util.List;

import org.hootina.platform.R;
import org.hootina.platform.activities.member.ForwardActivity;
import org.hootina.platform.userinfo.UserInfo;
import org.hootina.platform.utils.PictureUtil;

import android.content.Context;
import android.graphics.Bitmap;
import android.os.Handler;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.ImageView;

public class FriendsAdapter extends BaseAdapter<UserInfo> {
	private Handler handler;

	public FriendsAdapter(Context context, List<UserInfo> list, Handler handler) {
		super(context, list);
		this.handler = handler;
	}

	@Override
	public int getContentView() {
		return R.layout.friend_item;
	}

	@Override
	public void onInitView(View view, int position) {
		UserInfo friendInfo = getList().get(position);
		setText(R.id.tv_window_title, friendInfo.get_nickname());
		setText2(R.id.txt_sign, friendInfo.get_onlinetype(), friendInfo.get_signature());

		ImageView iv = (ImageView) (view.findViewById(R.id.img_head));
		final ImageView iv_product_selected = (ImageView) (view
				.findViewById(R.id.iv_product_selected));
		if (iv == null) {
			return;
		}
		Bitmap bmp = null;
		if (context != null) {
			bmp = PictureUtil.getFriendHeadPic(context.getAssets(), friendInfo);
			if (bmp != null) {
				iv.setImageBitmap(bmp);
			}
		}

		if (bmp == null) {
			iv.setImageDrawable(iv.getResources().getDrawable(R.drawable.head));
		}
		if (ForwardActivity.isCheck) {
			iv_product_selected.setVisibility(View.VISIBLE);
		} else {
			iv_product_selected.setVisibility(View.GONE);
		}
		if (ForwardActivity.isChoice) {
			iv_product_selected
			.setBackgroundResource(R.drawable.rbtn_payway_unchecked_sc);
		}
		iv_product_selected.setTag(position);
		iv_product_selected.setOnClickListener(new OnClickListener() {

			@Override
			public void onClick(View v) {
				int position = (Integer) v.getTag();
				int uTargetID = getList().get(position).get_userid();
				String StrNickName=	getList().get(position).get_nickname();
//				getList().get(position).setSelected(!getList().get(position).isSelected());
//				Message msg = Message.obtain();
//				if (getList().get(position).isSelected()) {
//					iv_product_selected
//							.setBackgroundResource(R.drawable.rbtn_payway_checked_sc);
//					// 添加
//
//					msg.arg1 = uTargetID;
//					msg.obj=StrNickName;
//					msg.what = 0;
//					handler.sendMessage(msg);
//				} else {
//					// 取消
//					iv_product_selected
//							.setBackgroundResource(R.drawable.rbtn_payway_unchecked_sc);
//					msg.arg1 = uTargetID;
//					msg.obj=StrNickName;
//					msg.what = 1;
//					handler.sendMessage(msg);
//
//				}

			}
		});

	}

}
