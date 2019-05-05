package org.hootina.platform.activities;

import java.util.ArrayList;
import java.util.List;

import org.hootina.platform.R;
import org.hootina.platform.adapters.SearchFriendAdapter;
import org.hootina.platform.adapters.ViewPagerAdapter;
import org.hootina.platform.utils.PointUtil;
import org.hootina.platform.widgets.ScaleImageView;

import android.content.Intent;
import android.os.Bundle;
import android.support.v4.view.ViewPager;
import android.support.v4.view.ViewPager.OnPageChangeListener;
import android.view.LayoutInflater;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.LinearLayout;

public class GuideActivity extends BaseActivity {
	private ViewPager vp_guide;
	private LinearLayout ll_points;

	// 图片数组。改变图片，只需要改这里
	private int[] pics;

	// 未选中点的背景
	private final int UNSELECTED_POINT_IMAGE = R.drawable.gray_point_gd;
	// 选中点的背景
	private final int SELECTED_POINT_IMAGE = R.drawable.white_point_gd;

	@Override
	protected void onCreate(Bundle arg0) {
		super.onCreate(arg0);

		showPics();
		PointUtil.initPoints(this, pics.length, SELECTED_POINT_IMAGE, UNSELECTED_POINT_IMAGE, ll_points);
	}

	@Override
	protected int getContentView() {
		return R.layout.activity_guide;
	}

	@Override
	protected void initData() {
		
			pics = new int[] {R.drawable.guide_talk1, R.drawable.guide_talk2, R.drawable.guide_talk3};
		
	}

	@Override
	protected void setData() {
		vp_guide.setOnPageChangeListener(new OnPageChangeListener() {

			@Override
			public void onPageSelected(int arg0) {
				PointUtil.updatePoints(arg0, pics.length, SELECTED_POINT_IMAGE, UNSELECTED_POINT_IMAGE, ll_points);
			}

			@Override
			public void onPageScrolled(int arg0, float arg1, int arg2) {

			}

			@Override
			public void onPageScrollStateChanged(int arg0) {

			}
		});
	}

	@Override
	public void onClick(View v) {

	}

	/**
	 * 初始化viewpager
	 */
	private void showPics() {
		List<View> views = new ArrayList<View>();

		int length = pics.length;
		for (int i = 0; i < length; i++) {
			View view = LayoutInflater.from(this).inflate(R.layout.item_guide, null);

			ScaleImageView iv = (ScaleImageView) view.findViewById(R.id.iv_pic);
			iv.setImageResource(pics[i]);

			views.add(view);
		}
		// 点击最后一张图片跳转到主界面
		views.get(length - 1).setOnClickListener(lastPicListener);

		vp_guide.setAdapter(new ViewPagerAdapter(views));
	}

	/**
	 * 点击最后一张图片跳转到主界面
	 */
	private OnClickListener lastPicListener = new OnClickListener() {

		@Override
		public void onClick(View v) {
			startActivity(new Intent(GuideActivity.this,LoginActivity.class));
			GuideActivity.this.finish();
		}
	};

}
