package org.hootina.platform.fragments;

import java.lang.reflect.Field;

import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Message;
import android.support.v4.app.Fragment;
import android.view.LayoutInflater;
import android.view.View;
import android.view.View.OnClickListener;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.TextView;

import org.hootina.platform.R;
import org.hootina.platform.FlamingoApplication;
import org.hootina.platform.activities.BaseActivity;
import org.hootina.platform.utils.SharedPreferencesUtils;


/**
 * @author Administrator 所有fragment类的父类
 */
public abstract class BaseFragment extends Fragment implements OnClickListener {
	protected static FlamingoApplication application;
	//protected static DbUtils db;

	@Override
	public View onCreateView(LayoutInflater inflater, ViewGroup container,
			Bundle savedInstanceState) {
		application = (FlamingoApplication) getActivity().getApplication();
		View view = inflater.inflate(getContentView(), container, false);
		autoInitAllWidgets(view);
		initData(view);
		setData();
		processLogic();
//		if (db == null) {
//			db = DbUtils.create(application);
//		}
		boolean isFirstUse = (Boolean) SharedPreferencesUtils.get(
				getActivity(), "isFirstUse", true);

		if (application.getMemberEntity() == null && isFirstUse == false) {
			String strAccountNo2 = (String) SharedPreferencesUtils.get(
					getActivity(), "strAccountNo2", "");

			String password = (String) SharedPreferencesUtils.get(
					getActivity(), "password", "");
			//con.login(strAccountNo2, password, tms.User.LoginInfo.type.n_AccountNO2);
		}

//		if (con.isConnected()) {
//		} else {
//			if (application.getMemberEntity() != null) {
//				con.stopWork();
//				String mobilenumber = application.getMemberEntity()
//						.getStrAccountNo2();
//				String password = application.getMemberEntity().getPassword();
//				//con.login(mobilenumber, password, tms.User.LoginInfo.type.n_AccountNO2);
//			}
//		}
		return view;
	}

//	public static DbUtils getDb() {
//		return db;
//	}
//
//	public static void setDb(DbUtils db) {
//		BaseFragment.db = db;
//	}

	/**
	 * 获取布局文件ID
	 *
	 * @return
	 */
	protected abstract int getContentView();

	/**
	 * 初始化控件,view在这里初始化
	 */
	// protected abstract void initView(LayoutInflater inflater,
	// ViewGroup container, Bundle savedInstanceState);

	/**
	 * 初始化数据
	 */
	protected abstract void initData(View view);

	/**
	 * 初始化数据
	 */
	protected abstract void setData();

	/**
	 * 处理逻辑
	 */
	protected abstract void processLogic();

	/**
	 * 调用findViewById,避免强制类型转换
	 *
	 * @param id
	 * @return
	 */
	// protected <T extends View> T getView(int id) {
	// return (T) view.findViewById(id);
	// }

	/**
	 * 自动初始化所有控件
	 */
	protected void autoInitAllWidgets(View view) {

		Field[] fields = getClass().getDeclaredFields();

		for (Field field : fields) {

			// 反射访问私有成员，必须加上这句
			field.setAccessible(true);

			try {
				int id = R.id.class.getField(field.getName())
						.getInt(new R.id());
				// 然后对这个属性赋值
				field.set(this, view.findViewById(id));

			} catch (Exception e) {
			}

		}
	}

	public void processMessage(Message msg) {
	}

	public static void sendMessage(int cmd, String text) {
		Message msg = new Message();
		msg.what = cmd;
		msg.obj = text;
		sendMessage(msg);
	}

	public static void sendMessage(Message msg) {
		handler.sendMessage(msg);
	}

	public static void sendEmptyMessage(int what) {
		handler.sendEmptyMessage(what);
	}

	// Handler对象是静态的，则所有TuliaoBaseActivity的子类都是共用同一个消息队列
	private static Handler handler = new Handler() {

		@Override
		public void handleMessage(Message msg) {
			// ((BaseFragment)
			// application.getAppManager().currentActivity()).processMessage(msg);
			BaseActivity acitivity = ((BaseActivity) application
					.getAppManager().currentActivity());
			if (acitivity != null) {
				acitivity.processMessage(msg);
			}
		}
	};

	// ----------------------------------------------
	// --------------------跳转函数--------------------------
	// ----------------------------------------------

	protected void startActivity(Class<?> cls) {
		Intent intent = new Intent(getActivity(), cls);
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
//	protected void displayImage(ImageView iv, String imageUrl,
//			float screenWidthScale, float heightWidthScale,
//			DisplayImageOptions options) {
//		if (imageUrl == null) {
//			return;
//		}
//
//		ViewGroup.LayoutParams params = iv.getLayoutParams();
//		params.width = (int) (screenWidthScale * application.getScreenWidth());
//		params.height = (int) (params.width * heightWidthScale);
//		iv.setLayoutParams(params);
//
//		ImageLoader.getInstance().displayImage(imageUrl, iv, options);
//	}

}
