package org.hootina.platform.fragments;

import android.content.Intent;
import android.os.Handler;
import android.os.Message;
import android.support.v7.widget.LinearLayoutManager;
import android.support.v7.widget.RecyclerView;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageButton;
import android.widget.ImageView;
import android.widget.ListView;
import android.widget.RelativeLayout;
import android.widget.TextView;

import com.bumptech.glide.Glide;

import org.hootina.platform.R;
import org.hootina.platform.activities.details.FriendDetailInfoActivity;
import org.hootina.platform.activities.member.AddFriendActivity;
import org.hootina.platform.activities.member.GroupListActivity;
import org.hootina.platform.activities.member.NewFriendActivity;
import org.hootina.platform.enums.ClientType;
import org.hootina.platform.enums.MsgType;
import org.hootina.platform.net.NetWorker;
import org.hootina.platform.result.FileInfo;
import org.hootina.platform.result.FriendInfo;
import org.hootina.platform.userinfo.UserInfo;
import org.hootina.platform.userinfo.UserSession;
import org.hootina.platform.utils.HeadUtil;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

/*
 * 联系人
 */
public class FriendFragment extends BaseFragment {
    private ImageButton btn_add;
    private RelativeLayout rel_newfrd;
    private RelativeLayout rel_group;
    private ListView lv_friends;
    private FriendInfo friendInfo;
    private String picname;
    private File file;
    private byte[] contentIntleng;
    private int uAccountID;
    private List<FileInfo> m_downloadingFiles;
    private int mnLoad = 0;
    private TextView tv_friend_num;
    private RelativeLayout rl_newfriend;

    //private FriendList mFriendList;

    private List<UserInfo> friends = new ArrayList<>();

    private RecyclerView mRecyclerView;
    private FriendListAdapter mAdapter;

    public int getAccountID() {
        return uAccountID;
    }

    private MysHandler handler = new MysHandler();

    public class MysHandler extends Handler {
        @Override
        public void dispatchMessage(Message msg) {
            super.dispatchMessage(msg);
            switch (msg.what) {
                case 0:
                    break;
                case 1:
                    break;
            }
        }
    }

    @Override
    public void onClick(View v) {
        switch (v.getId()) {
            case R.id.btn_add:
                Intent intent = new Intent(getActivity(), AddFriendActivity.class);
                startActivityForResult(intent, 0);
                break;

            case R.id.rel_newfrd:
                Intent intent1 = new Intent(getActivity(), NewFriendActivity.class);
                // startActivityForResult(NewFriendActivity.class, 0);
                startActivityForResult(intent1, 0);
                break;

            case R.id.rel_group:
                startActivity(GroupListActivity.class);
                break;

            default:
                break;
        }
    }


    @Override
    public void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        // con.getFriends(1);
        //loadFriendList();
        //NetWorker.getFriendList();

        Log.d("zzh", "onActivityResult");
    }

    @Override
    public void onResume() {
        super.onResume();
        loadFriendList();
        mAdapter.notifyDataSetChanged();
    }

    public void setFrirndlistCount() {
        if (mnLoad == 0) {
            // con.getFriends(1);
        }
    }

    @Override
    public void onHiddenChanged(boolean hidden) {
        super.onHiddenChanged(hidden);
        //if (!hidden) {
        //    NetWorker.getFriendList();
        //}
    }

    private void loadFriendList() {
        List<UserInfo> storedFriendList = UserSession.getInstance().friends;
        friends = storedFriendList;
    }

    @Override
    protected int getContentView() {
        return R.layout.activity_friend;
    }


    @Override
    protected void initData(View view) {
        loadFriendList();

        mRecyclerView = (RecyclerView) view.findViewById(R.id.recycler);
        mAdapter = new FriendListAdapter();
        mRecyclerView.setLayoutManager(new LinearLayoutManager(getContext()));
        mRecyclerView.setAdapter(mAdapter);
    }

    @Override
    protected void setData() {
        btn_add.setOnClickListener(this);
        rel_group.setOnClickListener(this);
        rel_newfrd.setOnClickListener(this);
//		lv_friends.setOnItemClickListener(new OnItemClickListener() {
//			@Override
//			public void onItemClick(AdapterView<?> parent, View view, int position, long id) {
//				UserInfo currentUser = friendsAdapter.getList().get(position);
//				if (currentUser == null)
//					return;
//				Intent intent = new Intent(getActivity(), FriendDetailInfoActivity.class);
//				intent.putExtra("userid", currentUser.get_userid());
//				intent.putExtra("nickname", currentUser.get_nickname());
//				intent.putExtra("username", currentUser.get_username());
//				intent.putExtra("signature", currentUser.get_signature());
//				startActivityForResult(intent, 0);
//			}
//		});
    }

    @Override
    protected void processLogic() {
        Log.i("zhangyl test", "aaa");
    }

    @Override
    public void processMessage(Message msg) {
        super.processMessage(msg);

        if (msg.what == MsgType.msg_type_getfriendlist) {
			if (msg.arg1 != MsgType.ERROR_CODE_SUCCESS)
			    return;

			loadFriendList();
            //setFriendList((FriendList) msg.obj);
            mAdapter.notifyDataSetChanged();
        } else if (msg.what == MsgType.msg_type_operatefriend) {
            if (msg.arg1 == 3) {
                NetWorker.getFriendList();
            }
        } else if (msg.what == MsgType.msg_type_userstatuschange) {
            int userid = msg.arg1;
            int status = msg.arg2;
            //上线onlinestatus=1, 离线onlinestatus=0 2隐身 3隐身 4离开 5移动版在线 6移动版下线 7手机和电脑同时在线
            for (UserInfo item : friends) {
                if (item.get_userid() == userid) {
                    switch (status) {
                        case 0:
                            //item.setState("[离线]");
                        case 1:
                            //item.setState("[上线]");
                            break;
                        case 2:
                            //item.setState("[隐身]");
                            break;
                        case 3:
                            //item.setState("[隐身]");
                            break;
                        case 4:
                            //item.setState("[离开]");
                            break;
                        case 5:
                            //item.setState("[移动版在线]");
                            break;
                        case 6:
                            //item.setState("[移动版下线]");
                            break;
                        case 7:
                            //item.setState("[手机和电脑同时在线]");
                            break;
                    }
                    break;
                }
            }

            mAdapter.notifyDataSetChanged();
        }
    }

    public class FriendListAdapter extends RecyclerView.Adapter<FriendListAdapter.FriendViewHolder> {
        @Override
        public FriendViewHolder onCreateViewHolder(ViewGroup parent, int viewType) {
            View view = LayoutInflater.from(parent.getContext()).inflate(R.layout.friend_item, parent, false);
            return new FriendViewHolder(view);
        }

        @Override
        public void onBindViewHolder(FriendViewHolder holder, int position) {
            holder.bindDataToView(friends.get(position));
        }

        @Override
        public int getItemCount() {
            return friends.size();
        }

        public class FriendViewHolder extends RecyclerView.ViewHolder {

            private ImageView mImgHead;
            private TextView mTxtName;
            private TextView mTxtSign;

            public FriendViewHolder(View itemView) {
                super(itemView);

                mImgHead = (ImageView) itemView.findViewById(R.id.img_head);
                mTxtName = (TextView) itemView.findViewById(R.id.tv_window_title);
                mTxtSign = (TextView) itemView.findViewById(R.id.txt_sign);
            }

            public void bindDataToView(final UserInfo result) {

                Glide.with(FriendFragment.this)
                            .load("file:///android_asset/head" + result.get_faceType() + ".png")
                            .into(mImgHead);
//                String status = "";
////                switch (result.get_onlinetype()) {
////                    case OnlineType.online_type_offline:
////                    case OnlineType.online_type_pc_invisible:
////                        status = "[离线]";
////                        break;
////
////                    case OnlineType.online_type_pc_online:
////                        status = "[PC在线]";
////                        break;
////
////                    case OnlineType.online_type_android_cellular:
////                    case OnlineType.online_type_android_wifi:
////                            status = "[Android在线]";
////                        break;
////
////                    case OnlineType.online_type_ios:
////                        status = "[IOS在线]";
////                        break;
////
////                    case OnlineType.online_type_mac:
////                        status = "[Mac在线]";
////                        break;
////
////                    default:
////                        break;
////                }
                String status = "";
                switch (result.get_clientType()) {
                    case ClientType.CLIENT_TYPE_PC:
                        status = "[PC在线]";
                        break;

                    case ClientType.CLIENT_TYPE_ANDROID:
                        status = "[Android在线]";
                        break;

                    case ClientType.CLIENT_TYPE_IOS:
                        status = "[IOS在线]";
                        break;

                    default:
                        status = "[离线]";
                        break;
                }

//                //0离线 1在线 2忙碌 3离开 4隐身
//                switch (result.get_onlinetype()) {
//                    case 0:
//                        status = status + "离线]";
//                        break;
//                    case 1:
//                        status = status + "在线]";
//                        break;
//                    case 2:
//                        status = status + "忙碌]";
//                        break;
//                    case 3:
//                        status = status + "离开]";
//                        break;
//                    case 4:
//                        status = status + "隐身]";
//                        break;
//                }

                mTxtName.setText(result.get_username());
                mTxtSign.setText(status + result.get_signature());

                itemView.setOnClickListener(new View.OnClickListener() {
                    @Override
                    public void onClick(View v) {
                        Intent intent = new Intent(getActivity(), FriendDetailInfoActivity.class);
                        intent.putExtra("userid", result.get_userid());
                        intent.putExtra("nickname", result.get_nickname());
                        intent.putExtra("username", result.get_username());
                        intent.putExtra("signature", result.get_signature());
                        //getActivity().startActivity(intent);
                        startActivityForResult(intent, 0);
                    }
                });

                HeadUtil.put(result.get_userid(), result.get_faceType());
            }
        }
    }

}
