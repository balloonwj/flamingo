package org.hootina.platform.widgets.x;

import android.content.Context;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.RelativeLayout;
import android.widget.Scroller;
import android.widget.TextView;

import org.hootina.platform.R;

public class SwipeItemView extends LinearLayout {

	private Context mContext;
	private LinearLayout mViewContent;
	private RelativeLayout mHolder;
	private Scroller mScroller;
	private OnSlideListener mOnSlideListener;

	private int mHolderWidth;

	private int mLastX = 0;
	private int mLastY = 0;
	private static final int TAN = 2;
	private boolean isHorizontalMove = true;// 判断是否横滑

	public interface OnSlideListener {
		public static final int SLIDE_STATUS_OFF = 0;
		public static final int SLIDE_STATUS_START_SCROLL = 1;
		public static final int SLIDE_STATUS_ON = 2;

		/**
		 * @param view
		 *            current SlideView
		 * @param status
		 *            SLIDE_STATUS_ON or SLIDE_STATUS_OFF
		 */
		public void onSlide(View view, int status);
	}

	public SwipeItemView(Context context) {
		super(context);
		initView();
	}

	public SwipeItemView(Context context, AttributeSet attrs) {
		super(context, attrs);
		initView();
	}

	private void initView() {
		mContext = getContext();
		// 初始化弹性滑动对象
		mScroller = new Scroller(mContext);
		// 设置其方向为横向
		setOrientation(LinearLayout.HORIZONTAL);
		// 将slide_view_merge加载进来
		View.inflate(mContext, R.layout.slide_view_merge, this);
		mViewContent = (LinearLayout) findViewById(R.id.view_content);

		mHolder = (RelativeLayout) findViewById(R.id.holder);
		mHolder.measure(View.MeasureSpec.UNSPECIFIED,
				View.MeasureSpec.UNSPECIFIED);
		mHolderWidth = mHolder.getMeasuredWidth();
		mHolder.setLayoutParams(new LinearLayout.LayoutParams(mHolderWidth,
				LayoutParams.MATCH_PARENT));
	}

	public void setButtonText(CharSequence text) {
		((TextView) findViewById(R.id.delete)).setText(text);
	}

	public void setContentView(View view) {
		mViewContent.addView(view);
	}

	public void setOnSlideListener(OnSlideListener onSlideListener) {
		mOnSlideListener = onSlideListener;
	}

	public void shrink() {
		if (getScrollX() != 0) {
			this.smoothScrollTo(0, 0);
		}
	}

	public void shrinkNoAnim() {
		if (getScrollX() != 0) {
			this.scrollTo(0, 0);
		}
	}

	public void onRequireTouchEvent(MotionEvent event) {
		int x = (int) event.getX();
		int y = (int) event.getY();
		int scrollX = getScrollX();

		switch (event.getAction()) {
		case MotionEvent.ACTION_DOWN: {
			isHorizontalMove = true;// 按下默认为true，当移上下则为false
			if (!mScroller.isFinished()) {
				mScroller.abortAnimation();
			}
			if (mOnSlideListener != null) {
				mOnSlideListener.onSlide(this,
						OnSlideListener.SLIDE_STATUS_START_SCROLL);
			}
			break;
		}
		case MotionEvent.ACTION_MOVE: {
			int deltaX = x - mLastX;
			int deltaY = y - mLastY;
			// 上下移动
			if (Math.abs(deltaX) < Math.abs(deltaY) * TAN) {
				isHorizontalMove = false;
				break;
			}

			if (!isHorizontalMove)
				break;
			int newScrollX = scrollX - deltaX;

			if (deltaX != 0) {
				if (newScrollX < 0) {
					newScrollX = 0;
				} else if (newScrollX > mHolderWidth) {
					newScrollX = mHolderWidth;
				}
				this.scrollTo(newScrollX, 0);
			}
			break;
		}
		case MotionEvent.ACTION_UP: {
			int newScrollX = 0;
			if (scrollX - mHolderWidth * 0.5 > 0) {
				newScrollX = mHolderWidth;
			}
			this.smoothScrollTo(newScrollX, 0);
			if (mOnSlideListener != null) {
				mOnSlideListener.onSlide(this,
						newScrollX == 0 ? OnSlideListener.SLIDE_STATUS_OFF
								: OnSlideListener.SLIDE_STATUS_ON);
			}
			break;
		}
		case MotionEvent.ACTION_CANCEL: {
			int newScrollX = 0;
			if (scrollX - mHolderWidth * 0.5 > 0) {
				newScrollX = mHolderWidth;
			}
			this.smoothScrollTo(newScrollX, 0);
			if (mOnSlideListener != null) {
				mOnSlideListener.onSlide(this,
						newScrollX == 0 ? OnSlideListener.SLIDE_STATUS_OFF
								: OnSlideListener.SLIDE_STATUS_ON);
			}
			break;
		}
		default:
			break;
		}

		mLastX = x;
		mLastY = y;
	}

	private void smoothScrollTo(int destX, int destY) {
		int scrollX = getScrollX();
		int delta = destX - scrollX;
		mScroller.startScroll(scrollX, 0, delta, 0, Math.abs(delta) * 3);
		invalidate();
	}

	@Override
	public void computeScroll() {
		if (mScroller.computeScrollOffset()) {
			scrollTo(mScroller.getCurrX(), mScroller.getCurrY());
			postInvalidate();
		}
	}

	public boolean getisHorizontalMove() {
		return isHorizontalMove;
	}

}
