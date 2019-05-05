package org.hootina.platform.fragments;

import android.content.Intent;
import android.content.res.AssetManager;
import android.graphics.Bitmap;
import android.os.Message;
import android.view.View;
import android.widget.ImageView;
import android.widget.RelativeLayout;
import android.widget.TextView;

import org.hootina.platform.R;
import org.hootina.platform.activities.member.MyPersonInfoActivity;
import org.hootina.platform.activities.member.SettingActivity;
import org.hootina.platform.activities.member.FavoriteActivity;
import org.hootina.platform.model.LoginResult;
import org.hootina.platform.userinfo.UserSession;
import org.hootina.platform.utils.PictureUtil;

import java.io.File;

/*
 * 我的
 */
public class AboutMeFragment extends BaseFragment {
	private TextView 		txt_setting;
	private TextView 		txt_shoucang;
	private TextView 		tvname;
	private TextView 		tvnum;
	private RelativeLayout 	view_user;
	private ImageView 		iv_head;
	private File 			file;
	private byte[] 			contentIntleng;

	private View mView;

	@Override
	public void onClick(View v) {
		switch (v.getId()) {
		case R.id.txt_setting: {
			Intent intent = new Intent(getActivity(), SettingActivity.class);
			startActivity(intent);

			// startActivity(SettingActivity.class);
			break;
		}

		case R.id.view_user: {
			Intent intent = new Intent(getActivity(), MyPersonInfoActivity.class);
			startActivity(intent);
			break;
		}

		case R.id.txt_shoucang: {
			Intent intent = new Intent(getActivity(), FavoriteActivity.class);
			startActivity(intent);
			// startActivity(MyPersonInfoActivity.class);
			break;
		}

		default:
			break;
		}
	}

	@Override
	protected int getContentView() {
		return R.layout.activity_fragment_me;
	}

	@Override
	protected void initData(View view) {
        //tvname.setText(application.getMemberEntity().getNickname());
        //tvnum.setText("账号:" + application.getMemberEntity().getStrAccountNo2());
        tvname.setText(LoginResult.getInstance().getNickname());
        tvnum.setText("账号:" + LoginResult.getInstance().getUsername());
        AssetManager mgr = getActivity().getAssets();
        Bitmap bmp = PictureUtil.getHeadPic(mgr, UserSession.getInstance().loginUser);
        if (bmp != null) {
            iv_head.setImageBitmap(bmp);
		}
	}

	@Override
	public void onResume() {
		super.onResume();
		tvname.setText(LoginResult.getInstance().getNickname());
		tvnum.setText("账号:" + LoginResult.getInstance().getUsername());
		AssetManager mgr = getActivity().getAssets();
		Bitmap bmp = PictureUtil.getHeadPic(mgr, UserSession.getInstance().loginUser);
		if (bmp != null) {
			iv_head.setImageBitmap(bmp);
		}
	}

	@Override
	protected void setData() {
		view_user.setOnClickListener(this);
		txt_setting.setOnClickListener(this);
		txt_shoucang.setOnClickListener(this);
	}

	@Override
	public void processMessage(Message msg) {
		super.processMessage(msg);
	//	if (msg.what == MegAsnType.FileLoadData) {
			// 下载成功
//			AssetManager mgr = getActivity().getAssets();
//			Bitmap bmp = PictureUtil.getHeadPic(mgr, application.getMemberEntity());
//			if (bmp != null) {
//				iv_head.setImageBitmap(bmp);
//			}
		//}
	}

	@Override
	protected void processLogic() {
		if (application == null) {
			return;
		}

		if (application.getMemberEntity() == null) {

		} else {
//			AssetManager mgr = getActivity().getAssets();
//			Bitmap bmp = PictureUtil.getHeadPic(mgr, application.getMemberEntity());
//			if (bmp != null) {
//				iv_head.setImageBitmap(bmp);
//			}
		}

	}
}
