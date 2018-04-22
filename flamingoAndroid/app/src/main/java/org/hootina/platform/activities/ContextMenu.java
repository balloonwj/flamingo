package org.hootina.platform.activities;

import org.hootina.platform.R;

import android.view.View;
import android.widget.TextView;

public class ContextMenu extends BaseActivity {
    private TextView copy,delete,save,forward;
	private int position;

	@Override
	public void onClick(View v) {
switch (v.getId()) {
case R.id.save:
	
	break;
case R.id.delete:
	
	break;
case R.id.forward:
	
	break;
case R.id.copy:
	
	break;
	

default:
	break;
}
	}

	@Override
	protected int getContentView() {
		return R.layout.dialog;
	}

	@Override
	protected void initData() {
		position = getIntent().getIntExtra("position", -1);
		copy.setOnClickListener(this);
		delete.setOnClickListener(this);
		save.setOnClickListener(this);
		forward.setOnClickListener(this);
		
	}

	@Override
	protected void setData() {
		
		
		
		
	}

	// public boolean onTouchEvent(MotionEvent event) {
	// finish();
	// return true;
	// }

	// public void copy(View view) {
	// setResult(ChatActivity.RESULT_CODE_COPY,
	// new Intent().putExtra("position", position));
	// finish();
	// }

	// public void delete(View view) {
	// setResult(ChatActivity.RESULT_CODE_DELETE,
	// new Intent().putExtra("position", position));
	// finish();
	// }

	// public void forward(View view) {
	// setResult(ChatActivity.RESULT_CODE_FORWARD,
	// new Intent().putExtra("position", position));
	// finish();
	// }

	// public void open(View v) {
	// setResult(ChatActivity.RESULT_CODE_OPEN,
	// new Intent().putExtra("position", position));
	// finish();
	// }

	// public void download(View v) {
	// setResult(ChatActivity.RESULT_CODE_DWONLOAD,
	// new Intent().putExtra("position", position));
	// finish();
	// }

	// public void toCloud(View v) {
	// setResult(ChatActivity.RESULT_CODE_TO_CLOUD,
	// new Intent().putExtra("position", position));
	// finish();
	// }

}