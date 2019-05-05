package org.hootina.platform.activities.details;

import android.os.Message;
import android.view.View;
import android.widget.TextView;

import org.hootina.platform.R;
import org.hootina.platform.activities.BaseActivity;
import org.hootina.platform.result.MemberEntity;
import org.hootina.platform.userinfo.UserSession;

public class ModifySignatureActivity extends BaseActivity {
	/*
	 * @see 用户个性签名
	 */
	private TextView 		save;
	private TextView 		et_sign;
	private String 			signature;
	private MemberEntity 	memberEntity;

	@Override
	public void onClick(View v) {
		signature = et_sign.getText().toString();
		switch (v.getId()) {
		case R.id.btn_back:
			finish();
			break;

		case R.id.save:
			if (signature.equals(UserSession.getInstance().loginUser.get_signature())) {
				finish();
				return;
			}

			//TODO: 网络通信修改用户签名
//			if (application.getMemberEntity() != null) {
//				if (application.getMemberEntity().getStrSigature().equals(sign)) {
//					finish();
//				} else {
//
//					String nickname = application.getMemberEntity()
//							.getNickname();
//					String uBirthday = application.getMemberEntity()
//							.getuBirthday();
//					String headurl = application.getMemberEntity()
//							.getHeadpath();
//					int naface = application.getMemberEntity().getnFace();
//					tms.User.UserPrivateInfo.Builder uUserPrivateInfo = tms.User.UserPrivateInfo
//							.newBuilder();
//					uUserPrivateInfo
//							.setStrCustomFace(com.google.protobuf.ByteString
//									.copyFromUtf8(headurl));
//					uUserPrivateInfo
//							.setStrSigature(com.google.protobuf.ByteString
//									.copyFromUtf8(sign));
//					uUserPrivateInfo.setUBirthday(Integer.valueOf(uBirthday));
//					tms.User.UpdateUserInfo.Builder uUpdateUserInfo = tms.User.UpdateUserInfo
//							.newBuilder();
//					uUpdateUserInfo.setPrivateInfos(uUserPrivateInfo);
//
//					uUpdateUserInfo.setNFace(naface);
//					uUpdateUserInfo
//							.setmNickName(com.google.protobuf.ByteString
//									.copyFromUtf8(nickname));
//					con.send(tms.Base.cmd.n_UpdateUserInfo_VALUE,
//							uUpdateUserInfo.build());
//				}
//			}
			break;

		default:
			break;
		}
	}

	@Override
	protected int getContentView() {
		return R.layout.activity_sign;
	}

	@Override
	protected void initData() {
		save.setOnClickListener(this);

	}

	@Override
	protected void setData() {
		signature = getIntent().getStringExtra("signature");
		et_sign.setText(signature);
	}

	@Override
	public void processMessage(Message msg) {
		super.processMessage(msg);
//		if (msg.what == MegAsnType.UpdateUserInfo) {
//			if (application.getMemberEntity() != null) {
//				memberEntity = application.getMemberEntity();
//				memberEntity.setStrSigature(signature);
//				//application.setMemberEntity(memberEntity);
//				try {
//					BaseActivity.getDb().update(memberEntity);
//				} catch (DbException e) {
//					e.printStackTrace();
//				}
//
//
//
//			}
//			// startActivity(MyPersonInfoActivity.class);
//			finish();
//		}
	}
}
