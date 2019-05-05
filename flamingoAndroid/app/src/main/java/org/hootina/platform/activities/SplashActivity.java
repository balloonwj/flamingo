package org.hootina.platform.activities;

import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.os.Bundle;
import android.support.v4.view.ViewPager;
import android.text.SpannableString;
import android.text.TextUtils;
import android.util.AttributeSet;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.animation.AlphaAnimation;
import android.view.animation.Animation;
import android.view.animation.Animation.AnimationListener;
import android.view.inputmethod.InputMethodManager;
import android.widget.AdapterView;
import android.widget.Button;
import android.widget.EditText;
import android.widget.GridView;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.RelativeLayout;

import org.hootina.platform.R;
import org.hootina.platform.activities.member.ChattingActivity;
import org.hootina.platform.adapters.SearchFriendAdapter;
import org.hootina.platform.utils.ScreenUtils;
import org.hootina.platform.utils.SharedPreferencesUtils;
import org.hootina.platform.utils.UIUtils;

import com.lidroid.xutils.DbUtils;

import java.util.ArrayList;
import java.util.List;

/**
 * @author 欢迎页
 *
 */
public class SplashActivity extends BaseActivity {

	private RelativeLayout rl_splash;
	private ImageView iv_up;

	private final String IS_FIRST_USE_KEY = "isFirstUse";

	private boolean isAnimFinish = false;

	private DbUtils db;

	@Override
	protected void onCreate(Bundle arg0) {
		super.onCreate(arg0);

		jump();

		// 渐变动画
		AlphaAnimation alphaAnimation = new AlphaAnimation(0.0f, 1.0f);

		alphaAnimation.setAnimationListener(new AnimationListener() {

			@Override
			public void onAnimationStart(Animation animation) {

			}

			@Override
			public void onAnimationRepeat(Animation animation) {

			}

			@Override
			public void onAnimationEnd(Animation animation) {
				isAnimFinish = true;

				jump();
			}
		});
		alphaAnimation.setDuration(3000);
		rl_splash.startAnimation(alphaAnimation);
	}

	@Override
	protected int getContentView() {
		return R.layout.activity_splash;
	}

	@Override
	public void onDestroy() {
		super.onDestroy();

	}

	@Override
	protected void initData() {
		db = DbUtils.create(this);
		// 初始化屏幕宽高，在后面用
		if (application != null) {
			application.setScreenHeight(ScreenUtils.getScreenHeight(this));
			application.setScreenWidth(ScreenUtils.getScreenWidth(this));

//			try {
//				// 获取保存的登录用户信息
//				//application.setMemberEntity(db.findFirst(MemberEntity.class));
//			} catch (DbException e) {
//				e.printStackTrace();
//			}
		}
	}

	@Override
	protected void setData() {

		iv_up.setImageResource(R.drawable.splash_down);

	}

	/**
	 * 跳转
	 */
	private void jump() {
		if (isAnimFinish) {
			boolean isFirstUse = (Boolean) SharedPreferencesUtils.get(
					SplashActivity.this, IS_FIRST_USE_KEY, true);
			Intent intent = new Intent();
			// 如果是第一次使用跳转到引导页，如果不是跳转到主界面
			if (isFirstUse) {
				intent.setClass(SplashActivity.this, LoginActivity.class);
				// // 设置第一次使用flag为false
				// SharedPreferencesUtils.put(SplashActivity.this,
				// IS_FIRST_USE_KEY, false);
			} else {
				if (application.getMemberEntity() == null) {
					intent.setClass(SplashActivity.this, LoginActivity.class);
				} else {
					if (application.getMemberEntity().getStrAccountNo2() != null
							&& application.getMemberEntity().getPassword() != null) {
						String mobilenumber = application.getMemberEntity()
								.getStrAccountNo2();
						String password = application.getMemberEntity()
								.getPassword();
						//con.login(mobilenumber, password, tms.User.LoginInfo.type.n_AccountNO2);
					}
					intent.setClass(SplashActivity.this, MainActivity.class);
				}
			}
			startActivity(intent);
			SplashActivity.this.finish();
			isAnimFinish = false;
		}
	}

	@Override
	public void onClick(View v) {

	}
}
