package org.hootina.platform.utils;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.net.URL;
import java.net.URLConnection;

import android.app.AlertDialog;
import android.app.Dialog;
import android.content.Context;
import android.content.DialogInterface;
import android.content.DialogInterface.OnKeyListener;
import android.content.Intent;
import android.net.Uri;
import android.os.Environment;
import android.os.Handler;
import android.os.Message;
import android.util.Log;
import android.view.KeyEvent;
import android.webkit.URLUtil;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;


/**
 * @author Admin 版本升级工具
 *
 */
public class AutoUpdate {
	/*** 上下文对象 **/
	public Context mContext;
	/***** 是否强制升级标志 */
	boolean isMustUpdate;
	public int versionCode = 0;

	public String versionName = "";

	private static final String TAG = "MyAutoUpdate";

	private String currentTempFilePath = "";

	private String fileEx = "";

	private String fileNa = "";

	private String strURL = "";
	private TextView mText;
	private Dialog progress;
	private int downsize = 0;
	private int downTotalSize = 0;
	private ProgressBar MProgressBar;
	private File myTempFile;
	private static final int EOOOR = 100001;
	private Handler myHandler = new Handler() {
		public void handleMessage(Message msg) {
			switch (msg.what) {
			case 1000:
				downTotalSize = msg.getData().getInt("totalsize");
				MProgressBar.setMax(downTotalSize);
				break;
			case 1001:

				updateNotify(msg);
				break;
			case 1002:
				progress.dismiss();
				// Toast.makeText(mContext, "更新文件不存在",
				// Toast.LENGTH_SHORT).show();
				break;
			case EOOOR:
				progress.dismiss();
				Toast.makeText(mContext, "更新失败", Toast.LENGTH_SHORT).show();
			default:
				break;
			}
		};
	};

	public AutoUpdate(Context mContext, String strURL) {
		this.mContext = mContext;
		this.strURL = strURL;
	}

	private void initProgressBar() {
//		progress = new Dialog(mContext, R.style.LoadingDialog);
//		progress.setContentView(R.layout.layout_download);
//		mText = (TextView) progress.findViewById(R.id.TvDownPro);
//		mText.setText("      00KB/0000KB");
//		MProgressBar = (ProgressBar) progress
//				.findViewById(R.id.ProgressDownload);
		progress.setOnKeyListener(new OnKeyListener() {

			@Override
			public boolean onKey(DialogInterface dialog, int keyCode,
					KeyEvent event) {
				if (keyCode == KeyEvent.KEYCODE_BACK) {
					if (isMustUpdate) {
						System.exit(0);
					} else {
						progress.dismiss();
					}
				}

				return false;
			}
		});
		progress.show();
	}

	public void check(Boolean mustUpdate, String updateStr) {
		isMustUpdate = mustUpdate;
		showUpdateDialog(updateStr);
	}

	@SuppressWarnings("deprecation")
	public void showUpdateDialog(String updateStr) {
		// @SuppressWarnings("unused")
		final AlertDialog alert = new AlertDialog.Builder(mContext).create();
		alert.setTitle("提示");
		alert.setMessage(updateStr);

		alert.setButton("立即更新", new DialogInterface.OnClickListener() {
			public void onClick(DialogInterface dialog, int which) {
				downloadTheFile(strURL);
			}
		});
		if (!isMustUpdate) {// 是否强制更新
			alert.setButton2("稍后更新", new DialogInterface.OnClickListener() {
				public void onClick(DialogInterface dialog, int which) {
					dialog.cancel();
				}
			});
		}
		alert.setOnKeyListener(new OnKeyListener() {

			@Override
			public boolean onKey(DialogInterface arg0, int arg1, KeyEvent arg2) {

				if (arg1 == KeyEvent.KEYCODE_BACK) {
					if (isMustUpdate) {
						System.exit(0);
					} else {
						alert.dismiss();
					}
				}
				return false;
			}
		});
		alert.show();
	}

	private void downloadTheFile(final String strPath) {
		Log.e(TAG, "文件下载路径strPath:" + strPath);
		// 获取后缀
		fileEx = strURL.substring(strURL.lastIndexOf(".") + 1, strURL.length())
				.toLowerCase();
		// 文件名
		fileNa = "aimer";
		try {

			initProgressBar();
			new Thread(new Runnable() {
				public void run() {
					try {
						doDownloadTheFile(strPath);
					} catch (Exception e) {
						if (progress != null && progress.isShowing()) {
							progress.dismiss();
						}
						Log.e(TAG, e.getMessage(), e);
						myHandler.sendEmptyMessage(EOOOR);
					}
				}
			}).start();

		} catch (Exception e) {
			myHandler.sendEmptyMessage(EOOOR);
			e.printStackTrace();
		}
	}

	/**
	 * 文件下载
	 * 
	 * @param 文件路径strPath
	 * @throws Exception
	 */
	public void doDownloadTheFile(String strPath) throws Exception {
		if (!URLUtil.isNetworkUrl(strPath)) {
			Log.i(TAG, "getDataSource() It's a wrong URL!");
		} else {
			URL myURL = new URL(strPath);
			URLConnection conn = myURL.openConnection();
			conn.connect();
			InputStream is = conn.getInputStream();
			Message msg = new Message();
			msg.what = 1000;
			msg.getData().putInt("totalsize", conn.getContentLength());
			myHandler.sendMessage(msg);
			if (is == null) {
				throw new RuntimeException("stream is null");
			}
			myTempFile = new File(Environment.getExternalStorageDirectory(),
					fileNa + "." + fileEx);
			currentTempFilePath = myTempFile.getAbsolutePath();
			FileOutputStream fos = new FileOutputStream(myTempFile);
			byte buf[] = new byte[1024 * 8];
			do {
				int numread = is.read(buf);
				downsize += numread;
				if (numread < 0) {
					Message mesg = new Message();
					mesg.what = 1002;
					myHandler.sendMessage(mesg);
					break;
				}
				fos.write(buf, 0, numread);
				Message mesg = new Message();
				mesg.getData().putInt("downsize", downsize);
				mesg.what = 1001;
				myHandler.sendMessage(mesg);
			} while (true);
			Log.i(TAG, "getDataSource() Download ok...");
			try {
				is.close();
			} catch (Exception ex) {
				Log.e(TAG, "getDataSource() error: " + ex.getMessage(), ex);
			}
			openFile(myTempFile);

		}

	}

	/**
	 * 通过文件名打开文件
	 * 
	 * @param
	 */
	private void openFile(File f) {
		if (progress != null && progress.isShowing()) {
			progress.dismiss();
		}
		Intent intent = new Intent();
		intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
		intent.setAction(android.content.Intent.ACTION_VIEW);
		String type = getMIMEType(f);
		intent.setDataAndType(Uri.fromFile(f), type);
		mContext.startActivity(intent);
		// delFile();
	}

	public void delFile() {
		Log.i(TAG, "The TempFile(" + currentTempFilePath + ") was deleted.");
		File myFile = new File(currentTempFilePath);
		if (myFile.exists()) {
			myFile.delete();
		}
	}

	/**
	 * 获取文件类型
	 * 
	 * @param f
	 * @return
	 */
	private String getMIMEType(File f) {
		String type = "";
		String fName = f.getName();
		String end = fName
				.substring(fName.lastIndexOf(".") + 1, fName.length())
				.toLowerCase();

		if (end.equals("m4a") || end.equals("mp3") || end.equals("mid")
				|| end.equals("xmf") || end.equals("ogg") || end.equals("wav")) {
			type = "audio";
		} else if (end.equals("3gp") || end.equals("mp4")) {
			type = "video";
		} else if (end.equals("jpg") || end.equals("gif") || end.equals("png")
				|| end.equals("jpeg") || end.equals("bmp")) {
			type = "image";
		} else if (end.equals("apk")) {
			type = "application/vnd.android.package-archive";
		} else {
			type = "*";
		}
		if (end.equals("apk")) {
		} else {
			type += "/*";
		}
		return type;
	}

	/**
	 * 更新下载进度
	 * 
	 * @param msg
	 */
	private void updateNotify(Message msg) {
		int downsize = msg.getData().getInt("downsize");
		double downsized = downsize;
		double downtotalsized = downTotalSize;
		int myprogress = (int) ((downsized / downtotalsized) * 100);
		String pro = myprogress + "%" + "      " + (downsize / 1024) + "KB/"
				+ (downTotalSize / 1024) + "KB";
		mText.setText(pro);
		MProgressBar.setProgress(downsize);

	}
}
