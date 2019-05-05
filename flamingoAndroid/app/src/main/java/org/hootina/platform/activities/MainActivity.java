package org.hootina.platform.activities;

import android.app.AlertDialog;
import android.content.DialogInterface;
import android.os.Bundle;
import android.os.Message;
import android.support.v4.app.Fragment;
import android.support.v4.app.FragmentManager;
import android.support.v4.app.FragmentTransaction;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.widget.ImageView;
import android.widget.RelativeLayout;
import android.widget.TextView;

import org.hootina.platform.R;
import org.hootina.platform.db.MyDbUtil;
import org.hootina.platform.enums.TabbarEnum;
import org.hootina.platform.fragments.AboutMeFragment;
import org.hootina.platform.fragments.FriendFragment;
import org.hootina.platform.fragments.SessionFragment;
import org.hootina.platform.net.ChatMsgMgr;
import org.hootina.platform.net.ChatSessionMgr;
import org.hootina.platform.net.NetWorker;
import org.hootina.platform.result.AppData;
import org.hootina.platform.utils.FaceConversionUtil;

public class MainActivity extends BaseActivity {
    private ImageView iv_message;
    private ImageView iv_friend;
    private ImageView iv_my;

    private FragmentManager fragmentManager = getSupportFragmentManager();
    private Fragment messageFragment;
    private Fragment friendFragment;
    private Fragment aboutMeFragment;

    private int uAccountID;

    private TextView tv_message_num;
    private TextView tv_friend_num;
    private RelativeLayout rl_message_nums;
    private RelativeLayout rl_friend_numss;
    private TabbarEnum mCurrentTabIndex = TabbarEnum.FRIEND;

    @Override
    protected void onResume() {
        super.onResume();

        NetWorker.clearSenderCount();
        NetWorker.clearNotificationCount();

        //TODO:将来做成保存用户选择记录
        // 默认显示主页
        if (ChatSessionMgr.getInstance().isEmpty()) {
            setTabSelection(TabbarEnum.FRIEND);
            application.setTabIndex(TabbarEnum.FRIEND);
        } else {
            TabbarEnum index = application.getTabIndex();
            setTabSelection(index);
        }

        updateNewMsgCount();
    }


    @Override
    protected void onCreate(Bundle bundle) {
        super.onCreate(bundle);
    }

    @Override
    protected void onPause() {
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        // 解决程序退出后，默认选中不是home的问题
        //application.setTabIndex(TabbarEnum.MESSAGE);
    }

    @Override
    public void onClick(View v) {
        switch (v.getId()) {
            case R.id.ll_message:
                setTabSelection(TabbarEnum.MESSAGE);
                // 调用消息数量的方法
                ((SessionFragment) messageFragment).refreshSessionList();
                break;

            case R.id.ll_friend:
                setTabSelection(TabbarEnum.FRIEND);
                break;

            case R.id.ll_my:
                setTabSelection(TabbarEnum.MY);
//			((AboutMeFragment) aboutMeFragment).update();
                break;

            default:
                break;
        }
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        super.onCreateOptionsMenu(menu);
        menu.add(R.string.exit_app_menu_item);
        // 如果希望显示菜单，请返回true
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        switch (item.getItemId()) {
            case 0:
                NetWorker.disconnectChatServer();
                application.getAppManager().finishAllActivity();
                break;

            default:
                return super.onOptionsItemSelected(item);
        }

        // 返回true表示处理完菜单项的事件，不需要将该事件继续传播下去了
        return true;
    }

    /**
     * 退出登录后调用
     */
    public void logout() {

        messageFragment = null;
        friendFragment = null;
        aboutMeFragment = null;
    }

    private void setTabSelection(TabbarEnum index) {
        // 开启一个Fragment事务
        FragmentTransaction transaction = fragmentManager.beginTransaction();
        // 清除选中状态
        clearSelection(transaction);

        switch (index) {
            case MESSAGE:
                // 消息
                iv_message.setImageResource(R.drawable.tab_weixin_pressed);
                if (messageFragment == null) {
                    messageFragment = new SessionFragment();
                    transaction.add(R.id.fl_content, messageFragment);
                } else {
                    transaction.show(messageFragment);
                }
                mCurrentTabIndex = TabbarEnum.MESSAGE;
                break;

            case FRIEND:
                // 好友列表
                iv_friend.setImageResource(R.drawable.tab_find_frd_pressed);
                iv_friend.refreshDrawableState();
                if (friendFragment == null) {
                    friendFragment = new FriendFragment();
                    //((FriendFragment)friendFragment).setFrirndlistCount();

                    transaction.add(R.id.fl_content, friendFragment);
                } else {
                    transaction.show(friendFragment);
                }
                mCurrentTabIndex = TabbarEnum.FRIEND;
                break;

            case MY:
                // 我
                iv_my.setImageResource(R.drawable.tab_me_pressed);
                if (aboutMeFragment == null) {
                    aboutMeFragment = new AboutMeFragment();
                    transaction.add(R.id.fl_content, aboutMeFragment);
                } else {
                    transaction.show(aboutMeFragment);
                }
                mCurrentTabIndex = TabbarEnum.MY;
                break;

            default:
                break;
        }

        transaction.commit();
        // 记录索引下标
        application.setTabIndex(index);
    }

    /**
     * 清除tab选中状态
     */
    private void clearSelection(FragmentTransaction transaction) {
        iv_message.setImageResource(R.drawable.tab_weixin_normal);
        if (messageFragment != null) {
            transaction.hide(messageFragment);
        }

        iv_friend.setImageResource(R.drawable.tab_find_frd_normal);
        if (friendFragment != null) {
            transaction.hide(friendFragment);
        }

        iv_my.setImageResource(R.drawable.tab_me_normal);
        if (aboutMeFragment != null) {
            transaction.hide(aboutMeFragment);
        }
    }

    @Override
    protected int getContentView() {
        return R.layout.activity_main;
    }

    @Override
    protected void initData() {
        new Thread(new Runnable() {
            @Override
            public void run() {
                if (application != null) {
                    FaceConversionUtil.getInstace().getFileText(application);
                }
            }
        }).start();
    }

    @Override
    protected void setData() {
        //if (application.getMemberEntity() != null) {
        //	uAccountID = application.getMemberEntity().getmSelfID();

        // 从数据库中加载好友列表
        //uAccountID = UserSession.getInstance().loginUser.get_userid();
        //	AppData.Init(uAccountID, db);
//			List<FriendInfo> list = AppData.getFriendList(uAccountID);
//			if (list != null) {
//				for (int i = 0; i < list.size(); ++i) {
//					FriendInfo fff = list.get(i);
//					fff.setHeadpath(fff.getHeadpath());
//				}
//			}


        //}

    }

    public void updateNewMsgCount() {
        // 显示未处理的好友请求数量
        int nWaitFriend = AppData.getFriendApplyWaiting(uAccountID);
        if (nWaitFriend == 0) {
            rl_friend_numss.setVisibility(View.GONE);
        } else {
            rl_friend_numss.setVisibility(View.VISIBLE);
            String strText = "..";
            if (nWaitFriend < 100) {
                strText = String.valueOf(nWaitFriend);
            }
            tv_friend_num.setText(strText);
        }

        // 显示未读消息数量
        int nUnreadCount = ChatMsgMgr.getInstance().getTotalUnreadChatMsgCount();
        if (nUnreadCount == 0) {
            rl_message_nums.setVisibility(View.GONE);
        } else {
            rl_message_nums.setVisibility(View.VISIBLE);
            String strText = String.valueOf(nUnreadCount);
            tv_message_num.setText(strText);
        }
    }

    @Override
    public void processMessage(Message msg) {
        super.processMessage(msg);
        if (messageFragment != null)
            ((SessionFragment) messageFragment).processMessage(msg);

        if (friendFragment != null)
            ((FriendFragment) friendFragment).processMessage(msg);

        if (aboutMeFragment != null)
            ((AboutMeFragment) aboutMeFragment).processMessage(msg);

        updateNewMsgCount();
    }

    @Override
    public void onBackPressed() {
        new AlertDialog.Builder(MainActivity.this)
                                .setTitle("提示")
                                .setMessage("退出Flamingo吗？")
                                .setPositiveButton("确定",  new DialogInterface.OnClickListener() {
                                    public void onClick(DialogInterface dialog, int which) {
                                        dialog.dismiss();
                                        exitApp();
                                        //super.onBackPressed();
                                    }
                                })
                                .setNegativeButton("取消", new DialogInterface.OnClickListener() {
                                    public void onClick(DialogInterface dialog, int which) {
                                        dialog.dismiss();
                                    }
                                })
                                .show();
    }

    private void exitApp() {
        //断开网络
        NetWorker.uninit();
        MyDbUtil.uninit();
        application.getAppManager().finishAllActivity();
    }
}
