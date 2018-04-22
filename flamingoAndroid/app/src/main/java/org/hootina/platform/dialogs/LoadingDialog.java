package org.hootina.platform.dialogs;

import android.app.Dialog;
import android.content.Context;
import android.os.Bundle;

import org.hootina.platform.R;

/**
 * @author Administrator 网络请求进度条
 *
 */
public class LoadingDialog extends Dialog {

	public LoadingDialog(Context context) {
		super(context, R.style.LoadingDialog);
	}

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.dialog_loading);

		// 设置点击其它地方进度条不消失
		setCanceledOnTouchOutside(false);
	}

}
