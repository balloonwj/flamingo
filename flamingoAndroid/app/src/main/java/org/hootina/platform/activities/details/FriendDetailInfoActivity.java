package org.hootina.platform.activities.details;

import java.io.File;

import android.annotation.SuppressLint;
import android.app.Dialog;
import android.app.ActionBar.LayoutParams;
import android.content.Intent;
import android.graphics.Bitmap;
import android.os.Message;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.TextView;
import android.widget.Toast;

import org.hootina.platform.R;
import org.hootina.platform.activities.BaseActivity;
import org.hootina.platform.activities.MainActivity;
import org.hootina.platform.activities.member.ChattingActivity;
import org.hootina.platform.enums.MsgType;
import org.hootina.platform.net.ChatSessionMgr;
import org.hootina.platform.net.NetWorker;
import org.hootina.platform.userinfo.UserInfo;
import org.hootina.platform.userinfo.UserSession;
import org.hootina.platform.utils.PictureUtil;

/*
 * 好友详细信息
 */
public class FriendDetailInfoActivity extends BaseActivity {

    private MainActivity mainActivity;
    private TextView tv_name;
    private TextView tv_num;
    private TextView tv_sign;
    private ImageView iv_head;
    private Button btn_send;
    private Button btn_detail;
    private Button btn_delete;
    private Button btn_cancel;
    private int _userid;
    private String _username;
    private String _signature;
    private String _nickname;
    private String msgtexts;
    private String type;
    private String picname;
    private File file;
    private byte[] contentIntleng;
    private String path;
    private int uAccountID;
    private Dialog dialog;

    @Override
    protected int getContentView() {
        return R.layout.activity_friend_details;
    }

    @Override
    public void onClick(View v) {
        switch (v.getId()) {
            case R.id.btn_back:
                finish();
                break;

            //启动聊天Activity
            case R.id.btn_send:
                Intent intent = new Intent(this, ChattingActivity.class);
                intent.putExtra("nickname", _nickname);
                intent.putExtra("friendid", _userid);
                intent.putExtra("msgtexts", "");
                intent.putExtra("type", "");
                startActivity(intent);
                break;

            case R.id.btn_detail:
                showDialog();
                break;

            case R.id.btn_sesseion:
                ChatSessionMgr.getInstance().deleteSession(_userid);
                BaseActivity.getChatMsgDb().deleteChatMsg(_userid);
                dialog.dismiss();
                finish();
                break;

            case R.id.btn_delete:
                // 删除好友
                NetWorker.deleteFriend(_userid);
                ChatSessionMgr.getInstance().deleteSession(_userid);
                BaseActivity.getChatMsgDb().deleteChatMsg(_userid);
                dialog.dismiss();

                NetWorker.getFriendList();

                //finish();
                break;
            case R.id.btn_cancel:
                // 取消
                dialog.dismiss();
                break;

            default:
                break;
        }

    }

    @Override
    protected void initData() {
        mainActivity = new MainActivity();
        _nickname = getIntent().getStringExtra("nickname");
        _userid = getIntent().getIntExtra("userid", 0);
        _username = getIntent().getStringExtra("username");
        _signature = getIntent().getStringExtra("signature");
        tv_name.setText(_nickname);
        tv_num.setText("账号:" + _username);
        tv_sign.setText(_signature);
    }

    @Override
    protected void setData() {
        //TODO: 先从Session中拿取
        if (_userid <= 0)
            return;

        UserInfo friendInfo = UserSession.getInstance().getUserInfoById(_userid);
        if (friendInfo == null)
            return;

        Bitmap bm = PictureUtil.getFriendHeadPic(getAssets(), friendInfo);
        if (bm != null) {
            iv_head.setImageBitmap(bm);
        }

        //TODO: 拿不到再从本地存储中拿
        //uAccountID = application.getMemberEntity().getmSelfID();
//		try {
//			List<UserInfo> list = BaseActivity.getDb().findAll(
//					Selector.from(FriendInfo.class)
//							.where("uAccountID", "=", uAccountID)
//							.and(WhereBuilder
//									.b("uTargetID", "=", uTargetID)));
//			if (list != null && list.size() > 0) {
//				UserInfo friendInfo = list.get(0);
//				Bitmap bm = null;
//				if (getAssets() != null) {
//					bm = PictureUtil.getFriendHeadPic(getAssets(), friendInfo);
//					if (bm != null) {
//						iv_head.setImageBitmap(bm);
//					}
//				}
//			}
//		} catch (DbException e) {
//			e.printStackTrace();
//		}

    }

    // 自定义相册对话框
    @SuppressLint("NewApi")
    private void showDialog() {
        View view = getLayoutInflater().inflate(R.layout.deletefriend_dialog,
                null);
        dialog = new Dialog(this, R.style.ActionSheetDialogAnimation);
        dialog.setContentView(view, new LayoutParams(LayoutParams.FILL_PARENT,
                LayoutParams.WRAP_CONTENT));
        Window window = dialog.getWindow();
        // 设置显示动画
        window.setWindowAnimations(R.style.popwin_down_up);
        WindowManager.LayoutParams wl = window.getAttributes();
        wl.x = 0;
        wl.y = getWindowManager().getDefaultDisplay().getHeight();
        // 以下这两句是为了保证按钮可以水平满屏
        wl.width = ViewGroup.LayoutParams.MATCH_PARENT;
        wl.height = ViewGroup.LayoutParams.WRAP_CONTENT;
        // 设置显示位置
        dialog.onWindowAttributesChanged(wl);
        // 设置点击外围解散
        dialog.setCanceledOnTouchOutside(true);
        dialog.show();
        View btn_sesseion = view.findViewById(R.id.btn_sesseion);
        btn_sesseion.setOnClickListener(this);
        View btn_cancel = view.findViewById(R.id.btn_cancel);
        btn_cancel.setOnClickListener(this);
        View btn_delete = view.findViewById(R.id.btn_delete);
        btn_delete.setOnClickListener(this);
    }

    @Override
    public void processMessage(Message msg) {
        super.processMessage(msg);

        if (msg.what == MsgType.msg_type_userstatuschange) {
            finish();
        } else if (msg.what == MsgType.msg_type_operatefriend) {
            //删除好友成功
            if (msg.arg1 == 5) {
                NetWorker.getFriendList();
            }
        } else if (msg.what == MsgType.msg_type_getfriendlist) {
            if (msg.arg1 != MsgType.ERROR_CODE_SUCCESS)
                return;

            //FriendList.setFriendList((FriendList) msg.obj);
            Toast.makeText(this, "删除好友成功", Toast.LENGTH_SHORT);
            finish();
        }
    }
}
