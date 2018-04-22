package org.hootina.platform.widgets;

import android.content.Context;
import android.util.AttributeSet;
import android.webkit.WebChromeClient;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.ProgressBar;
import android.widget.RelativeLayout;

/**
 * @author Administrator 带进度条的WebView
 *
 */
public class ProgressWebView extends RelativeLayout {

	private WebView wv;

	private ProgressBar progressbar;

	public ProgressWebView(Context context, AttributeSet attrs) {
		super(context, attrs);
		wv = new WebView(context);
		wv.setLayoutParams(new LayoutParams(LayoutParams.MATCH_PARENT,
				LayoutParams.MATCH_PARENT));
		wv.setWebChromeClient(webChromeClient);
		addView(wv);

		progressbar = new ProgressBar(context, null,
				android.R.attr.progressBarStyleHorizontal);
		progressbar.setLayoutParams(new LayoutParams(LayoutParams.MATCH_PARENT,
				4));
		addView(progressbar);

	}
	
	WebChromeClient webChromeClient = new WebChromeClient() {
		@Override
		public void onProgressChanged(WebView view, int newProgress) {
			if (newProgress == 100) {
				progressbar.setVisibility(GONE);
			} else {
				if (progressbar.getVisibility() == GONE) {
					progressbar.setVisibility(VISIBLE);
				}
				progressbar.setProgress(newProgress);
			}
			super.onProgressChanged(view, newProgress);
		}
	};

	public void setJavaScriptEnabled(boolean isJsEnabled) {
		wv.getSettings().setJavaScriptEnabled(isJsEnabled);
	}

	public void loadUrl(String url) {
		wv.loadUrl(url);
	}

	public void setWebViewClient(WebViewClient webViewClient) {
		wv.setWebViewClient(webViewClient);
	}

}
