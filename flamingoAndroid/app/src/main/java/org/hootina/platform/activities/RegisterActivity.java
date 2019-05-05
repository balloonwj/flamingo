package org.hootina.platform.activities;


import android.content.Intent;
import android.os.Message;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.Toast;

import org.hootina.platform.R;
import org.hootina.platform.enums.MsgType;
import org.hootina.platform.net.NetWorker;
import org.hootina.platform.utils.NetUtils;

public class RegisterActivity extends BaseActivity {
	private EditText et_mobile, et_name, et_password, et_surepwd;
	private Button makesure_register;
	private String mobile, username, password;
	private String szAccountNo, szNickName, szPasswd, surePasswd;

	@Override
	protected int getContentView() {
		return R.layout.activity_register;
	}

	@Override
	protected void initData() {
//		// 获取数据库操作对象
//		try {
//			userlist = BaseActivity.getDb().findAll(UserInfo.class);
//		} catch (DbException e) {
//			e.printStackTrace();
//		}
	}

	@Override
	protected void setData() {
		makesure_register.setOnClickListener(this);
	}

	@Override
	public void onClick(View v) {
		szAccountNo = et_mobile.getText().toString().trim();
		szNickName = et_name.getText().toString().trim();
		szPasswd = et_password.getText().toString().trim();
		surePasswd = et_surepwd.getText().toString().trim();
		switch (v.getId()) {
			case R.id.btn_back:
				setResult(BaseActivity.REGISTER_RESULT_CANCEL);
				finish();
				break;

			case R.id.makesure_register:
				if (!NetUtils.isConnected(this))
				{
					Toast.makeText(this, R.string.net_not_available, Toast.LENGTH_SHORT).show();
					return;
				}

				// 注册
				if (szAccountNo.trim().length() <= 0) {
					Toast.makeText(this, "请输入账户名！", Toast.LENGTH_SHORT).show();
					return;
				}
				if (szNickName.trim().length() < 0) {
					Toast.makeText(this, "请输入用户名！", Toast.LENGTH_SHORT).show();
					return;
				}

				if (szPasswd.length() < 3 || szPasswd.length() > 20) {
					Toast.makeText(this, "密码请输入6-16位字符！！", Toast.LENGTH_SHORT)
							.show();
					return;
				}
				if (surePasswd.length() < 3 || surePasswd.length() > 20) {
					Toast.makeText(this, "两次密码输入不一致！！", Toast.LENGTH_SHORT).show();
					return;
				}

				Button btnRegister = (Button)findViewById(R.id.makesure_register);
				btnRegister.setEnabled(false);
				NetWorker.registerUser(szAccountNo, szPasswd, szNickName);
				break;

			default:
				break;
		}
	}

	@Override
	public void processMessage(Message msg) {
			super.processMessage(msg);
		 if (msg.what == MsgType.msg_type_register) {
			if (msg.arg1 == MsgType.ERROR_CODE_SUCCESS) {
				Toast.makeText(this, "注册成功", Toast.LENGTH_SHORT).show();
				//将用户名和密码传给登录界面
				Intent intent = new Intent();
				intent.putExtra("register_username", szAccountNo);
				intent.putExtra("register_password", szPasswd);
				setResult(BaseActivity.REGISTER_RESULT_OK, intent);
				//startActivity(intent);
				finish();
			} else if (msg.arg1 == MsgType.ERROR_CODE_REGISTERED) {
				Toast.makeText(this, "该用户已被注册", Toast.LENGTH_SHORT).show();

			} else  {
				Toast.makeText(this, "注册失败", Toast.LENGTH_SHORT).show();
			}

			Button btnRegister = (Button)findViewById(R.id.makesure_register);
			btnRegister.setEnabled(true);
		}

	} //end processMessage
}// end class
