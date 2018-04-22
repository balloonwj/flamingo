package org.hootina.platform.utils;

import android.content.Context;
import android.widget.ImageView;
import android.widget.LinearLayout;

public class PointUtil {

	/**
	 * 添加点
	 * @param context 上下文
	 * @param length 点的数量
	 * @param selectBg 选中时点的背景
	 * @param unselectBg 未选中时点的背景
	 * @param container 点的容器，一般新建LinearLayout
	 */
	public static void initPoints(Context context, int length, int selectBg,
			int unselectBg, LinearLayout container) {

		ImageView iv;
		for (int i = 0; i < length; i++) {
			iv = new ImageView(context);
			if (i == 0) {
				iv.setImageResource(selectBg);
			} else {
				iv.setImageResource(unselectBg);
			}

			iv.setPadding(4, 4, 4, 4);

			container.addView(iv);
		}

	}

	/**
	 * 改变点的背景图片
	 * @param index 选中点的下标
	 * @param length 点的数量
	 * @param selectBg 选中时点的背景
	 * @param unselectBg 未选中时点的背景
	 * @param container 点的容器，一般新建LinearLayout
	 */
	public static void updatePoints(int index, int length, int selectBg,
			int unselectBg, LinearLayout container) {

		ImageView iv;
		for (int i = 0; i < length; i++) {
			iv = (ImageView) container.getChildAt(i);
			if (i == index) {
				iv.setImageResource(selectBg);
			} else {
				iv.setImageResource(unselectBg);
			}
		}
	}
}
