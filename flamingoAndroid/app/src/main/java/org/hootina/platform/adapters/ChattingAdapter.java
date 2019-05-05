package org.hootina.platform.adapters;

import android.content.Context;
import android.content.res.AssetManager;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.drawable.BitmapDrawable;
import android.graphics.drawable.Drawable;
import android.text.Html;
import android.text.Html.ImageGetter;
import android.text.Spannable;
import android.text.SpannableString;
import android.text.style.ImageSpan;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.BaseAdapter;
import android.widget.ImageView;
import android.widget.ProgressBar;
import android.widget.TextView;

import org.hootina.platform.R;
import org.hootina.platform.activities.member.ChattingActivity;
import org.hootina.platform.dialogs.MsgDialog;
import org.hootina.platform.model.GroupInfos;
import org.hootina.platform.result.ContentText;
import org.hootina.platform.result.MessageTextEntity;
import org.hootina.platform.services.Face;
import org.hootina.platform.userinfo.UserInfo;
import org.hootina.platform.userinfo.UserSession;
import org.hootina.platform.utils.FaceConversionUtil;
import org.hootina.platform.utils.HeadUtil;
import org.hootina.platform.utils.LoggerFile;
import org.hootina.platform.utils.PictureUtil;
import org.hootina.platform.utils.TimeUtil;
import org.hootina.platform.utils.UIUtils;
import org.hootina.platform.widgets.CircularImage;

import com.bumptech.glide.Glide;
import com.lidroid.xutils.DbUtils;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.util.Date;
import java.util.List;

public class ChattingAdapter extends BaseAdapter {
    private static final String     LOG_TAG = "ChattingAdapterTag";

    private Context                 mContext;
    private List<MessageTextEntity> mChatMessages;
    private DbUtils                 db;
    private String                  drawablename;
    //private Date                  date;
    Bitmap                          bm;
    private int                     mnAccountID;
    private int                     position;
    private MsgDialog               mDialog;
    //private int					mResourceID;

    public ChattingAdapter(Context context, List<MessageTextEntity> messages) {
        super();
        mContext = context;
        mChatMessages = messages;
        mDialog = new MsgDialog(context);
    }

    public void setChatMessages(List<MessageTextEntity> chatMessages) {
        mChatMessages = chatMessages;
    }

    @Override
    public int getCount() {
        if (mChatMessages == null)
            return 0;

        return mChatMessages.size();
    }

    @Override
    public Object getItem(int position) {
        return mChatMessages.get(position);
    }

    @Override
    public long getItemId(int position) {
        return position;
    }

    @Override
    public View getView(int position, View convertView, ViewGroup parent) {
        MessageTextEntity msgItem = mChatMessages.get(position);
        int msgType = msgItem.getMsgType();
        int senderID = msgItem.getSenderID();
        int targetID = msgItem.getTargetID();
        int selfID = UserSession.getInstance().loginUser.get_userid();
        if (msgType == MessageTextEntity.CONTENT_TYPE_TEXT) {
            if (senderID == selfID)
                convertView = LayoutInflater.from(mContext).inflate(R.layout.raw_send_message, null);
            else
                convertView = LayoutInflater.from(mContext).inflate(R.layout.raw_received_message, null);
        } else if (msgType == MessageTextEntity.CONTENT_TYPE_IMAGE_CONFIRM ||
                   msgType == MessageTextEntity.CONTENT_TYPE_MOBILE_IMAGE) {
            ImageView img,headImg;
            if (senderID == selfID)
                convertView = LayoutInflater.from(mContext).inflate(R.layout.picture_right, null);
            else
                convertView = LayoutInflater.from(mContext).inflate(R.layout.picture_left, null);

            img = (ImageView) convertView.findViewById(R.id.img);
            headImg = (ImageView) convertView.findViewById(R.id.img_head);
            Glide.with(convertView.getContext())
                    .load(new File(mChatMessages.get(position).getImgFile()))
                    .into(img);

            //好友发送的消息
            if (senderID != selfID) {
//				List<UserInfo> storedUserList = null;
//
//				try {
//
//					if (isGroup(targetID)) {
//						storedUserList = BaseActivity.getDb().findAll(
//								Selector.from(UserInfo.class)
//										.where("mTargetID", "=", targetID)
//										.and(WhereBuilder.b("mSenderID", "=", senderID)));
//					} else {
//						storedUserList = BaseActivity.getDb().findAll(
//								Selector.from(UserInfo.class)
//										.where("mTargetID", "=", targetID)
//										.and(WhereBuilder.b("mSenderID", "=", senderID)));
//					}
//				} catch (DbException e) {
//					e.printStackTrace();
//				}

                //UserInfo friendInfo = UserSession.getInstance().getUserInfoById(senderID);

                //MyDbUtil.getContactsDao().load((long) senderID);

                Integer faceID;
                if (UserInfo.isGroup(senderID))
                    faceID = HeadUtil.get(targetID);
                else
                    faceID = HeadUtil.get(senderID);

                if (faceID == null) {
                    Log.e(LOG_TAG, "line 156, HeadUtil.get error, targetID: " + targetID + ", senderID: " + senderID);
                    faceID = 0;
                }

                Glide.with(convertView.getContext())
                            .load("file:///android_asset/head" + faceID + ".png")
                            .into(headImg);
            } else {
                Bitmap bmp = PictureUtil.getHeadPic(mContext.getAssets(), UserSession.getInstance().loginUser);
                if (bmp != null)
                    headImg.setImageBitmap(bmp);
            }

            return convertView;
        } else {
            //TODO: 万一出现这种情况怎么办？
            LoggerFile.LogError("Illegal msg type:" + msgType);
        }

        final ViewHolder holder = new ViewHolder();
        long msgTimeLong = msgItem.getMsgTime() * 1000;
        Date msgTime = new Date(msgTimeLong);
        String msgTimeStr = TimeUtil.getFormattedTimeString(msgTime);

        holder.time = (TextView) convertView.findViewById(R.id.timestamp);
        holder.time.setText(msgTimeStr);
        holder.img = (CircularImage) convertView.findViewById(R.id.iv_userhead);
        holder.msg_state = (ImageView) convertView.findViewById(R.id.msg_status);
        holder.tv_name = (TextView) convertView.findViewById(R.id.tv_name);
        holder.text = (TextView) convertView.findViewById(R.id.tv_chatcontent);
        holder.iv_sendPicture = (ImageView) convertView.findViewById(R.id.iv_sendPicture);
        holder.tvSenderName = (TextView) convertView.findViewById(R.id.tv_window_title);

        //群消息显示发消息人的用户昵称
        if (UserInfo.isGroup(targetID)) {
            String senderName = UserSession.getInstance().getGroupMemberNickname(targetID, senderID);
            holder.tvSenderName.setText(senderName);
        }

        ChattingActivity chatActivity = (ChattingActivity) mContext;
        if (chatActivity != null) {
            if (UserInfo.isGroup(chatActivity.getTargetID())) {
                String userName = UserSession.getInstance().getNicknameById(senderID);
                if (holder.tv_name != null) {
                    holder.tv_name.setVisibility(View.VISIBLE);
                    holder.tv_name.setText(userName);
                }
            }
        }

        holder.bar = (ProgressBar) convertView.findViewById(R.id.pb_sending);

        //正在发送和发送标识是否显隐藏
        int nFlag1 = View.GONE;
        int nFlag2 = View.GONE;
//		if (message.getmMsgState() == 0) {
//			nFlag1 = View.GONE;
//			nFlag2 = View.VISIBLE;
//		} else if (message.getmMsgState() == 1) {
//			nFlag1 = View.GONE;
//			nFlag2 = View.GONE;
//		} else {
//			nFlag1 = View.VISIBLE;
//			nFlag2 = View.GONE;
//		}

        if (holder.msg_state != null) {
            holder.msg_state.setVisibility(nFlag1);
        }
        if (holder.bar != null) {
            holder.bar.setVisibility(nFlag2);
        }

        //好友发送的消息
        if (senderID != selfID) {
//				List<UserInfo> storedUserList = null;
//
//				try {
//
//					if (isGroup(targetID)) {
//						storedUserList = BaseActivity.getDb().findAll(
//								Selector.from(UserInfo.class)
//										.where("mTargetID", "=", targetID)
//										.and(WhereBuilder.b("mSenderID", "=", senderID)));
//					} else {
//						storedUserList = BaseActivity.getDb().findAll(
//								Selector.from(UserInfo.class)
//										.where("mTargetID", "=", targetID)
//										.and(WhereBuilder.b("mSenderID", "=", senderID)));
//					}
//				} catch (DbException e) {
//					e.printStackTrace();
//				}

            //UserInfo friendInfo = UserSession.getInstance().getUserInfoById(senderID);

            //MyDbUtil.getContactsDao().load((long) senderID);

            Integer faceID;
            if (UserInfo.isGroup(senderID))
                faceID = HeadUtil.get(targetID);
            else
                faceID = HeadUtil.get(senderID);

            if (faceID == null) {
                Log.e(LOG_TAG, "line 267, HeadUtil.get error, targetID: " + targetID + ", senderID: " + senderID);
                faceID = 0;
            }

            Glide.with(convertView.getContext())
                            .load("file:///android_asset/head" + faceID + ".png")
                            .into(holder.img);


            //if (storedUserList != null && storedUserList.size() > 0) {
//                Bitmap bmp = PictureUtil.getFriendHeadPic(mContext.getAssets(), friendInfo);
//                if (bmp != null) {
//                    holder.img.setImageBitmap(bmp);
//                }
            //}

            //自己发送的消息
        } else {
            Bitmap bmp = PictureUtil.getHeadPic(mContext.getAssets(), UserSession.getInstance().loginUser);
            if (bmp != null) {
                holder.img.setImageBitmap(bmp);
            }
        }

        //idd: [{"msgText":"dfg"}]
        //idd: |[1]|
        Face f;
        List<ContentText> c = msgItem.getContent();
        if (c != null) {
            for (int i = 0; i < c.size(); ++i) {
                ContentText t = c.get(i);
                if (t == null)
                    continue;

                if (t.getFaceID() != Face.DEFAULT_NULL_FACEID) {
                    //String html = "<img src='" + drawablename + "'/>";
                    String html = "<img src='" + "face" + t.getFaceID() + ".png'/>";
                    ImageGetter imgGetter = new ImageGetter() {

                        @Override
                        public Drawable getDrawable(String source) {
                            Bitmap b = getImageFromAssetsFile(source);
                            Drawable drawable = new BitmapDrawable(b);
                            drawable.setBounds(0, 0, 56, 56);
                            //drawable.setBackgroundColor(Color.TRANSPARENT);
                            return drawable;
                            // 获取到资源id
                            //int id = Integer.parseInt("14");
                            //int id = mContext.getResources().getIdentifier(source , "drawable", mContext.getPackageName());
//						Drawable drawable = mContext.getResources().getDrawable(R.drawable.face0);
//						//Drawable drawable = Drawable.createFromPath(source);
//						drawable.setBounds(0, 0, drawable.getIntrinsicWidth(), drawable.getIntrinsicHeight());
//						return drawable;

                        }
                    };
                    CharSequence charSequence = Html.fromHtml(html, imgGetter, null);

//				getImageFromAssetsFile("face" + t.getFaceID() + ".png"
                    String faceIDStr = String.valueOf(t.getFaceID());
                    ImageSpan imgSpan = new ImageSpan(holder.text.getContext(), FaceConversionUtil.getInstace().getEmojeId(faceIDStr));
                    SpannableString spannableString = new SpannableString(" ");
                    spannableString.setSpan(imgSpan, 0, 1, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);

                    if (charSequence != null)
                        holder.text.append(spannableString);
                } else {
                    if (t.getMsgText() != null) {
                        holder.text.append(t.getMsgText());
                    }

                }
            }
        }

//		String idd = msgItem.getMsgText();
//		String[] ss = idd.split("\\|");
//		if (msgType == ChatMessage.CONTENT_TYPE_TEXT) {
//			for (int i = 0; i < ss.length; i++) {
//				String id = ss[i];
//				if (id.startsWith("[") && id.endsWith("]")) {
//					String faceId = id.substring(1, id.length() - 1);
//					db = DbUtils.create(mContext);
//					try {
//						List<Face> list = db.findAll(Selector.from(Face.class)
//								.where("faceid", "=", faceId));
//						if (list != null) {
//							for (int f = 0; f < list.size(); f++) {
//								String fid = list.get(f).getFaceid();
//								if (fid.equals(faceId)) {
//									drawablename = list.get(f).getFile();
//
//								}
//							}
//						}
//					} catch (DbException e) {
//						e.printStackTrace();
//					}
//					String html = "<img src='" + drawablename + "'/>";
//					ImageGetter imgGetter = new ImageGetter() {
//
//						@Override
//						public Drawable getDrawable(String source) {
//							Bitmap b = getImageFromAssetsFile(source);
//							Drawable d = new BitmapDrawable(b);
//							d.setBounds(0, 0, 60, 60);
//							return d;
//						}
//					};
//					CharSequence charSequence = Html.fromHtml(html, imgGetter,
//							null);
//
//					holder.text.append(charSequence);
//				} else if (id.startsWith("([") && id.endsWith("])")) {
//					holder.text.append("");
//
//				} else {
//					holder.text.append(id);
//				}
//			}
//			holder.text.setTag(position);
//			holder.text.setOnLongClickListener(new OnLongClickListener() {
//
//				@Override
//				public boolean onLongClick(View v) {
//					int position = (Integer) holder.text.getTag();
//					int uAccountID = mChatMessages.get(position).getTargetID();
//					int uSendID = mChatMessages.get(position).getSenderID();
//					int id = mChatMessages.get(position).getId();
//					String text = mChatMessages.get(position).getMsgText();
//					String typ = "";
//					MsgDialog.setpostion(position, uAccountID, uSendID, id, text, typ);
//					mDialog.show();
//					return false;
//				}
//			});
//
//		} else if (msgType == ChatMessage.CONTENT_TYPE_IMAGE_CONFIRM ||
//					msgType == ChatMessage.CONTENT_TYPE_MOBILE_IMAGE) {
//			if (msgItem.getMsgBitmap() == null) {
//				String a = idd.substring(2, idd.length() - 2);
//				String[] sss = a.split("\\,");
//				String name = sss[0];
//				name = name.replaceAll("\"", "");
//
//				bm = null;
//				if (senderID == targetID) // 自己发送的图，
//																		// 直接取本地图片
//				{
//
//					bm = PictureUtil.decodePicFromFullPath("/sdcard/flamingo/" + name);
//
//				} else {
//					String servername = sss[1];
//					servername = servername.replaceAll("\"", "");
//					String imge = "/sdcard/flamingo" + name;
//					File file = new File(imge);
//					if (file.exists()) {
//						String picWidth = sss[3];
//						String picHeight = sss[4];
//
//						bm = PictureUtil.decodePicFromFile(name, 1);
//					} else {
//						PictureUtil.loadfile(name, servername);
//					}
//				}
//
//				if (bm != null)
//					msgItem.setMsgBitmap(bm);
// 				else
//					holder.iv_sendPicture.setBackgroundResource(R.drawable.arrow_down);
//			}
//			holder.iv_sendPicture.setImageBitmap(msgItem.getMsgBitmap());
//			holder.iv_sendPicture.setTag(position);
//
//			holder.iv_sendPicture
//					.setOnLongClickListener(new OnLongClickListener() {
//
//						@Override
//						public boolean onLongClick(View v) {
//							int position = (Integer) holder.iv_sendPicture
//									.getTag();
//							int uAccountID = mChatMessages.get(position).getTargetID();
//							int uSendID = mChatMessages.get(position).getSenderID();
//							int id = mChatMessages.get(position).getId();
//							String text = mChatMessages.get(position).getmMsgText();
//							String typ = "2";
//							MsgDialog.setpostion(position, uAccountID, uSendID,
//									id, text, typ);
//							mDialog.show();
//
//							return true;
//						}
//					});
//
//			holder.iv_sendPicture.setOnClickListener(new OnClickListener() {
//
//				@Override
//				public void onClick(View v) {
//					// TODO Auto-generated method stub
//					// 加载popupWindow的布局文件
//					View contentView = LayoutInflater.from(mContext).inflate(
//							R.layout.popup, null);
//					// 设置popupWindow的背景颜色
//					contentView.setBackgroundColor(Color.BLACK);
//					// 声明一个弹出框
//					final PopupWindow popupWindow = new PopupWindow(
//							contentView, LayoutParams.FILL_PARENT,
//							LayoutParams.FILL_PARENT);
//					popupWindow.setFocusable(true);
//					popupWindow.setOutsideTouchable(true);
//
//					popupWindow.setBackgroundDrawable(new PaintDrawable());
//
//					TextView tv = (TextView) contentView.findViewById(R.id.tv);
//					popupWindow.showAtLocation(tv, Gravity.CENTER_VERTICAL, 0,
//							0);
//					final ImageView imageView = (ImageView) contentView
//							.findViewById(R.id.logo_b);
//					int width = ScreenUtils.getScreenWidth(mContext);
//					int height = ScreenUtils.getScreenHeight(mContext);
//					int postions = (Integer) v.getTag();
//					// bm=(Bitmap) v.getTag();
//					bm = mChatMessages.get(postions).getMsgBitmap();
//					// 将图片设置为宽100，高200，在这儿就可以实现图片的大小缩放
//					if (bm == null) {
//						return;
//					}
//					int bw = bm.getWidth();
//					int bh = bm.getHeight();
//					if (bw > width) {
//						bw = width;
//					}
//					if (bh > height) {
//						bh = height;
//					}
//					double scale;
//					double scalew = width / bw;
//					double scaleh = height / bh;
//					Double obj1 = new Double(scalew);
//					Double obj2 = new Double(scaleh);
//					int retval = obj1.compareTo(obj2);
//					if (retval >= 0) {
//						scale = scaleh;
//					} else {
//						scale = scalew;
//					}
//
//					Bitmap resize = Bitmap.createScaledBitmap(bm, bw, bh, true);
//					Matrix m = new Matrix();
//					float scaleWidth = (float) (1 * scale);
//					float scaleHeight = (float) (1 * scale);
//					m.postScale(scaleWidth, scaleHeight);
//					// 做好旋转与大小之后，重新创建位图，0-width宽度上显示的区域，高度类似
//					Bitmap b = Bitmap.createBitmap(resize, 0, 0, bw, bh, m,
//							true);
//					// 显示图片
//					imageView.setImageBitmap(b);
//					imageView.setOnClickListener(new OnClickListener() {
//
//						@Override
//						public void onClick(View v) {
//							// TODO Auto-generated method stub
//							popupWindow.dismiss();
//						}
//					});
//
//				}
//			});
//		}

        return convertView;
    }

    // private static int calculateInSampleSize(BitmapFactory.Options options,
    // int reqWidth, int reqHeight) {
    // // raw height and width of image
    // final int height = options.outHeight;
    // final int width = options.outWidth;
    //
    // int initSize = 1;
    // if (height > reqHeight || width > reqWidth) {
    // if (width > height) {
    // initSize = Math.round((float) height / (float) reqHeight);
    // } else {
    // initSize = Math.round((float) width / (float) reqWidth);
    // }
    // }
    //
    // /*
    // * the function rounds up the sample size to a power of 2 or multiple of
    // * 8 because BitmapFactory only honors sample size this way. For
    // * example, BitmapFactory down samples an image by 2 even though the
    // * request is 3.
    // */
    // int roundedSize;
    // if (initSize <= 8) {
    // roundedSize = 1;
    // while (roundedSize < initSize) {
    // roundedSize <<= 1;
    // }
    // } else {
    // roundedSize = (initSize + 7) / 8 * 8;
    // }
    //
    // return roundedSize;
    // }


    private Bitmap getImageFromAssetsFile(String fileName) {
        Bitmap image = null;
        AssetManager am = mContext.getResources().getAssets();
        try {
            BitmapFactory.Options opts = new BitmapFactory.Options();
            opts.inPreferredConfig = Bitmap.Config.ARGB_8888;

            InputStream is = am.open(fileName);
            image = BitmapFactory.decodeStream(is, null, opts);
            is.close();
        } catch (IOException e) {
            e.printStackTrace();
        }

        return image;
    }

    static class ViewHolder {
        TextView      time;
        TextView      text;
        TextView      tv;
        TextView      tv_name;
        CircularImage img;                        //头像
        ImageView     iv_sendPicture;
        ImageView     msg_state;
        ProgressBar   bar;
        TextView      tvSenderName;               //发消息人昵称
    }
}
