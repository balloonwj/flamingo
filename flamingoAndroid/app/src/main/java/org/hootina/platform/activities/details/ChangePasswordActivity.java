package org.hootina.platform.activities.details;

import android.os.Message;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.Toast;

import org.hootina.platform.R;
import org.hootina.platform.activities.BaseActivity;

public class ChangePasswordActivity extends BaseActivity {
	/*
	 * (non-Javadoc)
	 * 
	 * @see 修改密码
	 */
	private Button btnsure;
	private EditText et_oldpass, et_newpass, et_surepass;
	private String oldpass, newpass, surepass;

	@Override
	public void onClick(View v) {
		oldpass = et_oldpass.getText().toString().trim();
		newpass = et_newpass.getText().toString().trim();
		surepass = et_surepass.getText().toString().trim();
		switch (v.getId()) {
		case R.id.btn_back:
			finish();
			break;
		case R.id.btnsure:
			save();
			break;
		default:
			break;
		}

	}

	@Override
	protected int getContentView() {
		return R.layout.activity_changepass;
	}

	@Override
	protected void initData() {

	}

	@Override
	protected void setData() {
		btnsure.setOnClickListener(this);

	}

	private void save() {
		if (oldpass.length() <= 0) {
			Toast.makeText(this, "请输入旧密码！", Toast.LENGTH_SHORT).show();
			return;
		}
		if (newpass.length() <= 0) {
			Toast.makeText(this, "请输入新密码！", Toast.LENGTH_SHORT).show();
			return;
		}
		if (surepass.length() <= 0) {
			Toast.makeText(this, "请确认 新密码！", Toast.LENGTH_SHORT).show();
			return;
		}
		if (!newpass.equals(surepass)) {
			Toast.makeText(this, "您输入的密码不一致", Toast.LENGTH_SHORT).show();
			return;
		}
		//tms.User.ChangePasswd.Builder changePasswd = tms.User.ChangePasswd.newBuilder();
		//changePasswd.setStrOldPasswd(oldpass);
		//changePasswd.setStrNewPasswd(newpass);
		//con.send(tms.Base.cmd.n_ChangePasswd_VALUE, changePasswd.build());
	}

	@Override
	public void processMessage(Message msg) {
		super.processMessage(msg);
		//if(msg.what==MegAsnType.ChagePwd){
			//ChangePasswd changepwd=(ChangePasswd) msg.obj;
			//if(changepwd.getUError()==0){
			//	ToastUtils.showLongToast(this, "密码修改成功");
			//	finish();
			//}else {
			//	ToastUtils.showShortToast(this, "密码修改失败");
			//}
		//}
	}

	@Override
	public void onBackPressed() {
		super.onBackPressed();
		finish();
	}
}
