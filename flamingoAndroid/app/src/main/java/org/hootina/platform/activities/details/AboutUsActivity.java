package org.hootina.platform.activities.details;

import android.view.View;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import org.hootina.platform.R;
import org.hootina.platform.activities.BaseActivity;
import org.hootina.platform.widgets.ProgressWebView;

public class AboutUsActivity extends BaseActivity {
	/*
	 * (non-Javadoc)
	 * 
	 * @关于我们
	 */
	private ProgressWebView wv_about_us;
	private String title, url;

	@Override
	protected int getContentView() {
		return R.layout.activity_about_us;
	}

	@Override
	protected void initData() {

		url ="http://www.hootina.org";
	}

	@Override
	protected void setData() {

		wv_about_us.setJavaScriptEnabled(true);
		// 限制点击链接时不跳转到浏览器
		wv_about_us.setWebViewClient(new WebViewClient() {
			@Override
			public boolean shouldOverrideUrlLoading(WebView view, String url) {
				return false;
			}
		});
		wv_about_us.loadUrl(url);
	}

	@Override
	public void onClick(View v) {
		switch (v.getId()) {
		case R.id.btn_back:
			finish();
			break;

		default:
			break;
		}
	}

}