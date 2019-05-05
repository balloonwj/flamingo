package org.hootina.platform.activities.member;

import android.app.Dialog;
import android.content.Intent;
import android.graphics.Bitmap;
import android.os.Handler;
import android.os.Message;
import android.view.LayoutInflater;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.AdapterView;
import android.widget.AdapterView.OnItemClickListener;
import android.widget.Button;
import android.widget.ImageButton;
import android.widget.ListView;
import android.widget.TextView;

import org.hootina.platform.R;
import org.hootina.platform.activities.BaseActivity;
import org.hootina.platform.adapters.FriendsAdapter;
import org.hootina.platform.result.ChatMessage;
import org.hootina.platform.result.ChatSession;
import org.hootina.platform.result.SendMessage;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

public class ForwardActivity extends BaseActivity {
	/*
	 * @s转发
	 */

	private ListView lv_friends;
	private FriendsAdapter friendsAdapter;
	private String strNickName;
	private int uTargetID;
	private TextView tv_name, btn_call, btn_more;
	private String msgtexts, type;
	public static boolean isCheck = false; // 是否显示checkbox
	public static boolean isChoice = false; // 是否多选
	private ImageButton btn_back;
	List arraylist = new ArrayList();
	private String talkmsg;
	private SendMessage sendMessage;

	private int UserId;
	private MyHandler handler = new MyHandler();
	private ChatMessage messages;
	private int uAccountID;
	List namelist = new ArrayList();

	private ChatSession chatSession;
	private List<ChatSession> infos;
	private List<ChatMessage> mChatMessages = new ArrayList<ChatMessage>();
	private Bitmap bitmap;

	public class MyHandler extends Handler {
		@Override
		public void dispatchMessage(Message msg) {
			super.dispatchMessage(msg);
			switch (msg.what) {
			case 0:
				// 选择群发用户uTargetID
				UserId = msg.arg1;
				strNickName = (String) msg.obj;
				arraylist.add(UserId);
				namelist.add(strNickName);
				if (arraylist.size() <= 0) {
					btn_more.setTextColor(getResources().getColor(
							R.color.half_white));
					btn_more.setEnabled(false);
				} else {
					btn_more.setTextColor(getResources()
							.getColor(R.color.white));
					btn_more.setEnabled(true);
				}

				break;
			case 1:
				// 取消群发用户uTargetID
				UserId = msg.arg1;
				strNickName = (String) msg.obj;
				arraylist.remove(Integer.valueOf(UserId));
				namelist.remove(strNickName);
				if (arraylist.size() <= 0) {
					btn_more.setTextColor(getResources().getColor(
							R.color.half_white));
					btn_more.setEnabled(false);
				} else {
					btn_more.setTextColor(getResources()
							.getColor(R.color.white));
					btn_more.setEnabled(true);
				}
				break;
			default:
				break;
			}
		}
	}

	@Override
	public void onClick(View v) {
		switch (v.getId()) {
		case R.id.btn_back:

			finish();
			break;

		case R.id.btn_call:
			// 取消
			isCheck = false;
			isChoice = false;
			btn_more.setText("多选");
			btn_more.setTextColor(getResources().getColor(R.color.white));
			btn_more.setEnabled(true);
			arraylist.clear();
			namelist.clear();
			btn_back.setVisibility(View.VISIBLE);
			btn_call.setVisibility(View.GONE);
			friendsAdapter.notifyDataSetChanged();
			break;

		case R.id.btn_more:
			// 多选
			btn_more.setText("发送");
			btn_back.setVisibility(View.GONE);
			btn_call.setVisibility(View.VISIBLE);
			isCheck = true;
			isChoice = true;
			friendsAdapter.notifyDataSetChanged();
			// 群发消息
			if (arraylist.size() <= 0) {
				btn_more.setTextColor(getResources().getColor(
						R.color.half_white));
				btn_more.setEnabled(false);
			} else {
				sendgroupmsg();
			}
			break;

		default:
			break;
		}
	}

	@Override
	protected int getContentView() {
		return R.layout.activity_forword;
	}

	@Override
	protected void initData() {
		if (application.getMemberEntity() != null) {
			uAccountID = application.getMemberEntity().getuAccountID();
		}

		msgtexts = getIntent().getStringExtra("msgtexts");
		type = getIntent().getStringExtra("type");
		friendinfolist();
		btn_more.setOnClickListener(this);
		btn_call.setOnClickListener(this);
	}

	@Override
	protected void setData() {
		lv_friends.setOnItemClickListener(new OnItemClickListener() {
			@Override
			public void onItemClick(AdapterView<?> parent, View view,
					int position, long id) {
				strNickName = friendsAdapter.getList().get(position).get_nickname();
				uTargetID = friendsAdapter.getList().get(position).get_userid();
				final Dialog mDialog = new Dialog(ForwardActivity.this,
						R.style.CustomDialogTheme);
				// 获取要填充的布局
				View v = LayoutInflater.from(getApplicationContext()).inflate(
						R.layout.forword_dialog, null);
				// 设置自定义的dialog布局
				mDialog.setContentView(v);
				// false表示点击对话框以外的区域对话框不消失，true则相反
				mDialog.setCanceledOnTouchOutside(true);
				if (isCheck) {
					mDialog.cancel();
					// barray[position] = true;
					// arraylist.add(uTargetID);
					// arraylist.remove(uTargetID);
				} else {
					mDialog.show();
				}
				// 获取自定义dialog布局控件
				Button confirmBt = (Button) v.findViewById(R.id.bt_send);
				Button cancelBt = (Button) v.findViewById(R.id.bt_cancel);
				TextView tv_name = (TextView) v.findViewById(R.id.tv_name);
				tv_name.setText(strNickName);
				// 确定按钮点击事件
				confirmBt.setOnClickListener(new OnClickListener() {

					@Override
					public void onClick(View v) {
						Intent intent = new Intent();
						intent.putExtra("strNickName", strNickName);
						intent.putExtra("msgtexts", msgtexts);
						intent.putExtra("uTargetID", String.valueOf(uTargetID));
						intent.putExtra("type", type);
						intent.setClass(ForwardActivity.this,
								ChattingActivity.class);
						startActivity(intent);
						finish();
						mDialog.dismiss();
					}
				});
				// 取消按钮点击事件
				cancelBt.setOnClickListener(new OnClickListener() {

					@Override
					public void onClick(View v) {
						mDialog.dismiss();
					}
				});

			}
		});
	}

	private void friendinfolist() {
//		List<FriendInfo> list = AppData.getFriendList(uAccountID);
//		if (list != null && lv_friends != null) {
//			friendsAdapter = new FriendsAdapter(this, list, handler);
//			lv_friends.setAdapter(friendsAdapter);
//		}
	}

	// 群发消息
	private void sendgroupmsg() {
		// loadSendingMsg();
//		if (type.equals("")) {
//			sendMessage = new SendMessage();
//			sendMessage.setmSenderClientType(3);
//			sendMessage.setmSenderID(uAccountID);
//			// sendMessage.setmTargetID(Integer.valueOf(uTargetID));
//			sendMessage.setmMsgID("groupmsg");
//			sendMessage.setmMsgType(1);
//			long epoch = System.currentTimeMillis() / 1000;
//			sendMessage.setmMsgTime(epoch);
//			List<Object> obj = new ArrayList<Object>();
//			msgtexts = msgtexts.replace("||", "|");
//			String[] ss = msgtexts.split("\\|");
//			for (int i = 0; i < ss.length; i++) {
//				Map<String, Object> map = new HashMap<String, Object>();
//				String id = ss[i];
//				if (id.startsWith("[") && id.endsWith("]")) {
//					String faceId = id.substring(1, id.length() - 1);
//					map.put("faceID", Integer.valueOf(faceId));
//
//				} else {
//					map.put("msgText", id);
//				}
//
//				obj.add(map);
//
//			}
//			sendMessage.setmContent(obj);
//			talkmsg = JSON.toJSONString(sendMessage);
//			tms.User.MultiTalkMsg.Builder sysMultiTalkMsg = tms.User.MultiTalkMsg
//					.newBuilder();
//
//			for (int i = 0; i < arraylist.size(); i++) {
//				sysMultiTalkMsg.addNodes((Integer) arraylist.get(i));
//				// 保存数据库
//				try {
//					messages = new ChatMessage();
//					messages.setSenderID(uAccountID);
//					messages.setmMsgType(1);
//					// uMsgID = tTalkMsgAns.getUMsgID() + 1;
//					messages.setMsgSenderClientType(1);
//					messages.setTargetID((Integer) arraylist.get(i));// haoyou
//					messages.setMsgTime(epoch);
//					messages.setMsgState(1);
//
//					List<ContentText> cons = new ArrayList<ContentText>();
//					ContentText ct = new ContentText();
//					ct.setMsgText(msgtexts);
//					cons.add(ct);
//					messages.setmContent(cons);
//					BaseActivity.getDb().save(messages);
//				} catch (DbException e) {
//					e.printStackTrace();
//				}
//			}
//			tms.User.TalkMsg.Builder systalkmsg = tms.User.TalkMsg.newBuilder();
//			systalkmsg.setMsg(com.google.protobuf.ByteString
//					.copyFromUtf8(talkmsg));
//			systalkmsg.setUTargetID(0);
//			systalkmsg.setUSendID(uAccountID);
//			systalkmsg.setUMsgID(-1);
//			sysMultiTalkMsg.setObjs(systalkmsg);
//			//con.sendProto(tms.Base.cmd.n_MultiTalkMsg_VALUE, sysMultiTalkMsg.build());
//		} else {
//			if (msgtexts.startsWith("([") && msgtexts.endsWith("])")) {
//				String pics = msgtexts.substring(2, msgtexts.length() - 2);
//				String pic = pics.replace("\"", "");
//				String[] ssa = pic.split("\\,");
//				String name = ssa[0];
//				String uFilesize = ssa[2];
//
//				String strPath = "/sdcard/org.org.hootina/" + name;
//				bitmap = PictureUtil.decodePicFromFullPath(strPath);
//				final File file = saveMyBitmap(bitmap, name);
//				String strChecksum = MyMD5.getMD5(file);
//				// String newFileName = uFilesize + strChecksum +
//				// ".jpg";
//				String newFileName = name;
//				//contwo.sendFileUpInfo(newFileName, strChecksum, 0, Long.parseLong(uFilesize));
//				sendPicture(newFileName, strChecksum, 0,
//						Long.parseLong(uFilesize));
//			}
//
//		}

	}

	@Override
	public void processMessage(Message msg) {
		super.processMessage(msg);
//		if (msg.what == MegAsnType.ToTalkingmsg) {
//			TalkMsgAns tTalkMsgAns = (TalkMsgAns) msg.obj;
//			if (tTalkMsgAns.getUError() == 0) {
//				chatSession = new ChatSession();
//				for (int i = 0; i < arraylist.size(); i++) {
//					uTargetID = (Integer) arraylist.get(i);
//					strNickName = (String) namelist.get(i);
//					chatSession.setmNickName(strNickName);
//					chatSession.setmLastMsgText(msgtexts);
//					chatSession.setmFriendID(uTargetID);
//					chatSession.setmSelfID(uAccountID);
//					try {
//						infos = BaseActivity.getDb().findAll(
//								Selector.from(ChatSession.class).where(
//										"uAccountID", "=", uAccountID));
//						if (infos != null) {
//							for (int n = 0; n < infos.size(); n++) {
//								if (infos.get(n).getmFriendID() == Integer
//										.valueOf(uTargetID)) {
//									BaseActivity.getDb().delete(infos.get(n));
//									break;
//								}
//							}
//						}
//						BaseActivity.getDb().save(chatSession);
//						//AppData.updateChatSessionByFriendID(chatSession);
//						updateMessageState(1, null);
//					} catch (DbException e) {
//						e.printStackTrace();
//					}
//				}
//
//			}
//			isCheck = false;
//			isChoice = false;
//			arraylist.clear();
//			AppData.clearsession();
//			finish();
//
//		} else if (msg.what == MegAsnType.Topicturemsg) {
//			FileLoadInfo fileLoad = (FileLoadInfo) msg.obj;
//			String name = fileLoad.getStrName().toStringUtf8();
//			// String path = "/sdcard/org.org.hootina/" + name;
//			// File file = new File(path);
//
//			int uError = 0;
//			int uOffset = fileLoad.getUOffset();
//			int uDownsize = fileLoad.getUDownsize();
//			PictureUtil.topictrue(name, uError, uOffset, uDownsize);
//		}
//		else if (msg.what == MegAsnType.FileUpData) {
//			// 上传图片成功
//			FileUpInfo fileUpInfo = (FileUpInfo) msg.obj;
//			String name = fileUpInfo.getStrName().toStringUtf8();
//			int uFilesize = fileUpInfo.getUFilesize();
//
//			// 更新消息状态
//			// updateMessageState(2, name);
//
//			// int TargetID = Integer.parseInt(uTargetID);
//			sendMessage = new SendMessage();
//			sendMessage.setmSenderClientType(3);
//			sendMessage.setmMsgID("groupmsg");
//			sendMessage.setmMsgType(5);
//			long epoch = System.currentTimeMillis() / 1000;
//			sendMessage.setmMsgTime(epoch);
//			List<Object> obj = new ArrayList<Object>();
//			Map<String, Object> map = new HashMap<String, Object>();
//
//			int nWidth = 0;
//			int nHeight = 0;
//			File file = new File("/org.org.hootina/" + name);
//			if (file.exists()) {
//				BitmapFactory.Options opts = new BitmapFactory.Options();
//				opts.inPreferredConfig = Bitmap.Config.RGB_565;
//				opts.inJustDecodeBounds = true;
//				BitmapFactory.decodeFile("/sdcard/org.org.hootina/" + name, opts);
//				nWidth = opts.outWidth;
//				nHeight = opts.outHeight;
//			}
//
//			Object[] strArray = { name, fileUpInfo.getStrUrl(), uFilesize,
//					nWidth, nHeight };
//			map.put("pic", strArray);
//			obj.add(map);
//			sendMessage.setmContent(obj);
//			talkmsg = JSON.toJSONString(sendMessage);
//			// con.sendmsg(uSendID, TargetID, uMsgID, talkmsg);
//			int uMsgID = -1;
//			tms.User.MultiTalkMsg.Builder sysMultiTalkMsg = tms.User.MultiTalkMsg
//					.newBuilder();
//			for (int i = 0; i < arraylist.size(); i++) {
//				sysMultiTalkMsg.addNodes((Integer) arraylist.get(i));
////				chatSession.setmNickName(strNickName);
//				chatSession =new ChatSession();
//				chatSession.setmLastMsgText(talkmsg);
//				int nTargetID = (Integer) arraylist.get(i);
//				chatSession.setmFriendID(nTargetID);
//				chatSession.setmSelfID(uAccountID);
////				try {
////					infos = BaseActivity.getDb().findAll(
////							Selector.from(ChatSession.class).where(
////									"uAccountID", "=",
////									application.getMemberEntity().getuAccountID()));
////					if (infos != null) {
////						for (int n = 0; i < infos.size(); n++) {
////							if (infos.get(n).getmFriendID() == nTargetID) {
////								db.delete(infos.get(n));
////								break;
////							}
////						}
////					}
////					BaseActivity.getDb().save(chatSession);
////				} catch (DbException e) {
////					e.printStackTrace();
////				}
//			}
//
//			tms.User.TalkMsg.Builder systalkmsg = tms.User.TalkMsg.newBuilder();
//			systalkmsg.setMsg(com.google.protobuf.ByteString
//					.copyFromUtf8(talkmsg));
//			systalkmsg.setUTargetID(0);
//			systalkmsg.setUSendID(uAccountID);
//			systalkmsg.setUMsgID(-1);
//			sysMultiTalkMsg.setObjs(systalkmsg);
//			//con.sendProto(tms.Base.cmd.n_MultiTalkMsg_VALUE, sysMultiTalkMsg.build());
//
//			// con.sendmsg(uAccountID, TargetID, uMsgID, talkmsg);
////			chatSession.setmNickName(strNickName);
////			chatSession.setmLastMsgText(talkmsg);
////			int nTargetID = Integer.valueOf(uTargetID);
////			chatSession.setmFriendID(nTargetID);
////			chatSession.setmSelfID(uAccountID);
////			try {
////				infos = BaseActivity.getDb().findAll(
////						Selector.from(ChatSession.class).where(
////								"uAccountID", "=",
////								application.getMemberEntity().getmSelfID()));
////				if (infos != null) {
////					for (int i = 0; i < infos.size(); i++) {
////						if (infos.get(i).getmFriendID() == nTargetID) {
////							db.delete(infos.get(i));
////							break;
////						}
////					}
////				}
////				BaseActivity.getDb().save(chatSession);
////			} catch (DbException e) {
////				e.printStackTrace();
////			}
//		}

	}

	// 0:没有消息, 1:有新文本, 2:有新图片, 3:断网
	private void updateMessageState(int hasNewMsg, String fileName) {
		// 最近10秒的消息设置为发送成功
//		try {
//			List<ChatMessage> tis = mChatMessages;
//
//			for (int i = 0; i < mChatMessages.size(); ++i) {
//				ChatMessage msg = mChatMessages.get(i);
//				if (msg == null) {
//					mChatMessages.remove(i);
//					i--;
//					continue;
//				}
//				if (msg.getMsgState() != 0) {
//					mChatMessages.remove(i);
//					i--;
//					continue;
//				}
//			}
//
//			if (tis != null && tis.size() > 0) {
//				long epoch = System.currentTimeMillis() / 1000;
//
//				int nRefreshCount = 0;
//				for (int i = 0; i < tis.size(); ++i) {
//					ChatMessage msg = tis.get(i);
//					if (msg.getMsgState() == 0) {
//						if (hasNewMsg == 3) {
//							msg.setMsgState(2);
//							BaseActivity.getDb().update(msg);
//							nRefreshCount++;
//						} else {
//							// 文本
//							if (msg.getmMsgType() != 2 && msg.getmMsgType() != 5) {
//								if (msg.getMsgTime() + 10 <= epoch) {
//									msg.setMsgState(2);
//									BaseActivity.getDb().update(msg);
//									nRefreshCount++;
//								}
//								// 有新消息且不是图片， 则设置最近发送为成功
//								else if (hasNewMsg == 1) {
//									msg.setMsgState(1);
//									BaseActivity.getDb().update(msg);
//									nRefreshCount++;
//								}
//							} else {
//								if (msg.getMsgState() == 0
//										&& msg.getMsgTime() + 150 <= epoch) {
//									msg.setMsgState(2);
//									BaseActivity.getDb().update(msg);
//									nRefreshCount++;
//								}
//
//								if (fileName == null) {
//									continue;
//								}
//								if (msg.getmMsgText().indexOf(fileName) > 0) {
//									msg.setMsgState(1);
//									BaseActivity.getDb().update(msg);
//									nRefreshCount++;
//								}
//
//							}
//						}
//					}
//
//				}
//
//				if (hasNewMsg == 0 && nRefreshCount > 0) {
//					// messagelist();
//				}
//			}
//		} catch (DbException e) {
//			e.printStackTrace();
//		}
	}

	// 相册或相机发送图片
	public void sendpictrue(String newFileName, String strChecksum,
			int uDownsize, long uFilesize) {

//		// int TargetID = Integer.parseInt(uTargetID);
//		sendMessage = new SendMessage();
//		sendMessage.setmSenderClientType(3);
//		// sendMessage.setmSenderID(uSendID);
//		// sendMessage.setmTargetID(TargetID);
//		sendMessage.setmMsgID("groupmsg");
//		sendMessage.setmMsgType(5);
//		long epoch = System.currentTimeMillis() / 1000;
//		sendMessage.setmMsgTime(epoch);
//		List<Object> obj = new ArrayList<Object>();
//		Map<String, Object> map = new HashMap<String, Object>();
//
//		Object[] strArray = { newFileName, strChecksum, uFilesize, 0, 0 };
//		map.put("pic", strArray);
//		obj.add(map);
//		sendMessage.setmContent(obj);
//		String talkmsgaa = JSON.toJSONString(sendMessage);
//
//		for (int i = 0; i < arraylist.size(); i++) {
//			int uUserId = (Integer) arraylist.get(i);
//
//			try {
//				messages = new ChatMessage();
//				messages.setSenderID(uAccountID);// haoyou
//				messages.setmMsgID("");
//				messages.setmMsgType(5);
//				messages.setTargetID(uUserId);
//				messages.setMsgSenderClientType(3);
//				messages.setMsgTime(epoch);
//				List<ContentText> cons = new ArrayList<ContentText>();
//				ContentText ct = new ContentText();
//				String talkmsges = talkmsgaa.substring(
//						talkmsgaa.indexOf("[{\"pic\":["),
//						talkmsgaa.indexOf("]}]"));
//				talkmsges = talkmsges.replace("[{\"pic\":[", "[");
//				String talkmsger = talkmsges + "]";
//				ct.setPic(talkmsger);
//				cons.add(ct);
//				messages.setmContent(cons);
//				BaseActivity.getDb().save(messages);
//			} catch (DbException e) {
//				e.printStackTrace();
//			}
//		}
	}

	public File saveMyBitmap(Bitmap mBitmap, String bitName) {
		File f = new File("/sdcard/org.org.hootina/" + bitName);
		if (f.exists()) {
			f.delete();
		}
		FileOutputStream fOut = null;
		try {
			fOut = new FileOutputStream(f);
		} catch (FileNotFoundException e) {
			e.printStackTrace();
		}

		ByteArrayOutputStream out = new ByteArrayOutputStream();
		mBitmap.compress(Bitmap.CompressFormat.JPEG, 100, out);
		int options = 100;
		while (out.toByteArray().length / 1024 > 200 && options >= 50) {
			options -= 10;
			out.reset();
			mBitmap.compress(Bitmap.CompressFormat.JPEG, options, out);
		}

		try {
			fOut.write(out.toByteArray());
			fOut.flush();
		} catch (IOException e) {
			e.printStackTrace();
		}
		try {
			fOut.close();
		} catch (IOException e) {
			e.printStackTrace();
		}
		return f;
	}
}
