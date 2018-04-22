package org.hootina.platform.widgets;

import android.content.Context;
import android.util.AttributeSet;
import android.widget.GridView;

/**
 * @author Administrator 固定长度的GridView
 *
 */
public class FixedGridView extends GridView {
	public FixedGridView(Context context, AttributeSet attrs) {
		super(context, attrs);
	}

	public FixedGridView(Context context) {
		super(context);
	}

	public FixedGridView(Context context, AttributeSet attrs, int defStyle) {
		super(context, attrs, defStyle);
	}

	@Override
	public void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {

		int expandSpec = MeasureSpec.makeMeasureSpec(Integer.MAX_VALUE >> 2,
				MeasureSpec.AT_MOST);
		super.onMeasure(widthMeasureSpec, expandSpec);
	}
}
