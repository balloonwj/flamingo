package org.hootina.platform.activities;

import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Message;
import android.support.v4.app.FragmentActivity;
import android.view.View.OnClickListener;
import android.view.ViewGroup;
import android.view.Window;
import android.widget.ImageView;
import android.widget.TextView;
import android.widget.Toast;

import org.hootina.platform.FlamingoApplication;
import org.hootina.platform.R;
import org.hootina.platform.db.ChatMsgDb;
import org.hootina.platform.enums.MsgType;
import org.hootina.platform.result.MemberEntity;

import com.nostra13.universalimageloader.core.DisplayImageOptions;
import com.nostra13.universalimageloader.core.ImageLoader;

import java.lang.reflect.Field;

//import org.json.JSONObject;

/**
 * 所有activity类的父类
 */
public abstract class BaseActivity extends FragmentActivity implements OnClickListener {
    //注册Activity返回的结果
	public static final int REGISTER_RESULT		   = 0;
	public static final int REGISTER_RESULT_CANCEL = 1;
	public static final int REGISTER_RESULT_OK 	   = 2;

    protected static FlamingoApplication 	application;
	private   static ChatMsgDb				msgDb;

	public static boolean 					bLogin = false;

	@Override
	protected void onCreate(Bundle bundle) {
		super.onCreate(bundle);

		// 设置隐藏标题栏
		requestWindowFeature(Window.FEATURE_NO_TITLE);

		// 初始化application
		application = (FlamingoApplication) getApplication();
		// 添加Activity到堆栈
		application.getAppManager().addActivity(this);

		msgDb = application.getChatMsgDb();
		//application.getMemberEntity();

		setContentView(getContentView());
		autoInitAllWidgets();

		initData();
		setData();
	}
	
	@Override
	protected void onRestoreInstanceState(Bundle savedInstanceState) {
		super.onRestoreInstanceState(savedInstanceState);
		application = (FlamingoApplication) getApplication();
		// 添加Activity到堆栈
		application.getAppManager().addActivity(this);
		application.getMemberEntity();
	}

	public static ChatMsgDb getChatMsgDb(){
        return msgDb;
    }

	@Override
	protected void onDestroy() {
		super.onDestroy();
		// 结束Activity&从堆栈中移除
		application.getAppManager().finishActivity(this);
	}

	public void processMessage(Message msg) {
	    if (msg.what == MsgType.msg_type_kickuser) {
	    	Intent intent = new Intent(BaseActivity.this, LoginActivity.class);
	    	startActivity(intent);
	    	Toast.makeText(this,"当前账户在其他地点登录\r\n如果这不是您本人行为，建议修改密码", Toast.LENGTH_LONG).show();
		}
	}

	public static void sendMessage(int cmd, String text) {
		Message msg = new Message();
		msg.what = cmd;
		msg.obj = text;
		sendMessage(msg);
	}

	public static void sendMessage(Message msg) {
		handler.sendMessage(msg);
//		if (msg.what == MegAsnType.FromTalkingmsg) {
//		}
	}

	public static void sendEmptyMessage(int what) {
		handler.sendEmptyMessage(what);
	}

	// Handler对象是静态的，则所有继承BaseActivity的子类都是共用同一个消息队列
	private static Handler handler = new Handler() {

		@Override
		public void handleMessage(Message msg) {
			//ProcessNetMsg(msg);

			BaseActivity acitivity = ((BaseActivity) application.getAppManager().currentActivity());
			if (acitivity != null) {
				if (msg.what == MsgType.msg_networker_disconnect) {
					//Toast.makeText(acitivity, "网络已经断开", Toast.LENGTH_SHORT).show();
				}
				else
					acitivity.processMessage(msg);
			}
		}
	};

	/**
	 * 获取布局文件ID
	 *
	 * @return
	 */
	protected abstract int getContentView();

	/**
	 * 初始化控件
	 */
	// protected abstract void initView();

	/**
	 * 初始化数据
	 */
	protected abstract void initData();

	/**
	 * 设置
	 */
	protected abstract void setData();

	/**
	 * 调用findViewById,避免强制类型转换
	 *
	 * @param id
	 * @return
	 */
	// protected <T extends View> T getView(int id) {
	// return (T) findViewById(id);
	// }

	/**
	 * 自动初始化所有控件
	 */
	private void autoInitAllWidgets() {
		// 得到该Activity的所有字段
		Field[] fields = this.getClass().getDeclaredFields();

		for (Field field : fields) {

			// 反射访问私有成员，必须加上这句
			field.setAccessible(true);

			try {
				int id = R.id.class.getField(field.getName())
						.getInt(new R.id());
				// 然后对这个属性赋值
				field.set(this, this.findViewById(id));

			} catch (Exception e) {
			}

		}
	}

	// ----------------------------------------------
	// --------------------跳转函数--------------------------
	// ----------------------------------------------

	protected void startActivity(Class<?> cls) {
		Intent intent = new Intent(this, cls);
		startActivity(intent);
	}

	// ----------------------------------------------
	// --------------------基本控件设置--------------------------
	// ----------------------------------------------

	/**
	 * 设置文字
	 *
	 * @param tv
	 *            TextView
	 * @param str
	 *            要设置的字符串
	 */
	protected void setText(TextView tv, String str) {
		if (str == null || tv == null) {
			return;
		}
		tv.setText(str);
	}

	/**
	 * 显示网络图片, 可重新配置options
	 */
	protected void displayImage(ImageView iv, String imageUrl,
			float screenWidthScale, float heightWidthScale,
			DisplayImageOptions options) {
		if (imageUrl == null) {
			return;
		}

		ViewGroup.LayoutParams params = iv.getLayoutParams();
		params.width = (int) (screenWidthScale * application.getScreenWidth());
		params.height = (int) (params.width * heightWidthScale);
		iv.setLayoutParams(params);

		ImageLoader.getInstance().displayImage(imageUrl, iv, options);
	}

	public static int getMyAccountID() {
		if (application != null) {
			return application.getMemberEntity().getuAccountID();
		} else {
			return 0;
		}
	}

	public static MemberEntity getMyMemberEntity() {
		if (application != null) {
			return application.getMemberEntity();
		} else {
			return null;
		}
	}
}
