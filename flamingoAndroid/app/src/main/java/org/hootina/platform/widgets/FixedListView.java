package org.hootina.platform.widgets;

import android.content.Context;
import android.util.AttributeSet;
import android.widget.ListView;

/**
 * @author Administrator 固定长度的ListView
 *
 */
public class FixedListView  extends ListView{

	public FixedListView(Context context) {
		super(context);
	}

	public FixedListView(Context context, AttributeSet attrs) {
		super(context, attrs);
	}
	
	public FixedListView(Context context, AttributeSet attrs, int defStyle) {
		super(context, attrs, defStyle);
	}
	
	@Override
	protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
		int expandSpec = MeasureSpec.makeMeasureSpec(Integer.MAX_VALUE >> 2, MeasureSpec.AT_MOST);
		super.onMeasure(widthMeasureSpec, expandSpec);
	}
} 
