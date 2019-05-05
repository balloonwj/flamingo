package org.hootina.platform.activities.member;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;

import android.annotation.SuppressLint;
import android.app.ActionBar.LayoutParams;
import android.app.Dialog;
import android.content.Intent;
import android.content.res.AssetManager;
import android.graphics.Bitmap;
import android.net.Uri;
import android.os.Environment;
import android.os.Message;
import android.provider.MediaStore;
import android.util.Log;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.RelativeLayout;
import android.widget.TextView;
import android.widget.Toast;

import org.hootina.platform.R;
import org.hootina.platform.activities.BaseActivity;
import org.hootina.platform.activities.MainActivity;
import org.hootina.platform.activities.details.ModifyNicknameActivity;
import org.hootina.platform.activities.details.ModifySignatureActivity;
import org.hootina.platform.result.MemberEntity;
import org.hootina.platform.userinfo.UserInfo;
import org.hootina.platform.userinfo.UserSession;
import org.hootina.platform.utils.Md5Util;
import org.hootina.platform.utils.PictureUtil;

public class MyPersonInfoActivity extends BaseActivity {
    private static final int    PHOTO_REQUEST_CAMERA    = 1;// 拍照
    private static final int    PHOTO_REQUEST_GALLERY   = 2;// 从相册中选择
    private static final int    PHOTO_REQUEST_CUT       = 3;// 结果
    private static final String PHOTO_FILE_NAME         = "temp_photo.jpg";// 图片名称

    private RelativeLayout  rl_head;
    private RelativeLayout  rl_name;
    private RelativeLayout  rl_number;
    private RelativeLayout  rl_sign;
	private ImageView       iv_head;
	private TextView        tv_name;
    private TextView        tv_number;
    private TextView        tv_sign;
	private Button          btn_pictures;
    private TextView        btn_photo;
    private TextView        btn_unselect;
	private Dialog          photodialog;
	private MemberEntity    memberEntity;

	private File            tempFile;
	private Bitmap          bitmap;
	private File            file;
	private byte[]          contentIntleng;
	private String          picname;
	private String          name;
	private String          strUrl;

	@Override
	public void onClick(View v) {
		switch (v.getId()) {
		case R.id.btn_back:
		{
			startActivity(MainActivity.class);
			finish();
			break;
		}

		// 头像
		case R.id.rl_head:
			showPhotoDialog();
			break;

			// 昵称
		case R.id.rl_name:
			Intent intent = new Intent(this, ModifyNicknameActivity.class);
			intent.putExtra("nickname", UserSession.getInstance().loginUser.get_nickname());
			startActivity(intent);
			break;

		case R.id.rl_number:
			break;

			// 个性签名
		case R.id.rl_sign:
			Intent intents = new Intent(this, ModifySignatureActivity.class);
			intents.putExtra("signature", UserSession.getInstance().loginUser.get_signature());
			startActivity(intents);
			break;

		case R.id.btn_pictures:
			// 照相
			camera();
			break;

		case R.id.btn_photo:
			// 从相册获取
			gallery();
			break;

		case R.id.btn_unselect:
			// 取消相册
			photodialog.dismiss();
			break;

		default:
			break;
		}
	}

	@Override
	protected int getContentView() {
		return R.layout.activity_personinfo;

	}

	@Override
	protected void onResume() {
		super.onResume();

		UserInfo loginUser = UserSession.getInstance().loginUser;
		tv_name.setText(loginUser.get_nickname());
		tv_sign.setText(loginUser.get_signature());
		tv_number.setText(loginUser.get_username());

	}

	@Override
	protected void initData() {
		UserInfo loginUser = UserSession.getInstance().loginUser;

		tv_name.setText(loginUser.get_nickname());
		tv_sign.setText(loginUser.get_signature());
		tv_number.setText(loginUser.get_username());

        AssetManager mgr = this.getAssets();
        Bitmap bmp = PictureUtil.getHeadPic(mgr, loginUser);
        if (bmp != null) {
            iv_head.setImageBitmap(bmp);
        }

	}

	@Override
	protected void setData() {
		rl_head.setOnClickListener(this);
		rl_name.setOnClickListener(this);
		rl_number.setOnClickListener(this);
		rl_sign.setOnClickListener(this);
	}

	/*
	 * 从相册获取
	 */
	public void gallery() {
		// 激活系统图库，选择一张图片
		Intent intent = new Intent(Intent.ACTION_PICK);
		intent.setType("image/*");
		startActivityForResult(intent, PHOTO_REQUEST_GALLERY);
	}

	/*
	 * 照相
	 */
	public void camera() {
		Intent intent = new Intent("android.media.action.IMAGE_CAPTURE");
		// 判断存储卡是否可以用，可用进行存储
		if (hasSdcard()) {
			intent.putExtra(MediaStore.EXTRA_OUTPUT,
					Uri.fromFile(new File(Environment
							.getExternalStorageDirectory(), PHOTO_FILE_NAME)));
		}
		startActivityForResult(intent, PHOTO_REQUEST_CAMERA);
	}

	@Override
	protected void onActivityResult(int requestCode, int resultCode, Intent data) {
		if (requestCode == PHOTO_REQUEST_GALLERY) {
			if (data != null) {
				// 得到图片的全路径
				Uri uri = data.getData();
				crop(uri);
			}

		} else if (requestCode == PHOTO_REQUEST_CAMERA) {
			if (hasSdcard()) {
				tempFile = new File(Environment.getExternalStorageDirectory(),
						PHOTO_FILE_NAME);
				crop(Uri.fromFile(tempFile));
			} else {
				Toast.makeText(MyPersonInfoActivity.this, "未找到存储卡，无法存储照片！", 0).show();
			}

		} else if (requestCode == PHOTO_REQUEST_CUT) {
			if (data != null) {
				bitmap = data.getParcelableExtra("data");
				this.iv_head.setImageBitmap(bitmap);
				//

				if (application.getMemberEntity() != null) {
					name = application.getMemberEntity().getStrAccountNo()
							+ System.currentTimeMillis() + ".jpg";
				} else {
					name = System.currentTimeMillis() + ".jpg";
				}

				final File file = saveMyBitmap(bitmap, name);

				String strChecksum = Md5Util.getMD5(file);
				int uDownsize = 0;
				long uFilesize = file.length();

				//contwo.sendFileUpInfo(name, strChecksum, uDownsize, uFilesize);
			}
		}

		super.onActivityResult(requestCode, resultCode, data);
	}

	public File saveMyBitmap(Bitmap mBitmap, String bitName) {
		File f = new File("/sdcard/flamingo/" + bitName);
		if (f.exists()) {
			f.delete();
		}
		FileOutputStream fOut = null;
		try {
			fOut = new FileOutputStream(f);
		} catch (FileNotFoundException e) {
			e.printStackTrace();
		}

		ByteArrayOutputStream out = new ByteArrayOutputStream();
		mBitmap.compress(Bitmap.CompressFormat.JPEG, 100, out);
		int options = 100;
		while (out.toByteArray().length / 1024 > 200 && options >= 10) {
			options -= 10;
			Log.i("save bitmap " + bitName,
					String.valueOf(out.toByteArray().length));
			out.reset();
			mBitmap.compress(Bitmap.CompressFormat.JPEG, options, out);
		}

		try {
			fOut.write(out.toByteArray());
			fOut.flush();
		} catch (IOException e) {
			e.printStackTrace();
		}
		try {
			fOut.close();
		} catch (IOException e) {
			e.printStackTrace();
		}
		return f;
	}

	/**
	 * 剪切图片
	 * 
	 * @function:
	 * @author:Jerry
	 * @param uri
	 */
	private void crop(Uri uri) {
		// 裁剪图片意图
		Intent intent = new Intent("org.android.getImageFromCamera.action.CROP");
		intent.setDataAndType(uri, "image/*");
		intent.putExtra("crop", "true");
		// 裁剪框的比例，1：1
		intent.putExtra("aspectX", 1);
		intent.putExtra("aspectY", 1);
		// 裁剪后输出图片的尺寸大小
		intent.putExtra("outputX", 250);
		intent.putExtra("outputY", 250);
		// 图片格式
		intent.putExtra("outputFormat", "JPEG");
		intent.putExtra("noFaceDetection", true);// 取消人脸识别
		intent.putExtra("return-data", true);// true:不返回uri，false：返回uri
		startActivityForResult(intent, PHOTO_REQUEST_CUT);
	}

	private boolean hasSdcard() {
		if (Environment.getExternalStorageState().equals(
				Environment.MEDIA_MOUNTED)) {
			return true;
		} else {
			return false;
		}
	}

	// 自定义相册对话框
	@SuppressLint("NewApi")
	private void showPhotoDialog() {
		View view = getLayoutInflater()
				.inflate(R.layout.uplodeimg_dialog, null);
		photodialog = new Dialog(this, R.style.ActionSheetDialogAnimation);
		photodialog.setContentView(view, new LayoutParams(
				LayoutParams.FILL_PARENT, LayoutParams.WRAP_CONTENT));
		Window window = photodialog.getWindow();
		// 设置显示动画
		window.setWindowAnimations(R.style.popwin_down_up);
		WindowManager.LayoutParams wl = window.getAttributes();
		wl.x = 0;
		wl.y = getWindowManager().getDefaultDisplay().getHeight();
		// 以下这两句是为了保证按钮可以水平满屏
		wl.width = ViewGroup.LayoutParams.MATCH_PARENT;
		wl.height = ViewGroup.LayoutParams.WRAP_CONTENT;
		// 设置显示位置
		photodialog.onWindowAttributesChanged(wl);
		// 设置点击外围解散
		photodialog.setCanceledOnTouchOutside(true);
		photodialog.show();
		View btn_pictures = view.findViewById(R.id.btn_pictures);
		btn_pictures.setOnClickListener(this);
		View btn_photo = view.findViewById(R.id.btn_photo);
		btn_photo.setOnClickListener(this);
		View btn_unselect = view.findViewById(R.id.btn_unselect);
		btn_unselect.setOnClickListener(this);
	}

	@Override
	public void processMessage(Message msg) {
		super.processMessage(msg);
	//	if (msg.what == MegAsnType.Topicturemsg) {
//			FileLoadInfo fileLoad = (FileLoadInfo) msg.obj;
//			String name = fileLoad.getStrName().toStringUtf8();
//			int uError = 0;
//			int uOffset = fileLoad.getUOffset();
//			int uDownsize = fileLoad.getUDownsize();
//			PictureUtil.topictrues("h/" + name, uError, uOffset, uDownsize);

	//	}
		//else if (msg.what == MegAsnType.FileUpData) {
			// 上传头像成功
//			FileUpInfo fileUpInfo = (FileUpInfo) msg.obj;
//			strUrl = fileUpInfo.getStrUrl();
//			String nickname = application.getMemberEntity().getNickname();
//			String sign = application.getMemberEntity().getStrSigature();
//			String uBirthday = application.getMemberEntity().getuBirthday();
//			int naface = application.getMemberEntity().getnFace();
//			tms.User.UserPrivateInfo.Builder uUserPrivateInfo = tms.User.UserPrivateInfo
//					.newBuilder();
//			uUserPrivateInfo.setStrCustomFace(com.google.protobuf.ByteString
//					.copyFromUtf8(strUrl));
//			uUserPrivateInfo.setStrSigature(com.google.protobuf.ByteString
//					.copyFromUtf8(sign));
//			uUserPrivateInfo.setUBirthday(Integer.valueOf(uBirthday));
//			tms.User.UpdateUserInfo.Builder uUpdateUserInfo = tms.User.UpdateUserInfo
//					.newBuilder();
//			uUpdateUserInfo.setPrivateInfos(uUserPrivateInfo);
//
//			uUpdateUserInfo.setNFace(naface);
//			uUpdateUserInfo.setStrNickName(com.google.protobuf.ByteString
//					.copyFromUtf8(nickname));
//			con.send(tms.Base.cmd.n_UpdateUserInfo_VALUE,
//					uUpdateUserInfo.build());

		//} else if (msg.what == MegAsnType.UpdateUserInfo) {
//			if (application.getMemberEntity() != null) {
//				memberEntity = application.getMemberEntity();
//				memberEntity.setHeadpath(name);
//				//application.setMemberEntity(memberEntity);
//				try {
//					BaseActivity.getDb().update(memberEntity);
//				} catch (DbException e) {
//					e.printStackTrace();
//				}
//			}
//			makeTextLong("修改成功");
		//}

	}
	
	
	@Override
	public void onBackPressed() {
		super.onBackPressed();
		finish();
		startActivity(MainActivity.class);
	}
}
