package org.hootina.platform.activities;

import org.hootina.platform.R;
import org.hootina.platform.net.NetWorker;

import android.content.SharedPreferences;
import android.view.View;
import android.widget.EditText;
import android.widget.Toast;

public class ChangeServerActivity extends BaseActivity {
	private static String  SHARED_PREFERENCE_KEY = "sharedPreferenceKey";
	private static String  SHARED_PREFERENCE_CHATSERVERIP = "sharedPreferenceChatServerIp";
	private static String  SHARED_PREFERENCE_CHATSERVERPORT = "sharedPreferenceChatServerPort";
	private static String  SHARED_PREFERENCE_IMGSERVERIP = "sharedPreferenceImgServerIp";
	private static String  SHARED_PREFERENCE_IMGSERVERPORT = "sharedPreferenceImgServerPort";
	private static String  SHARED_PREFERENCE_FILESERVERIP = "sharedPreferenceFileServerIp";
	private static String  SHARED_PREFERENCE_FILESERVERPORT = "sharedPreferenceFileServerPort";

	private String mChatServerIp;
	private String mChatServerPort;
	private String mImgServerIp;
	private String mImgServerPort;
	private String mFileServerIp;
	private String mFileServerPort;

	@Override
	public void onClick(View v) {
		switch (v.getId()) {
		case R.id.btn_back:
		case R.id.btn_cancel:
			finish();
			break;

		case R.id.btn_ok:
			if (saveServerInfoToSharedPreferences())
				finish();
			else
				Toast.makeText(this, "无效的端口号设置！", Toast.LENGTH_SHORT).show();
			break;

		default:
			break;
		}

	}

	@Override
	protected int getContentView() {
		return R.layout.activity_changesever;
	}

	@Override
	protected void initData() {
		getServerInfoFromSharedPreferences();
	}

	@Override
	protected void setData() {

	}

	private boolean saveServerInfoToSharedPreferences() {
		mChatServerIp = ((EditText)findViewById(R.id.chat_server_ip)).getText().toString().trim();
		mChatServerPort =((EditText)findViewById(R.id.chat_server_port)).getText().toString().trim();
		mImgServerIp = ((EditText)findViewById(R.id.img_server_ip)).getText().toString().trim();
		mImgServerPort =((EditText)findViewById(R.id.img_server_port)).getText().toString().trim();
		mFileServerIp = ((EditText)findViewById(R.id.file_server_ip)).getText().toString().trim();
		mFileServerPort =((EditText)findViewById(R.id.file_server_port)).getText().toString().trim();

		if (!applySettings())
			return false;

		SharedPreferences.Editor editor = getSharedPreferences(SHARED_PREFERENCE_KEY, MODE_PRIVATE).edit();
		editor.putString(SHARED_PREFERENCE_CHATSERVERIP, mChatServerIp);
		editor.putString(SHARED_PREFERENCE_CHATSERVERPORT, mChatServerPort);
		editor.putString(SHARED_PREFERENCE_IMGSERVERIP, mImgServerIp);
		editor.putString(SHARED_PREFERENCE_IMGSERVERIP, mImgServerPort);
		editor.putString(SHARED_PREFERENCE_FILESERVERIP, mFileServerIp);
		editor.putString(SHARED_PREFERENCE_FILESERVERIP, mFileServerPort);
		editor.commit();

		return true;
	}

	private void getServerInfoFromSharedPreferences() {
		SharedPreferences pref = getSharedPreferences(SHARED_PREFERENCE_KEY, MODE_PRIVATE);
		mChatServerIp = pref.getString(SHARED_PREFERENCE_CHATSERVERIP, "");
		mChatServerPort = pref.getString(SHARED_PREFERENCE_CHATSERVERPORT, "");

		mImgServerIp = pref.getString(SHARED_PREFERENCE_IMGSERVERIP, "");
		mImgServerPort = pref.getString(SHARED_PREFERENCE_IMGSERVERPORT, "");

		mFileServerIp = pref.getString(SHARED_PREFERENCE_FILESERVERIP, "");
		mFileServerPort = pref.getString(SHARED_PREFERENCE_FILESERVERPORT, "");

		if (mChatServerIp.isEmpty() || mChatServerPort.isEmpty() || mImgServerIp.isEmpty() ||
				mImgServerPort.isEmpty() || mFileServerIp.isEmpty() || mFileServerPort.isEmpty()){
			mChatServerIp = NetWorker.getChatServerIp();
			mChatServerPort = String.valueOf(NetWorker.getChatPort());

			mImgServerIp = NetWorker.getImgServerIp();
			mImgServerPort = String.valueOf(NetWorker.getImgPort());

			mFileServerIp = NetWorker.getFileServerIp();
			mFileServerPort = String.valueOf(NetWorker.getFilePort());
		}

		((EditText)findViewById(R.id.chat_server_ip)).setText(mChatServerIp);
		((EditText)findViewById(R.id.chat_server_port)).setText(mChatServerPort);

		((EditText)findViewById(R.id.img_server_ip)).setText(mImgServerIp);
		((EditText)findViewById(R.id.img_server_port)).setText(mImgServerPort);

		((EditText)findViewById(R.id.file_server_ip)).setText(mFileServerIp);
		((EditText)findViewById(R.id.file_server_port)).setText(mFileServerPort);
	}

	private boolean applySettings(){
		try {
			NetWorker.setChatServerIp(mChatServerIp);
			NetWorker.setChatPort((short)Integer.parseInt(mChatServerPort));

			NetWorker.setImgServerIp(mImgServerIp);
			NetWorker.setImgPort((short)Integer.parseInt(mImgServerPort));

			NetWorker.setFileServerIp(mFileServerIp);
			NetWorker.setFilePort((short)Integer.parseInt(mFileServerPort));
		} catch (NumberFormatException e) {
			//e.printStackTrace();
			return false;
		}

		return true;
	}

}
