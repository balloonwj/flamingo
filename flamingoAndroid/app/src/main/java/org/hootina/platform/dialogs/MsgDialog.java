package org.hootina.platform.dialogs;

import java.util.Date;

import org.hootina.platform.R;
import org.hootina.platform.activities.member.ForwardActivity;

import android.annotation.SuppressLint;
import android.app.Dialog;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.view.View;
import android.view.Window;
import android.widget.TextView;
import android.widget.Toast;

public class MsgDialog extends Dialog {
	Context context;
	private TextView copy, delete, forward, save;
	private static int positions, uAccountIDs, uSendIDs, ids;
	private static String msgtexts, type;
	private String name, headpath, time;
	private int nface;
	private Date date;

	public MsgDialog(Context context) {
		super(context);
		this.context = context;
	}

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		// TODO Auto-generated method stub
		super.onCreate(savedInstanceState);
		this.requestWindowFeature(Window.FEATURE_NO_TITLE);
		this.setContentView(R.layout.dialog);
		setCanceledOnTouchOutside(true);
		intview();
		intlistner();
	}

	private void intview() {
		copy = (TextView) findViewById(R.id.copy);
		delete = (TextView) findViewById(R.id.delete);
		forward = (TextView) findViewById(R.id.forward);
		save = (TextView) findViewById(R.id.save);
	}
	private void intlistner() {
		// 复制
		copy.setOnClickListener(new android.view.View.OnClickListener() {
			@SuppressLint("NewApi")
			@Override
			public void onClick(View v) {
				MsgDialog.this.cancel();
				ClipboardManager cmb = (ClipboardManager) context
						.getSystemService(Context.CLIPBOARD_SERVICE);
				cmb.setText(msgtexts);

			}
		});

		// 删除
		delete.setOnClickListener(new android.view.View.OnClickListener() {
			@Override
			public void onClick(View v) {
				MsgDialog.this.cancel();
//				try {
//					BaseActivity.getDb().delete(
//							ChatMessage.class,
//							WhereBuilder.b("uSendID", "=", uSendIDs)
//									.and("uAccountID", "=", uAccountIDs)
//									.and("id", "=", ids));
//				} catch (DbException e) {
//					e.printStackTrace();
//				}
//
//				Message msg = new Message();
//				msg.what = MegAsnType.Refresh;
//				BaseActivity.sendMessage(msg);
			}
		});
		// 转发
		forward.setOnClickListener(new android.view.View.OnClickListener() {
			@Override
			public void onClick(View v) {
				MsgDialog.this.cancel();
				Intent intent = new Intent();
				intent.setClass(context, ForwardActivity.class);
				intent.putExtra("msgtexts", msgtexts);
				intent.putExtra("type", type);
				context.startActivity(intent);
			}
		});
		// 收藏
		save.setOnClickListener(new android.view.View.OnClickListener() {
			@Override
			public void onClick(View v) {
				MsgDialog.this.cancel();
				Toast.makeText(context, "收藏成功", 1000).show();
				save();
			}
		});

	}
	public void save() {
//		SimpleDateFormat formatter = new SimpleDateFormat("yyyy-MM-dd HH:mm");
//
//		long epoch = System.currentTimeMillis();
//		date = new Date(epoch);
//		time = formatter.format(date);
//		FavoriteItem shouCangInfo = new FavoriteItem();
//		shouCangInfo.setMsgtext(msgtexts);
//		shouCangInfo.setType(type);
//		shouCangInfo.setuAccountID(uAccountIDs);
//		shouCangInfo.setuTargetID(uSendIDs);
//		shouCangInfo.setmMsgTime(time);
//		if (uAccountIDs != uSendIDs) {
//			try {
//				List<FriendInfo> friendlist = BaseActivity.getDb().findAll(
//						Selector.from(FriendInfo.class)
//								.where("uAccountID", "=", uAccountIDs)
//								.and("uTargetID", "=", uSendIDs));
//
//				nface = friendlist.get(0).getnFace();
//				headpath = friendlist.get(0).getHeadpath();
//				name = friendlist.get(0).getStrNickName();
//
//			} catch (DbException e) {
//				e.printStackTrace();
//			}
//		} else {
//			nface = BaseActivity.getMyMemberEntity().getnFace();
//			headpath = BaseActivity.getMyMemberEntity().getHeadpath();
//			name = BaseActivity.getMyMemberEntity().getNickname();
//		}
//
//		try {
//			shouCangInfo.setName(name);
//			shouCangInfo.setnFace(nface);
//			shouCangInfo.setHeadpath(headpath);
//			BaseActivity.getDb().save(shouCangInfo);
//		} catch (DbException e) {
//			e.printStackTrace();
//		}

	}
	public static void setpostion(int position, int uAccountID, int uSendID,
			int id, String text, String typ) {
		positions = position;
		uAccountIDs = uAccountID;
		uSendIDs = uSendID;
		ids = id;
		msgtexts = text;
		type = typ;
	}
}
