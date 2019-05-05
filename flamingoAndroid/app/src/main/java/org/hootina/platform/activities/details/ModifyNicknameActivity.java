package org.hootina.platform.activities.details;

import android.os.Message;
import android.view.View;
import android.widget.EditText;
import android.widget.TextView;

import org.hootina.platform.R;
import org.hootina.platform.activities.BaseActivity;
import org.hootina.platform.result.MemberEntity;
import org.hootina.platform.userinfo.UserSession;

public class ModifyNicknameActivity extends BaseActivity {
	/*
	 * @see 个人信息，昵称
	 */
	private TextView 		save;
	private EditText 		et_nickname;
	private String 			nickname;
	private String 			sign;
	private String 			uBirthday;
	private String 			headurl;
	private MemberEntity 	memberEntity;

	@Override
	public void onClick(View v) {
		nickname = et_nickname.getText().toString();
		switch (v.getId()) {
		case R.id.btn_back:
			finish();
			break;

		case R.id.save:
			if (nickname.equals(UserSession.getInstance().loginUser.get_nickname())) {
				finish();
				return;
			}

			//TODO: 网络通信，修改用户信息
//			if (application.getMemberEntity() != null) {
//				if (application.getMemberEntity().getNickname()
//						.equals(nickname)) {
//					finish();
//				} else {
//					sign = application.getMemberEntity().getStrSigature();
//					uBirthday = application.getMemberEntity().getuBirthday();
//					headurl = application.getMemberEntity().getHeadpath();
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
		return R.layout.activity_nickname;
	}

	@Override
	protected void initData() {
		save.setOnClickListener(this);
	}

	@Override
	protected void setData() {
		nickname = getIntent().getStringExtra("nickname");
		et_nickname.setText(nickname);
	}

	@Override
	public void processMessage(Message msg) {
		super.processMessage(msg);
//		if (msg.what == MegAsnType.UpdateUserInfo) {
//			if (application.getMemberEntity() != null) {
//				memberEntity = application.getMemberEntity();
//				memberEntity.setNickname(nickname);
//				//application.setMemberEntity(memberEntity);
//
//				try {
//					BaseActivity.getDb().update(memberEntity);
//				} catch (DbException e) {
//					e.printStackTrace();
//				}
//			}
//			finish();
//		}
	}
}
