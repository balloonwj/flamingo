package org.hootina.platform.widgets.x;

import android.annotation.TargetApi;
import android.content.Context;
import android.os.Build;
import android.util.AttributeSet;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewTreeObserver;
import android.view.ViewTreeObserver.OnGlobalLayoutListener;
import android.view.animation.DecelerateInterpolator;
import android.widget.AbsListView;
import android.widget.AbsListView.OnScrollListener;
import android.widget.LinearLayout;
import android.widget.RelativeLayout;
import android.widget.ScrollView;
import android.widget.Scroller;
import android.widget.TextView;
import org.hootina.platform.R;
/**
 * XScrollView, modified from {@link org.dianba.personal.widgets.x.XListView}
 *
 * @author markmjw
 * @date 2013-10-08
 * @see org.dianba.personal.widgets.x.XListView
 */
public class XScrollView extends ScrollView implements OnScrollListener {
	// private static final String TAG = "XScrollView";

	private final static int SCROLL_BACK_HEADER = 0;
	private final static int SCROLL_BACK_FOOTER = 1;

	private final static int SCROLL_DURATION = 400;

	// when pull up >= 50px
	private final static int PULL_LOAD_MORE_DELTA = 50;

	// support iOS like pull
	private final static float OFFSET_RADIO = 1.8f;

	private float mLastY = -1;

	// used for scroll back
	private Scroller mScroller;
	// user's scroll listener
	private OnScrollListener mScrollListener;
	// for mScroller, scroll back from header or footer.
	private int mScrollBack;

	// the interface to trigger refresh and load more.
	private IXScrollViewListener mListener;

	private LinearLayout mLayout;
	private LinearLayout mContentLayout;

	private XHeaderView mHeader;
	// header view content, use it to calculate the Header's height. And hide it
	// when disable pull refresh.
	private RelativeLayout mHeaderContent;
	private TextView mHeaderTime;
	private int mHeaderHeight;

	private XFooterView mFooterView;
	private TextView mHintView;

	private boolean mEnablePullRefresh = true;
	private boolean mPullRefreshing = false;

	private boolean mEnablePullLoad = true;
	private boolean mEnableAutoLoad = false;
	private boolean mPullLoading = false;

	public XScrollView(Context context) {
		super(context);
		initWithContext(context);
	}

	public XScrollView(Context context, AttributeSet attrs) {
		super(context, attrs);
		initWithContext(context);
	}

	public XScrollView(Context context, AttributeSet attrs, int defStyle) {
		super(context, attrs, defStyle);
		initWithContext(context);
	}

	private float xDistance, yDistance, xLast, yLast;

	// 解决横向竖向冲突的问题
	@Override
	public boolean onInterceptTouchEvent(MotionEvent ev) {
		switch (ev.getAction()) {
		case MotionEvent.ACTION_DOWN:
			xDistance = yDistance = 0f;
			xLast = ev.getX();
			yLast = ev.getY();
			break;
		case MotionEvent.ACTION_MOVE:
			final float curX = ev.getX();
			final float curY = ev.getY();

			xDistance += Math.abs(curX - xLast);
			yDistance += Math.abs(curY - yLast);
			xLast = curX;
			yLast = curY;

			if (xDistance > yDistance) {
				return false;
			}
		}

		return super.onInterceptTouchEvent(ev);
	}

	private void initWithContext(Context context) {
		mLayout = (LinearLayout) View.inflate(context,
				R.layout.vw_xscrollview_layout, null);
		mContentLayout = (LinearLayout) mLayout
				.findViewById(R.id.content_layout);

		mScroller = new Scroller(context, new DecelerateInterpolator());
		// XScrollView need the scroll event, and it will dispatch the event to
		// user's listener (as a proxy).
		this.setOnScrollListener(this);

		// init header view
		mHeader = new XHeaderView(context);
		mHeaderContent = (RelativeLayout) mHeader
				.findViewById(R.id.header_content);
		mHeaderTime = (TextView) mHeader.findViewById(R.id.header_hint_time);
		LinearLayout headerLayout = (LinearLayout) mLayout
				.findViewById(R.id.header_layout);
		headerLayout.addView(mHeader);

		// init footer view
		mFooterView = new XFooterView(context);
		mHintView = (TextView) mFooterView.findViewById(R.id.footer_hint_text);
		LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
				LinearLayout.LayoutParams.MATCH_PARENT,
				LinearLayout.LayoutParams.MATCH_PARENT);
		params.gravity = Gravity.CENTER;
		LinearLayout footLayout = (LinearLayout) mLayout
				.findViewById(R.id.footer_layout);
		footLayout.addView(mFooterView, params);

		// init header height
		ViewTreeObserver observer = mHeader.getViewTreeObserver();
		if (null != observer) {
			observer.addOnGlobalLayoutListener(new OnGlobalLayoutListener() {
				@TargetApi(Build.VERSION_CODES.JELLY_BEAN)
				@SuppressWarnings("deprecation")
				@Override
				public void onGlobalLayout() {
					mHeaderHeight = mHeaderContent.getHeight();
					ViewTreeObserver observer = getViewTreeObserver();
					if (null != observer) {
						if (Build.VERSION.SDK_INT < Build.VERSION_CODES.JELLY_BEAN) {
							observer.removeGlobalOnLayoutListener(this);
						} else {
							observer.removeOnGlobalLayoutListener(this);
						}
					}
				}
			});
		}

		this.addView(mLayout);

		// 解决刚进入显示文字的问题
		mFooterView.setBottomMargin(0);
		mFooterView.hide();
		mFooterView.setPadding(0, 0, 0, mFooterView.getHeight() * (-1));
		mFooterView.setOnClickListener(null);
	}

	/**
	 * Set the content ViewGroup for XScrollView.
	 *
	 * @param content
	 */
	public void setContentView(ViewGroup content) {
		if (mLayout == null) {
			return;
		}

		if (mContentLayout == null) {
			mContentLayout = (LinearLayout) mLayout
					.findViewById(R.id.content_layout);
		}

		if (mContentLayout.getChildCount() > 0) {
			mContentLayout.removeAllViews();
		}
		mContentLayout.addView(content);
	}

	/**
	 * Set the content View for XScrollView.
	 *
	 * @param content
	 */
	public void setView(View content) {
		if (mLayout == null) {
			return;
		}

		if (mContentLayout == null) {
			mContentLayout = (LinearLayout) mLayout
					.findViewById(R.id.content_layout);
		}
		mContentLayout.addView(content);
	}

	/**
	 * Enable or disable pull down refresh feature.
	 *
	 * @param enable
	 */
	public void setPullRefreshEnable(boolean enable) {
		mEnablePullRefresh = enable;

		// disable, hide the content
		mHeaderContent.setVisibility(enable ? View.VISIBLE : View.INVISIBLE);
	}

	/**
	 * Enable or disable pull up load more feature.
	 *
	 * @param enable
	 */
	public void setPullLoadEnable(boolean enable) {
		mEnablePullLoad = enable;

		if (!mEnablePullLoad) {

			// mFooterView.setBottomMargin(0);
			// mFooterView.hide();
			// mFooterView.setPadding(0, 0, 0, mFooterView.getHeight() * (-1));
			// mFooterView.setOnClickListener(null);
			mFooterView.setPadding(0, 0, 0, 0);
			mFooterView.show();
			mHintView.setText("没有更多数据啦");
			mFooterView.setOnClickListener(null);

		} else {
			mPullLoading = false;
			mFooterView.setPadding(0, 0, 0, 0);
			mFooterView.show();
			mFooterView.setState(XFooterView.STATE_NORMAL);
			// both "pull up" and "click" will invoke load more.
			mFooterView.setOnClickListener(new OnClickListener() {
				@Override
				public void onClick(View v) {
					startLoadMore();
				}
			});
		}
	}

	/**
	 * Enable or disable auto load more feature when scroll to bottom.
	 *
	 * @param enable
	 */
	public void setAutoLoadEnable(boolean enable) {
		mEnableAutoLoad = enable;
	}

	/**
	 * Stop refresh, reset header view.
	 */
	public void stopRefresh() {
		if (mPullRefreshing) {
			mPullRefreshing = false;
			resetHeaderHeight();
		}
	}

	/**
	 * Stop load more, reset footer view.
	 */
	public void stopLoadMore() {
		if (mPullLoading) {
			mPullLoading = false;
			if (mEnablePullLoad) {
				mHintView.setText("更多");
				mFooterView.setOnClickListener(new OnClickListener() {
					@Override
					public void onClick(View v) {
						startLoadMore();
					}
				});
			} else {
				mHintView.setText("没有更多数据啦");
				mFooterView.setOnClickListener(null);
			}
			mFooterView.setState(XFooterView.STATE_NORMAL);
		}
	}

	/**
	 * Set last refresh time
	 *
	 * @param time
	 */
	public void setRefreshTime(String time) {
		mHeaderTime.setText(time);
	}

	/**
	 * Set listener.
	 *
	 * @param listener
	 */
	public void setIXScrollViewListener(IXScrollViewListener listener) {
		mListener = listener;
	}

	/**
	 * Auto call back refresh.
	 */
	public void autoRefresh() {
		mHeader.setVisibleHeight(mHeaderHeight);

		if (mEnablePullRefresh && !mPullRefreshing) {
			// update the arrow image not refreshing
			if (mHeader.getVisibleHeight() > mHeaderHeight) {
				mHeader.setState(XHeaderView.STATE_READY);
			} else {
				mHeader.setState(XHeaderView.STATE_NORMAL);
			}
		}

		mPullRefreshing = true;
		mHeader.setState(XHeaderView.STATE_REFRESHING);
		refresh();
	}

	private void invokeOnScrolling() {
		if (mScrollListener instanceof OnXScrollListener) {
			OnXScrollListener l = (OnXScrollListener) mScrollListener;
			l.onXScrolling(this);
		}
	}

	private void updateHeaderHeight(float delta) {
		mHeader.setVisibleHeight((int) delta + mHeader.getVisibleHeight());

		if (mEnablePullRefresh && !mPullRefreshing) {
			// update the arrow image unrefreshing
			if (mHeader.getVisibleHeight() > mHeaderHeight) {
				mHeader.setState(XHeaderView.STATE_READY);
			} else {
				mHeader.setState(XHeaderView.STATE_NORMAL);
			}
		}

		// scroll to top each time
		post(new Runnable() {
			@Override
			public void run() {
				XScrollView.this.fullScroll(ScrollView.FOCUS_UP);
			}
		});
	}

	private void resetHeaderHeight() {
		int height = mHeader.getVisibleHeight();
		if (height == 0)
			return;

		// refreshing and header isn't shown fully. do nothing.
		if (mPullRefreshing && height <= mHeaderHeight)
			return;

		// default: scroll back to dismiss header.
		int finalHeight = 0;
		// is refreshing, just scroll back to show all the header.
		if (mPullRefreshing && height > mHeaderHeight) {
			finalHeight = mHeaderHeight;
		}

		mScrollBack = SCROLL_BACK_HEADER;
		mScroller.startScroll(0, height, 0, finalHeight - height,
				SCROLL_DURATION);

		// trigger orgputeScroll
		invalidate();
	}

	private void updateFooterHeight(float delta) {
		int height = mFooterView.getBottomMargin() + (int) delta;

		if (mEnablePullLoad && !mPullLoading) {
			if (height > PULL_LOAD_MORE_DELTA) {
				// height enough to invoke load more.
				mFooterView.setState(XFooterView.STATE_READY);
			} else {
				mFooterView.setState(XFooterView.STATE_NORMAL);
			}
		}

		mFooterView.setBottomMargin(height);

		// scroll to bottom
		post(new Runnable() {
			@Override
			public void run() {
				XScrollView.this.fullScroll(ScrollView.FOCUS_DOWN);
			}
		});
	}

	private void resetFooterHeight() {
		int bottomMargin = mFooterView.getBottomMargin();

		if (bottomMargin > 0) {
			mScrollBack = SCROLL_BACK_FOOTER;
			mScroller.startScroll(0, bottomMargin, 0, -bottomMargin,
					SCROLL_DURATION);
			invalidate();
		}
	}

	private void startLoadMore() {
		if (!mPullLoading) {
			mPullLoading = true;
			mFooterView.setState(XFooterView.STATE_LOADING);
			loadMore();
		}
	}

	@Override
	public boolean onTouchEvent(MotionEvent ev) {
		if (mLastY == -1) {
			mLastY = ev.getRawY();
		}

		switch (ev.getAction()) {
		case MotionEvent.ACTION_DOWN:
			mLastY = ev.getRawY();
			break;

		case MotionEvent.ACTION_MOVE:
			final float deltaY = ev.getRawY() - mLastY;
			mLastY = ev.getRawY();

			if (isTop() && (mHeader.getVisibleHeight() > 0 || deltaY > 0)) {
				// the first item is showing, header has shown or pull down.
				updateHeaderHeight(deltaY / OFFSET_RADIO);
				invokeOnScrolling();

			} else if (isBottom()
					&& (mFooterView.getBottomMargin() > 0 || deltaY < 0)) {
				// last item, already pulled up or want to pull up.
				// updateFooterHeight(-deltaY / OFFSET_RADIO);

			}
			break;

		default:
			// reset
			mLastY = -1;

			resetHeaderOrBottom();
			break;
		}

		return super.onTouchEvent(ev);
	}

	private void resetHeaderOrBottom() {
		if (isTop()) {
			// invoke refresh
			if (mEnablePullRefresh
					&& mHeader.getVisibleHeight() > mHeaderHeight) {
				mPullRefreshing = true;
				mHeader.setState(XHeaderView.STATE_REFRESHING);
				refresh();
			}
			resetHeaderHeight();

		} else if (isBottom()) {
			// invoke load more.
			if (mEnablePullLoad
					&& mFooterView.getBottomMargin() > PULL_LOAD_MORE_DELTA) {
				startLoadMore();
			}
			resetFooterHeight();
		}
	}

	private boolean isTop() {
		return getScrollY() <= 0 || mHeader.getVisibleHeight() > mHeaderHeight;
	}

	private boolean isBottom() {
		return Math.abs(getScrollY() + getHeight()
				- computeVerticalScrollRange()) <= 5
				|| (getScrollY() > 0 && null != mFooterView && mFooterView
						.getBottomMargin() > 0);
	}

	@Override
	public void computeScroll() {
		if (mScroller.computeScrollOffset()) {
			if (mScrollBack == SCROLL_BACK_HEADER) {
				mHeader.setVisibleHeight(mScroller.getCurrY());
			} else {
				mFooterView.setBottomMargin(mScroller.getCurrY());
			}

			postInvalidate();
			invokeOnScrolling();
		}
		super.computeScroll();
	}

	public void setOnScrollListener(OnScrollListener l) {
		mScrollListener = l;
	}

	@Override
	public void onScrollStateChanged(AbsListView view, int scrollState) {
		if (mScrollListener != null) {
			mScrollListener.onScrollStateChanged(view, scrollState);
		}
	}

	@Override
	protected void onScrollChanged(int l, int t, int oldl, int oldt) {
		// Grab the last child placed in the ScrollView, we need it to
		// determinate the bottom position.
		View view = getChildAt(getChildCount() - 1);

		if (null != view) {
			// Calculate the scroll diff
			// int diff = (view.getBottom() - (view.getHeight() +
			// view.getScrollY()));

			// if diff is zero, then the bottom has been reached
			if (isBottom() && mEnableAutoLoad) {
				// notify that we have reached the bottom
				startLoadMore();
			}
		}

		super.onScrollChanged(l, t, oldl, oldt);
	}

	@Override
	public void onScroll(AbsListView view, int firstVisibleItem,
			int visibleItemCount, int totalItemCount) {
		// send to user's listener
		if (mScrollListener != null) {
			mScrollListener.onScroll(view, firstVisibleItem, visibleItemCount,
					totalItemCount);
		}
	}

	private void refresh() {
		if (mEnablePullRefresh && null != mListener) {
			mListener.onRefresh();
		}
	}

	private void loadMore() {
		if (mEnablePullLoad && null != mListener) {
			mListener.onLoadMore();
		}
	}

	/**
	 * You can listen ListView.OnScrollListener or this one. it will invoke
	 * onXScrolling when header/footer scroll back.
	 */
	public interface OnXScrollListener extends OnScrollListener {
		public void onXScrolling(View view);
	}

	/**
	 * Implements this interface to get refresh/load more event.
	 */
	public interface IXScrollViewListener {
		public void onRefresh();

		public void onLoadMore();
	}
}
